import ctypes
import json
from pathlib import Path
import shutil
import subprocess
import sys
import uuid

probe=Path(sys.argv[1]).resolve()
root=Path(sys.argv[2]).resolve()/uuid.uuid4().hex/'人工 更新'
program=root/'程序 空格'
program.mkdir(parents=True)
child=program/probe.name
shutil.copy2(probe,child)
results={}
for mode,count in [('ok',8),('parallel',1),('mismatch',1),('bad_nonce',1),('bad_cli',1),('foreign_parent',1),('die',1),('cancel',8),('hang',1),('parent_death',1)]:
    directory=root/mode
    completed=subprocess.run([str(probe),str(child),str(directory),mode,str(count)],capture_output=True,text=True,timeout=20)
    assert completed.returncode==0,(mode,completed.returncode,completed.stdout,completed.stderr)
    if mode=='parent_death':
        pid=int((directory/'child_id.txt').read_text())
        kernel=ctypes.WinDLL('kernel32',use_last_error=True)
        kernel.OpenProcess.restype=ctypes.c_void_p
        kernel.OpenProcess.argtypes=[ctypes.c_uint32,ctypes.c_int,ctypes.c_uint32]
        kernel.WaitForSingleObject.argtypes=[ctypes.c_void_p,ctypes.c_uint32]
        kernel.CloseHandle.argtypes=[ctypes.c_void_p]
        handle=kernel.OpenProcess(0x00100000,False,pid)
        if handle:
            try: assert kernel.WaitForSingleObject(handle,3000)==0
            finally: kernel.CloseHandle(handle)
        results[mode]={'owned_child_gone_after_parent_exit':True}
    else:
        results[mode]=json.loads(completed.stdout)
        assert results[mode]['passed']
        assert results[mode]['forced_owned_jobs']==(1 if mode=='hang' else 0)
    for metadata in directory.glob('*.json'):
        value=json.loads(metadata.read_text())
        assert value['readonly_request'] and value['wait_only_cancel'] and value['extra_not_inherited']
        assert value['parent_id']>0 and value['version'] in {'1.2.3.7','1.2.3.8'}
        if mode!='parent_death': assert value['parent_id']==results[mode]['parent_id']
    if mode!='parent_death': assert not list(directory.glob('*.pending'))
(root.parent/'results.json').write_text(json.dumps(results,indent=2),encoding='utf-8')
print(json.dumps(results))

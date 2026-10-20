from pathlib import Path
import subprocess
import sys
import zipfile

exe,root=sys.argv[1],Path(sys.argv[2])
subprocess.run([exe,str(root)],check=True)
with zipfile.ZipFile(root/'result.zip') as archive:
    assert archive.testzip() is None
    assert set(archive.namelist())=={'日志.txt','nested/empty.txt'}
    assert archive.read('日志.txt')==(root/'日志.txt').read_bytes()
    assert archive.read('nested/empty.txt')==b''
    info=archive.getinfo('日志.txt')
    assert info.flag_bits & 0x800
    assert info.compress_type==zipfile.ZIP_DEFLATED
    assert info.compress_size<info.file_size
for name in ('missing.zip','escape.zip','duplicate.zip'):assert not (root/name).exists()
print('ZIP Unicode, compression, CRC, empty file, failure cleanup and preservation verified')

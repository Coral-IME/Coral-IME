from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
from pathlib import Path
import subprocess
import sys
import threading
import time
import uuid

probe=Path(sys.argv[1]).resolve()
root=Path(sys.argv[2]).resolve()/uuid.uuid4().hex/'人工 HTTP'
root.mkdir(parents=True)
counts={}
class Handler(BaseHTTPRequestHandler):
    def log_message(self,*args): pass
    def do_GET(self):
        counts[self.path]=counts.get(self.path,0)+1
        if self.path=='/redirect':
            self.send_response(302); self.send_header('Location','/file'); self.send_header('Content-Length','0'); self.end_headers(); return
        data={'/metadata':b'{"data":{}}','/limit':b'x'*65536,'/oversize':b'x'*65537,
              '/file':b'abc','/oversize-file':b'abcd','/short':b'ab','/stall':b'abc'}[self.path]
        self.send_response(200); self.send_header('Content-Length',str(len(data))); self.end_headers()
        if self.path=='/stall': time.sleep(2)
        try: self.wfile.write(data)
        except (BrokenPipeError,ConnectionResetError,ConnectionAbortedError): pass
server=ThreadingHTTPServer(('127.0.0.1',0),Handler)
server.daemon_threads=False
thread=threading.Thread(target=server.serve_forever)
thread.start()
results={}
try:
    base=f'http://127.0.0.1:{server.server_port}'
    cases=[('get','/metadata',0,0),('get','/limit',0,0),('get','/oversize',0,5),
           ('save','/file',3,0),('save','/oversize-file',3,5),('save','/short',3,4),
           ('get','/redirect',0,4),('cancel','/stall',0,1),('timeout','/stall',0,11),
           ('production','/metadata',0,2),('callback','/metadata',0,0)]
    for index,(mode,path,limit,expected) in enumerate(cases):
        file=root/f'{index}.bin'
        old_file_count=counts.get('/file',0)
        completed=subprocess.run([str(probe),mode,base+path,str(file),str(limit)],capture_output=True,text=True,timeout=10)
        assert completed.returncode==0,(mode,path,completed.stdout,completed.stderr)
        value=json.loads(completed.stdout)
        assert value['error']==expected,(mode,path,value)
        if mode=='save':
            assert file.stat().st_size<=limit
            if expected==0: assert file.read_bytes()==b'abc'
        else: assert value['body_size']<=65536
        if mode in ('cancel','timeout'): assert value['elapsed_ms']<1500,value
        if path=='/redirect': assert counts.get('/file',0)==old_file_count
        results[f'{mode}{path}']=value
    assert counts['/metadata']==2  # get + 回调测试；生产门控与仅 HTTPS 回调不会发送 HTTP。
finally:
    server.shutdown(); thread.join(); server.server_close()
(root.parent/'results.json').write_text(json.dumps(results,indent=2),encoding='utf-8')
print(json.dumps(results))

from pathlib import Path
import subprocess
import sys
import uuid

cmake,script=sys.argv[1:3]
root=Path(sys.argv[3]).resolve()/uuid.uuid4().hex
root.mkdir(parents=True)
valid=[('', ''),('https://new.example.test','https://new.example.test'),
       ('https://new.example.test:00080/catalog///','https://new.example.test:00080/catalog'),
       ('https://new.example.test:65535/a_~/','https://new.example.test:65535/a_~')]
invalid=['http://new.example.test','https://','https://u:p@new.example.test','https://new.example.test?x=1',
         'https://new.example.test/#fragment','https://new.example.test/a b','https://new.example.test/a;b',
         'https://new.example.test/"','https://new.example.test:0','https://new.example.test:65536',
         'https://new.example.test:-1','https://[::1]','https://新.example.test','https://new.example.test/\\x','x'*8193]
for index,(base,expected) in enumerate(valid+[(value,None) for value in invalid]):
    output=root/f'{index}.txt'
    result=subprocess.run([cmake,f'-DINPUT_BASE={base}',f'-DOUTPUT_FILE={output}','-P',script],capture_output=True,text=True,timeout=5)
    if expected is None:
        assert result.returncode!=0,(base,result.stdout,result.stderr)
        assert not output.exists()
    else:
        assert result.returncode==0,(base,result.stdout,result.stderr)
        assert output.read_text()==expected
print('Actual CMake validator accepted 4 bases and rejected 15 invalid bases without networking')

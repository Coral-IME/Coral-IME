import os
import subprocess
import sys
import uuid

token=uuid.uuid4().hex
children=[]
try:
    for label in ('a','b'):
        env=os.environ.copy();env['CORAL_TEST_NAMESPACE']=token+label
        child=subprocess.Popen([sys.argv[1],label],env=env,stdin=subprocess.PIPE,stdout=subprocess.PIPE,text=True)
        children.append(child)
        assert child.stdout.readline().strip()=='READY'
    # 第二个对端写入其值时，两个对端都持有共享内存句柄。
    for child in children:
        child.stdin.write('check\n');child.stdin.flush()
        assert child.stdout.readline().strip()=='UNCHANGED'
        assert child.wait(timeout=10)==0
finally:
    for child in children:
        if child.poll() is None:child.terminate();child.wait(timeout=10)
print('Concurrent configuration namespaces kept independent values')

"""Launch one engine test with a unique configuration/model and user-data scope."""
from pathlib import Path
import argparse
import os
import subprocess
import uuid

def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--user-root',type=Path,required=True)
    parser.add_argument('--data-dir',type=Path)
    parser.add_argument('command',nargs=argparse.REMAINDER)
    args=parser.parse_args()
    command=args.command[1:] if args.command[:1]==['--'] else args.command
    if not command: parser.error('A child command is required')
    token=uuid.uuid4().hex
    root=args.user_root.resolve()
    user=root/token
    user.mkdir(parents=True,exist_ok=False)
    env=os.environ.copy()
    env['CORAL_TEST_NAMESPACE']=token
    env['CORAL_USER_DATA_DIR']=str(user)
    if args.data_dir:
        env['CORAL_LANGENGINE_DATA_DIR']=str(args.data_dir.resolve())
    command=[str(user) if value=='@USER_DIR@' else value for value in command]
    return subprocess.run(command,env=env,check=False).returncode

if __name__=='__main__': raise SystemExit(main())

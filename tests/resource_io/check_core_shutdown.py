"""Run each core lifetime in a fresh isolated process and require normal exit."""
from pathlib import Path
import os
import subprocess
import sys
import uuid

def main():
    executable, runtime, root = (Path(value).resolve() for value in sys.argv[1:])
    for scenario in ('sync', 'async', 'failure'):
        user = root / (scenario + '-' + uuid.uuid4().hex)
        user.mkdir(parents=True)
        env = os.environ.copy()
        env.update(CORAL_TEST_NAMESPACE=uuid.uuid4().hex, CORAL_USER_DATA_DIR=str(user), CORAL_LANGENGINE_DATA_DIR=str(runtime))
        result = subprocess.run([str(executable), str(runtime), str(user), scenario], env=env, timeout=10)
        if result.returncode:
            return result.returncode
    return 0

if __name__ == '__main__':
    raise SystemExit(main())

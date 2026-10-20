"""The default agent must exit before network, product mutex, log or IPC startup."""
import subprocess
import sys

result = subprocess.run([sys.argv[1]], capture_output=True, timeout=10)
assert result.returncode == 2, (result.returncode, result.stdout, result.stderr)
print('Unconfigured update agent exits with explicit disabled status')

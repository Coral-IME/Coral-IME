"""以独立用户目录验证正式资源，只输出可公开的测试状态。"""

import argparse
import json
import os
from pathlib import Path
import subprocess
import uuid

from official_data import digest


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binaries', type=Path, required=True)
    parser.add_argument('--runtime-dir', type=Path, required=True)
    parser.add_argument('--output-dir', type=Path, required=True)
    args = parser.parse_args()
    runtime = args.runtime_dir.resolve()
    binaries = args.binaries.resolve()
    output = args.output_dir.resolve()
    if output == runtime or output.is_relative_to(runtime) or runtime.is_relative_to(output):
        parser.error('Test output and runtime must not overlap')
    checks = {
        'sequence_test': ['@USER_DIR@'],
        'lexicon_import_engine_test': [str(runtime)],
        'uvmode_engine_test': [str(runtime), '@USER_DIR@'],
        'config_gate_test': [str(runtime)],
    }
    for name in checks:
        if not (binaries / (name + '.exe')).is_file():
            parser.error('Missing formal-data test executable: ' + name)
    output.mkdir(parents=True, exist_ok=False)
    results = []
    for name, parameters in checks.items():
        token = uuid.uuid4().hex
        user = output / 'users' / token
        user.mkdir(parents=True, exist_ok=False)
        env = os.environ.copy()
        env['CORAL_TEST_NAMESPACE'] = token
        env['CORAL_USER_DATA_DIR'] = str(user)
        env['CORAL_LANGENGINE_DATA_DIR'] = str(runtime)
        command = [str(binaries / (name + '.exe')),
                   *(str(user) if value == '@USER_DIR@' else value for value in parameters)]
        with (output / (name + '.log')).open('wb') as stream:
            try:
                result = subprocess.run(
                    command, env=env, stdout=stream, stderr=subprocess.STDOUT, timeout=120, check=False)
                status = {'name': name, 'exit_code': result.returncode, 'passed': result.returncode == 0}
            except subprocess.TimeoutExpired:
                status = {'name': name, 'exit_code': None, 'passed': False, 'timed_out': True}
        results.append(status)
        print(name + ': ' + ('PASS' if status['passed'] else 'FAIL'), flush=True)
    summary = {'format_version': 1, 'passed': all(item['passed'] for item in results),
               'runtime_manifest_sha256': digest(runtime / 'runtime-manifest.json'), 'tests': results}
    (output / 'summary.json').write_text(json.dumps(summary, indent=2) + '\n', encoding='utf-8')
    return 0 if summary['passed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())

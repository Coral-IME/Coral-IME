"""以独立用户目录验证正式资源，只输出可公开的测试状态。"""

import argparse
import json
import math
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
    parser.add_argument('--require-character-model', action='store_true')
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
    if args.require_character_model:
        model = runtime / 'data/generallexicon/character_model.coral'
        probe = binaries / 'coral_character_probe.exe'
        from official_data import character_record
        model_record = character_record(model)
        if model_record['precision'] != 'int8':
            raise ValueError('The release requires INT8 character weights')
        result = subprocess.run([str(probe), str(model), '--batch'],
                                input='今天\t可以写了\n我\t喜欢打字\n'.encode('utf-8'),
                                capture_output=True, timeout=120, check=False)
        (output / 'character_model_test.log').write_bytes(result.stdout + result.stderr)
        try:
            scores = [float(line) for line in result.stdout.decode('utf-8').splitlines()]
            passed = result.returncode == 0 and len(scores) == 2 and all(math.isfinite(score) for score in scores)
        except (ValueError, UnicodeError):
            passed = False
        results.append({'name': 'character_model_test', 'exit_code': result.returncode, 'passed': passed})
        print('character_model_test: ' + ('PASS' if passed else 'FAIL'), flush=True)
    summary = {'format_version': 1, 'passed': all(item['passed'] for item in results),
               'runtime_manifest_sha256': digest(runtime / 'runtime-manifest.json'), 'tests': results}
    if args.require_character_model:
        summary['character_model'] = model_record
    (output / 'summary.json').write_text(json.dumps(summary, indent=2) + '\n', encoding='utf-8')
    return 0 if summary['passed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())

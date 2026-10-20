"""Exercise the actual host executable and independent client processes."""
from pathlib import Path
import json
import os
import queue
import re
import shutil
import subprocess
import sys
import threading
import time
import uuid


def main():
    host, probe, runtime, root = (Path(value).resolve() for value in sys.argv[1:])
    scope = root / uuid.uuid4().hex
    user = scope / 'user'
    user.mkdir(parents=True)
    env = os.environ.copy()
    env.update(CORAL_TEST_NAMESPACE=scope.name, CORAL_USER_DATA_DIR=str(user), CORAL_LANGENGINE_DATA_DIR=str(runtime))
    processes = []
    results = {'scenarios': [], 'normal_stop_ms': [], 'forced_cleanup': False}
    flags = subprocess.CREATE_NO_WINDOW

    def spawn(executable, *args, environment=env):
        process = subprocess.Popen([str(executable), *args], env=environment, stdin=subprocess.PIPE,
                                   stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True,
                                   encoding='utf-8', creationflags=flags)
        processes.append(process)
        return process

    def check(mode, *args, environment=env, timeout=12):
        result = subprocess.run([str(probe), mode, *args], env=environment, capture_output=True,
                                text=True, encoding='utf-8', timeout=timeout, creationflags=flags)
        if result.returncode:
            raise RuntimeError(f'{mode}: {result.returncode}: {result.stdout} {result.stderr}')
        return result

    def read_line(process):
        messages = queue.Queue()
        threading.Thread(target=lambda: messages.put(process.stdout.readline()), daemon=True).start()
        line = messages.get(timeout=8).strip()
        if not line:
            raise RuntimeError(f'Probe failed before readiness: {process.communicate(timeout=3)}')
        return line

    def release(process):
        output, error = process.communicate('continue\n', timeout=8)
        if process.returncode:
            raise RuntimeError(f'Probe exit {process.returncode}: {output} {error}')

    def start():
        process = spawn(host)
        deadline = time.monotonic() + 10
        while time.monotonic() < deadline:
            if process.poll() is not None:
                raise RuntimeError(f'Host failed at startup: {process.returncode}: {process.communicate()}')
            try:
                check('ping', timeout=4)
                return process
            except RuntimeError:
                time.sleep(0.05)
        raise TimeoutError('Actual host readiness timed out')

    def stop(process):
        started = time.monotonic()
        check('stop', timeout=4)
        output, error = process.communicate(timeout=5)
        results['normal_stop_ms'].append(round((time.monotonic() - started) * 1000, 1))
        if process.returncode:
            raise RuntimeError(f'Host normal stop failed: {process.returncode}: {output} {error}')

    try:
        check('bootstrap', timeout=20)
        results['scenarios'].append('native bootstrap launches adjacent real host without registry/service; installation gate; zero-budget warm readiness; normal process exit')
        current = start()
        duplicate = spawn(host)
        duplicate.communicate(timeout=5)
        if duplicate.returncode != 0:
            raise RuntimeError('Duplicate host did not leave the active instance intact')
        check('basic')
        results['scenarios'].append('actual query, settings, malformed frame, client owner, generations, deadline, singleton')
        concurrent = [spawn(probe, 'basic') for _ in range(4)]
        for peer in concurrent:
            output, error = peer.communicate(timeout=8)
            if peer.returncode:
                raise RuntimeError(f'Concurrent peer failed: {peer.returncode}: {output} {error}')
        results['scenarios'].append('four independent client processes query concurrently')
        check('release', timeout=15)
        results['scenarios'].append('six port replacements release 64 sessions each while the client process stays alive')
        owner = spawn(probe, 'owner')
        identities = read_line(owner).split()
        if len(identities) != 3:
            raise RuntimeError(f'Unexpected owner identities: {identities}')
        check('attack', *identities)
        release(owner)
        check('capacity')
        results['scenarios'].append('cross-process forged client ID, owner remains usable, dead-process reaping, 64/256 session caps')
        check('timeout')
        results['scenarios'].append('eight idle clients expire and host becomes available')
        reconnect = spawn(probe, 'reconnect')
        if read_line(reconnect) != 'READY':
            raise RuntimeError('Reconnect probe readiness')
        stop(current)
        current = start()
        release(reconnect)
        results['scenarios'].append('same client survives direct host replacement, old session stays invalid, fresh query')
        bridge = spawn(probe, 'bridge-reconnect')
        if read_line(bridge) != 'READY':
            raise RuntimeError('Session bridge readiness')
        stop(current)
        current = start()
        release(bridge)
        results['scenarios'].append('text service bridge retains two logical sessions and restores distinct settings across actual host replacement; stale revision refused; destructor releases lease')
        for mode in ('idle', 'unread'):
            peer = spawn(probe, mode)
            if read_line(peer) != 'READY':
                raise RuntimeError(f'{mode} readiness')
            stop(current)  # 对端保持打开，直到真实宿主退出。
            release(peer)
            results['scenarios'].append(f'normal stop while eight {mode} client handles remain open')
            current = start()
        stop(current)
        results['scenarios'].append('normal stop while all workers await connection')

        bad_runtime = scope / 'bad-runtime'
        shutil.copytree(runtime, bad_runtime)
        config = bad_runtime / 'config/coralpinyinConfig.ini'
        text, count = re.subn(r'(?m)^CorrectionData\s*=\s*[^\r\n]*', 'CorrectionData = incorrect-checksum', config.read_bytes().decode('utf-8'))
        if count != 1:
            raise ValueError('Expected one artificial correction checksum')
        config.write_bytes(text.encode('utf-8'))
        bad_env = env | {'CORAL_LANGENGINE_DATA_DIR': str(bad_runtime),
                         'CORAL_USER_DATA_DIR': str(scope / 'bad-user'),
                         'CORAL_TEST_NAMESPACE': scope.name + 'bad'}
        broken = spawn(host, environment=bad_env)
        broken.communicate(timeout=8)
        if broken.returncode != 2:
            raise RuntimeError(f'Failed model startup did not exit normally with 2: {broken.returncode}')
        repaired = spawn(host, environment=bad_env | {'CORAL_LANGENGINE_DATA_DIR': str(runtime),
                                                     'CORAL_USER_DATA_DIR': str(scope / 'repaired-user')})
        check('ping', environment=bad_env | {'CORAL_USER_DATA_DIR': str(scope / 'repaired-user')}, timeout=4)
        check('stop', environment=bad_env, timeout=4)
        repaired.communicate(timeout=5)
        if repaired.returncode != 0:
            raise RuntimeError('Host cannot restart after failed startup')
        results['scenarios'].append('failed initialization normal exit and repaired host startup/stop')
        results['passed'] = True
        print(json.dumps(results, ensure_ascii=False))
        return 0
    finally:
        for process in processes:
            if process.poll() is None:
                # 仅用于失败清理：强制杀死绝不算成功。
                results['forced_cleanup'] = True
                process.kill()
                process.communicate(timeout=5)
        (scope / 'results.json').write_text(json.dumps(results, ensure_ascii=False, indent=2), 'utf-8')


if __name__ == '__main__':
    raise SystemExit(main())

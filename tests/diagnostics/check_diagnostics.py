"""Only isolated laboratory processes are faulted or captured; never installed applications."""
import argparse
import ctypes
import json
import os
from pathlib import Path
import struct
import shutil
import subprocess
import time
import uuid
import zipfile


def verify_report(directory, exception, pid=None):
    with zipfile.ZipFile(directory / 'report.zip') as archive:
        assert set(archive.namelist()) == {'process.dmp', 'report.json'}
        assert archive.testzip() is None
        for name in archive.namelist():
            assert archive.read(name) == (directory / name).read_bytes()
        manifest = json.loads(archive.read('report.json'))
        dump = archive.read('process.dmp')
    assert manifest['module'] == 'settings' and manifest['format_version'] == 1
    assert manifest['exception_capture'] == exception
    if pid is not None:
        assert manifest['process_id'] == pid
    assert dump[:4] == b'MDMP'
    count, table = struct.unpack_from('<II', dump, 8)
    streams = {struct.unpack_from('<III', dump, table + i * 12)[0]:
               struct.unpack_from('<III', dump, table + i * 12)[1:] for i in range(count)}
    if exception:
        size, offset = streams[6]
        assert size >= 168
        assert struct.unpack_from('<I', dump, offset)[0] == manifest['thread_id']
        assert struct.unpack_from('<I', dump, offset + 8)[0] == 0xE04C0101
    else:
        assert 6 not in streams


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--probe', required=True)
    parser.add_argument('--collector', required=True)
    parser.add_argument('--root', required=True)
    args = parser.parse_args()
    root = Path(args.root).resolve() / uuid.uuid4().hex / '人工 诊断'
    root.mkdir(parents=True)
    sentinel = root / 'existing.txt'
    sentinel.write_text('keep', encoding='utf-8')
    env = dict(os.environ, CORAL_DIAGNOSTIC_OUTPUT_DIR=str(root))
    def run(command, expected):
        completed = subprocess.run(command, env=env, timeout=12, capture_output=True)
        assert completed.returncode == expected, (command, completed.returncode, completed.stderr)
    def directories():
        return set(p for p in root.iterdir() if p.is_dir())
    scenarios = []
    collector = str(Path(args.collector).resolve())
    probe = str(Path(args.probe).resolve())
    malformed = [[], ['-pid=123', '-tid=0', '-exp=0', '-module=coralpinyin'],
                 ['--coral-crash=2', '--pid=1', '--tid=0', '--exception=0', '--module=settings'],
                 ['--coral-crash=1', '--pid=1', '--tid=0', '--exception=0', '--module=../settings']]
    for fields in malformed:
        run([collector, *fields], 2)
    assert not directories()
    run([probe, 'missing', str(root / 'missing.exe')], 0)
    run([probe, 'twice', collector], 0)
    scenarios.append('invalid requests create no output; absent collector preserves existing filter; second install rejected')
    # 对同一实验进程的两个并发捕获必须保留不同的报告。
    with subprocess.Popen([probe, 'snapshot'], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                          text=True, env=env) as target:
        try:
            pid = int(target.stdout.readline())
            fields = ['--coral-crash=1', f'--pid={pid}', '--tid=0', '--exception=0', '--module=settings']
            children = [subprocess.Popen([collector, *fields], env=env) for _ in range(2)]
            for child in children:
                assert child.wait(timeout=12) == 0
            assert len(directories()) == 2
            for directory in directories():
                verify_report(directory, False, pid)
            before = directories()
            for output in ('relative-output', str(sentinel)):
                env['CORAL_DIAGNOSTIC_OUTPUT_DIR'] = output
                run([collector, *fields], 4)
                assert directories() == before
            env['CORAL_DIAGNOSTIC_OUTPUT_DIR'] = str(root)
            # 非法的远端异常地址显式失败，且不发布压缩包。
            before = directories()
            run([collector, '--coral-crash=1', f'--pid={pid}', '--tid=1', '--exception=1', '--module=settings'], 3)
            assert all(not (p / 'report.zip').exists() for p in directories() - before)
        finally:
            target.communicate('\n', timeout=5)
    scenarios.append('real concurrent snapshots with unique directories; invalid exception address does not become snapshot success')
    program_directory = root / '实验 程序'
    program_directory.mkdir()
    copied_probe = program_directory / Path(probe).name
    copied_collector = program_directory / Path(collector).name
    shutil.copy2(probe, copied_probe)
    shutil.copy2(collector, copied_collector)
    for mode in ('adjacent', 'worker'):
        before = directories()
        run([str(copied_probe), mode, str(copied_collector)], 1)
        added = directories() - before
        assert len(added) == 1
        verify_report(added.pop(), True)
    scenarios.append('real unhandled main/worker faults from Chinese/space executable paths produce ZIP with matching exception code/thread')
    # 无法启动的常规文件用于演练 CreateProcess 失败。
    invalid_exe = root / 'invalid.exe'
    invalid_exe.write_text('not a PE executable', encoding='utf-8')
    before = directories()
    run([probe, 'launch_failure', str(invalid_exe)], 1)
    assert before == directories()
    marker = root / 'slow_pid.txt'
    env['CORAL_DIAGNOSTIC_PROBE_MARKER'] = str(marker)
    started = time.monotonic()
    run([probe, 'slow', probe], 1)
    elapsed = time.monotonic() - started
    assert elapsed < 1.8, elapsed
    deadline = time.monotonic() + 4
    while not marker.exists() and time.monotonic() < deadline:
        time.sleep(0.02)
    pid = int(marker.read_text())
    kernel = ctypes.WinDLL('kernel32', use_last_error=True)
    kernel.OpenProcess.restype = ctypes.c_void_p
    kernel.OpenProcess.argtypes = [ctypes.c_uint32, ctypes.c_int, ctypes.c_uint32]
    kernel.WaitForSingleObject.argtypes = [ctypes.c_void_p, ctypes.c_uint32]
    kernel.CloseHandle.argtypes = [ctypes.c_void_p]
    handle = kernel.OpenProcess(0x00100000, False, pid)
    if handle:
        try:
            assert kernel.WaitForSingleObject(handle, 4000) == 0
        finally:
            kernel.CloseHandle(handle)
    assert sentinel.read_text(encoding='utf-8') == 'keep'
    scenarios.append('failed launch exits; slow collector wait bounded and isolated child exits naturally; existing files retained')
    result = {'passed': True, 'forced_cleanup': False, 'slow_wait_ms': round(elapsed * 1000), 'scenarios': scenarios}
    (root.parent / 'results.json').write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding='utf-8')
    print(json.dumps(result, ensure_ascii=False))


if __name__ == '__main__':
    main()

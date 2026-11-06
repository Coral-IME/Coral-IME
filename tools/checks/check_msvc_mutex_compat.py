"""使用固定的旧版 Microsoft 运行库检查实际编译的 mutex 初始化行为。"""

import argparse
import hashlib
import json
import pathlib
import shutil
import subprocess
import tempfile
import urllib.request


RUNTIME_URL = (
    "https://msdl.microsoft.com/download/symbols/"
    "MSVCP140.dll/5E74AEB691000/MSVCP140.dll"
)
RUNTIME_SHA256 = "88f55d86b50b0a7e55e71ad2d8f7552146ba26e927230daf2e26ad3a971973c5"
RUNTIME_SIZE = 590112


def run_probe(executable, runtime, directory):
    directory.mkdir()
    staged_executable = directory / executable.name
    staged_runtime = directory / "MSVCP140.dll"
    shutil.copy2(executable, staged_executable)
    shutil.copy2(runtime, staged_runtime)
    result = subprocess.run(
        [str(staged_executable), str(staged_runtime)],
        cwd=directory,
        capture_output=True,
        text=True,
        errors="replace",
        timeout=15,
    )
    return {
        "returncode": result.returncode,
        "exit_code_hex": f"0x{result.returncode & 0xffffffff:08x}",
        "stdout": result.stdout,
        "stderr": result.stderr,
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--probe", type=pathlib.Path, required=True)
    parser.add_argument("--control", type=pathlib.Path)
    parser.add_argument("--runtime", type=pathlib.Path)
    parser.add_argument("--work-dir", type=pathlib.Path, required=True)
    args = parser.parse_args()
    work_dir = args.work_dir.resolve()
    work_dir.mkdir(parents=True, exist_ok=True)
    report = {"runtime_sha256": RUNTIME_SHA256}
    with tempfile.TemporaryDirectory(prefix="mutex-", dir=work_dir) as temporary:
        temporary = pathlib.Path(temporary)
        runtime = temporary / "MSVCP140.dll"
        if args.runtime:
            payload = args.runtime.read_bytes()
        else:
            with urllib.request.urlopen(RUNTIME_URL, timeout=30) as response:
                payload = response.read(RUNTIME_SIZE + 1)
        if len(payload) != RUNTIME_SIZE or hashlib.sha256(payload).hexdigest() != RUNTIME_SHA256:
            raise RuntimeError("旧运行库大小或 SHA-256 不匹配")
        runtime.write_bytes(payload)
        report["compatible"] = run_probe(args.probe.resolve(), runtime, temporary / "compatible")
        if args.control:
            report["control"] = run_probe(args.control.resolve(), runtime, temporary / "control")
    (work_dir / "report.json").write_text(
        json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8"
    )
    print(json.dumps(report, ensure_ascii=False, indent=2))
    if report["compatible"]["returncode"] != 0:
        raise RuntimeError("兼容构建在旧运行库下执行失败")
    if args.control and report["control"]["exit_code_hex"] != "0xc0000005":
        raise RuntimeError("未启用兼容构造的对照未产生预期的访问异常")


if __name__ == "__main__":
    main()

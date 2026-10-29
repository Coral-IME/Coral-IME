"""限定小样本调用生产语音探针，记录转写、延迟与去标点字符错误率。"""
import argparse
import json
import random
import subprocess
import time
import unicodedata
import wave
from pathlib import Path

def normalize(text):
    return ''.join(c.lower() for c in unicodedata.normalize('NFKC', text)
                   if not c.isspace() and not unicodedata.category(c).startswith(('P', 'Z')))

def distance(a, b):
    row = list(range(len(b) + 1))
    for i, x in enumerate(a, 1):
        current = [i]
        for j, y in enumerate(b, 1):
            current.append(min(current[-1] + 1, row[j] + 1, row[j - 1] + (x != y)))
        row = current
    return row[-1]

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--probe', type=Path, required=True)
    parser.add_argument('--manifest', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--count', type=int, default=15, choices=range(3, 21))
    parser.add_argument('--seed', type=int, default=20261028)
    args = parser.parse_args()
    if args.output.exists():
        parser.error('输出文件已存在，请指定新的文件名以保留原始证据')
    entries = [json.loads(line) for line in args.manifest.read_text(encoding='utf-8').splitlines() if line.strip()]
    ordered = sorted(entries, key=lambda e: e['duration_s'])
    selected = [ordered[0], ordered[-1]] + random.Random(args.seed).sample(ordered[1:-1], args.count - 2)
    rows = []
    for entry in selected:
        audio = args.manifest.parent / entry['audio']
        with wave.open(str(audio), 'rb') as wav:
            if (wav.getframerate(), wav.getnchannels(), wav.getsampwidth()) != (16000, 1, 2):
                raise ValueError('不支持的音频格式: ' + entry['id'])
        started = time.perf_counter()
        try:
            result = subprocess.run([str(args.probe.resolve()), str(audio.resolve())],
                                    capture_output=True, encoding='utf-8', errors='replace', timeout=40)
            hypothesis = result.stdout.strip() if result.returncode == 0 else ''
            status = result.stderr.strip()
            code = result.returncode
        except subprocess.TimeoutExpired:
            hypothesis, status, code = '', 'probe_timeout', -1
        reference = normalize(entry['text'])
        row = dict(id=entry['id'], duration_s=entry['duration_s'], reference=entry['text'],
                   recognized=hypothesis, exit_code=code, details=status,
                   latency_s=round(time.perf_counter() - started, 3),
                   reference_chars=len(reference), errors=distance(reference, normalize(hypothesis)))
        rows.append(row)
        print(f'{len(rows)}/{args.count}: {entry["id"]} status={code} latency={row["latency_s"]}s', flush=True)
    successful = [r for r in rows if r['exit_code'] == 0]
    chars = sum(r['reference_chars'] for r in successful)
    summary = dict(seed=args.seed, total=len(rows), succeeded=len(successful),
                   audio_duration_s=round(sum(r['duration_s'] for r in rows), 3),
                   mean_latency_s=round(sum(r['latency_s'] for r in rows) / len(rows), 3),
                   normalized_cer=sum(r['errors'] for r in successful) / chars if chars else None)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(dict(summary=summary, samples=rows), ensure_ascii=False, indent=2), encoding='utf-8')
    print(json.dumps(summary, ensure_ascii=False))
    return 0 if len(successful) == len(rows) else 1

if __name__ == '__main__':
    raise SystemExit(main())

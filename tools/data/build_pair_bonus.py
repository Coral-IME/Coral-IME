"""将独立词对增益 TSV 编译为 CBGN0001；不读取词库或重估其他词对。"""
import argparse
import csv
import hashlib
import math
from pathlib import Path
import struct

HEADER = struct.Struct('<8sIIQIIII32s8s')


def inspect_pairs(data):
    """完整校验二进制语义；打包和紧凑编码共享同一格式契约。"""
    if not HEADER.size <= len(data) <= 512 * 1024 * 1024:
        raise ValueError('Invalid pair bonus size')
    magic, version, header_size, total, words, pairs, pool_size, flags, digest, reserved = HEADER.unpack_from(data)
    if (magic != b'CBGN0001' or version != 1 or header_size != HEADER.size or total != len(data)
            or words > 2_000_000 or pairs > 20_000_000 or flags or reserved != bytes(8)
            or HEADER.size + 8 * words + 12 * pairs + pool_size != len(data)):
        raise ValueError('Invalid pair bonus header or sections')
    if hashlib.sha256(memoryview(data)[HEADER.size:]).digest() != digest:
        raise ValueError('Pair bonus checksum mismatch')
    pool_start = HEADER.size + 8 * words + 12 * pairs
    offset = 0
    names = set()
    for i in range(words):
        start, length = struct.unpack_from('<II', data, HEADER.size + 8 * i)
        if start != offset or not 0 < length <= 4096 or start + length > pool_size:
            raise ValueError('Invalid pair bonus string bounds')
        word = data[pool_start + start:pool_start + start + length].decode('utf-8')
        if word in names or any(ord(c) < 32 or ord(c) == 127 for c in word):
            raise ValueError('Invalid or duplicate pair bonus word')
        names.add(word)
        offset += length
    if offset != pool_size:
        raise ValueError('Unreferenced pair bonus strings')
    previous_key = 0
    largest = 0.
    for i in range(pairs):
        previous, following, bonus = struct.unpack_from('<IIf', data, HEADER.size + 8 * words + 12 * i)
        key = previous << 32 | following
        if not (1 <= previous <= words and 1 <= following <= words and key > previous_key
                and math.isfinite(bonus) and 0 <= bonus <= 64):
            raise ValueError('Invalid pair bonus entry')
        previous_key = key
        largest = max(largest, bonus)
    return {'format': 'CBGN0001', 'dictionary_entries_binding': 'word-surface',
            'states': words, 'explicit_arcs': pairs, 'max_bonus': largest}


def compile_source(path):
    """编译完整词对源；返回确定性字节，不依赖主词库。"""
    with Path(path).open(encoding='utf-8-sig', newline='') as stream:
        reader = csv.DictReader(stream, delimiter='\t')
        if not {'prev_word', 'next_word', 'bonus'} <= set(reader.fieldnames or []):
            raise ValueError('Expected prev_word, next_word, bonus columns')
        return encode_pairs((r['prev_word'], r['next_word'], r['bonus']) for r in reader)


def encode_pairs(rows):
    pairs = {}
    for previous, following, bonus in rows:
        for word in (previous, following):
            if not word or len(word.encode('utf-8')) > 4096 or any(ord(c) < 32 or ord(c) == 127 for c in word):
                raise ValueError('Invalid pair word')
        bonus = float(bonus)
        if not math.isfinite(bonus) or not 0 <= bonus <= 64:
            raise ValueError('Bonus must be finite and between 0 and 64')
        if (previous, following) in pairs:
            raise ValueError('Duplicate pair')
        pairs[previous, following] = bonus
    words = sorted({word for pair in pairs for word in pair})
    if len(words) > 2_000_000 or len(pairs) > 20_000_000:
        raise ValueError('Pair table limit exceeded')
    ids = {word: i + 1 for i, word in enumerate(words)}
    pool = bytearray()
    records = bytearray()
    for word in words:
        encoded = word.encode('utf-8')
        records.extend(struct.pack('<II', len(pool), len(encoded)))
        pool.extend(encoded)
    for (previous, following), bonus in sorted(pairs.items(), key=lambda row: (ids[row[0][0]], ids[row[0][1]])):
        records.extend(struct.pack('<IIf', ids[previous], ids[following], bonus))
    body = records + pool
    size = HEADER.size + len(body)
    if size > 512 * 1024 * 1024:
        raise ValueError('Pair file limit exceeded')
    return HEADER.pack(b'CBGN0001', 1, HEADER.size, size, len(words), len(pairs), len(pool),
                       0, hashlib.sha256(body).digest(), bytes(8)) + body


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', type=Path)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    blob = compile_source(args.source)
    # 编译到新文件；避免覆盖正在映射或正式使用的资源。
    with args.output.open('xb') as stream:
        stream.write(blob)
    print(f'Compiled {len(blob)} bytes; sha256={hashlib.sha256(blob).hexdigest()}')


if __name__ == '__main__':
    main()

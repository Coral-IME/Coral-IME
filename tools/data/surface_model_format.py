"""CSLM0001 显式小端编码；不读取旧词频列，也不分配读音计数。"""
import hashlib
import math
import os
from pathlib import Path
import struct

HEADER = struct.Struct('<8sIIQIIIIII32s32s16s')
TOKEN = struct.Struct('<dddII')
ARC = struct.Struct('<Id')
PHRASE = struct.Struct('<III')
U32 = struct.Struct('<I')


def encode_model(tokens, arcs, phrases, sequences, unknown_state, lexicon_digest):
    """tokens 是 (ln初始, ln低阶, ln回退, 出弧起点, 出弧数)，端点状态从一开始。"""
    if len(lexicon_digest) != 32 or not any(lexicon_digest):
        raise ValueError('Expected nonzero SHA256 lexicon identity')
    body = bytearray()
    for row in tokens:
        if not all(math.isfinite(v) for v in row[:3]):
            raise ValueError('Nonfinite probability')
        body.extend(TOKEN.pack(*row))
    for row in arcs:
        body.extend(ARC.pack(*row))
    for row in phrases:
        body.extend(PHRASE.pack(*row))
    for state in sequences:
        body.extend(U32.pack(state))
    header = HEADER.pack(b'CSLM0001', 1, HEADER.size, HEADER.size + len(body),
        len(tokens), len(arcs), len(phrases), len(sequences), unknown_state, 0,
        lexicon_digest, hashlib.sha256(body).digest(), bytes(16))
    return header + body


def sha256_file(path):
    with Path(path).open('rb') as stream:
        digest = hashlib.sha256()
        for chunk in iter(lambda: stream.read(1024*1024), b''):
            digest.update(chunk)
        return digest.digest()


def write_model(path, tokens, arc_rows, arc_count, phrases, sequences, unknown_state, lexicon_digest):
    """大模型逐块写入；计数不符则保留 partial，不覆盖已有正式输出。"""
    path = Path(path)
    if path.exists() or len(lexicon_digest) != 32 or not any(lexicon_digest):
        raise ValueError('Output exists or lexicon identity is invalid')
    temporary = path.with_name(path.name + '.partial')
    size = HEADER.size + len(tokens)*TOKEN.size + arc_count*ARC.size + len(phrases)*PHRASE.size + len(sequences)*4
    digest = hashlib.sha256()
    with temporary.open('xb') as stream:
        stream.write(bytes(HEADER.size))
        buffer = bytearray()
        def emit(value):
            buffer.extend(value)
            if len(buffer) >= 1024*1024:
                digest.update(buffer)
                stream.write(buffer)
                buffer.clear()
        for row in tokens:
            emit(TOKEN.pack(*row))
        observed = 0
        for row in arc_rows:
            emit(ARC.pack(*row))
            observed += 1
        if observed != arc_count:
            raise ValueError('Arc iterator length mismatch')
        for row in phrases:
            emit(PHRASE.pack(*row))
        for state in sequences:
            emit(U32.pack(state))
        digest.update(buffer)
        stream.write(buffer)
        if stream.tell() != size:
            raise ValueError('Encoded length mismatch')
        stream.seek(0)
        stream.write(HEADER.pack(b'CSLM0001',1,HEADER.size,size,len(tokens),arc_count,
            len(phrases),len(sequences),unknown_state,0,lexicon_digest,digest.digest(),bytes(16)))
    # 同目录硬链接原子提交且拒绝覆盖；失败仍保留 partial 供诊断。
    os.link(temporary, path)
    temporary.unlink()

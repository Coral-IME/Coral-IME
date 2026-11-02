"""配套词库与CSLM模型的无损列式安装包；不重新计算任何浮点分值。"""
import argparse
from array import array
import hashlib
import json
import lzma
import os
from pathlib import Path
import struct
import sys

sys.dont_write_bytecode = True
from paths import output_path
from surface_model_format import HEADER as MODEL_HEADER
from build_pair_bonus import inspect_pairs

HEADER = struct.Struct('<8sII4Q32s32s32s')
MAGIC = b'CSTPACK1'
MAX_BYTES = 1 << 30
PROTOTYPES = tuple((i * 4, 4, i in (0, 2)) for i in range(6))
MODEL_TOKENS = tuple((i * 4, 4, i == 3) for i in range(5))
MODEL_ARCS = ((0, 4, True), (4, 4, False))
MODEL_PHRASES = ((0, 4, True), (4, 4, True), (8, 4, False))
MODEL_SEQUENCE = ((0, 4, False),)


def require(value, message):
    if not value:
        raise ValueError(message)


def planes(data, start, count, stride, fields):
    block = memoryview(data)[start:start + count * stride]
    require(len(block) == count * stride, 'Truncated record block')
    output = bytearray()
    for offset, width, delta in fields:
        if delta:
            values = array('I' if width == 4 else 'Q')
            values.frombytes(block)
            if sys.byteorder != 'little':
                values.byteswap()
            values = values[offset // width::stride // width]
            mask = (1 << (8 * width)) - 1
            for i in range(len(values) - 1, 0, -1):
                values[i] = (values[i] - values[i - 1]) & mask
            if sys.byteorder != 'little':
                values.byteswap()
            raw = values.tobytes()
            for j in range(width):
                output.extend(raw[j::width])
        else:
            for j in range(width):
                output.extend(block[offset + j::stride].tobytes())
    require(len(output) == count * stride, 'Record schema size mismatch')
    return output


def uleb(value):
    result = bytearray()
    while value >= 128:
        result.append((value & 127) | 128)
        value >>= 7
    result.append(value)
    return result


def front_strings(unique):
    output = bytearray()
    previous = b''
    for value in unique:
        prefix = 0
        limit = min(len(previous), len(value))
        while prefix < limit and previous[prefix] == value[prefix]:
            prefix += 1
        suffix = value[prefix:]
        output.extend(uleb(prefix))
        output.extend(uleb(len(suffix)))
        output.extend(suffix)
        previous = value
    return output


def encode_lexicon(data):
    require(64 <= len(data) <= MAX_BYTES, 'Invalid lexicon size')
    h = struct.unpack_from('<8s14I', data)
    _, version, keys, entries, pool_size, key_off, entry_off, pool_off, size = h[:9]
    aligned = lambda n: (n + 63) // 64 * 64
    require(h[0] == b'KNAPRGB1' and version == 2 and size == len(data) and not any(h[9:]), 'Invalid lexicon header')
    require(keys and entries and key_off == 64 and entry_off == aligned(64 + keys * 24)
            and pool_off == aligned(entry_off + entries * 32) and pool_off + pool_size == size,
            'Noncanonical lexicon layout')
    require(not any(data[key_off + keys * 24:entry_off]) and not any(data[entry_off + entries * 32:pool_off]),
            'Nonzero lexicon alignment padding')
    # 主词典的字符串池按“检索键、词面、读音”顺序写入；验证后可以省去全部偏移。
    cursor = 0
    def string_at(offset, length):
        nonlocal cursor
        require(offset == cursor and offset + length < pool_size and data[pool_off + offset + length] == 0,
                'Noncanonical lexicon string pool')
        value = data[pool_off + offset:pool_off + offset + length]
        require(b'\0' not in value, 'NUL in lexicon string')
        cursor += length + 1
        return value
    observed = {}
    aliases = array('I')
    key_strings = []
    previous = b''
    consumed = 0
    for i in range(keys):
        prefix, offset, length, start, count = struct.unpack_from('<Q4I', data, key_off + 24 * i)
        key = string_at(offset, length)
        require((i == 0 or key > previous) and prefix == int.from_bytes(key[:8].ljust(8, b'\0'), 'big')
                and start == consumed and count <= entries - consumed, 'Noncanonical lexicon keys')
        previous = key
        key_strings.append((key, count))
        for j in range(count):
            wo, wl, ro, rl, identity, weight, backoff, flag = struct.unpack_from('<8I', data, entry_off + 32 * (start + j))
            record = (string_at(wo, wl), string_at(ro, rl), identity, weight, backoff, flag)
            aliases.append(observed.setdefault(record, len(observed)))
        consumed += count
    require(consumed == entries and cursor == pool_size, 'Lexicon pool/entry coverage mismatch')
    prototypes = sorted(observed)
    strings = sorted({value for p in prototypes for value in p[:2]})
    string_ids = {value: i for i, value in enumerate(strings)}
    prototype_ids = {p: i for i, p in enumerate(prototypes)}
    remap = array('I', (prototype_ids[p] for p in observed))
    aliases = array('I', (remap[value] for value in aliases))
    prototype_data = bytearray()
    for word, reading, identity, weight, backoff, flag in prototypes:
        prototype_data.extend(struct.pack('<6I', string_ids[word], string_ids[reading], identity, weight, backoff, flag))
    result = bytearray(data[:64])
    result.extend(struct.pack('<II', len(prototypes), len(strings)))
    result.extend(front_strings(strings))
    result.extend(planes(prototype_data, 0, len(prototypes), 24, PROTOTYPES))
    previous = b''
    for key, count in key_strings:
        prefix = 0
        while prefix < min(len(previous), len(key)) and previous[prefix] == key[prefix]:
            prefix += 1
        result.extend(uleb(prefix)); result.extend(uleb(len(key) - prefix)); result.extend(key[prefix:]); result.extend(uleb(count))
        previous = key
    if sys.byteorder != 'little':
        aliases.byteswap()
    raw = aliases.tobytes()
    for j in range(4):
        result.extend(raw[j::4])
    return result


def encode_model(data, lexicon_digest):
    require(MODEL_HEADER.size <= len(data) <= MAX_BYTES, 'Invalid model size')
    h = MODEL_HEADER.unpack_from(data)
    require(h[0] == b'CSLM0001' and h[1] == 1 and h[2] == 128 and h[3] == len(data)
            and h[4] > 0 and 1 <= h[8] <= h[4] and h[9] == 0 and h[12] == bytes(16), 'Invalid model header')
    require(h[10] == lexicon_digest, 'Model is bound to a different dictionary')
    require(h[11] == hashlib.sha256(memoryview(data)[128:]).digest(), 'Model body checksum mismatch')
    require(128 + h[4] * 32 + h[5] * 12 + h[6] * 12 + h[7] * 4 == len(data), 'Invalid model section lengths')
    # 概率按完整64位位型去重；不转成float32，不舍入，也不重算对数。
    values = set()
    for i in range(h[4]):
        values.update(struct.unpack_from('<3Q', data, 128 + i * 32))
    arc_offset = 128 + h[4] * 32
    for i in range(h[5]):
        values.add(struct.unpack_from('<Q', data, arc_offset + i * 12 + 4)[0])
    values = sorted(values)
    ids = {value: i for i, value in enumerate(values)}
    dictionary = array('Q', values)
    if sys.byteorder != 'little': dictionary.byteswap()
    tokens = bytearray(); arcs = bytearray()
    for i in range(h[4]):
        a, b, c, offset, count = struct.unpack_from('<3Q2I', data, 128 + i * 32)
        tokens.extend(struct.pack('<5I', ids[a], ids[b], ids[c], offset, count))
    for i in range(h[5]):
        target, probability = struct.unpack_from('<IQ', data, arc_offset + i * 12)
        arcs.extend(struct.pack('<2I', target, ids[probability]))
    result = bytearray(data[:128]); result.extend(struct.pack('<I', len(values)))
    result.extend(planes(dictionary.tobytes(), 0, len(values), 8, ((0, 8, True),)))
    result.extend(planes(tokens, 0, h[4], 20, MODEL_TOKENS))
    result.extend(planes(arcs, 0, h[5], 8, MODEL_ARCS))
    offset = arc_offset + h[5] * 12
    for count, width, fields in [(h[6], 12, MODEL_PHRASES), (h[7], 4, MODEL_SEQUENCE)]:
        result.extend(planes(data, offset, count, width, fields)); offset += count * width
    return result


def encode_parts(lexicon, model):
    require(len(lexicon) + len(model) <= MAX_BYTES, 'Restored pair exceeds 1 GiB limit')
    lex_hash = hashlib.sha256(lexicon).digest()
    lex = encode_lexicon(lexicon)
    flags = 3
    if model[:8] == b'CBGN0001':
        inspect_pairs(model)
        lm = model
        flags = 7
    else:
        lm = encode_model(model, lex_hash)
    require(HEADER.size + len(lex) + len(lm) <= MAX_BYTES, 'Encoded pair exceeds 1 GiB limit')
    body = hashlib.sha256(lex)
    body.update(lm)
    header = HEADER.pack(MAGIC, 1, flags, len(lexicon), len(model), len(lex), len(lm),
                         lex_hash, hashlib.sha256(model).digest(), body.digest())
    return header, lex, lm


def pack_tables(tables_dir, output):
    tables_dir = Path(tables_dir).resolve()
    output = output_path(output, [tables_dir])
    lex_path = tables_dir / 'mainlexicon.coral'
    model_path = tables_dir / 'languagemodel.coral'
    print('Encoding paired lexicon and model without changing scores', flush=True)
    parts = encode_parts(lex_path.read_bytes(), model_path.read_bytes())
    temporary = output.with_name(output.name + '.partial')
    with temporary.open('xb') as raw:
        with lzma.LZMAFile(raw, 'w', format=lzma.FORMAT_XZ, check=lzma.CHECK_CRC64,
                           preset=9 | lzma.PRESET_EXTREME) as stream:
            for part in parts:
                stream.write(part)
    # Windows同目录提交，拒绝覆盖已有包；失败的partial保留供检查。
    require(not output.exists(), 'Output already exists')
    os.rename(temporary, output)
    info = {'format': 'CSTPACK1', 'version': 1, 'flags': HEADER.unpack(parts[0])[2], 'lossless': True, 'compressed_bytes': output.stat().st_size,
            'encoded_bytes': sum(map(len, parts)), 'sha256': hashlib.sha256(output.read_bytes()).hexdigest(),
            'files': {name: {'size': p.stat().st_size, 'sha256': hashlib.sha256(p.read_bytes()).hexdigest()}
                      for name, p in [('mainlexicon.coral', lex_path), ('languagemodel.coral', model_path)]}}
    return info


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--tables-dir', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    try:
        print(json.dumps(pack_tables(args.tables_dir, args.output), ensure_ascii=False), flush=True)
    except (ValueError, OSError, lzma.LZMAError) as error:
        parser.exit(1, str(error) + '\n')


if __name__ == '__main__':
    main()

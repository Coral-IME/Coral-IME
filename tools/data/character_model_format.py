"""字符模型的布局、完整性检查与逐输出行 INT8 转换，不依赖训练框架。"""
from array import array
import hashlib
import math
from pathlib import Path
import struct
import sys


def tensor_layout(layers, width, feedforward, vocabulary, positions):
    yield 'embedding', vocabulary, width, True
    yield 'position', positions, width, False
    yield 'final_gamma', 1, width, False
    yield 'final_beta', 1, width, False
    for layer in range(layers):
        for name, rows, columns, matrix in [
            ('gamma1', 1, width, False), ('beta1', 1, width, False),
            ('qkv', 3*width, width, True), ('qkv_bias', 1, 3*width, False),
            ('project', width, width, True), ('project_bias', 1, width, False),
            ('gamma2', 1, width, False), ('beta2', 1, width, False),
            ('expand', feedforward, width, True), ('expand_bias', 1, feedforward, False),
            ('contract', width, feedforward, True), ('contract_bias', 1, width, False)]:
            yield f'layer{layer}.{name}', rows, columns, matrix


def read_header(header, size):
    if (len(header) != 128 or header[:8] != b'CCHLM001' or
            struct.unpack_from('<I', header, 8)[0] not in (1, 2) or
            struct.unpack_from('<IQ', header, 12) != (128, size) or
            any(header[60:64]) or any(header[96:128]) or not 128 <= size <= 256*1024**2):
        raise ValueError('Invalid character model header')
    version = struct.unpack_from('<I', header, 8)[0]
    layers, width, heads, feedforward, vocabulary, positions, bos, unknown = struct.unpack_from('<8I', header, 24)
    epsilon = struct.unpack_from('<f', header, 56)[0]
    if (not 1 <= layers <= 8 or not 1 <= width <= 512 or not 1 <= heads <= width or width % heads or
            not 1 <= feedforward <= 4096 or not 1 <= vocabulary <= 32768 or not 1 <= positions <= 2048 or
            bos >= vocabulary or unknown >= vocabulary or not math.isfinite(epsilon) or not 0 < epsilon <= .1):
        raise ValueError('Invalid character model dimensions')
    layout = list(tensor_layout(layers, width, feedforward, vocabulary, positions))
    expected = 128 + vocabulary*4 + sum(rows*(columns+4) if version == 2 and matrix else rows*columns*4
                                      for _, rows, columns, matrix in layout)
    if expected != size:
        raise ValueError('Invalid character model payload size')
    return version, vocabulary, layout


def inspect_model(path):
    path = Path(path)
    size = path.stat().st_size
    with path.open('rb') as stream:
        header = stream.read(128)
        version, vocabulary, layout = read_header(header, size)
        checksum = hashlib.sha256()
        for block in iter(lambda: stream.read(1024*1024), b''):
            checksum.update(block)
    if checksum.digest() != header[64:96]:
        raise ValueError('Invalid character model payload checksum')
    return version, header, vocabulary, layout


def quantize_model(source, output):
    source, output = Path(source), Path(output)
    version, header, vocabulary, layout = inspect_model(source)
    if version != 1:
        raise ValueError('INT8 conversion requires a float32 version 1 model')
    data = source.read_bytes()
    if hashlib.sha256(data[128:]).digest() != header[64:96] or data[:128] != header:
        raise ValueError('Character model changed during conversion')
    offset = 128 + vocabulary*4
    payload = bytearray(data[128:offset])
    for _, rows, columns, matrix in layout:
        size = rows*columns*4
        values = array('f')
        values.frombytes(data[offset:offset+size]); offset += size
        if sys.byteorder != 'little': values.byteswap()
        if any(not math.isfinite(value) or abs(value) > 10000 for value in values):
            raise ValueError('Invalid character model weight')
        if not matrix:
            payload.extend(data[offset-size:offset]); continue
        scales, packed = [], array('b')
        for row in range(rows):
            chunk = values[row*columns:(row+1)*columns]
            maximum = max(map(abs, chunk))
            scale = max(maximum / 127, 2**-149) if maximum else 1.
            scale = struct.unpack('<f', struct.pack('<f', scale))[0]
            scales.append(scale)
            packed.extend(max(-127, min(127, round(value / scale))) for value in chunk)
        payload.extend(struct.pack(f'<{rows}f', *scales))
        payload.extend(packed.tobytes())
    assert offset == len(data)
    result = bytearray(header)
    struct.pack_into('<I', result, 8, 2)
    struct.pack_into('<Q', result, 16, 128+len(payload))
    result[64:96] = hashlib.sha256(payload).digest()
    with output.open('xb') as stream:
        stream.write(result); stream.write(payload)


def main():
    import argparse
    from paths import output_path
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--input', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    output = output_path(args.output, (args.input,))
    quantize_model(args.input, output)
    print(f'INT8 model: {output.stat().st_size} bytes')


if __name__ == '__main__':
    main()

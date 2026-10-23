"""人工小网络的独立概率、坏资源拒绝和解码器接入检查。"""
import hashlib
import math
from pathlib import Path
import struct
import subprocess
import sys
import tempfile


def fixture():
    # 最终归一化的 gamma 为零、beta 为 [1,0]，输出只由手写嵌入第一维决定。
    vocabulary = [0x110000, 0x110001, ord('在'), ord('再'), ord('它'), ord('甲')]
    d, f, v, p = 2, 4, len(vocabulary), 128
    embedding = [0.0] * (v*d)
    embedding[3*d] = 4.0
    weights = embedding + [0.0]*(p*d) + [0.0]*d + [1.0, 0.0]
    weights += [0.0]*(4*d*d+2*d*f+9*d+f)
    payload = struct.pack('<'+'I'*v, *vocabulary) + struct.pack('<'+'f'*len(weights), *weights)
    header = bytearray(128)
    header[:8] = b'CCHLM001'
    struct.pack_into('<IIQIIIIIIIIfI', header, 8, 1, 128, 128+len(payload), 1, d, 1, f, v, p, 0, 1, 1e-5, 0)
    header[64:96] = hashlib.sha256(payload).digest()
    return bytes(header) + payload


def main():
    probe = sys.argv[1]
    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        good, bad = root/'tiny.coral', root/'bad.coral'
        good.write_bytes(fixture())
        bad.write_bytes(b'broken')
        text = '\t在\n\t再\n\t在再\n它\t再\n\t龘\n\t\n'
        result = subprocess.run([probe,str(good),str(bad)],input=text,text=True,encoding='utf-8',capture_output=True)
        assert result.returncode == 0, result.stderr
        values = [float(value) for value in result.stdout.splitlines()]
        normalizer = math.log(5+math.exp(4))
        expected = [-normalizer,4-normalizer,4-2*normalizer,4-normalizer]
        assert len(values) == 6 and all(abs(a-b)<1e-5 for a,b in zip(values,expected))
        assert all(math.isnan(value) for value in values[4:])
        for mutation in ['digest','shape','reserved','vocab','trailing','nan']:
            data = bytearray(good.read_bytes())
            if mutation == 'digest': data[-1] ^= 1
            elif mutation == 'shape': struct.pack_into('<I',data,28,0xffffffff)
            elif mutation == 'reserved': data[100]=1
            elif mutation == 'vocab': struct.pack_into('<I',data,128+3*4,ord('在'))
            elif mutation == 'trailing': data += b'\0'
            elif mutation == 'nan': struct.pack_into('<f',data,len(data)-4,float('nan'))
            if mutation in ['vocab','nan']: data[64:96] = hashlib.sha256(data[128:]).digest()
            bad.write_bytes(data)
            assert subprocess.run([probe,str(bad)],capture_output=True).returncode != 0, mutation
        if len(sys.argv) == 5:
            result = subprocess.run([sys.argv[2],sys.argv[3],sys.argv[4],str(good)],capture_output=True)
            assert result.returncode == 0, result.stderr
    print('Character probabilities, invalid resources, and reload preservation passed')


if __name__ == '__main__':
    main()

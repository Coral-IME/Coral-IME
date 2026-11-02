"""独立字节及图夹具验证增益、空表、词库换号和损坏资源。"""
import hashlib
import math
from pathlib import Path
import struct
import subprocess
import sys
import tempfile

sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'tools/data'))
from build_pair_bonus import encode_pairs


def lexicon(shift):
    pool = bytearray()
    def text(value):
        raw = value.encode('utf-8')
        result = len(pool), len(raw)
        pool.extend(raw + b'\0')
        return result
    key = text('a')
    reading = text('a')
    entries = []
    for i, word in enumerate(['甲', '乙', '丙', '甲乙']):
        entries.append(struct.pack('<5I2fI', *text(word), *reading, 12 + i + shift, -1., 0., 1))
    header = struct.pack('<8s14I', b'KNAPRGB1', 2, 1, 4, len(pool), 64, 128, 256,
                         256 + len(pool), *([0] * 6))
    record = struct.pack('<Q4I', int.from_bytes(b'a' + bytes(7), 'big'), *key, 0, 4)
    return header + record + bytes(40) + b''.join(entries) + pool


def main():
    rows = [('甲','乙',8.), ('甲','甲',1.), ('乙','丙',2.), ('甲乙','丙',1.), ('<s>','乙',.5)]
    blob = encode_pairs(rows)
    # 手写三字节词面的独立文件布局，防止编译与读者共享同一偏移错误。
    body = struct.pack('<4I',0,3,3,3) + struct.pack('<IIf',2,1,2.) + '乙甲'.encode('utf-8')
    expected = struct.pack('<8sIIQIIII32s8s', b'CBGN0001',1,80,80+len(body),2,1,6,0,
                           hashlib.sha256(body).digest(),bytes(8)) + body
    assert encode_pairs([('甲','乙',2.)]) == expected
    for invalid in [rows + [rows[0]], [('','乙',1)], [('甲','乙',math.nan)],
                    [('甲','乙',math.inf)], [('甲','乙',-1)], [('甲','乙',65)]]:
        try:
            encode_pairs(invalid)
        except ValueError:
            pass
        else:
            raise AssertionError('Accepted invalid source')
    with tempfile.TemporaryDirectory(prefix='coral-pair-bonus-') as folder:
        root = Path(folder)
        good, bad, zero, lex, changed = [root / name for name in ['good','bad','zero','lex','changed']]
        good.write_bytes(blob)
        zero.write_bytes(encode_pairs([]))
        lex.write_bytes(lexicon(0))
        changed.write_bytes(lexicon(100))
        deleted = root / 'deleted'
        deleted.write_bytes(encode_pairs(rows[1:]))
        def run(value):
            bad.write_bytes(value)
            result = subprocess.run([sys.argv[1], str(good), str(bad), str(zero), str(lex), str(changed), str(deleted)],
                                    capture_output=True, text=True)
            assert result.returncode == 0, result.stderr + result.stdout
        run(blob[:-1])
        # 修复摘要后仍应拒绝语义损坏，不只测截断和校验和。
        for at, value in [(36, struct.pack('<I',1)), (80, struct.pack('<I',1)),
                          (80 + 8 * 5 + 8, struct.pack('<f',math.nan))]:
            altered = bytearray(blob)
            altered[at:at+len(value)] = value
            altered[40:72] = hashlib.sha256(altered[80:]).digest()
            run(altered)
        # 删除仅撤销本词对；保留词对的已量化参数逐项不变。
        removed = encode_pairs(rows[1:])
        assert len(removed) < len(blob)
    print('Pair bonus format and native search checks passed')


if __name__ == '__main__':
    main()

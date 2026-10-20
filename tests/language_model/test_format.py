"""固定字节夹具和数学期望独立检查，包含校验和正确但语义错误的资源。"""
import hashlib
import math
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'tools/data'))
from surface_model_format import encode_model, write_model

PROBE, NATIVE = sys.argv[1:3]
DECODER = sys.argv[3] if len(sys.argv) > 3 else None
sys.argv[1:] = []


class FormatTests(unittest.TestCase):
    def test_math_and_failures(self):
        with tempfile.TemporaryDirectory(prefix='coral-lm-') as directory:
            root = Path(directory)
            lexicon = root / 'mainlexicon.coral'
            lexicon.write_bytes(b'artificial lexicon identity')
            digest = hashlib.sha256(lexicon.read_bytes()).digest()
            # 初始与低阶不同；第一行仅保留目标一，回退系数为二。
            tokens = [(math.log(.7), math.log(.6), math.log(2), 0, 1),
                (math.log(.2), math.log(.3), 0., 1, 0),
                (math.log(.1), math.log(.1), math.log(.4/.7), 1, 1)]
            data = encode_model(tokens, [(1, math.log(.2)), (2, math.log(.6))],
                [(10, 0, 2), (11, 2, 2), (12, 4, 1), (13, 5, 1), (14, 6, 1)],
                [2, 3, 2, 3, 1, 2, 3], 3, digest)
            model = root / 'model.coral'
            model.write_bytes(data)
            streamed = root / 'streamed.coral'
            arguments = (tokens, iter([(1, math.log(.2)), (2, math.log(.6))]), 2,
                [(10, 0, 2), (11, 2, 2), (12, 4, 1), (13, 5, 1), (14, 6, 1)],
                [2, 3, 2, 3, 1, 2, 3], 3, digest)
            write_model(streamed, *arguments)
            self.assertEqual(streamed.read_bytes(), data)
            with self.assertRaises(ValueError):
                write_model(streamed, *arguments)
            incomplete = root / 'incomplete.coral'
            with self.assertRaises(ValueError):
                write_model(incomplete, tokens, iter([]), 2, [], [], 3, digest)
            self.assertFalse(incomplete.exists())
            self.assertTrue(incomplete.with_name(incomplete.name+'.partial').exists())
            def run(value, commands=''):
                model.write_bytes(value)
                return subprocess.run([PROBE, str(model), str(lexicon)], input=commands,
                    capture_output=True, text=True)
            result = run(data, 'S 0 2\nS 1 2\nS 1 1\nP 1 10\n')
            self.assertEqual(result.returncode, 0, result.stderr)

            if DECODER:
                decoded = subprocess.run([DECODER, str(model), str(lexicon)], capture_output=True, text=True)
                self.assertEqual(decoded.returncode, 0, decoded.stderr)
            output = result.stdout.splitlines()
            self.assertAlmostEqual(float(output[0]), math.log(.2), places=12)
            self.assertAlmostEqual(float(output[1]), math.log(.6), places=12)
            self.assertAlmostEqual(float(output[2]), math.log(.2), places=12)
            self.assertAlmostEqual(float(output[3].split()[0]), math.log(.06), places=12)
            self.assertEqual(output[3].split()[1:], ['3', '0'])
            malformed = [data[:-1], data + b'x', b'KNAGBIN1' + data[8:]]
            def change(offset, value, repair=True):
                altered = bytearray(data)
                altered[offset:offset+len(value)] = value
                if repair:
                    altered[80:112] = hashlib.sha256(altered[128:]).digest()
                return bytes(altered)
            malformed += [change(8, struct.pack('<I', 2)), change(48, bytes(32)),
                change(128, struct.pack('<d', math.nan)), change(128, struct.pack('<d', .1)),
                change(144, struct.pack('<d', 0.)),  # 回退归一化错误
                change(224, struct.pack('<I', 4)),  # 弧越界
                change(260, struct.pack('<I', 10)), # 重复候选 ID
                change(308, struct.pack('<I', 4)),  # 词元越界
                change(112, b'\1'), change(128, b'\0', repair=False)]
            for index, value in enumerate(malformed):
                result = run(value)
                self.assertNotEqual(result.returncode, 0, f'malformed case {index}')
            bad = root / 'bad.coral'
            bad.write_bytes(data[:-1])
            model.write_bytes(data)
            result = subprocess.run([NATIVE, str(model), str(lexicon), str(bad)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)


if __name__ == '__main__':
    unittest.main()

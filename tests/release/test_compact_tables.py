"""人工字节夹具核验无损还原、损坏拒绝、Unicode路径和成对替换回滚。"""
import ctypes
import hashlib
import lzma
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools/data'))
from compact_tables import encode_parts, HEADER
from surface_model_format import encode_model
from build_pair_bonus import encode_pairs

RESTORE = Path(sys.argv.pop(1)).resolve() if len(sys.argv) > 1 and sys.argv[1].lower().endswith('.exe') else None


def fixture():
    pool = bytearray(); keys = bytearray(); entries = bytearray()
    # 相同词条可共享多种检索键，且同词另一别名也可能具有不同分值。
    data = [(b'a', [('甲', b'a', 42, 0xbf812345), ('乙', b'yi', 43, 0x80000000)]),
            (b'a\'a', [('甲', b'a', 42, 0xbf812345)]),
            (b'a\'b', [('甲', b'a', 42, 0xbf812346)])]
    def text(value):
        offset = len(pool); pool.extend(value); pool.append(0); return offset, len(value)
    index = 0
    for key, records in data:
        offset, size = text(key)
        keys.extend(struct.pack('<Q4I', int.from_bytes(key[:8].ljust(8, b'\0'), 'big'), offset, size, index, len(records)))
        for word, reading, identity, weight in records:
            wo, wl = text(word.encode()); ro, rl = text(reading)
            entries.extend(struct.pack('<8I', wo, wl, ro, rl, identity, weight, 0x80000000, 1)); index += 1
    aligned = lambda n: (n + 63) // 64 * 64
    entry_off = aligned(64 + len(keys)); pool_off = aligned(entry_off + len(entries))
    lexicon = bytearray(pool_off + len(pool))
    struct.pack_into('<8s14I', lexicon, 0, b'KNAPRGB1', 2, len(data), index, len(pool), 64, entry_off, pool_off, len(lexicon), *([0] * 6))
    lexicon[64:64 + len(keys)] = keys; lexicon[entry_off:entry_off + len(entries)] = entries; lexicon[pool_off:] = pool
    model = encode_model([(0., 0., -0., 0, 1)], [(1, 0.)], [(42, 0, 1), (43, 1, 2)], [1, 1, 1], 1, hashlib.sha256(lexicon).digest())
    return bytes(lexicon), model


class EncodingTest(unittest.TestCase):
    def test_pair_bonus_has_separate_flags_and_no_dictionary_binding(self):
        lex, _ = fixture()
        model = encode_pairs([('甲','乙',1.25)])
        h, a, b = encode_parts(lex,model)
        self.assertEqual(HEADER.unpack(h)[:3], (b'CSTPACK1',1,7))
        self.assertEqual(b,model)
        damaged = bytearray(model); damaged[-1] ^= 1
        with self.assertRaises(ValueError): encode_parts(lex,damaged)
    def test_rejects_wrong_pair_or_noncanonical_input(self):
        lex, model = fixture()
        h, a, b = encode_parts(lex, model)
        self.assertEqual(HEADER.unpack(h)[:3], (b'CSTPACK1', 1, 3))
        self.assertEqual(HEADER.unpack(h)[-1], hashlib.sha256(a + b).digest())
        wrong = bytearray(model); wrong[48] ^= 1
        with self.assertRaisesRegex(ValueError, 'different dictionary'): encode_parts(lex, bytes(wrong))
        with self.assertRaisesRegex(ValueError, 'header'): encode_parts(lex + b'tail', model)
        bad = bytearray(lex); struct.pack_into('<I', bad, 64 + 8, 1)
        with self.assertRaisesRegex(ValueError, 'string pool'): encode_parts(bytes(bad), model)


@unittest.skipUnless(RESTORE, 'Native restore tool not supplied')
class RestoreTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='coral-compact-')
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.lex, self.model = fixture()
        self.raw = b''.join(encode_parts(self.lex, self.model))
        self.bundle = self.root / '压缩 数据.xz'
        self.dest = self.root / '安装 输出'
        # 从新安装位置运行同一份字节，避免本机构建目录的进程写权限规则干扰。
        self.restore = self.root / 'coral_table_restore.exe'
        shutil.copyfile(RESTORE, self.restore)
        self.assertEqual(hashlib.sha256(self.restore.read_bytes()).digest(), hashlib.sha256(RESTORE.read_bytes()).digest())

    def run_restore(self, raw=None, packed=None, replace=False):
        self.bundle.write_bytes(packed if packed is not None else lzma.compress(self.raw if raw is None else raw, preset=1))
        return subprocess.run([str(self.restore), str(self.bundle), str(self.dest)] + (['--replace'] if replace else []), capture_output=True)

    def rehash(self, raw):
        raw = bytearray(raw); raw[112:144] = hashlib.sha256(raw[144:]).digest(); return bytes(raw)

    def test_exact_bits_aliases_and_unicode_paths(self):
        result = self.run_restore(); self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual((self.dest / 'mainlexicon.coral').read_bytes(), self.lex)
        self.assertEqual((self.dest / 'languagemodel.coral').read_bytes(), self.model)
        self.assertNotEqual(self.run_restore().returncode, 0)
        self.assertEqual(self.run_restore(replace=True).returncode, 0)

    def test_pair_bonus_and_empty_table_roundtrip(self):
        for model in [encode_pairs([('甲','乙',1.25)]),encode_pairs([])]:
            self.raw = b''.join(encode_parts(self.lex,model))
            result = self.run_restore(replace=self.dest.exists())
            self.assertEqual(result.returncode,0,result.stderr)
            self.assertEqual((self.dest/'mainlexicon.coral').read_bytes(),self.lex)
            self.assertEqual((self.dest/'languagemodel.coral').read_bytes(),model)
        # 所有外层摘要都修复后，原生端仍须拒绝非有限增益，且保留已安装资源。
        model=bytearray(encode_pairs([('甲','乙',1.25)]))
        struct.pack_into('<f',model,80+2*8+8,float('nan'))
        model[40:72]=hashlib.sha256(model[80:]).digest()
        header,lex_section,good=encode_parts(self.lex,encode_pairs([('甲','乙',1.25)]))
        raw=bytearray(header+lex_section+model)
        raw[80:112]=hashlib.sha256(model).digest()
        result=self.run_restore(raw=self.rehash(raw),replace=True)
        self.assertNotEqual(result.returncode,0)
        self.assertEqual((self.dest/'languagemodel.coral').read_bytes(),encode_pairs([]))

    def test_bad_archives_never_modify_previous_pair(self):
        self.dest.mkdir()
        old = {n: b'previous ' + n.encode() for n in ['mainlexicon.coral', 'languagemodel.coral']}
        for n, data in old.items(): (self.dest / n).write_bytes(data)
        packed = lzma.compress(self.raw, preset=1)
        cases = [('truncated XZ', None, packed[:-4]), ('trailing XZ', None, packed + b'tail'),
                 ('concatenated XZ', None, packed + packed), ('short header', b'bad header', None),
                 ('truncated body', self.raw[:-1], None), ('trailing body', self.raw + b'tail', None)]
        raw = bytearray(self.raw); raw[12] = 127; cases.append(('unsupported flags', raw, None))
        raw = bytearray(self.raw); raw[144 + 12:144 + 16] = b'\xff' * 4; cases.append(('huge key count', self.rehash(raw), None))
        raw = bytearray(self.raw); raw[48] ^= 1; cases.append(('output digest', raw, None))
        raw = bytearray(self.raw); raw[144 + 72:144 + 82] = b'\x80' * 10; cases.append(('varint overflow', self.rehash(raw), None))
        raw = bytearray(self.raw); raw[144 + HEADER.unpack_from(raw)[5] - 1] ^= 1; cases.append(('bad prototype ref', self.rehash(raw), None))
        # 保持外层摘要正确，仍须拒绝模型绑定另一份词库。
        raw = bytearray(self.raw); model_start = 144 + HEADER.unpack_from(raw)[5]
        raw[model_start + 48] ^= 1
        altered_model = bytearray(self.model); altered_model[48] ^= 1
        raw[80:112] = hashlib.sha256(altered_model).digest()
        cases.append(('wrong model binding', self.rehash(raw), None))
        for title, raw, compressed in cases:
            with self.subTest(title=title):
                self.assertNotEqual(self.run_restore(raw=raw, packed=compressed, replace=True).returncode, 0)
                for n, data in old.items(): self.assertEqual((self.dest / n).read_bytes(), data)
                self.assertFalse(list(self.dest.glob('.coral-tables-*')))

    def test_replace_and_invalid_second_destination(self):
        self.dest.mkdir(); (self.dest / 'mainlexicon.coral').write_bytes(b'old')
        (self.dest / 'languagemodel.coral').mkdir()
        self.assertNotEqual(self.run_restore(replace=True).returncode, 0)
        self.assertEqual((self.dest / 'mainlexicon.coral').read_bytes(), b'old')
        (self.dest / 'languagemodel.coral').rmdir()
        self.assertEqual(self.run_restore(replace=True).returncode, 0)
        self.assertEqual((self.dest / 'mainlexicon.coral').read_bytes(), self.lex)
        self.assertEqual((self.dest / 'languagemodel.coral').read_bytes(), self.model)

    @unittest.skipUnless(os.name == 'nt', 'Windows file sharing test')
    def test_locked_second_file_rolls_back_first_backup(self):
        self.dest.mkdir(); lex = self.dest / 'mainlexicon.coral'; model = self.dest / 'languagemodel.coral'
        lex.write_bytes(b'old lexicon'); model.write_bytes(b'old model')
        kernel = ctypes.WinDLL('kernel32', use_last_error=True)
        create = kernel.CreateFileW
        create.argtypes = [ctypes.c_wchar_p, ctypes.c_uint32, ctypes.c_uint32, ctypes.c_void_p, ctypes.c_uint32, ctypes.c_uint32, ctypes.c_void_p]
        create.restype = ctypes.c_void_p
        close = kernel.CloseHandle; close.argtypes = [ctypes.c_void_p]
        handle = create(str(model), 0x80000000, 1, None, 3, 0, None)
        self.assertNotIn(handle, (None, ctypes.c_void_p(-1).value))
        try:
            result = self.run_restore(replace=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn(b'Cannot publish resource file', result.stderr)
        finally: close(handle)
        self.assertEqual(lex.read_bytes(), b'old lexicon'); self.assertEqual(model.read_bytes(), b'old model')
        self.assertFalse(list(self.dest.glob('.coral-tables-*')))


if __name__ == '__main__':
    unittest.main()

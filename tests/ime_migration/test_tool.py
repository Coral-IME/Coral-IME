"""迁移工具的人工夹具回归；不使用真实用户词库。"""
import csv
import json
from pathlib import Path
import struct
import unittest
from unittest import mock
import uuid
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'tools/data'))
import ime_dictionary_tool as tool

BASE = Path(__file__).resolve().parents[2] / 'build/ime-migration-tests'
BASE.mkdir(parents=True, exist_ok=True)


def vi(number):
    data = bytearray()
    while number >= 128:
        data.append((number & 127) | 128)
        number >>= 7
    data.append(number)
    return bytes(data)


def lp(data):
    return vi(len(data)) + data


def log_record(data):
    return struct.pack('<IHB', tool.masked_crc32c(b'\x01' + data), len(data), 1) + data


class ToolTests(unittest.TestCase):
    def setUp(self):
        self.root = BASE / ('.tool-test-' + uuid.uuid4().hex)
        self.root.mkdir()
        self.output = self.root / 'output'

    def tearDown(self):
        tool.cleanup_owned(self.root, BASE)

    def microsoft_file(self, count=1):
        data = bytearray(0x2400 + 60)
        data[:12] = bytes.fromhex('55aa88810200600055aa55aa')
        struct.pack_into('<I', data, 12, count)
        data[0x2400 + 10] = 2
        data[0x2400 + 11] = 0x5a
        data[0x2400 + 12:0x2400 + 16] = '你好'.encode('utf-16le')
        struct.pack_into('<HH', data, 0x2400 + 16, tool.MS_PINYIN.index('ni'), tool.MS_PINYIN.index('hao'))
        path = self.root / '用户词库.dat'
        path.write_bytes(data)
        return path

    def wechat_folder(self, missing_log=False):
        folder = self.root / '微信账号'
        folder.mkdir()
        (folder / 'CURRENT').write_text('MANIFEST-000001\n', encoding='ascii')
        manifest = b'\x01' + lp(b'leveldb.BytewiseComparator') + b'\x02\x01\x03\x02\x04\x00'
        (folder / 'MANIFEST-000001').write_bytes(log_record(manifest))
        if not missing_log:
            (folder / '000001.log').write_bytes(b'')
        return folder

    def test_microsoft_reading_and_no_overwrite(self):
        source = self.microsoft_file()
        before = source.read_bytes()
        first = tool.export_dictionaries({'microsoft': source}, self.output, details=False)
        second = tool.export_dictionaries({'microsoft': source}, self.output, details=False)
        self.assertNotEqual(first['output_directory'], second['output_directory'])
        self.assertEqual(source.read_bytes(), before)
        with (Path(first['output_directory']) / 'microsoft_migration.tsv').open(encoding='utf-8-sig', newline='') as f:
            self.assertEqual(list(csv.DictReader(f, delimiter='\t')),
                             [dict(word='你好', pinyin='ni hao', source_weight_raw_max='', source_records='1')])
        self.assertEqual(len(first['files']), 2)

    def test_failed_batch_does_not_publish_partial_files(self):
        good = self.microsoft_file()
        bad = self.root / '错误.bin'
        bad.write_bytes(b'wrong')
        self.output.mkdir()
        marker = self.output / '已有文件.txt'
        marker.write_text('keep', encoding='utf-8')
        with self.assertRaises(tool.FormatError):
            tool.export_dictionaries({'microsoft': good, 'sogou': bad}, self.output)
        self.assertEqual([p.name for p in self.output.iterdir()], ['已有文件.txt'])
        self.assertEqual(marker.read_text(encoding='utf-8'), 'keep')

    def test_truncated_microsoft_rejected(self):
        path = self.microsoft_file(count=2)
        with self.assertRaises(tool.FormatError):
            tool.export_dictionaries({'microsoft': path}, self.output)
        self.assertEqual(list(self.output.iterdir()), [])

    def test_empty_wechat_has_all_headers(self):
        source = self.wechat_folder()
        result = tool.export_dictionaries({'wechat': source}, self.output)
        self.assertEqual(result['results']['wechat']['word_pinyin_pairs'], 0)
        self.assertEqual(len(result['files']), 7)
        self.assertTrue(all(f['rows'] == 0 for f in result['files']))
        self.assertTrue(all(p.name != '.wechat-input' for p in Path(result['output_directory']).iterdir()))

    def test_wal_only_put_and_delete(self):
        source = self.wechat_folder()
        key = b'!u_d_v_p!ni,hao\x01' + '你好'.encode('utf-8')
        put = struct.pack('<QI', 10, 1) + b'\x01' + lp(key) + lp(b'metadata')
        (source / '000001.log').write_bytes(log_record(put))
        result = tool.export_dictionaries({'wechat': source}, self.output)
        self.assertEqual(result['results']['wechat']['word_pinyin_pairs'], 1)
        delete = struct.pack('<QI', 11, 1) + b'\x00' + lp(key)
        (source / '000001.log').write_bytes(log_record(put) + log_record(delete))
        result = tool.export_dictionaries({'wechat': source}, self.output)
        self.assertEqual(result['results']['wechat']['word_pinyin_pairs'], 0)
        self.assertEqual(result['results']['wechat']['deleted_keys_excluded'], 1)

    def test_missing_log_requires_recovery_and_records_warning(self):
        source = self.wechat_folder(missing_log=True)
        with self.assertRaises(tool.FormatError):
            tool.export_dictionaries({'wechat': source}, self.output)
        self.assertEqual(list(self.output.iterdir()), [])
        result = tool.export_dictionaries({'wechat': source}, self.output, recovery=True)
        self.assertTrue(result['results']['wechat']['warnings'])
        report = json.loads((Path(result['output_directory']) / 'export_report.json').read_text(encoding='utf-8'))
        self.assertTrue(report['results']['wechat']['warnings'])

    def test_source_folder_cannot_be_output(self):
        source = self.wechat_folder()
        with self.assertRaises(tool.FormatError):
            tool.export_dictionaries({'wechat': source}, source / 'results')
        self.assertFalse((source / 'results').exists())

    def test_empty_sogou_does_not_crash(self):
        data = bytearray(84)
        data[:4] = b'SGPU'
        struct.pack_into('<6I', data, 0x38, 84, 0, 0, 84, 0, 0)
        source = self.root / 'empty.bin'
        source.write_bytes(data)
        result = tool.export_dictionaries({'sogou': source}, self.output)
        self.assertEqual(result['results']['sogou']['word_pinyin_pairs'], 0)

    def test_publication_recovers_from_transient_windows_lock(self):
        error = PermissionError('temporary directory lock')
        error.winerror = 32
        stage = mock.Mock()
        stage.rename.side_effect = [error, None]
        with mock.patch.object(tool.time, 'sleep'):
            tool.publish_directory(stage, self.root / 'final')
        self.assertEqual(stage.rename.call_count, 2)


    def test_combined_import_uses_only_word_and_pinyin(self):
        first = self.root / 'first.tsv'
        second = self.root / 'second.tsv'
        first.write_text('word\tpinyin\tsource_weight_raw_max\tsource_records\n你好\tni hao\t999999\t10\n', encoding='utf-8-sig')
        second.write_text('word\tpinyin\n你好\tni hao\n重庆\tchong qing\n', encoding='utf-8-sig')
        output = self.root / 'coral.tsv'
        self.assertEqual(tool.convert_tsv_files([first, second], output), 2)
        with output.open(encoding='utf-8-sig', newline='') as stream:
            reader = csv.DictReader(stream, delimiter='\t')
            self.assertEqual(reader.fieldnames, ['word', 'pinyin'])
            self.assertEqual(len(list(reader)), 2)
        context = self.root / 'context.tsv'
        context.write_text('previous_word\tnext_word\n你好\t世界\n', encoding='utf-8-sig')
        with self.assertRaises(tool.FormatError):
            tool.convert_tsv_files([context], output)

    def test_release_package_contains_standalone_tool(self):
        sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'tools/release'))
        import package_windows
        package = self.root / 'package'
        package.mkdir()
        package_windows.stage_migration_tool(package)
        source = Path(__file__).resolve().parents[2] / 'tools/data/ime_dictionary_tool.py'
        self.assertEqual((package / 'tools/ime_dictionary_tool.py').read_bytes(), source.read_bytes())
        self.assertEqual([p.name for p in (package / 'tools').iterdir()], ['ime_dictionary_tool.py'])

if __name__ == '__main__':
    unittest.main()

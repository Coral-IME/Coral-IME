"""用人工小样本验证私有数据边界、摘要和正式发行门槛。"""

import json
from pathlib import Path
import sys
import tempfile
import unittest
import zipfile
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'tools/release'))
import official_data as DATA
import demo_release as RELEASE


class OfficialDataTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.inputs = self.root / 'inputs'
        self.repository = self.root / 'repository'
        self.output = self.root / 'extracted'
        self.lock_path = self.root / 'lock.json'
        for name in DATA.REQUIRED:
            path = self.inputs / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(('self-authored fixture: ' + name).encode('utf-8'))

    def prepare(self):
        DATA.pack(self.inputs, self.repository, 'fixture-v1')
        self.lock = {
            'format_version': 1, 'repository': 'example/private-data', 'revision': 'b' * 40,
            'data_version': 'fixture-v1', 'archive': 'official-inputs.zip',
            'archive_sha256': DATA.digest(self.repository / 'official-inputs.zip'),
            'manifest_sha256': DATA.digest(self.repository / 'inputs-manifest.json'),
            'model_mode': 'explicit-reuse',
        }
        manifest = json.loads((self.repository / 'inputs-manifest.json').read_text(encoding='utf-8'))
        if manifest.get('character_model'):
            self.lock['character_model'] = manifest['character_model']
        self.write_lock()

    def write_lock(self):
        self.lock_path.write_text(json.dumps(self.lock), encoding='utf-8')

    def test_verified_inputs_preserve_bytes(self):
        self.prepare()
        DATA.unpack(self.repository, self.lock_path, self.output)
        for name in DATA.REQUIRED:
            self.assertEqual((self.output / name).read_bytes(), (self.inputs / name).read_bytes())

    def test_private_configuration_cannot_enter_archive(self):
        config = self.inputs / 'config/credentials.ini'
        config.parent.mkdir()
        config.write_text('synthetic secret fixture', encoding='utf-8')
        with self.assertRaisesRegex(ValueError, 'Unexpected file'):
            DATA.pack(self.inputs, self.repository, 'fixture-v1')
        self.assertFalse(self.repository.exists())

    def test_missing_model_cannot_be_packed(self):
        (self.inputs / 'models/char_lm.dat').unlink()
        with self.assertRaisesRegex(ValueError, 'missing'):
            DATA.pack(self.inputs, self.repository, 'fixture-v1')

    def test_archive_tampering_is_rejected_before_extraction(self):
        self.prepare()
        with (self.repository / 'official-inputs.zip').open('ab') as stream:
            stream.write(b'tampered')
        with self.assertRaisesRegex(ValueError, 'locked checksums'):
            DATA.unpack(self.repository, self.lock_path, self.output)
        self.assertFalse(self.output.exists())

    def test_unexpected_archive_path_cannot_escape(self):
        self.prepare()
        with zipfile.ZipFile(self.repository / 'official-inputs.zip', 'a') as archive:
            archive.writestr('../escaped.txt', b'escape fixture')
        self.lock['archive_sha256'] = DATA.digest(self.repository / 'official-inputs.zip')
        self.write_lock()
        with self.assertRaisesRegex(ValueError, 'inventory'):
            DATA.unpack(self.repository, self.lock_path, self.output)
        self.assertFalse((self.root / 'escaped.txt').exists())
        self.assertFalse(self.output.exists())

    def test_existing_output_and_wrong_version_are_rejected(self):
        self.prepare()
        self.output.mkdir()
        marker = self.output / 'previous.txt'
        marker.write_bytes(b'preserve')
        with self.assertRaises(FileExistsError):
            DATA.unpack(self.repository, self.lock_path, self.output)
        self.assertEqual(marker.read_bytes(), b'preserve')
        self.lock['data_version'] = 'wrong-version'
        self.write_lock()
        with self.assertRaisesRegex(ValueError, 'version'):
            DATA.unpack(self.repository, self.lock_path, self.root / 'new-output')

    def prepare_release(self):
        self.prepare()
        self.package = self.root / 'package'
        self.package.mkdir()
        installer = self.package / 'coralpinyin-window-x64-1.0.0.4.exe'
        installer.write_bytes(b'synthetic installer fixture')
        self.manifest = {
            'complete': True, 'installer_compiled': True, 'version': '1.0.0.4',
            'sign_mode': 'None', 'edition': 'standard', 'manual_launch': True,
            'installer_sha256': RELEASE.digest(installer),
            'runtime_manifest_sha256': 'd' * 64,
            'runtime': {'engine_version': 'a' * 40, 'data_version': 'fixture-v1',
                        'model_mode': 'explicit-reuse', 'installer_bins_match': True},
        }
        self.summary = {'format_version': 1, 'passed': True, 'runtime_manifest_sha256': 'd' * 64, 'tests': [
            {'name': name, 'passed': True, 'exit_code': 0} for name in (
                'sequence_test', 'lexicon_import_engine_test', 'uvmode_engine_test', 'config_gate_test')]}
        self.summary_path = self.root / 'summary.json'
        self.write_release()

    def write_release(self):
        (self.package / 'package-manifest.json').write_text(json.dumps(self.manifest), encoding='utf-8')
        self.summary_path.write_text(json.dumps(self.summary), encoding='utf-8')

    def bundle(self):
        RELEASE.bundle(self.package, self.root / 'release', '1.0.0.4', 'a' * 40,
                       'example/source', 'https://github.com/example/source/actions/runs/123',
                       edition='official', data_lock=self.lock_path, test_summary=self.summary_path)

    def test_official_release_records_data_commit_and_checks(self):
        self.prepare_release()
        self.bundle()
        info = json.loads((self.root / 'release/BUILD_INFO.json').read_text(encoding='utf-8'))
        self.assertEqual(info['edition'], 'official-data-test')
        self.assertEqual(info['data']['revision'], 'b' * 40)
        self.assertTrue(info['formal_data_tests']['passed'])
        self.assertFalse(info['installation_verified'])

    def test_failed_formal_data_check_blocks_release(self):
        self.prepare_release()
        self.summary['tests'][3]['passed'] = False
        self.summary['tests'][3]['exit_code'] = 1
        self.write_release()
        with self.assertRaisesRegex(ValueError, 'all pass'):
            self.bundle()
        self.assertFalse((self.root / 'release').exists())

    def test_results_from_another_runtime_block_release(self):
        self.prepare_release()
        self.summary['runtime_manifest_sha256'] = 'e' * 64
        self.write_release()
        with self.assertRaisesRegex(ValueError, 'all pass'):
            self.bundle()
        self.assertFalse((self.root / 'release').exists())

    def test_mismatched_data_or_source_blocks_release(self):
        self.prepare_release()
        for field, value in (('engine_version', 'c' * 40), ('data_version', 'wrong'),
                             ('model_mode', 'rebuilt'), ('installer_bins_match', False)):
            original = self.manifest['runtime'][field]
            self.manifest['runtime'][field] = value
            self.write_release()
            with self.subTest(field=field), self.assertRaisesRegex(ValueError, 'runtime differs'):
                self.bundle()
            self.manifest['runtime'][field] = original
        self.assertFalse((self.root / 'release').exists())

    def test_character_weights_are_locked_and_preserved(self):
        sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'language_model'))
        from test_character_model import fixture
        from character_model_format import quantize_model
        source = self.root / 'float.coral'
        source.write_bytes(fixture())
        quantize_model(source, self.inputs / DATA.CHARACTER_MODEL)
        self.prepare()
        self.assertEqual(self.lock['character_model']['precision'], 'int8')
        DATA.unpack(self.repository, self.lock_path, self.output)
        self.assertEqual((self.inputs / DATA.CHARACTER_MODEL).read_bytes(), (self.output / DATA.CHARACTER_MODEL).read_bytes())
        self.lock['character_model']['sha256'] = '0' * 64
        self.write_lock()
        with self.assertRaisesRegex(ValueError, 'Character model differs'):
            DATA.unpack(self.repository, self.lock_path, self.root / 'bad-extracted')

    def prepare_neural_release(self):
        self.prepare_release()
        character = {'path': DATA.CHARACTER_MODEL, 'size': 256, 'sha256': '1' * 64, 'precision': 'int8'}
        self.lock['character_model'] = character
        self.write_lock()
        self.manifest['character_model'] = {key: character[key] for key in ('size', 'sha256')}
        self.summary['character_model'] = character
        self.summary['tests'].append({'name': 'character_model_test', 'passed': True, 'exit_code': 0})
        self.write_release()

    def test_neural_release_requires_model_and_native_score_check(self):
        self.prepare_neural_release()
        self.manifest['character_model']['sha256'] = '0' * 64
        self.write_release()
        with self.assertRaisesRegex(ValueError, 'character model differs'):
            self.bundle()
        self.manifest['character_model']['sha256'] = '1' * 64
        self.summary['tests'].pop()
        self.write_release()
        with self.assertRaisesRegex(ValueError, 'all pass'):
            self.bundle()

    def test_neural_release_records_enabled_updates_and_model_hash(self):
        self.prepare_neural_release()
        RELEASE.bundle(self.package, self.root / 'release', '1.0.0.4', 'a' * 40,
                       'example/source', 'https://github.com/example/source/actions/runs/123',
                       edition='official', data_lock=self.lock_path, test_summary=self.summary_path, automatic_updates=True)
        info = json.loads((self.root / 'release/BUILD_INFO.json').read_text(encoding='utf-8'))
        self.assertTrue(info['automatic_updates'])
        self.assertEqual(info['character_model'], self.lock['character_model'])

    def prepare_optimized(self):
        from test_compact_tables import fixture as table_fixture
        sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'language_model'))
        from test_character_model import fixture as character_fixture
        from character_model_format import quantize_model
        from build_pair_bonus import compile_source
        # 新格式只收集明确允许的运行资源，不携带本机配置和历史概率源。
        for name in DATA.OPTIMIZED_REQUIRED:
            path = self.root / 'optimized-inputs' / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(('self-authored fixture: ' + name).encode())
        self.inputs = self.root / 'optimized-inputs'
        (self.inputs / 'source/pair-bonuses.tsv').write_text('prev_word\tnext_word\tbonus\n甲\t乙\t1.25\n', encoding='utf-8')
        general = self.inputs / 'runtime/data/generallexicon'
        (general / 'mainlexicon.coral').write_bytes(table_fixture()[0])
        (general / 'languagemodel.coral').write_bytes(compile_source(self.inputs / 'source/pair-bonuses.tsv'))
        character = self.root / 'character.coral'
        character.write_bytes(character_fixture())
        (self.inputs / DATA.CHARACTER_MODEL).unlink()
        quantize_model(character, self.inputs / DATA.CHARACTER_MODEL)
        DATA.pack(self.inputs, self.repository, 'optimized-v1', DATA.PAIR_MODE)
        manifest = json.loads((self.repository / 'inputs-manifest.json').read_text(encoding='utf-8'))
        self.lock = {key: value for key, value in manifest.items() if key != 'files'}
        self.lock.update(repository='example/private-data', revision='b' * 40, archive='official-inputs.zip',
                         archive_sha256=DATA.digest(self.repository / 'official-inputs.zip'),
                         manifest_sha256=DATA.digest(self.repository / 'inputs-manifest.json'))
        self.write_lock()

    def test_optimized_inputs_stage_exact_tables_and_public_configuration(self):
        self.prepare_optimized()
        DATA.unpack(self.repository, self.lock_path, self.output)
        config = self.root / 'config'
        (config / 'doublepinyin').mkdir(parents=True)
        (config / 'coralpinyinConfig.ini').write_text('[app]\nVersion=0.0.0.0\n[settingsconfig]\nsmartCandidates=true\n', encoding='utf-8')
        runtime = self.root / 'runtime'
        with patch('subprocess.run') as native:
            DATA.stage_optimized(self.output, self.lock_path, runtime, config, 'a' * 40, '1.0.0.6', Path(sys.executable))
            native.assert_called_once()
        for name in self.lock['precompiled_tables']['files']:
            self.assertEqual((runtime / 'data/generallexicon' / name).read_bytes(),
                             (self.inputs / 'runtime/data/generallexicon' / name).read_bytes())
        record = json.loads((runtime / 'build-record.json').read_text())
        self.assertEqual(record['engine_version'], 'a' * 40)
        self.assertEqual(record['model_mode'], DATA.PAIR_MODE)

    def test_optimized_tampered_dictionary_source_blocks_staging(self):
        self.prepare_optimized()
        DATA.unpack(self.repository, self.lock_path, self.output)
        (self.output / 'source/coral_lexicon.csv').write_bytes(b'changed after extraction')
        with self.assertRaisesRegex(ValueError, 'Selected input differs'):
            DATA.stage_optimized(self.output, self.lock_path, self.root / 'runtime', self.root / 'config',
                                 'a' * 40, '1.0.0.6', Path(sys.executable))
        self.assertFalse((self.root / 'runtime').exists())

    def test_optimized_release_requires_exact_native_restoration(self):
        self.prepare_release()
        self.lock.update(format_version=2, model_mode=DATA.PAIR_MODE,
                         precompiled_tables={'format': 'CBGN0001', 'files': {
                             name: {'size': 128, 'sha256': 'e' * 64}
                             for name in ('mainlexicon.coral', 'languagemodel.coral')}})
        self.write_lock()
        self.manifest['runtime']['model_mode'] = DATA.PAIR_MODE
        tables = dict(self.lock['precompiled_tables'], storage='compact',
                      native_load_verified=True, installer_restoration_verified=True)
        self.manifest['precompiled_tables'] = tables
        tables['installer_restoration_verified'] = False
        self.write_release()
        with self.assertRaisesRegex(ValueError, 'verify native restoration'):
            self.bundle()
        tables['installer_restoration_verified'] = True
        self.write_release()
        self.bundle()


if __name__ == '__main__':
    unittest.main()

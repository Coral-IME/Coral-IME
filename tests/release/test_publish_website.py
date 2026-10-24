"""公开人工发行夹具验证上传前的包身份、模型和摘要门槛。"""
import json
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'tools/release'))
from publish_website import verified_release, digest, NoRedirect


class PublishWebsiteTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.name = 'coralpinyin-official-test-windows-x64-1.0.0.5.exe'
        (self.root / self.name).write_bytes(b'MZ synthetic release installer')
        model = {'path': 'models/character_model.coral', 'size': 256, 'sha256': 'c' * 64, 'precision': 'int8'}
        self.info = {'version': '1.0.0.5', 'edition': 'official-data-test', 'automatic_updates': True,
                     'source_sha': 'a' * 40, 'repository': 'Coral-IME/Coral-IME', 'character_model': model,
                     'formal_data_tests': {'passed': True}, 'installer': {'name': self.name, 'sha256': digest(self.root / self.name)}}
        self.package = {'version': '1.0.0.5', 'complete': True, 'installer_sha256': self.info['installer']['sha256'],
                        'character_model': {key: model[key] for key in ('size', 'sha256')}}
        self.write()

    def write(self):
        (self.root / 'BUILD_INFO.json').write_text(json.dumps(self.info), encoding='utf-8')
        (self.root / 'package-manifest.json').write_text(json.dumps(self.package), encoding='utf-8')
        (self.root / 'SHA256SUMS.txt').write_text(''.join(digest(self.root / name) + '  ' + name + '\n'
            for name in (self.name, 'BUILD_INFO.json', 'package-manifest.json')), encoding='utf-8')

    def test_verified_installer_is_selected(self):
        path, info = verified_release(self.root, '1.0.0.5')
        self.assertEqual(path.name, self.name)
        self.assertEqual(info['character_model']['precision'], 'int8')

    def test_missing_model_or_disabled_updates_blocks_deployment(self):
        self.info['automatic_updates'] = False
        self.write()
        with self.assertRaises(ValueError):
            verified_release(self.root, '1.0.0.5')
        self.info['automatic_updates'] = True
        self.package.pop('character_model')
        self.write()
        with self.assertRaisesRegex(ValueError, 'INT8'):
            verified_release(self.root, '1.0.0.5')

    def test_changed_installer_and_unsafe_name_are_rejected(self):
        (self.root / self.name).write_bytes(b'MZ modified installer')
        with self.assertRaisesRegex(ValueError, 'checksum'):
            verified_release(self.root, '1.0.0.5')
        self.info['installer']['name'] = '../outside.exe'
        self.write()
        with self.assertRaises(ValueError):
            verified_release(self.root, '1.0.0.5')

    def test_redirect_does_not_forward_deployment_credentials(self):
        with self.assertRaisesRegex(ValueError, 'redirects'):
            NoRedirect().redirect_request(None, None, 302, '', {}, 'https://other.example/')

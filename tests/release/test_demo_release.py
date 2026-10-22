"""验证发行时拒绝错误版本、损坏安装包和混入旧输出。"""

import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest


SPEC = importlib.util.spec_from_file_location(
    'demo_release', Path(__file__).resolve().parents[2] / 'tools/release/demo_release.py')
RELEASE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(RELEASE)


class DemoReleaseTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.package = self.root / 'package'
        self.package.mkdir()
        self.output = self.root / 'release'
        self.installer = self.package / 'coralpinyin-window-x64-0.0.0.0.exe'
        self.installer.write_bytes(b'installer fixture')
        self.manifest = {
            'complete': True, 'installer_compiled': True, 'version': '0.0.0.0',
            'sign_mode': 'None', 'edition': 'demo', 'installer_sha256': RELEASE.digest(self.installer),
        }
        self.write_manifest()

    def write_manifest(self):
        (self.package / 'package-manifest.json').write_text(
            json.dumps(self.manifest), encoding='utf-8')

    def bundle(self):
        RELEASE.bundle(self.package, self.output, '0.0.0.0', 'a' * 40,
                       'Coral-IME/Coral-IME',
                       'https://github.com/Coral-IME/Coral-IME/actions/runs/123')

    def test_version_boundaries(self):
        for value in ('0.0.0.0', '1.0.0.3', '65535.65535.65535.65535'):
            self.assertEqual(RELEASE.app_version(value), value)
        for value in ('1.2.3', '1.2.3.65536', '01.0.0.0', '-1.0.0.0', '1.0.0.0;echo bad'):
            with self.subTest(value=value), self.assertRaises(argparse.ArgumentTypeError):
                RELEASE.app_version(value)

    def test_download_files_and_checksums(self):
        self.bundle()
        info = json.loads((self.output / 'BUILD_INFO.json').read_text(encoding='utf-8'))
        self.assertEqual(info['source_sha'], 'a' * 40)
        self.assertEqual(info['edition'], 'demo')
        self.assertFalse(info['signed'])
        self.assertFalse(info['installation_verified'])
        entries = (self.output / 'SHA256SUMS.txt').read_text().splitlines()
        self.assertEqual(len(entries), 4)
        for entry in entries:
            checksum, name = entry.split('  ')
            self.assertEqual(hashlib.sha256((self.output / name).read_bytes()).hexdigest(), checksum)

    def test_tampered_installer_is_rejected(self):
        self.installer.write_bytes(b'changed after packaging')
        with self.assertRaisesRegex(ValueError, 'SHA-256'):
            self.bundle()
        self.assertFalse(self.output.exists())

    def test_wrong_version_or_incomplete_package_is_rejected(self):
        for field, value in (('version', '1.0.0.3'), ('complete', False),
                             ('installer_compiled', False), ('sign_mode', 'LocalTest'),
                             ('edition', 'standard')):
            original = self.manifest[field]
            self.manifest[field] = value
            self.write_manifest()
            with self.subTest(field=field), self.assertRaises(ValueError):
                self.bundle()
            self.manifest[field] = original
            self.assertFalse(self.output.exists())

    def test_existing_output_is_preserved(self):
        self.output.mkdir()
        marker = self.output / 'previous-installer.exe'
        marker.write_bytes(b'previous release')
        with self.assertRaises(FileExistsError):
            self.bundle()
        self.assertEqual(marker.read_bytes(), b'previous release')
        self.assertEqual(list(self.output.iterdir()), [marker])

    def test_unrelated_build_url_is_rejected(self):
        with self.assertRaisesRegex(ValueError, 'build URL'):
            RELEASE.bundle(self.package, self.output, '0.0.0.0', 'a' * 40,
                           'Coral-IME/Coral-IME', 'https://example.com/build/123')
        self.assertFalse(self.output.exists())


if __name__ == '__main__':
    unittest.main()

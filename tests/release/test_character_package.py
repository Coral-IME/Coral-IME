"""人工模型检查漏带、摘要不符和无模型兼容行为。"""
import copy
from pathlib import Path
import sys
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'tools/release'))
sys.path.insert(0,str(ROOT/'tests/language_model'))
from package_windows import package_character_model
from runtime_manifest import character_model_info
from test_character_model import fixture
from character_model_format import quantize_model

class CharacterPackageTest(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root=Path(self.temp.name)
        self.model=self.root/'data/generallexicon/character_model.coral'
        self.model.parent.mkdir(parents=True)

    def write_model(self):
        self.model.write_bytes(fixture())
        return {'character_model':character_model_info(self.model)}

    def test_missing_required_model_and_legacy_package(self):
        self.assertIsNone(package_character_model(self.root,{}))
        with self.assertRaisesRegex(ValueError,'missing'):
            package_character_model(self.root,{},True)

    def test_recorded_model_is_selected(self):
        record=self.write_model()
        self.assertEqual(package_character_model(self.root,record,True),record['character_model'])

    def test_quantized_model_is_selected(self):
        source=self.root/'float.coral';source.write_bytes(fixture())
        quantize_model(source,self.model)
        record={'character_model':character_model_info(self.model)}
        self.assertEqual(package_character_model(self.root,record,True),record['character_model'])

    def test_unrecorded_or_changed_model_is_rejected(self):
        record=self.write_model()
        with self.assertRaisesRegex(ValueError,'provenance'):
            package_character_model(self.root,{},True)
        stale=copy.deepcopy(record)
        stale['character_model']['sha256']='0'*64
        with self.assertRaisesRegex(ValueError,'provenance'):
            package_character_model(self.root,stale)
        self.model.unlink()
        with self.assertRaisesRegex(ValueError,'missing'):
            package_character_model(self.root,record)

    def test_corrupt_container_is_rejected(self):
        valid=fixture()
        for data in (b'not a model',valid+b'tail',
                     b'BADMAGIC'+valid[8:],valid[:-1]+bytes([valid[-1]^1])):
            self.model.write_bytes(data)
            with self.subTest(size=len(data)),self.assertRaises(ValueError):
                character_model_info(self.model)

if __name__=='__main__':
    unittest.main()

"""验证用户改词对后冻结、收集与拒绝过期输入的打包契约。"""
import argparse
import configparser
import hashlib
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'tools/release'))
sys.path.insert(0,str(ROOT/'tools/data'))
sys.path.insert(0,str(ROOT/'tests/language_model'))
import prepare_optimized_runtime as tool
from build_pair_bonus import compile_source, encode_pairs, inspect_pairs
from runtime_manifest import ENGINE_FILES
from package_windows import precompiled_tables_info, package_candidate_ranker, verify_ranking_build
from candidate_ranker_format import FEATURE_NAMES, export_model
from character_model_format import quantize_model
from test_character_model import fixture


class PairPackagingTest(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory(prefix='coral-pair-package-')
        self.addCleanup(self.temp.cleanup)
        self.root=Path(self.temp.name)
        self.source=self.root/'source'
        for relative in ENGINE_FILES.values():
            path=self.source/relative;path.parent.mkdir(parents=True,exist_ok=True)
            path.write_bytes(b'synthetic resource')
        self.lex=self.source/'data/generallexicon/mainlexicon.coral'
        self.lex.write_bytes(b'current dictionary identity')
        # 旧模型故意不是合法格式，独立词对源启用后不得再被它阻塞。
        (self.source/'data/generallexicon/languagemodel.coral').write_bytes(b'obsolete probability model')
        config=self.source/'config/coralpinyinConfig.ini';config.parent.mkdir()
        config.write_text('[app]\nVersion=1.0.0.1\n[settingsconfig]\nsmartCandidates=true\n[LexiconFiles]\n',encoding='utf-8')
        self.character=self.root/'character.coral';self.character.write_bytes(fixture())
        self.pairs=self.root/'pairs.tsv';self.pairs.write_text('prev_word\tnext_word\tbonus\n甲\t乙\t1.25\n',encoding='utf-8')
        self.work=self.root/'work';self.work.mkdir()
        self.args=argparse.Namespace(source_runtime=self.source,character_model=self.character,
            version='1.0.0.1',work_root=self.work,selection_file=self.work/'selection.json',
            pair_bonus_source=self.pairs,output_dir=self.work/'runtime',native_probe=Path(sys.executable))

    def test_compile_and_stage_without_rebuilding_dictionary(self):
        before=tool.resource_inventory(self.source)
        tool.check(self.args,ROOT)
        selected=json.loads(self.args.selection_file.read_text(encoding='utf-8'))
        self.assertEqual(selected['tables']['format'],'CBGN0001')
        self.assertEqual(selected['tables']['explicit_arcs'],1)
        self.assertEqual(Path(selected['compiled_model']).read_bytes(),compile_source(self.pairs))
        with patch.object(tool.subprocess,'run') as native:
            tool.stage(self.args,ROOT)
            native.assert_called_once()
        self.assertEqual((self.args.output_dir/'data/generallexicon/languagemodel.coral').read_bytes(),compile_source(self.pairs))
        self.assertEqual((self.args.output_dir/'data/generallexicon/mainlexicon.coral').read_bytes(),self.lex.read_bytes())
        self.assertEqual(tool.resource_inventory(self.source),before)
        cfg=configparser.ConfigParser();cfg.read(self.args.output_dir/'config/coralpinyinConfig.ini')
        self.assertTrue(cfg.getboolean('settingsconfig','smartCandidates'))

    def test_edit_after_preflight_requires_new_freeze(self):
        tool.check(self.args,ROOT)
        self.pairs.write_text('prev_word\tnext_word\tbonus\n甲\t乙\t0.5\n',encoding='utf-8')
        with self.assertRaisesRegex(ValueError,'Pair source changed'):
            tool.stage(self.args,ROOT)
        self.assertFalse(self.args.output_dir.exists())

    def test_dictionary_change_does_not_require_reestimating_pairs(self):
        first=compile_source(self.pairs)
        model=self.root/'model';model.write_bytes(first)
        one=precompiled_tables_info(self.lex.parent,model)
        self.lex.write_bytes(b'different dictionary numbering and frequencies')
        two=precompiled_tables_info(self.lex.parent,model)
        self.assertNotEqual(one['files']['mainlexicon.coral'],two['files']['mainlexicon.coral'])
        self.assertEqual(one['files']['languagemodel.coral'],two['files']['languagemodel.coral'])
        self.assertEqual(first,compile_source(self.pairs))

    def test_corrupt_bonus_cannot_be_packaged(self):
        model=bytearray(encode_pairs([('甲','乙',1.)]))
        model[-1]^=1
        with self.assertRaisesRegex(ValueError,'checksum'):inspect_pairs(model)

    def test_package_retains_selected_source_provenance(self):
        tool.check(self.args,ROOT)
        selected=json.loads(self.args.selection_file.read_text(encoding='utf-8'))
        package={'complete':True,'version':selected['version'],'files':{},
            'pair_bonus_source':selected['pair_bonus_source'],
            'precompiled_tables':{**selected['tables'],'storage':'compact',
                'installer_restoration_verified':True,'native_load_verified':True}}
        manifest=self.work/'package-manifest.json';self.args.package_manifest=manifest
        manifest.write_text(json.dumps(package),encoding='utf-8')
        tool.verify_package(self.args,ROOT)
        package['pair_bonus_source']={**package['pair_bonus_source'],'sha256':'0'*64}
        manifest.write_text(json.dumps(package),encoding='utf-8')
        with self.assertRaisesRegex(ValueError,'provenance'):
            tool.verify_package(self.args,ROOT)

    def select_ranker(self):
        source=self.character;self.character=self.root/'int8.coral'
        quantize_model(source,self.character);self.args.character_model=self.character
        self.ranker=self.root/'candidate_ranker.coral'
        export_model(dict(objective='lambdarank',feature_names=FEATURE_NAMES,
            tree_info=[dict(tree_structure=dict(leaf_value=1.))]),self.ranker)
        self.args.candidate_ranker=self.ranker
        self.args.ranker_probe=Path(sys.executable)
        tool.check(self.args,ROOT)
        selected=json.loads(self.args.selection_file.read_text(encoding='utf-8'))
        profile=dict(format_version=1,candidate_ranker_sha256=selected['candidate_ranker']['sha256'],
            character_model_sha256=selected['character_model_info']['sha256'],
            dictionary_sha256=selected['tables']['files']['mainlexicon.coral']['sha256'],
            pair_bonus_sha256=selected['tables']['files']['languagemodel.coral']['sha256'],
            character_weight=.5,search_paths=32,neural_paths=8,rerank_threshold=2)
        self.args.ranking_profile=self.root/'ranking-profile.json'
        self.args.ranking_profile.write_text(json.dumps(profile),encoding='utf-8')
        tool.check(self.args,ROOT)

    def test_ranker_profile_stage_and_build_parameters(self):
        self.select_ranker()
        selected=json.loads(self.args.selection_file.read_text(encoding='utf-8'))
        with patch.object(tool.subprocess,'run') as native:
            tool.stage(self.args,ROOT)
            self.assertEqual(native.call_count,2)
        record=json.loads((self.args.output_dir/'build-record.json').read_text(encoding='utf-8'))
        self.assertEqual(package_candidate_ranker(self.args.output_dir,record,True),selected['candidate_ranker'])
        self.assertEqual((self.args.output_dir/'data/generallexicon/candidate_ranker.coral').read_bytes(),self.ranker.read_bytes())
        build=self.root/'build';build.mkdir()
        cache=build/'CMakeCache.txt'
        text='CORAL_ENABLE_CHARACTER_MODEL:BOOL=ON\nCORAL_CHARACTER_RERANK_PATHS:STRING=8\nCORAL_CHARACTER_RERANK_THRESHOLD:STRING=2\n'
        cache.write_text(text,encoding='utf-8');verify_ranking_build(build,record)
        cache.write_text(text.replace('THRESHOLD:STRING=2','THRESHOLD:STRING=3'),encoding='utf-8')
        with self.assertRaisesRegex(ValueError,'parameter mismatch'):verify_ranking_build(build,record)

    def test_changed_ranker_or_profile_rejected_before_staging(self):
        self.select_ranker()
        raw=self.ranker.read_bytes();self.ranker.write_bytes(raw[:-1]+bytes([raw[-1]^1]))
        with self.assertRaisesRegex(ValueError,'checksum'):tool.stage(self.args,ROOT)
        self.assertFalse(self.args.output_dir.exists())
        self.ranker.write_bytes(raw)
        self.args.ranking_profile.write_text('{}',encoding='utf-8')
        with self.assertRaisesRegex(ValueError,'profile changed'):tool.stage(self.args,ROOT)
        self.assertFalse(self.args.output_dir.exists())

    def test_ranker_missing_or_unrecorded_is_rejected(self):
        self.assertIsNone(package_candidate_ranker(self.source,{}))
        with self.assertRaisesRegex(ValueError,'missing'):package_candidate_ranker(self.source,{},True)
        self.select_ranker()
        target=self.source/'data/generallexicon/candidate_ranker.coral';target.write_bytes(self.ranker.read_bytes())
        with self.assertRaisesRegex(ValueError,'provenance'):package_candidate_ranker(self.source,{})
        profile=json.loads(self.args.ranking_profile.read_text(encoding='utf-8'));profile['dictionary_sha256']='0'*64
        self.args.ranking_profile.write_text(json.dumps(profile),encoding='utf-8')
        with self.assertRaisesRegex(ValueError,'does not match'):tool.check(self.args,ROOT)

    def test_package_rejects_missing_ranker_payload(self):
        self.select_ranker()
        selected=json.loads(self.args.selection_file.read_text(encoding='utf-8'))
        package=dict(complete=True,version=selected['version'],pair_bonus_source=selected['pair_bonus_source'],
            candidate_ranker=selected['candidate_ranker'],ranking_profile=selected['ranking_profile'],
            precompiled_tables=dict(selected['tables'],storage='compact',installer_restoration_verified=True,native_load_verified=True),
            files={'data/generallexicon/candidate_ranker.coral':{key:selected['candidate_ranker'][key] for key in ('size','sha256')}})
        self.args.package_manifest=self.work/'package-manifest.json'
        self.args.package_manifest.write_text(json.dumps(package),encoding='utf-8');tool.verify_package(self.args,ROOT)
        package['files'].clear();self.args.package_manifest.write_text(json.dumps(package),encoding='utf-8')
        with self.assertRaisesRegex(ValueError,'payload'):tool.verify_package(self.args,ROOT)


if __name__=='__main__':unittest.main()

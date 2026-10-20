"""Regression for the indexed CSV path and parity with the direct C++ builder.

Run: python tests/mixed_language/test_pipeline.py [path/to/coral_lexicon_builder.exe]
"""
import csv
import importlib.util
import math
import lzma
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("coral_index", ROOT / "tools/data/index_lexicon.py")
indexer = importlib.util.module_from_spec(spec)
spec.loader.exec_module(indexer)
sys.path.insert(0, str(ROOT / "tools/data"))
pack_spec = importlib.util.spec_from_file_location("coral_pack", ROOT / "tools/data/encode_dictpack.py")
packer = importlib.util.module_from_spec(pack_spec)
pack_spec.loader.exec_module(packer)
PBIN = Path(sys.argv.pop(1)).resolve() if len(sys.argv) > 1 else None


class PipelineTest(unittest.TestCase):
    def test_shared_identity_and_direct_builder_parity(self):
        with tempfile.TemporaryDirectory(prefix="coral-mixed-pipeline-") as temporary:
            directory = Path(temporary)
            source = directory / "l.csv"
            english = directory / "eng_lexicon.csv"
            indexed = directory / "indexed.csv"
            source.write_text("pinyin,frequency,word,fullPinyin\n"
                "yong,1000000,用,yong\nti'me,5000,Time,ti'me\n"
                "t'i'm'e,9000,time,t'i'm'e\nxie,1000000,写,xie\n", encoding="utf-8")
            english.write_text("search_word,display_word,frequency,can_split\ntime,time,26,1\npython,Python,6770,0\nrare,Rare,1,0\n", encoding="utf-8")
            self.assertTrue(indexer.add_index_and_filter_fields(source, indexed))
            indexer.merge_english_vocabulary(str(indexed), english)
            with indexed.open(encoding="utf-8", newline="") as stream:
                next(stream)
                count = int(next(stream))
                rows = list(csv.DictReader(stream))
            self.assertEqual(count, len(rows))
            times = [row for row in rows if row["word"].lower() == "time"]
            self.assertEqual({int(row["index"]) for row in times}, {1})
            self.assertEqual({int(row["frequency"]) for row in times}, {9000})
            python = next(row for row in rows if row["word"] == "Python")
            self.assertEqual(int(python["index"]), 4)
            self.assertEqual(python["pinyin"], "~en:python")
            self.assertEqual(int(next(row for row in rows if row["word"] == "写")["index"]), 3)

            if PBIN is None:
                return
            bigram = directory / "m.csv"
            bigram.write_text("prob,prev_word,next_word,prev_fullPinyin,next_fullPinyin\n"
                "-0.1,用,python,yong,python\n-0.2,PYTHON,写,python,xie\n", encoding="utf-8")
            subprocess.run([str(PBIN), "--csv", str(source), str(bigram), str(directory), str(english)], check=True)
            data = (directory / "mainlexicon.coral").read_bytes()
            header = struct.unpack_from("<8s14I", data)
            entries_count, entries_offset, pool_offset = header[3], header[6], header[7]
            identities = {}
            for i in range(entries_count):
                word_offset, word_len, _, _, token, weight, _, _ = struct.unpack_from("<5I2fI", data, entries_offset + i * 32)
                word = data[pool_offset + word_offset:pool_offset + word_offset + word_len].decode("utf-8")
                identities.setdefault(word, set()).add((token, round(weight, 5)))
            for row in rows:
                expected = (int(row["index"]), round(math.log(int(row["frequency"]) / 1e9), 5))
                self.assertIn(expected, identities[row["word"]])
            model = (directory / "languagemodel.coral").read_bytes()
            h = struct.unpack_from("<8s14I", model)
            uni_offset = h[11]
            for word in ("Python", "Rare"):
                token = int(next(row for row in rows if row["word"] == word)["index"])
                state, weight = struct.unpack_from("<if", model, uni_offset + token * 8)
                self.assertGreaterEqual(state, 0, "English-only and rare English have model states")
                self.assertLess(weight, 0)

            # 精简安装包在没有英文源参数的情况下从压缩包重建词表。
            # 必须产生完全相同的 ID、词频与二元组，
            # 包括大小写不敏感的英文词对。
            with source.open(encoding="utf-8", newline="") as stream:
                bundle_rows = [dict(row, frequency=int(row["frequency"]), identifier=0,
                    isPolyphonic=False, inM2=False) for row in csv.DictReader(stream)]
            packer.append_english_vocabulary(bundle_rows, english)
            raw, stats = packer.encode_bundle(bundle_rows, bigram)
            self.assertEqual(stats["rows"], 2)
            compressed = directory / "bundle.xz"
            compressed.write_bytes(lzma.compress(raw, preset=1))
            decoded = directory / "decoded"
            decoded.mkdir()
            subprocess.run([str(PBIN.parent / "coral_bundle_decoder.exe"), str(compressed),
                str(decoded / "l.csv"), str(decoded / "m.csv")], check=True)
            subprocess.run([str(PBIN), "--csv", str(decoded / "l.csv"),
                str(decoded / "m.csv"), str(decoded)], check=True)
            self.assertEqual((decoded / "mainlexicon.coral").read_bytes(), data)
            self.assertEqual((decoded / "languagemodel.coral").read_bytes(), model)

    def test_invalid_english_does_not_replace_indexed_file(self):
        with tempfile.TemporaryDirectory(prefix="coral-mixed-invalid-") as temporary:
            directory = Path(temporary)
            source = directory / "l.csv"
            english = directory / "eng_lexicon.csv"
            indexed = directory / "indexed.csv"
            source.write_text("pinyin,frequency,word,fullPinyin\nai,1000,爱,ai\n", encoding="utf-8")
            english.write_text("search_word,display_word,frequency,can_split\nbad,bad,0,0\n", encoding="utf-8")
            self.assertTrue(indexer.add_index_and_filter_fields(source, indexed))
            before = indexed.read_bytes()
            with self.assertRaises(ValueError):
                indexer.merge_english_vocabulary(str(indexed), english)
            self.assertEqual(indexed.read_bytes(), before)


if __name__ == "__main__":
    unittest.main()

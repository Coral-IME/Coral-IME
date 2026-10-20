#!/usr/bin/env python3
"""
DictPack v2 Encoder

CLI: encode_dictpack.py coral_lexicon.csv bundle_coral_languagemodel.csv output.dictpack.lzma2
The paired M2 input must come from index_bigram.py. Legacy v1 codec functions
remain available internally; the public CLI emits the explicit v2 bundle.

Bundle v2 stores:
  1) the already-tested DictPack v1 representation of coral_lexicon.csv;
  2) an M2 block where prev/next word+fullPinyin are preferably replaced by
     dense references to l_sort rows marked in_m2=1 (with automatic fallback
     to direct l_sort row IDs if the flag is not perfectly aligned);
  3) prob is encoded as a fixed-point signed integer when the whole column
     uses one fixed number of decimal places, otherwise it falls back to the
     original UTF-8 token;
  4) the whole bundle is compressed once with LZMA/XZ preset=9|EXTREME.
"""
import sys
sys.dont_write_bytecode=True

import csv
import lzma
import os
import re
import struct
import sys
from index_lexicon import english_identity_key, read_english_vocabulary

# ---------------------------------------------------------------------------
# 既有的 l_sort DictPack v1 格式 —— 刻意保持不变
# ---------------------------------------------------------------------------

LSORT_MAGIC = b"KDICTPK\x00"
LSORT_VERSION = 1
LSORT_HEADER_SIZE = 152  # 8(magic) + 17*8（uint64 字段）

# ---------------------------------------------------------------------------
# 新的外层 bundle + M2 块
# ---------------------------------------------------------------------------

BUNDLE_MAGIC = b"KDBNDL2\x00"
BUNDLE_VERSION = 2
BUNDLE_HEADER_SIZE = 64  # magic + 7 * uint64

M2_MAGIC = b"KM2PACK\x00"
M2_VERSION = 1
M2_HEADER_SIZE = 96  # magic + 11 * uint64

# M2 flags
M2_FLAG_DENSE_INM2_REFS = 1 << 0
M2_FLAG_FIXED_DECIMAL_PROB = 1 << 1
M2_FLAG_DELTA_REFS = 1 << 2

_FIXED_DECIMAL_RE = re.compile(r"^-?(?:0|[1-9][0-9]*)(?:\.([0-9]+))?$")


# ---------------------------------------------------------------------------
# 整数编解码
# ---------------------------------------------------------------------------

def uleb128_encode(value):
    if value < 0:
        raise ValueError("ULEB128 cannot encode a negative value")
    buf = bytearray()
    while True:
        b = value & 0x7F
        value >>= 7
        if value:
            b |= 0x80
        buf.append(b)
        if not value:
            break
    return bytes(buf)


def zigzag_encode(value):
    return value * 2 if value >= 0 else (-value * 2 - 1)


# ---------------------------------------------------------------------------
# Bitset
# ---------------------------------------------------------------------------

def make_bitset(values):
    n = len(values)
    bs = bytearray((n + 7) // 8)
    for i, v in enumerate(values):
        if v:
            bs[i >> 3] |= 1 << (i & 7)
    return bytes(bs)


# ---------------------------------------------------------------------------
# l_sort CSV 加载
# ---------------------------------------------------------------------------

def load_lsort_csv(path):
    rows = []
    with open(path, "r", encoding="utf-8-sig", newline="") as f:
        reader = csv.reader(f)
        header = next(reader)
        if len(header) != 7:
            raise ValueError(f"l_sort: expected 7 columns, got {len(header)}")
        for lineno, fields in enumerate(reader, 2):
            if len(fields) != 7:
                raise ValueError(
                    f"l_sort line {lineno}: expected 7 fields, got {len(fields)}"
                )
            rows.append({
                "pinyin": fields[0],
                "frequency": int(fields[1]),
                "word": fields[2],
                "fullPinyin": fields[3],
                "identifier": int(fields[4]),
                "isPolyphonic": fields[5].strip() == "1",
                "inM2": fields[6].strip() == "1",
            })
    return rows


# ---------------------------------------------------------------------------
# 既有的 l_sort 编码器 —— 与已验证的 v1 脚本使用相同表示
# ---------------------------------------------------------------------------

def append_english_vocabulary(rows, english_path):
    """Keep model-only English in the bundle without changing runtime syllables."""
    frequencies = {}
    for row in rows:
        key = english_identity_key(row['word'])
        if key:
            frequencies[key] = max(frequencies.get(key, 0), row['frequency'])
    primary_words = set(frequencies)
    additions = []
    for spelling, word, frequency, split in read_english_vocabulary(english_path):
        key = english_identity_key(word)
        if key not in primary_words:
            frequencies[key] = max(frequencies.get(key, 0), frequency)
        additions.append(dict(pinyin='~en:' + spelling.lower(), frequency=frequency,
            word=word, fullPinyin=spelling.lower(), identifier=0, isPolyphonic=False, inM2=False))
    for row in rows + additions:
        key = english_identity_key(row['word'])
        if key:
            row['frequency'] = frequencies[key]
    rows.extend(additions)


def m2_key(word, full_pinyin):
    # 英文词标识没有读音区分；中文保持既有的
    # 整词/全拼精确绑定。
    identity = english_identity_key(word)
    return (identity, '') if identity else (word, full_pinyin)


def encode_lsort(rows):
    n = len(rows)
    if n == 0:
        raise ValueError("l_sort is empty")

    # --- 构建音节词典 ---
    seen = set()
    for r in rows:
        for s in r["pinyin"].split("'"):
            seen.add(s)
        for s in r["fullPinyin"].split("'"):
            seen.add(s)
    syllables = sorted(seen)
    sym2id = {s: i + 1 for i, s in enumerate(syllables)}

    # --- 音节词典流 ---
    sym_stream = bytearray()
    sym_stream.extend(struct.pack("<I", len(syllables)))
    for sym in syllables:
        enc = sym.encode("utf-8")
        sym_stream.extend(struct.pack("<H", len(enc)))
        sym_stream.extend(enc)

    # --- 拼音流（音节 ID + 前向编码 LCP） ---
    pinyin_stream = bytearray()
    prev_tokens = []
    for r in rows:
        tokens = r["pinyin"].split("'")
        lcp = 0
        min_len = min(len(prev_tokens), len(tokens))
        while lcp < min_len and prev_tokens[lcp] == tokens[lcp]:
            lcp += 1
        new_count = len(tokens) - lcp
        pinyin_stream.extend(uleb128_encode(lcp))
        pinyin_stream.extend(uleb128_encode(new_count))
        for t in tokens[lcp:]:
            pinyin_stream.extend(uleb128_encode(sym2id[t]))
        prev_tokens = tokens

    # --- 词频流 ---
    freq_stream = b"".join(uleb128_encode(r["frequency"]) for r in rows)

    # --- 词语流 ---
    word_stream = bytearray()
    for r in rows:
        wb = r["word"].encode("utf-8")
        word_stream.extend(uleb128_encode(len(wb)))
        word_stream.extend(wb)

    # --- 全拼位：sameAsPinyin 位集 + 例外流 ---
    same_bits = []
    exceptions = []
    for r in rows:
        is_same = r["fullPinyin"] == r["pinyin"]
        same_bits.append(is_same)
        if not is_same:
            exceptions.append(r["fullPinyin"].split("'"))
    fp_bitset = make_bitset(same_bits)
    fp_exc_stream = bytearray()
    for tokens in exceptions:
        fp_exc_stream.extend(uleb128_encode(len(tokens)))
        for t in tokens:
            fp_exc_stream.extend(uleb128_encode(sym2id[t]))
    fp_exc_stream = bytes(fp_exc_stream)

    # --- 标识符流 ---
    nonzero_count = sum(1 for r in rows if r["identifier"] != 0)
    use_sparse = nonzero_count < n // 10
    flags = 1 if use_sparse else 0

    id_stream = bytearray()
    if use_sparse:
        id_stream.extend(uleb128_encode(nonzero_count))
        prev_row = 0
        for i, r in enumerate(rows):
            if r["identifier"] != 0:
                id_stream.extend(uleb128_encode(i - prev_row))
                id_stream.extend(uleb128_encode(r["identifier"]))
                prev_row = i
    else:
        for r in rows:
            id_stream.extend(uleb128_encode(r["identifier"]))

    # --- 布尔位集 ---
    poly_bitset = make_bitset([r["isPolyphonic"] for r in rows])
    m2_bitset = make_bitset([r["inM2"] for r in rows])

    # --- 按偏移组装 ---
    sizes = [
        len(sym_stream), len(pinyin_stream), len(freq_stream),
        len(word_stream), len(fp_bitset), len(fp_exc_stream),
        len(id_stream), len(poly_bitset), len(m2_bitset),
    ]
    offsets = [LSORT_HEADER_SIZE]
    for s in sizes[:-1]:
        offsets.append(offsets[-1] + s)
    file_size = offsets[-1] + sizes[-1]

    header = bytearray(LSORT_HEADER_SIZE)
    header[0:8] = LSORT_MAGIC
    hdr_fields = [
        LSORT_VERSION, flags, n,
        offsets[0], sizes[0],
        offsets[1], sizes[1],
        offsets[2], sizes[2],
        offsets[3], sizes[3],
        offsets[4], offsets[5],
        sizes[5],
        offsets[6],
        offsets[7], offsets[8],
        file_size,
    ]
    for i, val in enumerate(hdr_fields):
        struct.pack_into("<Q", header, 8 + i * 8, val)

    return (
        bytes(header) + bytes(sym_stream) + bytes(pinyin_stream)
        + bytes(freq_stream) + bytes(word_stream)
        + fp_bitset + bytes(fp_exc_stream)
        + bytes(id_stream) + poly_bitset + m2_bitset
    )


# ---------------------------------------------------------------------------
# M2 引用索引
# ---------------------------------------------------------------------------

def build_m2_reference_index(lsort_rows):
    """
    Build a dense reference table from l_sort rows whose in_m2 flag is 1.

    key = (word, fullPinyin)
    value = dense ID [0, m2_word_count)

    Duplicate keys are harmless for m2 reconstruction: every in_m2 row keeps
    its dense position, while the key map points to the first matching row.
    """
    key_to_ref = {}
    ref_to_row = []
    duplicate_count = 0

    for row_index, r in enumerate(lsort_rows):
        if not r["inM2"]:
            continue
        dense_id = len(ref_to_row)
        ref_to_row.append(row_index)
        key = m2_key(r["word"], r["fullPinyin"])
        if key in key_to_ref:
            duplicate_count += 1
        else:
            key_to_ref[key] = dense_id

    return key_to_ref, ref_to_row, duplicate_count


# ---------------------------------------------------------------------------
# M2 概率编解码选择
# ---------------------------------------------------------------------------

def inspect_fixed_decimal(token):
    """Return fractional digit count, or None when token is not canonical fixed decimal."""
    m = _FIXED_DECIMAL_RE.fullmatch(token)
    if not m:
        return None

    # 必须保留精确文本：带符号的负零无法仅用带符号整型系数表示，
    # 因此该情形强制使用原始字符串模式。
    if token.startswith("-"):
        numeric = token[1:].replace(".", "")
        if numeric and all(ch == "0" for ch in numeric):
            return None

    frac = m.group(1)
    return len(frac) if frac is not None else 0


def fixed_decimal_to_int(token, scale):
    negative = token.startswith("-")
    s = token[1:] if negative else token
    if scale == 0:
        value = int(s)
    else:
        int_part, frac_part = s.split(".", 1)
        if len(frac_part) != scale:
            raise ValueError("prob scale mismatch")
        value = int(int_part) * (10 ** scale) + int(frac_part)
    return -value if negative else value


# ---------------------------------------------------------------------------
# M2 encoding
# ---------------------------------------------------------------------------

class MissingM2ReferenceError(ValueError):
    pass


def inspect_m2_csv(path, key_to_ref):
    """First pass: keep only rows whose prev/next (word, fullPinyin) both exist in l_sort."""
    row_count = 0
    skipped_count = 0
    fixed_scale = None
    can_fixed = True

    with open(path, "r", encoding="utf-8-sig", newline="") as f:
        reader = csv.reader(f)
        header = next(reader)
        expected = [
            "prob", "prev_word", "next_word", "prev_fullPinyin", "next_fullPinyin"
        ]
        if header != expected:
            raise ValueError(f"m2 header mismatch: expected {expected!r}, got {header!r}")

        for lineno, fields in enumerate(reader, 2):
            if len(fields) != 5:
                raise ValueError(f"m2 line {lineno}: expected 5 fields, got {len(fields)}")

            prob, prev_word, next_word, prev_fp, next_fp = fields
            prev_key = m2_key(prev_word, prev_fp)
            next_key = m2_key(next_word, next_fp)

            # 严格绑定规则：只有当 M2 行的两端都能在当前 l_sort 中
            # 按词 + 全拼精确解析时才有用。
            # 词或拼音缺失/不匹配时丢弃整条 M2 行。
            if prev_key not in key_to_ref or next_key not in key_to_ref:
                skipped_count += 1
                continue

            if can_fixed:
                scale = inspect_fixed_decimal(prob)
                if scale is None:
                    can_fixed = False
                elif fixed_scale is None:
                    fixed_scale = scale
                elif scale != fixed_scale:
                    can_fixed = False

            row_count += 1

    if row_count == 0:
        raise ValueError("coral_languagemodel.csv has no valid rows matching l_sort")

    if not can_fixed:
        fixed_scale = 0

    return row_count, skipped_count, can_fixed, fixed_scale


def encode_m2(path, key_to_ref, dense_inm2_refs=False):
    row_count, skipped_count, use_fixed_prob, prob_scale = inspect_m2_csv(path, key_to_ref)

    prob_stream = bytearray()
    prev_stream = bytearray()
    next_stream = bytearray()

    prev_ref_prev = 0
    next_ref_prev = 0

    with open(path, "r", encoding="utf-8-sig", newline="") as f:
        reader = csv.reader(f)
        next(reader)  # 文件头，已在第一遍校验

        for fields in reader:
            prob, prev_word, next_word, prev_fp, next_fp = fields
            prev_key = m2_key(prev_word, prev_fp)
            next_key = m2_key(next_word, next_fp)

            # 必须与 inspect_m2_csv 完全一致，使 row_count 和各流保持对齐。
            if prev_key not in key_to_ref or next_key not in key_to_ref:
                continue

            # prob：尽量用精确定点数，否则用原始词元
            if use_fixed_prob:
                scaled = fixed_decimal_to_int(prob, prob_scale)
                prob_stream.extend(uleb128_encode(zigzag_encode(scaled)))
            else:
                pb = prob.encode("utf-8")
                prob_stream.extend(uleb128_encode(len(pb)))
                prob_stream.extend(pb)

            prev_ref = key_to_ref[prev_key]
            next_ref = key_to_ref[next_key]

            # Delta + ZigZag 让相邻引用保持较小，
            # 且在序列反向移动时依然安全。
            prev_stream.extend(
                uleb128_encode(zigzag_encode(prev_ref - prev_ref_prev))
            )
            next_stream.extend(
                uleb128_encode(zigzag_encode(next_ref - next_ref_prev))
            )
            prev_ref_prev = prev_ref
            next_ref_prev = next_ref

    flags = M2_FLAG_DELTA_REFS
    if dense_inm2_refs:
        flags |= M2_FLAG_DENSE_INM2_REFS
    if use_fixed_prob:
        flags |= M2_FLAG_FIXED_DECIMAL_PROB

    prob_off = M2_HEADER_SIZE
    prev_off = prob_off + len(prob_stream)
    next_off = prev_off + len(prev_stream)
    block_size = next_off + len(next_stream)

    header = bytearray(M2_HEADER_SIZE)
    header[0:8] = M2_MAGIC
    fields = [
        M2_VERSION,
        flags,
        row_count,
        prob_scale,
        prob_off,
        len(prob_stream),
        prev_off,
        len(prev_stream),
        next_off,
        len(next_stream),
        block_size,
    ]
    for i, value in enumerate(fields):
        struct.pack_into("<Q", header, 8 + i * 8, value)

    block = bytes(header) + bytes(prob_stream) + bytes(prev_stream) + bytes(next_stream)
    stats = {
        "rows": row_count,
        "skipped_rows": skipped_count,
        "fixed_prob": use_fixed_prob,
        "prob_scale": prob_scale,
        "prob_bytes": len(prob_stream),
        "prev_ref_bytes": len(prev_stream),
        "next_ref_bytes": len(next_stream),
        "raw_block_bytes": len(block),
        "reference_mode": "dense in_m2 IDs" if dense_inm2_refs else "direct l_sort row IDs",
    }
    return block, stats


# ---------------------------------------------------------------------------
# Bundle 组装
# ---------------------------------------------------------------------------

def build_full_lsort_reference_index(lsort_rows):
    """Fallback index: key -> actual l_sort row index."""
    key_to_ref = {}
    duplicate_count = 0
    for row_index, r in enumerate(lsort_rows):
        key = m2_key(r["word"], r["fullPinyin"])
        if key in key_to_ref:
            duplicate_count += 1
            continue
        key_to_ref[key] = row_index
    return key_to_ref, duplicate_count


def encode_bundle(lsort_rows, m2_path):
    lsort_blob = encode_lsort(lsort_rows)

    # M2 绑定到完整的 l_sort 词表。只有两端 (word, fullPinyin) 都精确匹配时才编码，
    # 否则跳过该行。
    # 直接使用 l_sort 行号，使该规则不依赖 in_m2 标志。
    key_to_ref, duplicate_count = build_full_lsort_reference_index(lsort_rows)
    m2_blob, m2_stats = encode_m2(m2_path, key_to_ref, dense_inm2_refs=False)
    reference_count = len(lsort_rows)
    used_fallback = False

    lsort_off = BUNDLE_HEADER_SIZE
    m2_off = lsort_off + len(lsort_blob)
    file_size = m2_off + len(m2_blob)

    header = bytearray(BUNDLE_HEADER_SIZE)
    header[0:8] = BUNDLE_MAGIC
    fields = [
        BUNDLE_VERSION,
        0,  # 保留标志位
        lsort_off,
        len(lsort_blob),
        m2_off,
        len(m2_blob),
        file_size,
    ]
    for i, value in enumerate(fields):
        struct.pack_into("<Q", header, 8 + i * 8, value)

    stats = {
        "lsort_raw_bytes": len(lsort_blob),
        "m2_reference_count": reference_count,
        "m2_duplicate_lsort_keys": duplicate_count,
        "m2_reference_fallback": used_fallback,
        **m2_stats,
    }
    return bytes(header) + lsort_blob + m2_blob, stats


# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------

def main():
    import argparse
    from paths import output_path
    parser=argparse.ArgumentParser(description="Encode a paired l_sort + canonical M2 bundle (existing format v2)")
    parser.add_argument('lsort');parser.add_argument('m2');parser.add_argument('output')
    parser.add_argument('--english',help='English source CSV to include in the model vocabulary')
    args=parser.parse_args()
    try:
        dest=output_path(args.output,[args.lsort,args.m2] + ([args.english] if args.english else []))
        rows=load_lsort_csv(args.lsort)
        if args.english:append_english_vocabulary(rows,args.english)
        raw,stats=encode_bundle(rows,args.m2)
        print(f"Lexicon rows: {len(rows)}; M2 stats: {stats}")
        # 引用缺失必须在上游显式处理，绝不能在这里静默丢弃。
        if stats['skipped_rows']:raise ValueError('Unmatched M2 references; first run index_bigram.py and use bundle_coral_languagemodel.csv')
        compressed=lzma.compress(raw,preset=9|lzma.PRESET_EXTREME)
        with open(dest,'xb') as stream:stream.write(compressed)
        print(f"Bundle written: {dest}; bytes={len(compressed)}")
    except (ValueError,OSError) as error:parser.exit(1,str(error)+'\n')

if __name__=='__main__':main()

#!/usr/bin/env python3
"""
DictPack v2 Decoder

CLI: decode_dictpack.py bundle.dictpack.lzma2 coral_lexicon.csv coral_languagemodel.csv
Legacy v1 codec functions remain internal; output paths are always explicit.
"""
import sys
sys.dont_write_bytecode=True

import csv
import lzma
import os
import struct
import sys

LSORT_MAGIC = b"KDICTPK\x00"
LSORT_HEADER_SIZE = 152

BUNDLE_MAGIC = b"KDBNDL2\x00"
BUNDLE_VERSION = 2
BUNDLE_HEADER_SIZE = 64

M2_MAGIC = b"KM2PACK\x00"
M2_VERSION = 1
M2_HEADER_SIZE = 96

M2_FLAG_DENSE_INM2_REFS = 1 << 0
M2_FLAG_FIXED_DECIMAL_PROB = 1 << 1
M2_FLAG_DELTA_REFS = 1 << 2


# ---------------------------------------------------------------------------
# 整数编解码
# ---------------------------------------------------------------------------

def uleb128_decode(data, pos, end=None):
    result = 0
    shift = 0
    limit = len(data) if end is None else end
    while True:
        if pos >= limit:
            raise ValueError("ULEB128: unexpected end of data")
        b = data[pos]
        pos += 1
        result |= (b & 0x7F) << shift
        if (b & 0x80) == 0:
            break
        shift += 7
        if shift > 70:
            raise ValueError("ULEB128: value too large")
    return result, pos


def zigzag_decode(value):
    return -(value // 2) - 1 if (value & 1) else value // 2


# ---------------------------------------------------------------------------
# Bitset
# ---------------------------------------------------------------------------

def read_bitset(data, n):
    return [bool(data[i >> 3] & (1 << (i & 7))) for i in range(n)]


# ---------------------------------------------------------------------------
# l_sort DictPack v1 解码器
# ---------------------------------------------------------------------------

def decode_lsort_raw(raw):
    if len(raw) < LSORT_HEADER_SIZE:
        raise ValueError("l_sort block too small for header")
    if raw[0:8] != LSORT_MAGIC:
        raise ValueError(f"Bad l_sort magic: {raw[0:8]!r}")

    def hq(off):
        return struct.unpack_from("<Q", raw, off)[0]

    version = hq(8)
    flags = hq(16)
    n = hq(24)
    sym_off = hq(32)
    sym_size = hq(40)
    py_off = hq(48)
    py_size = hq(56)
    freq_off = hq(64)
    word_off = hq(80)
    fp_bs_off = hq(96)
    fp_exc_off = hq(104)
    fp_exc_sz = hq(112)
    id_off = hq(120)
    poly_off = hq(128)
    m2_off = hq(136)
    declared_size = hq(144)

    if version != 1:
        raise ValueError(f"Unsupported l_sort DictPack version: {version}")
    if declared_size != len(raw):
        raise ValueError(
            f"l_sort block size mismatch: header={declared_size}, actual={len(raw)}"
        )
    if not (LSORT_HEADER_SIZE <= sym_off <= py_off <= freq_off <= word_off <= fp_bs_off <= fp_exc_off <= id_off <= poly_off <= m2_off <= len(raw)):
        raise ValueError("l_sort block contains invalid/non-monotonic offsets")
    if sym_off + sym_size > len(raw) or py_off + py_size > len(raw):
        raise ValueError("l_sort stream exceeds block size")

    use_sparse_id = bool(flags & 1)
    pos = 0

    # --- 音节词典 ---
    sym_count = struct.unpack_from("<I", raw, sym_off)[0]
    pos = sym_off + 4
    id2sym = []
    for _ in range(sym_count):
        if pos + 2 > len(raw):
            raise ValueError("Truncated syllable dictionary")
        slen = struct.unpack_from("<H", raw, pos)[0]
        pos += 2
        if pos + slen > len(raw):
            raise ValueError("Truncated syllable text")
        s = raw[pos:pos + slen].decode("utf-8")
        pos += slen
        id2sym.append(s)

    # --- Pinyin ---
    pos = py_off
    py_end = py_off + py_size
    prev_tokens = []
    pinyin_list = []
    for _ in range(n):
        lcp, pos = uleb128_decode(raw, pos, py_end)
        new_count, pos = uleb128_decode(raw, pos, py_end)
        if lcp > len(prev_tokens):
            raise ValueError("Invalid pinyin LCP")
        new_tokens = []
        for _ in range(new_count):
            tid, pos = uleb128_decode(raw, pos, py_end)
            if tid == 0 or tid > len(id2sym):
                raise ValueError(f"Invalid syllable ID: {tid}")
            new_tokens.append(id2sym[tid - 1])
        tokens = prev_tokens[:lcp] + new_tokens
        pinyin_list.append("'".join(tokens))
        prev_tokens = tokens
    if pos != py_end:
        raise ValueError("Pinyin stream has trailing/unused bytes")

    # --- Frequency ---
    pos = freq_off
    freq_list = []
    for _ in range(n):
        val, pos = uleb128_decode(raw, pos, word_off)
        freq_list.append(val)
    if pos != word_off:
        raise ValueError("Frequency stream has trailing/unused bytes")

    # --- Word ---
    pos = word_off
    word_list = []
    for _ in range(n):
        blen, pos = uleb128_decode(raw, pos, fp_bs_off)
        if pos + blen > fp_bs_off:
            raise ValueError("Word stream exceeds boundary")
        wb = raw[pos:pos + blen]
        pos += blen
        word_list.append(wb.decode("utf-8"))
    if pos != fp_bs_off:
        raise ValueError("Word stream has trailing/unused bytes")

    # --- FullPinyin ---
    fp_bs_len = (n + 7) // 8
    if fp_bs_off + fp_bs_len != fp_exc_off:
        raise ValueError("Unexpected fullPinyin bitset size")
    same_bits = read_bitset(raw[fp_bs_off:fp_exc_off], n)

    pos = fp_exc_off
    fp_exc_end = fp_exc_off + fp_exc_sz
    if fp_exc_end > id_off:
        raise ValueError("fullPinyin exception stream exceeds identifier offset")
    fp_exceptions = []
    while pos < fp_exc_end:
        tok_count, pos = uleb128_decode(raw, pos, fp_exc_end)
        tokens = []
        for _ in range(tok_count):
            tid, pos = uleb128_decode(raw, pos, fp_exc_end)
            if tid == 0 or tid > len(id2sym):
                raise ValueError(f"Invalid fullPinyin syllable ID: {tid}")
            tokens.append(id2sym[tid - 1])
        fp_exceptions.append("'".join(tokens))

    exc_idx = 0
    full_pinyin_list = []
    for i in range(n):
        if same_bits[i]:
            full_pinyin_list.append(pinyin_list[i])
        else:
            if exc_idx >= len(fp_exceptions):
                raise ValueError("Missing fullPinyin exception")
            full_pinyin_list.append(fp_exceptions[exc_idx])
            exc_idx += 1
    if exc_idx != len(fp_exceptions):
        raise ValueError("Unused fullPinyin exceptions")

    # --- Identifier ---
    pos = id_off
    id_list = []
    if use_sparse_id:
        count, pos = uleb128_decode(raw, pos, poly_off)
        prev_row = 0
        for _ in range(count):
            delta, pos = uleb128_decode(raw, pos, poly_off)
            row = prev_row + delta
            if row >= n:
                raise ValueError("Sparse identifier row out of range")
            while len(id_list) < row:
                id_list.append(0)
            val, pos = uleb128_decode(raw, pos, poly_off)
            id_list.append(val)
            prev_row = row
        while len(id_list) < n:
            id_list.append(0)
    else:
        for _ in range(n):
            val, pos = uleb128_decode(raw, pos, poly_off)
            id_list.append(val)
    if pos != poly_off:
        raise ValueError("Identifier stream has trailing/unused bytes")

    # --- 布尔位集 ---
    bitset_len = (n + 7) // 8
    if poly_off + bitset_len != m2_off:
        raise ValueError("Unexpected isPolyphonic bitset size")
    if m2_off + bitset_len != len(raw):
        raise ValueError("Unexpected in_m2 bitset size")
    poly_list = read_bitset(raw[poly_off:m2_off], n)
    m2_list = read_bitset(raw[m2_off:m2_off + bitset_len], n)

    rows = []
    for i in range(n):
        rows.append([
            pinyin_list[i],
            str(freq_list[i]),
            word_list[i],
            full_pinyin_list[i],
            str(id_list[i]),
            "1" if poly_list[i] else "0",
            "1" if m2_list[i] else "0",
        ])
    return rows


# ---------------------------------------------------------------------------
# M2 decoder
# ---------------------------------------------------------------------------

def fixed_decimal_from_int(value, scale):
    sign = "-" if value < 0 else ""
    value = abs(value)
    if scale == 0:
        return sign + str(value)
    base = 10 ** scale
    int_part, frac_part = divmod(value, base)
    return f"{sign}{int_part}.{frac_part:0{scale}d}"


def build_dense_inm2_rows(lsort_rows):
    """Return every in_m2=1 l_sort row in original row order."""
    return [row_index for row_index, row in enumerate(lsort_rows) if row[6] == "1"]


def decode_m2_raw(raw, lsort_rows):
    if len(raw) < M2_HEADER_SIZE:
        raise ValueError("m2 block too small for header")
    if raw[0:8] != M2_MAGIC:
        raise ValueError(f"Bad m2 magic: {raw[0:8]!r}")

    def hq(off):
        return struct.unpack_from("<Q", raw, off)[0]

    version = hq(8)
    flags = hq(16)
    n = hq(24)
    prob_scale = hq(32)
    prob_off = hq(40)
    prob_size = hq(48)
    prev_off = hq(56)
    prev_size = hq(64)
    next_off = hq(72)
    next_size = hq(80)
    block_size = hq(88)

    if version != M2_VERSION:
        raise ValueError(f"Unsupported m2 block version: {version}")
    if block_size != len(raw):
        raise ValueError(f"m2 block size mismatch: header={block_size}, actual={len(raw)}")
    if not (M2_HEADER_SIZE <= prob_off <= prev_off <= next_off <= len(raw)):
        raise ValueError("m2 block contains invalid/non-monotonic offsets")
    if prob_off + prob_size != prev_off:
        raise ValueError("m2 prob stream size mismatch")
    if prev_off + prev_size != next_off:
        raise ValueError("m2 prev-ref stream size mismatch")
    if next_off + next_size != len(raw):
        raise ValueError("m2 next-ref stream size mismatch")

    if not (flags & M2_FLAG_DELTA_REFS):
        raise ValueError("Unsupported m2 non-delta reference mode")

    dense_refs = bool(flags & M2_FLAG_DENSE_INM2_REFS)
    if dense_refs:
        ref_to_row = build_dense_inm2_rows(lsort_rows)
        if not ref_to_row:
            raise ValueError("Decoded l_sort has no in_m2=1 entries")
        ref_limit = len(ref_to_row)
    else:
        ref_to_row = None
        ref_limit = len(lsort_rows)

    # --- prob ---
    probs = []
    pos = prob_off
    prob_end = prev_off
    if flags & M2_FLAG_FIXED_DECIMAL_PROB:
        for _ in range(n):
            u, pos = uleb128_decode(raw, pos, prob_end)
            value = zigzag_decode(u)
            probs.append(fixed_decimal_from_int(value, prob_scale))
    else:
        for _ in range(n):
            blen, pos = uleb128_decode(raw, pos, prob_end)
            if pos + blen > prob_end:
                raise ValueError("m2 raw probability exceeds stream boundary")
            probs.append(raw[pos:pos + blen].decode("utf-8"))
            pos += blen
    if pos != prob_end:
        raise ValueError("m2 probability stream has trailing/unused bytes")

    # --- 前驱引用 ---
    prev_refs = []
    pos = prev_off
    prev_end = next_off
    previous = 0
    for _ in range(n):
        u, pos = uleb128_decode(raw, pos, prev_end)
        current = previous + zigzag_decode(u)
        if current < 0 or current >= ref_limit:
            raise ValueError(f"m2 prev reference out of range: {current}")
        prev_refs.append(current)
        previous = current
    if pos != prev_end:
        raise ValueError("m2 prev-ref stream has trailing/unused bytes")

    # --- 后继引用 ---
    next_refs = []
    pos = next_off
    next_end = len(raw)
    previous = 0
    for _ in range(n):
        u, pos = uleb128_decode(raw, pos, next_end)
        current = previous + zigzag_decode(u)
        if current < 0 or current >= ref_limit:
            raise ValueError(f"m2 next reference out of range: {current}")
        next_refs.append(current)
        previous = current
    if pos != next_end:
        raise ValueError("m2 next-ref stream has trailing/unused bytes")

    rows = []
    for i in range(n):
        prev_row_index = ref_to_row[prev_refs[i]] if dense_refs else prev_refs[i]
        next_row_index = ref_to_row[next_refs[i]] if dense_refs else next_refs[i]
        prev_row = lsort_rows[prev_row_index]
        next_row = lsort_rows[next_row_index]
        rows.append([
            probs[i],
            prev_row[2],      # word
            next_row[2],      # word
            prev_row[3],      # fullPinyin
            next_row[3],      # fullPinyin
        ])
    return rows


# ---------------------------------------------------------------------------
# Bundle 解码器
# ---------------------------------------------------------------------------

def decode_bundle_raw(raw):
    if len(raw) < BUNDLE_HEADER_SIZE:
        raise ValueError("Bundle too small for header")
    if raw[0:8] != BUNDLE_MAGIC:
        raise ValueError(f"Bad bundle magic: {raw[0:8]!r}")

    def hq(off):
        return struct.unpack_from("<Q", raw, off)[0]

    version = hq(8)
    flags = hq(16)
    lsort_off = hq(24)
    lsort_size = hq(32)
    m2_off = hq(40)
    m2_size = hq(48)
    file_size = hq(56)

    if version != BUNDLE_VERSION:
        raise ValueError(f"Unsupported bundle version: {version}")
    if flags != 0:
        raise ValueError(f"Unsupported bundle flags: 0x{flags:x}")
    if file_size != len(raw):
        raise ValueError(f"Bundle size mismatch: header={file_size}, actual={len(raw)}")
    if lsort_off != BUNDLE_HEADER_SIZE:
        raise ValueError("Unexpected l_sort bundle offset")
    if lsort_off + lsort_size != m2_off:
        raise ValueError("Bundle l_sort size/offset mismatch")
    if m2_off + m2_size != len(raw):
        raise ValueError("Bundle m2 size/offset mismatch")

    lsort_raw = raw[lsort_off:lsort_off + lsort_size]
    m2_raw = raw[m2_off:m2_off + m2_size]

    lsort_rows = decode_lsort_raw(lsort_raw)
    m2_rows = decode_m2_raw(m2_raw, lsort_rows)
    return lsort_rows, m2_rows


# ---------------------------------------------------------------------------
# CSV 写出
# ---------------------------------------------------------------------------

def write_lsort_csv(rows, path):
    with open(path, "w", encoding="utf-8-sig", newline="") as f:
        writer = csv.writer(f, lineterminator="\r\n")
        writer.writerow([
            "pinyin", "frequency", "word", "fullPinyin",
            "identifier", "isPolyphonic", "in_m2",
        ])
        writer.writerows(rows)


def write_m2_csv(rows, path):
    with open(path, "w", encoding="utf-8-sig", newline="") as f:
        writer = csv.writer(f, lineterminator="\r\n")
        writer.writerow([
            "prob", "prev_word", "next_word", "prev_fullPinyin", "next_fullPinyin"
        ])
        writer.writerows(rows)


# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------

def main():
    import argparse
    from paths import output_path
    parser=argparse.ArgumentParser(description="Decode a v2 bundle to two explicit output CSVs")
    parser.add_argument('input');parser.add_argument('lsort_output');parser.add_argument('m2_output')
    args=parser.parse_args()
    try:
        lsort_out=output_path(args.lsort_output,[args.input]);m2_out=output_path(args.m2_output,[args.input,lsort_out])
        with open(args.input,'rb') as stream:raw=lzma.decompress(stream.read())
        if not raw.startswith(BUNDLE_MAGIC):raise ValueError('The public CLI expects a v2 bundle; legacy v1 has no M2 block')
        lsort,m2=decode_bundle_raw(raw)
        write_lsort_csv(lsort,lsort_out);write_m2_csv(m2,m2_out)
        print(f"Decoded lexicon rows={len(lsort)}; M2 rows={len(m2)}")
    except (ValueError,OSError) as error:parser.exit(1,str(error)+'\n')

if __name__=='__main__':main()

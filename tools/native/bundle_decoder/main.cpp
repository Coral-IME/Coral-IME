/*
 * @brief: dictionary_bundle.dictpack.lzma2 安装期解包工具（纯 Win32，无 Python 依赖）
 *
 * 输入: dictionary_bundle.dictpack.lzma2 (XZ 压缩的 DictPack v2 bundle)
 * 输出: coral_lexicon.csv + coral_languagemodel.csv（UTF-8 BOM + CRLF，与原始文件逐字节一致）
 *
 * 用法:
 *   coral_bundle_decoder <bundle.dictpack.lzma2> <coral_lexicon.csv> <coral_languagemodel.csv>
 *
 * XZ 解码使用 XZ Embedded（0BSD 公有领域，静态链接）。
 * DictPack v2 格式与 script/dictpack_encode_with_m2.py 严格对应。
 */
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <string>
#include <vector>

#include "xz.h"

// ---------------------------------------------------------------------------
// 错误处理
// ---------------------------------------------------------------------------

namespace
{

struct DecodeError
{
	const char* msg = nullptr;
};

[[noreturn]] void fail(const char* msg)
{
	throw DecodeError{ msg };
}

// ---------------------------------------------------------------------------
// XZ 解压（XZ Embedded, XZ_DYNALLOC 多调用模式）
// ---------------------------------------------------------------------------

std::vector<uint8_t> xzDecompressFile(const char* path)
{
	FILE* f = nullptr;
	if (fopen_s(&f, path, "rb") != 0 || !f)
		fail("cannot open input file");

	std::vector<uint8_t> out;
	out.reserve(256u << 20);

	xz_crc32_init();
#ifdef XZ_USE_CRC64
	xz_crc64_init();
#endif

	struct xz_dec* dec = xz_dec_init(XZ_DYNALLOC, 64u << 20);
	if (!dec)
	{
		fclose(f);
		fail("xz_dec_init failed (out of memory)");
	}

	std::vector<uint8_t> in(64 * 1024);
	std::vector<uint8_t> outBuf(256 * 1024);
	struct xz_buf b;
	std::memset(&b, 0, sizeof(b));
	b.in = in.data();
	b.out = outBuf.data();
	b.out_size = static_cast<uint32_t>(outBuf.size());

	bool done = false;
	while (!done)
	{
		if (b.in_pos == b.in_size)
		{
			b.in_size = static_cast<uint32_t>(fread(in.data(), 1, in.size(), f));
			b.in_pos = 0;
			if (ferror(f))
			{
				xz_dec_end(dec);
				fclose(f);
				fail("read error on input file");
			}
		}

		enum xz_ret ret = xz_dec_run(dec, &b);
		if (b.out_pos > 0)
		{
			out.insert(out.end(), outBuf.data(), outBuf.data() + b.out_pos);
			b.out_pos = 0;
		}

		switch (ret)
		{
		case XZ_OK:
			break;
		case XZ_STREAM_END:
			done = true;
			break;
		case XZ_MEM_ERROR:
			xz_dec_end(dec);
			fclose(f);
			fail("xz: out of memory");
		case XZ_MEMLIMIT_ERROR:
			xz_dec_end(dec);
			fclose(f);
			fail("xz: memory usage limit reached");
		case XZ_FORMAT_ERROR:
			xz_dec_end(dec);
			fclose(f);
			fail("xz: not a .xz file");
		case XZ_OPTIONS_ERROR:
			xz_dec_end(dec);
			fclose(f);
			fail("xz: unsupported options in headers");
		case XZ_DATA_ERROR:
		case XZ_BUF_ERROR:
			xz_dec_end(dec);
			fclose(f);
			fail("xz: file is corrupt");
		default:
			xz_dec_end(dec);
			fclose(f);
			fail("xz: internal bug");
		}
	}

	xz_dec_end(dec);
	fclose(f);
	return out;
}

// ---------------------------------------------------------------------------
// ULEB128 / ZigZag / bitset / 小工具
// ---------------------------------------------------------------------------

struct Cursor
{
	const uint8_t* p;
	const uint8_t* end;
};

uint64_t uleb(Cursor& c)
{
	uint64_t v = 0;
	int shift = 0;
	for (;;)
	{
		if (c.p >= c.end)
			fail("ULEB128: unexpected end of data");
		uint8_t b = *c.p++;
		v |= static_cast<uint64_t>(b & 0x7F) << shift;
		if (!(b & 0x80))
			return v;
		shift += 7;
		if (shift > 70)
			fail("ULEB128: value too large");
	}
}

int64_t zigzagDecode(uint64_t u)
{
	return static_cast<int64_t>(u >> 1) ^ -static_cast<int64_t>(u & 1);
}

inline bool bitAt(const uint8_t* bs, size_t i)
{
	return (bs[i >> 3] >> (i & 7)) & 1;
}

uint64_t readU64(const uint8_t* p)
{
	uint64_t v = 0;
	for (int i = 7; i >= 0; --i)
		v = (v << 8) | p[i];
	return v;
}

uint64_t pow10u(unsigned n)
{
	uint64_t v = 1;
	while (n--)
		v *= 10;
	return v;
}

// ---------------------------------------------------------------------------
// CSV 输出（UTF-8 BOM + CRLF；字段含 , " \r \n 时加引号转义，与 Python csv.writer 一致）
// ---------------------------------------------------------------------------

void csvWriteRow(FILE* f, const std::vector<std::string>& fields)
{
	for (size_t i = 0; i < fields.size(); ++i)
	{
		if (i)
			fputc(',', f);
		const std::string& s = fields[i];
		const bool needQuote = s.find_first_of(",\"\r\n") != std::string::npos;
		if (needQuote)
		{
			fputc('"', f);
			for (char ch : s)
			{
				if (ch == '"')
					fputc('"', f);
				fputc(ch, f);
			}
			fputc('"', f);
		}
		else
		{
			fwrite(s.data(), 1, s.size(), f);
		}
	}
	fputs("\r\n", f);
}

// ---------------------------------------------------------------------------
// l_sort DictPack v1 块解码：写 CSV + 构建 m2 引用所需的行结构
// ---------------------------------------------------------------------------

struct LsortRow
{
	std::string word;
	std::string fullPinyin;
	bool inM2 = false;
};

void decodeLsortBlock(const uint8_t* raw, size_t size, FILE* out, std::vector<LsortRow>& rows)
{
	static const char LSORT_MAGIC[8] = { 'K', 'D', 'I', 'C', 'T', 'P', 'K', '\0' };
	constexpr size_t LSORT_HEADER_SIZE = 152;

	if (size < LSORT_HEADER_SIZE)
		fail("l_sort block too small for header");
	if (std::memcmp(raw, LSORT_MAGIC, 8) != 0)
		fail("bad l_sort magic");

	const uint64_t version = readU64(raw + 8);
	const uint64_t flags = readU64(raw + 16);
	const uint64_t n = readU64(raw + 24);
	const uint64_t symOff = readU64(raw + 32);
	const uint64_t symSize = readU64(raw + 40);
	const uint64_t pyOff = readU64(raw + 48);
	const uint64_t pySize = readU64(raw + 56);
	const uint64_t freqOff = readU64(raw + 64);
	const uint64_t word_offset = readU64(raw + 80);
	const uint64_t fpBsOff = readU64(raw + 96);
	const uint64_t fpExcOff = readU64(raw + 104);
	const uint64_t fpExcSz = readU64(raw + 112);
	const uint64_t idOff = readU64(raw + 120);
	const uint64_t polyOff = readU64(raw + 128);
	const uint64_t m2Off = readU64(raw + 136);
	const uint64_t declaredSize = readU64(raw + 144);

	if (version != 1)
		fail("unsupported l_sort DictPack version");
	if (declaredSize != size)
		fail("l_sort block size mismatch");
	if (!(LSORT_HEADER_SIZE <= symOff && symOff <= pyOff && pyOff <= freqOff
		&& freqOff <= word_offset && word_offset <= fpBsOff && fpBsOff <= fpExcOff
		&& fpExcOff <= idOff && idOff <= polyOff && polyOff <= m2Off && m2Off <= size))
		fail("l_sort block contains invalid/non-monotonic offsets");
	if (symOff + symSize > size || pyOff + pySize > size)
		fail("l_sort stream exceeds block size");
	if (n == 0)
		fail("l_sort is empty");

	const bool useSparseId = (flags & 1) != 0;

	// --- 音节字典 ---
	if (symOff + 4 > size)
		fail("truncated syllable dictionary");
	const uint32_t symCount = static_cast<uint32_t>(raw[symOff])
		| (static_cast<uint32_t>(raw[symOff + 1]) << 8)
		| (static_cast<uint32_t>(raw[symOff + 2]) << 16)
		| (static_cast<uint32_t>(raw[symOff + 3]) << 24);
	std::vector<std::string> id2sym;
	id2sym.reserve(symCount);
	{
		Cursor c{ raw + symOff + 4, raw + symOff + symSize };
		for (uint32_t i = 0; i < symCount; ++i)
		{
			if (c.p + 2 > c.end)
				fail("truncated syllable dictionary");
			const size_t len = c.p[0] | (c.p[1] << 8);
			c.p += 2;
			if (c.p + len > c.end)
				fail("truncated syllable text");
			id2sym.emplace_back(reinterpret_cast<const char*>(c.p), len);
			c.p += len;
		}
	}

	// --- pinyin（音节 ID + front coding LCP）---
	std::vector<std::string> pinyinList;
	pinyinList.reserve(static_cast<size_t>(n));
	{
		Cursor c{ raw + pyOff, raw + pyOff + pySize };
		std::vector<std::string> prevTokens;
		for (uint64_t i = 0; i < n; ++i)
		{
			const uint64_t lcp = uleb(c);
			const uint64_t newCount = uleb(c);
			if (lcp > prevTokens.size())
				fail("invalid pinyin LCP");
			std::vector<std::string> tokens(prevTokens.begin(), prevTokens.begin() + lcp);
			for (uint64_t k = 0; k < newCount; ++k)
			{
				const uint64_t tid = uleb(c);
				if (tid == 0 || tid > id2sym.size())
					fail("invalid syllable ID");
				tokens.push_back(id2sym[static_cast<size_t>(tid) - 1]);
			}
			std::string joined;
			for (size_t k = 0; k < tokens.size(); ++k)
			{
				if (k)
					joined += '\'';
				joined += tokens[k];
			}
			pinyinList.push_back(std::move(joined));
			prevTokens = std::move(tokens);
		}
		if (c.p != c.end)
			fail("pinyin stream has trailing bytes");
	}

	// --- frequency ---
	std::vector<uint64_t> freqList;
	freqList.reserve(static_cast<size_t>(n));
	{
		Cursor c{ raw + freqOff, raw + word_offset };
		for (uint64_t i = 0; i < n; ++i)
			freqList.push_back(uleb(c));
		if (c.p != c.end)
			fail("frequency stream has trailing bytes");
	}

	// --- word ---
	std::vector<std::string> wordList;
	wordList.reserve(static_cast<size_t>(n));
	{
		Cursor c{ raw + word_offset, raw + fpBsOff };
		for (uint64_t i = 0; i < n; ++i)
		{
			const uint64_t len = uleb(c);
			if (c.p + len > c.end)
				fail("word stream exceeds boundary");
			wordList.emplace_back(reinterpret_cast<const char*>(c.p), static_cast<size_t>(len));
			c.p += len;
		}
		if (c.p != c.end)
			fail("word stream has trailing bytes");
	}

	// --- fullPinyin（sameAsPinyin bitset + 异常流）---
	const size_t fpBsLen = (static_cast<size_t>(n) + 7) / 8;
	if (fpBsOff + fpBsLen != fpExcOff)
		fail("unexpected fullPinyin bitset size");
	const uint8_t* fpBs = raw + fpBsOff;
	std::vector<std::string> fpExceptions;
	{
		Cursor c{ raw + fpExcOff, raw + fpExcOff + fpExcSz };
		if (c.end > raw + idOff)
			fail("fullPinyin exception stream exceeds identifier offset");
		while (c.p < c.end)
		{
			const uint64_t tokCount = uleb(c);
			std::vector<std::string> tokens;
			for (uint64_t k = 0; k < tokCount; ++k)
			{
				const uint64_t tid = uleb(c);
				if (tid == 0 || tid > id2sym.size())
					fail("invalid fullPinyin syllable ID");
				tokens.push_back(id2sym[static_cast<size_t>(tid) - 1]);
			}
			std::string joined;
			for (size_t k = 0; k < tokens.size(); ++k)
			{
				if (k)
					joined += '\'';
				joined += tokens[k];
			}
			fpExceptions.push_back(std::move(joined));
		}
	}

	// --- identifier（sparse 或 dense）---
	std::vector<uint64_t> idList(static_cast<size_t>(n), 0);
	{
		Cursor c{ raw + idOff, raw + polyOff };
		if (useSparseId)
		{
			const uint64_t count = uleb(c);
			uint64_t prevRow = 0;
			for (uint64_t k = 0; k < count; ++k)
			{
				const uint64_t delta = uleb(c);
				const uint64_t row = prevRow + delta;
				if (row >= n)
					fail("sparse identifier row out of range");
				idList[static_cast<size_t>(row)] = uleb(c);
				prevRow = row;
			}
		}
		else
		{
			for (uint64_t i = 0; i < n; ++i)
				idList[static_cast<size_t>(i)] = uleb(c);
		}
		if (c.p != c.end)
			fail("identifier stream has trailing bytes");
	}

	// --- 布尔 bitsets ---
	const size_t boolBsLen = (static_cast<size_t>(n) + 7) / 8;
	if (polyOff + boolBsLen != m2Off)
		fail("unexpected isPolyphonic bitset size");
	if (m2Off + boolBsLen != size)
		fail("unexpected in_m2 bitset size");
	const uint8_t* polyBs = raw + polyOff;
	const uint8_t* m2Bs = raw + m2Off;

	// --- 写出 CSV + 构建行结构 ---
	csvWriteRow(out, { "pinyin", "frequency", "word", "fullPinyin",
		"identifier", "isPolyphonic", "in_m2" });

	rows.clear();
	rows.resize(static_cast<size_t>(n));

	char num[32];
	size_t excIdx = 0;
	for (uint64_t i = 0; i < n; ++i)
	{
		const size_t idx = static_cast<size_t>(i);
		std::string fullPinyin;
		if (bitAt(fpBs, idx))
			fullPinyin = pinyinList[idx];
		else
		{
			if (excIdx >= fpExceptions.size())
				fail("missing fullPinyin exception");
			fullPinyin = fpExceptions[excIdx++];
		}

		sprintf_s(num, sizeof(num), "%llu", freqList[idx]);
		const std::string freqStr = num;
		sprintf_s(num, sizeof(num), "%llu", idList[idx]);
		const std::string idStr = num;

		csvWriteRow(out, { pinyinList[idx], freqStr, wordList[idx], fullPinyin,
			idStr,
			bitAt(polyBs, idx) ? "1" : "0",
			bitAt(m2Bs, idx) ? "1" : "0" });

		rows[idx].word = std::move(wordList[idx]);
		rows[idx].fullPinyin = std::move(fullPinyin);
		rows[idx].inM2 = bitAt(m2Bs, idx);
	}
	if (excIdx != fpExceptions.size())
		fail("unused fullPinyin exceptions");
}

// ---------------------------------------------------------------------------
// m2 块解码
// ---------------------------------------------------------------------------

void decodeM2Block(const uint8_t* raw, size_t size, const std::vector<LsortRow>& lsortRows, FILE* out)
{
	static const char M2_MAGIC[8] = { 'K', 'M', '2', 'P', 'A', 'C', 'K', '\0' };
	constexpr size_t M2_HEADER_SIZE = 96;
	constexpr uint64_t M2_FLAG_DENSE_INM2_REFS = 1 << 0;
	constexpr uint64_t M2_FLAG_FIXED_DECIMAL_PROB = 1 << 1;
	constexpr uint64_t M2_FLAG_DELTA_REFS = 1 << 2;

	if (size < M2_HEADER_SIZE)
		fail("m2 block too small for header");
	if (std::memcmp(raw, M2_MAGIC, 8) != 0)
		fail("bad m2 magic");

	const uint64_t version = readU64(raw + 8);
	const uint64_t flags = readU64(raw + 16);
	const uint64_t n = readU64(raw + 24);
	const uint64_t probScale = readU64(raw + 32);
	const uint64_t probOff = readU64(raw + 40);
	const uint64_t probSize = readU64(raw + 48);
	const uint64_t prevOff = readU64(raw + 56);
	const uint64_t prevSize = readU64(raw + 64);
	const uint64_t nextOff = readU64(raw + 72);
	const uint64_t nextSize = readU64(raw + 80);
	const uint64_t blockSize = readU64(raw + 88);

	if (version != 1)
		fail("unsupported m2 block version");
	if (blockSize != size)
		fail("m2 block size mismatch");
	if (!(M2_HEADER_SIZE <= probOff && probOff <= prevOff && prevOff <= nextOff && nextOff <= size))
		fail("m2 block contains invalid/non-monotonic offsets");
	if (probOff + probSize != prevOff)
		fail("m2 prob stream size mismatch");
	if (prevOff + prevSize != nextOff)
		fail("m2 prev-ref stream size mismatch");
	if (nextOff + nextSize != size)
		fail("m2 next-ref stream size mismatch");
	if (!(flags & M2_FLAG_DELTA_REFS))
		fail("unsupported m2 non-delta reference mode");

	const bool denseRefs = (flags & M2_FLAG_DENSE_INM2_REFS) != 0;
	std::vector<uint32_t> refToRow;
	uint64_t refLimit = 0;
	if (denseRefs)
	{
		for (uint32_t i = 0; i < lsortRows.size(); ++i)
			if (lsortRows[i].inM2)
				refToRow.push_back(i);
		if (refToRow.empty())
			fail("decoded l_sort has no in_m2=1 entries");
		refLimit = refToRow.size();
	}
	else
	{
		refLimit = lsortRows.size();
	}

	// --- prob（定点 or 原始 token）---
	std::vector<std::string> probs;
	probs.reserve(static_cast<size_t>(n));
	{
		Cursor c{ raw + probOff, raw + prevOff };
		if (flags & M2_FLAG_FIXED_DECIMAL_PROB)
		{
			const uint64_t base = pow10u(static_cast<unsigned>(probScale));
			for (uint64_t i = 0; i < n; ++i)
			{
				const int64_t v = zigzagDecode(uleb(c));
				char buf[48];
				if (probScale == 0)
					sprintf_s(buf, sizeof(buf), "%lld", v);
				else
				{
					const bool neg = v < 0;
					const uint64_t a = neg ? static_cast<uint64_t>(-(v + 1)) + 1 : static_cast<uint64_t>(v);
					const uint64_t ip = a / base;
					const uint64_t fp = a % base;
					if (neg)
						sprintf_s(buf, sizeof(buf), "-%llu.%0*llu", ip, static_cast<int>(probScale), fp);
					else
						sprintf_s(buf, sizeof(buf), "%llu.%0*llu", ip, static_cast<int>(probScale), fp);
				}
				probs.push_back(buf);
			}
		}
		else
		{
			for (uint64_t i = 0; i < n; ++i)
			{
				const uint64_t len = uleb(c);
				if (c.p + len > c.end)
					fail("m2 raw probability exceeds stream boundary");
				probs.emplace_back(reinterpret_cast<const char*>(c.p), static_cast<size_t>(len));
				c.p += len;
			}
		}
		if (c.p != c.end)
			fail("m2 probability stream has trailing bytes");
	}

	// --- prev/next refs（delta + zigzag）---
	std::vector<uint32_t> prevRefs(static_cast<size_t>(n)), nextRefs(static_cast<size_t>(n));
	{
		Cursor c{ raw + prevOff, raw + nextOff };
		uint64_t prev = 0;
		for (uint64_t i = 0; i < n; ++i)
		{
			const uint64_t cur = static_cast<uint64_t>(
				static_cast<int64_t>(prev) + zigzagDecode(uleb(c)));
			if (cur >= refLimit)
				fail("m2 prev reference out of range");
			prevRefs[static_cast<size_t>(i)] = static_cast<uint32_t>(cur);
			prev = cur;
		}
		if (c.p != c.end)
			fail("m2 prev-ref stream has trailing bytes");
	}
	{
		Cursor c{ raw + nextOff, raw + size };
		uint64_t prev = 0;
		for (uint64_t i = 0; i < n; ++i)
		{
			const uint64_t cur = static_cast<uint64_t>(
				static_cast<int64_t>(prev) + zigzagDecode(uleb(c)));
			if (cur >= refLimit)
				fail("m2 next reference out of range");
			nextRefs[static_cast<size_t>(i)] = static_cast<uint32_t>(cur);
			prev = cur;
		}
		if (c.p != c.end)
			fail("m2 next-ref stream has trailing bytes");
	}

	// --- 写出 CSV ---
	csvWriteRow(out, { "prob", "prev_word", "next_word", "prev_fullPinyin", "next_fullPinyin" });
	for (uint64_t i = 0; i < n; ++i)
	{
		const size_t idx = static_cast<size_t>(i);
		const uint32_t prevRowIdx = denseRefs ? refToRow[prevRefs[idx]] : prevRefs[idx];
		const uint32_t nextRowIdx = denseRefs ? refToRow[nextRefs[idx]] : nextRefs[idx];
		const LsortRow& pr = lsortRows[prevRowIdx];
		const LsortRow& nr = lsortRows[nextRowIdx];
		csvWriteRow(out, { probs[idx], pr.word, nr.word, pr.fullPinyin, nr.fullPinyin });
	}
}

// ---------------------------------------------------------------------------
// Bundle v2 解码
// ---------------------------------------------------------------------------

void decodeBundle(const std::vector<uint8_t>& raw, const char* lsortOutPath, const char* m2OutPath)
{
	static const char BUNDLE_MAGIC[8] = { 'K', 'D', 'B', 'N', 'D', 'L', '2', '\0' };
	constexpr size_t BUNDLE_HEADER_SIZE = 64;

	if (raw.size() < BUNDLE_HEADER_SIZE)
		fail("bundle too small for header");
	if (std::memcmp(raw.data(), BUNDLE_MAGIC, 8) != 0)
		fail("bad bundle magic");

	const uint64_t version = readU64(raw.data() + 8);
	const uint64_t flags = readU64(raw.data() + 16);
	const uint64_t lsortOff = readU64(raw.data() + 24);
	const uint64_t lsortSize = readU64(raw.data() + 32);
	const uint64_t m2Off = readU64(raw.data() + 40);
	const uint64_t m2Size = readU64(raw.data() + 48);
	const uint64_t file_size = readU64(raw.data() + 56);

	if (version != 2)
		fail("unsupported bundle version");
	if (flags != 0)
		fail("unsupported bundle flags");
	if (file_size != raw.size())
		fail("bundle size mismatch");
	if (lsortOff != BUNDLE_HEADER_SIZE)
		fail("unexpected l_sort bundle offset");
	if (lsortOff + lsortSize != m2Off)
		fail("bundle l_sort size/offset mismatch");
	if (m2Off + m2Size != raw.size())
		fail("bundle m2 size/offset mismatch");

	// l_sort 解码：写 CSV + 保留 m2 引用所需字段
	std::vector<LsortRow> lsortRows;
	{
		FILE* out = nullptr;
		if (fopen_s(&out, lsortOutPath, "wb") != 0 || !out)
			fail("cannot open l_sort output file");
		fwrite("\xEF\xBB\xBF", 1, 3, out);  // UTF-8 BOM（字节顺序标记）
		decodeLsortBlock(raw.data() + lsortOff, static_cast<size_t>(lsortSize), out, lsortRows);
		if (fclose(out) != 0)
			fail("failed to write l_sort output file");
	}

	// m2 解码
	{
		FILE* out = nullptr;
		if (fopen_s(&out, m2OutPath, "wb") != 0 || !out)
			fail("cannot open m2 output file");
		fwrite("\xEF\xBB\xBF", 1, 3, out);  // UTF-8 BOM（字节顺序标记）
		decodeM2Block(raw.data() + m2Off, static_cast<size_t>(m2Size), lsortRows, out);
		if (fclose(out) != 0)
			fail("failed to write m2 output file");
	}
}

} // namespace

int main(int argc, char** argv)
{
	if (argc != 4)
	{
		std::fprintf(stderr,
			"usage: coral_bundle_decoder <bundle.dictpack.lzma2> <coral_lexicon.csv> <coral_languagemodel.csv>\n"
			"  Decompresses a DictPack v2 bundle into coral_lexicon.csv + coral_languagemodel.csv (UTF-8 BOM, CRLF).\n");
		return 2;
	}

	try
	{
		std::fprintf(stderr, "Decompressing %s ...\n", argv[1]);
		const std::vector<uint8_t> raw = xzDecompressFile(argv[1]);
		std::fprintf(stderr, "  raw size: %zu bytes\n", raw.size());
		decodeBundle(raw, argv[2], argv[3]);
		std::fprintf(stderr, "Done.\n");
		return 0;
	}
	catch (const DecodeError& e)
	{
		std::fprintf(stderr, "ERROR: %s\n", e.msg);
		return 1;
	}
	catch (const std::exception& e)
	{
		std::fprintf(stderr, "ERROR: %s\n", e.what());
		return 1;
	}
}

#include "pch.h"
#include "lexicon_importer.h"
#include "syllable_parser.h"
#include "syllable_catalog.h"

#include <algorithm>
#include <cmath>
#include <functional>

namespace
{
	constexpr size_t kMaxPhraseChars = 24;
	constexpr size_t kMaxSegmentChars = 8;
	constexpr size_t kReadingsPerChar = 4;
	constexpr size_t kWholeWordCombos = 512;
	constexpr size_t kSegmentCombos = 64;
	constexpr size_t kMaxShortPinyinChars = 10;
	constexpr size_t kMaxSyllableBytes = 6;
	// 次常用读音的词频达到最常用读音的 1/10 即视为需要人工确认
	const float kAmbiguousGap = std::log(10.0f);

	// 解码一个 UTF-8 码点；非法序列返回 false
	bool DecodeUtf8(const std::string& text, size_t& pos, char32_t& cp)
	{
		const auto byte = [&](size_t i) { return static_cast<unsigned char>(text[i]); };
		const unsigned char lead = byte(pos);
		size_t length = lead < 0x80 ? 1 : (lead >> 5) == 0x6 ? 2 : (lead >> 4) == 0xE ? 3 : (lead >> 3) == 0x1E ? 4 : 0;
		if (length == 0 || pos + length > text.size())
			return false;
		cp = length == 1 ? lead : length == 2 ? (lead & 0x1F) : length == 3 ? (lead & 0x0F) : (lead & 0x07);
		for (size_t i = 1; i < length; ++i)
		{
			if ((byte(pos + i) & 0xC0) != 0x80)
				return false;
			cp = (cp << 6) | (byte(pos + i) & 0x3F);
		}
		pos += length;
		return true;
	}

	bool IsHan(char32_t cp)
	{
		return cp == 0x3007 || (cp >= 0x3400 && cp <= 0x4DBF) || (cp >= 0x4E00 && cp <= 0x9FFF) ||
			(cp >= 0xF900 && cp <= 0xFAFF) || (cp >= 0x20000 && cp <= 0x2EBEF) || (cp >= 0x30000 && cp <= 0x3134F);
	}

	// 拼音字符规范化：大写转小写、去声调、ü→v；分隔符返回 ' '，无法识别返回 0
	char PinyinChar(char32_t cp)
	{
		if (cp >= 'a' && cp <= 'z') return static_cast<char>(cp);
		if (cp >= 'A' && cp <= 'Z') return static_cast<char>(cp - 'A' + 'a');
		if ((cp >= '0' && cp <= '9') || cp == '\'' || cp == ' ' || cp == '\t' || cp == '-' || cp == '_' ||
			cp == '.' || cp == ',' || cp == '|' || cp == '/' || cp == 0x2018 || cp == 0x2019 || cp == 0xB7 ||
			cp == 0x3000)
			return ' ';
		switch (cp)
		{
		case 0x101: case 0xE1: case 0x1CE: case 0xE0: case 0x100: case 0xC1: case 0x1CD: case 0xC0: return 'a';
		case 0x113: case 0xE9: case 0x11B: case 0xE8: case 0xEA: case 0x112: case 0xC9: case 0x11A: case 0xC8: return 'e';
		case 0x12B: case 0xED: case 0x1D0: case 0xEC: case 0x12A: case 0xCD: case 0x1CF: case 0xCC: return 'i';
		case 0x14D: case 0xF3: case 0x1D2: case 0xF2: case 0x14C: case 0xD3: case 0x1D1: case 0xD2: return 'o';
		case 0x16B: case 0xFA: case 0x1D4: case 0xF9: case 0x16A: case 0xDA: case 0x1D3: case 0xD9: return 'u';
		case 0xFC: case 0x1D6: case 0x1D8: case 0x1DA: case 0x1DC: case 0xDC: return 'v';
		case 0x144: case 0x148: case 0x1F9: return 'n';
		case 0x1E3F: return 'm';
		default: return 0;
		}
	}

	std::string Join(const std::vector<std::string>& parts)
	{
		std::string out;
		for (const auto& part : parts)
		{
			if (!out.empty()) out += '\'';
			out += part;
		}
		return out;
	}

	std::vector<std::string> SplitApostrophe(const std::string& text)
	{
		std::vector<std::string> parts;
		size_t start = 0;
		while (start <= text.size())
		{
			const size_t end = text.find('\'', start);
			parts.push_back(text.substr(start, end == std::string::npos ? std::string::npos : end - start));
			if (end == std::string::npos) break;
			start = end + 1;
		}
		return parts;
	}

	coral::history::Partition PartitionOf(coral::LanguageVariant charset)
	{
		return charset == coral::LanguageVariant::Traditional ? coral::history::Partition::Traditional
			: coral::history::Partition::Simplified;
	}

	std::string IdentityKey(coral::history::Partition partition, const std::string& lookupKey, const std::string& phrase)
	{
		std::string key(1, static_cast<char>('0' + static_cast<int>(partition)));
		key += '\x1F';
		key += lookupKey;
		key += '\x1F';
		key += phrase;
		return key;
	}

	void SetInvalid(UserLexiconImportItem& item, std::string message)
	{
		item.status = UserLexiconImportStatus::Invalid;
		item.message = std::move(message);
	}
}

coral::phonetics::LexiconImporter& coral::phonetics::LexiconImporter::instance()
{
	static coral::phonetics::LexiconImporter importer;
	return importer;
}

bool coral::phonetics::LexiconImporter::ensure_readings()
{
	if (readings_ready_)
		return true;
	const coral::phonetics::SystemLexicon& primary = coral::phonetics::SyllableParser::instance().get_lexicon_manager().get_primary_lexicon();
	if (!primary.is_loaded())
		return false;
	primary.for_each_single_char_entry([this](std::string_view word, std::string_view pinyin, float weight) {
		// 单字键里混有简拼（c、zh 等），只取完整音节
		if (!validPinyinCombines.count(pinyin))
			return;
		auto& readings = readings_[std::string(word)];
		const auto it = std::find_if(readings.begin(), readings.end(), [&](const Reading& r) { return r.pinyin == pinyin; });
		if (it == readings.end())
			readings.push_back({ std::string(pinyin), weight });
		else
			it->weight = (std::max)(it->weight, weight);
	});
	for (auto& [ch, readings] : readings_)
		std::sort(readings.begin(), readings.end(), [](const Reading& a, const Reading& b) { return a.weight > b.weight; });
	readings_ready_ = !readings_.empty();
	LOG_INFO("Langengine: lexicon import readings built, chars=" + std::to_string(readings_.size()));
	return readings_ready_;
}

bool coral::phonetics::LexiconImporter::is_ambiguous(const std::string& ch) const
{
	const auto it = readings_.find(ch);
	return it != readings_.end() && it->second.size() > 1 &&
		it->second[1].weight >= it->second[0].weight - kAmbiguousGap;
}

bool coral::phonetics::LexiconImporter::dictionary_pinyin(const std::vector<std::string>& chars, size_t from, size_t to,
	size_t comboLimit, std::vector<std::string>& out) const
{
	const coral::phonetics::SystemLexicon& primary = coral::phonetics::SyllableParser::instance().get_lexicon_manager().get_primary_lexicon();
	std::string word;
	std::vector<const std::vector<Reading>*> options;
	for (size_t i = from; i < to; ++i)
	{
		const auto it = readings_.find(chars[i]);
		if (it == readings_.end())
			return false;
		options.push_back(&it->second);
		word += chars[i];
	}
	// 里程表式枚举，先试各字最常用的读音；读音组合按 comboLimit 截断
	std::vector<size_t> pick(options.size(), 0);
	float best_weight = 0.0f;
	bool found = false;
	for (size_t tried = 0; tried < comboLimit; ++tried)
	{
		std::string key;
		for (size_t i = 0; i < options.size(); ++i)
		{
			if (i) key += '\'';
			key += (*options[i])[pick[i]].pinyin;
		}
		float weight = 0.0f;
		if (primary.find_phrase(key, word, weight) && (!found || weight > best_weight))
		{
			found = true;
			best_weight = weight;
			out.clear();
			for (size_t i = 0; i < options.size(); ++i)
				out.push_back((*options[i])[pick[i]].pinyin);
		}
		size_t digit = options.size();
		while (digit > 0)
		{
			--digit;
			const size_t limit = (std::min)(kReadingsPerChar, options[digit]->size());
			if (++pick[digit] < limit) break;
			pick[digit] = 0;
			if (digit == 0) return found;
		}
	}
	return found;
}

bool coral::phonetics::LexiconImporter::annotate(const std::vector<std::string>& chars, std::string& pinyin, bool& polyphonic,
	std::string& missing) const
{
	const size_t n = chars.size();
	for (const auto& ch : chars)
	{
		if (readings_.find(ch) == readings_.end())
		{
			missing = ch;
			return false;
		}
	}
	std::vector<std::string> whole;
	if (n >= 2 && dictionary_pinyin(chars, 0, n, kWholeWordCombos, whole))
	{
		pinyin = Join(whole);
		polyphonic = false;
		return true;
	}
	// 按主词典词语切分：段数最少者优先（等价于尽量用长词确认读音），单字兜底
	struct Cell { size_t segments = SIZE_MAX; size_t from = 0; std::vector<std::string> pinyin; };
	std::vector<Cell> best(n + 1);
	best[0].segments = 0;
	for (size_t end = 1; end <= n; ++end)
	{
		const size_t lowest = end > kMaxSegmentChars ? end - kMaxSegmentChars : 0;
		for (size_t start = lowest; start + 1 < end; ++start)
		{
			if (best[start].segments == SIZE_MAX || best[start].segments + 1 >= best[end].segments)
				continue;
			std::vector<std::string> segment;
			if (dictionary_pinyin(chars, start, end, kSegmentCombos, segment))
				best[end] = { best[start].segments + 1, start, std::move(segment) };
		}
		if (best[end - 1].segments + 1 < best[end].segments)
			best[end] = { best[end - 1].segments + 1, end - 1, { readings_.at(chars[end - 1]).front().pinyin } };
	}
	std::vector<std::string> syllables;
	polyphonic = false;
	for (size_t end = n; end > 0; end = best[end].from)
	{
		const Cell& cell = best[end];
		if (end - cell.from == 1 && is_ambiguous(chars[cell.from]))
			polyphonic = true;
		syllables.insert(syllables.begin(), cell.pinyin.begin(), cell.pinyin.end());
	}
	pinyin = Join(syllables);
	return true;
}

std::string coral::phonetics::LexiconImporter::canonical_syllable(const std::string& token) const
{
	if (validPinyinCombines.count(token))
		return token;
	// ü 的两种写法：lue/nue ↔ lve/nve
	std::string alternative = token;
	if (alternative.size() >= 2 && (alternative[0] == 'l' || alternative[0] == 'n'))
	{
		if (alternative[1] == 'u') alternative[1] = 'v';
		else if (alternative[1] == 'v') alternative[1] = 'u';
		if (validPinyinCombines.count(alternative))
			return alternative;
	}
	return {};
}

bool coral::phonetics::LexiconImporter::split_syllables(const std::string& text, size_t count, std::vector<std::string>& out) const
{
	// reachable[pos][k]：从 pos 开始能否恰好切成 k 个音节
	const size_t length = text.size();
	if (count == 0 || length < count)
		return false;
	std::vector<std::vector<signed char>> memo(length + 1, std::vector<signed char>(count + 1, -1));
	std::function<bool(size_t, size_t)> reachable = [&](size_t pos, size_t left) -> bool {
		if (left == 0) return pos == length;
		if (pos >= length) return false;
		auto& cached = memo[pos][left];
		if (cached >= 0) return cached != 0;
		bool ok = false;
		for (size_t len = (std::min)(kMaxSyllableBytes, length - pos); len > 0 && !ok; --len)
			ok = !canonical_syllable(text.substr(pos, len)).empty() && reachable(pos + len, left - 1);
		cached = ok ? 1 : 0;
		return ok;
	};
	if (!reachable(0, count))
		return false;
	out.clear();
	size_t pos = 0;
	for (size_t left = count; left > 0; --left)
	{
		for (size_t len = (std::min)(kMaxSyllableBytes, length - pos); len > 0; --len)
		{
			const std::string syllable = canonical_syllable(text.substr(pos, len));
			if (!syllable.empty() && reachable(pos + len, left - 1))
			{
				out.push_back(syllable);
				pos += len;
				break;
			}
		}
	}
	return true;
}

bool coral::phonetics::LexiconImporter::normalize_pinyin(const std::string& raw, size_t count, std::vector<std::string>& syllables) const
{
	std::vector<std::string> tokens(1);
	size_t pos = 0;
	while (pos < raw.size())
	{
		char32_t cp = 0;
		if (!DecodeUtf8(raw, pos, cp))
			return false;
		if (cp == ':' && !tokens.back().empty() && tokens.back().back() == 'u')
		{
			tokens.back().back() = 'v';
			continue;
		}
		const char c = PinyinChar(cp);
		if (c == 0)
			return false;
		if (c == ' ')
		{
			if (!tokens.back().empty()) tokens.emplace_back();
		}
		else
		{
			tokens.back() += c;
		}
	}
	if (tokens.back().empty())
		tokens.pop_back();
	if (tokens.empty())
		return false;
	if (tokens.size() == count)
	{
		syllables.clear();
		for (const auto& token : tokens)
		{
			const std::string syllable = canonical_syllable(token);
			if (syllable.empty()) break;
			syllables.push_back(syllable);
		}
		if (syllables.size() == count)
			return true;
	}
	std::string joined;
	for (const auto& token : tokens)
		joined += token;
	return split_syllables(joined, count, syllables);
}

void coral::phonetics::LexiconImporter::prepare_item(UserLexiconImportItem& item, const UserLexiconImportOptions& options) const
{
	item.pinyin.clear();
	item.lookupKey.clear();
	item.message.clear();
	item.status = UserLexiconImportStatus::Ready;
	item.source = UserLexiconPinyinSource::None;
	item.polyphonic = false;

	if (item.phrase.empty())
		return SetInvalid(item, "未识别到词语");
	std::vector<std::string> chars;
	size_t pos = 0;
	while (pos < item.phrase.size())
	{
		const size_t start = pos;
		char32_t cp = 0;
		if (!DecodeUtf8(item.phrase, pos, cp))
			return SetInvalid(item, "词语编码异常");
		if (!IsHan(cp))
			return SetInvalid(item, "包含非汉字字符");
		chars.push_back(item.phrase.substr(start, pos - start));
	}
	if (chars.size() > kMaxPhraseChars)
		return SetInvalid(item, "词语过长（超过 " + std::to_string(kMaxPhraseChars) + " 个字）");

	const std::string& filePinyin = item.rawPinyin.empty() ? item.rawLookupKey : item.rawPinyin;
	if (!filePinyin.empty())
	{
		std::vector<std::string> syllables;
		if (!normalize_pinyin(filePinyin, chars.size(), syllables))
			return SetInvalid(item, "拼音与词语字数不符或含无效拼音：" + filePinyin);
		item.pinyin = Join(syllables);
		item.lookupKey = item.pinyin;
		item.source = UserLexiconPinyinSource::File;
		if (!item.rawLookupKey.empty())
		{
			// 自有 CSV 的 Pinyin 字段可能是简拼：逐段须是对应全拼音节的前缀
			std::string lookup = item.rawLookupKey;
			std::transform(lookup.begin(), lookup.end(), lookup.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			const auto parts = SplitApostrophe(lookup);
			bool ok = parts.size() == syllables.size();
			for (size_t i = 0; ok && i < parts.size(); ++i)
				ok = !parts[i].empty() && syllables[i].compare(0, parts[i].size(), parts[i]) == 0;
			if (!ok)
				return SetInvalid(item, "Pinyin 与 FullPinyin 字段不一致：" + item.rawLookupKey);
			item.lookupKey = lookup;
		}
		return;
	}
	if (!options.autoPinyin)
		return SetInvalid(item, "缺少拼音（未开启自动补全拼音）");
	std::string missing;
	if (!annotate(chars, item.pinyin, item.polyphonic, missing))
		return SetInvalid(item, "无法为“" + missing + "”生成拼音，请在文件中提供拼音");
	item.lookupKey = item.pinyin;
	item.source = UserLexiconPinyinSource::Generated;
}

bool coral::phonetics::LexiconImporter::prepare(std::vector<UserLexiconImportItem>& items, const UserLexiconImportOptions& options)
{
	std::lock_guard<std::mutex> lock(mutex_);
	coral::phonetics::PersonalLexicon& user = coral::phonetics::SyllableParser::instance().get_lexicon_manager().get_user_lexicon();
	if (!ensure_readings() || !user.is_alive())
		return false;

	std::unordered_set<std::string> existing;
	for (const auto& entry : user.history_entries())
	{
		if (!(entry.flags & coral::history::kDeleted))
			existing.insert(IdentityKey(entry.partition, entry.lookup_key, entry.phrase));
	}
	std::unordered_map<std::string, int> seen;
	for (auto& item : items)
	{
		prepare_item(item, options);
		if (item.status != UserLexiconImportStatus::Ready)
			continue;
		const std::string key = IdentityKey(PartitionOf(item.charset), item.lookupKey, item.phrase);
		const auto [it, inserted] = seen.emplace(key, item.line);
		if (!inserted)
		{
			item.status = UserLexiconImportStatus::Repeated;
			item.message = "与第 " + std::to_string(it->second) + " 行重复";
		}
		else if (options.skipExisting && existing.count(key))
		{
			item.status = UserLexiconImportStatus::Exists;
			item.message = "用户词库中已存在";
		}
	}
	return true;
}

UserLexiconImportReport coral::phonetics::LexiconImporter::commit(const std::vector<UserLexiconImportItem>& items)
{
	std::lock_guard<std::mutex> lock(mutex_);
	UserLexiconImportReport report;
	coral::phonetics::PersonalLexicon& user = coral::phonetics::SyllableParser::instance().get_lexicon_manager().get_user_lexicon();
	for (const auto& item : items)
	{
		if (item.status == UserLexiconImportStatus::Exists || item.status == UserLexiconImportStatus::Repeated)
			++report.skipped;
		else if (item.status == UserLexiconImportStatus::Invalid)
			++report.failed;
	}
	const auto fail = [&report](int line, const std::string& reason) {
		++report.failed;
		report.failures.emplace_back(line, reason);
	};
	if (!user.is_alive())
	{
		for (const auto& item : items)
			if (item.status == UserLexiconImportStatus::Ready) fail(item.line, "用户词库不可用");
		return report;
	}

	std::unordered_set<std::string> existing;
	const auto current = user.history_entries();
	for (const auto& entry : current)
		existing.insert(IdentityKey(entry.partition, entry.lookup_key, entry.phrase));
	size_t total = current.size();
	// 导入词条视同刚输入过一次：沿用当前序号，不推进时钟，避免已有词条的热度被整体衰减
	const std::uint64_t sequence = user.history_sequence() ? user.history_sequence() : 1;
	std::vector<coral::history::Entry> entries;
	std::vector<coral::history::EntryIdentity> added;
	std::vector<int> lines;
	const auto append = [&](const UserLexiconImportItem& item, const std::string& lookupKey, std::uint16_t flags) {
		const auto partition = PartitionOf(item.charset);
		const std::string key = IdentityKey(partition, lookupKey, item.phrase);
		const bool isNew = existing.insert(key).second;
		coral::history::Entry entry;
		entry.partition = partition;
		entry.flags = flags;
		entry.lookup_key = lookupKey;
		entry.full_pinyin = item.pinyin;
		entry.phrase = item.phrase;
		entry.commit_count = 1;
		entry.score_state = 1;
		entry.last_used_sequence = sequence;
		if (isNew)
		{
			added.push_back({ partition, lookupKey, item.phrase });
			++total;
		}
		entries.push_back(std::move(entry));
	};
	for (const auto& item : items)
	{
		if (item.status != UserLexiconImportStatus::Ready)
			continue;
		if (total >= static_cast<size_t>(MAX_CAPACITY))
		{
			fail(item.line, "超出用户词库容量上限（" + std::to_string(MAX_CAPACITY) + " 条）");
			continue;
		}
		append(item, item.lookupKey, item.lookupKey == item.pinyin ? coral::history::kNone : coral::history::kShortPinyin);
		lines.push_back(item.line);
		// 与输入时自学习一致：额外写一条简拼索引，导入后可直接用简拼打出
		std::string shortPinyin;
		if (item.rawLookupKey.empty() && total < static_cast<size_t>(MAX_CAPACITY) &&
			hlp::utf8ToWstringSize(item.phrase) <= kMaxShortPinyinChars &&
			coral::phonetics::PersonalLexicon::get_jian_pin_code(item.pinyin, shortPinyin) && shortPinyin != item.pinyin &&
			!existing.count(IdentityKey(PartitionOf(item.charset), shortPinyin, item.phrase)))
		{
			append(item, shortPinyin, coral::history::kShortPinyin);
		}
	}
	if (entries.empty())
		return report;
	if (!user.import_history_entries(std::move(entries)))
	{
		for (int line : lines) fail(line, "写入用户词库失败");
		return report;
	}
	report.imported = static_cast<int>(lines.size());
	last_import_ = std::move(added);
	coral::phonetics::SyllableParser::instance().clear_pinyin_model_cache();
	LOG_INFO("Langengine: user lexicon import committed, imported=" + std::to_string(report.imported) +
		", skipped=" + std::to_string(report.skipped) + ", failed=" + std::to_string(report.failed));
	return report;
}

bool coral::phonetics::LexiconImporter::revert_last()
{
	std::lock_guard<std::mutex> lock(mutex_);
	if (last_import_.empty())
		return false;
	coral::phonetics::PersonalLexicon& user = coral::phonetics::SyllableParser::instance().get_lexicon_manager().get_user_lexicon();
	if (!user.remove_history_entries(last_import_))
		return false;
	LOG_INFO("Langengine: user lexicon import reverted, entries=" + std::to_string(last_import_.size()));
	last_import_.clear();
	coral::phonetics::SyllableParser::instance().clear_pinyin_model_cache();
	return true;
}

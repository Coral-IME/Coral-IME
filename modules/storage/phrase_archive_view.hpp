/*
* @author: liuzhuanghuai@coral.cn
* @date 2025-06-23 10:03:33
*/
#pragma once
#ifndef CORAL_MODULES_STORAGE_PHRASE_ARCHIVE_VIEW_HPP_INCLUDED
#define CORAL_MODULES_STORAGE_PHRASE_ARCHIVE_VIEW_HPP_INCLUDED
#ifdef OS_WINDOW
#include <windows.h>
#endif

#include <fstream>
#include <string>
#include <cstdint>
#include <iostream>
#include <cstring>
#include <string_view>
#include "slice_view.h"
/**********************************************************************************/
//专门为了PhraseInfo适配的数据结构
#include "lexicon_source.h"
#include "model_view.h"
#include "bigram_table.h"

#pragma pack(push, 1)
namespace coral::storage {
struct PhraseArchiveHeader {
	uint32_t outer_size{};
};
} // 命名空间 coral::storage 结束（批次 3）

namespace coral::storage {
struct PhraseArchiveIndex {
	uint32_t begin{};
	uint32_t count{};
};
} // 命名空间 coral::storage 结束（批次 3）

namespace coral::storage {
struct LegacyPhraseRecord {
	uint32_t    phrase_off{};
	uint32_t    pinyin_off{};
	coral::phonetics::Score weight{};
	uint32_t    id{};
	coral::phonetics::Score backprob{};
};
} // 命名空间 coral::storage 结束（批次 3）

namespace coral::storage {
struct PhraseRecord {
	uint32_t    phrase_off{};
	uint32_t    pinyin_off{};
	coral::phonetics::Score weight{};
	uint32_t    id{};
	coral::phonetics::Score backprob{};
	bool        b_in_m2{};
};
} // 命名空间 coral::storage 结束（批次 3）

#pragma pack(pop)

namespace coral::storage {
struct PhraseRecordView {
	std::string_view word;
	std::string_view pinyin;
	coral::phonetics::Score      weight{};
	uint32_t         id{};
	coral::phonetics::Score      backprob{};
	bool             b_in_m2{};
};
} // 命名空间 coral::storage 结束（批次 3）

namespace coral::storage {
struct OwnedPhraseRecord {
	OwnedPhraseRecord() = default;
	OwnedPhraseRecord(const std::string& word, const std::string& pinyin, coral::phonetics::Score weight, uint32_t wordId, coral::phonetics::Score backProb, bool b_in_m2 = false)
		: phrase(word), phrase_pinyin(pinyin), phrase_weight(weight), id(wordId), backprob(backProb), b_in_m2(b_in_m2) {}
	string      phrase;
	string      phrase_pinyin;
	coral::phonetics::Score phrase_weight{};
	uint32_t    id{};
	coral::phonetics::Score backprob{};
	bool        b_in_m2{};
};
} // 命名空间 coral::storage 结束（批次 3）

/*
* 结构布局 后续很方便适配string_view
┌────────────┐
│FileHeader  │  (外层 vector 数量)
├────────────┤
│OuterIndex[]│  (outer_size 个元素，记录每个内层有几项)
├────────────┤
│PackedInfo[]│  (所有 PhraseInfo，顺序 = 外层遍历 + 内层遍历)
├────────────┤
│StringPool  │  所有字符串按 '\0' 终止连在一起
└────────────┘

*/

/**********************************************************************************/

constexpr size_t kLegacyPhraseRecordSize = sizeof(coral::storage::LegacyPhraseRecord);

static bool dump_lexicon(const std::vector<std::vector<coral::storage::OwnedPhraseRecord>>& src,
	const std::filesystem::path& path, bool enableInM2 = true)
{
	std::ofstream ofs(path, std::ios::binary);
	if (!ofs) { return false; }

	coral::storage::PhraseArchiveHeader hdr{ static_cast<uint32_t>(src.size()) };
	std::vector<coral::storage::PhraseArchiveIndex> index;
	std::vector<coral::storage::PhraseRecord> infos;
	std::string pool;
	pool.reserve(1 << 20); // todo 第一次添加之后可以修改数值为实际大小

	uint32_t begin = 0;
	for (auto const& vec : src) {
		index.push_back({ begin, static_cast<uint32_t>(vec.size()) });
		for (auto const& ph : vec) {
			coral::storage::PhraseRecord pi;
			pi.phrase_off = static_cast<uint32_t>(pool.size());
			pool.append(ph.phrase).push_back('\0');
			pi.pinyin_off = static_cast<uint32_t>(pool.size());
			pool.append(ph.phrase_pinyin).push_back('\0');
			pi.weight = ph.phrase_weight;
			pi.id = ph.id;
			pi.backprob = ph.backprob;
			pi.b_in_m2 = ph.b_in_m2;
			infos.push_back(pi);
		}
		begin += index.back().count;
	}

	ofs.write(reinterpret_cast<char*>(&hdr), sizeof hdr);
	ofs.write(reinterpret_cast<char*>(index.data()), index.size() * sizeof(coral::storage::PhraseArchiveIndex));
	if (enableInM2) {
		ofs.write(reinterpret_cast<char*>(infos.data()), infos.size() * sizeof(coral::storage::PhraseRecord));
	} else {
		for (auto& pi : infos)
			ofs.write(reinterpret_cast<char*>(&pi), kLegacyPhraseRecordSize);
	}
	ofs.write(pool.data(), pool.size());
	return true;
}

namespace coral::storage {
class PhraseArchiveView : public coral::storage::ModelView {
public:
	void set_enable_in_m2(bool enabled) { 
		enable_in_m2_ = enabled;
	}

	bool map(const std::wstring& fileName, const std::wstring& mapName, const std::string& identifier) override {
		if (!coral::storage::ModelView::map(fileName, mapName, identifier)) {
			return false;
		}
		parse();
		return true;
	}

	void reset() override
	{
		coral::storage::ModelView::reset();
		hdr_ = nullptr;
		idx_array_ = nullptr;
		infos_array_ = nullptr;
		pool_base_ = nullptr;
		converted_infos_.clear();
	}

	coral::foundation::SliceView<const coral::storage::PhraseRecord> operator[](size_t i) const {
		if (!view_ || !hdr_ || !idx_array_ || !infos_array_ || i >= outer_size()) {
			return coral::foundation::SliceView<const coral::storage::PhraseRecord>(nullptr, 0);
		}
		const coral::storage::PhraseArchiveIndex& outer_idx_info = idx_array_[i];
		return coral::foundation::SliceView<const coral::storage::PhraseRecord>(&infos_array_[outer_idx_info.begin], outer_idx_info.count);
	}
	coral::storage::PhraseRecordView get_phrase_view(const coral::storage::PhraseRecord& packedInfo) const {
		if (!pool_base_) {
			return {};
		}
		return {
			std::string_view(pool_base_ + packedInfo.phrase_off),
			std::string_view(pool_base_ + packedInfo.pinyin_off),
			packedInfo.weight,
			packedInfo.id,
			packedInfo.backprob,
			packedInfo.b_in_m2
		};
	}

	size_t outer_size() const { return hdr_ ? hdr_->outer_size : 0; }

private:
	void parse() {
		if (!view_) {
			hdr_ = nullptr;
			idx_array_ = nullptr;
			infos_array_ = nullptr;
			pool_base_ = nullptr;
			return;
		}

		hdr_ = reinterpret_cast<const coral::storage::PhraseArchiveHeader*>(view_);
		if (size_ < sizeof(coral::storage::PhraseArchiveHeader)) {
			hdr_ = nullptr;
			return;
		}

		idx_array_ = reinterpret_cast<const coral::storage::PhraseArchiveIndex*>(reinterpret_cast<const char*>(view_) + sizeof(coral::storage::PhraseArchiveHeader));

		// 解引用前确保 m_idxArray 未越界
		if (size_ < sizeof(coral::storage::PhraseArchiveHeader) + (size_t)hdr_->outer_size * sizeof(coral::storage::PhraseArchiveIndex)) {
			hdr_ = nullptr;
			return;
		}

		infos_array_ = reinterpret_cast<const coral::storage::PhraseRecord*>(idx_array_ + hdr_->outer_size);

		if (hdr_->outer_size == 0) {
			pool_base_ = reinterpret_cast<const char*>(infos_array_);
			return;
		}

		size_t total_packed_infos = 0;
		if (hdr_->outer_size > 0) {
			const coral::storage::PhraseArchiveIndex& last_outer_idx = idx_array_[hdr_->outer_size - 1];
			total_packed_infos = static_cast<size_t>(last_outer_idx.begin) + last_outer_idx.count;
		}

		const size_t infoSize = enable_in_m2_ ? sizeof(coral::storage::PhraseRecord) : kLegacyPhraseRecordSize;

		if (!enable_in_m2_) {
			const auto* src = reinterpret_cast<const coral::storage::LegacyPhraseRecord*>(idx_array_ + hdr_->outer_size);
			converted_infos_.resize(total_packed_infos);
			for (size_t i = 0; i < total_packed_infos; ++i) {
				const auto& compat = src[i];
				converted_infos_[i] = { compat.phrase_off, compat.pinyin_off, compat.weight, compat.id, compat.backprob, false };
			}
			infos_array_ = converted_infos_.data();
		}
		pool_base_ = reinterpret_cast<const char*>(idx_array_ + hdr_->outer_size) + total_packed_infos * infoSize;

		if (pool_base_ < reinterpret_cast<const char*>(view_) || pool_base_ > reinterpret_cast<const char*>(view_) + size_) {
			hdr_ = nullptr;
			idx_array_ = nullptr;
			infos_array_ = nullptr;
			pool_base_ = nullptr;
		}
	}

	bool enable_in_m2_ = true;
	std::vector<coral::storage::PhraseRecord> converted_infos_;
	const coral::storage::PhraseArchiveHeader* hdr_ = nullptr;
	const coral::storage::PhraseArchiveIndex* idx_array_ = nullptr;
	const coral::storage::PhraseRecord* infos_array_ = nullptr;
	const char* pool_base_ = nullptr;
};
} // 命名空间 coral::storage 结束（批次 3）


// languagemodel.coral 二元语言模型视图：整个 languagemodel.coral 文件作为只读内存映射，
// 加载时完成 header / 区段边界 / 规范布局校验，失败即 invalid。
namespace coral::storage {
class BigramMappedView : public coral::storage::ModelView
{
public:
	bool map(const std::wstring& fileName, const std::wstring& mapName, const std::string& identifier) override
	{
		if (!coral::storage::ModelView::map(fileName, mapName, identifier))
			return false;
		if (model_.bind_data(view_, size_))
			return true;
		reset();
		return false;
	}

	const coral::phonetics::BigramTable& model() const { return model_; }
	bool valid() const { return model_.valid(); }

protected:
	void reset() override
	{
		model_.reset_data();
		coral::storage::ModelView::reset();
	}

private:
	coral::phonetics::BigramTable model_;
};
} // 命名空间 coral::storage 结束（批次 3）


#endif // SERIALIZATION_HPP（序列化头文件）

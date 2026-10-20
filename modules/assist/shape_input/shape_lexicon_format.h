#pragma once
#include <fstream>
#include <vector>
#include <string>
#include <cstdint>
#include <string_view>
#include <filesystem>
#include <unordered_map>
#include <cstring>
#include "shape_entry.h"
#include "slice_view.h"
#pragma pack(push, 1)
namespace coral::storage {
struct ShapeArchiveHeader
{
    uint32_t outer_size;
};
} // 命名空间 coral::storage 结束（批次 3）

namespace coral::storage {
struct ShapeArchiveIndex
{
    uint32_t begin;
    uint32_t count;
};
} // 命名空间 coral::storage 结束（批次 3）

namespace coral::storage {
struct ShapePhraseRecord
{
	uint32_t word_off;
	uint32_t pinyin_off;
};
} // 命名空间 coral::storage 结束（批次 3）

#pragma pack(pop)

namespace coral::storage {
struct ShapePhraseView
{
	std::string_view word;
	std::string_view pinyin;
};
} // 命名空间 coral::storage 结束（批次 3）

/*
 * 结构布局 后续很方便适配string_view
 * ┌──────────────────────────────┐
 * │ ULexiconHeader               │  // 记录外层词条数（即笔画/部首串的数量）
 * ├──────────────────────────────┤
 * │ StrokeIndex[]                │  // 每个笔画串对应的词条数量和起始索引（大小为 outer_size）
 * ├──────────────────────────────┤
 * │ StrokePackedInfo[]           │  // 所有词条在字符串池中的偏移信息（大小为所有词条总数）
 * ├──────────────────────────────┤
 * │ 字符串池 char[]              │  // 所有汉字和拼音按 '\0' 分隔拼接
 * └──────────────────────────────┘
 *
 * 说明：
 * - outer_size 表示 StrokeIndex[] 的元素个数。
 * - StrokeIndex 记录每个键（部首/笔画串）对应的词条起始位置和数量。
 * - 每个词条由 StrokePackedInfo 描述其 word 和 pinyin 在字符串池中的偏移。
 * - 字符串池中依次拼接所有 word 和 pinyin，以 '\0' 分隔。
 */

// 存放U模式词典提取出来的数据，数据的索引（字符串）已经存放在trie中，这里只需要存放value
// 数据格式：1字符3字节的汉字 + 拼音
static bool dump_stroke_lexicon(const std::vector<std::vector<WordAndPinyin>>& data,
                       const std::filesystem::path& path)
{
    std::ofstream ofs(path, std::ios::binary);
	if (!ofs) {return false; }

	coral::storage::ShapeArchiveHeader count{ static_cast<uint32_t>(data.size()) };
    std::vector<coral::storage::ShapeArchiveIndex> index;
	std::vector<coral::storage::ShapePhraseRecord> packedInfos;
	std::string pool;
    // TODO：后续修改为pool实际占用的空间大小
	pool.reserve(1 << 18);

    std::unordered_map<std::string_view, uint32_t> offsets;
    const auto intern = [&](const std::string& value) {
        const auto found = offsets.find(value);
        if (found != offsets.end()) return found->second;
        const auto offset = static_cast<uint32_t>(pool.size());
        pool.append(value).push_back('\0');
        // 键指向源数据，其生命周期长于池构建器。
        offsets.emplace(value, offset);
        return offset;
    };
    uint32_t begin = 0;
    for (auto const& items : data)
    {
        index.push_back({ begin, static_cast<uint32_t>(items.size()) });
        for (auto const& item : items)
        {
            coral::storage::ShapePhraseRecord pi;
            pi.word_off = intern(item.word);
            pi.pinyin_off = intern(item.pinyin);
            packedInfos.emplace_back(pi);
        }
        begin += index.back().count;
    }

    // 存放总词条数的头部
	ofs.write(reinterpret_cast<const char*>(&count), sizeof(count));
    ofs.write(reinterpret_cast<const char*>(index.data()), index.size() * sizeof(coral::storage::ShapeArchiveIndex));
	ofs.write(reinterpret_cast<const char*>(packedInfos.data()), packedInfos.size() * sizeof(coral::storage::ShapePhraseRecord));
	ofs.write(pool.data(), pool.size());
	return static_cast<bool>(ofs);
}

namespace coral::assist {
class ShapeDataView
{
public:
    bool bind(const void* data, std::size_t bytes) {
        view_ = data;
        size_ = bytes;
        return parse();
    }

	size_t size() const { return hdr_ ? hdr_->outer_size : 0; }

    // 返回指定笔画串下的所有词条的偏移信息（是一个数组）
    // 只会返回词条的索引（偏移）信息，不会返回词条内容
    coral::foundation::SliceView<const coral::storage::ShapePhraseRecord> operator[](size_t i) const {
        if (!view_ || !hdr_ || !index_array_ || !infos_array_ || i >= size())
        {
            return coral::foundation::SliceView<const coral::storage::ShapePhraseRecord>(nullptr, 0);
        }
        const coral::storage::ShapeArchiveIndex& idx = index_array_[i];
        if (idx.begin > info_count_ || idx.count > info_count_ - idx.begin) return coral::foundation::SliceView<const coral::storage::ShapePhraseRecord>(nullptr, 0);
        return coral::foundation::SliceView<const coral::storage::ShapePhraseRecord>(&infos_array_[idx.begin], idx.count);
    }

    coral::storage::ShapePhraseView get_phrase_view(const coral::storage::ShapePhraseRecord& pi) const {
        const auto text = [&](uint32_t offset) -> std::string_view {
            if (!pool_ || offset >= pool_size_) return {};
            const char* begin = pool_ + offset;
            const auto* end = static_cast<const char*>(std::memchr(begin, 0, pool_size_ - offset));
            return end ? std::string_view(begin, end - begin) : std::string_view{};
        };
        return {text(pi.word_off), text(pi.pinyin_off)};
    }

private:
    bool parse() {
        hdr_ = nullptr;
        index_array_ = nullptr;
        infos_array_ = nullptr;
        pool_ = nullptr;
        if (!view_ || size_ < sizeof(coral::storage::ShapeArchiveHeader)) return false;
        const auto* header = static_cast<const coral::storage::ShapeArchiveHeader*>(view_);
        if (header->outer_size > (size_ - sizeof(coral::storage::ShapeArchiveHeader)) / sizeof(coral::storage::ShapeArchiveIndex)) return false;
        const auto* indices = reinterpret_cast<const coral::storage::ShapeArchiveIndex*>(header + 1);
        const std::size_t indexEnd = sizeof(coral::storage::ShapeArchiveHeader) + header->outer_size * sizeof(coral::storage::ShapeArchiveIndex);
        const std::size_t maxInfos = (size_ - indexEnd) / sizeof(coral::storage::ShapePhraseRecord);
        std::size_t total = 0;
        for (uint32_t i = 0; i < header->outer_size; ++i) {
            if (indices[i].begin != total || indices[i].count > maxInfos - total) return false;
            total += indices[i].count;
        }
        const auto* infos = reinterpret_cast<const coral::storage::ShapePhraseRecord*>(indices + header->outer_size);
        const std::size_t poolBytes = size_ - indexEnd - total * sizeof(coral::storage::ShapePhraseRecord);
        for (std::size_t i = 0; i < total; ++i)
            if (infos[i].word_off >= poolBytes || infos[i].pinyin_off >= poolBytes) return false;
        if (poolBytes && reinterpret_cast<const char*>(infos + total)[poolBytes - 1] != 0) return false;
        hdr_ = header;
        index_array_ = indices;
        infos_array_ = infos;
        info_count_ = total;
        pool_ = reinterpret_cast<const char*>(infos + total);
        pool_size_ = poolBytes;
        return true;
    }

private:
    const void* view_ = nullptr;
    std::size_t size_ = 0;
    // 分别指向数据文件的各个部分：头部、索引数组、词条偏移信息数组、字符串池
    const coral::storage::ShapeArchiveHeader* hdr_ = nullptr;      
    const coral::storage::ShapeArchiveIndex* index_array_ = nullptr;
	const coral::storage::ShapePhraseRecord* infos_array_ = nullptr;
	const char* pool_ = nullptr;
    std::size_t info_count_ = 0, pool_size_ = 0;
};
} // 命名空间 coral::assist 结束（批次 2 辅助输入）


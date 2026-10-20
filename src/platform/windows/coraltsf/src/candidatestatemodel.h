#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace coral::tsf
{

// 仅显示用的预编辑：原始输入加上分段候选拼音的音节分隔符。
// rawToDisplay 有 raw.size() + 1 个条目，把原始输入中的插入符
// 偏移映射到 text 中的偏移。
struct SegmentedComposition
{
	std::wstring text;
	std::vector<int> rawToDisplay;
};

// 分隔符只在候选字母与原始输入匹配时借用，
// 因此纠正/模糊候选绝不会改变用户输入的字母。
SegmentedComposition SegmentComposition(std::wstring_view raw, std::wstring_view segmentedPinyin);

struct CandidateStateItem
{
	std::uint64_t id = 0;
	std::wstring text;
	std::wstring pinyin;
	std::wstring commit_text; // 标签即文本的候选项为空。
	const std::wstring& word() const { return commit_text.empty() ? text : commit_text; }
};

// 与 UI 无关的候选集合与导航状态。渲染代码
// 只消费该对象的快照，从不拥有或修改输入状态。
class CandidateStateModel final
{
public:
	void reset();
	void beginComposition(std::uint64_t version);
	void replace(std::vector<CandidateStateItem> items, bool hasMore);
	void append(std::vector<CandidateStateItem> items, bool hasMore);

	void setPageSize(std::size_t pageSize);
	std::size_t pageSize() const { return m_pageSize; }
	std::size_t size() const { return m_items.size(); }
	bool empty() const { return m_items.empty(); }
	bool hasMore() const { return m_hasMore; }
	void setHasMore(bool hasMore) { m_hasMore = hasMore; }
	std::uint64_t version() const { return m_version; }
	const std::vector<CandidateStateItem>& items() const { return m_items; }
	const CandidateStateItem* item(std::size_t index) const;

	std::size_t selectedIndex() const { return m_selectedIndex; }
	std::size_t selectedRow() const;
	std::size_t selectedColumn() const;
	std::size_t currentRowSize() const;
	bool select(std::size_t index);
	bool selectById(std::uint64_t id);
	bool moveNext();
	bool movePrevious();
	bool moveNextRow();
	bool movePreviousRow();

	bool expanded() const { return m_expanded; }
	void expand() { m_expanded = !m_items.empty(); }
	void collapse() { m_expanded = false; }

private:
	void normalizeSelection();

	std::vector<CandidateStateItem> m_items;
	std::size_t m_selectedIndex = 0;
	std::size_t m_pageSize = 5;
	std::uint64_t m_version = 0;
	bool m_hasMore = false;
	bool m_expanded = false;
};

} // 命名空间 coral::tsf 结束

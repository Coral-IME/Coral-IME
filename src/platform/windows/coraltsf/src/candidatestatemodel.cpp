#include "candidatestatemodel.h"

#include <cwctype>

namespace coral::tsf
{

SegmentedComposition SegmentComposition(std::wstring_view raw, std::wstring_view segmentedPinyin)
{
	constexpr wchar_t kSeparator = L'\'';
	SegmentedComposition result;
	result.text.reserve(raw.size() * 2);
	result.rawToDisplay.reserve(raw.size() + 1);
	std::size_t seg = 0;
	bool aligned = !segmentedPinyin.empty();
	for (const wchar_t ch : raw)
	{
		result.rawToDisplay.push_back(static_cast<int>(result.text.size()));
		if (aligned && ch == kSeparator)
		{
			if (seg < segmentedPinyin.size() && segmentedPinyin[seg] == kSeparator)
				++seg;
		}
		else if (aligned)
		{
			bool boundary = seg == segmentedPinyin.size();
			if (!boundary && segmentedPinyin[seg] == kSeparator)
			{
				boundary = true;
				++seg;
			}
			if (boundary && !result.text.empty() && result.text.back() != kSeparator)
				result.text.push_back(kSeparator);
			if (seg < segmentedPinyin.size() &&
				std::towlower(segmentedPinyin[seg]) == std::towlower(ch))
				++seg;
			else
				aligned = false;
		}
		result.text.push_back(ch);
	}
	result.rawToDisplay.push_back(static_cast<int>(result.text.size()));
	return result;
}

void CandidateStateModel::reset()
{
	m_items.clear();
	m_selectedIndex = 0;
	m_hasMore = false;
	m_expanded = false;
}

void CandidateStateModel::beginComposition(std::uint64_t version)
{
	reset();
	m_version = version;
}

void CandidateStateModel::replace(std::vector<CandidateStateItem> items, bool hasMore)
{
	m_items = std::move(items);
	m_hasMore = hasMore;
	m_expanded = false;
	normalizeSelection();
}

void CandidateStateModel::append(std::vector<CandidateStateItem> items, bool hasMore)
{
	for (auto& item : items)
	{
		const auto duplicate = std::find_if(m_items.begin(), m_items.end(),
			[&item](const CandidateStateItem& current) {
				return current.id != 0 && current.id == item.id;
			});
		if (duplicate == m_items.end())
			m_items.push_back(std::move(item));
	}
	m_hasMore = hasMore;
	normalizeSelection();
}

void CandidateStateModel::setPageSize(std::size_t pageSize)
{
	m_pageSize = std::max<std::size_t>(1, pageSize);
	normalizeSelection();
}

const CandidateStateItem* CandidateStateModel::item(std::size_t index) const
{
	return index < m_items.size() ? &m_items[index] : nullptr;
}

std::size_t CandidateStateModel::selectedRow() const
{
	return m_selectedIndex / m_pageSize;
}

std::size_t CandidateStateModel::selectedColumn() const
{
	return m_selectedIndex % m_pageSize;
}

std::size_t CandidateStateModel::currentRowSize() const
{
	if (m_items.empty())
		return 0;
	const std::size_t first = selectedRow() * m_pageSize;
	return std::min(m_pageSize, m_items.size() - first);
}

bool CandidateStateModel::select(std::size_t index)
{
	if (index >= m_items.size())
		return false;
	m_selectedIndex = index;
	return true;
}

bool CandidateStateModel::selectById(std::uint64_t id)
{
	const auto found = std::find_if(m_items.begin(), m_items.end(),
		[id](const CandidateStateItem& item) { return id != 0 && item.id == id; });
	if (found == m_items.end())
		return false;
	m_selectedIndex = static_cast<std::size_t>(found - m_items.begin());
	return true;
}

bool CandidateStateModel::moveNext()
{
	return select(m_selectedIndex + 1);
}

bool CandidateStateModel::movePrevious()
{
	if (m_selectedIndex == 0)
		return false;
	--m_selectedIndex;
	return true;
}

bool CandidateStateModel::moveNextRow()
{
	if (m_items.empty())
		return false;
	const std::size_t next = m_selectedIndex + m_pageSize;
	if (next >= m_items.size())
		return false;
	m_selectedIndex = next;
	return true;
}

bool CandidateStateModel::movePreviousRow()
{
	if (m_selectedIndex < m_pageSize)
		return false;
	m_selectedIndex -= m_pageSize;
	return true;
}

void CandidateStateModel::normalizeSelection()
{
	if (m_items.empty())
		m_selectedIndex = 0;
	else if (m_selectedIndex >= m_items.size())
		m_selectedIndex = m_items.size() - 1;
}

} // 命名空间 coral::tsf 结束

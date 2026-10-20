#include "clipboard_ring.h"
#include <cwchar>
#include <locale>
#include <codecvt>
CircularList::CircularList(size_t capacity)
	: history_(), capacity_(capacity), mtx_() 
{
}
bool CircularList::push(const std::wstring& value)
{
	std::lock_guard<std::mutex> lock(mtx_);
	if (history_.size() == capacity_)
	{
		history_.pop_front();
	}
	history_.push_back(value);
	return true;
}

bool CircularList::empty() const
{
	std::lock_guard<std::mutex> lock(mtx_);
	return history_.empty();
}

size_t CircularList::size() const
{
	std::lock_guard<std::mutex> lock(mtx_);
	return history_.size();
}

std::vector<std::string> CircularList::get_recently_data() const
{
	std::wstring_convert<std::codecvt_utf8<wchar_t>, wchar_t> converter;
	std::lock_guard<std::mutex> lock(mtx_);
	std::vector<std::string> ret(capacity_);
	if (history_.empty())
	{
		return ret;
	}

	for (auto it = history_.rbegin(); it != history_.rend(); ++it)
	{
		ret.push_back(converter.to_bytes(*it));
	}
	return ret;
}

#pragma once
#ifndef CORAL_MODULES_ASSIST_VALUE_INPUT_CLIPBOARD_CLIPBOARD_RING_H_INCLUDED
#define CORAL_MODULES_ASSIST_VALUE_INPUT_CLIPBOARD_CLIPBOARD_RING_H_INCLUDED

// -------------------------------------------------------------------------- //
//	文件名		：	circularlist.h
//	创建者		：	
//	创建时间	：	2024-08-18
//	功能描述	：	vv模式获取剪切板历史记录-线程安全的循环数组
//
// -------------------------------------------------------------------------- //
#include <deque>
#include <mutex>
#include <vector>

class CircularList 
{
public:
	explicit CircularList(size_t capacity);
	bool push(const std::wstring& value);
	bool empty() const;
	size_t size() const;
	std::vector<std::string> get_recently_data() const;
private:
	std::deque<std::wstring> history_;
	size_t capacity_;
	mutable std::mutex mtx_;
};

#endif // KCIRCULARLIST_H

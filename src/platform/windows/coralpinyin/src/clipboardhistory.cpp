#include "pch.h"
#include "clipboardhistory.h"
#include "ui/settingswindow.h"

#include <ctime>

namespace
{
std::int64_t Now() { return static_cast<std::int64_t>(std::time(nullptr)); }

// 密码管理器等应用用这些格式声明内容不应进入剪贴板历史。
bool ExcludedByOwner()
{
	static const UINT exclude = ::RegisterClipboardFormatW(L"ExcludeClipboardContentFromMonitorProcessing");
	static const UINT viewerIgnore = ::RegisterClipboardFormatW(L"Clipboard Viewer Ignore");
	return (exclude && ::IsClipboardFormatAvailable(exclude)) ||
		(viewerIgnore && ::IsClipboardFormatAvailable(viewerIgnore));
}

bool HistoryDisallowed()
{
	static const UINT format = ::RegisterClipboardFormatW(L"CanIncludeInClipboardHistory");
	if (!format || !::IsClipboardFormatAvailable(format)) return false;
	HANDLE data = ::GetClipboardData(format);
	if (!data || ::GlobalSize(data) < sizeof(DWORD)) return false;
	const auto* value = static_cast<const DWORD*>(::GlobalLock(data));
	const bool disallowed = value && *value == 0;
	if (value) ::GlobalUnlock(data);
	return disallowed;
}
}

ClipboardHistory& ClipboardHistory::instance()
{
	static ClipboardHistory history;
	return history;
}

void ClipboardHistory::attach(HWND listener)
{
	if (m_listener || !listener) return;
	if (::AddClipboardFormatListener(listener))
		m_listener = listener;
	else
		LOG_ERROR("ClipboardHistory: AddClipboardFormatListener failed");
}

void ClipboardHistory::detach()
{
	if (m_listener) ::RemoveClipboardFormatListener(m_listener);
	m_listener = nullptr;
}

bool ClipboardHistory::readClipboardText(HWND owner, std::wstring& text)
{
	if (!::IsClipboardFormatAvailable(CF_UNICODETEXT) || ExcludedByOwner()) return false;
	// 复制方可能仍持有剪贴板，短暂重试。
	bool opened = false;
	for (int attempt = 0; attempt < 4 && !opened; ++attempt)
	{
		opened = ::OpenClipboard(owner) != FALSE;
		if (!opened) ::Sleep(15);
	}
	if (!opened) return false;
	bool ok = false;
	if (!HistoryDisallowed())
	{
		HANDLE data = ::GetClipboardData(CF_UNICODETEXT);
		const SIZE_T bytes = data ? ::GlobalSize(data) : 0;
		const auto* chars = data ? static_cast<const wchar_t*>(::GlobalLock(data)) : nullptr;
		if (chars)
		{
			const std::size_t capacity = bytes / sizeof(wchar_t);
			const std::size_t length = wcsnlen(chars, capacity);
			if (length <= coral::clipboard::kMaxTextLength)
			{
				text.assign(chars, length);
				ok = true;
			}
			::GlobalUnlock(data);
		}
	}
	::CloseClipboard();
	return ok;
}

void ClipboardHistory::onClipboardUpdate()
{
	if (!sConfig->getIsClipboardEnabled()) return;
	std::wstring text;
	if (!readClipboardText(m_listener, text)) return;
	ensureLoaded();
	if (!coral::clipboard::Remember(m_entries, std::move(text), Now())) return;
	persist();
	SettingsWindow::refreshClipboardList();
}

const std::vector<coral::clipboard::Entry>& ClipboardHistory::entries()
{
	ensureLoaded();
	pruneExpired();
	return m_entries;
}

bool ClipboardHistory::remove(std::size_t index)
{
	ensureLoaded();
	if (index >= m_entries.size()) return false;
	m_entries.erase(m_entries.begin() + static_cast<std::ptrdiff_t>(index));
	persist();
	return true;
}

void ClipboardHistory::clear()
{
	ensureLoaded();
	m_entries.clear();
	persist();
}

void ClipboardHistory::ensureLoaded()
{
	if (m_loaded) return;
	m_loaded = true;
	m_path = coral::clipboard::HistoryPath(hlp::getLocalLowCoralPinyinPath());
	m_entries = coral::clipboard::Load(m_path, Now());
}

void ClipboardHistory::pruneExpired()
{
	const std::int64_t now = Now();
	const auto expired = std::remove_if(m_entries.begin(), m_entries.end(),
		[now](const coral::clipboard::Entry& entry) {
			return now - entry.time > coral::clipboard::kRetentionSeconds;
		});
	if (expired == m_entries.end()) return;
	m_entries.erase(expired, m_entries.end());
	persist();
}

void ClipboardHistory::persist()
{
	if (!coral::clipboard::Save(m_path, m_entries))
		LOG_ERROR("ClipboardHistory: failed to save clipboard history");
}

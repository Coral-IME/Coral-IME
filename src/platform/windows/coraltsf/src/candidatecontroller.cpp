#include "pch.h"
#include "candidatecontroller.h"
#include "coreapi.h"
#include "input_timing.h"
#include "clipboard_history.h"

#include <algorithm>
#include <ctime>
#include <limits>

namespace
{
constexpr int kMaxPinyinLength = 40;
constexpr int kMaxSeparators = 20;
constexpr int kFetchSize = 60;
constexpr int kPrefetchThreshold = 15;
constexpr int kMaxCandidateCount = 600;
constexpr UINT kCandidateLoadComplete = WM_APP + 0x35B;
constexpr wchar_t kCandidateLoaderClass[] = L"CoralPinyin.CandidateLoader";
constexpr std::size_t kClipboardPageSize = 10;
// 与渲染器的列表行数保持一致，使 PageUp/PageDown 恰好移动一屏。
constexpr int kClipboardScreenRows = 6;

coral::tsf::CandidateStateItem CandidateItem(std::uint64_t id,
	const coral::text_service::CandidateSnapshot& snapshot, std::size_t index)
{
	coral::tsf::CandidateStateItem item{ id,
		std::wstring(CA2W(snapshot.words[index].c_str(), CP_UTF8)),
		std::wstring(CA2W(snapshot.pinyins[index].c_str(), CP_UTF8)) };
	if (index < snapshot.display_texts.size() && !snapshot.display_texts[index].empty())
	{
		item.commit_text = std::move(item.text);
		item.text = std::wstring(CA2W(snapshot.display_texts[index].c_str(), CP_UTF8));
	}
	return item;
}
}

CandidateController::CandidateController()
{
    translation_backend_ = [](const coral::translation::Request& request, const coral::translation::Cancellation& cancel) {
        auto stored = sConfig->get_translation_credentials();
        coral::translation::Credentials credentials{std::move(stored.first), std::move(stored.second)};
        return coral::translation::translate_general(request, credentials, cancel);
    };
	m_candidates.setPageSize(5);
	WNDCLASSW windowClass{};
	windowClass.lpfnWndProc = &CandidateController::LoaderWindowProc;
	windowClass.hInstance = g_hInst;
	windowClass.lpszClassName = kCandidateLoaderClass;
	if (::RegisterClassW(&windowClass) || ::GetLastError() == ERROR_CLASS_ALREADY_EXISTS)
		m_loaderWindow = ::CreateWindowExW(0, kCandidateLoaderClass, L"", 0,
			0, 0, 0, 0, HWND_MESSAGE, nullptr, g_hInst, this);
	m_loaderThread = std::thread([this]() { loaderMain(); });
}

CandidateController::~CandidateController()
{
    cancel_translation_request();
	{
		std::lock_guard<std::mutex> lock(m_loaderMutex);
		m_stopLoader = true;
		m_pendingLoad.reset();
	}
	m_loaderWake.notify_one();
	if (m_loaderThread.joinable()) m_loaderThread.join();
	if (m_loaderWindow) ::DestroyWindow(m_loaderWindow);
}

LRESULT CALLBACK CandidateController::LoaderWindowProc(
	HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
	auto* self = reinterpret_cast<CandidateController*>(::GetWindowLongPtrW(window, GWLP_USERDATA));
	if (message == WM_NCCREATE)
	{
		auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
		self = static_cast<CandidateController*>(create->lpCreateParams);
		::SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
	}
	if (message == kCandidateLoadComplete && self)
	{
		self->applyLoadedCandidates();
		return 0;
	}
	return ::DefWindowProcW(window, message, wParam, lParam);
}

void CandidateController::loaderMain()
{
	for (;;)
	{
        LoadRequest request;
        std::optional<TranslationJob> translation;
        {
            std::unique_lock<std::mutex> lock(m_loaderMutex);
            m_loaderWake.wait(lock, [this]() { return m_stopLoader || m_pendingLoad.has_value() || pending_translation_.has_value(); });
            if (m_stopLoader) return;
            if (pending_translation_) {
                translation = std::move(pending_translation_);
                pending_translation_.reset();
            } else {
                request = std::move(*m_pendingLoad);
                m_pendingLoad.reset();
            }
        }
        if (translation) {
            coral::translation::Result result{translation->request.id, coral::translation::Status::kFailed, {}, coral::translation::Failure::kInternal};
            try {
                if (!translation->cancelled->load() && translation->backend)
                    result = translation->backend(translation->request, translation->cancelled);
            } catch (...) { /*  返回通用失败；绝不记录提供方异常或凭据。 */ }
            {
                std::lock_guard<std::mutex> lock(m_loaderMutex);
                if (m_stopLoader) return;
                translation_results_.clear();
                translation_results_.push_back(std::move(result));
            }
            if (m_loaderWindow) ::PostMessageW(m_loaderWindow, kCandidateLoadComplete, 0, 0);
            continue;
        }
		LoadResult result;
		result.version = request.version;
		if (request.source)
			result.status = request.source->query(request.query, result.snapshot);
		{
			std::lock_guard<std::mutex> lock(m_loaderMutex);
			if (m_stopLoader) return;
			m_loadedResults.push_back(std::move(result));
		}
		if (m_loaderWindow) ::PostMessageW(m_loaderWindow, kCandidateLoadComplete, 0, 0);
	}
}

void CandidateController::applyLoadedCandidates()
{
    std::vector<coral::translation::Result> translated;
	std::vector<LoadResult> results;
	{
		std::lock_guard<std::mutex> lock(m_loaderMutex);
		results.swap(m_loadedResults);
        translated.swap(translation_results_);
	}
    for (auto& result : translated) apply_translation_result(std::move(result));
	for (auto& result : results)
	{
		if (result.version != m_version) continue;
		std::vector<coral::tsf::CandidateStateItem> items;
		const std::size_t count = std::min(
			result.snapshot.words.size(), result.snapshot.pinyins.size());
		items.reserve(count);
		for (std::size_t i = 0; i < count; ++i)
			items.push_back(CandidateItem((m_version << 32) | (result.snapshot.offset + i + 1),
				result.snapshot, i));
		m_candidates.append(std::move(items), SUCCEEDED(result.status) &&
			count != 0 && result.snapshot.has_more);
		m_visualRowStarts.clear();
		m_itemCenters.clear();
		publishCandidates();
	}
	m_isLoading = false;
}

void CandidateController::set_candidate_provider(
	std::shared_ptr<coral::text_service::CandidateProvider> source,
	coral::composition::SessionToken session_id, bool traditional)
{
	m_source = std::move(source);
	m_sessionId = session_id;
	m_traditional = traditional;
}

void CandidateController::setConfigSnapshot(
	std::shared_ptr<const coral::tsf::ImeConfigSnapshot> config)
{
	if (m_config && config && m_config->revision == config->revision)
		return;
    const bool key_changed = m_config && config &&
        (m_config->translation_key_configured != config->translation_key_configured ||
         m_config->translation_credential_revision != config->translation_credential_revision ||
         m_config->translation_target != config->translation_target);
    const bool prediction_changed = m_config && config && m_config->smart_prediction != config->smart_prediction;
    if (prediction_changed || !config || !config->smart_prediction) resetConditionWord();
	m_config = std::move(config);
    if (is_translation_mode()) {
        if (!m_config || !m_config->translation_enabled) exit_translation();
        else if (key_changed) {
            cancel_translation_request();
            translation_.begin(translation_.request().text, m_config->translation_key_configured, m_config->translation_target);
            m_candidates.replace({}, false);
            m_visualRowStarts.clear(); m_itemCenters.clear();
            start_translation_request();
            publishCandidates();
        }
    }
    updateInputMode();
    if (prediction_changed && !m_unfinished.IsEmpty() && !is_translation_mode()) queryCandidates();
	if (m_config)
	{
		m_candidates.setPageSize(static_cast<std::size_t>(
			std::max(1, m_config->candidateCount)));
		if (m_sink) m_sink->syncCandidateUI(m_candidates);
	}
}

void CandidateController::notifyPresentation()
{
	if (m_sink)
		m_sink->setCandidatePresentation(m_candidates.expanded(), m_candidates.hasMore());
}

void CandidateController::publishCandidates()
{
	if (!m_sink) return;
	m_sink->syncCandidateUI(m_candidates);
	notifyPresentation();
	if (m_uiLessMode)
		m_sink->updateCandidateUIElement();
}

std::size_t CandidateController::visualRowFor(std::size_t index) const
{
	if (m_visualRowStarts.empty()) return index / m_candidates.pageSize();
	auto upper = std::upper_bound(m_visualRowStarts.begin(), m_visualRowStarts.end(),
		static_cast<std::uint32_t>(index));
	return upper == m_visualRowStarts.begin() ? 0
		: static_cast<std::size_t>((upper - m_visualRowStarts.begin()) - 1);
}

std::size_t CandidateController::visualRowEnd(std::size_t row) const
{
	if (m_visualRowStarts.empty())
		return std::min(m_candidates.size(), (row + 1) * m_candidates.pageSize());
	return row + 1 < m_visualRowStarts.size() ? m_visualRowStarts[row + 1] : m_candidates.size();
}

void CandidateController::setVisualLayout(
	std::vector<std::uint32_t> rowStarts, std::vector<std::uint32_t> itemCenters)
{
	if (rowStarts.empty() || rowStarts.front() != 0 || itemCenters.size() != m_candidates.size())
		return;
	if (!std::is_sorted(rowStarts.begin(), rowStarts.end()) ||
		rowStarts.back() >= m_candidates.size())
		return;
	m_visualRowStarts = std::move(rowStarts);
	m_itemCenters = std::move(itemCenters);
}

void CandidateController::applyPinyinSettings()
{
	const bool enabled = m_config && m_config->doublepinyinMode;
	std::string schemeName = m_config ? m_config->doublepinyinScheme : std::string();
	if (schemeName.empty())
		schemeName = "xiaohe";
	if (!enabled)
	{
		if (m_doublepinyin)
			Reset();
		m_doublepinyin = false;
		m_doublepinyinScheme.reset();
		return;
	}
	if (m_doublepinyinScheme && m_doublepinyinScheme->get_name() == schemeName)
	{
		m_doublepinyin = true;
		return;
	}
	auto scheme = DoublepinyinSchemeFactory::instance().create(schemeName);
	if (!scheme)
		scheme = DoublepinyinSchemeFactory::instance().create("xiaohe");
	if (!scheme)
	{
		m_doublepinyin = false;
		return;
	}
	m_doublepinyinScheme = std::move(scheme);
	m_doublepinyin = true;
	Reset();
}

CString CandidateController::withoutSeparators(CString value)
{
	value.Replace(L"'", L"");
	return value;
}

int CandidateController::comparePinyinSeparator(
	const CString& subPinyin, const CString& basePinyin)
{
	int base = 0;
	int sub = 0;
	while (sub < subPinyin.GetLength() && base < basePinyin.GetLength())
	{
		if (basePinyin[base] == L'\'')
		{
			++base;
			continue;
		}
		if (towlower(subPinyin[sub]) != towlower(basePinyin[base]))
			break;
		++base;
		++sub;
	}
	return base;
}

int CandidateController::consumedRawLength() const
{
	int consumed = 0;
	for (const auto& segment : m_selected)
		consumed += segment.rawLength;
	return std::clamp(consumed, 0, m_rawInput.GetLength());
}

void CandidateController::rebuildDerivedState()
{
	m_selectedWord.Empty();
	m_selectedPinyin.Empty();
	for (std::size_t i = 0; i < m_selected.size(); ++i)
	{
		m_selectedWord += m_selected[i].word;
		if (i != 0)
			m_selectedPinyin += L"'";
		m_selectedPinyin += m_selected[i].pinyin;
	}
	const int consumed = consumedRawLength();
	m_unfinished = m_rawInput.Mid(consumed);
	if (m_unfinished == L"'")
		m_unfinished.Empty();
	m_cursor = std::clamp(m_cursor, 0, m_unfinished.GetLength());
}

void CandidateController::updateInputMode()
{
    const auto mode = coral::composition::specialMode(std::wstring_view(m_rawInput.GetString(), m_rawInput.GetLength()),
        m_doublepinyin, m_config && m_config->u_mode, m_config && m_config->v_mode);
    switch (mode) {
    case coral::composition::SpecialMode::Stroke: m_inputMode = InputMode::IM_UMode; break;
    case coral::composition::SpecialMode::Symbols: m_inputMode = InputMode::IM_UUMode; break;
    case coral::composition::SpecialMode::Convert: m_inputMode = InputMode::IM_VMode; break;
    default: m_inputMode = InputMode::IM_Normal; break;
    }
}

std::wstring_view CandidateController::modeHint() const {
    using coral::composition::SpecialMode;
    if (is_translation_mode()) return translation_.hint();
    if (m_clipboardMode)
        return m_candidates.empty() ? std::wstring_view(L"剪贴板暂无内容，复制的文字会出现在这里") : std::wstring_view();
    SpecialMode mode = SpecialMode::Normal;
    switch (m_inputMode) {
    case InputMode::IM_UMode: mode = SpecialMode::Stroke; break;
    case InputMode::IM_UUMode: mode = SpecialMode::Symbols; break;
    case InputMode::IM_VMode: mode = SpecialMode::Convert; break;
    default: break;
    }
    return coral::composition::specialModeHint(mode,
        std::wstring_view(m_rawInput.GetString(), m_rawInput.GetLength()), m_specialMatch, m_queryUnavailable);
}

CString CandidateController::convertAllDoublepinyin(const CString& input)
{
	if (!m_doublepinyinScheme)
		return input;
	m_doublepinyinScheme->resetState();
	CString converted;
	for (int i = 0; i < input.GetLength(); ++i)
	{
		const char letter = static_cast<char>(input[i]);
		if (m_doublepinyinScheme->zeroProcess(letter) && !converted.IsEmpty())
			converted.Truncate(converted.GetLength() - 1);
		converted += m_doublepinyinScheme->parseDoublepinyinInput(letter);
	}
	return converted;
}

bool CandidateController::useDoublepinyinParsing() const
{
	return m_doublepinyin && m_inputMode == InputMode::IM_Normal && m_doublepinyinScheme;
}

int CandidateController::consumedDoublepinyinLength(const CString& candidatePinyin)
{
	const CString target = withoutSeparators(candidatePinyin);
	for (int length = 1; length <= m_unfinished.GetLength(); ++length)
	{
		const CString converted = withoutSeparators(convertAllDoublepinyin(m_unfinished.Left(length)));
		if (converted.CompareNoCase(target) == 0)
			return length;
		if (converted.GetLength() > target.GetLength())
			break;
	}
	return 0;
}

void CandidateController::addLetter(WCHAR letter)
{
	if (m_rawInput.GetLength() >= kMaxPinyinLength)
		return;
	if (letter == L'\'' && std::count(m_rawInput.GetString(),
		m_rawInput.GetString() + m_rawInput.GetLength(), L'\'') >= kMaxSeparators)
		return;
	const int rawPosition = consumedRawLength() + m_cursor;
	m_rawInput.Insert(rawPosition, letter);
	++m_cursor;
	rebuildDerivedState();
	updateInputMode();
	queryCandidates();
}

bool CandidateController::back()
{
	if (m_clipboardMode)
	{
		exitClipboardMode();
		return true;
	}
	if (m_cursor > 0 && !m_unfinished.IsEmpty())
	{
		const int rawPosition = consumedRawLength() + m_cursor - 1;
		m_rawInput.Delete(rawPosition, 1);
		--m_cursor;
	}
	else if (!m_selected.empty())
	{
		m_selected.pop_back();
	}
	else
	{
		return false;
	}
	rebuildDerivedState();
	updateInputMode();
	if (withoutSeparators(m_unfinished).IsEmpty())
	{
		notifyComposition();
		return false;
	}
	queryCandidates();
	notifyComposition();
	return true;
}

bool CandidateController::removeNextLetter()
{
	if (m_cursor >= m_unfinished.GetLength())
		return !m_rawInput.IsEmpty();
	const int rawPosition = consumedRawLength() + m_cursor;
	m_rawInput.Delete(rawPosition, 1);
	rebuildDerivedState();
	updateInputMode();
	if (withoutSeparators(m_unfinished).IsEmpty())
		return false;
	queryCandidates();
	notifyComposition();
	return true;
}

void CandidateController::moveCursorHome()
{
	m_compositionEditing = true;
	m_cursor = 0;
	if (m_sink)
		m_sink->setCursorPos(compositionCursor());
}

void CandidateController::moveCursorEnd()
{
	m_compositionEditing = true;
	m_cursor = m_unfinished.GetLength();
	if (m_sink)
		m_sink->setCursorPos(compositionCursor());
}

void CandidateController::refreshSegmentation(const CString& before)
{
	if (getPinyinHolder() != before)
		notifyComposition();
}

void CandidateController::nextCol()
{
	const CString before = getPinyinHolder();
	moveNextCol();
	refreshSegmentation(before);
}

void CandidateController::prevCol()
{
	const CString before = getPinyinHolder();
	movePrevCol();
	refreshSegmentation(before);
}

bool CandidateController::nextRow()
{
	const CString before = getPinyinHolder();
	const bool moved = moveNextRow();
	refreshSegmentation(before);
	return moved;
}

bool CandidateController::prevRow(const bool& toFirst)
{
	const CString before = getPinyinHolder();
	const bool handled = movePrevRow(toFirst);
	refreshSegmentation(before);
	return handled;
}

void CandidateController::moveNextCol()
{
	if (m_compositionEditing)
	{
		if (m_cursor < m_unfinished.GetLength())
		{
			++m_cursor;
			if (m_sink) m_sink->setCursorPos(compositionCursor());
			return;
		}
		m_compositionEditing = false;
		m_candidates.select(0);
	}
	const std::size_t current = m_candidates.selectedIndex();
	if (!m_candidates.expanded() && current + 1 >= visualRowEnd(visualRowFor(current)))
	{
		m_candidates.expand();
		notifyPresentation();
		prefetchCandidates(true);
		return;
	}
	if (m_candidates.moveNext() && m_sink)
	{
		m_sink->syncCandidateUI(m_candidates);
		m_sink->updateCandidateUIElement();
		prefetchCandidates();
	}
	else
		prefetchCandidates(true);
}

void CandidateController::movePrevCol()
{
	if (m_candidates.expanded())
	{
		if (m_candidates.selectedIndex() == 0)
		{
			m_candidates.collapse();
			notifyPresentation();
		}
		else if (m_candidates.movePrevious() && m_sink)
		{
			m_sink->syncCandidateUI(m_candidates);
			m_sink->updateCandidateUIElement();
		}
		return;
	}
	if (m_compositionEditing)
	{
		if (m_cursor > 0) --m_cursor;
		if (m_sink) m_sink->setCursorPos(compositionCursor());
		return;
	}
	if (m_candidates.movePrevious() && m_sink)
	{
		m_sink->syncCandidateUI(m_candidates);
		m_sink->updateCandidateUIElement();
		return;
	}
	m_compositionEditing = true;
	m_cursor = std::max(0, m_unfinished.GetLength() - 1);
	if (m_sink) m_sink->setCursorPos(compositionCursor());
}

bool CandidateController::selectVisualNeighbor(int rowDelta)
{
	if (m_candidates.empty()) return false;
	const std::size_t current = m_candidates.selectedIndex();
	const std::size_t row = visualRowFor(current);
	if (rowDelta < 0 && row == 0) return false;
	const std::size_t targetRow = static_cast<std::size_t>(static_cast<int>(row) + rowDelta);
	const std::size_t rowCount = m_visualRowStarts.empty()
		? (m_candidates.size() + m_candidates.pageSize() - 1) / m_candidates.pageSize()
		: m_visualRowStarts.size();
	if (targetRow >= rowCount) return false;
	const std::size_t begin = m_visualRowStarts.empty()
		? targetRow * m_candidates.pageSize() : m_visualRowStarts[targetRow];
	const std::size_t end = visualRowEnd(targetRow);
	std::size_t target = begin;
	if (m_itemCenters.size() == m_candidates.size())
	{
		const auto center = m_itemCenters[current];
		for (std::size_t i = begin + 1; i < end; ++i)
			if (std::abs(static_cast<int>(m_itemCenters[i]) - static_cast<int>(center)) <
				std::abs(static_cast<int>(m_itemCenters[target]) - static_cast<int>(center)))
				target = i;
	}
	else
		target = std::min(begin + (current % m_candidates.pageSize()), end - 1);
	return m_candidates.select(target);
}

bool CandidateController::moveNextRow()
{
	if (!m_candidates.expanded())
	{
		m_candidates.expand();
		notifyPresentation();
		prefetchCandidates(true);
		return true;
	}
	const bool moved = selectVisualNeighbor(1);
	if (moved && m_sink)
	{
		m_sink->syncCandidateUI(m_candidates);
		m_sink->updateCandidateUIElement();
	}
	if (moved) prefetchCandidates();
	else prefetchCandidates(true);
	return moved;
}

bool CandidateController::movePrevRow(bool toFirst)
{
	if (!m_candidates.expanded())
		return false;
	if (toFirst)
		m_candidates.select(0);
	else if (!selectVisualNeighbor(-1))
		m_candidates.collapse();
	if (!m_candidates.expanded()) notifyPresentation();
	if (m_sink)
	{
		m_sink->syncCandidateUI(m_candidates);
		m_sink->updateCandidateUIElement();
	}
	return true;
}

void CandidateController::enterCandidateExpand()
{
	m_candidates.expand();
	notifyPresentation();
	prefetchCandidates(true);
}

void CandidateController::collapseCandidateExpand()
{
	m_candidates.collapse();
	notifyPresentation();
}

int CandidateController::selectCol(CString& text, int column)
{
	if (column < 0)
	{
		return selectCurrentCandidate(text);
	}
	const std::size_t row = visualRowFor(m_candidates.selectedIndex());
	const std::size_t begin = m_visualRowStarts.empty()
		? row * m_candidates.pageSize() : m_visualRowStarts[row];
	const std::size_t index = begin + static_cast<std::size_t>(column);
	if (index >= visualRowEnd(row)) return 0;
	return selectCandidateByIndex(text, static_cast<int>(index));
}

CString CandidateController::getCandidateText(int index) const
{
	const auto* item = index < 0 ? nullptr : m_candidates.item(static_cast<std::size_t>(index));
	return item ? CString(item->text.c_str()) : CString();
}

bool CandidateController::canDisableCandidate(int index) const
{
    if (is_translation_mode()) return false;
	const auto* item = index < 0 ? nullptr : m_candidates.item(static_cast<std::size_t>(index));
	return item && coral::CanDisableCandidate(std::string(CW2A(item->word().c_str(), CP_UTF8)));
}

int CandidateController::selectCurrentCandidate(CString& text)
{
	return selectCandidateByIndex(text, getSelectedCandidateIndex());
}

int CandidateController::selectCandidateByIndex(CString& text, int index)
{
	if (index < 0)
		return 0;
	const auto* candidate = m_candidates.item(static_cast<std::size_t>(index));
	if (!candidate)
		return 0;
    if (is_translation_mode()) {
        m_candidates.select(static_cast<std::size_t>(index));
        text = candidate->word().c_str();
        m_verbatimCommit = true;
        resetConditionWord();
        if (m_sink) {
            if (m_uiLessMode) m_sink->endCandidateUIElement();
            m_sink->setKeepPlaceholder(true);
        }
        return 1;
    }
	if (m_clipboardMode)
	{
		if (static_cast<std::size_t>(index) >= m_clipboardTexts.size())
			return 0;
		m_candidates.select(static_cast<std::size_t>(index));
		text = m_clipboardTexts[static_cast<std::size_t>(index)].c_str();
		m_verbatimCommit = true;
        resetConditionWord();
		if (m_sink)
		{
			if (m_uiLessMode)
				m_sink->endCandidateUIElement();
			m_sink->setKeepPlaceholder(true);
		}
		return 1;
	}
	if (index == m_clipboardEntryIndex)
	{
		enterClipboardMode();
		text.Empty();
		return 0;
	}
	const CString candidatePinyin(candidate->pinyin.c_str());
	int rawLength = useDoublepinyinParsing()
		? consumedDoublepinyinLength(candidatePinyin)
		: comparePinyinSeparator(withoutSeparators(candidatePinyin), m_unfinished);
	// 首个候选代表引擎的最佳完整转换。其元数据有时只描述
	// 可解析/规范的前缀；不能让这一实现细节把裸后缀
	// 滞留在组串中。
	if (index == 0)
		rawLength = m_unfinished.GetLength();
	// 绝不上屏没有输入进度的候选。缺少此保护时，
	// 同一候选可能被无限次提交而裸尾部一直保留。
	if (rawLength <= 0 && !m_unfinished.IsEmpty())
		return 0;
	m_candidates.select(static_cast<std::size_t>(index));
	m_selected.push_back({ candidate->word().c_str(), candidate->pinyin.c_str(), rawLength });
	rebuildDerivedState();
	if (!withoutSeparators(m_unfinished).IsEmpty())
	{
		m_cursor = m_unfinished.GetLength();
		queryCandidates();
		notifyComposition();
		text = m_selectedWord;
		return 0;
	}

	text = m_selectedWord;
    prediction_window_ = ::GetForegroundWindow();
    if (m_config && m_config->smart_prediction && m_inputMode == InputMode::IM_Normal)
        m_condition = coral::composition::prediction_context(m_condition.GetString(), m_selectedWord.GetString()).c_str();
    else resetConditionWord();
	std::uint32_t conditionId = (std::numeric_limits<std::uint32_t>::max)();
	if (m_source && m_sessionId)
		m_source->remember_candidate(m_sessionId,
			std::string(CT2A(m_selectedPinyin, CP_UTF8)),
			std::string(CT2A(text, CP_UTF8)), conditionId);
	if (m_sink)
	{
		auto& history = m_sink->getCandInput();
		history.push_back(text);
		if (history.size() > 6)
			history.erase(history.begin());
		if (m_uiLessMode)
			m_sink->endCandidateUIElement();
		m_sink->setKeepPlaceholder(true);
	}
	m_selectedWordCount += text.GetLength();
	return 1;
}

coral::tsf::SegmentedComposition CandidateController::segmentedUnfinished() const
{
	const std::wstring_view raw(m_unfinished.GetString(), m_unfinished.GetLength());
	// 跟随高亮候选；新查询高亮第一个候选，
	// 即引擎对整个输入的最佳转换。
	// 双拼键与特殊模式并不拼出候选拼音。
	const auto* reference = m_candidates.item(m_candidates.selectedIndex());
	if (!reference || m_clipboardMode || m_doublepinyin || m_inputMode != InputMode::IM_Normal)
		return coral::tsf::SegmentComposition(raw, {});
	return coral::tsf::SegmentComposition(raw, reference->pinyin);
}

int CandidateController::compositionCursor() const
{
    if (is_translation_mode()) return static_cast<int>(translation_.request().text.size());
	const auto segmented = segmentedUnfinished();
	const int cursor = std::clamp(m_cursor, 0, static_cast<int>(segmented.rawToDisplay.size()) - 1);
	return m_selectedWord.GetLength() + segmented.rawToDisplay[static_cast<std::size_t>(cursor)];
}

CString CandidateController::getPinyinHolder() const
{
    if (is_translation_mode()) return translation_.request().text.c_str();
	return m_selectedWord + segmentedUnfinished().text.c_str();
}

int CandidateController::get_pinyin_length() const
{
	return withoutSeparators(m_rawInput).GetLength();
}

int CandidateController::getSelCol() const
{
	const std::size_t row = visualRowFor(m_candidates.selectedIndex());
	const std::size_t begin = m_visualRowStarts.empty()
		? row * m_candidates.pageSize() : m_visualRowStarts[row];
	return static_cast<int>(m_candidates.selectedIndex() - begin);
}

int CandidateController::getCurrColMaxSize() const
{
	if (m_candidates.empty()) return 0;
	const std::size_t row = visualRowFor(m_candidates.selectedIndex());
	const std::size_t begin = m_visualRowStarts.empty()
		? row * m_candidates.pageSize() : m_visualRowStarts[row];
	return static_cast<int>(visualRowEnd(row) - begin);
}

void CandidateController::notifyComposition()
{
	if (!m_sink)
		return;
	m_sink->updateEditSessionAccumulatedPinyin(getPinyinHolder());
	if (m_uiLessMode)
		m_sink->updateCompartmentString(getPinyinHolder());
}

void CandidateController::queryCandidates()
{
    if (prediction_window_ != ::GetForegroundWindow()) resetConditionWord();
	coral::input_timing::Scope timing(L"tsf.query_candidates", m_version + 1);
	++m_version;
	m_candidates.beginComposition(m_version);
	m_visualRowStarts.clear();
	m_itemCenters.clear();
	m_compositionEditing = false;
	m_status = STATUS::INPUT_STATUS;
	std::vector<coral::tsf::CandidateStateItem> items;
	bool hasMore = false;
    m_specialMatch = false;
    m_queryUnavailable = false;
	if (m_source && m_sessionId && !withoutSeparators(m_unfinished).IsEmpty() &&
        !(m_inputMode != InputMode::IM_Normal && m_unfinished.GetLength() <=
            (m_inputMode == InputMode::IM_UUMode ? 2 : 1)))
	{
		coral::text_service::CandidateLookup query;
		query.session_id = m_sessionId;
		query.request_id = m_version;
		query.condition = prediction_condition();
		const bool doublepinyin = useDoublepinyinParsing();
		query.pinyin = std::string(CT2A(
			doublepinyin ? convertAllDoublepinyin(m_unfinished) : m_unfinished, CP_UTF8));
		query.offset = 0;
		query.page_size = kFetchSize;
		query.mode = m_doublepinyin ? coral::composition::kModeDoublepinyin : coral::composition::kModeQuanpin;
		coral::text_service::CandidateSnapshot snapshot;
		const auto queryStart = coral::input_timing::nowUs();
		const HRESULT queryHr = m_source->query(query, snapshot);
		coral::input_timing::mark(L"tsf.query_engine", queryStart, m_version, static_cast<unsigned long>(queryHr));
		m_queryUnavailable = FAILED(queryHr);
		if (SUCCEEDED(queryHr))
		{
			hasMore = snapshot.has_more;
			const std::size_t count = std::min(snapshot.words.size(), snapshot.pinyins.size());
			items.reserve(count);
			for (std::size_t i = 0; i < count; ++i)
				items.push_back(CandidateItem((m_version << 32) | (i + 1), snapshot, i));
		}
	}
    m_specialMatch = !items.empty() && !(items.size() == 1 && items[0].text == m_unfinished.GetString());
	if (items.empty() && !m_unfinished.IsEmpty())
		items.push_back({ (m_version << 32) | 1,
			std::wstring(m_unfinished.GetString()),
			std::wstring(m_unfinished.GetString()) });
	m_clipboardEntryIndex = -1;
	if (clipboardEntryAvailable())
	{
		m_clipboardEntryIndex = static_cast<int>(items.size());
		items.push_back({ (m_version << 32) | (items.size() + 1), L"剪贴板", std::wstring() });
	}
	m_candidates.replace(std::move(items), hasMore);
	const auto publishStart = coral::input_timing::nowUs();
	publishCandidates();
	coral::input_timing::mark(L"tsf.publish_candidates", publishStart, m_version);
}

void CandidateController::prefetchCandidates(bool force)
{
    if (is_translation_mode()) return;
	if (m_clipboardMode)
	{
		const std::size_t remaining = m_candidates.size() > m_candidates.selectedIndex()
			? m_candidates.size() - m_candidates.selectedIndex() - 1 : 0;
		if ((force || remaining <= 2) && appendClipboardPage())
			publishCandidates();
		return;
	}
	if (m_isLoading || !m_candidates.hasMore() || !m_source || !m_sessionId ||
		m_candidates.size() >= kMaxCandidateCount)
		return;
	const std::size_t remaining = m_candidates.size() > m_candidates.selectedIndex()
		? m_candidates.size() - m_candidates.selectedIndex() - 1 : 0;
	if (!force && remaining > kPrefetchThreshold) return;
	m_isLoading = true;
	LoadRequest request;
	request.version = m_version;
	request.source = m_source;
	request.query.session_id = m_sessionId;
	request.query.request_id = m_version;
	request.query.condition = prediction_condition();
	const bool doublepinyin = useDoublepinyinParsing();
	request.query.pinyin = std::string(CT2A(
		doublepinyin ? convertAllDoublepinyin(m_unfinished) : m_unfinished, CP_UTF8));
	request.query.offset = static_cast<int>(m_candidates.size());
	request.query.page_size = std::min(kFetchSize,
		kMaxCandidateCount - static_cast<int>(m_candidates.size()));
	request.query.mode = m_doublepinyin ? coral::composition::kModeDoublepinyin : coral::composition::kModeQuanpin;
	{
		std::lock_guard<std::mutex> lock(m_loaderMutex);
		m_pendingLoad = std::move(request);
	}
	m_loaderWake.notify_one();
}

void CandidateController::Reset()
{
    cancel_translation_request();
    translation_.reset();
    original_candidates_.reset();
	++m_version;
	m_candidates.beginComposition(m_version);
	m_selected.clear();
	m_rawInput.Empty();
	m_unfinished.Empty();
	m_selectedWord.Empty();
	m_selectedPinyin.Empty();
	m_cursor = 0;
	m_status = STATUS::NONE_STATUS;
	m_inputMode = InputMode::IM_Normal;
	m_visible = false;
	m_mouseSelected = false;
	m_compositionEditing = false;
	m_clipboardEntryIndex = -1;
	m_clipboardMode = false;
	m_verbatimCommit = false;
	m_clipboardTexts.clear();
	// 进行中的请求由 m_version 判定失效，返回时将被丢弃。
	m_visualRowStarts.clear();
	m_itemCenters.clear();
	if (m_doublepinyinScheme)
		m_doublepinyinScheme->resetState();
}

std::string CandidateController::prediction_condition() const
{
    if (!m_config || !m_config->smart_prediction || m_inputMode != InputMode::IM_Normal) return {};
    const auto text = coral::composition::prediction_context(m_condition.GetString(), m_selectedWord.GetString());
    return std::string(CW2A(text.c_str(), CP_UTF8));
}

void CandidateController::resetConditionWord()
{
	m_condition.Empty();
}

void CandidateController::refreshCandidateContent()
{
    if (is_translation_mode()) return;
	if (m_clipboardMode)
		return;
	if (m_source && m_sessionId)
		m_source->clear_cache(m_sessionId);
	if (!m_unfinished.IsEmpty())
		queryCandidates();
}

void CandidateController::adjustTodayWord(int offset)
{
	m_selectedWordCount = std::max(0, m_selectedWordCount + offset);
}

bool CandidateController::applyCandidateActionByIndex(
	int index, coral::CandidateAction action)
{
	if (index < 0 || !m_source || !m_sessionId || m_clipboardMode || is_translation_mode() || index == m_clipboardEntryIndex)
		return false;
	const auto* candidate = m_candidates.item(static_cast<std::size_t>(index));
	if (!candidate)
		return false;
	if (action == coral::CandidateAction::Disable && !canDisableCandidate(index))
		return false;
	const bool changed = m_source->apply_candidate_rule(
		m_sessionId,
		std::string(CT2A(candidate->pinyin.c_str(), CP_UTF8)),
		std::string(CT2A(candidate->word().c_str(), CP_UTF8)),
		action, m_traditional ? coral::LanguageVariant::Traditional : coral::LanguageVariant::Simplified);
	if (changed)
	{
		m_source->clear_cache(m_sessionId);
		queryCandidates();
	}
	return changed;
}

void CandidateController::showAddPhraseByIndex(int index)
{
    if (is_translation_mode()) return;
	if (m_inputMode != InputMode::IM_Normal)
		return;
	const auto* candidate = index < 0 ? nullptr
		: m_candidates.item(static_cast<std::size_t>(index));
	const CString word = candidate ? CString(candidate->word().c_str()) : CString();
	Coreapi::showAddPhrasePage(m_rawInput, word);
	if (m_sink)
		m_sink->hideCandidateDlg();
}

bool CandidateController::clipboardEntryAvailable() const
{
	return m_inputMode == InputMode::IM_VMode && m_config && m_config->clipboardEnabled &&
		m_selected.empty() && withoutSeparators(m_unfinished).GetLength() == 1;
}

// 历史文件由常驻进程 coralpinyin.exe 维护，这里每次进入时读取最新内容。
void CandidateController::enterClipboardMode()
{
	const auto now = static_cast<std::int64_t>(std::time(nullptr));
	auto entries = coral::clipboard::Load(
		coral::clipboard::HistoryPath(hlp::getLocalLowCoralPinyinPath()), now);
	m_clipboardTexts.clear();
	m_clipboardTexts.reserve(entries.size());
	for (auto& entry : entries)
		m_clipboardTexts.push_back(std::move(entry.text));
	m_clipboardMode = true;
	m_clipboardEntryIndex = -1;
	++m_version;
	m_candidates.beginComposition(m_version);
	m_visualRowStarts.clear();
	m_itemCenters.clear();
	m_compositionEditing = false;
	appendClipboardPage();
	m_candidates.expand();
	publishCandidates();
}

bool CandidateController::appendClipboardPage()
{
	const std::size_t begin = m_candidates.size();
	if (begin >= m_clipboardTexts.size())
		return false;
	const std::size_t end = std::min(m_clipboardTexts.size(), begin + kClipboardPageSize);
	std::vector<coral::tsf::CandidateStateItem> items;
	items.reserve(end - begin);
	for (std::size_t i = begin; i < end; ++i)
		items.push_back({ (m_version << 32) | (i + 1),
			coral::clipboard::DisplayLine(m_clipboardTexts[i]), std::wstring() });
	m_candidates.append(std::move(items), end < m_clipboardTexts.size());
	// 列表每行一项；先按此布局导航，渲染端回报的布局与之相同。
	m_visualRowStarts.resize(m_candidates.size());
	for (std::size_t i = 0; i < m_visualRowStarts.size(); ++i)
		m_visualRowStarts[i] = static_cast<std::uint32_t>(i);
	m_itemCenters.assign(m_candidates.size(), 0);
	return true;
}

void CandidateController::exitClipboardMode()
{
	if (!m_clipboardMode)
		return;
	m_clipboardMode = false;
	m_clipboardTexts.clear();
	queryCandidates();
	if (m_clipboardEntryIndex >= 0 && m_candidates.select(static_cast<std::size_t>(m_clipboardEntryIndex)))
		publishCandidates();
}

void CandidateController::clipboardMove(int delta)
{
	if (!m_clipboardMode || m_candidates.empty())
		return;
	const int current = static_cast<int>(m_candidates.selectedIndex());
	while (delta > 0 && current + delta >= static_cast<int>(m_candidates.size()) && appendClipboardPage()) {}
	const int last = static_cast<int>(m_candidates.size()) - 1;
	m_candidates.select(static_cast<std::size_t>(std::clamp(current + delta, 0, last)));
	publishClipboardSelection();
}

void CandidateController::clipboardMoveTo(bool last)
{
	if (!m_clipboardMode || m_candidates.empty())
		return;
	while (last && appendClipboardPage()) {}
	m_candidates.select(last ? m_candidates.size() - 1 : 0);
	publishClipboardSelection();
}

void CandidateController::publishClipboardSelection()
{
	publishCandidates();
	prefetchCandidates();
}

int CandidateController::clipboardScreenRows()
{
	return kClipboardScreenRows;
}


bool CandidateController::begin_translation()
{
    if (!m_config || !m_config->translation_enabled || is_translation_mode() ||
        m_clipboardMode || m_inputMode != InputMode::IM_Normal || m_rawInput.IsEmpty()) return false;
    const auto* candidate = m_candidates.item(m_candidates.selectedIndex());
    if (!candidate) return false;
    const std::wstring text = std::wstring(m_selectedWord.GetString()) + candidate->word();
    if (!translation_.begin(text, m_config->translation_key_configured, m_config->translation_target)) return false;
    original_candidates_ = m_candidates;
    ++m_version; // 丢弃所有未决的普通候选结果。
    m_isLoading = false;
    m_candidates.beginComposition(m_version);
    m_visualRowStarts.clear();
    m_itemCenters.clear();
    m_compositionEditing = false;
    start_translation_request();
    notifyComposition();
    publishCandidates();
    return true;
}

void CandidateController::exit_translation()
{
    if (!is_translation_mode()) return;
    cancel_translation_request();
    translation_.reset();
    ++m_version;
    if (original_candidates_) m_candidates = std::move(*original_candidates_);
    original_candidates_.reset();
    m_visualRowStarts.clear();
    m_itemCenters.clear();
    notifyComposition();
    publishCandidates();
}

void CandidateController::apply_translation_result(coral::translation::Result result)
{
    if (!translation_.accept(std::move(result))) return;
    std::vector<coral::tsf::CandidateStateItem> items;
    for (const auto& text : translation_.translations())
        items.push_back({ (m_version << 32) | (items.size() + 1), text, {} });
    m_candidates.replace(std::move(items), false);
    m_visualRowStarts.resize(m_candidates.size());
    for (std::size_t i = 0; i < m_visualRowStarts.size(); ++i)
        m_visualRowStarts[i] = static_cast<std::uint32_t>(i);
    m_itemCenters.assign(m_candidates.size(), 0);
    publishCandidates();
}

void CandidateController::move_translation_selection(int delta)
{
    if (!is_translation_mode() || m_candidates.empty()) return;
    const int last = static_cast<int>(m_candidates.size()) - 1;
    const int current = static_cast<int>(m_candidates.selectedIndex());
    m_candidates.select(static_cast<std::size_t>(std::clamp(current + delta, 0, last)));
    publishCandidates();
}

void CandidateController::cancel_translation_request()
{
    if (translation_cancel_) translation_cancel_->store(true);
    translation_cancel_.reset();
    std::lock_guard<std::mutex> lock(m_loaderMutex);
    pending_translation_.reset();
    translation_results_.clear();
}

void CandidateController::start_translation_request()
{
    if (!m_config || !m_config->translation_key_configured) return;
    if (!m_loaderWindow) {
        apply_translation_result({translation_.request().id, coral::translation::Status::kFailed, {}, coral::translation::Failure::kInternal});
        return;
    }
    translation_cancel_ = std::make_shared<std::atomic<bool>>(false);
    {
        std::lock_guard<std::mutex> lock(m_loaderMutex);
        pending_translation_ = TranslationJob{translation_.request(), translation_cancel_, translation_backend_};
    }
    m_loaderWake.notify_one();
}

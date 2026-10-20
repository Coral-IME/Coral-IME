#include "pch.h"
#include "configauthority.h"

#include "preference_registry.h"

namespace coral::tsf
{

namespace
{
bool SameConfigContent(const ImeConfigSnapshot& a, const ImeConfigSnapshot& b)
{
	return a.candidateCount == b.candidateCount &&
		a.candidateFontSize == b.candidateFontSize &&
		a.candidateFontName == b.candidateFontName && a.theme == b.theme &&
		a.candidateSkin == b.candidateSkin &&
		a.floatingToolbarOpen == b.floatingToolbarOpen &&
		a.smart_prediction == b.smart_prediction && a.u_mode == b.u_mode && a.v_mode == b.v_mode &&
		a.doublepinyinMode == b.doublepinyinMode && a.doublepinyinScheme == b.doublepinyinScheme &&
		a.defaultEnglish == b.defaultEnglish &&
		a.useEnglishPunctuation == b.useEnglishPunctuation &&
		a.autoSymbolCompletion == b.autoSymbolCompletion &&
		a.autoConvertDigitSymbol == b.autoConvertDigitSymbol &&
		a.auto_convert_letter_symbol == b.auto_convert_letter_symbol &&
		a.clipboardEnabled == b.clipboardEnabled &&
		a.ctrlSpaceShortcut == b.ctrlSpaceShortcut && a.shiftShortcut == b.shiftShortcut &&
		a.ctrlShortcut == b.ctrlShortcut && a.shiftSpaceShortcut == b.shiftSpaceShortcut &&
		a.simplifiedTraditionalShortcut == b.simplifiedTraditionalShortcut &&
		a.simplifiedTraditionalKey == b.simplifiedTraditionalKey &&
		a.ctrlPeriodShortcut == b.ctrlPeriodShortcut &&
		a.page_up_down == b.page_up_down && a.bracketPage == b.bracketPage &&
        a.translation_enabled == b.translation_enabled &&
        a.minus_equals_page == b.minus_equals_page &&
        a.translation_shortcut_key == b.translation_shortcut_key &&
        a.translation_key_configured == b.translation_key_configured &&
        a.translation_credential_revision == b.translation_credential_revision &&
        a.translation_target == b.translation_target &&
		a.commaPeriodPage == b.commaPeriodPage &&
		a.secondThirdCandidateKey == b.secondThirdCandidateKey &&
		a.wordToCharacterKey == b.wordToCharacterKey &&
		a.pinyin_correction == b.pinyin_correction && a.fuzzy_choices == b.fuzzy_choices &&
		a.limit_count == b.limit_count && a.show_infos == b.show_infos &&
		a.combined_lexicon_version == b.combined_lexicon_version &&
		a.defaultFullWidth == b.defaultFullWidth &&
		a.defaultTraditional == b.defaultTraditional &&
		a.debugEnabled == b.debugEnabled;
}
}

composition::SessionSettings ImeConfigSnapshot::engineConfig(
	bool full_width, bool traditional) const
{
	composition::SessionSettings result;
	result.revision = revision;
	result.smart_prediction = smart_prediction;
	result.u_mode = u_mode;
	result.v_mode = v_mode;
	result.pinyin_correction = pinyin_correction;
	result.full_width = full_width;
	result.traditional_chinese = traditional;
	result.show_infos = show_infos;
	result.limit_count = limit_count;
	result.combined_lexicon_version = combined_lexicon_version;
	result.fuzzy_choices = fuzzy_choices;
	return result;
}

ConfigAuthority::ConfigAuthority()
{
	auto initial = std::make_shared<ImeConfigSnapshot>();
	initial->revision = m_nextRevision++;
	m_snapshot = std::move(initial);
}

std::shared_ptr<const ImeConfigSnapshot> ConfigAuthority::refresh(bool force)
{
	std::lock_guard<std::mutex> lock(m_mutex);
	if (!force && !sConfig->pollForUpdates())
		return m_snapshot;
	auto next = buildSnapshot();
	if (m_snapshot && SameConfigContent(*m_snapshot, *next))
		return m_snapshot;
	next->revision = m_nextRevision++;
	m_snapshot = std::move(next);
	return m_snapshot;
}

std::shared_ptr<const ImeConfigSnapshot> ConfigAuthority::current() const
{
	std::lock_guard<std::mutex> lock(m_mutex);
	return m_snapshot;
}

std::shared_ptr<ImeConfigSnapshot> ConfigAuthority::buildSnapshot() const
{
	auto config = std::make_shared<ImeConfigSnapshot>();
	config->candidateCount = sConfig->getCandidateCount();
	config->candidateFontSize = sConfig->getCandidateFontSize();
	config->candidateFontName = sConfig->getCandidateFontName();
	config->theme = sConfig->getTheme();
	config->candidateSkin = sConfig->getCandidateSkin();
	config->floatingToolbarOpen = sConfig->getIsFloatingToolbarOpen();
	config->smart_prediction = sConfig->get_smart_prediction();
	config->u_mode = sConfig->getIsUMode();
	config->v_mode = sConfig->getIsVMode();
	config->doublepinyinMode = sConfig->getIsDoublepinyinMode();
	config->doublepinyinScheme = sConfig->getDoublepinyinSchemeName();
	config->defaultEnglish = sConfig->getIsDefaultEnglishMode();
	config->useEnglishPunctuation = sConfig->getIsUseEnglishPunctuation();
	config->autoSymbolCompletion = sConfig->getIsAutoSymbolCompletion();
	config->autoConvertDigitSymbol = sConfig->getIsAutoConvertDigitSymbol();
	config->auto_convert_letter_symbol = sConfig->get_auto_convert_letter_symbol();
	config->clipboardEnabled = sConfig->getIsClipboardEnabled();
	config->ctrlSpaceShortcut = sConfig->getIsCtrlAndSpaceShortCutOpen();
	config->shiftShortcut = sConfig->getIsShiftShortCutOpen();
	config->ctrlShortcut = sConfig->getIsCtrlShortCutOpen();
	config->shiftSpaceShortcut = sConfig->getIsShiftAndSpaceShortCutOpen();
	config->simplifiedTraditionalShortcut = sConfig->getIsShortCutChangeCharacterSimOrTraOpen();
	config->simplifiedTraditionalKey = sConfig->getCharacterChangeSimOrTra();
	config->ctrlPeriodShortcut = sConfig->getIsCtrlAndPeriodShortCutOpen();
	config->page_up_down = sConfig->get_page_up_down_enabled();
    config->translation_enabled = sConfig->get_translation_enabled();
    config->minus_equals_page = sConfig->get_minus_equals_page_enabled();
    config->translation_shortcut_key = translation::shortcut_option(sConfig->get_translation_shortcut()).virtual_key;
    config->translation_key_configured = sConfig->get_translation_key_configured();
    config->translation_credential_revision = sConfig->get_translation_credential_revision();
    config->translation_target = sConfig->get_translation_target();
	config->bracketPage = sConfig->getIsBraceFlipPageOpen();
	config->commaPeriodPage = sConfig->getIsCommaAndPeriodFlipPageOpen();
	config->secondThirdCandidateKey = static_cast<int>(sConfig->getSecondThirdCandidateKey());
	config->wordToCharacterKey = static_cast<int>(sConfig->getWordToCharacterKey());
	config->pinyin_correction = sConfig->getPinyinCorrection();
	config->fuzzy_choices = sConfig->getFuzzyChoice();
	config->limit_count = sConfig->getLimitCount();
	config->show_infos = sConfig->getIsShowInfos();
	config->combined_lexicon_version = sConfig->getCombinedLexiconVersion();
	config->defaultFullWidth = sConfig->runtime_full_width();
	config->defaultTraditional = sConfig->runtime_traditional();
	config->debugEnabled = sConfig->getIsDebugEnabled();
	return config;
}

void RuntimeInputMode::reset(const ImeConfigSnapshot& config, bool english)
{
	m_english = english;
	m_fullWidth = config.defaultFullWidth;
	m_traditional = config.defaultTraditional;
	m_englishPunctuation = config.useEnglishPunctuation;
	++m_revision;
}

} // 命名空间 coral::tsf 结束

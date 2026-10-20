#pragma once

#include "coral/composition/session_port.h"
#include "coral/translation/shortcut.h"

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>

namespace coral::tsf
{

struct ImeConfigSnapshot
{
	std::uint64_t revision = 0;
	int candidateCount = 5;
	int candidateFontSize = 16;
	std::string candidateFontName;
	std::string theme;
	std::string candidateSkin;
	bool floatingToolbarOpen = true;
	bool smart_prediction = true;
    bool u_mode = true;
	bool v_mode = true;
	bool doublepinyinMode = false;
	std::string doublepinyinScheme;
	bool defaultEnglish = false;
	bool useEnglishPunctuation = false;
	bool autoSymbolCompletion = false;
	bool autoConvertDigitSymbol = false;
	bool auto_convert_letter_symbol = false;
	bool clipboardEnabled = true;
	bool ctrlSpaceShortcut = true;
	bool shiftShortcut = true;
	bool ctrlShortcut = false;
	bool shiftSpaceShortcut = true;
	bool simplifiedTraditionalShortcut = false;
	std::string simplifiedTraditionalKey;
	bool ctrlPeriodShortcut = true;
	bool page_up_down = true;
	bool translation_enabled = false;
    bool minus_equals_page = false;
    std::uint32_t translation_shortcut_key = 0xBB;
	bool translation_key_configured = false;
    std::string translation_credential_revision;
    std::string translation_target = "en";
	bool bracketPage = true;
	bool commaPeriodPage = true;
	int secondThirdCandidateKey = 0;
	int wordToCharacterKey = 0;
	bool pinyin_correction = true;
	std::vector<int> fuzzy_choices;
	int limit_count = 10;
	bool show_infos = false;
	int combined_lexicon_version = -1;
	bool defaultFullWidth = false;
	bool defaultTraditional = false;
	bool debugEnabled = false;

	bool translation_shortcut_available() const {
        return translation_enabled && !translation::shortcut_conflicts(translation_shortcut_key, minus_equals_page);
    }

	composition::SessionSettings engineConfig(bool full_width, bool traditional) const;
};

class ConfigAuthority final
{
public:
	ConfigAuthority();
	std::shared_ptr<const ImeConfigSnapshot> refresh(bool force = false);
	std::shared_ptr<const ImeConfigSnapshot> current() const;

private:
	std::shared_ptr<ImeConfigSnapshot> buildSnapshot() const;

	mutable std::mutex m_mutex;
	std::shared_ptr<const ImeConfigSnapshot> m_snapshot;
	std::uint64_t m_nextRevision = 1;
};

class RuntimeInputMode final
{
public:
	void reset(const ImeConfigSnapshot& config, bool english);
	void setEnglish(bool value) { m_english = value; ++m_revision; }
	void setFullWidth(bool value) { m_fullWidth = value; ++m_revision; }
	void setTraditional(bool value) { m_traditional = value; ++m_revision; }
	void setEnglishPunctuation(bool value) { m_englishPunctuation = value; ++m_revision; }
	bool english() const { return m_english; }
	bool full_width() const { return m_fullWidth; }
	bool traditional() const { return m_traditional; }
	bool englishPunctuation() const { return m_englishPunctuation; }
	std::uint64_t revision() const { return m_revision; }

private:
	bool m_english = false;
	bool m_fullWidth = false;
	bool m_traditional = false;
	bool m_englishPunctuation = false;
	std::uint64_t m_revision = 1;
};

} // 命名空间 coral::tsf 结束

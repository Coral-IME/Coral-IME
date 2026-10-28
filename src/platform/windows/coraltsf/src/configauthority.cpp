#include "pch.h"
#include "configauthority.h"

#include "preference_registry.h"
#include "input_timing.h"
#include <atomic>
#include <filesystem>
#include <limits>

namespace coral::tsf
{

namespace
{
struct RefreshTask {
    HMODULE module = nullptr;
    std::function<void()> run;
};

void CALLBACK RunConfigRefresh(PTP_CALLBACK_INSTANCE instance, void* context)
{
    std::unique_ptr<RefreshTask> task(static_cast<RefreshTask*>(context));
    // 回调只拥有读取状态；最后一条指令返回前由系统释放 DLL 引用。
    FreeLibraryWhenCallbackReturns(instance, task->module);
    task->run();
}

bool ScheduleConfigRefresh(std::function<void()> run)
{
    auto task = std::make_unique<RefreshTask>();
    task->run = std::move(run);
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
        reinterpret_cast<LPCWSTR>(&RunConfigRefresh), &task->module)) return false;
    if (!TrySubmitThreadpoolCallback(RunConfigRefresh, task.get(), nullptr)) {
        FreeLibrary(task->module);
        return false;
    }
    task.release();
    return true;
}

struct ConfigReader {
    explicit ConfigReader(std::string file) : path(std::move(file)) {}
    std::string path;
    SharedMemoryConfig shared;
    std::filesystem::file_time_type last_write{};
    std::uint64_t last_generation = 0;
    bool last_shared = false;
    bool loaded = false;

    std::unique_ptr<SettingsConfig> read()
    {
        coral::input_timing::Scope timing(L"tsf.config_background");
        std::error_code error;
        const auto file_start = coral::input_timing::nowUs();
        const auto stamp = std::filesystem::last_write_time(path, error);
        coral::input_timing::mark(L"tsf.config_file", file_start, 0, error.value());
        if (error) return {};
        const auto connect_start = coral::input_timing::nowUs();
        const bool connected = shared.open_existing();
        coral::input_timing::mark(L"tsf.config_source", connect_start, 0, connected ? 1 : 0);
        std::uint64_t generation = 0;
        if (connected && !shared.getGeneration(generation)) return {};
        if (connected && (generation & 1)) return {};
        if (loaded && stamp == last_write && connected == last_shared &&
            (!connected || generation == last_generation)) return {};
        std::unordered_map<std::string, std::string> values;
        if (connected && !shared.read_snapshot(values, generation)) return {};
        auto settings = SettingsConfig::read_only_snapshot(path, values);
        if (!settings) return {};
        const auto after = std::filesystem::last_write_time(path, error);
        if (error || stamp != after) return {};
        std::uint64_t final_generation = 0;
        if (connected && (!shared.getGeneration(final_generation) ||
            final_generation != generation)) return {};
        last_write = stamp;
        last_generation = generation;
        last_shared = connected;
        loaded = true;
        return settings;
    }
};

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
		a.debugEnabled == b.debugEnabled && a.placeholder_schemes == b.placeholder_schemes;
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

struct ConfigAuthority::State {
    Snapshot snapshot;
    Loader loader;
    Schedule schedule;
    std::shared_ptr<std::atomic<bool>> pending = std::make_shared<std::atomic<bool>>(false);
    std::atomic<bool> stopped{false};
};

ConfigAuthority::ConfigAuthority()
    : ConfigAuthority({}, {}, std::make_shared<ImeConfigSnapshot>()) {}

ConfigAuthority::ConfigAuthority(Loader loader, Schedule schedule, Snapshot initial)
    : state_(std::make_shared<State>())
{
    state_->loader = std::move(loader);
    state_->schedule = std::move(schedule);
    state_->snapshot = initial ? std::move(initial) : std::make_shared<ImeConfigSnapshot>();
}

ConfigAuthority::~ConfigAuthority() { stop(); }

void ConfigAuthority::stop()
{
    // 停用只撤销发布资格；在途任务独立拥有状态和 DLL，不回调 COM，也不阻塞宿主退出。
    state_->stopped.store(true, std::memory_order_release);
}

void ConfigAuthority::initialize(const std::string& path)
{
    const auto pending = state_->pending;
    stop();
    state_ = std::make_shared<State>();
    // 重新激活也共用在途额度，旧磁盘读取未结束时不再叠加新任务。
    state_->pending = pending;
    auto initial = buildSnapshot(*sConfig);
    initial->revision = 1;
    state_->snapshot = std::move(initial);
    auto reader = std::make_shared<ConfigReader>(path);
    state_->loader = [reader] {
        auto settings = reader->read();
        if (!settings) return std::shared_ptr<ImeConfigSnapshot>{};
        try { return buildSnapshot(*settings); }
        catch (...) {
            reader->loaded = false;
            throw;
        }
    };
    state_->schedule = ScheduleConfigRefresh;
}

void ConfigAuthority::request_refresh()
{
    const auto state = state_;
    if (state->stopped.load(std::memory_order_acquire) || !state->loader || !state->schedule) return;
    bool expected = false;
    if (!state->pending->compare_exchange_strong(expected, true)) return;
    try {
        if (state->schedule([state] {
            try {
                if (!state->stopped.load(std::memory_order_acquire)) {
                    auto next = state->loader();
                    const auto previous = std::atomic_load(&state->snapshot);
                    if (next && !state->stopped.load(std::memory_order_acquire) &&
                        !SameConfigContent(*previous, *next) &&
                        previous->revision != (std::numeric_limits<std::uint64_t>::max)()) {
                        next->revision = previous->revision + 1;
                        std::atomic_store(&state->snapshot, Snapshot(std::move(next)));
                    }
                }
            } catch (...) {
                // 慢读取、解析或分配失败不能发布默认值或半份配置；下一轮继续尝试。
            }
            state->pending->store(false, std::memory_order_release);
        })) return;
    } catch (...) {}
    state->pending->store(false, std::memory_order_release);
}

std::shared_ptr<const ImeConfigSnapshot> ConfigAuthority::refresh(bool force)
{
    if (force) request_refresh();
    return current();
}

std::shared_ptr<const ImeConfigSnapshot> ConfigAuthority::current() const
{
    return std::atomic_load(&state_->snapshot);
}

std::shared_ptr<ImeConfigSnapshot> ConfigAuthority::buildSnapshot(const SettingsConfig& settings)
{
	auto config = std::make_shared<ImeConfigSnapshot>();
	config->candidateCount = settings.getCandidateCount();
	config->candidateFontSize = settings.getCandidateFontSize();
	config->candidateFontName = settings.getCandidateFontName();
	config->theme = settings.getTheme();
	config->candidateSkin = settings.getCandidateSkin();
	config->floatingToolbarOpen = settings.getIsFloatingToolbarOpen();
	config->smart_prediction = settings.get_smart_prediction();
	config->u_mode = settings.getIsUMode();
	config->v_mode = settings.getIsVMode();
	config->doublepinyinMode = settings.getIsDoublepinyinMode();
	config->doublepinyinScheme = settings.getDoublepinyinSchemeName();
	config->defaultEnglish = settings.getIsDefaultEnglishMode();
	config->useEnglishPunctuation = settings.getIsUseEnglishPunctuation();
	config->autoSymbolCompletion = settings.getIsAutoSymbolCompletion();
	config->autoConvertDigitSymbol = settings.getIsAutoConvertDigitSymbol();
	config->auto_convert_letter_symbol = settings.get_auto_convert_letter_symbol();
	config->clipboardEnabled = settings.getIsClipboardEnabled();
	config->ctrlSpaceShortcut = settings.getIsCtrlAndSpaceShortCutOpen();
	config->shiftShortcut = settings.getIsShiftShortCutOpen();
	config->ctrlShortcut = settings.getIsCtrlShortCutOpen();
	config->shiftSpaceShortcut = settings.getIsShiftAndSpaceShortCutOpen();
	config->simplifiedTraditionalShortcut = settings.getIsShortCutChangeCharacterSimOrTraOpen();
	config->simplifiedTraditionalKey = settings.getCharacterChangeSimOrTra();
	config->ctrlPeriodShortcut = settings.getIsCtrlAndPeriodShortCutOpen();
	config->page_up_down = settings.get_page_up_down_enabled();
    config->translation_enabled = settings.get_translation_enabled();
    config->minus_equals_page = settings.get_minus_equals_page_enabled();
    config->translation_shortcut_key = translation::shortcut_option(settings.get_translation_shortcut()).virtual_key;
    config->translation_key_configured = settings.get_translation_key_configured();
    config->translation_credential_revision = settings.get_translation_credential_revision();
    config->translation_target = settings.get_translation_target();
	config->bracketPage = settings.getIsBraceFlipPageOpen();
	config->commaPeriodPage = settings.getIsCommaAndPeriodFlipPageOpen();
	config->secondThirdCandidateKey = static_cast<int>(settings.getSecondThirdCandidateKey());
	config->wordToCharacterKey = static_cast<int>(settings.getWordToCharacterKey());
	config->pinyin_correction = settings.getPinyinCorrection();
	config->fuzzy_choices = settings.getFuzzyChoice();
	config->limit_count = settings.getLimitCount();
	config->show_infos = settings.getIsShowInfos();
	config->combined_lexicon_version = settings.getCombinedLexiconVersion();
	config->defaultFullWidth = settings.runtime_full_width();
	config->defaultTraditional = settings.runtime_traditional();
	config->debugEnabled = settings.getIsDebugEnabled();
    config->placeholder_schemes = settings.get_placeholder_schemes();
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

#include "pch.h"
#include "candidatecontroller.h"
#include "coreapi.h"

#include <iostream>
#include <chrono>
#include <thread>
#include <stdexcept>

HINSTANCE g_hInst = GetModuleHandleW(nullptr);
namespace Coreapi { void showAddPhrasePage(const CString&, const CString&) {} }

namespace {
void require(bool value, const char* message)
{
    if (!value) throw std::runtime_error(message);
}

class Source final : public coral::text_service::CandidateProvider {
public:
    int remembered = 0;
    bool prediction_test = false;
    std::string last_condition;
    HRESULT query(const coral::text_service::CandidateLookup& lookup,
        coral::text_service::CandidateSnapshot& snapshot) override
    {
        last_condition = lookup.condition;
        if (prediction_test) {
            if (lookup.pinyin == "chuhe") { snapshot.words = {u8"锄禾"}; snapshot.pinyins = {"chu'he"}; }
            else if (lookup.pinyin == "ri") { snapshot.words = {u8"日"}; snapshot.pinyins = {"ri"}; }
            else { snapshot.words = {u8"当午"}; snapshot.pinyins = {"dang'wu"}; }
            return S_OK;
        }
        snapshot.words = { "你好", "您好" };
        snapshot.pinyins = { "ni'hao", "nin'hao" };
        return S_OK;
    }
    HRESULT remember_candidate(coral::composition::SessionToken, const std::string&,
        const std::string&, std::uint32_t&) override { ++remembered; return S_OK; }
    bool apply_candidate_rule(coral::composition::SessionToken, const std::string&,
        const std::string&, coral::CandidateAction, coral::LanguageVariant) override { return false; }
    HRESULT clear_cache(coral::composition::SessionToken) override { return S_OK; }
};

class Sink final : public CandidateActionSink {
public:
    coral::tsf::CandidateStateModel model;
    std::vector<CString> history;
    bool getIsEngPuncMode() const override { return false; }
    void setIsEngPuncMode(bool) override {}
    HRESULT commitCandidateByIndex(int) override { return S_OK; }
    void updateEditSessionAccumulatedPinyin(const CString&) override {}
    HRESULT setCursorPos(int) override { return S_OK; }
    void syncCandidateUI(const coral::tsf::CandidateStateModel& value) override { model = value; }
    void setCandidatePresentation(bool, bool) override {}
    void updateCandidateUIElement() override {}
    std::vector<CString>& getCandInput() override { return history; }
    void endCandidateUIElement() override {}
    void setKeepPlaceholder(bool) override {}
    void updateCompartmentString(const CString&) override {}
    void hideCandidateDlg(BOOL) override {}
};
}

template<class Predicate>
void pump_until(Predicate predicate, int seconds = 3)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(seconds);
    while (!predicate() && std::chrono::steady_clock::now() < deadline) {
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&message); DispatchMessageW(&message); }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    require(predicate(), "Background translation did not finish in time");
}

void check_async(const coral::translation::Credentials* live_credentials)
{
    auto source = std::make_shared<Source>();
    Sink sink;
    CandidateController controller;
    controller.setTextService(&sink);
    controller.set_candidate_provider(source, 1, false);
    auto config = std::make_shared<coral::tsf::ImeConfigSnapshot>();
    config->revision = 1; config->translation_enabled = true; config->translation_key_configured = true;
    controller.setConfigSnapshot(config);
    for (const wchar_t letter : std::wstring(L"nihao")) controller.addLetter(letter);
    auto entered = std::make_shared<std::atomic<bool>>(false);
    controller.set_translation_backend([entered](const coral::translation::Request& request, const coral::translation::Cancellation& cancel) {
        entered->store(true);
        while (!cancel->load()) std::this_thread::sleep_for(std::chrono::milliseconds(1));
        return coral::translation::Result{request.id, coral::translation::Status::kReady, {L"cancelled late response"}};
    });
    require(controller.begin_translation(), "Async entry failed");
    pump_until([&] { return entered->load(); });
    controller.exit_translation();
    require(!controller.is_translation_mode() && controller.getPinyin() == L"nihao", "Async cancellation lost composition");
    controller.set_translation_backend([live_credentials](const coral::translation::Request& request, const coral::translation::Cancellation& cancel) {
        require(request.source_language == "zh", "Source language must be Chinese");
        if (live_credentials) {
            const auto result = coral::translation::translate_general(request, *live_credentials, cancel);
            if (result.status != coral::translation::Status::kReady)
                std::cout << "[LIVE] transport_error=" << result.transport_error << '\n';
            return result;
        }
        return coral::translation::Result{request.id, coral::translation::Status::kReady,
            {request.target_language == "ja" ? L"こんにちは" : L"Hello"}};
    });
    require(controller.begin_translation(), "Async reentry failed");
    pump_until([&] { return controller.modeHint().find(L"翻译中") == std::wstring_view::npos; }, live_credentials ? 20 : 3);
    if (live_credentials && sink.model.empty()) {
        const std::wstring hint(controller.modeHint());
        std::cout << "[LIVE] " << CW2A(hint.c_str(), CP_UTF8) << '\n';
    }
    require(!sink.model.empty(), "Native translation did not produce a candidate");
    if (live_credentials) {
        std::cout << "[LIVE] Native worker -> HTTPS -> candidate -> commit: ";
        std::cout << CW2A(controller.getCandidateText(0), CP_UTF8) << '\n';
    } else {
        config = std::make_shared<coral::tsf::ImeConfigSnapshot>(*config);
        config->revision++; config->translation_target = "ja";
        controller.setConfigSnapshot(config);
        pump_until([&] { return !sink.model.empty(); });
        require(controller.getCandidateText(0) == L"こんにちは" && controller.translation_request().target_language == "ja",
            "Target change did not restart the request");
    }
    CString text;
    require(controller.selectCurrentCandidate(text) == 1 && !text.IsEmpty() && controller.takeVerbatimCommit(), "Async result cannot commit");
    require(source->remembered == 0 && sink.history.empty(), "Async translation entered pinyin learning");
    controller.Reset();
}

void check_prediction()
{
    auto source = std::make_shared<Source>(); source->prediction_test = true;
    Sink sink;
    CandidateController controller;
    controller.setTextService(&sink);
    controller.set_candidate_provider(source, 1, false);
    auto config = std::make_shared<coral::tsf::ImeConfigSnapshot>(); config->revision = 1;
    require(config->smart_prediction, "Prediction must default on");
    controller.setConfigSnapshot(config);
    auto type = [&](const wchar_t* text) { for (wchar_t c : std::wstring(text)) controller.addLetter(c); };
    auto select = [&] {
        CString text;
        require(controller.selectCurrentCandidate(text) == 1, "Prediction fixture commit failed");
        controller.Reset();
    };
    type(L"chuhe"); require(source->last_condition.empty(), "Initial context must be empty"); select();
    type(L"ri"); require(source->last_condition == u8"锄禾", "Previous selection missing"); select();
    type(L"dangwu"); require(source->last_condition == u8"锄禾日", "Single character replaced useful context");
    config = std::make_shared<coral::tsf::ImeConfigSnapshot>(*config);
    config->revision++; config->smart_prediction = false;
    controller.setConfigSnapshot(config);
    require(source->last_condition.empty(), "Disable must immediately requery without context");
    select();
    config = std::make_shared<coral::tsf::ImeConfigSnapshot>(*config);
    config->revision++; config->smart_prediction = true;
    controller.setConfigSnapshot(config);
    type(L"ri"); require(source->last_condition.empty(), "Reenable revived disabled history"); select();
    controller.resetConditionWord();
    type(L"dangwu"); require(source->last_condition.empty(), "Focus boundary retained old context");
    using coral::composition::prediction_context;
    require(prediction_context(L"锄禾", L"日") == L"锄禾日", "Context accumulation failed");
    require(prediction_context(L"猪肉", L"，").empty(), "Punctuation retained context");
    require(prediction_context(L"猪肉", L"abc").empty(), "Latin retained context");
    require(prediction_context(std::wstring(16, L'中'), L"文").size() == 16, "Context is unbounded");
    std::cout << "[PASS] Prediction defaults, accumulated context, toggle, reset and boundaries\n";
}

int main(int argc, char** argv)
{
    try {
        check_prediction();
        coral::tsf::ImeConfigSnapshot shortcuts;
        shortcuts.translation_enabled = true;
        require(shortcuts.translation_shortcut_available(), "Default = shortcut unavailable");
        shortcuts.minus_equals_page = true;
        require(!shortcuts.translation_shortcut_available(), "Conflicting = shortcut still active");
        for (const auto& option : coral::translation::kShortcutOptions) {
            shortcuts.translation_shortcut_key = option.virtual_key;
            require(shortcuts.translation_shortcut_available() == (option.virtual_key != VK_OEM_PLUS), "Function key falsely conflicts");
        }
        shortcuts.translation_enabled = false;
        require(!shortcuts.translation_shortcut_available(), "Disabled translation shortcut active");
        auto source = std::make_shared<Source>();
        Sink sink;
        CandidateController controller;
        controller.setTextService(&sink);
        controller.set_translation_backend([](const coral::translation::Request& request, const coral::translation::Cancellation&) {
            return coral::translation::Result{request.id, coral::translation::Status::kLoading, {}};
        });
        controller.set_candidate_provider(source, 1, false);
        auto config = std::make_shared<coral::tsf::ImeConfigSnapshot>();
        config->revision = 1;
        controller.setConfigSnapshot(config);
        require(!controller.begin_translation(), "Empty input opened translation");
        for (const wchar_t letter : std::wstring(L"nihao")) controller.addLetter(letter);
        require(!controller.begin_translation(), "Disabled translation opened");
        config = std::make_shared<coral::tsf::ImeConfigSnapshot>(*config);
        config->revision++;
        config->translation_enabled = true;
        controller.setConfigSnapshot(config);
        controller.nextCol();
        require(controller.getSelectedCandidateIndex() == 1, "Cannot highlight source candidate");
        const CString raw = controller.getPinyin();
        require(controller.begin_translation(), "Enabled translation did not open");
        require(controller.translation_request().text == L"您好", "Wrong selected source text");
        require(controller.modeHint().find(L"AccessKey") != std::wstring_view::npos,
            "Missing credential hint absent");
        require(sink.model.empty(), "Source candidates were not removed from translation view");
        CString text;
        require(controller.selectCurrentCandidate(text) == 0 && text.IsEmpty(),
            "Status hint could be committed");
        const auto old_id = controller.translation_request().id;
        controller.exit_translation();
        require(controller.getPinyin() == raw && controller.getSelectedCandidateIndex() == 1 &&
            sink.model.size() == 2, "Cancel did not restore input and selection");
        controller.apply_translation_result({old_id, coral::translation::Status::kReady, {L"stale"}});
        require(controller.getCandidateText(0) == L"你好", "Cancelled result replaced source candidates");

        config = std::make_shared<coral::tsf::ImeConfigSnapshot>(*config);
        config->revision++;
        config->translation_key_configured = true;
        controller.setConfigSnapshot(config);
        require(controller.begin_translation(), "Translation did not reopen");
        require(controller.modeHint().find(L"翻译中") != std::wstring_view::npos,
            "Offline provider incorrectly reports success");
        const auto id = controller.translation_request().id;
        require(id != old_id, "Reopened request reused an ID");
        controller.apply_translation_result({old_id, coral::translation::Status::kReady, {L"stale"}});
        require(sink.model.empty(), "Stale response entered a later translation");
        controller.apply_translation_result({id, coral::translation::Status::kReady, {L"", L"Hello", L"Hi!"}});
        require(sink.model.size() == 2, "Translation result candidates absent");
        controller.move_translation_selection(-1);
        controller.move_translation_selection(1);
        require(controller.selectCurrentCandidate(text) == 1 && text == L"Hi!" &&
            controller.takeVerbatimCommit(), "Selected translation did not commit verbatim");
        require(source->remembered == 0 && sink.history.empty(), "Translation polluted pinyin learning");
        require(!controller.canDisableCandidate(0), "Translation exposed dictionary actions");
        controller.exit_translation();
        controller.begin_translation();
        config = std::make_shared<coral::tsf::ImeConfigSnapshot>(*config);
        config->revision++;
        config->translation_enabled = false;
        controller.setConfigSnapshot(config);
        require(!controller.is_translation_mode() && controller.getPinyin() == raw,
            "Disabling translation left stale mode active");
        controller.Reset();
        controller.apply_translation_result({id, coral::translation::Status::kReady, {L"stale"}});
        require(!controller.is_translation_mode() && controller.getPinyin().IsEmpty(),
            "Reset was undone by stale response");
        check_async(nullptr);
        if (argc == 2 && std::string(argv[1]) == "--live") {
            coral::translation::Credentials credentials;
            std::getline(std::cin, credentials.access_key_id);
            std::getline(std::cin, credentials.access_key_secret);
            const auto started = std::chrono::steady_clock::now();
            check_async(&credentials);
            std::cout << "[LIVE] elapsed_ms=" << std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - started).count() << '\n';
        }
        std::cout << "[PASS] Translation entry, status, selection, verbatim commit, cancellation and stale responses\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}

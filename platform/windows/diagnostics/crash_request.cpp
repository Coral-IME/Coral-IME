#include "crash_request.h"
#include <limits>

namespace coral::diagnostics {
namespace {
bool parse_decimal(std::wstring_view text, std::uint64_t maximum, std::uint64_t& result) noexcept {
    if (text.empty() || (text.size() > 1 && text.front() == L'0')) return false;
    std::uint64_t value = 0;
    for (const wchar_t character : text) {
        if (character < L'0' || character > L'9') return false;
        const auto digit = static_cast<std::uint64_t>(character - L'0');
        if (value > (maximum - digit) / 10) return false;
        value = value * 10 + digit;
    }
    result = value;
    return true;
}
}

const wchar_t* module_name(CrashModule module) noexcept {
    switch (module) {
    case CrashModule::kSettings: return L"settings";
    case CrashModule::kUpdater: return L"updater";
    case CrashModule::kSessionHost: return L"session_host";
    }
    return nullptr;
}

bool parse_crash_request(const std::vector<std::wstring_view>& arguments, CrashRequest& result) noexcept {
    if (arguments.size() != 5) return false;
    CrashRequest candidate;
    unsigned seen = 0;
    for (const auto argument : arguments) {
        const auto equal = argument.find(L'=');
        if (equal == std::wstring_view::npos) return false;
        const auto key = argument.substr(0, equal), value = argument.substr(equal + 1);
        unsigned bit = 0;
        std::uint64_t number = 0;
        if (key == L"--coral-crash") {
            bit = 1;
            if (value != L"1") return false;
        } else if (key == L"--pid") {
            bit = 2;
            if (!parse_decimal(value, UINT32_MAX, number) || !number || number == UINT32_MAX) return false;
            candidate.process_id = static_cast<std::uint32_t>(number);
        } else if (key == L"--tid") {
            bit = 4;
            if (!parse_decimal(value, UINT32_MAX, number) || number == UINT32_MAX) return false;
            candidate.thread_id = static_cast<std::uint32_t>(number);
        } else if (key == L"--exception") {
            bit = 8;
            if (!parse_decimal(value, UINT64_MAX, candidate.exception_address)) return false;
        } else if (key == L"--module") {
            bit = 16;
            if (value == L"settings") candidate.module = CrashModule::kSettings;
            else if (value == L"updater") candidate.module = CrashModule::kUpdater;
            else if (value == L"session_host") candidate.module = CrashModule::kSessionHost;
            else return false;
        } else return false;
        if (seen & bit) return false;
        seen |= bit;
    }
    if (seen != 31 || (candidate.exception_address && !candidate.thread_id)) return false;
    result = candidate;
    return true;
}
}

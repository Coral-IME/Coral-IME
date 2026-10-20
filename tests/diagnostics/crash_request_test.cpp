#include "crash_request.h"
#include <iostream>
#include <string>

int main() {
    using namespace coral::diagnostics;
    const std::vector<std::wstring_view> valid{L"--module=session_host", L"--tid=42", L"--pid=7",
        L"--exception=18446744073709551615", L"--coral-crash=1"};
    CrashRequest result;
    if (!parse_crash_request(valid, result) || result.process_id != 7 || result.thread_id != 42
        || result.exception_address != UINT64_MAX || result.module != CrashModule::kSessionHost) return 1;
    auto invalid = valid;
    for (const auto value : {L"--pid=0", L"--pid=00", L"--pid=+7", L"--pid=-7", L"--pid=7tail", L"--pid= 7",
        L"--pid=4294967295", L"--pid=4294967296", L"--pid=", L"x--pid=7", L"--unknown=7", L"--tid=42"}) {
        invalid = valid; invalid[2] = value;
        if (parse_crash_request(invalid, result) || result.process_id != 7 || result.exception_address != UINT64_MAX) return 1;
    }
    for (const auto value : {L"--exception=18446744073709551616", L"--exception=000", L"--exception=0x123",
        L"--exception=-1", L"--exception=1=2"}) {
        invalid = valid; invalid[3] = value; if (parse_crash_request(invalid, result)) return 1;
    }
    for (const auto value : {L"--module=../settings", L"--module=coraltsf", L"--module=", L"--module=Settings"}) {
        invalid = valid; invalid[0] = value; if (parse_crash_request(invalid, result)) return 1;
    }
    invalid = valid; invalid[4] = L"--coral-crash=2"; if (parse_crash_request(invalid, result)) return 1;
    invalid = valid; invalid[1] = L"--tid=0"; if (parse_crash_request(invalid, result)) return 1;
    invalid[3] = L"--exception=0"; if (!parse_crash_request(invalid, result) || result.thread_id || result.exception_address) return 1;
    invalid.pop_back(); if (parse_crash_request(invalid, result)) return 1;
    invalid = valid; invalid.push_back(L"--coral-crash=1"); if (parse_crash_request(invalid, result)) return 1;
    if (module_name(static_cast<CrashModule>(-1))) return 1;
    std::cout << "Strict request fixtures and transactional rejection passed\n";
    return 0;
}

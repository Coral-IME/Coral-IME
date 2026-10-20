#pragma once
#include <cstdint>
#include <string_view>
#include <vector>

namespace coral::diagnostics {
enum class CrashModule { kSettings, kUpdater, kSessionHost };
struct CrashRequest {
    std::uint32_t process_id = 0;
    std::uint32_t thread_id = 0;
    std::uint64_t exception_address = 0;
    CrashModule module = CrashModule::kSettings;
};
const wchar_t* module_name(CrashModule module) noexcept;
// 恰好五个带版本字段，规范的无符号十进制，失败时不产生半成品。
bool parse_crash_request(const std::vector<std::wstring_view>& arguments, CrashRequest& result) noexcept;
}

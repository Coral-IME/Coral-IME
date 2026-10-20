#include "crash_reporter.h"
#include "coral/product/product_identity.h"
#include <windows.h>
#include <algorithm>
#include <string>
#include <vector>

namespace coral::diagnostics {
namespace {
constexpr std::size_t kCommandCapacity = 32768;
wchar_t collector_path[kCommandCapacity]{};
wchar_t working_directory[kCommandCapacity]{};
wchar_t fault_command[kCommandCapacity]{};
const wchar_t* fault_module = nullptr;
DWORD collector_wait_ms = 0;
volatile LONG installation_state = 0;
volatile LONG fault_started = 0;

std::filesystem::path executable_path() {
    for (DWORD capacity = 512; capacity <= kCommandCapacity; capacity *= 2) {
        std::vector<wchar_t> buffer(capacity);
        const auto length = ::GetModuleFileNameW(nullptr, buffer.data(), capacity);
        if (!length) return {};
        if (length < capacity) return std::wstring(buffer.data(), length);
    }
    return {};
}

void append_text(wchar_t*& output, const wchar_t* text) noexcept {
    while (*text) *output++ = *text++;
}
void append_decimal(wchar_t*& output, std::uint64_t number) noexcept {
    wchar_t digits[20];
    unsigned count = 0;
    do { digits[count++] = static_cast<wchar_t>(L'0' + number % 10); number /= 10; } while (number);
    while (count) *output++ = digits[--count];
}

LONG WINAPI report_fault(EXCEPTION_POINTERS* exception) {
    if (::InterlockedCompareExchange(&fault_started, 1, 0) != 0) return EXCEPTION_CONTINUE_SEARCH;
    // 固定的进程生命周期存储；此处无日志、无注册表读取、无 C++ 分配、不新建线程。
    auto output = fault_command;
    append_text(output, L"\""); append_text(output, collector_path);
    append_text(output, L"\" --coral-crash=1 --pid="); append_decimal(output, ::GetCurrentProcessId());
    append_text(output, L" --tid="); append_decimal(output, ::GetCurrentThreadId());
    append_text(output, L" --exception="); append_decimal(output, reinterpret_cast<std::uintptr_t>(exception));
    append_text(output, L" --module="); append_text(output, fault_module); *output = L'\0';
    STARTUPINFOW startup{sizeof(startup)};
    PROCESS_INFORMATION child{};
    if (::CreateProcessW(collector_path, fault_command, nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
        nullptr, working_directory, &startup, &child)) {
        ::CloseHandle(child.hThread);
        ::WaitForSingleObject(child.hProcess, collector_wait_ms);
        ::CloseHandle(child.hProcess);
    }
    // 只有本 EXE 安装此回调。绝不终止文字服务宿主。
    ::TerminateProcess(::GetCurrentProcess(), 1);
    return EXCEPTION_EXECUTE_HANDLER;
}
}

bool install_crash_reporter(CrashModule module, const std::filesystem::path& collector_override,
    std::uint32_t wait_ms) noexcept {
    if (!module_name(module) || !wait_ms || wait_ms > 30000) return false;
    if (::InterlockedCompareExchange(&installation_state, -1, 0) != 0) return false;
    try {
        const auto image = executable_path();
        const auto collector = collector_override.empty()
            ? image.parent_path() / product::kDiagnosticExeWide : collector_override;
        std::error_code error;
        const auto path = collector.wstring();
        const auto directory = collector.parent_path().wstring();
        if (image.empty() || !collector.is_absolute() || path.size() + 256 >= kCommandCapacity
            || path.find_first_of(L"\"\r\n") != std::wstring::npos || path.find(L'\0') != std::wstring::npos
            || !std::filesystem::is_regular_file(collector, error) || error) {
            ::InterlockedExchange(&installation_state, 0);
            return false;
        }
        std::copy(path.begin(), path.end(), collector_path); collector_path[path.size()] = L'\0';
        std::copy(directory.begin(), directory.end(), working_directory); working_directory[directory.size()] = L'\0';
        fault_module = module_name(module);
        collector_wait_ms = wait_ms;
        ::SetUnhandledExceptionFilter(report_fault);
        ::InterlockedExchange(&installation_state, 1);
        return true;
    } catch (...) {
        ::InterlockedExchange(&installation_state, 0);
        return false;
    }
}
}

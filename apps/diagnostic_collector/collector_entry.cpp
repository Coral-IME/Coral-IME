#include "crash_request.h"
#include "zip_archive.h"
#include <windows.h>
#include <dbghelp.h>
#include <bcrypt.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

namespace coral::diagnostics {
namespace {
class OwnedHandle {
public:
    explicit OwnedHandle(HANDLE value) : value_(value) {}
    ~OwnedHandle() { if (value_ && value_ != INVALID_HANDLE_VALUE) ::CloseHandle(value_); }
    OwnedHandle(const OwnedHandle&) = delete;
    OwnedHandle& operator=(const OwnedHandle&) = delete;
    HANDLE get() const { return value_; }
    explicit operator bool() const { return value_ && value_ != INVALID_HANDLE_VALUE; }
private:
    HANDLE value_;
};
class SystemDumpLibrary {
public:
    SystemDumpLibrary() : library_(::LoadLibraryExW(L"dbghelp.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32)) {}
    ~SystemDumpLibrary() { if (library_) ::FreeLibrary(library_); }
    SystemDumpLibrary(const SystemDumpLibrary&) = delete;
    SystemDumpLibrary& operator=(const SystemDumpLibrary&) = delete;
    decltype(&::MiniDumpWriteDump) writer() const {
        return library_ ? reinterpret_cast<decltype(&::MiniDumpWriteDump)>(::GetProcAddress(library_, "MiniDumpWriteDump")) : nullptr;
    }
private:
    HMODULE library_;
};

std::filesystem::path output_root() {
    const DWORD required = ::GetEnvironmentVariableW(L"CORAL_DIAGNOSTIC_OUTPUT_DIR", nullptr, 0);
    if (required) {
        if (required > 32768) return {};
        std::vector<wchar_t> buffer(required);
        const auto length = ::GetEnvironmentVariableW(L"CORAL_DIAGNOSTIC_OUTPUT_DIR", buffer.data(), required);
        if (!length || length >= required) return {};
        return std::wstring(buffer.data(), length);
    }
    std::vector<wchar_t> buffer(32768);
    const auto length = ::GetTempPathW(static_cast<DWORD>(buffer.size()), buffer.data());
    if (!length || length >= buffer.size()) return {};
    return std::filesystem::path(std::wstring(buffer.data(), length)) / L"CoralPinyin" / L"diagnostics";
}

std::filesystem::path reserve_report_directory(const std::filesystem::path& root) {
    if (!root.is_absolute() || root.wstring().size() > 32000) return {};
    std::error_code error;
    std::filesystem::create_directories(root, error);
    if (error) return {};
    constexpr wchar_t kHex[] = L"0123456789abcdef";
    for (unsigned attempt = 0; attempt != 8; ++attempt) {
        UCHAR random[16]{};
        if (::BCryptGenRandom(nullptr, random, sizeof(random), BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0) return {};
        std::wstring name = L"report_";
        for (const auto byte : random) { name += kHex[byte >> 4]; name += kHex[byte & 15]; }
        const auto candidate = root / name;
        if (std::filesystem::create_directory(candidate, error)) return candidate;
        if (error) return {};
    }
    return {};
}

bool capture_dump(const CrashRequest& request, HANDLE target, const std::filesystem::path& destination) {
    SystemDumpLibrary library;
    const auto writer = library.writer();
    if (!writer) return false;
    OwnedHandle file(::CreateFileW(destination.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
        FILE_ATTRIBUTE_NORMAL, nullptr));
    if (!file) return false;
    MINIDUMP_EXCEPTION_INFORMATION exception{};
    exception.ThreadId = request.thread_id;
    exception.ExceptionPointers = reinterpret_cast<EXCEPTION_POINTERS*>(static_cast<std::uintptr_t>(request.exception_address));
    exception.ClientPointers = TRUE; // 地址属于仍然存活的出错进程。
    const auto type = static_cast<MINIDUMP_TYPE>(MiniDumpNormal | MiniDumpWithUnloadedModules | MiniDumpWithThreadInfo);
    // 异常捕获失败仍按失败处理；不静默回退到快照。
    return writer(target, request.process_id, file.get(), type,
        request.exception_address ? &exception : nullptr, nullptr, nullptr) != FALSE;
}

int collect_report(const CrashRequest& request) {
    if (request.exception_address > std::numeric_limits<std::uintptr_t>::max()) return 2;
    OwnedHandle target(::OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, request.process_id));
    if (!target) return 3;
    BOOL target_wow64 = FALSE, self_wow64 = FALSE;
    if (!::IsWow64Process(target.get(), &target_wow64) || !::IsWow64Process(::GetCurrentProcess(), &self_wow64)
        || target_wow64 != self_wow64) return 3;
    const auto directory = reserve_report_directory(output_root());
    if (directory.empty()) return 4;
    const auto dump = directory / L"process.dmp";
    if (!capture_dump(request, target.get(), dump)) return 3;
    const auto manifest = directory / L"report.json";
    {
        std::ofstream output(manifest, std::ios::binary);
        output << "{\"format_version\":1,\"module\":\"";
        for (const auto character : std::wstring_view(module_name(request.module))) output << static_cast<char>(character);
        output << "\",\"process_id\":" << request.process_id << ",\"thread_id\":" << request.thread_id
            << ",\"exception_capture\":" << (request.exception_address ? "true" : "false") << "}\n";
        output.flush();
        if (!output) return 4;
    }
    std::string error;
    if (!archive::writeZip(directory / L"report.zip", {{"process.dmp", dump}, {"report.json", manifest}}, error)) return 4;
    return 0;
}
}
}

int wmain(int argc, wchar_t** argv) {
    try {
        std::vector<std::wstring_view> arguments;
        for (int index = 1; index < argc; ++index) arguments.emplace_back(argv[index]);
        coral::diagnostics::CrashRequest request;
        if (!coral::diagnostics::parse_crash_request(arguments, request)) return 2;
        return coral::diagnostics::collect_report(request);
    } catch (...) { return 4; }
}

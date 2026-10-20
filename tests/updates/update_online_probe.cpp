#include "coral/updates/update_notice.h"
#include "coral/updates/update_service.h"
#include "update_http.h"
#include "update_process.h"
#include "package_acceptance.h"
#include <windows.h>
#include <iostream>
#include <stdexcept>

int wmain(int argc, wchar_t** argv) {
    using namespace coral::updates;
    try {
        if (argc != 3 || !update_service_available()) return 2;
        const auto root = std::filesystem::absolute(argv[1]);
        std::filesystem::create_directories(root);
        // 子进程继承独立临时目录，不使用现用输入法的更新缓存。
        if (!SetEnvironmentVariableW(L"TMP", root.c_str()) || !SetEnvironmentVariableW(L"TEMP", root.c_str())) return 3;
        std::wcerr << L"Isolated cache: " << package_cache_directory().c_str() << L'\n';
        const auto preflight = reserve_pending_package();
        if (preflight.empty()) throw std::runtime_error("Cannot prepare isolated cache, Win32=" + std::to_string(GetLastError()));
        std::error_code cleanup_error;
        std::filesystem::remove(preflight, cleanup_error);
        const std::wstring installed_text(argv[2]);
        std::string installed_ascii(installed_text.begin(), installed_text.end());
        ReleaseVersion installed;
        if (!parse_release_version(installed_ascii, installed)) return 4;
        UpdateHttp http;
        const auto reply = http.get(release_check_url(), [] { return false; });
        UpdateNotice notice;
        if (reply.error != DownloadError::kNone || !parse_update_notice(reply.body, notice) || !notice.package)
            throw std::runtime_error("HTTPS metadata check failed");
        if (!is_newer_version(notice.version, installed)) throw std::runtime_error("No newer version for fixture");
        auto process = UpdateProcess::start(adjacent_update_agent(), *notice.package);
        if (!process) throw std::runtime_error("Cannot launch production download agent");
        const auto deadline = GetTickCount64() + 610000;
        for (;;) {
            const auto progress = process->poll();
            if (progress.stage == DownloadStage::kReady) break;
            if (terminal_stage(progress.stage) || GetTickCount64() >= deadline)
                throw std::runtime_error("Download failed, stage=" + std::to_string(static_cast<unsigned>(progress.stage))
                    + ", error=" + std::to_string(static_cast<unsigned>(progress.error)));
            Sleep(50);
        }
        process.reset();
        const auto ready = ready_package_path(notice.version);
        if (verify_package(ready, notice.package->package) != PackageStatus::kReady)
            throw std::runtime_error("Final package verification failed");
        std::cout << "{\"verified\":true,\"version\":\"" << format_release_version(notice.version)
            << "\",\"bytes\":" << notice.package->package.byte_count << ",\"installer_executed\":false}\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
    return 0;
}

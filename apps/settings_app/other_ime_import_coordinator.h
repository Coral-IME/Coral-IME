#pragma once
#ifndef CORAL_APPS_SETTINGS_APP_OTHER_IME_IMPORT_COORDINATOR_H_INCLUDED
#define CORAL_APPS_SETTINGS_APP_OTHER_IME_IMPORT_COORDINATOR_H_INCLUDED

#include <windows.h>
#include <filesystem>
#include <string>

namespace coral::settings_app
{
// 独立工具窗口由用户操作；本对象只持有启动进程句柄与当前接收窗口的关联。
class OtherImeImportCoordinator
{
public:
    static constexpr ULONG_PTR kImportMessage = 0x43494D31;
    OtherImeImportCoordinator() = default;
    ~OtherImeImportCoordinator();
    OtherImeImportCoordinator(const OtherImeImportCoordinator&) = delete;
    OtherImeImportCoordinator& operator=(const OtherImeImportCoordinator&) = delete;
    bool start(HWND owner, const std::filesystem::path& output, std::wstring& error);
    bool accept_message(HWND sender, const COPYDATASTRUCT* data, std::wstring& path) const;
private:
    HANDLE process_ = nullptr;
    DWORD process_id_ = 0;
};
}
#endif

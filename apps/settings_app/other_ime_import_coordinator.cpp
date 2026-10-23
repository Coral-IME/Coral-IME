#include "other_ime_import_coordinator.h"

#include <algorithm>
#include <vector>

namespace coral::settings_app
{
namespace
{
    bool regular_file(const std::filesystem::path& path)
    {
        std::error_code error;
        return path.is_absolute() && std::filesystem::is_regular_file(path, error);
    }

    std::wstring quote_argument(const std::wstring& argument)
    {
        std::wstring result = L"\"";
        size_t slashes = 0;
        for (const wchar_t c : argument)
        {
            if (c == L'\\') { ++slashes; continue; }
            result.append(slashes * (c == L'"' ? 2 : 1), L'\\');
            if (c == L'"') result += L'\\';
            result += c;
            slashes = 0;
        }
        result.append(slashes * 2, L'\\');
        result += L'"';
        return result;
    }

    std::filesystem::path find_python()
    {
        // 只从 Python 的安装登记与显式 PATH 查找，不搜索当前工作目录。
        for (HKEY root : { HKEY_CURRENT_USER, HKEY_LOCAL_MACHINE })
        for (REGSAM view : { KEY_WOW64_64KEY, KEY_WOW64_32KEY })
        {
            HKEY key = nullptr;
            if (RegOpenKeyExW(root, L"Software\\Python\\PythonCore", 0, KEY_READ | view, &key) != ERROR_SUCCESS) continue;
            std::vector<std::wstring> versions;
            for (DWORD i = 0;; ++i)
            {
                wchar_t name[256]{};
                DWORD length = 256;
                if (RegEnumKeyExW(key, i, name, &length, nullptr, nullptr, nullptr, nullptr) != ERROR_SUCCESS) break;
                if (std::wstring(name).find(L"3.") == 0) versions.emplace_back(name);
            }
            RegCloseKey(key);
            for (const auto& version : versions)
            {
                const auto subkey = L"Software\\Python\\PythonCore\\" + version + L"\\InstallPath";
                wchar_t value[32768]{};
                DWORD size = sizeof(value);
                if (RegGetValueW(root, subkey.c_str(), nullptr, RRF_RT_REG_SZ |
                    (view == KEY_WOW64_64KEY ? RRF_SUBKEY_WOW6464KEY : RRF_SUBKEY_WOW6432KEY), nullptr, value, &size) == ERROR_SUCCESS)
                {
                    for (const auto* name : { L"pythonw.exe", L"python.exe" })
                    {
                        const auto path = std::filesystem::path(value) / name;
                        if (regular_file(path)) return path;
                    }
                }
            }
        }
        const DWORD needed = GetEnvironmentVariableW(L"PATH", nullptr, 0);
        if (needed)
        {
            std::wstring search(needed, L'\0');
            if (GetEnvironmentVariableW(L"PATH", search.data(), needed))
            for (const auto* name : { L"pythonw.exe", L"python.exe" })
            {
                wchar_t path[32768]{};
                const DWORD length = SearchPathW(search.c_str(), name, nullptr, 32768, path, nullptr);
                if (length && length < 32768 && regular_file(path)) return path;
            }
        }
        return {};
    }
}

OtherImeImportCoordinator::~OtherImeImportCoordinator()
{
    if (process_) CloseHandle(process_);
}

bool OtherImeImportCoordinator::start(HWND owner, const std::filesystem::path& output, std::wstring& error)
{
    if (process_ && WaitForSingleObject(process_, 0) == WAIT_TIMEOUT)
    {
        error = L"迁移工具已经打开，请在工具窗口中继续操作。";
        return false;
    }
    if (process_) { CloseHandle(process_); process_ = nullptr; }
    process_id_ = 0;
    wchar_t module[32768]{};
    const DWORD length = GetModuleFileNameW(nullptr, module, 32768);
    if (!length || length == 32768) { error = L"无法确定工具所在位置。"; return false; }
    const auto script = std::filesystem::path(module).parent_path() / L"tools" / L"ime_dictionary_tool.py";
    if (!regular_file(script)) { error = L"迁移工具文件缺失，请重新安装完整的珊瑚输入法。"; return false; }
    const auto python = find_python();
    if (python.empty())
    {
        error = L"此工具需要 Python 3（含 Tkinter 窗口组件）。请安装 Python 后重试；已有 TSV 可直接使用“导入词库”。";
        return false;
    }
    std::wstring command = quote_argument(python.wstring()) + L" " + quote_argument(script.wstring()) +
        L" --coral-window " + std::to_wstring(reinterpret_cast<ULONG_PTR>(owner)) +
        L" --coral-pid " + std::to_wstring(GetCurrentProcessId()) + L" --output " + quote_argument(output.wstring());
    STARTUPINFOW startup{sizeof(startup)};
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(python.c_str(), command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
        nullptr, script.parent_path().c_str(), &startup, &process))
    {
        error = L"无法打开 Python 迁移工具。请检查 Python 是否安装完整，也可先单独运行工具后导入 TSV。";
        return false;
    }
    CloseHandle(process.hThread);
    process_ = process.hProcess;
    process_id_ = process.dwProcessId;
    return true;
}

bool OtherImeImportCoordinator::accept_message(HWND sender, const COPYDATASTRUCT* data, std::wstring& path) const
{
    if (!process_ || !process_id_ || WaitForSingleObject(process_, 0) != WAIT_TIMEOUT ||
        !data || data->dwData != kImportMessage || !data->lpData || data->cbData < 4 ||
        data->cbData > 65536 || data->cbData % sizeof(wchar_t)) return false;
    DWORD sender_pid = 0;
    if (!IsWindow(sender) || !GetWindowThreadProcessId(sender, &sender_pid) || sender_pid != process_id_) return false;
    const auto* text = static_cast<const wchar_t*>(data->lpData);
    const size_t count = data->cbData / sizeof(wchar_t);
    if (text[count - 1] || std::find(text, text + count - 1, L'\0') != text + count - 1) return false;
    const std::filesystem::path file(std::wstring(text, count - 1));
    if (!regular_file(file) || file.extension() != L".tsv") return false;
    path = file.wstring();
    return true;
}
}

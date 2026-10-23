#include "other_ime_import_coordinator.h"
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace
{
coral::settings_app::OtherImeImportCoordinator* coordinator = nullptr;
std::wstring received;
void require(bool value, const char* message)
{
    if (!value) throw std::runtime_error(message);
}
LRESULT CALLBACK receive(HWND hwnd, UINT message, WPARAM sender, LPARAM payload)
{
    if (message == WM_COPYDATA && coordinator)
        return coordinator->accept_message(reinterpret_cast<HWND>(sender),
            reinterpret_cast<const COPYDATASTRUCT*>(payload), received) ? TRUE : FALSE;
    return DefWindowProcW(hwnd, message, sender, payload);
}
}

int main()
{
    HWND window = nullptr;
    try
    {
        wchar_t module[32768]{};
        require(GetModuleFileNameW(nullptr, module, 32768) != 0, "Missing test location");
        const auto directory = std::filesystem::path(module).parent_path();
        std::filesystem::create_directories(directory / L"tools");
        // 人工助手只创建隐藏窗口并回传人工 TSV，不读取安装输入法或真实用户数据。
        std::ofstream script(directory / L"tools" / L"ime_dictionary_tool.py", std::ios::binary);
        script << R"PY(import argparse, ctypes
from ctypes import wintypes as w
from pathlib import Path
p=argparse.ArgumentParser()
p.add_argument('--coral-window',type=int)
p.add_argument('--coral-pid',type=int)
p.add_argument('--output')
a=p.parse_args()
out=Path(a.output);out.mkdir(parents=True,exist_ok=True)
file=out/'coral_import.tsv'
file.write_text('word\tpinyin\n\u4f60\u597d\tni hao\n',encoding='utf-8-sig')
u=ctypes.WinDLL('user32')
u.CreateWindowExW.argtypes=[w.DWORD,w.LPCWSTR,w.LPCWSTR,w.DWORD,ctypes.c_int,ctypes.c_int,ctypes.c_int,ctypes.c_int,w.HWND,w.HMENU,w.HINSTANCE,ctypes.c_void_p]
u.CreateWindowExW.restype=w.HWND
sender=u.CreateWindowExW(0,'STATIC','migration test',0,0,0,1,1,None,None,None,None)
class Data(ctypes.Structure):
 _fields_=[('tag',ctypes.c_size_t),('size',w.DWORD),('data',ctypes.c_void_p)]
text=ctypes.create_unicode_buffer(str(file.resolve()))
data=Data(0x43494D31,ctypes.sizeof(text),ctypes.cast(text,ctypes.c_void_p))
u.SendMessageTimeoutW.argtypes=[w.HWND,w.UINT,ctypes.c_size_t,ctypes.c_ssize_t,w.UINT,w.UINT,ctypes.POINTER(ctypes.c_size_t)]
u.SendMessageTimeoutW.restype=ctypes.c_ssize_t
result=ctypes.c_size_t()
ok=u.SendMessageTimeoutW(a.coral_window,0x004a,sender,ctypes.addressof(data),2,5000,ctypes.byref(result))
u.DestroyWindow.argtypes=[w.HWND]
u.DestroyWindow(sender)
raise SystemExit(0 if ok and result.value else 1)
)PY";
        script.close();
        WNDCLASSW wc{};
        wc.lpfnWndProc = receive;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = L"CoralMigrationHandoffTest";
        require(RegisterClassW(&wc) != 0, "Register test window failed");
        window = CreateWindowW(wc.lpszClassName, L"", 0, 0, 0, 1, 1, nullptr, nullptr, wc.hInstance, nullptr);
        require(window != nullptr, "Create test window failed");
        coral::settings_app::OtherImeImportCoordinator instance;
        coordinator = &instance;
        std::wstring error;
        const auto output = directory / L"人工输出 空格";
        require(instance.start(window, output, error), "Start migration helper failed");
        std::wstring duplicate_error;
        require(!instance.start(window, output, duplicate_error), "Duplicate tool process accepted");
        // 本进程伪造的发送窗口不属于启动的 Python 进程，应被拒绝。
        wchar_t fake[] = L"C:\\fake.tsv";
        COPYDATASTRUCT data{coral::settings_app::OtherImeImportCoordinator::kImportMessage, sizeof(fake), fake};
        std::wstring ignored;
        require(!instance.accept_message(window, &data, ignored), "Unexpected sender accepted");
        const ULONGLONG deadline = GetTickCount64() + 10000;
        while (received.empty() && GetTickCount64() < deadline)
        {
            MsgWaitForMultipleObjects(0, nullptr, FALSE, 50, QS_ALLINPUT);
            MSG message{};
            while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) DispatchMessageW(&message);
        }
        require(received == (output / L"coral_import.tsv").wstring(), "Unicode TSV handoff failed");
        DestroyWindow(window);
        window = nullptr;
        coordinator = nullptr;
        std::cout << "Migration process launch and authenticated TSV handoff passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        if (window) DestroyWindow(window);
        coordinator = nullptr;
        std::cerr << error.what() << '\n';
        return 1;
    }
}

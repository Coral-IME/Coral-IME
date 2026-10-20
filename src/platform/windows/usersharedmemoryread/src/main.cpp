#include <filesystem>
#include <iostream>
#include <string>
#include <iomanip>
#include <windows.h>
#include <boost/interprocess/exceptions.hpp>
#include "structure.h"
#include "regex"

namespace bip = boost::interprocess;

/*  * @brief 打印共享映射的内容。
 * @param map 要打印的共享映射。
 * @param map_name 用于显示的映射名称。
  */
void printSharedMap(const shared_map& map, const std::string& map_name) {
    // 使用 .c_str() 把 Boost 字符串转成 C 风格字符串用于输出
    std::cout << "--- " << map_name << " (Size: " << map.size() << ") ---" << std::endl;

    if (map.empty()) {
        std::cout << "  (Map is empty)" << std::endl;
        std::cout << "-------------------------------------" << std::endl;
        return;
    }

    // 遍历映射中的每个键值对（拼音 -> 节点集合）
    for (const auto& [pinyinKey, nodeSet] : map) {

        std::cout << "  Key: [" << pinyinKey.c_str() << "]" << std::endl;
        std::cout << "  Phrases (Count: " << nodeSet.size() << "):" << std::endl;

        // 遍历集合中的每个节点指针
        for (const auto& nodePtr : nodeSet) {
            // 像普通指针一样解引用 offset_ptr
            if (nodePtr) {
                const UserPhraseNode& node = *nodePtr;
                std::cout << "    - Phrase: " << node.phrase.c_str()
                          << ", Pinyin: " << node.pinyin.c_str()
                          << ", QuanPinyin: " << node.quanPinyin.c_str()
                          << ", Commit: " << node.commit
                          << ", Tick: " << node.tick
                          << ", Dee: " << node.dee << std::endl;
            }
        }
    }
    std::cout << "-------------------------------------" << std::endl;
}

bool isVersionNumber(const std::string& str) {
    const std::regex version_regex(R"(^(\d+\.){0,3}\d+$)");
    return std::regex_match(str, version_regex);
}

int main() {
    // --- 关键步骤：把控制台输出设为 UTF-8，以正确显示共享内存中的多字节字符 ---
    SetConsoleOutputCP(CP_UTF8);
    // 确保 std::cout 使用新的代码页
    setvbuf(stdout, nullptr, _IOFBF, 1000);

    // --- 1. 从用户输入获取共享内存名称 ---
    std::string lineInput;
    std::cout << "Please enter the name of the shared memory segment: ";
    std::getline(std::cin, lineInput);

    // 新逻辑：只使用输入中第一个空格或制表符之前的部分。
    std::string shmNameStr = lineInput.substr(0, lineInput.find_first_of(" \t"));

    if (isVersionNumber(shmNameStr))
    {
        const std::filesystem::path userPath(std::getenv("USERPROFILE"));
        shmNameStr = "C__Users_"+userPath.filename().string()+"_AppData_LocalLow_coralpinyin_"+shmNameStr+"_userlexicon";
    }

    if (shmNameStr.empty()) {
        std::cerr << "Error: Shared memory name cannot be empty." << std::endl;
        return 1;
    }

    // --- 2. 定义名称 ---
    // 共享内存中根对象的名称

    // 根据用户提供的共享内存名构造互斥量名称
    const std::wstring shmNameW(shmNameStr.begin(), shmNameStr.end());
    const std::wstring mutexNameW = shmNameW + L"_Mutex";

    HANDLE hMutex = nullptr;

    try {
        const auto ROOT_OBJECT_NAME = "UserLexiconDataRootCrossArch";
        // --- 3. 打开并锁定互斥量 ---
        std::wcout << L"Attempting to open mutex: " << mutexNameW.c_str() << std::endl;
        hMutex = OpenMutexW(MUTEX_ALL_ACCESS, FALSE, mutexNameW.c_str());

        if (hMutex == nullptr) {
            std::cerr << "Error: Could not open mutex. Error code: " << GetLastError() << std::endl;
            std::cerr << "Please ensure the writer process is running and has created the mutex." << std::endl;
            return 1;
        }

        std::cout << "Mutex opened. Waiting for lock..." << std::endl;
        // 无限等待以取得互斥量的所有权
        const DWORD waitResult = WaitForSingleObject(hMutex, INFINITE);

        if (waitResult != WAIT_OBJECT_0) {
            std::cerr << "Error: Failed to lock mutex. Error code: " << GetLastError() << std::endl;
            CloseHandle(hMutex);
            return 1;
        }

        std::cout << "Mutex locked successfully. Accessing shared memory..." << std::endl;

        // --- 临界区开始：现在可以安全访问共享内存 ---

        // --- 4. 打开共享内存 ---
        // 我们是读取方，使用 open_only 模式
        InteropManagedSharedMemory shm(bip::open_only, shmNameStr.c_str());

        // --- 5. 打印共享内存信息 ---
        const auto totalSize = shm.get_size();
        const auto freeMemory = shm.get_free_memory();
        const auto usedMemory = totalSize - freeMemory;
        std::cout << "\n===== Shared Memory '" << shmNameStr << "' Status =====\n" << std::endl;
        std::cout << std::fixed << std::setprecision(2); // 设置输出的浮点精度
        std::cout << "  Total Size:   " << std::setw(12) << totalSize << " bytes (" << static_cast<double>(totalSize) / (1024 * 1024) << " MB)" << std::endl;
        std::cout << "  Used Space:   " << std::setw(12) << usedMemory << " bytes (" << static_cast<double>(usedMemory) / (1024 * 1024) << " MB)" << std::endl;
        std::cout << "  Free Space:   " << std::setw(12) << freeMemory << " bytes (" << static_cast<double>(freeMemory) / (1024 * 1024) << " MB)" << std::endl;
        std::cout << "\n========================================\n" << std::endl;


        // --- 6. 查找根对象 ---
        const auto result = shm.find<UserSharedLexiconData>(ROOT_OBJECT_NAME);

        if (result.first == nullptr) {
            std::cerr << "Error: Could not find root object '" << ROOT_OBJECT_NAME << "' in shared memory." << std::endl;
            ReleaseMutex(hMutex);
            CloseHandle(hMutex);
            return 1;
        }

        const UserSharedLexiconData& rootData = *result.first;

        // --- 7. 读取并打印数据 ---
        std::cout << "===== Reading User Lexicon Data =====\n" << std::endl;
        std::cout << "Version Traditional: " << rootData.versionTraditional << std::endl;
        std::cout << "Version Simplified:  " << rootData.versionSimplified << std::endl;
        std::cout << "Ticked Traditional:  " << rootData.tickedTraditional << std::endl;
        std::cout << "Ticked Simplified:   " << rootData.tickedSimplified << std::endl;
        std::cout << std::endl;

        printSharedMap(rootData.mapTraditional, "Traditional Map");
        printSharedMap(rootData.mapSimplified, "Simplified Map");

        std::cout << "\n===== Finished Reading Data =====\n" << std::endl;

        // --- 临界区结束 ---

        // --- 8. 释放互斥量 ---
        std::cout << "Releasing mutex..." << std::endl;
        if (!ReleaseMutex(hMutex)) {
            std::cerr << "Warning: Failed to release mutex. Error code: " << GetLastError() << std::endl;
        }

    } catch (const bip::interprocess_exception& ex) {
        std::cerr << "Boost.Interprocess Exception: " << ex.what() << std::endl;
        std::cerr << "Please ensure the shared memory segment '" << shmNameStr << "' exists." << std::endl;
        if (hMutex != nullptr) {
            ReleaseMutex(hMutex);
        }
    } catch (const std::exception& ex) {
        std::cerr << "Standard Exception: " << ex.what() << std::endl;
        if (hMutex != nullptr) {
            ReleaseMutex(hMutex);
        }
    }

    // --- 9. 关闭互斥量句柄 ---
    if (hMutex != nullptr) {
        CloseHandle(hMutex);
    }

    std::cout << "Press Enter to exit..." << std::endl;
    // 等待最后的回车前清空输入缓冲
    std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
    std::cin.get();

    return 0;
}

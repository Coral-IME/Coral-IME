// coral_history_inspector - 用户词典实时监控工具
// 独立可执行程序，直接读取共享内存显示用户词典状态
#include <iostream>
#include <iomanip>
#include <string>
#include <vector>
#include <map>
#include <thread>
#include <chrono>
#include <sstream>
#include <conio.h>  // 用于 _kbhit() 和 _getch()
#include "../../../modules/foundation/text_file_helpers.hpp"

// Boost.Interprocess 需要的类型重载
namespace boost::interprocess::ipcdetail {
    template<class SizeType1, class SizeType2>
    inline SizeType1 get_rounded_size(SizeType1 origSize, SizeType2 roundTo)
    {
        return ((origSize - 1) / static_cast<SizeType1>(roundTo) + 1) * static_cast<SizeType1>(roundTo);
    }
}

#include <boost/interprocess/managed_windows_shared_memory.hpp>
#include <boost/interprocess/containers/string.hpp>
#include <boost/interprocess/containers/map.hpp>
#include <boost/interprocess/allocators/allocator.hpp>
#include <boost/interprocess/containers/set.hpp>
#include <boost/interprocess/offset_ptr.hpp>
#include <boost/interprocess/indexes/iset_index.hpp>

#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#include <windows.h>  // 用于控制台 ANSI 支持

namespace bip = boost::interprocess;

// 复制用户词典的类型定义（与 userlexicon.h 保持一致）
template<class T>
using InteropOffsetPtr = bip::offset_ptr<T, std::int64_t, std::uint64_t>;

using InteropMutexFamily = bip::mutex_family;

using InteropMemAlgo = bip::rbtree_best_fit<
    InteropMutexFamily,
    InteropOffsetPtr<void>
>;

using InteropManagedSharedMemory = bip::basic_managed_windows_shared_memory<
    char,
    InteropMemAlgo,
    bip::iset_index
>;

template <typename T>
class InteropAllocator : public bip::allocator<T, InteropManagedSharedMemory::segment_manager>
{
public:
    typedef std::uint64_t size_type;
    typedef std::int64_t  difference_type;

    template<class T2>
    struct rebind {
        typedef InteropAllocator<T2> other;
    };

    InteropAllocator(InteropManagedSharedMemory::segment_manager* segment_mngr)
        : bip::allocator<T, InteropManagedSharedMemory::segment_manager>(segment_mngr) {}

    template<class T2>
    InteropAllocator(const InteropAllocator<T2>& other)
        : bip::allocator<T, InteropManagedSharedMemory::segment_manager>(other.get_segment_manager()) {}
};

using ShString = bip::basic_string<char, std::char_traits<char>, InteropAllocator<char>>;

struct UserPhraseNode;
using NodePtr = InteropOffsetPtr<UserPhraseNode>;
namespace coral::phonetics {
using Score = float;
} // 命名空间 coral::phonetics 结束（批次 3）

namespace coral::phonetics {
using PersonalLexiconIndex = int32_t;
} // 命名空间 coral::phonetics 结束（批次 3）


#pragma pack(push, 1)
struct alignas(8) UserPhraseNode {
    UserPhraseNode(
        const InteropAllocator<void>& alloc,
        const char* word,
        const int commit,
        const coral::phonetics::Score dee,
        const int tick,
        const char* jpy,
        const char* qpy,
        const coral::phonetics::PersonalLexiconIndex idx = -1,
        const int isFixed = 0
    ) : phrase(word, alloc), pinyin(jpy, alloc), quanPinyin(qpy, alloc), commit(commit), tick(tick), dee(dee), index(idx), is_fixed_candidate(isFixed) {}

    ShString phrase;
    ShString pinyin;
    ShString quanPinyin;
    int commit;
    int tick;
    coral::phonetics::Score dee;
    coral::phonetics::PersonalLexiconIndex index;
    int is_fixed_candidate;

    bool operator<(const UserPhraseNode& other) const {
        if (this->dee != other.dee) {
            return this->dee > other.dee;
        }
        return this->phrase < other.phrase;
    }
};
#pragma pack(pop)

struct NodePtrCompare {
    bool operator()(const NodePtr& a, const NodePtr& b) const {
        return *a < *b;
    }
};

using shared_set = bip::set<
    NodePtr,
    NodePtrCompare,
    InteropAllocator<NodePtr>
>;

struct TransparentStringLess {
    using is_transparent = void;

    template<class T1, class T2>
    bool operator()(const T1& s1, const T2& s2) const {
        return std::string_view(s1) < std::string_view(s2);
    }
};

using shared_map = boost::interprocess::map<
    ShString,
    shared_set,
    TransparentStringLess,
    InteropAllocator<std::pair<const ShString, shared_set>>
>;

#pragma pack(push, 1)
struct alignas(8) UserSharedLexiconData {
    coral::phonetics::Score versionTraditional;
    coral::phonetics::Score versionSimplified;
    coral::phonetics::Score versionSimplifiedFixed;
    coral::phonetics::Score versionTraditionalFixed;
    int   tickedTraditional;
    int   tickedSimplified;
    int   tickedSimplifiedFixed;
    int   tickedTraditionalFixed;
    shared_map mapTraditional;
    shared_map mapSimplified;
    shared_map mapSimplifiedFixed;
    shared_map mapTraditionalFixed;

    explicit UserSharedLexiconData(const InteropAllocator<void>& alloc)
      : versionTraditional(1.0f),
        versionSimplified(1.0f),
        versionSimplifiedFixed(1.0f),
        versionTraditionalFixed(1.0f),
        tickedTraditional(0),
        tickedSimplified(0),
        tickedSimplifiedFixed(0),
        tickedTraditionalFixed(0),
        mapTraditional(TransparentStringLess(),alloc),
        mapSimplified(TransparentStringLess(),alloc),
        mapSimplifiedFixed(TransparentStringLess(),alloc),
        mapTraditionalFixed(TransparentStringLess(),alloc)
    {}
};
#pragma pack(pop)

// 辅助函数：构建共享内存名称（需要与 userlexicon.cpp 中的逻辑一致）
std::string buildSHMName() {
    // 与 hlp::buildSHName(std::string(USER_LEXICON_VERSION), "userlexicon") 保持一致
    // USER_LEXICON_VERSION = "1.0"（用户词库版本）
    // 格式：CORALPY_<version>_<filename>
    return "CORALPY_1.0_userlexicon";
}

// 启用 Windows 控制台 ANSI 转义序列支持
void enableAnsiSupport() {
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hOut != INVALID_HANDLE_VALUE) {
        DWORD dwMode = 0;
        if (GetConsoleMode(hOut, &dwMode)) {
            dwMode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
            SetConsoleMode(hOut, dwMode);
        }
    }
}

// 隐藏光标
void hideCursor() {
    std::cout << "\033[?25l" << std::flush;
}

// 显示光标
void showCursor() {
    std::cout << "\033[?25h" << std::flush;
}

// 清屏函数
void clearScreen() {
    system("cls");
    hideCursor();  // 清屏后重新隐藏光标
}

// 状态快照结构
struct LexiconSnapshot {
    float versionSimplified;
    float versionTraditional;
    float versionSimplifiedFixed;
    float versionTraditionalFixed;
    int tickedSimplified;
    int tickedTraditional;
    int tickedSimplifiedFixed;
    int tickedTraditionalFixed;
    size_t totalEntries;
    
    bool operator==(const LexiconSnapshot& other) const {
        return versionSimplified == other.versionSimplified &&
               versionTraditional == other.versionTraditional &&
               versionSimplifiedFixed == other.versionSimplifiedFixed &&
               versionTraditionalFixed == other.versionTraditionalFixed &&
               tickedSimplified == other.tickedSimplified &&
               tickedTraditional == other.tickedTraditional &&
               tickedSimplifiedFixed == other.tickedSimplifiedFixed &&
               tickedTraditionalFixed == other.tickedTraditionalFixed &&
               totalEntries == other.totalEntries;
    }
    
    bool operator!=(const LexiconSnapshot& other) const {
        return !(*this == other);
    }
};

// 创建状态快照
LexiconSnapshot createSnapshot(const UserSharedLexiconData* data) {
    LexiconSnapshot snapshot;
    snapshot.versionSimplified = data->versionSimplified;
    snapshot.versionTraditional = data->versionTraditional;
    snapshot.versionSimplifiedFixed = data->versionSimplifiedFixed;
    snapshot.versionTraditionalFixed = data->versionTraditionalFixed;
    snapshot.tickedSimplified = data->tickedSimplified;
    snapshot.tickedTraditional = data->tickedTraditional;
    snapshot.tickedSimplifiedFixed = data->tickedSimplifiedFixed;
    snapshot.tickedTraditionalFixed = data->tickedTraditionalFixed;
    
    // 计算总条目数
    size_t totalEntries = 0;
    for (const auto& [_, nodeSet] : data->mapSimplified) {
        totalEntries += static_cast<size_t>(nodeSet.size());
    }
    for (const auto& [_, nodeSet] : data->mapTraditional) {
        totalEntries += static_cast<size_t>(nodeSet.size());
    }
    for (const auto& [_, nodeSet] : data->mapSimplifiedFixed) {
        totalEntries += static_cast<size_t>(nodeSet.size());
    }
    for (const auto& [_, nodeSet] : data->mapTraditionalFixed) {
        totalEntries += static_cast<size_t>(nodeSet.size());
    }
    snapshot.totalEntries = totalEntries;
    
    return snapshot;
}

// 显示单个词典的内容
void displayLexicon(std::stringstream& ss, const shared_map& lexicon, const std::string& lexiconName) {
    if (lexicon.empty()) {
        return;
    }

    ss << "\n[" << lexiconName << "]" << std::endl;
    ss << std::string(100, '-') << std::endl;
    
    // 表头
    ss << std::left 
       << std::setw(20) << "Pinyin" 
       << std::setw(30) << "Phrase" 
       << std::setw(8) << "Commit" 
       << std::setw(12) << "Status"
       << std::setw(8) << "Dee"
       << std::endl;
    ss << std::string(100, '-') << std::endl;

    int count = 0;
    int deletedCount = 0;
    int fixedCount = 0;
    
    for (const auto& [pinyinKey, nodeSet] : lexicon) {
        for (const auto& nodePtr : nodeSet) {
            // 根据 index 确定状态
            std::string status;
            if (nodePtr->index == 0) {
                status = "[DELETED]";
                deletedCount++;
            } else if (nodePtr->index >= 1) {
                status = "[FIX:" + std::to_string(nodePtr->index) + "]";
                fixedCount++;
            } else {
                status = "[ACTIVE]";
            }
            
            ss << std::left
               << std::setw(20) << std::string(nodePtr->pinyin.c_str())
               << std::setw(30) << std::string(nodePtr->phrase.c_str())
               << std::setw(8) << nodePtr->commit
               << std::setw(12) << status
               << std::setw(8) << std::fixed << std::setprecision(2) << nodePtr->dee
               << std::endl;
            count++;
        }
    }

    ss << std::string(100, '-') << std::endl;
    ss << "Subtotal: " << count << " entries (" 
       << (count - deletedCount - fixedCount) << " active, " 
       << fixedCount << " fixed, "
       << deletedCount << " deleted)" << std::endl;
}

// 主监控函数
void monitorUserLexicon() {
    std::string shmName = buildSHMName();
    
    // ASCII loader 动画帧（避免 Unicode 编码问题）
    const std::vector<std::string> loaderFrames = {
        "|", "/", "-", "\\", "|", "/", "-", "\\"
    };
    size_t loaderIndex = 0;
    
    // 上次的状态快照
    LexiconSnapshot lastSnapshot = {};
    bool hasLastSnapshot = false;
    
    while (true) {
        try {
            // 打开共享内存（只读模式）
            InteropManagedSharedMemory segment(bip::open_read_only, shmName.c_str());
            
            // 查找用户词典数据
            auto result = segment.find<UserSharedLexiconData>("UserLexiconDataRootCrossArch");
            if (result.second == 0) {
                clearScreen();
                std::cout << std::string(100, '=') << std::endl;
                std::cout << "USER LEXICON MONITOR" << std::endl;
                std::cout << std::string(100, '=') << std::endl;
                std::cout << "\nError: User lexicon data not found in shared memory!" << std::endl;
                std::cout << "Please ensure a process providing the matching legacy user-lexicon layout is initialized." << std::endl;
                std::cout << "\nPress 'q' to quit, any other key to retry..." << std::endl;
                
                if (_kbhit()) {
                    char ch = _getch();
                    if (ch == 'q' || ch == 'Q') {
                        break;
                    }
                }
                std::this_thread::sleep_for(std::chrono::seconds(2));
                continue;
            }

            UserSharedLexiconData* data = result.first;
            
            // 创建当前状态快照
            LexiconSnapshot currentSnapshot = createSnapshot(data);
            
            // 检查状态是否变化
            bool stateChanged = !hasLastSnapshot || (currentSnapshot != lastSnapshot);
            
            if (stateChanged) {
                // 状态变化，完全刷新显示
                // 使用 stringstream 缓存所有输出，最后一次性显示
                std::stringstream ss;
                
                ss << std::string(100, '=') << std::endl;
                ss << "USER LEXICON MONITOR" << std::endl;
                ss << "Shared Memory: " << shmName << std::endl;
                
                // 显示时间
                auto now = std::chrono::system_clock::now();
                auto time = std::chrono::system_clock::to_time_t(now);
                std::tm tm = {};
                LOCALTIME_R(&time, &tm);
                ss << "Time: " << std::put_time(&tm, "%Y-%m-%d %H:%M:%S") << std::endl;
                ss << std::string(100, '=') << std::endl;

                // 显示版本和 tick 信息
                ss << "\nVersion Info:" << std::endl;
                ss << "  Simplified: " << data->versionSimplified << " (tick: " << data->tickedSimplified << ")" << std::endl;
                ss << "  Traditional: " << data->versionTraditional << " (tick: " << data->tickedTraditional << ")" << std::endl;
                ss << "  SimplifiedCandidatePolicy: " << data->versionSimplifiedFixed << " (tick: " << data->tickedSimplifiedFixed << ")" << std::endl;
                ss << "  TraditionalCandidatePolicy: " << data->versionTraditionalFixed << " (tick: " << data->tickedTraditionalFixed << ")" << std::endl;

                // 显示各个词典内容
                displayLexicon(ss, data->mapSimplified, "Simplified");
                displayLexicon(ss, data->mapTraditional, "Traditional");
                displayLexicon(ss, data->mapSimplifiedFixed, "SimplifiedCandidatePolicy");
                displayLexicon(ss, data->mapTraditionalFixed, "TraditionalCandidatePolicy");

                ss << "\n" << std::string(100, '=') << std::endl;
                ss << "TOTAL: " << currentSnapshot.totalEntries << " entries across all lexicons" << std::endl;
                ss << std::string(100, '=') << std::endl;
                ss << "\nPress 'q' to quit" << std::endl;
                ss << "Monitoring... ";
                
                // 清屏并一次性输出所有内容
                clearScreen();
                std::cout << ss.str() << std::flush;
                
                // 更新快照
                lastSnapshot = currentSnapshot;
                hasLastSnapshot = true;
            } else {
                // 状态未变化，只更新加载指示器
                std::cout << "\rMonitoring... " << loaderFrames[loaderIndex] << std::flush;
            }
            
            // 更新加载动画索引
            loaderIndex = (loaderIndex + 1) % loaderFrames.size();

        } catch (const bip::interprocess_exception& e) {
            clearScreen();
            std::cout << std::string(100, '=') << std::endl;
            std::cout << "USER LEXICON MONITOR" << std::endl;
            std::cout << std::string(100, '=') << std::endl;
            std::cout << "\nError: Cannot open shared memory!" << std::endl;
            std::cout << "Details: " << e.what() << std::endl;
            std::cout << "\nPossible reasons:" << std::endl;
            std::cout << "  1. Langengine DLL not loaded" << std::endl;
            std::cout << "  2. User lexicon not initialized yet" << std::endl;
            std::cout << "  3. Shared memory name mismatch" << std::endl;
            std::cout << "\nPress 'q' to quit, any other key to retry..." << std::endl;
            
            if (_kbhit()) {
                char ch = _getch();
                if (ch == 'q' || ch == 'Q') {
                    break;
                }
            }
            std::this_thread::sleep_for(std::chrono::seconds(2));
            continue;
        }

        // 等待用户输入或自动刷新
        auto start = std::chrono::steady_clock::now();
        while (true) {
            if (_kbhit()) {
                char ch = _getch();
                if (ch == 'q' || ch == 'Q') {
                    std::cout << "\n";  // 换行，避免光标停留在加载指示器上
                    return;
                }
            }

            auto elapsed = std::chrono::steady_clock::now() - start;
            if (elapsed >= std::chrono::milliseconds(200)) {
                break;  // 200ms 检查一次，更新加载动画
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    }
}

int main(int argc, char* argv[]) {
    // 设置控制台代码页为 UTF-8
    system("chcp 65001 > nul");
    
    // 启用 ANSI 转义序列支持
    enableAnsiSupport();
    hideCursor();

    std::cout << "User Lexicon Real-time Monitor" << std::endl;
    std::cout << "Connecting to shared memory..." << std::endl;
    std::cout << std::endl;

    std::this_thread::sleep_for(std::chrono::seconds(1));

    monitorUserLexicon();

    showCursor();  // 恢复光标显示
    std::cout << "\nMonitor stopped." << std::endl;
    return 0;
}

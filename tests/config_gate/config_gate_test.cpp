// Phase 6 Step 4 跨进程配置更新（代际门闸）测试
// 机制：共享内存传输（seqlock 双缓冲，读侧无锁）+ SettingsConfig::pollForUpdates
//       代际门闸（m_seqNum 每写 +2）+ 共享内存不可用时的 INI mtime 回退
//       （回退分支需要共享内存不可用的 UWP 环境，不在本测试覆盖范围）。
// 模拟方式：测试进程内第二个 SharedMemoryConfig 实例与引擎 DLL 共享同一命名段，
//       等价于设置界面进程的写入视角（真实跨进程语义）。
// 行为断言：跨进程写 fuzzy_choices → 下一词解析即生效（门闸放行、快照重建、
//       buildFuzzyMap 读新快照重建模糊表）；无后续写时输出稳定（门闸关闭、
//       快照不变）；连续写取最终值；写回原值恢复基线。
// 用法：config_gate_test <runtimeDataDir>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#include <windows.h>  // comm_help.hpp 依赖 Sddl.h 需先有 windows.h

#include "syllable_helpers.h"      // hlp::ImportResult 等，api.h 依赖
#include "core_operations.h"
#include "shared_preferences.h"
#include "preference_registry.h"

namespace fs = std::filesystem;

namespace {

int g_failures = 0;

void Check(bool cond, const std::string& name) {
	std::cout << (cond ? "[PASS] " : "[FAIL] ") << name << "\n";
	if (!cond) ++g_failures;
}

// 引擎静态析构存在已知段错误（线程池静态析构期 join），
// 输出已 flush 后直接退出，不触发 DLL 静态析构（测试采用立即退出策略）。
[[noreturn]] void Finish(int code) {
	std::cout.flush();
	std::cerr.flush();
	::TerminateProcess(::GetCurrentProcess(), (UINT)code);
}

std::string ParseToString(const std::string& input) {
	std::vector<std::string> words, pys;
	int counts = 0;
	coral::runtime::parse_candidates("", input.c_str(),
		std::back_inserter(words), std::back_inserter(pys),
		0, 7, 200, &counts, 0);
	std::string joined;
	for (const auto& w : words) joined += w + "|";
	return joined;
}

bool SetupScratch(const fs::path& dataDir, fs::path& scratch) {
	const char* user = std::getenv("CORAL_USER_DATA_DIR");
	scratch = user && *user ? fs::u8path(user) / "config-gate-scratch" : dataDir / ".." / "config-gate-scratch";
	std::error_code ec;
	fs::remove_all(scratch, ec);
	fs::create_directories(scratch / "config", ec);
	if (ec) return false;
	// 引擎从 coralDir/config/coralpinyinConfig.ini 读配置，用户词典也落在 coralDir
	fs::copy_file(dataDir / "config" / "coralpinyinConfig.ini",
		scratch / "config" / "coralpinyinConfig.ini",
		fs::copy_options::overwrite_existing, ec);
	if (ec) return false;
	// 系统词库目录通过环境变量选择传入的运行资源目录
	std::string dir = dataDir.string();
	_putenv_s("CORAL_LANGENGINE_DATA_DIR", dir.c_str());
	return true;
}

}  // namespace

int main(int argc, char** argv) {
	if (argc < 2) {
		std::cerr << "usage: config_gate_test <runtimeDataDir>\n";
		Finish(2);
	}
	fs::path dataDir = fs::absolute(argv[1]);
	fs::path scratch;
	if (!SetupScratch(dataDir, scratch)) {
		std::cerr << "failed to setup scratch dir under " << scratch << "\n";
		Finish(2);
	}
	if (!coral::runtime::start_runtime(scratch.string().c_str(), true)) {
		std::cerr << "start_runtime failed\n";
		Finish(3);
	}

	// --- 1. 基线：fuzzy_choices 全关（INI 默认）时 "zongguo" 的解析输出 ---
	// （zong 是合法音节，无 z→zh 模糊时不会出现 zhongguo 对应的候选）
	const std::string baseline = ParseToString("zongguo");
	Check(!baseline.empty(), "baseline parse produces output");
	std::cout << "[DBG] baseline  = " << baseline << "\n";

	{
		// --- 2. 模拟设置界面进程写共享内存：z→zh 模糊音开启 ---
		SharedMemoryConfig writer;
		if (!writer.initialize()) {
			std::cerr << "writer SharedMemoryConfig initialize failed\n";
			Finish(4);
		}
        Check(sConfig->get_smart_prediction(), "prediction defaults on for existing config");
        writer.setConfig("smartPrediction", "false");
        ParseToString("nihao");
        Check(!sConfig->get_smart_prediction(), "prediction disable is synchronized");
        writer.setConfig("smartPrediction", "true");
        ParseToString("nihao");
        Check(sConfig->get_smart_prediction(), "prediction enable is synchronized");
		writer.setConfig("fuzzyChoice", "1000000000000000000");

		// --- 3. 下一次解析入口即生效（门闸放行 → 快照重建 → buildFuzzyMap 读新快照） ---
		const std::string fuzzyOn = ParseToString("zongguo");
		std::cout << "[DBG] fuzzyOn   = " << fuzzyOn << "\n";
		Check(fuzzyOn != baseline, "cross-process write takes effect on next parse");
		Check(fuzzyOn.find("中国") != std::string::npos, "fuzzy z->zh surfaces zhongguo candidate");

		// --- 4. 门闸关闭：无后续写时输出稳定 ---
		Check(ParseToString("zongguo") == fuzzyOn, "stable output when no further writes");

		// --- 5. 连续写（代际多次推进），最终值生效 ---
		writer.setConfig("fuzzyChoice", "0000000000000000000");
		writer.setConfig("fuzzyChoice", "1000000000000000000");
		Check(ParseToString("zongguo") == fuzzyOn, "latest of consecutive writes wins");

		// --- 6. 写回原值，恢复基线 ---
		writer.setConfig("fuzzyChoice", "0000000000000000000");
		Check(ParseToString("zongguo") == baseline, "write back restores baseline");
	}

	std::cout << (g_failures == 0 ? "CONFIG GATE PASS\n" : "CONFIG GATE FAIL\n");
	Finish(g_failures == 0 ? 0 : 1);
}

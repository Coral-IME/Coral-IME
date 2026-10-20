// coral::composition 契约类型：KeyEvent / InputContext / Candidate / Output（Phase 4）。
// 平台无关：只有 Unicode code point、UTF-8 文本与通用 modifier；
// Windows VK / COM 类型一律在平台适配层转换，禁止进入本头。
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace coral::composition {

constexpr int kModeQuanpin = 0;
constexpr int kModeDoublepinyin = 1;

// 通用特殊键（VK 在适配层映射到这些值）
enum class Key : std::uint32_t
{
	kNone = 0,
	kBackspace,
	kEscape,
	kSpace,
	kEnter,
	kTab,
	kPageUp,
	kPageDown,
	kHome,
	kEnd,
	kLeft,
	kRight,
	kUp,
	kDown,
	kDelete,
};

// 按键事件：codepoint > 0 表示可打印字符输入
struct KeyEvent
{
	std::uint32_t codepoint = 0; // Unicode code point（可打印字符）
	Key key = Key::kNone;         // 特殊键（codepoint == 0 时有效）
	bool shift = false;
	bool ctrl = false;
	bool alt = false;
	std::uint32_t vk = 0; // 原始平台键码，仅诊断/适配用，核心逻辑不得依赖

	bool printable() const { return codepoint > 0; }
};

// 输入上下文：焦点与应用相关信息（首期最小集）
struct InputContext
{
	// 应用进程名（用于 App 兼容规则；空表示未知）
	std::string process_name;
};

// 候选项：词 + 对应拼音（均 UTF-8）
struct Candidate
{
	std::uint64_t id = 0; // 快照稳定的 id；UI 动作不得使用视觉索引
	std::string word;
	std::string pinyin;
	std::string display_text; // 可选的带注音标签；词语始终按原文上屏。
};

// 候选页视图
struct CandidatePage
{
	std::vector<Candidate> candidates;
	int offset = 0;         // 当前页在完整候选列表中的起始下标
	bool has_next_page = false;

	bool empty() const { return candidates.empty(); }
};

// 单次按键的完整输出：平台层只负责应用，不做业务决策
struct Output
{
	bool consumed = false;             // 是否消费该按键
	std::string composition;           // 预编辑文本（UTF-8；空表示无 composition）
	std::string commit_text;            // 本次上屏文本（UTF-8；空表示不上屏）
	CandidatePage candidates;          // 当前候选页
	bool candidates_visible = false;    // 是否显示候选窗
};

} // 命名空间 coral::composition 结束

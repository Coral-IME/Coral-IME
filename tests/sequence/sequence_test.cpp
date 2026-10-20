// 按键序列测试（Phase 4）：无 COM、无窗口，直接驱动 coral::composition::CompositionSession。
// 验证：composition / candidate / commit 与 TSF 行为一致（引擎同源，
// 断言固定为序列语义而非快照，细节回归由 golden 覆盖）。
#include "coral/composition/composition_session.h"

#include <windows.h> // comm_help.hpp 依赖（api.h 链）
#include "syllable_helpers.h"
#include "core_operations.h"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using namespace coral::composition;

namespace {

int g_failures = 0;

void expect(bool cond, const char* what)
{
	if (!cond)
	{
		std::fprintf(stderr, "FAIL: %s\n", what);
		++g_failures;
	}
}

KeyEvent charKey(char c) { KeyEvent e; e.codepoint = static_cast<unsigned char>(c); return e; }
KeyEvent key(Key k) { KeyEvent e; e.key = k; return e; }

void type(CompositionSession& s, const char* chars, Output& last)
{
	InputContext ctx;
	for (const char* p = chars; *p; ++p)
		last = s.send_key(charKey(*p), ctx);
}

} // namespace

int main(int argc, char** argv)
{
	if (argc < 2)
	{
		std::fprintf(stderr, "usage: sequence_test <runtimeDataDir>\n");
		return 2;
	}
	// 引擎初始化：与 TSF CPinyinModule::start 相同路径
	// （数据目录来自环境变量覆盖或参数；此处使用隔离测试数据目录）
	if (!coral::runtime::start_runtime(argv[1], true))
	{
		std::fprintf(stderr, "engine init failed\n");
		return 2;
	}

	CompositionSession s;
	InputContext ctx;
	Output out;

	// ---- 场景 1：输入拼音出候选 ----
	type(s, "nihao", out);
	expect(out.consumed, "s1 consumed");
	expect(out.composition == "nihao", "s1 composition");
	expect(!out.candidates.empty(), "s1 candidates non-empty");
	expect(out.candidates_visible, "s1 candidates visible");
	expect(out.commit_text.empty(), "s1 no commit");

	// ---- 场景 2：空格首选上屏 ----
	out = s.send_key(key(Key::kSpace), ctx);
	expect(out.consumed, "s2 consumed");
	expect(!out.commit_text.empty(), "s2 commit non-empty");
	expect(!out.candidates_visible, "s2 candidates closed");
	expect(s.pinyin().empty() || out.composition.empty(), "s2 state cleared");

	// ---- 场景 3：退格删除拼音 ----
	type(s, "abc", out);
	out = s.send_key(key(Key::kBackspace), ctx);
	expect(out.composition == "ab", "s3 backspace composition");

	// ---- 场景 4：Esc 清空 ----
	out = s.send_key(key(Key::kEscape), ctx);
	expect(out.consumed, "s4 consumed");
	expect(!s.has_input(), "s4 state cleared");

	// ---- 场景 5：翻页 ----
	type(s, "shi", out);
	const int firstPageCount = static_cast<int>(out.candidates.candidates.size());
	out = s.send_key(key(Key::kPageDown), ctx);
	expect(out.consumed, "s5 consumed");
	if (out.candidates.offset > 0)
		expect(out.candidates.offset == firstPageCount, "s5 page offset");
	out = s.send_key(key(Key::kPageUp), ctx);
	expect(out.candidates.offset == 0, "s5 back to first page");
	s.send_key(key(Key::kEscape), ctx);

	// ---- 场景 6：数字选词 + 连续输入 ----
	type(s, "nihaoshijie", out);
	expect(!out.candidates.empty(), "s6 candidates");
	out = s.send_key(charKey('1'), ctx);
	expect(out.consumed, "s6 consumed");
	// 长句首选可能一次全部转换（无剩余）或留下剩余候选，两者皆合法
	if (out.candidates_visible)
	{
		expect(!out.composition.empty(), "s6 remaining composition");
		out = s.send_key(charKey('1'), ctx);
	}
	expect(!out.commit_text.empty(), "s6 commit");
	expect(!out.candidates_visible, "s6 finished");

	// 输入层将小鹤 vsgo 展开为 zhong'guo，再以双拼模式交给引擎。
	// 引擎应直接使用已分隔的音节并返回与全拼一致的汉字候选。
	out = s.query_page("", "zhong'guo", 0, kModeDoublepinyin);
	expect(out.candidates_visible, "s7 doublepinyin candidates visible");
	bool foundChina = false;
	for (const auto& candidate : out.candidates.candidates)
		if (candidate.word == "中国") foundChina = true;
	expect(foundChina, "s7 doublepinyin zhong guo candidate");

	const bool pass = (g_failures == 0);
	std::printf("%s (%d failures)\n", pass ? "SEQUENCE PASS" : "SEQUENCE FAIL", g_failures);
	std::fflush(stdout);
	// 引擎异步日志线程池存在 teardown 竞态（Phase 3 遗留记录在案），
	// 硬退出保证退出码干净
	::TerminateProcess(::GetCurrentProcess(), pass ? 0 : 1);
	return pass ? 0 : 1;
}

// ConfigSnapshot：引擎侧配置快照（Phase 3 Step 3，宿主推送 + 原子换指针）。
// 引擎热路径只读本快照，不再触碰 SettingsConfig 单例与 INI/共享内存等平台机制；
// 配置的读取与装配全部收敛到宿主边界（engine.cpp / api.cpp），变更时整体推送。
// 参考 mozc session->SetConfig(shared_ptr<const Config>) 的原子换快照模式。
#pragma once

#include <memory>
#include <vector>

namespace coral {

// 引擎实际消费的配置项集合（不可变值对象：变更 = 边界侧整体重建后推送）
struct EngineConfig
{
	bool smart_prediction = true;
	bool isUMode = false;
	bool isVMode = false;
	bool pinyin_correction = true;
	std::vector<int> fuzzy_choices;	// 模糊拼音开关（FUZZY_CHOICES = 19 项）
	int limit_count = 10;
	bool isShowInfos = false;
	int combined_lexicon_version = -1;	// 未装配前的哨兵值，与旧 attach 适配层一致
};

class ConfigSnapshot
{
public:
	ConfigSnapshot();

	// 宿主推送：原子替换整份快照（线程安全；读侧无锁）
	void update(std::shared_ptr<const EngineConfig> config);

	bool valid() const;

	// ---- 引擎当前实际消费的配置项 ----

	bool smart_prediction() const;
	bool isUMode() const;
	bool isVMode() const;
	bool pinyin_correction() const;
	std::vector<int> fuzzy_choices() const;
	int limit_count() const;
	bool isShowInfos() const;
	// 组合词库版本（云词库增量加载的失效判据）
	int combined_lexicon_version() const;

private:
	std::shared_ptr<const EngineConfig> m_config;
};

} // 命名空间 coral 结束

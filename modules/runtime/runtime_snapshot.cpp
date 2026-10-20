#include "pch.h"
#include "runtime_snapshot.h"

namespace coral {
namespace {

// 未装配前的默认快照：对齐旧 sConfig 读穿透在未初始化时的兜底行为
//（默认配置表：pinyin_correction=true、limit_count=10、模糊拼音全关）
std::shared_ptr<const EngineConfig> defaultEngineConfig()
{
	auto config = std::make_shared<EngineConfig>();
	config->fuzzy_choices.assign(19, 0);
	return config;
}

}

ConfigSnapshot::ConfigSnapshot()
	: m_config(defaultEngineConfig())
{
}

void ConfigSnapshot::update(std::shared_ptr<const EngineConfig> config)
{
	std::atomic_store(&m_config, std::move(config));
}

bool ConfigSnapshot::valid() const
{
	return m_config != nullptr;
}

bool ConfigSnapshot::smart_prediction() const
{
    return std::atomic_load(&m_config)->smart_prediction;
}

bool ConfigSnapshot::isUMode() const
{
	return std::atomic_load(&m_config)->isUMode;
}

bool ConfigSnapshot::isVMode() const
{
	return std::atomic_load(&m_config)->isVMode;
}

bool ConfigSnapshot::pinyin_correction() const
{
	return std::atomic_load(&m_config)->pinyin_correction;
}

std::vector<int> ConfigSnapshot::fuzzy_choices() const
{
	return std::atomic_load(&m_config)->fuzzy_choices;
}

int ConfigSnapshot::limit_count() const
{
	return std::atomic_load(&m_config)->limit_count;
}

bool ConfigSnapshot::isShowInfos() const
{
	return std::atomic_load(&m_config)->isShowInfos;
}

int ConfigSnapshot::combined_lexicon_version() const
{
	return std::atomic_load(&m_config)->combined_lexicon_version;
}

} // 命名空间 coral 结束

#include "pch.h"
#include "runtime_snapshot.h"
#include <stdexcept>

namespace coral {
namespace {
thread_local ScopedConfigOverride* active_override = nullptr;

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

std::shared_ptr<const EngineConfig> ConfigSnapshot::current_config() const
{
    for (auto* scope = active_override; scope; scope = scope->previous_) {
        if (scope->owner_ == this) return scope->config_;
    }
    return std::atomic_load(&m_config);
}

ScopedConfigOverride::ScopedConfigOverride(const ConfigSnapshot& owner, std::shared_ptr<const EngineConfig> config)
    : owner_(&owner), config_(std::move(config)), previous_(active_override)
{
    if (!config_) throw std::invalid_argument("session configuration must not be null");
    active_override = this;
}

ScopedConfigOverride::~ScopedConfigOverride()
{
    active_override = previous_;
}

bool ConfigSnapshot::valid() const
{
	return current_config() != nullptr;
}

bool ConfigSnapshot::smart_prediction() const
{
    return current_config()->smart_prediction;
}

bool ConfigSnapshot::isUMode() const
{
	return current_config()->isUMode;
}

bool ConfigSnapshot::isVMode() const
{
	return current_config()->isVMode;
}

bool ConfigSnapshot::pinyin_correction() const
{
	return current_config()->pinyin_correction;
}

std::vector<int> ConfigSnapshot::fuzzy_choices() const
{
	return current_config()->fuzzy_choices;
}

int ConfigSnapshot::limit_count() const
{
	return current_config()->limit_count;
}

bool ConfigSnapshot::isShowInfos() const
{
	return current_config()->isShowInfos;
}

int ConfigSnapshot::combined_lexicon_version() const
{
	return current_config()->combined_lexicon_version;
}

} // 命名空间 coral 结束

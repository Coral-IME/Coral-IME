// EngineOptions：引擎初始化所需的全部路径与开关（Phase 3）。
// 平台相关的路径解析（注册表安装目录、LocalLow、环境变量覆盖）由调用方
// 在平台层完成后传入；本结构与 Engine 保持平台无关。
#pragma once

#include <filesystem>

namespace coral {

struct EngineOptions
{
	// 系统数据根（含 data/ 与 data/generallexicon/ 的运行资源目录）
	std::filesystem::path dataDir;
	// 用户配置 ini（coralpinyinConfig.ini）完整路径
	std::filesystem::path userConfigIni;
	// 用户词典文件完整路径
	std::filesystem::path userLexiconFile;
	// true：同步加载词库；false：允许后台加载
	bool syncLoad = false;
	// true：词库装载失败后在每次解析入口重试直至成功（典型场景：UWP 服务模式下
	// 宿主只开共享内存，服务就绪前词库装载失败）。由平台边界依据运行模式填入，
	// 引擎核心只消费该标志，不感知 UWP。
	bool retryModelLoadUntilSuccess = false;
};

} // 命名空间 coral 结束

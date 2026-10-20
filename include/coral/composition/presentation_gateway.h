#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace coral::composition
{

using CandidateId = std::uint64_t;

struct RenderContextToken
{
	std::uint64_t serviceInstanceId = 0;
	std::uint64_t contextId = 0;
	std::uint64_t contextGeneration = 0;
};

struct RenderCandidate
{
	CandidateId id = 0;
	std::string text;
	std::string pinyin;
	bool canDelete = false; // 由上屏文本计算，可能与 text 不同。
};

struct CandidateRenderSnapshot
{
	RenderContextToken context;
	std::uint64_t version = 0;
	bool visible = false;
	std::int32_t anchorLeft = 0;
	std::int32_t anchorTop = 0;
	std::int32_t anchorRight = 0;
	std::int32_t anchorBottom = 0;
	std::uint32_t selection = 0;
	std::uint32_t pageSize = 1;
	bool expanded = false;
	bool hasMore = false;
	std::int32_t fontSize = 16;
	std::string fontName;
	std::string theme;
	// 序列化的 CandidateThemeConfig（"candidateSkin" 配置值）。
	std::string skin;
	// 背景图像解析后的 UTF-8 路径；未启用图像皮肤时为空。
	std::string skinImage;
	std::vector<RenderCandidate> candidates;
    std::string hint; // 信息性文本，绝不是可选中的候选。
    bool letterSelection = false;
	// 每行一个候选且无索引标签（剪贴板历史）。
	bool listLayout = false;
};

struct ToolbarRenderSnapshot
{
	RenderContextToken context;
	std::uint64_t version = 0;
	bool visible = false;
	bool ascii = false;
	bool capsLock = false;
	bool full_width = false;
	bool englishPunctuation = false;
	bool traditional = false;
	std::string theme;
};

enum class RendererActionType : std::uint32_t
{
	None = 0,
	select_candidate = 1,
	PreviousPage = 2,
	NextPage = 3,
	ToggleAscii = 4,
	TogglePunctuation = 5,
	ToggleFullWidth = 6,
	ToggleCharacterSet = 7,
	PinCandidate = 8,
	DeleteCandidate = 9,
	AddPhrase = 10,
	ToggleExpansion = 11,
	RequestPrefetch = 12,
	CandidateLayout = 13,
	OpenSettingsMenu = 14,
};

struct RendererAction
{
	RendererActionType type = RendererActionType::None;
	RenderContextToken context;
	std::uint64_t snapshotVersion = 0;
	CandidateId candidateId = 0;
	std::uint32_t candidateIndex = 0;
	std::vector<std::uint32_t> rowStarts;
	std::vector<std::uint32_t> itemCenters;
};

class RendererPort
{
public:
	using ActionHandler = std::function<void(const RendererAction&)>;
	virtual ~RendererPort() = default;
	virtual bool available() const = 0;
	virtual void SetActionHandler(ActionHandler handler) = 0;
	virtual void PublishCandidate(CandidateRenderSnapshot snapshot) = 0;
	virtual void PublishToolbar(ToolbarRenderSnapshot snapshot) = 0;
	virtual void HideContext(RenderContextToken context, std::uint64_t version) = 0;
	virtual void ClearClient() = 0;
};

} // 命名空间 coral::composition 结束

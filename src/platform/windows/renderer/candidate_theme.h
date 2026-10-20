#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace coral::win32
{
// 颜色为直通（非预乘）0xAARRGGBB。
struct CandidatePalette
{
	std::uint32_t text = 0;
	std::uint32_t selectedText = 0;
	std::uint32_t background = 0;
	std::uint32_t selectedBackground = 0;
};

enum class CandidateBackground { Solid, Gradient, Image };

// 每个预设都带浅色与深色两套变体，以 DarkTheme() 索引。
struct CandidateSolidPreset
{
	const wchar_t* name;
	CandidatePalette palette[2];
};

struct CandidateGradientPreset
{
	const wchar_t* name;
	std::uint32_t from[2];
	std::uint32_t to[2];
	CandidatePalette palette[2];
};

struct CandidateImagePreset
{
	const wchar_t* name;
	const wchar_t* file;
	CandidatePalette palette[2];
};

constexpr int kCandidateSolidPresetCount = 6;
// 新预设只能追加，使已持久化的数字选择保持原有含义。
constexpr int kCandidateDefaultSolidPreset = 5;
constexpr int kCandidateGradientPresetCount = 4;
constexpr int kCandidateImagePresetCount = 3;
const CandidateSolidPreset& CandidateSolidPresetAt(int index);
const CandidateGradientPreset& CandidateGradientPresetAt(int index);
const CandidateImagePreset& CandidateImagePresetAt(int index);

constexpr std::uint64_t kCandidateImageMaxBytes = 10ull * 1024 * 1024;
constexpr int kCandidateOpacityMin = 30;

// 用户的皮肤选择，持久化为 "candidateSkin" 配置值。序列化形式
// 必须保持在 256 字节共享内存值上限以内，因此图像路径
// 从固定位置解析而不直接存储。
struct CandidateThemeConfig
{
	CandidateBackground kind = CandidateBackground::Solid;
	int solid = kCandidateDefaultSolidPreset;
	// kCandidateGradientPresetCount / kCandidateImagePresetCount 选中自定义项。
	int gradient = 0;
	int image = 0;
	bool customPalette = false;
	CandidatePalette palette[2];
	bool customGradientSet = false;
	std::uint32_t gradientFrom[2]{};
	std::uint32_t gradientTo[2]{};
	// 仅背景强度；文字、图标和滚动条不受影响。
	int opacity = 100;
	// 在不透明面板色之上的图像强度；不会淡化候选文字。
	int imageOpacity = 100;
	// 导入的自定义图像的扩展名（"png" 或 "jpg"）。
	std::string imageExtension;
	// 自定义图像文件被替换时随之变化。
	std::uint32_t imageRevision = 0;

	static CandidateThemeConfig parse(const std::string& value);
	std::string serialize() const;

	bool customGradient() const { return gradient >= kCandidateGradientPresetCount; }
	bool customImage() const { return image >= kCandidateImagePresetCount; }
	// 当前主题使用的调色板，在任何自定义覆盖之前。
	CandidatePalette themePalette(bool dark) const;
	CandidatePalette effectivePalette(bool dark) const;
	void gradientColors(bool dark, std::uint32_t& from, std::uint32_t& to) const;
	// 未选择图像背景时为空。
	std::wstring imagePath(const std::wstring& installDir, const std::wstring& userDir) const;
};

std::wstring CandidateBuiltinImagePath(const std::wstring& installDir, int index);
std::wstring CandidateCustomImagePath(const std::wstring& userDir, const std::string& extension);

// 已解码图像，不透明 0xFFRRGGBB 像素，自顶向下。
struct SkinImage
{
	int width = 0;
	int height = 0;
	// 大图在解码时被缩小之前，原始文件的大小。
	int sourceWidth = 0;
	int sourceHeight = 0;
	std::vector<std::uint32_t> pixels;
	std::uint32_t average = 0xFFFFFFFF;
};

// 通过 WIC 解码 JPG/PNG。结果按路径缓存，并按文件的
// 大小与写入时间重新校验，替换文件后会重新加载。
std::shared_ptr<const SkinImage> LoadSkinImage(const std::wstring& path);
std::shared_ptr<const SkinImage> DecodeSkinImage(const std::wstring& path);
// 把图像居中裁剪以填充 width x height（CSS object-fit: cover）。
std::vector<std::uint32_t> CoverSkinImage(const SkinImage& image, int width, int height);
// 从图像中裁出 x, y, width, height（图像像素）并缩放到 outWidth x outHeight。
std::shared_ptr<SkinImage> CropSkinImage(const SkinImage& image, int x, int y, int width,
	int height, int outWidth, int outHeight);
bool SaveSkinImagePng(const SkinImage& image, const std::wstring& path);

// 把图像混合到不透明面板之上，保持文字/背景 alpha 不变。
std::uint32_t BlendCandidateImage(std::uint32_t pixel, std::uint32_t background, int opacity);

std::wstring Utf8ToWide(const std::string& value);
std::string WideToUtf8(const std::wstring& value);
} // 命名空间 coral::win32 结束

#include "candidate_theme.h"
#include "soft_canvas.h"

#include <windows.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <mutex>

#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "ole32.lib")

namespace coral::win32
{
namespace
{
using Microsoft::WRL::ComPtr;
using soft::Lerp;

// 既有索引保持原有的浅色/深色配色；珊瑚色追加在后。
const CandidateSolidPreset kSolidPresets[kCandidateSolidPresetCount] = {
	{ L"浅天蓝", {
		{ 0xFF1F2329, 0xFF275F9E, 0xFFFCFCFD, 0xFFE6EEFB },
		{ 0xFFE8E9EC, 0xFFB8D2FF, 0xFF2B2C30, 0xFF2E4266 } } },
	{ L"群青蓝", {
		{ 0xFF1C2541, 0xFFFFFFFF, 0xFFF5F7FF, 0xFF2D4FD1 },
		{ 0xFFDCE3FA, 0xFFFFFFFF, 0xFF161D38, 0xFF3757D6 } } },
	{ L"淡粉色", {
		{ 0xFF4A2B36, 0xFFB03462, 0xFFFFF4F7, 0xFFFAD6E2 },
		{ 0xFFF4DDE4, 0xFFFFD4E2, 0xFF33242A, 0xFF6B3A4D } } },
	{ L"玄黑", {
		{ 0xFF1A1A1A, 0xFFFFFFFF, 0xFFF7F7F6, 0xFF1C1C1E },
		{ 0xFFE8E8E8, 0xFF111111, 0xFF0F0F10, 0xFFEDEDED } } },
	{ L"绿色", {
		{ 0xFF1E3A2B, 0xFFFFFFFF, 0xFFF3FAF5, 0xFF2E9D5B },
		{ 0xFFDCEFE3, 0xFFFFFFFF, 0xFF18271F, 0xFF2F8A55 } } },
	{ L"珊瑚橘", {
		{ 0xFF4A3027, 0xFFA64224, 0xFFFFFFFF, 0xFFFFE1D1 },
		{ 0xFFF4DED3, 0xFFFFC4A5, 0xFF30231E, 0xFF68402D } } },
};

const CandidateGradientPreset kGradientPresets[kCandidateGradientPresetCount] = {
	{ L"晨曦", { 0xFFFFE8D6, 0xFF3B2A2A }, { 0xFFFBD6EA, 0xFF3A2539 }, {
		{ 0xFF4A2E2A, 0xFFFFFFFF, 0xFFFDDFE0, 0xFFE0708A },
		{ 0xFFF5E1DC, 0xFFFFFFFF, 0xFF3A2830, 0xFFC4566F } } },
	{ L"海雾", { 0xFFD8EEFF, 0xFF18263D }, { 0xFFE4E2FF, 0xFF26213F }, {
		{ 0xFF1D2B45, 0xFFFFFFFF, 0xFFDEE8FF, 0xFF3D6FD8 },
		{ 0xFFDCE6F7, 0xFFFFFFFF, 0xFF1F233E, 0xFF3F6FD0 } } },
	{ L"薄荷", { 0xFFDDF6EA, 0xFF15302A }, { 0xFFEEF7D6, 0xFF25331C }, {
		{ 0xFF1D3A2C, 0xFFFFFFFF, 0xFFE5F6E0, 0xFF2E9D63 },
		{ 0xFFD8F0E2, 0xFFFFFFFF, 0xFF1D3123, 0xFF2F8A58 } } },
	{ L"暮紫", { 0xFFE9DDFB, 0xFF2A2140 }, { 0xFFFCDDEB, 0xFF3A2233 }, {
		{ 0xFF2E2445, 0xFFFFFFFF, 0xFFF2DDF3, 0xFF8357D6 },
		{ 0xFFE9E0F7, 0xFFFFFFFF, 0xFF32213A, 0xFF7A52C8 } } },
};

const CandidateImagePreset kImagePresets[kCandidateImagePresetCount] = {
	{ L"云岚", L"candidate_bg_1.jpg", {
		{ 0xFF1E2A3A, 0xFFFFFFFF, 0xFFEFF4FA, 0xFF3A6FB7 },
		{ 0xFFE8EEF6, 0xFFFFFFFF, 0xFF1D232B, 0xFF3A6FB7 } } },
	{ L"樱语", L"candidate_bg_2.jpg", {
		{ 0xFF4A2B36, 0xFFFFFFFF, 0xFFFDEEF2, 0xFFD0587F },
		{ 0xFFF6E3E9, 0xFFFFFFFF, 0xFF2B2226, 0xFFC04F75 } } },
	{ L"竹影", L"candidate_bg_3.jpg", {
		{ 0xFF1F3A2B, 0xFFFFFFFF, 0xFFEFF7F0, 0xFF3A8C5C },
		{ 0xFFE2F1E7, 0xFFFFFFFF, 0xFF1C2620, 0xFF3A8C5C } } },
};

// 自定义渐变与图像没有自己的设计调色板。
constexpr CandidatePalette kCustomPalette[2] = {
	{ 0xFF1F2329, 0xFFFFFFFF, 0xFFFCFCFD, 0xFF3A6FD0 },
	{ 0xFFEDEEF0, 0xFFFFFFFF, 0xFF26272B, 0xFF3A6FD0 } };

std::string Hex(std::uint32_t color)
{
	char buffer[8];
	std::snprintf(buffer, sizeof(buffer), "%06X", color & 0x00FFFFFFu);
	return buffer;
}

std::vector<std::uint32_t> ParseColors(const std::string& value)
{
	std::vector<std::uint32_t> colors;
	std::size_t start = 0;
	while (start <= value.size())
	{
		const std::size_t end = (std::min)(value.find('.', start), value.size());
		colors.push_back(0xFF000000u |
			static_cast<std::uint32_t>(std::strtoul(value.substr(start, end - start).c_str(), nullptr, 16)));
		start = end + 1;
	}
	return colors;
}

int ParseInt(const std::string& value, int minimum, int maximum)
{
	return (std::clamp)(std::atoi(value.c_str()), minimum, maximum);
}

std::uint32_t LuminanceText(std::uint32_t background)
{
	const float r = static_cast<float>((background >> 16) & 0xFF);
	const float g = static_cast<float>((background >> 8) & 0xFF);
	const float b = static_cast<float>(background & 0xFF);
	return (0.299f * r + 0.587f * g + 0.114f * b) > 150.0f ? 0xFF1F2329 : 0xFFEDEEF0;
}

std::uint32_t AverageColor(const std::vector<std::uint32_t>& pixels)
{
	if (pixels.empty()) return 0xFFFFFFFF;
	double sum[3]{};
	for (const std::uint32_t pixel : pixels)
	{
		sum[0] += (pixel >> 16) & 0xFF;
		sum[1] += (pixel >> 8) & 0xFF;
		sum[2] += pixel & 0xFF;
	}
	const double count = static_cast<double>(pixels.size());
	return 0xFF000000u |
		(static_cast<std::uint32_t>(sum[0] / count) << 16) |
		(static_cast<std::uint32_t>(sum[1] / count) << 8) |
		static_cast<std::uint32_t>(sum[2] / count);
}

struct CacheEntry
{
	ULONGLONG size = 0;
	FILETIME written{};
	std::shared_ptr<const SkinImage> image;
};
std::mutex g_cacheMutex;
std::map<std::wstring, CacheEntry> g_cache;
} // namespace

const CandidateSolidPreset& CandidateSolidPresetAt(int index)
{
	return kSolidPresets[(std::clamp)(index, 0, kCandidateSolidPresetCount - 1)];
}

const CandidateGradientPreset& CandidateGradientPresetAt(int index)
{
	return kGradientPresets[(std::clamp)(index, 0, kCandidateGradientPresetCount - 1)];
}

const CandidateImagePreset& CandidateImagePresetAt(int index)
{
	return kImagePresets[(std::clamp)(index, 0, kCandidateImagePresetCount - 1)];
}

CandidateThemeConfig CandidateThemeConfig::parse(const std::string& value)
{
	CandidateThemeConfig config;
	std::size_t start = 0;
	while (start < value.size())
	{
		const std::size_t end = (std::min)(value.find('|', start), value.size());
		const std::string token = value.substr(start, end - start);
		start = end + 1;
		const std::size_t colon = token.find(':');
		if (colon == std::string::npos) continue;
		const std::string key = token.substr(0, colon);
		const std::string field = token.substr(colon + 1);
		if (key == "k")
			config.kind = field == "g" ? CandidateBackground::Gradient :
				field == "i" ? CandidateBackground::Image : CandidateBackground::Solid;
		else if (key == "s") config.solid = ParseInt(field, 0, kCandidateSolidPresetCount - 1);
		else if (key == "g") config.gradient = ParseInt(field, 0, kCandidateGradientPresetCount);
		else if (key == "i") config.image = ParseInt(field, 0, kCandidateImagePresetCount);
		else if (key == "o") config.opacity = ParseInt(field, kCandidateOpacityMin, 100);
		else if (key == "a") config.imageOpacity = ParseInt(field, 0, 100);
		else if (key == "c") config.customPalette = field == "1";
		else if (key == "x") config.imageExtension = field == "png" ? "png" : field == "jpg" ? "jpg" : "";
		else if (key == "r") config.imageRevision = static_cast<std::uint32_t>(std::strtoul(field.c_str(), nullptr, 10));
		else if (key == "p")
		{
			const std::vector<std::uint32_t> colors = ParseColors(field);
			if (colors.size() != 8) continue;
			for (int mode = 0; mode < 2; ++mode)
				config.palette[mode] = { colors[mode * 4], colors[mode * 4 + 1],
					colors[mode * 4 + 2], colors[mode * 4 + 3] };
		}
		else if (key == "q")
		{
			const std::vector<std::uint32_t> colors = ParseColors(field);
			if (colors.size() != 4) continue;
			config.customGradientSet = true;
			config.gradientFrom[0] = colors[0];
			config.gradientTo[0] = colors[1];
			config.gradientFrom[1] = colors[2];
			config.gradientTo[1] = colors[3];
		}
	}
	return config;
}

std::uint32_t BlendCandidateImage(std::uint32_t pixel, std::uint32_t background, int opacity)
{
	const unsigned strength = static_cast<unsigned>((std::clamp)(opacity, 0, 100));
	std::uint32_t result = 0xFF000000;
	for (const int shift : { 0, 8, 16 }) {
		const unsigned imageChannel = (pixel >> shift) & 0xFF;
		const unsigned baseChannel = (background >> shift) & 0xFF;
		result |= ((imageChannel * strength + baseChannel * (100 - strength) + 50) / 100) << shift;
	}
	return result;
}


std::string CandidateThemeConfig::serialize() const
{
	std::string out = "k:";
	out += kind == CandidateBackground::Gradient ? "g" : kind == CandidateBackground::Image ? "i" : "s";
	out += "|s:" + std::to_string(solid) + "|g:" + std::to_string(gradient) +
		"|i:" + std::to_string(image) + "|o:" + std::to_string(opacity);
	if (imageOpacity != 100) out += "|a:" + std::to_string((std::clamp)(imageOpacity, 0, 100));
	if (customPalette)
	{
		out += "|c:1|p:";
		for (int mode = 0; mode < 2; ++mode)
		{
			const CandidatePalette& p = palette[mode];
			out += (mode ? "." : "") + Hex(p.text) + "." + Hex(p.selectedText) + "." +
				Hex(p.background) + "." + Hex(p.selectedBackground);
		}
	}
	if (customGradientSet)
		out += "|q:" + Hex(gradientFrom[0]) + "." + Hex(gradientTo[0]) + "." +
			Hex(gradientFrom[1]) + "." + Hex(gradientTo[1]);
	if (!imageExtension.empty())
		out += "|x:" + imageExtension + "|r:" + std::to_string(imageRevision);
	return out;
}

CandidatePalette CandidateThemeConfig::themePalette(bool dark) const
{
	const int mode = dark ? 1 : 0;
	switch (kind)
	{
	case CandidateBackground::Gradient:
		if (customGradient())
		{
			std::uint32_t from = 0, to = 0;
			gradientColors(dark, from, to);
			CandidatePalette result = kCustomPalette[mode];
			result.background = Lerp(from, to, 0.5f);
			result.text = LuminanceText(result.background);
			return result;
		}
		return CandidateGradientPresetAt(gradient).palette[mode];
	case CandidateBackground::Image:
		return customImage() ? kCustomPalette[mode] : CandidateImagePresetAt(image).palette[mode];
	default:
		return CandidateSolidPresetAt(solid).palette[mode];
	}
}

CandidatePalette CandidateThemeConfig::effectivePalette(bool dark) const
{
	return customPalette ? palette[dark ? 1 : 0] : themePalette(dark);
}

void CandidateThemeConfig::gradientColors(bool dark, std::uint32_t& from, std::uint32_t& to) const
{
	const int mode = dark ? 1 : 0;
	if (customGradient() && customGradientSet)
	{
		from = gradientFrom[mode];
		to = gradientTo[mode];
		return;
	}
	const CandidateGradientPreset& preset = CandidateGradientPresetAt(customGradient() ? 1 : gradient);
	from = preset.from[mode];
	to = preset.to[mode];
}

std::wstring CandidateThemeConfig::imagePath(const std::wstring& installDir,
	const std::wstring& userDir) const
{
	if (kind != CandidateBackground::Image) return {};
	if (!customImage()) return CandidateBuiltinImagePath(installDir, image);
	return imageExtension.empty() ? std::wstring() : CandidateCustomImagePath(userDir, imageExtension);
}

std::wstring CandidateBuiltinImagePath(const std::wstring& installDir, int index)
{
	return installDir + L"\\res\\skins\\" + CandidateImagePresetAt(index).file;
}

std::wstring CandidateCustomImagePath(const std::wstring& userDir, const std::string& extension)
{
	return userDir + L"\\skin\\candidate_custom." + Utf8ToWide(extension);
}

std::shared_ptr<const SkinImage> DecodeSkinImage(const std::wstring& path)
{
	ComPtr<IWICImagingFactory> factory;
	HRESULT hr = ::CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
		IID_PPV_ARGS(&factory));
	if (hr == CO_E_NOTINITIALIZED)
	{
		// 渲染器的 UI 线程尚未有 COM 套间；由它保留这一个。
		::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
		hr = ::CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
			IID_PPV_ARGS(&factory));
	}
	if (FAILED(hr)) return nullptr;
	ComPtr<IWICBitmapDecoder> decoder;
	ComPtr<IWICBitmapFrameDecode> frame;
	if (FAILED(factory->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ,
		WICDecodeMetadataCacheOnDemand, &decoder)) || FAILED(decoder->GetFrame(0, &frame)))
		return nullptr;
	UINT width = 0, height = 0;
	if (FAILED(frame->GetSize(&width, &height)) || !width || !height) return nullptr;
	const UINT sourceWidth = width;
	const UINT sourceHeight = height;
	ComPtr<IWICBitmapSource> source = frame;
	// 大照片提前缩小：面板永远用不到更大的尺寸。
	constexpr UINT kMaxSide = 2400;
	if (width > kMaxSide || height > kMaxSide)
	{
		const double factor = (std::min)(static_cast<double>(kMaxSide) / width,
			static_cast<double>(kMaxSide) / height);
		const UINT scaledWidth = (std::max)(1u, static_cast<UINT>(width * factor));
		const UINT scaledHeight = (std::max)(1u, static_cast<UINT>(height * factor));
		ComPtr<IWICBitmapScaler> scaler;
		if (FAILED(factory->CreateBitmapScaler(&scaler)) ||
			FAILED(scaler->Initialize(frame.Get(), scaledWidth, scaledHeight,
				WICBitmapInterpolationModeFant)))
			return nullptr;
		source = scaler;
		width = scaledWidth;
		height = scaledHeight;
	}
	ComPtr<IWICFormatConverter> converter;
	if (FAILED(factory->CreateFormatConverter(&converter)) ||
		FAILED(converter->Initialize(source.Get(), GUID_WICPixelFormat32bppBGRA,
			WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom)))
		return nullptr;
	auto image = std::make_shared<SkinImage>();
	image->width = static_cast<int>(width);
	image->height = static_cast<int>(height);
	image->sourceWidth = static_cast<int>(sourceWidth);
	image->sourceHeight = static_cast<int>(sourceHeight);
	image->pixels.resize(static_cast<std::size_t>(width) * height);
	if (FAILED(converter->CopyPixels(nullptr, width * 4,
		static_cast<UINT>(image->pixels.size() * 4), reinterpret_cast<BYTE*>(image->pixels.data()))))
		return nullptr;
	// PNG 的透明区域被压平到白色上：面板必须保持不透明。
	for (std::uint32_t& pixel : image->pixels)
	{
		const std::uint32_t a = pixel >> 24;
		const auto channel = [&](int shift) {
			return (((pixel >> shift) & 0xFF) * a + 255 * (255 - a) + 127) / 255;
		};
		pixel = 0xFF000000u | (channel(16) << 16) | (channel(8) << 8) | channel(0);
	}
	image->average = AverageColor(image->pixels);
	return image;
}

std::shared_ptr<SkinImage> CropSkinImage(const SkinImage& image, int x, int y, int width,
	int height, int outWidth, int outHeight)
{
	x = (std::clamp)(x, 0, (std::max)(0, image.width - 1));
	y = (std::clamp)(y, 0, (std::max)(0, image.height - 1));
	width = (std::clamp)(width, 1, image.width - x);
	height = (std::clamp)(height, 1, image.height - y);
	SkinImage region;
	region.width = width;
	region.height = height;
	region.pixels.resize(static_cast<std::size_t>(width) * height);
	for (int row = 0; row < height; ++row)
		std::copy_n(image.pixels.begin() + static_cast<std::ptrdiff_t>(y + row) * image.width + x,
			width, region.pixels.begin() + static_cast<std::ptrdiff_t>(row) * width);
	auto result = std::make_shared<SkinImage>();
	result->width = (std::max)(1, outWidth);
	result->height = (std::max)(1, outHeight);
	result->sourceWidth = result->width;
	result->sourceHeight = result->height;
	result->pixels = CoverSkinImage(region, result->width, result->height);
	result->average = AverageColor(result->pixels);
	return result;
}

bool SaveSkinImagePng(const SkinImage& image, const std::wstring& path)
{
	if (image.width <= 0 || image.height <= 0) return false;
	ComPtr<IWICImagingFactory> factory;
	if (FAILED(::CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
		IID_PPV_ARGS(&factory)))) return false;
	ComPtr<IWICStream> stream;
	ComPtr<IWICBitmapEncoder> encoder;
	ComPtr<IWICBitmapFrameEncode> frame;
	if (FAILED(factory->CreateStream(&stream)) ||
		FAILED(stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE)) ||
		FAILED(factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder)) ||
		FAILED(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache)) ||
		FAILED(encoder->CreateNewFrame(&frame, nullptr)) ||
		FAILED(frame->Initialize(nullptr)) ||
		FAILED(frame->SetSize(static_cast<UINT>(image.width), static_cast<UINT>(image.height))))
		return false;
	WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;
	if (FAILED(frame->SetPixelFormat(&format))) return false;
	std::vector<std::uint32_t> pixels = image.pixels;
	if (format != GUID_WICPixelFormat32bppBGRA)
	{
		// PNG 编码器可能只提供 24bpp；像素本来就不透明。
		std::vector<std::uint8_t> packed(static_cast<std::size_t>(image.width) * image.height * 3);
		for (std::size_t i = 0; i < pixels.size(); ++i)
		{
			packed[i * 3] = static_cast<std::uint8_t>(pixels[i]);
			packed[i * 3 + 1] = static_cast<std::uint8_t>(pixels[i] >> 8);
			packed[i * 3 + 2] = static_cast<std::uint8_t>(pixels[i] >> 16);
		}
		if (FAILED(frame->WritePixels(static_cast<UINT>(image.height), static_cast<UINT>(image.width) * 3,
			static_cast<UINT>(packed.size()), packed.data()))) return false;
	}
	else if (FAILED(frame->WritePixels(static_cast<UINT>(image.height), static_cast<UINT>(image.width) * 4,
		static_cast<UINT>(pixels.size() * 4), reinterpret_cast<BYTE*>(pixels.data()))))
		return false;
	return SUCCEEDED(frame->Commit()) && SUCCEEDED(encoder->Commit());
}

std::shared_ptr<const SkinImage> LoadSkinImage(const std::wstring& path)
{
	if (path.empty()) return nullptr;
	WIN32_FILE_ATTRIBUTE_DATA data{};
	if (!::GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data)) return nullptr;
	const ULONGLONG size = (static_cast<ULONGLONG>(data.nFileSizeHigh) << 32) | data.nFileSizeLow;
	if (size > kCandidateImageMaxBytes) return nullptr;
	{
		std::lock_guard<std::mutex> lock(g_cacheMutex);
		auto it = g_cache.find(path);
		if (it != g_cache.end() && it->second.size == size &&
			::CompareFileTime(&it->second.written, &data.ftLastWriteTime) == 0)
			return it->second.image;
	}
	std::shared_ptr<const SkinImage> image = DecodeSkinImage(path);
	std::lock_guard<std::mutex> lock(g_cacheMutex);
	if (g_cache.size() >= 8) g_cache.clear();
	g_cache[path] = { size, data.ftLastWriteTime, image };
	return image;
}

std::vector<std::uint32_t> CoverSkinImage(const SkinImage& image, int width, int height)
{
	std::vector<std::uint32_t> out(static_cast<std::size_t>((std::max)(0, width)) *
		(std::max)(0, height), image.average);
	if (width <= 0 || height <= 0 || image.width <= 0 || image.height <= 0) return out;
	const double scale = (std::max)(static_cast<double>(width) / image.width,
		static_cast<double>(height) / image.height);
	const double offsetX = (image.width - width / scale) * 0.5;
	const double offsetY = (image.height - height / scale) * 0.5;
	const auto at = [&image](int x, int y) {
		return image.pixels[static_cast<std::size_t>((std::clamp)(y, 0, image.height - 1)) * image.width +
			(std::clamp)(x, 0, image.width - 1)];
	};
	for (int y = 0; y < height; ++y)
	{
		const double sy0 = offsetY + y / scale;
		const double sy1 = offsetY + (y + 1) / scale;
		for (int x = 0; x < width; ++x)
		{
			const double sx0 = offsetX + x / scale;
			const double sx1 = offsetX + (x + 1) / scale;
			std::uint32_t pixel = 0;
			if (scale < 0.75)
			{
				// 缩小尺寸：对覆盖到的源块取平均。
				const int x0 = static_cast<int>(sx0), x1 = (std::max)(x0 + 1, static_cast<int>(sx1));
				const int y0 = static_cast<int>(sy0), y1 = (std::max)(y0 + 1, static_cast<int>(sy1));
				std::uint32_t r = 0, g = 0, b = 0, n = 0;
				for (int yy = y0; yy < y1; ++yy)
					for (int xx = x0; xx < x1; ++xx)
					{
						const std::uint32_t p = at(xx, yy);
						r += (p >> 16) & 0xFF;
						g += (p >> 8) & 0xFF;
						b += p & 0xFF;
						++n;
					}
				pixel = 0xFF000000u | ((r / n) << 16) | ((g / n) << 8) | (b / n);
			}
			else
			{
				const double fx = (sx0 + sx1) * 0.5 - 0.5;
				const double fy = (sy0 + sy1) * 0.5 - 0.5;
				const int ix = static_cast<int>(std::floor(fx));
				const int iy = static_cast<int>(std::floor(fy));
				const float tx = static_cast<float>(fx - ix);
				const float ty = static_cast<float>(fy - iy);
				const std::uint32_t top = Lerp(at(ix, iy), at(ix + 1, iy), tx);
				const std::uint32_t bottom = Lerp(at(ix, iy + 1), at(ix + 1, iy + 1), tx);
				pixel = Lerp(top, bottom, ty);
			}
			out[static_cast<std::size_t>(y) * width + x] = pixel;
		}
	}
	return out;
}

std::wstring Utf8ToWide(const std::string& value)
{
	if (value.empty()) return {};
	const int length = ::MultiByteToWideChar(CP_UTF8, 0, value.data(),
		static_cast<int>(value.size()), nullptr, 0);
	if (length <= 0) return {};
	std::wstring result(static_cast<std::size_t>(length), L'\0');
	::MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
		result.data(), length);
	return result;
}

std::string WideToUtf8(const std::wstring& value)
{
	if (value.empty()) return {};
	const int length = ::WideCharToMultiByte(CP_UTF8, 0, value.data(),
		static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
	if (length <= 0) return {};
	std::string result(static_cast<std::size_t>(length), '\0');
	::WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
		result.data(), length, nullptr, nullptr);
	return result;
}
} // 命名空间 coral::win32 结束

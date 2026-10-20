// 演练生产裁剪对话框、渲染与主题序列化。
#include "../../src/platform/windows/coralpinyin/src/ui/skincropdialog.cpp"
#include <filesystem>
#include <iostream>
#include <stdexcept>

void Require(bool ok, const char* message) {
    if (!ok) throw std::runtime_error(message);
}
void SavePreview(const Dialog& d, const std::filesystem::path& path) {
    Canvas canvas(kWidth, kHeight);
    Paint(d, canvas.dc());
    GdiFlush();
    SkinImage image;
    image.width = kWidth; image.height = kHeight;
    image.pixels.assign(canvas.pixels(), canvas.pixels() + kWidth * kHeight);
    Require(SaveSkinImagePng(image, path.wstring()), "Could not save dialog rendering");
}
void CheckGlyphWeight(HWND window) {
    CandidateSkin skin;
    skin.panel = skin.selectedFill = 0xFFFFFFFF;
    skin.text = skin.selectedText = 0xFF000000;
    skin.index = skin.selectedIndex = 0xFFFFFFFF;
    skin.border = skin.selectedRing = skin.rowDivider = skin.toggleIcon = 0;
    skin.shadows[0].color = skin.shadows[1].color = 0;
    skin.opacity = 0.65f;
    for (const int size : {14, 16, 20, 24}) {
        coral::composition::CandidateRenderSnapshot snapshot;
        snapshot.visible = snapshot.expanded = true;
        snapshot.fontSize = size;
        snapshot.candidates = {{1, "啤酒", "", false}};
        CandidateView view;
        view.setSkin(skin); view.update(window, snapshot);
        Canvas actual(view.width(), view.height());
        view.draw(actual.dc(), actual.pixels());
        std::uint64_t actual_ink = 0;
        for (std::size_t i = 0; i < actual.count(); ++i)
            actual_ink += (actual.pixels()[i] >> 24) - (actual.pixels()[i] & 255);
        // 独立的原生白底黑字参考渲染，字体度量完全一致。
        Canvas reference(512, 128);
        reference.surface().fill(0xFFFFFFFF);
        ScopedFont font(L"Microsoft YaHei UI", MulDiv(size, GetDpiForWindow(window), 96),
            FW_NORMAL, ANTIALIASED_QUALITY);
        const auto previous = SelectObject(reference.dc(), font.get());
        SetBkMode(reference.dc(), TRANSPARENT);
        SetTextColor(reference.dc(), RGB(0, 0, 0));
        RECT rect{16, 0, 500, 120};
        DrawTextW(reference.dc(), L"啤酒", 2, &rect, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
        SelectObject(reference.dc(), previous);
        GdiFlush();
        std::uint64_t reference_ink = 0;
        for (std::size_t i = 0; i < reference.count(); ++i)
            reference_ink += 255 - (reference.pixels()[i] & 255);
        const double ratio = static_cast<double>(actual_ink) / reference_ink;
        std::cout << "glyph size=" << size << " ink ratio=" << ratio << '\n';
        Require(std::abs(ratio - 1.0) < 0.03,
            "Translucent beer glyphs are thicker than native antialiased text");
        const auto ink_bounds = [](const Canvas& canvas, bool premultiplied) {
            RECT bounds{canvas.width(), canvas.height(), 0, 0};
            for (int y = 0; y < canvas.height(); ++y)
                for (int x = 0; x < canvas.width(); ++x) {
                    const auto pixel = canvas.pixels()[y * canvas.width() + x];
                    const int ink = (premultiplied ? pixel >> 24 : 255) - (pixel & 255);
                    if (!ink) continue;
                    bounds.left = (std::min)(bounds.left, static_cast<LONG>(x));
                    bounds.top = (std::min)(bounds.top, static_cast<LONG>(y));
                    bounds.right = (std::max)(bounds.right, static_cast<LONG>(x + 1));
                    bounds.bottom = (std::max)(bounds.bottom, static_cast<LONG>(y + 1));
                }
            return bounds;
        };
        const auto a = ink_bounds(actual, true), b = ink_bounds(reference, false);
        Require(a.right - a.left == b.right - b.left && a.bottom - a.top == b.bottom - b.top,
            "Translucent glyph outlines differ from native glyphs");
        for (int y = 0; y < a.bottom - a.top; ++y)
            for (int x = 0; x < a.right - a.left; ++x) {
                const auto pixel = actual.pixels()[(a.top + y) * actual.width() + a.left + x];
                const int coverage = (pixel >> 24) - (pixel & 255);
                const int expected = 255 - (reference.pixels()[(b.top + y) * reference.width() + b.left + x] & 255);
                Require(std::abs(coverage - expected) <= 1,
                    "Translucent glyph edge coverage differs from native antialiasing");
            }
    }
}
int main(int argc, char** argv) {
    try {
        Require(SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)), "Cannot initialize image codecs");
        const auto default_theme = CandidateThemeConfig::parse("");
        Require(default_theme.solid == kCandidateDefaultSolidPreset &&
            default_theme.effectivePalette(false).background == 0xFFFFFFFF &&
            default_theme.effectivePalette(false).selectedBackground == 0xFFFFE1D1,
            "Unconfigured candidate theme must use coral orange");
        Require(CandidateThemeConfig::parse("k:s|s:0").effectivePalette(false).selectedBackground == 0xFFE6EEFB &&
            CandidateThemeConfig::parse("k:s|s:1").effectivePalette(false).selectedBackground == 0xFF2D4FD1,
            "Existing sky-blue and ultramarine skin IDs must keep their palettes");
        Require(CandidateThemeConfig::parse(default_theme.serialize()).solid == kCandidateDefaultSolidPreset,
            "Default coral theme did not survive persistence");
        Require(CandidateThemeConfig::parse("k:i|o:65").imageOpacity == 100,
            "Existing skins must keep original image strength");
        auto config = CandidateThemeConfig::parse("k:i|i:3|o:65|a:40|x:png|r:8");
        const auto restored = CandidateThemeConfig::parse(config.serialize());
        Require(restored.imageOpacity == 40 && restored.opacity == 65 && restored.imageRevision == 8,
            "Image and background opacity did not persist independently");
        Require(CandidateThemeConfig::parse("a:200").imageOpacity == 100 &&
            CandidateThemeConfig::parse("a:-5").imageOpacity == 0, "Opacity limits were not enforced");
        Require(BlendCandidateImage(0xFF000000, 0xFFFFFFFF, 100) == 0xFF000000 &&
            BlendCandidateImage(0xFF000000, 0xFFFFFFFF, 0) == 0xFFFFFFFF &&
            BlendCandidateImage(0xFF000000, 0xFFFFFFFF, 50) == 0xFF808080,
            "Image blending changed opacity or failed to lighten a dark image");
        config.customPalette = true; config.customGradientSet = true;
        config.imageRevision = 0xFFFFFFFF;
        Require(config.serialize().size() < 256, "Skin config exceeded shared-memory limit");

        const auto output = std::filesystem::path(argc > 1 ? argv[1] : "opacity-renders");
        std::filesystem::create_directories(output);
        SkinImage image;
        image.width = image.sourceWidth = 592; image.height = image.sourceHeight = 352;
        image.pixels.assign(image.width * image.height, 0xFF13202B);
        image.average = 0xFF13202B;
        Dialog d;
        d.image = &image; d.display = image.pixels;
        d.imageRect = kStage; d.cropX = 45; d.cropY = 35; d.cropWidth = 500;
        d.aspect = 0.5; d.scale = 1; d.background = 0xFFFFFFFF;
        d.previewVeil = 0x26FFFFFF;
        d.outWidth = 1017; d.outHeight = 509;
        d.title = CreateFontW(-18, 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0,
            CLEARTYPE_QUALITY, 0, L"Microsoft YaHei UI");
        d.text = CreateFontW(-13, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0,
            CLEARTYPE_QUALITY, 0, L"Microsoft YaHei UI");
        d.caption = CreateFontW(-12, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0,
            CLEARTYPE_QUALITY, 0, L"Microsoft YaHei UI");
        RegisterDialogClass();
        HWND hwnd = CreateWindowExW(0, kDialogClass, L"Opacity test", WS_POPUP,
            0, 0, kWidth, kHeight, nullptr, nullptr, GetModuleHandleW(nullptr), &d);
        Require(hwnd != nullptr, "Cannot create crop dialog");
        const int trackY = (kOpacityTrack.top + kOpacityTrack.bottom) / 2;
        const auto click = [&](int x) {
            SendMessageW(hwnd, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(x, trackY));
            SendMessageW(hwnd, WM_LBUTTONUP, 0, MAKELPARAM(x, trackY));
        };
        click(kOpacityTrack.left);
        Require(d.imageOpacity == 100 && d.opacityFocus, "Original-image end is incorrect");
        SavePreview(d, output / "original.png");
        click((kOpacityTrack.left + kOpacityTrack.right) / 2);
        Require(d.imageOpacity == 50, "Slider midpoint is incorrect");
        SavePreview(d, output / "half.png");
        SendMessageW(hwnd, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(kOpacityTrack.left, trackY));
        SendMessageW(hwnd, WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(kOpacityTrack.right + 50, trackY));
        SendMessageW(hwnd, WM_LBUTTONUP, 0, MAKELPARAM(kOpacityTrack.right + 50, trackY));
        Require(d.imageOpacity == 0 && d.cropX == 45 && d.cropY == 35 && d.cropWidth == 500,
            "Opacity drag changed the crop or exceeded its range");
        SavePreview(d, output / "transparent.png");
        Require(HitTest(d, POINT{ kOpacityTrack.right, trackY }) == ImageOpacity,
            "Fully transparent slider thumb must remain draggable");
        SendMessageW(hwnd, WM_KEYDOWN, VK_LEFT, 0);
        Require(d.imageOpacity == 1, "Keyboard adjustment failed");
        SendMessageW(hwnd, WM_KEYDOWN, VK_HOME, 0);
        Require(d.imageOpacity == 100, "Home must show original image");
        SendMessageW(hwnd, WM_KEYDOWN, VK_END, 0);
        Require(d.imageOpacity == 0, "End must hide the image");
        SendMessageW(hwnd, WM_KEYDOWN, VK_ESCAPE, 0);
        Require(d.done && !d.accepted, "Cancel must not accept changes");
        d.skin.darkCaption = true; d.skin.surface = 0xFF202020; d.skin.text = 0xFFFFFFFF;
        d.skin.sub = d.skin.muted = 0xFFB0B0B0; d.background = 0xFF141519;
        d.previewVeil = 0x9E141519; d.imageOpacity = 50;
        SavePreview(d, output / "dark-half.png");
        DestroyWindow(hwnd);
        DeleteObject(d.title); DeleteObject(d.text); DeleteObject(d.caption);

        const auto imageFile = output / "source.png";
        Require(SaveSkinImagePng(image, imageFile.wstring()), "Cannot save source image");
        config = CandidateThemeConfig{};
        config.kind = CandidateBackground::Image; config.image = kCandidateImagePresetCount;
        config.imageOpacity = 100;
        const auto full = CandidateSkin::themed(false, config, imageFile.wstring());
        config.imageOpacity = 0;
        const auto clear = CandidateSkin::themed(false, config, imageFile.wstring());
        Require(full.text == clear.text && full.selectedText == clear.selectedText && clear.opacity == 1.0f,
            "Image transparency faded candidate text or window opacity");
        Require(clear.panel != full.panel && (clear.panel >> 24) == 255,
            "Transparent image must reveal an opaque panel");
        hwnd = CreateWindowExW(0, L"STATIC", L"Candidate opacity test", WS_POPUP,
            0, 0, 800, 600, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
        Require(hwnd != nullptr, "Cannot create candidate test host");
        CheckGlyphWeight(hwnd);
        // 渲染生产候选像素：背景 alpha 必须变化，
        // 而完全覆盖的文字/图标像素保持不透明，包括图像之上。
        for (bool dark : { false, true }) {
            for (auto kind : { CandidateBackground::Solid, CandidateBackground::Gradient,
                    CandidateBackground::Image }) {
                config = CandidateThemeConfig{};
                config.kind = kind;
                auto skin = CandidateSkin::themed(dark, config, imageFile.wstring());
                skin.selectionAnimationMs = skin.scrollAnimationMs = 0;
                coral::composition::CandidateRenderSnapshot snapshot;
                snapshot.visible = true; snapshot.expanded = true; snapshot.fontSize = 16;
                for (int i = 0; i < 80; ++i) {
                    coral::composition::RenderCandidate item;
                    item.id = i + 1; item.text = "候选文字";
                    snapshot.candidates.push_back(item);
                }
                CandidateView view;
                const auto render = [&](float opacity) {
                    skin.opacity = opacity;
                    view.setSkin(skin); view.update(hwnd, snapshot);
                    const auto toggle = view.expandRect();
                    view.setPointer({ (toggle.left + toggle.right) / 2,
                        (toggle.top + toggle.bottom) / 2 });
                    view.setScrollDragging(true);
                    Canvas canvas(view.width(), view.height());
                    view.draw(canvas.dc(), canvas.pixels());
                    return std::vector<std::uint32_t>(canvas.pixels(),
                        canvas.pixels() + view.width() * view.height());
                };
                const auto zero = render(0.0f);
                const auto low = render(0.3f);
                const auto mid = render(0.65f);
                const RECT panel = view.panelRect();
                const auto background = static_cast<std::size_t>(panel.top + 2) * view.width() +
                    (panel.left + panel.right) / 2;
                Require((low[background] >> 24) == 77 && (mid[background] >> 24) == 166,
                    "Panel background opacity was not applied to production pixels");
                int opaqueInk = 0;
                for (std::size_t i = 0; i < zero.size(); ++i) {
                    if ((zero[i] >> 24) == 255) {
                        ++opaqueInk;
                        Require(low[i] == zero[i] && mid[i] == zero[i],
                            "Background opacity faded text or button ink");
                    }
                    for (auto pixel : { low[i], mid[i] })
                        Require(((pixel >> 16) & 255) <= (pixel >> 24) &&
                            ((pixel >> 8) & 255) <= (pixel >> 24) &&
                            (pixel & 255) <= (pixel >> 24),
                            "Translucent rendering broke premultiplied alpha");
                }
                Require(opaqueInk > 100, "Candidate text has no opaque glyph interiors");
                // 滚动条的 alpha 必须是其自身 alpha 叠加在半透明面板上。
                const RECT thumb = view.scrollThumb();
                Require(!IsRectEmpty(&thumb), "Test did not expose a scrollbar");
                const auto center = static_cast<std::size_t>((thumb.top + thumb.bottom) / 2) *
                    view.width() + (thumb.left + thumb.right) / 2;
                const unsigned foreground = zero[center] >> 24;
                Require(foreground == (skin.scrollThumbActive >> 24),
                    "Scrollbar did not retain its own opacity");
                Require(std::abs(static_cast<int>(low[center] >> 24) -
                    static_cast<int>(foreground + 77 * (255 - foreground) / 255)) <= 1,
                    "Background opacity scaled the scrollbar");
            }
        }
        DestroyWindow(hwnd);
        std::cout << "[PASS] crop slider drag, keyboard, cancel, preview, independent image opacity and background-only candidate opacity\n";
        CoUninitialize();
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n'; return 1;
    }
}

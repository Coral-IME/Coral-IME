#include "pch.h"
#include "addphrasedialog.h"
#include "phrasecode.h"
#include "settingswindow.h"
#include "soft_canvas.h"
#include "core_operations.h"

#include <dwmapi.h>
#include <cmath>
#include <memory>

using namespace coral::win32::soft;

namespace
{
constexpr int kWidth = 460, kHeight = 222;
constexpr int kMargin = 16, kCardHeight = 130, kRowSplit = 80, kLabelWidth = 72;
constexpr int kButtonWidth = 80, kButtonHeight = 32, kFooter = 58;
constexpr int kMaxContentLength = 1000;
constexpr int ID_CONTENT = 0x2100, ID_CODE = 0x2101;
enum Hit { kNone = -1, kCancel = 0, kSubmit = 1 };

struct Dialog
{
	HWND owner = nullptr;
	HWND content = nullptr;
	HWND code = nullptr;
	HFONT font = nullptr;
	HBRUSH surfaceBrush = nullptr;
	AddPhraseDialog::Request request;
	std::wstring suggested;
	std::wstring error;
	float scale = 1.0f;
	int hover = kNone, pressed = kNone;
	int px(int value) const { return static_cast<int>(std::lround(value * scale)); }
};

Dialog* StateOf(HWND hwnd) { return reinterpret_cast<Dialog*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA)); }

RECT Card(const Dialog& d) { return { d.px(kMargin), d.px(kMargin), d.px(kWidth - kMargin), d.px(kMargin + kCardHeight) }; }
RECT Button(const Dialog& d, int which)
{
	const int right = d.px(kWidth - kMargin) - (which == kCancel ? d.px(kButtonWidth + 12) : 0);
	const int bottom = d.px(kHeight) - d.px((kFooter - kButtonHeight) / 2);
	return { right - d.px(kButtonWidth), bottom - d.px(kButtonHeight), right, bottom };
}
int HitTest(const Dialog& d, POINT point)
{
	for (int which : { kCancel, kSubmit }) {
		const RECT rect = Button(d, which);
		if (PtInRect(&rect, point)) return which;
	}
	return kNone;
}

std::wstring TextOf(HWND edit)
{
	const int length = GetWindowTextLengthW(edit);
	std::wstring text(static_cast<std::size_t>(length) + 1, L'\0');
	GetWindowTextW(edit, text.data(), length + 1);
	text.resize(static_cast<std::size_t>(length));
	return text;
}

std::wstring Trimmed(const std::wstring& text)
{
	const std::size_t first = text.find_first_not_of(L" \t\r\n\u3000");
	if (first == std::wstring::npos) return {};
	return text.substr(first, text.find_last_not_of(L" \t\r\n\u3000") - first + 1);
}

std::wstring PinyinOf(const std::wstring& han)
{
	UserLexiconImportOptions options;
	options.autoPinyin = true;
	options.skipExisting = false;
	for (const coral::LanguageVariant charset : { coral::LanguageVariant::Simplified, coral::LanguageVariant::Traditional }) {
		std::vector<UserLexiconImportItem> items(1);
		items[0].phrase = wstrToUtf8(han);
		items[0].charset = charset;
		if (coral::runtime::prepare_user_import(items, options) && !items[0].pinyin.empty())
			return phrase_code::InitialsFromPinyin(items[0].pinyin);
	}
	return {};
}

void UpdateSuggestion(Dialog& d)
{
	d.suggested = AddPhraseDialog::defaultCode(TextOf(d.content));
	const std::wstring cue = d.suggested.empty() ? L"默认输入码为前3个字或其拼音首字母" :
		L"默认输入码为前3个字或其拼音首字母“" + d.suggested + L"”";
	SendMessageW(d.code, EM_SETCUEBANNER, TRUE, reinterpret_cast<LPARAM>(cue.c_str()));
}

void SetError(HWND hwnd, Dialog& d, std::wstring error)
{
	d.error = std::move(error);
	InvalidateRect(hwnd, nullptr, FALSE);
}

void CloseDialog(HWND hwnd)
{
	if (Dialog* d = StateOf(hwnd); d && d->owner) {
		EnableWindow(d->owner, TRUE);
		SetActiveWindow(d->owner);
	}
	DestroyWindow(hwnd);
}

void Submit(HWND hwnd, Dialog& d)
{
	const std::wstring content = Trimmed(TextOf(d.content));
	if (content.empty()) {
		SetError(hwnd, d, L"请输入内容");
		SetFocus(d.content);
		return;
	}
	const std::wstring typed = TextOf(d.code);
	std::wstring code = phrase_code::NormalizeCode(typed);
	if (Trimmed(typed).empty()) code = AddPhraseDialog::defaultCode(content);
	if (code.empty()) {
		SetError(hwnd, d, Trimmed(typed).empty() ? L"无法生成默认输入码，请填写输入码" : L"输入码只能包含字母");
		SetFocus(d.code);
		return;
	}
	if (code.size() > phrase_code::kMaxCodeLength) {
		SetError(hwnd, d, L"输入码不能超过 24 个字母");
		SetFocus(d.code);
		return;
	}
	if (phrase_code::StartsSpecialMode(code) && !sConfig->getIsDoublepinyinMode()) {
		SetError(hwnd, d, L"u、v 开头会进入 U/V 模式，请更换输入码");
		SetFocus(d.code);
		return;
	}
	const AddPhraseDialog::Request& request = d.request;
	if (request.editing && (request.originalCode != code || request.originalContent != content))
		coral::runtime::remove_phrase_shortcut(wstrToUtf8(request.originalCode), wstrToUtf8(request.originalContent));
	if (!coral::runtime::add_phrase_shortcut(wstrToUtf8(code), wstrToUtf8(content))) {
		SetError(hwnd, d, L"添加失败，输入法词库可能尚未加载完成");
		return;
	}
	SettingsWindow::refreshPhraseList();
	CloseDialog(hwnd);
}

void Paint(HWND hwnd, const Dialog& d, HDC target)
{
	RECT client{}; GetClientRect(hwnd, &client);
	Canvas canvas(client.right, client.bottom);
	if (!canvas.valid()) return;
	Surface surface = canvas.surface();
	const SettingsSkin& s = SettingsWindow::skin();
	surface.fill(s.window);
	StrokeRoundRect(surface, { 0.5f, 0.5f, client.right - 0.5f, client.bottom - 0.5f }, 0.0f, 1.0f, s.lineStrong);
	const RECT card = Card(d);
	const float radius = static_cast<float>(d.px(10));
	FillRoundRect(surface, ToRectF(card), radius, s.surface);
	StrokeRoundRect(surface, ToRectF(card), radius, 1.0f, s.line);
	const int split = card.top + d.px(kRowSplit);
	FillRoundRect(surface, { static_cast<float>(card.left + d.px(16)), static_cast<float>(split),
		static_cast<float>(card.right - d.px(16)), static_cast<float>(split + 1) }, 0.0f, s.line);
	const int footer = client.bottom - d.px(kFooter);
	FillRoundRect(surface, { 1.0f, static_cast<float>(footer), client.right - 1.0f,
		static_cast<float>(footer + 1) }, 0.0f, s.line);

	const RECT cancel = Button(d, kCancel), submit = Button(d, kSubmit);
	const float buttonRadius = static_cast<float>(d.px(s.controlRadius));
	FillRoundRect(surface, ToRectF(cancel), buttonRadius,
		d.pressed == kCancel ? s.pressed : d.hover == kCancel ? s.hover : s.surface);
	StrokeRoundRect(surface, ToRectF(cancel), buttonRadius, 1.0f, d.hover == kCancel ? s.lineHover : s.lineStrong);
	FillRoundRect(surface, ToRectF(submit), buttonRadius,
		d.pressed == kSubmit ? s.accentPressed : d.hover == kSubmit ? s.accentHover : s.accent);

	canvas.beginText();
	HDC dc = canvas.dc();
	HGDIOBJ old = SelectObject(dc, d.font);
	SetBkMode(dc, TRANSPARENT);
	const auto draw = [dc](const std::wstring& text, RECT rect, std::uint32_t color, UINT format) {
		SetTextColor(dc, ToColorRef(color));
		DrawTextW(dc, text.c_str(), static_cast<int>(text.size()), &rect, format | DT_SINGLELINE | DT_NOPREFIX);
	};
	const int labelLeft = card.left + d.px(18);
	draw(L"内　容", { labelLeft, card.top + d.px(12), labelLeft + d.px(kLabelWidth), card.top + d.px(36) },
		s.text, DT_LEFT | DT_VCENTER);
	draw(L"输入码", { labelLeft, split + d.px(13), labelLeft + d.px(kLabelWidth), split + d.px(37) },
		s.text, DT_LEFT | DT_VCENTER);
	draw(L"取消", cancel, s.text, DT_CENTER | DT_VCENTER);
	draw(d.request.editing ? L"保存" : L"添加", submit, s.onAccent, DT_CENTER | DT_VCENTER);
	if (!d.error.empty())
		draw(d.error, { d.px(kMargin + 4), cancel.top, cancel.left - d.px(12), cancel.bottom },
			s.danger, DT_LEFT | DT_VCENTER | DT_END_ELLIPSIS);
	SelectObject(dc, old);
	canvas.endText();
	canvas.blit(target, 0, 0);
}

void Layout(Dialog& d)
{
	const RECT card = Card(d);
	const int left = card.left + d.px(18 + kLabelWidth);
	const int width = card.right - d.px(16) - left;
	SetWindowPos(d.content, nullptr, left, card.top + d.px(14), width, d.px(58), SWP_NOZORDER | SWP_NOACTIVATE);
	SetWindowPos(d.code, nullptr, left, card.top + d.px(kRowSplit + 14), width, d.px(22),
		SWP_NOZORDER | SWP_NOACTIVATE);
}
} // namespace

std::wstring AddPhraseDialog::defaultCode(const std::wstring& content)
{
	const phrase_code::Seed seed = phrase_code::ExtractSeed(content);
	if (!seed.latin.empty()) return seed.latin;
	if (seed.han.empty()) return {};
	std::wstring initials = PinyinOf(seed.han);
	if (!initials.empty()) return initials;
	// 整体注音失败时逐字注音，跳过无法识别的字。
	for (const wchar_t c : seed.han)
		initials += PinyinOf(std::wstring(1, c));
	return initials;
}

void AddPhraseDialog::show(HWND owner, const Request& request)
{
	if (s_hwnd) close();
	static const ATOM registered = [] {
		WNDCLASSEXW wc{ sizeof(wc) };
		wc.style = CS_DROPSHADOW;
		wc.lpfnWndProc = &AddPhraseDialog::wndProc;
		wc.hInstance = GetModuleHandleW(nullptr);
		wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
		wc.hIcon = LoadIconW(wc.hInstance, MAKEINTRESOURCEW(IDI_PINYIN));
		wc.lpszClassName = L"CoralPinyinAddPhraseDlg";
		return RegisterClassExW(&wc);
	}();
	if (!registered) return;
	if (owner && !IsWindowVisible(owner)) owner = nullptr;

	auto d = std::make_unique<Dialog>();
	d->owner = owner;
	d->request = request;
	d->scale = static_cast<float>(owner ? GetDpiForWindow(owner) : GetDpiForSystem()) / 96.0f;
	RECT area{};
	if (owner) GetWindowRect(owner, &area);
	else SystemParametersInfoW(SPI_GETWORKAREA, 0, &area, 0);
	const int width = d->px(kWidth), height = d->px(kHeight);
	const int x = area.left + (area.right - area.left - width) / 2;
	const int y = area.top + (area.bottom - area.top - height) / 2;
	HWND hwnd = CreateWindowExW(owner ? 0 : WS_EX_APPWINDOW | WS_EX_TOPMOST, L"CoralPinyinAddPhraseDlg",
		request.editing ? L"编辑常用语" : L"添加常用语", WS_POPUP | WS_CLIPCHILDREN,
		x, y, width, height, owner, nullptr, GetModuleHandleW(nullptr), d.get());
	if (!hwnd) return;
	d.release(); // 从这里开始由 WM_NCDESTROY 持有状态。
	if (owner) EnableWindow(owner, FALSE);
	ShowWindow(hwnd, SW_SHOW);
	SetForegroundWindow(hwnd);
	Dialog* state = StateOf(hwnd);
	SetFocus(request.content.empty() ? state->content : state->code);
	if (!request.content.empty()) SendMessageW(state->code, EM_SETSEL, 0, -1);
}

void AddPhraseDialog::close()
{
	if (s_hwnd) CloseDialog(s_hwnd);
}

LRESULT CALLBACK AddPhraseDialog::editProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam,
	UINT_PTR, DWORD_PTR)
{
	const HWND dialog = GetParent(hwnd);
	Dialog* d = StateOf(dialog);
	const bool isCode = GetDlgCtrlID(hwnd) == ID_CODE;
	switch (msg) {
	case WM_KEYDOWN:
		if (!d) break;
		if (wParam == VK_ESCAPE) { CloseDialog(dialog); return 0; }
		if (wParam == VK_TAB) { SetFocus(isCode ? d->content : d->code); return 0; }
		if (wParam == VK_RETURN && (isCode || (GetKeyState(VK_CONTROL) & 0x8000))) {
			Submit(dialog, *d);
			return 0;
		}
		break;
	case WM_CHAR:
		// 已在 WM_KEYDOWN 处理的按键不再插入字符或提示音。
		if (wParam == L'\t' || wParam == 0x1B || (wParam == L'\r' && isCode) || wParam == 0x0A) return 0;
		break;
	case WM_NCDESTROY:
		RemoveWindowSubclass(hwnd, editProc, 1);
		break;
	}
	return DefSubclassProc(hwnd, msg, wParam, lParam);
}

LRESULT CALLBACK AddPhraseDialog::wndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	if (msg == WM_NCCREATE) {
		const auto create = reinterpret_cast<CREATESTRUCTW*>(lParam);
		SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
	}
	Dialog* d = StateOf(hwnd);
	if (!d) return DefWindowProcW(hwnd, msg, wParam, lParam);
	switch (msg) {
	case WM_CREATE: {
		s_hwnd = hwnd;
		const SettingsSkin& s = SettingsWindow::skin();
		const int corner = 2; // DWMWCP_ROUND（圆角）
		DwmSetWindowAttribute(hwnd, 33 /*  DWMWA_WINDOW_CORNER_PREFERENCE（窗口圆角偏好） */, &corner, sizeof(corner));
		d->font = CreateFontW(-d->px(14), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
			OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
			s.fontName.c_str());
		d->surfaceBrush = CreateSolidBrush(ToColorRef(s.surface));
		const HINSTANCE instance = GetModuleHandleW(nullptr);
		d->content = CreateWindowExW(0, L"EDIT", d->request.content.c_str(),
			WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN,
			0, 0, 10, 10, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_CONTENT)), instance, nullptr);
		d->code = CreateWindowExW(0, L"EDIT", d->request.code.c_str(),
			WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
			0, 0, 10, 10, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_CODE)), instance, nullptr);
		for (HWND edit : { d->content, d->code }) {
			SendMessageW(edit, WM_SETFONT, reinterpret_cast<WPARAM>(d->font), FALSE);
			SendMessageW(edit, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, 0);
			SetWindowSubclass(edit, editProc, 1, 0);
		}
		SendMessageW(d->content, EM_LIMITTEXT, kMaxContentLength, 0);
		SendMessageW(d->code, EM_LIMITTEXT, phrase_code::kMaxCodeLength, 0);
		Layout(*d);
		UpdateSuggestion(*d);
		return 0;
	}
	case WM_ERASEBKGND:
		return 1;
	case WM_PAINT: {
		PAINTSTRUCT ps{};
		HDC dc = BeginPaint(hwnd, &ps);
		Paint(hwnd, *d, dc);
		EndPaint(hwnd, &ps);
		return 0;
	}
	case WM_CTLCOLOREDIT: {
		const SettingsSkin& s = SettingsWindow::skin();
		HDC dc = reinterpret_cast<HDC>(wParam);
		SetBkColor(dc, ToColorRef(s.surface));
		SetTextColor(dc, ToColorRef(s.text));
		return reinterpret_cast<LRESULT>(d->surfaceBrush);
	}
	case WM_COMMAND:
		if (HIWORD(wParam) == EN_CHANGE) {
			if (LOWORD(wParam) == ID_CONTENT) UpdateSuggestion(*d);
			if (!d->error.empty()) SetError(hwnd, *d, L"");
		}
		return 0;
	case WM_NCHITTEST: {
		POINT point{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
		ScreenToClient(hwnd, &point);
		return HitTest(*d, point) == kNone ? HTCAPTION : HTCLIENT;
	}
	case WM_SETCURSOR:
		if (LOWORD(lParam) == HTCLIENT) {
			SetCursor(LoadCursorW(nullptr, IDC_HAND));
			return TRUE;
		}
		break;
	case WM_MOUSEMOVE: {
		const int hover = HitTest(*d, { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) });
		if (hover != d->hover) {
			d->hover = hover;
			TRACKMOUSEEVENT track{ sizeof(track), TME_LEAVE, hwnd, 0 };
			TrackMouseEvent(&track);
			InvalidateRect(hwnd, nullptr, FALSE);
		}
		return 0;
	}
	case WM_MOUSELEAVE:
		d->hover = kNone;
		InvalidateRect(hwnd, nullptr, FALSE);
		return 0;
	case WM_LBUTTONDOWN:
		d->pressed = HitTest(*d, { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) });
		if (d->pressed != kNone) SetCapture(hwnd);
		InvalidateRect(hwnd, nullptr, FALSE);
		return 0;
	case WM_LBUTTONUP: {
		const int pressed = d->pressed;
		d->pressed = kNone;
		if (GetCapture() == hwnd) ReleaseCapture();
		InvalidateRect(hwnd, nullptr, FALSE);
		if (pressed != kNone && pressed == HitTest(*d, { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) })) {
			if (pressed == kSubmit) Submit(hwnd, *d);
			else CloseDialog(hwnd);
		}
		return 0;
	}
	case WM_CAPTURECHANGED:
		d->pressed = kNone;
		InvalidateRect(hwnd, nullptr, FALSE);
		return 0;
	case WM_ACTIVATE:
		if (LOWORD(wParam) != WA_INACTIVE) {
			HWND focus = GetFocus();
			if (focus != d->content && focus != d->code) SetFocus(d->content);
		}
		return 0;
	case WM_CLOSE:
		CloseDialog(hwnd);
		return 0;
	case WM_DESTROY:
		if (d->owner) EnableWindow(d->owner, TRUE);
		s_hwnd = nullptr;
		return 0;
	case WM_NCDESTROY: {
		SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
		if (d->font) DeleteObject(d->font);
		if (d->surfaceBrush) DeleteObject(d->surfaceBrush);
		delete d;
		return DefWindowProcW(hwnd, msg, wParam, lParam);
	}
	}
	return DefWindowProcW(hwnd, msg, wParam, lParam);
}

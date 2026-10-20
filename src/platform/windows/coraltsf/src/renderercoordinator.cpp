#include "coral/product/product_identity.h"
#include "pch.h"
#include "renderercoordinator.h"
#include "candidatecontroller.h"

#include "candidate_theme.h"
#include "coreapi.h"
#include "configauthority.h"
#include "windows_host_bootstrap.h"
#include "inprocesscandidatewindow.h"
#include "processmonitor.h"
#include "remote_renderer_port.h"
#include "textservice.h"
#include "tsfthreadcontext.h"
#include "input_timing.h"

#include <algorithm>
#include <cwchar>
#include <filesystem>
#include <vector>

namespace coral::tsf
{
namespace
{
// 按皮肤值每次解析：安装目录与 LocalLow 的查找按按键逐一执行代价过高。
std::string SkinImagePath(const std::string& skin)
{
	thread_local std::string cachedSkin;
	thread_local std::string cachedPath;
	thread_local bool cached = false;
	if (cached && skin == cachedSkin) return cachedPath;
	const win32::CandidateThemeConfig theme = win32::CandidateThemeConfig::parse(skin);
	cachedPath = theme.kind != win32::CandidateBackground::Image ? std::string() :
		win32::WideToUtf8(theme.imagePath(hlp::getInstallPath().wstring(),
			hlp::getLocalLowCoralPinyinPath().wstring()));
	cachedSkin = skin;
	cached = true;
	return cachedPath;
}

bool WaitForRendererPipe(std::uint32_t waitMs)
{
	const std::wstring pipeName = renderer_ipc::DefaultRendererPipeName();
	const ULONGLONG deadline = ::GetTickCount64() + waitMs;
	do
	{
		if (::WaitNamedPipeW(pipeName.c_str(), waitMs == 0 ? 0 : 25)) return true;
		const DWORD error = ::GetLastError();
		if (error != ERROR_FILE_NOT_FOUND && error != ERROR_SEM_TIMEOUT && error != ERROR_PIPE_BUSY)
			return false;
		if (waitMs == 0) return false;
		::Sleep(10);
	} while (::GetTickCount64() < deadline);
	return false;
}

bool NeedsInProcessCandidate()
{
	wchar_t path[MAX_PATH]{};
	if (!::GetModuleFileNameW(nullptr, path, ARRAYSIZE(path))) return false;
	const wchar_t* name = std::wcsrchr(path, L'\\');
	name = name ? name + 1 : path;
	return ::_wcsicmp(name, L"SearchHost.exe") == 0 ||
		::_wcsicmp(name, L"SearchApp.exe") == 0 ||
		::_wcsicmp(name, L"SearchUI.exe") == 0 ||
		::_wcsicmp(name, L"Taskmgr.exe") == 0 ||
		renderer_ipc::UsesHighIntegrityRenderer();
}

std::filesystem::path RendererPath()
{
	wchar_t modulePath[MAX_PATH] = {};
	if (::GetModuleFileNameW(g_hInst, modulePath, ARRAYSIZE(modulePath)))
	{
		auto adjacent = std::filesystem::path(modulePath).parent_path() / coral::product::kRendererExeWide;
		if (std::filesystem::exists(adjacent)) return adjacent;
	}
	return hlp::getInstallPath() / coral::product::kRendererExeWide;
}

bool EnsureRendererRunning(std::uint32_t waitMs)
{
	if (WaitForRendererPipe(0)) return true;
	if (isInstalling()) return false;
	if (coral::bootstrap::is_restricted_text_host())
		return Coreapi::LaunchRendererViaService() && WaitForRendererPipe(waitMs);
	const auto executable = RendererPath();
	bool launched = false;
	if (!executable.empty() && std::filesystem::exists(executable))
	{
		std::wstring command = L"\"" + executable.wstring() + L"\"";
		std::vector<wchar_t> buffer(command.begin(), command.end());
		buffer.push_back(L'\0');
		STARTUPINFOW startup{ sizeof(startup) };
		PROCESS_INFORMATION process{};
		launched = ::CreateProcessW(executable.c_str(), buffer.data(), nullptr, nullptr,
			FALSE, CREATE_NO_WINDOW, nullptr, executable.parent_path().c_str(), &startup, &process) != FALSE;
		if (launched)
		{
			::CloseHandle(process.hThread);
			::CloseHandle(process.hProcess);
		}
	}
	// 服务启动继承普通用户令牌，无法服务提权宿主窗口所需的
	// 独立高完整性管道。
	if (!launched && !renderer_ipc::UsesHighIntegrityRenderer())
		launched = Coreapi::LaunchRendererViaService();
	return launched && WaitForRendererPipe(waitMs);
}

composition::RenderContextToken ToRenderToken(const ContextToken& token)
{
	return { token.serviceInstanceId, token.contextId, token.generation };
}

ContextToken ToContextToken(const composition::RenderContextToken& token)
{
	return { token.serviceInstanceId, token.contextId, token.contextGeneration };
}

std::uint64_t CandidateIdFor(const CString& value, std::size_t index)
{
	const std::string utf8 = std::string(CT2A(value, CP_UTF8));
	std::uint64_t hash = 1469598103934665603ULL;
	for (unsigned char byte : utf8)
	{
		hash ^= byte;
		hash *= 1099511628211ULL;
	}
	hash ^= static_cast<std::uint64_t>(index) + 1;
	return hash ? hash : 1;
}

std::uint64_t CandidateListSignature(const CandidateSnapshot& snapshot)
{
	std::uint64_t hash = 1469598103934665603ULL;
	for (std::size_t i = 0; i < snapshot.candidates.size(); ++i)
	{
		const std::uint64_t id = i < snapshot.candidateIds.size() && snapshot.candidateIds[i] != 0
			? snapshot.candidateIds[i] : CandidateIdFor(snapshot.candidates[i], i);
		hash ^= id;
		hash *= 1099511628211ULL;
	}
	return hash ? hash : 1;
}
}

RendererCoordinator::~RendererCoordinator()
{
	stop();
}

HRESULT RendererCoordinator::start(CTextService* owner, TsfThreadContext* threadContext)
{
	if (m_port) return S_FALSE;
	if (!owner || !threadContext) return E_INVALIDARG;
	m_owner = owner;
	m_threadContext = threadContext;
	if (NeedsInProcessCandidate())
		m_inProcessCandidate = std::make_unique<InProcessCandidateWindow>();
	m_port = renderer_ipc::CreateRemoteRendererPort(
		renderer_ipc::DefaultRendererPipeName(), 50,
		[] { return EnsureRendererRunning(100); });
	if (m_port)
		m_port->SetActionHandler([this](const composition::RendererAction& action) { onAction(action); });
	return S_OK;
}

void RendererCoordinator::stop()
{
	m_inProcessCandidate.reset();
	m_toolbarContext = {};
	if (m_port)
		m_port->SetActionHandler({});
	m_port.reset();
	m_owner = nullptr;
	m_threadContext = nullptr;
}

bool RendererCoordinator::healthy() const
{
	return m_port && m_port->available();
}

bool RendererCoordinator::ensureAvailable()
{
	coral::input_timing::Scope timing(L"renderer.ensure_available");
	return m_owner && m_threadContext && m_port != nullptr;
}

void RendererCoordinator::publishCandidate(
	const ContextState& context, const CandidateSnapshot& snapshot,
	const RECT& anchor, bool visible, const ImeConfigSnapshot& config)
{
	composition::CandidateRenderSnapshot render;
	render.context = ToRenderToken(context.token());
	render.version = snapshot.version;
	render.visible = visible;
	render.anchorLeft = anchor.left;
	render.anchorTop = anchor.top;
	render.anchorRight = anchor.right;
	render.anchorBottom = anchor.bottom;
	render.selection = snapshot.selection;
	render.pageSize = snapshot.pageSize;
	render.expanded = snapshot.expanded;
	render.hasMore = snapshot.hasMore;
    const auto& controller = context.candidateController();
    const auto hint = controller.modeHint();
    if (!hint.empty()) render.hint = std::string(CW2A(std::wstring(hint).c_str(), CP_UTF8));
    render.listLayout = controller.isClipboardMode() || controller.is_translation_mode();
    render.letterSelection = !render.listLayout && controller.getInputMode() == InputMode::IM_VMode;
	render.fontSize = config.candidateFontSize;
	render.fontName = config.candidateFontName;
	render.theme = config.theme;
	render.skin = config.candidateSkin;
	render.skinImage = SkinImagePath(config.candidateSkin);
	render.candidates.reserve(snapshot.candidates.size());
	for (std::size_t i = 0; i < snapshot.candidates.size(); ++i)
	{
		composition::RenderCandidate item;
		item.id = i < snapshot.candidateIds.size() && snapshot.candidateIds[i] != 0
			? snapshot.candidateIds[i] : CandidateIdFor(snapshot.candidates[i], i);
		item.text = std::string(CT2A(snapshot.candidates[i], CP_UTF8));
		item.canDelete = controller.canDisableCandidate(static_cast<int>(i));
		render.candidates.push_back(std::move(item));
	}
	if (m_inProcessCandidate)
	{
		if (!visible) m_inProcessCandidate->hide();
		else if (presentInProcess(context, render)) return;
	}
	if (!ensureAvailable()) return;
	m_port->PublishCandidate(std::move(render));
}

bool RendererCoordinator::presentInProcess(const ContextState& context,
	const composition::CandidateRenderSnapshot& snapshot)
{
	HWND owner = nullptr;
	CComPtr<ITfContextView> view;
	if (ITfContext* tsfContext = m_owner->getContext(); tsfContext &&
		SUCCEEDED(tsfContext->GetActiveView(&view)) && view)
		view->GetWnd(&owner);
	if (!owner) owner = ::GetFocus();
	const ContextToken token = context.token();
	if (!m_inProcessCandidate->present(snapshot, owner,
		[this, token](std::size_t index, std::uint64_t id) {
			onInProcessSelect(token, index, id);
		},
		[this, token]() { onInProcessToggle(token); },
		[this, token]() { onInProcessPrefetch(token); }))
	{
		m_inProcessCandidate->hide();
		return false;
	}
	m_owner->applyCandidateLayout(m_inProcessCandidate->rowStarts(),
		m_inProcessCandidate->itemCenters());
	if (m_port)
		m_port->HideContext(ToRenderToken(token), snapshot.version);
	return true;
}

void RendererCoordinator::hideContext(const ContextState& context, std::uint64_t version)
{
	if (m_inProcessCandidate) m_inProcessCandidate->hide();
	if (m_port)
		m_port->HideContext(ToRenderToken(context.token()), version);
}

void RendererCoordinator::onInProcessSelect(ContextToken token,
	std::size_t index, std::uint64_t id)
{
	if (!m_owner || !m_threadContext) return;
	const auto* focused = m_threadContext->contexts().focused();
	if (!focused || focused->token().serviceInstanceId != token.serviceInstanceId ||
		focused->token().contextId != token.contextId ||
		focused->token().generation != token.generation) return;
	const auto& current = focused->candidates();
	if (index >= current.candidates.size()) return;
	const std::uint64_t currentId = index < current.candidateIds.size() &&
		current.candidateIds[index] != 0 ? current.candidateIds[index] :
		CandidateIdFor(current.candidates[index], index);
	if (currentId != id) return;
	m_owner->commitCandidateByIndex(static_cast<int>(index));
}

void RendererCoordinator::onInProcessToggle(ContextToken token)
{
	if (!m_owner || !m_threadContext) return;
	const auto* focused = m_threadContext->contexts().focused();
	if (!focused || focused->token().serviceInstanceId != token.serviceInstanceId ||
		focused->token().contextId != token.contextId ||
		focused->token().generation != token.generation) return;
	m_owner->toggleCandidateExpansion();
}

void RendererCoordinator::onInProcessPrefetch(ContextToken token)
{
	if (!m_owner || !m_threadContext) return;
	const auto* focused = m_threadContext->contexts().focused();
	if (!focused || focused->token().serviceInstanceId != token.serviceInstanceId ||
		focused->token().contextId != token.contextId ||
		focused->token().generation != token.generation) return;
	m_owner->requestCandidatePrefetch();
}

void RendererCoordinator::publishToolbar(
	const ContextState* context, bool visible, bool ascii, bool capsLock,
	bool full_width, bool englishPunctuation, bool traditional,
	const ImeConfigSnapshot& config)
{
	if (!ensureAvailable()) return;
	composition::ToolbarRenderSnapshot snapshot;
	snapshot.context = context ? ToRenderToken(context->token()) :
		composition::RenderContextToken{ m_threadContext->contexts().serviceInstanceId(), 0, 0 };
	m_toolbarContext = snapshot.context;
	snapshot.version = ++m_toolbarVersion;
	snapshot.visible = visible;
	snapshot.ascii = ascii;
	snapshot.capsLock = capsLock;
	snapshot.full_width = full_width;
	snapshot.englishPunctuation = englishPunctuation;
	snapshot.traditional = traditional;
	snapshot.theme = config.theme;
	m_port->PublishToolbar(std::move(snapshot));
}

void RendererCoordinator::onAction(const composition::RendererAction& action)
{
	if (!m_owner || !m_threadContext || action.snapshotVersion == 0) return;
	RequestToken request{ ToContextToken(action.context), action.snapshotVersion };
	CTextService* owner = m_owner;
	TsfThreadContext* threadContext = m_threadContext;
	const bool candidateAction =
		action.type == composition::RendererActionType::select_candidate ||
		action.type == composition::RendererActionType::PinCandidate ||
		action.type == composition::RendererActionType::DeleteCandidate ||
		action.type == composition::RendererActionType::AddPhrase ||
		action.type == composition::RendererActionType::ToggleExpansion ||
		action.type == composition::RendererActionType::RequestPrefetch ||
		action.type == composition::RendererActionType::CandidateLayout;
	auto callback = [this, owner, threadContext, action, candidateAction]() {
		const ContextState* state = threadContext->contexts().focused();
		if (candidateAction && (!state ||
			state->token().serviceInstanceId != action.context.serviceInstanceId ||
			state->token().contextId != action.context.contextId ||
			state->token().generation != action.context.contextGeneration)) return;
		std::size_t resolvedCandidateIndex = action.candidateIndex;
		if (candidateAction)
		{
			const CandidateSnapshot& snapshot = state->candidates();
			const bool itemAction = action.type == composition::RendererActionType::select_candidate ||
				action.type == composition::RendererActionType::PinCandidate ||
				action.type == composition::RendererActionType::DeleteCandidate ||
				action.type == composition::RendererActionType::AddPhrase;
			if (itemAction)
			{
				const auto found = std::find(
					snapshot.candidateIds.begin(), snapshot.candidateIds.end(), action.candidateId);
				if (found == snapshot.candidateIds.end()) return;
				resolvedCandidateIndex = static_cast<std::size_t>(
					found - snapshot.candidateIds.begin());
				if (resolvedCandidateIndex >= snapshot.candidates.size()) return;
			}
			else if (action.type == composition::RendererActionType::CandidateLayout)
			{
				if (action.candidateId != CandidateListSignature(snapshot)) return;
			}
			// 展开与预取基于当前模型操作，在仅锚点/可见性变化的
			// 快照修订间保持有效。
		}
		else if (action.snapshotVersion != m_toolbarVersion ||
			action.context.serviceInstanceId != m_toolbarContext.serviceInstanceId ||
			action.context.contextId != m_toolbarContext.contextId ||
			action.context.contextGeneration != m_toolbarContext.contextGeneration) return;
		if (!candidateAction)
		{
			BOOL threadFocused = FALSE;
			if (!owner->getThreadMgr() ||
				FAILED(owner->getThreadMgr()->IsThreadFocus(&threadFocused)) || !threadFocused) return;
		}
		switch (action.type)
		{
		case composition::RendererActionType::select_candidate:
			owner->commitCandidateByIndex(static_cast<int>(resolvedCandidateIndex));
			break;
		case composition::RendererActionType::PinCandidate:
			owner->pinCandidateByIndex(static_cast<int>(resolvedCandidateIndex));
			break;
		case composition::RendererActionType::DeleteCandidate:
			owner->deleteCandidateByIndex(static_cast<int>(resolvedCandidateIndex));
			break;
		case composition::RendererActionType::AddPhrase:
			owner->showAddPhraseByIndex(static_cast<int>(resolvedCandidateIndex));
			break;
		case composition::RendererActionType::ToggleExpansion:
			owner->toggleCandidateExpansion();
			break;
		case composition::RendererActionType::RequestPrefetch:
			owner->requestCandidatePrefetch();
			break;
		case composition::RendererActionType::CandidateLayout:
			owner->applyCandidateLayout(action.rowStarts, action.itemCenters);
			break;
		case composition::RendererActionType::ToggleAscii:
			owner->handleFloatingToolbarClick(Ascii);
			break;
		case composition::RendererActionType::TogglePunctuation:
			owner->handleFloatingToolbarClick(Punc);
			break;
		case composition::RendererActionType::ToggleFullWidth:
			owner->toggleFullHalfWidthFromUi();
			break;
		case composition::RendererActionType::ToggleCharacterSet:
			owner->toggleCharacterSetFromUi();
			break;
		case composition::RendererActionType::OpenSettingsMenu:
			owner->openSettingsMenuFromUi();
			break;
		default:
			break;
		}
	};
	if (candidateAction)
		threadContext->dispatcher().post(request, std::move(callback));
	else
		threadContext->dispatcher().postService(action.context.serviceInstanceId,
			action.snapshotVersion, std::move(callback));
}

} // 命名空间 coral::tsf 结束

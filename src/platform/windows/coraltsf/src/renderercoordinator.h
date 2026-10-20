#pragma once

#include "coral/composition/presentation_gateway.h"
#include "contextstore.h"
#include "inprocesscandidatewindow.h"

#include <memory>

class CTextService;

namespace coral::tsf
{

class TsfThreadContext;
struct ImeConfigSnapshot;

class RendererCoordinator final
{
public:
	RendererCoordinator() = default;
	~RendererCoordinator();
	RendererCoordinator(const RendererCoordinator&) = delete;
	RendererCoordinator& operator=(const RendererCoordinator&) = delete;

	HRESULT start(CTextService* owner, TsfThreadContext* threadContext);
	void stop();
	bool healthy() const;
	void publishCandidate(
		const ContextState& context, const CandidateSnapshot& snapshot,
		const RECT& anchor, bool visible, const ImeConfigSnapshot& config);
	void hideContext(const ContextState& context, std::uint64_t version);
	void publishToolbar(
		const ContextState* context, bool visible, bool ascii, bool capsLock,
		bool full_width, bool englishPunctuation, bool traditional,
		const ImeConfigSnapshot& config);

private:
	bool ensureAvailable();
	void onAction(const composition::RendererAction& action);
	bool presentInProcess(const ContextState& context,
		const composition::CandidateRenderSnapshot& snapshot);
	void onInProcessSelect(ContextToken token, std::size_t index, std::uint64_t id);
	void onInProcessToggle(ContextToken token);
	void onInProcessPrefetch(ContextToken token);

	CTextService* m_owner = nullptr;
	TsfThreadContext* m_threadContext = nullptr;
	std::unique_ptr<composition::RendererPort> m_port;
	std::unique_ptr<InProcessCandidateWindow> m_inProcessCandidate;
	composition::RenderContextToken m_toolbarContext{};
	std::uint64_t m_toolbarVersion = 0;
};

} // 命名空间 coral::tsf 结束

#include "remote_renderer_port.h"

#include <chrono>
#include <thread>

int main()
{
	auto port = coral::renderer_ipc::CreateRemoteRendererPort(
		coral::renderer_ipc::DefaultRendererPipeName(), 250);
	if (!port || !port->available()) return 1;
	coral::composition::CandidateRenderSnapshot candidate;
	candidate.context = { 1, 2, 3 };
	candidate.version = 1;
	candidate.visible = false;
	candidate.candidates.push_back({ 9, "smoke", "smoke" });
	port->PublishCandidate(std::move(candidate));
	coral::composition::ToolbarRenderSnapshot toolbar;
	toolbar.context = { 1, 2, 3 };
	toolbar.version = 1;
	toolbar.visible = false;
	port->PublishToolbar(std::move(toolbar));
	std::this_thread::sleep_for(std::chrono::milliseconds(300));
	return port->available() ? 0 : 2;
}

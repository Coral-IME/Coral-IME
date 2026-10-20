#include "renderer_protocol.h"

#undef NDEBUG
#include <cassert>
#include <cstring>
#include <string>
#include <vector>

int main()
{
	using namespace coral;
	renderer_ipc::Message source;
	source.type = renderer_ipc::SessionOperation::CandidateSnapshot;
	source.clientId = 42;
	source.rendererGeneration = 7;
	source.candidate.context = { 11, 12, 13 };
	source.candidate.version = 14;
	source.candidate.visible = true;
	source.candidate.anchorLeft = 100;
	source.candidate.anchorBottom = 200;
	source.candidate.selection = 1;
	source.candidate.pageSize = 5;
	source.candidate.expanded = true;
	source.candidate.hasMore = true;
	source.candidate.fontSize = 18;
	source.candidate.fontName = "Microsoft YaHei";
	source.candidate.theme = "dark";
	source.candidate.skin = "k:i|s:0|g:0|i:3|o:80|x:png|r:2";
	source.candidate.hint = u8"数字 v123 · A-I 选词";
	source.candidate.letterSelection = true;
	source.candidate.skinImage = "C:\\skin\\candidate_custom.png";
	source.candidate.candidates = {
		{ 101, u8"中(zhōng)", "zhong", false }, { 102, u8"中国", "zhong'guo", true } };
	std::vector<std::uint8_t> frame;
	std::string error;
	assert(renderer_ipc::EncodeMessage(source, frame, error));
	renderer_ipc::Message decoded;
	assert(renderer_ipc::DecodeMessage(frame.data(), frame.size(), decoded, error));
	assert(decoded.clientId == 42 && decoded.rendererGeneration == 7);
	assert(decoded.candidate.context.contextGeneration == 13);
	assert(decoded.candidate.candidates.size() == 2);
	assert(decoded.candidate.candidates[1].id == 102);
	assert(decoded.candidate.candidates[1].text == u8"中国");
	assert(!decoded.candidate.candidates[0].canDelete && decoded.candidate.candidates[1].canDelete);
	auto invalidFlag = frame;
	invalidFlag.back() = 2;
	assert(!renderer_ipc::DecodeMessage(invalidFlag.data(), invalidFlag.size(), decoded, error));
	assert(renderer_ipc::DecodeMessage(frame.data(), frame.size(), decoded, error));
	assert(decoded.candidate.fontSize == 18);
	assert(decoded.candidate.fontName == "Microsoft YaHei");
	assert(decoded.candidate.theme == "dark");
	assert(decoded.candidate.skin == source.candidate.skin);
	assert(decoded.candidate.skinImage == source.candidate.skinImage);
	assert(decoded.candidate.expanded && decoded.candidate.hasMore);
    assert(decoded.candidate.hint == source.candidate.hint && decoded.candidate.letterSelection);
	assert(!decoded.candidate.listLayout);

	source.candidate.letterSelection = false;
	source.candidate.listLayout = true;
	assert(renderer_ipc::EncodeMessage(source, frame, error));
	assert(renderer_ipc::DecodeMessage(frame.data(), frame.size(), decoded, error));
	assert(decoded.candidate.listLayout && !decoded.candidate.letterSelection);
	auto oldFrame = frame;
	renderer_ipc::FrameHeader oldHeader;
	std::memcpy(&oldHeader, oldFrame.data(), sizeof(oldHeader));
	oldHeader.schemaVersion = 6;
	std::memcpy(oldFrame.data(), &oldHeader, sizeof(oldHeader));
	assert(!renderer_ipc::DecodeMessage(oldFrame.data(), oldFrame.size(), decoded, error));

	renderer_ipc::Message action;
	action.type = renderer_ipc::SessionOperation::PollAction;
	action.clientId = 42;
	action.rendererGeneration = 7;
	action.action.type = composition::RendererActionType::select_candidate;
	action.action.context = { 11, 12, 13 };
	action.action.snapshotVersion = 14;
	action.action.candidateId = 102;
	action.action.candidateIndex = 1;
	action.action.rowStarts = { 0, 2 };
	action.action.itemCenters = { 50, 140 };
	assert(renderer_ipc::EncodeMessage(action, frame, error));
	assert(renderer_ipc::DecodeMessage(frame.data(), frame.size(), decoded, error));
	assert(decoded.action.type == composition::RendererActionType::select_candidate);
	assert(decoded.action.snapshotVersion == 14 && decoded.action.candidateId == 102);
	assert(decoded.action.rowStarts.size() == 2 && decoded.action.itemCenters[1] == 140);

	action.action.type = composition::RendererActionType::AddPhrase;
	assert(renderer_ipc::EncodeMessage(action, frame, error));
	assert(renderer_ipc::DecodeMessage(frame.data(), frame.size(), decoded, error));
	assert(decoded.action.type == composition::RendererActionType::AddPhrase);

	action.action.type = composition::RendererActionType::OpenSettingsMenu;
	assert(renderer_ipc::EncodeMessage(action, frame, error));
	assert(renderer_ipc::DecodeMessage(frame.data(), frame.size(), decoded, error));
	assert(decoded.action.type == composition::RendererActionType::OpenSettingsMenu);
	return 0;
}

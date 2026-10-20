#pragma once

#include "coral/composition/presentation_gateway.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace coral::renderer_ipc
{

constexpr std::uint32_t kFrameMagic = 0x524C5243; // "CRLR"
constexpr std::uint16_t kSchemaVersion = 7;
constexpr std::uint32_t kMaxPayloadBytes = 1024 * 1024;
constexpr std::uint32_t kMaxStringBytes = 64 * 1024;
constexpr std::uint32_t kMaxCandidates = 600;

enum class SessionOperation : std::uint16_t
{
	Hello = 1,
	CandidateSnapshot = 2,
	ToolbarSnapshot = 3,
	HideContext = 4,
	PollAction = 5,
	ClearClient = 6,
};

#pragma pack(push, 1)
struct FrameHeader
{
	std::uint32_t magic = kFrameMagic;
	std::uint16_t headerSize = sizeof(FrameHeader);
	std::uint16_t schemaVersion = kSchemaVersion;
	std::uint16_t messageType = 0;
	std::uint16_t flags = 0;
	std::uint32_t payloadSize = 0;
	std::uint64_t clientId = 0;
	std::uint64_t rendererGeneration = 0;
};
#pragma pack(pop)

static_assert(sizeof(FrameHeader) == 32, "Renderer IPC header is an on-wire ABI");

struct Message
{
	SessionOperation type = SessionOperation::Hello;
	std::uint64_t clientId = 0;
	std::uint64_t rendererGeneration = 0;
	composition::CandidateRenderSnapshot candidate;
	composition::ToolbarRenderSnapshot toolbar;
	composition::RenderContextToken context;
	std::uint64_t version = 0;
	composition::RendererAction action;
};

bool EncodeMessage(const Message& message, std::vector<std::uint8_t>& frame, std::string& error);
bool DecodeMessage(const void* data, std::size_t size, Message& message, std::string& error);
bool UsesHighIntegrityRenderer();
std::wstring DefaultRendererPipeName();

} // 命名空间 coral::renderer_ipc 结束

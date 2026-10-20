#include "coral/product/product_identity.h"
#include "renderer_protocol.h"

#include <windows.h>

#include <cstring>
#include <limits>
#include <type_traits>

namespace coral::renderer_ipc
{
namespace
{
template<typename T>
void Append(std::vector<std::uint8_t>& out, T value)
{
	static_assert(std::is_trivially_copyable_v<T>);
	const auto* bytes = reinterpret_cast<const std::uint8_t*>(&value);
	out.insert(out.end(), bytes, bytes + sizeof(T));
}

void AppendString(std::vector<std::uint8_t>& out, const std::string& value)
{
	Append(out, static_cast<std::uint32_t>(value.size()));
	out.insert(out.end(), value.begin(), value.end());
}

class Reader
{
public:
	Reader(const std::uint8_t* data, std::size_t size) : m_data(data), m_size(size) {}

	template<typename T>
	bool read(T& value)
	{
		static_assert(std::is_trivially_copyable_v<T>);
		if (m_offset > m_size || sizeof(T) > m_size - m_offset)
			return false;
		std::memcpy(&value, m_data + m_offset, sizeof(T));
		m_offset += sizeof(T);
		return true;
	}

	bool readString(std::string& value)
	{
		std::uint32_t bytes = 0;
		if (!read(bytes) || bytes > kMaxStringBytes || m_offset > m_size || bytes > m_size - m_offset)
			return false;
		value.assign(reinterpret_cast<const char*>(m_data + m_offset), bytes);
		m_offset += bytes;
		return true;
	}

	bool done() const { return m_offset == m_size; }

private:
	const std::uint8_t* m_data = nullptr;
	std::size_t m_size = 0;
	std::size_t m_offset = 0;
};

void EncodeContext(std::vector<std::uint8_t>& out, const composition::RenderContextToken& context)
{
	Append(out, context.serviceInstanceId);
	Append(out, context.contextId);
	Append(out, context.contextGeneration);
}

bool DecodeContext(Reader& reader, composition::RenderContextToken& context)
{
	return reader.read(context.serviceInstanceId) && reader.read(context.contextId) &&
		reader.read(context.contextGeneration);
}

void EncodeCandidate(std::vector<std::uint8_t>& out, const composition::CandidateRenderSnapshot& value)
{
	EncodeContext(out, value.context);
	Append(out, value.version);
	Append(out, static_cast<std::uint8_t>(value.visible));
	Append(out, value.anchorLeft);
	Append(out, value.anchorTop);
	Append(out, value.anchorRight);
	Append(out, value.anchorBottom);
	Append(out, value.selection);
	Append(out, value.pageSize);
	Append(out, static_cast<std::uint8_t>(value.expanded));
	Append(out, static_cast<std::uint8_t>(value.hasMore));
	Append(out, value.fontSize);
	AppendString(out, value.fontName);
	AppendString(out, value.theme);
	AppendString(out, value.skin);
	AppendString(out, value.skinImage);
    AppendString(out, value.hint);
    Append(out, static_cast<std::uint8_t>(value.letterSelection));
	Append(out, static_cast<std::uint8_t>(value.listLayout));
	Append(out, static_cast<std::uint32_t>(value.candidates.size()));
	for (const auto& item : value.candidates)
	{
		Append(out, item.id);
		AppendString(out, item.text);
		AppendString(out, item.pinyin);
		Append(out, static_cast<std::uint8_t>(item.canDelete));
	}
}

bool DecodeCandidate(Reader& reader, composition::CandidateRenderSnapshot& value)
{
	std::uint8_t visible = 0, expanded = 0, hasMore = 0, letters = 0, list = 0;
	std::uint32_t count = 0;
	if (!DecodeContext(reader, value.context) || !reader.read(value.version) ||
		!reader.read(visible) || !reader.read(value.anchorLeft) || !reader.read(value.anchorTop) ||
		!reader.read(value.anchorRight) || !reader.read(value.anchorBottom) ||
		!reader.read(value.selection) || !reader.read(value.pageSize) ||
		!reader.read(expanded) || !reader.read(hasMore) ||
		!reader.read(value.fontSize) || !reader.readString(value.fontName) ||
		!reader.readString(value.theme) || !reader.readString(value.skin) ||
		!reader.readString(value.skinImage) || !reader.readString(value.hint) ||
        !reader.read(letters) || letters > 1 || !reader.read(list) || list > 1 ||
		!reader.read(count) || count > kMaxCandidates)
		return false;
	value.letterSelection = letters != 0;
	value.listLayout = list != 0;
	value.visible = visible != 0;
	value.expanded = expanded != 0;
	value.hasMore = hasMore != 0;
	value.candidates.clear();
	value.candidates.reserve(count);
	for (std::uint32_t i = 0; i < count; ++i)
	{
		composition::RenderCandidate item;
		std::uint8_t canDelete = 0;
		if (!reader.read(item.id) || !reader.readString(item.text) || !reader.readString(item.pinyin) ||
			!reader.read(canDelete) || canDelete > 1)
			return false;
		item.canDelete = canDelete != 0;
		value.candidates.push_back(std::move(item));
	}
	return true;
}

void EncodeToolbar(std::vector<std::uint8_t>& out, const composition::ToolbarRenderSnapshot& value)
{
	EncodeContext(out, value.context);
	Append(out, value.version);
	Append(out, static_cast<std::uint8_t>(value.visible));
	Append(out, static_cast<std::uint8_t>(value.ascii));
	Append(out, static_cast<std::uint8_t>(value.capsLock));
	Append(out, static_cast<std::uint8_t>(value.full_width));
	Append(out, static_cast<std::uint8_t>(value.englishPunctuation));
	Append(out, static_cast<std::uint8_t>(value.traditional));
	AppendString(out, value.theme);
}

bool DecodeToolbar(Reader& reader, composition::ToolbarRenderSnapshot& value)
{
	std::uint8_t visible = 0, ascii = 0, caps = 0, full = 0, punctuation = 0, traditional = 0;
	if (!DecodeContext(reader, value.context) || !reader.read(value.version) ||
		!reader.read(visible) || !reader.read(ascii) || !reader.read(caps) ||
		!reader.read(full) || !reader.read(punctuation) || !reader.read(traditional) ||
		!reader.readString(value.theme))
		return false;
	value.visible = visible != 0;
	value.ascii = ascii != 0;
	value.capsLock = caps != 0;
	value.full_width = full != 0;
	value.englishPunctuation = punctuation != 0;
	value.traditional = traditional != 0;
	return true;
}

void EncodeAction(std::vector<std::uint8_t>& out, const composition::RendererAction& value)
{
	Append(out, static_cast<std::uint32_t>(value.type));
	EncodeContext(out, value.context);
	Append(out, value.snapshotVersion);
	Append(out, value.candidateId);
	Append(out, value.candidateIndex);
	Append(out, static_cast<std::uint32_t>(value.rowStarts.size()));
	for (std::uint32_t item : value.rowStarts) Append(out, item);
	Append(out, static_cast<std::uint32_t>(value.itemCenters.size()));
	for (std::uint32_t item : value.itemCenters) Append(out, item);
}

bool DecodeAction(Reader& reader, composition::RendererAction& value)
{
	std::uint32_t type = 0;
	if (!reader.read(type) || type > static_cast<std::uint32_t>(composition::RendererActionType::OpenSettingsMenu) ||
		!DecodeContext(reader, value.context) || !reader.read(value.snapshotVersion) ||
		!reader.read(value.candidateId) || !reader.read(value.candidateIndex))
		return false;
	std::uint32_t rowCount = 0;
	if (!reader.read(rowCount) || rowCount > kMaxCandidates) return false;
	value.rowStarts.resize(rowCount);
	for (auto& item : value.rowStarts) if (!reader.read(item)) return false;
	std::uint32_t centerCount = 0;
	if (!reader.read(centerCount) || centerCount > kMaxCandidates) return false;
	value.itemCenters.resize(centerCount);
	for (auto& item : value.itemCenters) if (!reader.read(item)) return false;
	value.type = static_cast<composition::RendererActionType>(type);
	return true;
}
}

bool EncodeMessage(const Message& message, std::vector<std::uint8_t>& frame, std::string& error)
{
	std::vector<std::uint8_t> payload;
	switch (message.type)
	{
	case SessionOperation::Hello:
	case SessionOperation::ClearClient:
		break;
	case SessionOperation::CandidateSnapshot:
		if (message.candidate.candidates.size() > kMaxCandidates)
		{
			error = "too many candidates";
			return false;
		}
		for (const auto& item : message.candidate.candidates)
			if (item.text.size() > kMaxStringBytes || item.pinyin.size() > kMaxStringBytes)
			{
				error = "candidate string too large";
				return false;
			}
		if (message.candidate.fontName.size() > kMaxStringBytes ||
			message.candidate.theme.size() > kMaxStringBytes ||
			message.candidate.skin.size() > kMaxStringBytes ||
			message.candidate.skinImage.size() > kMaxStringBytes ||
            message.candidate.hint.size() > kMaxStringBytes)
		{
			error = "candidate style string too large";
			return false;
		}
		EncodeCandidate(payload, message.candidate);
		break;
	case SessionOperation::ToolbarSnapshot:
		if (message.toolbar.theme.size() > kMaxStringBytes)
		{
			error = "toolbar style string too large";
			return false;
		}
		EncodeToolbar(payload, message.toolbar);
		break;
	case SessionOperation::HideContext:
		EncodeContext(payload, message.context);
		Append(payload, message.version);
		break;
	case SessionOperation::PollAction:
		EncodeAction(payload, message.action);
		break;
	}
	if (payload.size() > kMaxPayloadBytes)
	{
		error = "renderer payload too large";
		return false;
	}
	FrameHeader header;
	header.messageType = static_cast<std::uint16_t>(message.type);
	header.payloadSize = static_cast<std::uint32_t>(payload.size());
	header.clientId = message.clientId;
	header.rendererGeneration = message.rendererGeneration;
	frame.resize(sizeof(header));
	std::memcpy(frame.data(), &header, sizeof(header));
	frame.insert(frame.end(), payload.begin(), payload.end());
	return true;
}

bool DecodeMessage(const void* data, std::size_t size, Message& message, std::string& error)
{
	if (!data || size < sizeof(FrameHeader))
	{
		error = "truncated renderer header";
		return false;
	}
	FrameHeader header;
	std::memcpy(&header, data, sizeof(header));
	if (header.magic != kFrameMagic || header.headerSize != sizeof(FrameHeader) ||
		header.schemaVersion != kSchemaVersion || header.payloadSize > kMaxPayloadBytes ||
		size != sizeof(FrameHeader) + header.payloadSize ||
		header.messageType < static_cast<std::uint16_t>(SessionOperation::Hello) ||
		header.messageType > static_cast<std::uint16_t>(SessionOperation::ClearClient))
	{
		error = "invalid renderer frame";
		return false;
	}
	message = {};
	message.type = static_cast<SessionOperation>(header.messageType);
	message.clientId = header.clientId;
	message.rendererGeneration = header.rendererGeneration;
	Reader reader(static_cast<const std::uint8_t*>(data) + sizeof(FrameHeader), header.payloadSize);
	bool ok = false;
	switch (message.type)
	{
	case SessionOperation::Hello: ok = true; break;
	case SessionOperation::ClearClient: ok = true; break;
	case SessionOperation::CandidateSnapshot: ok = DecodeCandidate(reader, message.candidate); break;
	case SessionOperation::ToolbarSnapshot: ok = DecodeToolbar(reader, message.toolbar); break;
	case SessionOperation::HideContext:
		ok = DecodeContext(reader, message.context) && reader.read(message.version);
		break;
	case SessionOperation::PollAction: ok = DecodeAction(reader, message.action); break;
	}
	if (!ok || !reader.done())
	{
		error = "invalid renderer payload";
		return false;
	}
	return true;
}

bool UsesHighIntegrityRenderer()
{
	HANDLE token = nullptr;
	if (!::OpenProcessToken(::GetCurrentProcess(), TOKEN_QUERY, &token))
		return false;
	DWORD bytes = 0;
	::GetTokenInformation(token, TokenIntegrityLevel, nullptr, 0, &bytes);
	std::vector<std::uint8_t> buffer(bytes);
	const bool valid = bytes >= sizeof(TOKEN_MANDATORY_LABEL) &&
		::GetTokenInformation(token, TokenIntegrityLevel, buffer.data(), bytes, &bytes) != FALSE;
	::CloseHandle(token);
	if (!valid) return false;
	const PSID sid = reinterpret_cast<TOKEN_MANDATORY_LABEL*>(buffer.data())->Label.Sid;
	if (!::IsValidSid(sid)) return false;
	const DWORD subauthorityCount = *::GetSidSubAuthorityCount(sid);
	return subauthorityCount != 0 &&
		*::GetSidSubAuthority(sid, subauthorityCount - 1) >= SECURITY_MANDATORY_HIGH_RID;
}

std::wstring DefaultRendererPipeName()
{
	DWORD session_id = 0;
	::ProcessIdToSessionId(::GetCurrentProcessId(), &session_id);
	return coral::product::kRendererPipePrefixWide + std::to_wstring(session_id) +
		(UsesHighIntegrityRenderer() ? L".High" : L"");
}

} // 命名空间 coral::renderer_ipc 结束

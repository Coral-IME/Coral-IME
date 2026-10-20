#include "coral/transport/session_wire.h"
#include "pipe_session_client.h"

#include <windows.h>

#undef NDEBUG
#include <cassert>
#include <chrono>
#include <string>
#include <thread>
#include <vector>

namespace
{
enum class ReplyFault { kNone, kClient, kRequest, kSession, kGeneration, kOperation, kOldVersion, kLate };

void Serve(const std::wstring& pipeName, std::uint64_t generation, int calls, HANDLE ready, ReplyFault fault)
{
	for (int i = 0; i < calls; ++i)
	{
		HANDLE pipe = ::CreateNamedPipeW(
			pipeName.c_str(), PIPE_ACCESS_DUPLEX,
			PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
			1, coral::transport::kMaxPayloadBytes, coral::transport::kMaxPayloadBytes,
			0, nullptr);
		assert(pipe != INVALID_HANDLE_VALUE);
		if (i == 0) ::SetEvent(ready);
		const BOOL connected = ::ConnectNamedPipe(pipe, nullptr) || ::GetLastError() == ERROR_PIPE_CONNECTED;
		assert(connected);
		std::vector<std::uint8_t> input(coral::transport::kEnvelopeBytes + 4096);
		DWORD read = 0;
		assert(::ReadFile(pipe, input.data(), static_cast<DWORD>(input.size()), &read, nullptr));
		coral::transport::SessionRequest request;
		std::string error;
		assert(coral::transport::decode_request(input.data(), read, request, error));
		coral::transport::SessionReply response;
		response.operation = request.operation;
		response.client_id = request.client_id;
		response.engine.request_id = request.request_id;
		response.engine.session_id = request.session_id;
		response.engine.session_generation = request.session_generation;
		response.engine.server_generation = generation;
		if (request.operation == coral::transport::SessionOperation::kOpenSession)
		{
			response.engine.session_id = 99;
			response.engine.session_generation = 1;
		}
		if (request.operation == coral::transport::SessionOperation::kParseFull)
		{
			response.engine.output.candidates.candidates = { { 1, u8"瞐", "umumumu", u8"瞐(mò)" } };
			switch (fault)
			{
			case ReplyFault::kClient: ++response.client_id; break;
			case ReplyFault::kRequest: ++response.engine.request_id; break;
			case ReplyFault::kSession: ++response.engine.session_id; break;
			case ReplyFault::kGeneration: ++response.engine.server_generation; break;
			case ReplyFault::kOperation: response.operation = coral::transport::SessionOperation::kPing; break;
			case ReplyFault::kLate: ::Sleep(200); break;
			default: break;
			}
		}
		std::vector<std::uint8_t> output;
		assert(coral::transport::encode_reply(response, output, error));
		if (request.operation == coral::transport::SessionOperation::kParseFull && fault == ReplyFault::kOldVersion)
			output[4] = 3;
		DWORD written = 0;
		const bool sent = ::WriteFile(pipe, output.data(), static_cast<DWORD>(output.size()), &written, nullptr) != FALSE;
		assert(sent || fault == ReplyFault::kLate);
		::FlushFileBuffers(pipe);
		::DisconnectNamedPipe(pipe);
		::CloseHandle(pipe);
	}
}
}

int main()
{
	const std::wstring pipeName = L"\\\\.\\pipe\\CoralPinyin.RemoteTest." +
		std::to_wstring(::GetCurrentProcessId()) + L"." + std::to_wstring(::GetTickCount64());
	HANDLE ready = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
	assert(ready);
	std::thread server(Serve, pipeName, 0x12345678ULL, 4, ready, ReplyFault::kNone);
	assert(::WaitForSingleObject(ready, 1000) == WAIT_OBJECT_0);
	auto port = coral::transport::make_pipe_session_client(pipeName, 500);
	assert(port->available());
	assert(port->open_session() == 99);
	const auto parsed = port->query_page(99, 1, "", "umumumu", 0, 5, 0);
	assert(parsed);
	assert(parsed.output.candidates.candidates.size() == 1);
	assert(parsed.output.candidates.candidates[0].word == u8"瞐");
	assert(parsed.output.candidates.candidates[0].display_text == u8"瞐(mò)");
	server.join();
	::CloseHandle(ready);

	const std::wstring missing = pipeName + L".missing";
	auto unavailable = coral::transport::make_pipe_session_client(missing, 25);
	const auto started = std::chrono::steady_clock::now();
	assert(!unavailable->available());
	assert(std::chrono::steady_clock::now() - started < std::chrono::seconds(1));

	for (const auto fault : {ReplyFault::kClient, ReplyFault::kRequest, ReplyFault::kSession,
		ReplyFault::kGeneration, ReplyFault::kOperation, ReplyFault::kOldVersion, ReplyFault::kLate})
	{
		const auto isolated_pipe = pipeName + L"." + std::to_wstring(static_cast<int>(fault));
		HANDLE started_event = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
		assert(started_event);
		std::thread peer(Serve, isolated_pipe, 7, 3, started_event, fault);
		assert(::WaitForSingleObject(started_event, 1000) == WAIT_OBJECT_0);
		auto client = coral::transport::make_pipe_session_client(isolated_pipe, fault == ReplyFault::kLate ? 100 : 500);
		assert(client->open_session() == 99);
		const auto begin = std::chrono::steady_clock::now();
		const auto result = client->query_page(99, 123, "", "nihao", 0, 5, 0);
		assert(result.status == (fault == ReplyFault::kLate ? coral::composition::SessionStatus::kTimeout : coral::composition::SessionStatus::kProtocolError));
		assert(result.request_id == 123 && result.session_id == 99 && result.output.candidates.empty());
		assert(std::chrono::steady_clock::now() - begin < std::chrono::seconds(1));
		peer.join();
		::CloseHandle(started_event);
	}
	return 0;
}

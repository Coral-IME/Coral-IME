// 无需显示窗口，直接演练生产管道服务与真实 Windows 管道。
#include "../../src/platform/windows/renderer/main.cpp"
#include "../../src/platform/windows/ipc/common/pipeio.h"
#include <iostream>
#include <stdexcept>

namespace {
using coral::renderer_ipc::Message;
using coral::renderer_ipc::SessionOperation;

void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}

HANDLE connect_client(const std::wstring& name) {
    const auto deadline = GetTickCount64() + 1500;
    do {
        HANDLE pipe = CreateFileW(name.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
            OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
        if (pipe != INVALID_HANDLE_VALUE) {
            DWORD mode = PIPE_READMODE_MESSAGE;
            if (SetNamedPipeHandleState(pipe, &mode, nullptr, nullptr)) return pipe;
            CloseHandle(pipe);
            return INVALID_HANDLE_VALUE;
        }
        WaitNamedPipeW(name.c_str(), 10);
        Sleep(1);
    } while (GetTickCount64() < deadline);
    return INVALID_HANDLE_VALUE;
}

void send_request(HANDLE pipe, const Message& request) {
    std::vector<std::uint8_t> frame;
    std::string error;
    require(coral::renderer_ipc::EncodeMessage(request, frame, error), "encode fixture request");
    DWORD bytes = 0;
    require(SUCCEEDED(coral_ipc::Transfer(pipe, frame.data(), static_cast<DWORD>(frame.size()),
        bytes, true, GetTickCount64() + 1000)), "write fixture request");
}

Message read_reply(HANDLE pipe) {
    std::vector<std::uint8_t> input(sizeof(coral::renderer_ipc::FrameHeader) + coral::renderer_ipc::kMaxPayloadBytes);
    coral_ipc::Event event;
    require(event.get() != nullptr, "create client event");
    OVERLAPPED operation{};
    operation.hEvent = event.get();
    DWORD bytes = 0;
    const BOOL completed = ReadFile(pipe, input.data(), static_cast<DWORD>(input.size()), &bytes, &operation);
    if (!completed) {
        require(GetLastError() == ERROR_IO_PENDING, "begin reply read");
        require(SUCCEEDED(coral_ipc::Await(pipe, operation, 1000, nullptr, bytes)), "read reply within deadline");
    }
    Message response;
    std::string error;
    require(coral::renderer_ipc::DecodeMessage(input.data(), bytes, response, error), "decode renderer reply");
    return response;
}

void healthy_request(const std::wstring& name) {
    RendererHandle client(connect_client(name));
    require(static_cast<bool>(client), "healthy client admitted");
    Message request;
    request.clientId = 41;
    send_request(client.get(), request);
    const auto response = read_reply(client.get());
    require(response.type == SessionOperation::Hello && response.rendererGeneration == g_generation &&
        response.clientId == request.clientId, "healthy hello returns full correlated reply");
}

void expect_closed(HANDLE pipe) {
    const auto deadline = GetTickCount64() + 1000;
    do {
        DWORD bytes = 0;
        if (!PeekNamedPipe(pipe, nullptr, 0, nullptr, &bytes, nullptr)) {
            const auto error = GetLastError();
            require(error == ERROR_BROKEN_PIPE || error == ERROR_PIPE_NOT_CONNECTED || error == ERROR_NO_DATA,
                "stalled connection disconnected normally");
            return;
        }
        Sleep(1);
    } while (GetTickCount64() < deadline);
    require(false, "stalled client released within transaction deadline");
}

void stop_promptly(RendererPipeServer& server) {
    const auto start = GetTickCount64();
    server.stop();
    require(GetTickCount64() - start < 200, "stop cancels I/O before the transaction timeout");
    server.stop();
}

void queue_large_action(std::uint64_t client_id) {
    coral::composition::RendererAction action;
    action.type = coral::composition::RendererActionType::CandidateLayout;
    action.rowStarts.assign(600, 1);
    action.itemCenters.assign(600, 2);
    std::lock_guard<std::mutex> lock(g_stateMutex);
    g_state.actions[client_id].push_back(std::move(action));
}

void wait_action_taken(std::uint64_t client_id) {
    const auto deadline = GetTickCount64() + 1000;
    while (GetTickCount64() < deadline) {
        {
            std::lock_guard<std::mutex> lock(g_stateMutex);
            if (g_state.actions[client_id].empty()) return;
        }
        Sleep(1);
    }
    require(false, "server started large response");
}
}

int main() {
    try {
        g_generation = 123456;
        const std::wstring name = L"\\\\.\\pipe\\Coral.RendererPipeTest." + std::to_wstring(GetCurrentProcessId());
        RendererPipeServer server;
        require(server.start(name), "start production pipe pool");
        healthy_request(name);
        RendererHandle silent(connect_client(name));
        require(static_cast<bool>(silent), "silent client connected");
        const auto start = GetTickCount64();
        healthy_request(name);
        require(GetTickCount64() - start < 200, "one silent client cannot serialize healthy requests");
        {
            RendererHandle client(connect_client(name));
            require(static_cast<bool>(client), "snapshot client connected");
            Message snapshot;
            snapshot.clientId = 42;
            snapshot.rendererGeneration = g_generation;
            snapshot.type = SessionOperation::CandidateSnapshot;
            snapshot.candidate.visible = true;
            snapshot.candidate.context = {1, 2, 3};
            snapshot.candidate.version = 1;
            snapshot.candidate.candidates.push_back({9, "updated", "nihao"});
            send_request(client.get(), snapshot);
            require(read_reply(client.get()).type == snapshot.type, "candidate response delivered");
            std::lock_guard<std::mutex> lock(g_stateMutex);
            require(g_state.candidateClient == 42 && g_state.candidate.candidates.front().text == "updated",
                "candidate snapshot advances while silent client remains open");
        }
        expect_closed(silent.get());
        silent.reset();
        // 所有槽位被占满时也必须到期回收，而不是扩展连接线程。
        std::array<RendererHandle, kRendererPipeWorkers> occupied;
        for (auto& client : occupied) {
            client.reset(connect_client(name));
            require(static_cast<bool>(client), "fill fixed pipe slot");
        }
        healthy_request(name);
        for (auto& client : occupied) { expect_closed(client.get()); client.reset(); }
        {
            RendererHandle malformed(connect_client(name));
            std::uint8_t truncated[3]{};
            DWORD bytes = 0;
            require(SUCCEEDED(coral_ipc::Transfer(malformed.get(), truncated, 3, bytes, true, GetTickCount64() + 1000)),
                "send truncated frame");
            expect_closed(malformed.get());
        }
        {
            RendererHandle oversized(connect_client(name));
            std::vector<std::uint8_t> frame(sizeof(coral::renderer_ipc::FrameHeader) + coral::renderer_ipc::kMaxPayloadBytes + 1);
            DWORD bytes = 0;
            coral_ipc::Transfer(oversized.get(), frame.data(), static_cast<DWORD>(frame.size()),
                bytes, true, GetTickCount64() + 1000);
            expect_closed(oversized.get());
            healthy_request(name);
        }
        for (int i = 0; i < 64; ++i) {
            RendererHandle transient(connect_client(name));
            require(static_cast<bool>(transient), "rapid disconnect admitted");
        }
        for (int i = 0; i < 32; ++i) healthy_request(name);
        stop_promptly(server);
        require(server.start(name), "restart same pipe name");
        // 无客户端时取消连接等待；满槽客户端不关闭时取消读取等待。
        stop_promptly(server);
        require(server.start(name), "restart before read cancellation");
        for (auto& client : occupied) {
            client.reset(connect_client(name));
            require(static_cast<bool>(client), "connect pending read");
        }
        stop_promptly(server);
        for (auto& client : occupied) client.reset();
        // 缩小测试输出缓冲，使合法的大布局回复实际处于写入背压中。
        require(server.start(name, 128), "start backpressure fixture");
        {
            RendererHandle unread(connect_client(name));
            require(static_cast<bool>(unread), "unread reply client connected");
            queue_large_action(90);
            Message poll;
            poll.type = SessionOperation::PollAction;
            poll.clientId = 90;
            poll.rendererGeneration = g_generation;
            send_request(unread.get(), poll);
            wait_action_taken(90);
            expect_closed(unread.get());
            healthy_request(name);
        }
        {
            RendererHandle unread(connect_client(name));
            require(static_cast<bool>(unread), "stop during pending write client connected");
            queue_large_action(91);
            Message poll;
            poll.type = SessionOperation::PollAction;
            poll.clientId = 91;
            poll.rendererGeneration = g_generation;
            send_request(unread.get(), poll);
            wait_action_taken(91);
            stop_promptly(server);
        }
        for (int i = 0; i < 8; ++i) {
            require(server.start(name), "repeat start");
            healthy_request(name);
            stop_promptly(server);
        }
        std::cout << "Renderer pipe contention, deadlines and shutdown passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}

#include <Windows.h>
#include <atlstr.h>
#include <map>
#include <vector>
#include <string>
#include <thread>
#include <atomic>
#include <memory>
#include <cstdio>
#include <cstdlib>
#include "ipcclient.h"
#include "ipcserver.h"
#include "pipeclient.h"
#include "data.h"
#include "pipeio.h"

#define CHECK(condition) do { if (!(condition)) { std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #condition); std::abort(); } } while (0)

static CString PipeName()
{
    static unsigned counter = 0;
    CString name;
    name.Format(L"coralipc_test_%lu_%u", ::GetCurrentProcessId(), ++counter);
    return name;
}

static HRESULT CALLBACK Echo(LPVOID, const std::vector<CString>& params, CIpcResponse& response)
{
    response.AddKeyValue(L"echo", params.empty() ? CString() : params[0]);
    return S_OK;
}

static void CheckEcho(LPCWSTR name, LPCWSTR value = L"alive")
{
    CIpcClient client(name);
    client.Add(value);
    CHECK(client.Call(L"echo") == S_OK);
    CHECK(client.Get(L"echo") == value);
}

static void Codec()
{
    std::vector<CString> params = {L"中文 Ō Ŏ | : % %7C %25 😀", L"", L"%3A|"};
    CString encoded = CDataParser::ClientMerge(L"echo", params);
    std::vector<CString> decoded;
    CDataParser::ServerSplit(encoded, decoded);
    CHECK(decoded.size() == 4 && decoded[0] == L"echo");
    for (size_t i = 0; i < params.size(); ++i) CHECK(decoded[i + 1] == params[i]);
    std::map<CString, CString> values{{L"k|:%ŌŎ", params[0]}, {L"empty", L""}}, parsed;
    CDataParser::ClientSplit(CDataParser::ServerMerge(values), parsed);
    CHECK(values == parsed);
    decoded.clear();
    CDataParser::ServerSplit(L"echo|%7CŌŎ", decoded);
    CHECK(decoded[1] == L"%7C|:");
    parsed.clear();
    CDataParser::ClientSplit(CDataParser::ServerMerge({{L"legacy", L"%7C|:"}}, false), parsed);
    CHECK(parsed[L"legacy"] == L"%7C|:");
    CIpcResponse response;
    parsed.clear();
    CDataParser::ClientSplit(response.getResponse(), parsed);
    CHECK(wcstol(parsed[L"resultCode"], nullptr, 10) == E_FAIL);
    std::puts("codec, Unicode and default result: passed");
}

static void BadRequest(CIpcServer& server, LPCWSTR name, DWORD declared,
                       const void* payload = nullptr, DWORD bytes = 0, DWORD headerBytes = 4)
{
    CPipeClient raw;
    CHECK(SUCCEEDED(raw.ConnectServer(name, 3)));
    DWORD count = 0;
    CHECK(SUCCEEDED(raw.SendToServer(&declared, headerBytes, &count)));
    if (payload) raw.SendToServer(payload, bytes, &count);
    DWORD reply = 0;
    CHECK(FAILED(raw.RecvFromServer(&reply, sizeof(reply), &count)));
    raw.DisconnectServer();
    CheckEcho(name);
}

static void ServerSafety()
{
    CString name = PipeName();
    CIpcServer server;
    CHECK(server.RegisterApi(L"echo", Echo) == S_OK);
    CHECK(server.Start(name, nullptr, -1) == S_OK);
    CheckEcho(name, L"ŌŎ 中文 |:%7C 😀");
    CHECK(server.Start(name, nullptr, 1) == S_FALSE);
    CIpcServer duplicate;
    CHECK(FAILED(duplicate.Start(name, nullptr, 1)));
    duplicate.Stop();
    CHECK(duplicate.Start(PipeName(), nullptr, 1) == S_OK);
    duplicate.Stop();

    for (DWORD length : std::vector<DWORD>{0u, 0xffffffffu, 0x40000000u, coral_ipc::MaxMessageChars + 1})
        BadRequest(server, name, length);
    const WCHAR shortBody[] = L"x";
    BadRequest(server, name, 100, shortBody, 2);
    BadRequest(server, name, 1, shortBody, sizeof(shortBody)); // 超长消息
    BadRequest(server, name, 1, nullptr, 0, 2); // 截断的前缀
    const WCHAR nulBody[] = {L'e', 0, L'x'};
    BadRequest(server, name, 3, nulBody, sizeof(nulBody));
    BadRequest(server, name, 20); // 客户端从不提供声明的消息体

    // 用最大合法帧在较小的管道缓冲间触发 ERROR_IO_PENDING。
    std::wstring large(coral_ipc::MaxMessageChars - 64, L'x');
    CheckEcho(name, large.c_str());
    CIpcClient oversize(name);
    oversize.Add(std::wstring(coral_ipc::MaxMessageChars + 1, L'x').c_str());
    CHECK(oversize.Call(L"echo") == HRESULT_FROM_WIN32(ERROR_BAD_LENGTH));

    // 旧客户端保持旧解释，包括字面百分号序列。
    CPipeClient legacy;
    CHECK(legacy.ConnectServer(name, 3) == S_OK);
    CString request = L"echo|%7CŌŎ";
    DWORD length = request.GetLength(), count;
    CHECK(legacy.SendToServer(&length, 4, &count) == S_OK);
    CHECK(legacy.SendToServer(request.GetString(), length * 2, &count) == S_OK);
    CHECK(legacy.RecvFromServer(&length, 4, &count) == S_OK);
    std::vector<WCHAR> reply(length + 1);
    CHECK(legacy.RecvFromServer(reply.data(), length * 2, &count) == S_OK);
    CHECK(!CDataParser::IsPercentEncoded(reply.data()));
    std::map<CString, CString> values;
    CDataParser::ClientSplit(reply.data(), values);
    CHECK(values[L"echo"] == L"%7C|:");
    legacy.DisconnectServer();

    // 并发演练公开的 RegisterApi 路径与工作线程查找。
    std::thread registrar([&] { for (int i = 0; i < 1000; ++i) CHECK(server.RegisterApi(L"echo", Echo) == S_OK); });
    for (int i = 0; i < 25; ++i) CheckEcho(name);
    registrar.join();

    std::vector<std::unique_ptr<CPipeClient>> idleClients;
    for (int i = 0; i < 16; ++i)
    {
        auto idle = std::make_unique<CPipeClient>();
        CHECK(idle->ConnectServer(name, 3) == S_OK);
        idleClients.push_back(std::move(idle));
    }
    const auto start = ::GetTickCount64();
    server.Stop(); // 在没有假客户端的情况下中断 accept 与空闲工作线程 I/O。
    CHECK(::GetTickCount64() - start < 1000);
    idleClients.clear();
    server.Stop();
    for (int i = 0; i < 10; ++i)
    {
        CHECK(server.Start(name, nullptr, 1) == S_OK);
        CheckEcho(name);
        server.Stop();
    }
    std::puts("server framing, pool, registration and restart: passed");
}

enum class FakeMode { Huge, Zero, ShortHeader, LongHeader, ShortBody, NoReply, NoRead, SlowReply, BadStatus };
class FakeServer
{
public:
    CString name = PipeName();
    coral_ipc::Event stop;
    HANDLE pipe;
    std::thread worker;
    FakeServer(FakeMode mode)
    {
        CString path = L"\\\\.\\PIPE\\" + name;
        pipe = ::CreateNamedPipeW(path, PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
                                  PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
                                  1, 4096, 4096, 0, nullptr);
        CHECK(pipe != INVALID_HANDLE_VALUE);
        worker = std::thread([this, mode] {
            if (FAILED(coral_ipc::Connect(pipe, stop.get()))) return;
            if (mode == FakeMode::NoRead) { ::WaitForSingleObject(stop.get(), 4000); return; }
            DWORD length = 0, count = 0;
            const auto deadline = ::GetTickCount64() + 4000;
            if (FAILED(coral_ipc::Transfer(pipe, &length, 4, count, false, deadline, stop.get()))) return;
            std::vector<WCHAR> body(length);
            if (FAILED(coral_ipc::Transfer(pipe, body.data(), length * 2, count, false, deadline, stop.get()))) return;
            if (mode == FakeMode::NoReply) { ::WaitForSingleObject(stop.get(), 4000); return; }
            DWORD header[2] = {mode == FakeMode::Huge ? 0xffffffffu : mode == FakeMode::Zero ? 0u : 10u, 0};
            DWORD headerBytes = mode == FakeMode::ShortHeader ? 2 : mode == FakeMode::LongHeader ? 8 : 4;
            CString reply = mode == FakeMode::BadStatus ? L"@coralipc2|resultCode:garbage" : L"@coralipc2|resultCode:0";
            if (mode == FakeMode::SlowReply || mode == FakeMode::BadStatus) header[0] = reply.GetLength();
            if (mode == FakeMode::SlowReply) ::WaitForSingleObject(stop.get(), 800);
            coral_ipc::Transfer(pipe, header, headerBytes, count, true, deadline, stop.get());
            if (mode == FakeMode::ShortBody)
            {
                WCHAR shortReply = L'x';
                coral_ipc::Transfer(pipe, &shortReply, 2, count, true, deadline, stop.get());
            }
            if (mode == FakeMode::SlowReply || mode == FakeMode::BadStatus)
            {
                if (mode == FakeMode::SlowReply) ::WaitForSingleObject(stop.get(), 800);
                coral_ipc::Transfer(pipe, const_cast<LPWSTR>(reply.GetString()), reply.GetLength() * 2,
                                    count, true, deadline, stop.get());
            }
            ::WaitForSingleObject(stop.get(), 4000);
        });
    }
    ~FakeServer()
    {
        ::SetEvent(stop.get());
        worker.join();
        ::DisconnectNamedPipe(pipe);
        ::CloseHandle(pipe);
    }
};

static void ClientSafety()
{
    for (FakeMode mode : {FakeMode::Huge, FakeMode::Zero, FakeMode::ShortHeader,
                          FakeMode::LongHeader, FakeMode::ShortBody, FakeMode::BadStatus})
    {
        FakeServer fake(mode);
        CIpcClient client(fake.name);
        CHECK(FAILED(client.Call(L"echo")));
    }
    for (FakeMode mode : {FakeMode::NoReply, FakeMode::NoRead, FakeMode::SlowReply})
    {
        FakeServer fake(mode);
        CIpcClient client(fake.name);
        if (mode == FakeMode::NoRead) client.Add(std::wstring(200000, L'x').c_str());
        const auto start = ::GetTickCount64();
        CHECK(client.Call(L"echo") == HRESULT_FROM_WIN32(ERROR_TIMEOUT));
        const auto elapsed = ::GetTickCount64() - start;
        CHECK(elapsed >= 1300 && elapsed < 2100);
    }
    {
        FakeServer fake(FakeMode::NoRead);
        CPipeClient blocker;
        CHECK(blocker.ConnectServer(fake.name, 3) == S_OK);
        CPipeClient client;
        const auto start = ::GetTickCount64();
        CHECK(client.ConnectServer(fake.name, 3) == HRESULT_FROM_WIN32(ERROR_TIMEOUT));
        CHECK(::GetTickCount64() - start < 2100);
    }
    CIpcClient missing(PipeName());
    const auto start = ::GetTickCount64();
    CHECK(FAILED(missing.Call(L"echo")));
    CHECK(::GetTickCount64() - start < 500);
    std::puts("client malformed replies, blocked read/write and shared deadline: passed");
}

class InitServer : public CIpcServerImpl<InitServer>
{
public:
    int cleanup = 0;
    bool failRegistration = false;
    CString expectedPipe;
    HRESULT RegisterApi() override
    {
        if (!expectedPipe.IsEmpty())
        {
            CString path = L"\\\\.\\PIPE\\" + expectedPipe;
            HANDLE handle = ::CreateFileW(path, GENERIC_READ | GENERIC_WRITE, 0,
                                           nullptr, OPEN_EXISTING, 0, nullptr);
            CHECK(handle == INVALID_HANDLE_VALUE && ::GetLastError() == ERROR_FILE_NOT_FOUND);
        }
        CHECK(m_server.RegisterApi(L"echo", Echo) == S_OK);
        return failRegistration ? E_FAIL : S_OK;
    }
    void DoUnInit() override { ++cleanup; }
};

static void InitLifecycle()
{
    CString name = PipeName();
    InitServer server;
    server.expectedPipe = name;
    server.failRegistration = true;
    CHECK(server.Init(name) == E_FAIL && server.cleanup == 1);
    server.failRegistration = false;
    CHECK(server.Init(name) == S_OK);
    CheckEcho(name);
    CHECK(server.Init(name) == S_FALSE);
    InitServer duplicate;
    CHECK(FAILED(duplicate.Init(name)) && duplicate.cleanup == 1);
    server.Uninit();
    CHECK(server.cleanup == 2);
    server.Uninit();
    CHECK(server.cleanup == 2);
    std::puts("Init/Uninit error propagation and cleanup: passed");
}

int main()
{
    Codec();
    InitLifecycle();
    ServerSafety();
    ClientSafety();
    std::puts("All coralipc tests passed.");
}

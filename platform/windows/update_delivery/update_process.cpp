#include "update_process.h"
#include "coral/product/product_identity.h"
#include <windows.h>
#include <tlhelp32.h>
#include <bcrypt.h>
#include <algorithm>
#include <cstring>
#include <limits>
#include <string>

namespace coral::updates {
namespace {
class Handle {
public:
    explicit Handle(HANDLE value = nullptr) : value_(value) {}
    ~Handle() { reset(); }
    Handle(const Handle&) = delete; Handle& operator=(const Handle&) = delete;
    HANDLE get() const noexcept { return value_; }
    bool valid() const noexcept { return value_ && value_ != INVALID_HANDLE_VALUE; }
    void reset(HANDLE value = nullptr) noexcept { if (valid()) ::CloseHandle(value_); value_ = value; }
    HANDLE release() noexcept { const auto value = value_; value_ = nullptr; return value; }
private: HANDLE value_;
};
class View {
public:
    ~View() { if (data_) ::UnmapViewOfFile(data_); }
    View() = default; View(const View&) = delete; View& operator=(const View&) = delete;
    void* get() const noexcept { return data_; }
    bool map(HANDLE handle, DWORD access, std::size_t bytes) { data_ = ::MapViewOfFile(handle, access, 0, 0, bytes); return data_ != nullptr; }
private: void* data_ = nullptr;
};
Handle duplicate(HANDLE source, DWORD rights) {
    HANDLE result = nullptr;
    if (!::DuplicateHandle(::GetCurrentProcess(), source, ::GetCurrentProcess(), &result, rights, TRUE, 0)) return Handle{};
    return Handle{result};
}
class AttributeList {
public:
    ~AttributeList() { if (initialized_) ::DeleteProcThreadAttributeList(get()); }
    LPPROC_THREAD_ATTRIBUTE_LIST get() { return reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(buffer_.data()); }
    bool initialize(HANDLE* handles, std::size_t bytes) {
        SIZE_T size = 0; ::InitializeProcThreadAttributeList(nullptr, 1, 0, &size); buffer_.resize(size);
        initialized_ = ::InitializeProcThreadAttributeList(get(), 1, 0, &size) != FALSE;
        return initialized_ && ::UpdateProcThreadAttribute(get(), 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, handles, bytes, nullptr, nullptr);
    }
private: std::vector<BYTE> buffer_; bool initialized_ = false;
};
std::filesystem::path module_path() {
    for (DWORD capacity = 512; capacity <= 32768; capacity *= 2) {
        std::vector<wchar_t> text(capacity);
        const auto length = ::GetModuleFileNameW(nullptr, text.data(), capacity);
        if (!length) return {};
        if (length < capacity) return std::wstring(text.data(), length);
    }
    return {};
}
bool parse_handle(std::wstring_view text, HANDLE& result) {
    if (text.empty() || (text.size() > 1 && text[0] == L'0')) return false;
    std::uint64_t value = 0;
    for (const auto character : text) {
        if (character < L'0' || character > L'9') return false;
        const auto digit = static_cast<std::uint64_t>(character - L'0');
        if (value > (static_cast<std::uint64_t>(UINTPTR_MAX) - digit) / 10) return false;
        value = value * 10 + digit;
    }
    if (!value || value == UINTPTR_MAX) return false;
    result = reinterpret_cast<HANDLE>(static_cast<std::uintptr_t>(value)); return true;
}
bool actual_parent(HANDLE parent, DWORD& parent_id) {
    parent_id = ::GetProcessId(parent);
    DWORD parent_session = 0, current_session = 0;
    if (!parent_id || parent_id == ::GetCurrentProcessId() || ::WaitForSingleObject(parent, 0) != WAIT_TIMEOUT
        || !::ProcessIdToSessionId(parent_id, &parent_session) || !::ProcessIdToSessionId(::GetCurrentProcessId(), &current_session)
        || parent_session != current_session) return false;
    Handle snapshot(::CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0));
    if (!snapshot.valid()) return false;
    PROCESSENTRY32W entry{sizeof(entry)};
    for (BOOL more = ::Process32FirstW(snapshot.get(), &entry); more; more = ::Process32NextW(snapshot.get(), &entry))
        if (entry.th32ProcessID == ::GetCurrentProcessId()) return entry.th32ParentProcessID == parent_id;
    return false;
}
}

struct UpdateProcess::State {
    DownloadRequest request;
    std::unique_ptr<ProgressTracker> tracker;
    DownloadProgress failure;
    bool failed = false, forced = false;
    Handle process, job, mutex, cancel_event;
    View progress;
    DWORD process_id = 0;
    void fail(DownloadError error) {
        if (failed) return;
        failure = tracker->current().value_or(DownloadProgress{}); failure.nonce = request.nonce;
        failure.stage = DownloadStage::kFailed; failure.error = error; failure.percent = (std::min)(failure.percent, 99u);
        if (failure.sequence != UINT64_MAX) ++failure.sequence;
        failed = true; ::SetEvent(cancel_event.get());
    }
};
UpdateProcess::UpdateProcess(std::unique_ptr<State> state) : state_(std::move(state)) {}
UpdateProcess::~UpdateProcess() { stop(); }
std::filesystem::path adjacent_update_agent() {
    const auto image = module_path(); return image.empty() ? std::filesystem::path{} : image.parent_path() / product::kUpdateExeWide;
}
std::unique_ptr<UpdateProcess> UpdateProcess::start(const std::filesystem::path& executable, const ReleaseMetadata& release, UpdateLaunchFailure* failure) {
    UpdateLaunchStage stage = UpdateLaunchStage::kPath;
    const auto failed = [&](DWORD error = ::GetLastError()) -> std::unique_ptr<UpdateProcess> {
        if (failure) *failure = {stage, error ? error : ERROR_GEN_FAILURE};
        return {};
    };
    try {
        const auto path = executable.wstring(); std::error_code error;
        if (!executable.is_absolute() || path.size() > 32000 || path.find_first_of(L"\"\r\n") != std::wstring::npos
            || path.find(L'\0') != std::wstring::npos || !std::filesystem::is_regular_file(executable, error) || error) return failed(ERROR_INVALID_PARAMETER);
        stage = UpdateLaunchStage::kRequest;
        auto state = std::make_unique<State>(); state->request.release = release;
        if (::BCryptGenRandom(nullptr, state->request.nonce.data(), static_cast<ULONG>(state->request.nonce.size()), BCRYPT_USE_SYSTEM_PREFERRED_RNG) < 0) return failed();
        RequestFrame request;
        if (!encode_download_request(state->request, request)) return failed(ERROR_INVALID_DATA);
        state->tracker = std::make_unique<ProgressTracker>(state->request.nonce);
        DownloadProgress initial{state->request.nonce, DownloadStage::kStarting, 0, DownloadError::kNone, 1};
        state->tracker->accept(initial); ProgressFrame progress; encode_download_progress(initial, progress);
        stage = UpdateLaunchStage::kMapping;
        Handle request_mapping(::CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, kDownloadRequestBytes, nullptr));
        Handle progress_mapping(::CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, kDownloadProgressBytes, nullptr));
        if (!request_mapping.valid() || !progress_mapping.valid()) return failed();
        {
            View request_view, progress_view;
            if (!request_view.map(request_mapping.get(), FILE_MAP_WRITE, request.size()) || !progress_view.map(progress_mapping.get(), FILE_MAP_WRITE, progress.size())) return failed();
            std::memcpy(request_view.get(), request.data(), request.size()); std::memcpy(progress_view.get(), progress.data(), progress.size());
        }
        if (!state->progress.map(progress_mapping.get(), FILE_MAP_READ, progress.size())) return failed();
        stage = UpdateLaunchStage::kHandles;
        state->mutex.reset(::CreateMutexW(nullptr, FALSE, nullptr)); state->cancel_event.reset(::CreateEventW(nullptr, TRUE, FALSE, nullptr));
        Handle parent(::OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, ::GetCurrentProcessId()));
        if (!state->mutex.valid() || !state->cancel_event.valid() || !parent.valid()) return failed();
        auto child_request = duplicate(request_mapping.get(), FILE_MAP_READ);
        auto child_progress = duplicate(progress_mapping.get(), FILE_MAP_READ | FILE_MAP_WRITE);
        auto child_mutex = duplicate(state->mutex.get(), SYNCHRONIZE | MUTEX_MODIFY_STATE);
        auto child_cancel = duplicate(state->cancel_event.get(), SYNCHRONIZE);
        auto child_parent = duplicate(parent.get(), SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION);
        HANDLE inherited[]{child_request.get(), child_progress.get(), child_mutex.get(), child_cancel.get(), child_parent.get()};
        if (std::any_of(std::begin(inherited), std::end(inherited), [](HANDLE handle) { return !handle; })) return failed();
        request_mapping.reset(); progress_mapping.reset(); // 不再剩有任何可写的请求句柄。
        stage = UpdateLaunchStage::kAttributes;
        AttributeList attributes; if (!attributes.initialize(inherited, sizeof(inherited))) return failed();
        stage = UpdateLaunchStage::kJob;
        state->job.reset(::CreateJobObjectW(nullptr, nullptr));
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{}; limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        if (!state->job.valid() || !::SetInformationJobObject(state->job.get(), JobObjectExtendedLimitInformation, &limits, sizeof(limits))) return failed();
        constexpr const wchar_t* keys[]{L" --request=", L" --progress=", L" --mutex=", L" --cancel=", L" --parent="};
        std::wstring command = L"\"" + path + L"\" --coral-update=1";
        for (unsigned index = 0; index < 5; ++index) command += keys[index] + std::to_wstring(reinterpret_cast<std::uintptr_t>(inherited[index]));
        STARTUPINFOEXW startup{}; startup.StartupInfo.cb = sizeof(startup); startup.lpAttributeList = attributes.get();
        PROCESS_INFORMATION child{};
        stage = UpdateLaunchStage::kCreate;
        if (!::CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, TRUE,
            CREATE_NO_WINDOW | CREATE_SUSPENDED | EXTENDED_STARTUPINFO_PRESENT, nullptr, executable.parent_path().c_str(), &startup.StartupInfo, &child)) return failed();
        state->process.reset(child.hProcess); state->process_id = child.dwProcessId; Handle thread(child.hThread);
        stage = UpdateLaunchStage::kAssign;
        if (!::AssignProcessToJobObject(state->job.get(), state->process.get())) {
            (void)failed();
            ::TerminateProcess(state->process.get(), 1); ::WaitForSingleObject(state->process.get(), 2000); return {};
        }
        stage = UpdateLaunchStage::kResume;
        if (::ResumeThread(thread.get()) == static_cast<DWORD>(-1)) return failed();
        return std::unique_ptr<UpdateProcess>(new UpdateProcess(std::move(state)));
    } catch (...) { return failed(ERROR_NOT_ENOUGH_MEMORY); }
}
DownloadProgress UpdateProcess::poll() {
    if (state_->failed) return state_->failure;
    const auto read_snapshot = [&] {
        if (state_->failed) return;
        const auto locked = ::WaitForSingleObject(state_->mutex.get(), 0);
        if (locked == WAIT_OBJECT_0) {
            ProgressFrame frame{}; std::memcpy(frame.data(), state_->progress.get(), frame.size()); ::ReleaseMutex(state_->mutex.get());
            DownloadProgress progress;
            if (!decode_download_progress(frame.data(), frame.size(), progress) || state_->tracker->accept(progress) == ProgressAcceptance::kRejected)
                state_->fail(DownloadError::kChannelFailed);
        } else if (locked == WAIT_ABANDONED) {
            ::ReleaseMutex(state_->mutex.get()); state_->fail(DownloadError::kChannelFailed);
        } else if (locked != WAIT_TIMEOUT) state_->fail(DownloadError::kChannelFailed);
    };
    read_snapshot();
    const auto exited = ::WaitForSingleObject(state_->process.get(), 0);
    if (exited == WAIT_OBJECT_0) {
        // 在我们首次读取与进程退出之间，子进程可能发布其最终帧。
        // 先观察退出再重读一次，避免把这种正常竞争当成失败。
        read_snapshot();
    }
    auto progress = state_->tracker->current().value();
    if (exited == WAIT_OBJECT_0) {
        DWORD code = 1;
        if (!::GetExitCodeProcess(state_->process.get(), &code) || !terminal_stage(progress.stage)
            || (progress.stage == DownloadStage::kReady && code != 0)) state_->fail(DownloadError::kAgentExited);
    } else if (exited == WAIT_FAILED) state_->fail(DownloadError::kAgentExited);
    else if (progress.stage == DownloadStage::kReady) {
        progress.stage = DownloadStage::kPublishing; progress.percent = 95; --progress.sequence;
    }
    return state_->failed ? state_->failure : progress;
}
void UpdateProcess::cancel() noexcept { ::SetEvent(state_->cancel_event.get()); }
bool UpdateProcess::stop() noexcept {
    cancel();
    if (::WaitForSingleObject(state_->process.get(), 2000) == WAIT_TIMEOUT) {
        state_->forced = true; ::TerminateJobObject(state_->job.get(), 1);
        ::WaitForSingleObject(state_->process.get(), 2000);
    }
    return state_->forced;
}
bool UpdateProcess::wait_for_exit(std::uint32_t milliseconds) const noexcept { return ::WaitForSingleObject(state_->process.get(), milliseconds) == WAIT_OBJECT_0; }
std::uint32_t UpdateProcess::child_id() const noexcept { return state_->process_id; }
const DownloadRequest& UpdateProcess::request() const noexcept { return state_->request; }

struct InheritedUpdateChannel::State {
    Handle request_mapping, progress_mapping, mutex, cancel_event, parent;
    View request_view, progress_view;
    DownloadRequest request;
    DWORD parent_id = 0;
};
InheritedUpdateChannel::InheritedUpdateChannel(std::unique_ptr<State> state) : state_(std::move(state)) {}
InheritedUpdateChannel::~InheritedUpdateChannel() = default;
std::unique_ptr<InheritedUpdateChannel> InheritedUpdateChannel::open(const std::vector<std::wstring_view>& arguments) {
    if (arguments.size() != 6) return {};
    constexpr std::wstring_view keys[]{L"--request", L"--progress", L"--mutex", L"--cancel", L"--parent"};
    HANDLE handles[5]{}; unsigned seen = 0; bool version = false;
    for (const auto argument : arguments) {
        const auto equal = argument.find(L'='); if (equal == std::wstring_view::npos) return {};
        const auto key = argument.substr(0, equal), value = argument.substr(equal + 1);
        if (key == L"--coral-update") { if (version || value != L"1") return {}; version = true; continue; }
        bool found = false;
        for (unsigned index = 0; index < 5; ++index) if (key == keys[index]) {
            if ((seen & (1u << index)) || !parse_handle(value, handles[index])) return {};
            seen |= 1u << index; found = true; break;
        }
        if (!found) return {};
    }
    if (!version || seen != 31) return {};
    for (unsigned index = 0; index < 5; ++index) {
        DWORD flags = 0; if (!::GetHandleInformation(handles[index], &flags)) return {};
        for (unsigned previous = 0; previous < index; ++previous) if (handles[index] == handles[previous]) return {};
    }
    auto state = std::make_unique<State>();
    state->request_mapping.reset(handles[0]); state->progress_mapping.reset(handles[1]); state->mutex.reset(handles[2]);
    state->cancel_event.reset(handles[3]); state->parent.reset(handles[4]);
    if (!actual_parent(state->parent.get(), state->parent_id) || !state->request_view.map(state->request_mapping.get(), FILE_MAP_READ, kDownloadRequestBytes)
        || !state->progress_view.map(state->progress_mapping.get(), FILE_MAP_WRITE, kDownloadProgressBytes)
        || !decode_download_request(static_cast<const std::uint8_t*>(state->request_view.get()), kDownloadRequestBytes, state->request)) return {};
    return std::unique_ptr<InheritedUpdateChannel>(new InheritedUpdateChannel(std::move(state)));
}
bool InheritedUpdateChannel::cancelled() const noexcept {
    return ::WaitForSingleObject(state_->cancel_event.get(), 0) != WAIT_TIMEOUT || ::WaitForSingleObject(state_->parent.get(), 0) != WAIT_TIMEOUT;
}
bool InheritedUpdateChannel::report(const DownloadProgress& progress) noexcept {
    ProgressFrame frame;
    if (progress.nonce != state_->request.nonce || !encode_download_progress(progress, frame)) return false;
    const auto locked = ::WaitForSingleObject(state_->mutex.get(), 50);
    if (locked == WAIT_ABANDONED) { ::ReleaseMutex(state_->mutex.get()); return false; }
    if (locked != WAIT_OBJECT_0) return false;
    std::memcpy(state_->progress_view.get(), frame.data(), frame.size()); return ::ReleaseMutex(state_->mutex.get()) != FALSE;
}
const DownloadRequest& InheritedUpdateChannel::request() const noexcept { return state_->request; }
std::uint32_t InheritedUpdateChannel::parent_id() const noexcept { return state_->parent_id; }
}

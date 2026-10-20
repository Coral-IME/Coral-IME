#include "update_process.h"
#include "package_acceptance.h"
#include "coral/updates/download_job.h"
#include <windows.h>
#include <bcrypt.h>
#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
using namespace coral::updates;
void check(bool value,unsigned line) { if(!value) throw std::runtime_error("Private update process fixture failed at line "+std::to_string(line)+", Win32="+std::to_string(::GetLastError())); }
#define require(value) check((value),__LINE__)
std::wstring environment(const wchar_t* key) {
    const DWORD size=::GetEnvironmentVariableW(key,nullptr,0); if(!size) return {};
    std::wstring value(size,L'\0'); const auto count=::GetEnvironmentVariableW(key,value.data(),size);
    require(count && count<size); value.resize(count); return value;
}
HANDLE argument_handle(const std::vector<std::wstring_view>& arguments,std::wstring_view key) {
    for(const auto argument:arguments) if(argument.substr(0,key.size())==key)
        return reinterpret_cast<HANDLE>(static_cast<std::uintptr_t>(std::stoull(std::wstring(argument.substr(key.size())))));
    return nullptr;
}
std::string nonce_text(const RequestNonce& nonce) {
    constexpr char digits[]="0123456789abcdef"; std::string text;
    for(auto byte:nonce) { text+=digits[byte>>4]; text+=digits[byte&15]; } return text;
}
ReleaseMetadata release(std::uint32_t id=7) {
    ReleaseMetadata value;
    require(parse_release_metadata("{\"data\":{\"id\":7,\"version\":\"1.2.3.4\",\"fileSize\":3,\"fileSha256\":\""
        "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad\"}}",value));
    value.release_id=id; value.version.components[3]=static_cast<std::uint16_t>(id); return value;
}
class LabActions final : public DownloadActions {
public:
    LabActions(InheritedUpdateChannel& channel,std::filesystem::path root,std::wstring mode)
        : channel_(channel),root_(std::move(root)),mode_(std::move(mode)),nonce_(nonce_text(channel.request().nonce)) {}
    bool cancelled() override { return channel_.cancelled(); }
    bool report(const DownloadProgress& value) override { return channel_.report(value); }
    LocationReply locate(std::uint32_t id) override { require(id==channel_.request().release.release_id); return {DownloadError::kNone,"{\"data\":{\"url\":\"https://example.test/pinned\"}}"}; }
    std::filesystem::path reserve_pending() override { return root_/(nonce_+".pending"); }
    DownloadError download(const std::string&,const std::filesystem::path& pending,std::uint64_t limit,const std::function<bool(std::uint32_t)>& progress) override {
        require(limit==3); progress(40);
        if(mode_==L"cancel") { while(!cancelled()) ::Sleep(5); return DownloadError::kCancelled; }
        { std::ofstream file(pending,std::ios::binary); file<<(mode_==L"mismatch"?"abd":"abc"); }
        return DownloadError::kNone;
    }
    bool verify(const std::filesystem::path& pending,const PackageFingerprint& fingerprint) override { return verify_package(pending,fingerprint)==PackageStatus::kReady; }
    bool publish(const std::filesystem::path& pending,const ReleaseMetadata& metadata) override {
        return publish_package(pending,root_/std::filesystem::u8path(nonce_+"_"+format_release_version(metadata.version)+".bin"),metadata.package)==PackageStatus::kReady;
    }
    void discard(const std::filesystem::path& pending) noexcept override { std::error_code error; std::filesystem::remove(pending,error); }
private: InheritedUpdateChannel& channel_; std::filesystem::path root_; std::wstring mode_; std::string nonce_;
};
int child(std::vector<std::wstring_view> arguments) {
    const auto mode=environment(L"CORAL_UPDATE_PROBE_CASE"); const auto root=std::filesystem::path(environment(L"CORAL_UPDATE_PROBE_ROOT"));
    require(root.is_absolute());
    if(mode==L"bad_cli") { arguments[0]=L"--coral-update=2"; return InheritedUpdateChannel::open(arguments)?5:3; }
    std::wstring self_argument;
    if(mode==L"foreign_parent") {
        const auto self=::OpenProcess(SYNCHRONIZE|PROCESS_QUERY_LIMITED_INFORMATION,FALSE,::GetCurrentProcessId()); require(self!=nullptr);
        self_argument=L"--parent="+std::to_wstring(reinterpret_cast<std::uintptr_t>(self));
        for(auto& argument:arguments) if(argument.substr(0,9)==L"--parent=") argument=self_argument;
        return InheritedUpdateChannel::open(arguments)?5:3;
    }
    auto channel=InheritedUpdateChannel::open(arguments); if(!channel) return 3;
    void* writable=::MapViewOfFile(argument_handle(arguments,L"--request="),FILE_MAP_WRITE,0,0,kDownloadRequestBytes);
    require(!writable); if(writable) ::UnmapViewOfFile(writable);
    require(!::SetEvent(argument_handle(arguments,L"--cancel="))); // 子进程只具备等待能力。
    const auto excluded=environment(L"CORAL_UPDATE_PROBE_EXCLUDED");
    if(!excluded.empty()) {
        const auto handle=reinterpret_cast<HANDLE>(static_cast<std::uintptr_t>(std::stoull(excluded)));
        wchar_t name[32768]{}; const auto count=::GetFinalPathNameByHandleW(handle,name,32768,0);
        require(!count || std::wstring(name,count).find(L"excluded_file.txt")==std::wstring::npos);
    }
    const auto nonce=nonce_text(channel->request().nonce);
    { std::ofstream file(root/(nonce+".json")); file<<"{\"id\":"<<channel->request().release.release_id<<",\"parent_id\":"<<channel->parent_id()
        <<",\"version\":\""<<format_release_version(channel->request().release.version)<<"\",\"readonly_request\":true,\"wait_only_cancel\":true,\"extra_not_inherited\":true}"; }
    if(mode==L"hang") { ::Sleep(10000); return 4; }
    if(mode==L"die") return 4;
    if(mode==L"bad_nonce") {
        DownloadProgress progress{channel->request().nonce,DownloadStage::kDownloading,40,DownloadError::kNone,3}; progress.nonce[0]^=1;
        ProgressFrame frame; require(encode_download_progress(progress,frame));
        const auto mutex=argument_handle(arguments,L"--mutex="); require(::WaitForSingleObject(mutex,1000)==WAIT_OBJECT_0);
        void* memory=::MapViewOfFile(argument_handle(arguments,L"--progress="),FILE_MAP_WRITE,0,0,frame.size()); require(memory!=nullptr);
        std::memcpy(memory,frame.data(),frame.size()); ::UnmapViewOfFile(memory); ::ReleaseMutex(mutex);
        while(!channel->cancelled()) ::Sleep(5); return 1;
    }
    LabActions actions(*channel,root,mode);
    return execute_download(channel->request(),actions)==DownloadError::kNone?0:1;
}
DownloadProgress until_terminal(UpdateProcess& process) {
    const auto deadline=::GetTickCount64()+5000;
    for(;;) { const auto status=process.poll(); if(terminal_stage(status.stage)) return status; require(::GetTickCount64()<deadline); ::Sleep(5); }
}
}
int wmain(int argc,wchar_t** argv) {
    try {
        if(argc>1 && std::wstring_view(argv[1])==L"--coral-update=1") {
            std::vector<std::wstring_view> arguments; for(int index=1;index<argc;++index) arguments.emplace_back(argv[index]); return child(std::move(arguments));
        }
        require(argc==5); const std::filesystem::path executable=std::filesystem::absolute(argv[1]),root=std::filesystem::absolute(argv[2]);
        const std::wstring mode=argv[3]; const std::uint32_t count=static_cast<std::uint32_t>(std::stoul(argv[4]));
        std::filesystem::create_directories(root);
        ::SetEnvironmentVariableW(L"CORAL_UPDATE_PROBE_CASE",mode.c_str()); ::SetEnvironmentVariableW(L"CORAL_UPDATE_PROBE_ROOT",root.c_str());
        SECURITY_ATTRIBUTES inheritable{sizeof(inheritable),nullptr,TRUE};
        HANDLE excluded=::CreateFileW((root/L"excluded_file.txt").c_str(),GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,&inheritable,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
        require(excluded!=INVALID_HANDLE_VALUE);
        ::SetEnvironmentVariableW(L"CORAL_UPDATE_PROBE_EXCLUDED",std::to_wstring(reinterpret_cast<std::uintptr_t>(excluded)).c_str());
        UCHAR warmup[16]{}; require(::BCryptGenRandom(nullptr,warmup,sizeof(warmup),BCRYPT_USE_SYSTEM_PREFERRED_RNG)>=0);
        // CreateProcess 首次调用时会初始化进程级的 Windows 设施。
        // 在这次一次性初始化之后，比较反复完全销毁的请求。
        const auto warm_root=root/L"runtime_warmup"; std::filesystem::create_directories(warm_root);
        ::SetEnvironmentVariableW(L"CORAL_UPDATE_PROBE_CASE",L"ok");
        ::SetEnvironmentVariableW(L"CORAL_UPDATE_PROBE_ROOT",warm_root.c_str());
        {
            auto process=UpdateProcess::start(executable,release()); require(process!=nullptr);
            require(until_terminal(*process).stage==DownloadStage::kReady); require(!process->stop());
        }
        ::SetEnvironmentVariableW(L"CORAL_UPDATE_PROBE_CASE",mode.c_str());
        ::SetEnvironmentVariableW(L"CORAL_UPDATE_PROBE_ROOT",root.c_str());
        DWORD before=0,after=0; require(::GetProcessHandleCount(::GetCurrentProcess(),&before));
        unsigned forced=0;
        if(mode==L"parallel") {
            auto first=UpdateProcess::start(executable,release(7)),second=UpdateProcess::start(executable,release(8)); require(first && second);
            require(first->request().nonce!=second->request().nonce);
            require(until_terminal(*first).stage==DownloadStage::kReady && until_terminal(*second).stage==DownloadStage::kReady);
            require(!first->stop() && !second->stop());
        } else {
            for(std::uint32_t index=0;index<count;++index) {
                UpdateLaunchFailure failure;
                auto process=UpdateProcess::start(executable,release(),&failure);
                if(!process) throw std::runtime_error("launch stage="+std::to_string(static_cast<unsigned>(failure.stage))+", error="+std::to_string(failure.error));
                if(mode==L"parent_death") {
                    { std::ofstream file(root/"child_id.txt"); file<<process->child_id(); }
                    ::ExitProcess(0); // 不运行 C++ 析构函数直接关闭父进程持有的作业。
                }
                if(mode==L"cancel" || mode==L"hang") {
                    ::Sleep(100); process->cancel(); const auto hard=process->stop(); forced+=hard;
                    require(hard==(mode==L"hang")); require(process->wait_for_exit(1000));
                    if(mode==L"cancel") require(process->poll().stage==DownloadStage::kCancelled);
                } else {
                    const auto status=until_terminal(*process);
                    if(mode==L"ok") {
                        if(status.stage!=DownloadStage::kReady) throw std::runtime_error("unexpected stage="+std::to_string(static_cast<unsigned>(status.stage))
                            +", error="+std::to_string(static_cast<unsigned>(status.error))+", percent="+std::to_string(status.percent)+", sequence="+std::to_string(status.sequence));
                    } else {
                        require(status.stage==DownloadStage::kFailed);
                        if(mode==L"mismatch") require(status.error==DownloadError::kVerificationFailed);
                        if(mode==L"bad_nonce") require(status.error==DownloadError::kChannelFailed);
                        if(mode==L"die" || mode==L"bad_cli" || mode==L"foreign_parent") require(status.error==DownloadError::kAgentExited);
                    }
                    require(!process->stop());
                }
            }
        }
        require(::GetProcessHandleCount(::GetCurrentProcess(),&after));
        if(after>before+2) throw std::runtime_error("handle count before="+std::to_string(before)+", after="+std::to_string(after));
        ::CloseHandle(excluded);
        std::cout<<"{\"passed\":true,\"parent_id\":"<<::GetCurrentProcessId()<<",\"forced_owned_jobs\":"<<forced<<",\"handles_before\":"<<before<<",\"handles_after\":"<<after<<"}\n";
    } catch(const std::exception& error) {
        const auto root=environment(L"CORAL_UPDATE_PROBE_ROOT");
        if(!root.empty()) { std::ofstream file(std::filesystem::path(root)/(L"error_"+std::to_wstring(::GetCurrentProcessId())+L".txt")); file<<error.what(); }
        std::cerr<<error.what()<<'\n'; return 1;
    }
    return 0;
}

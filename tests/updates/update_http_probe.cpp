#include "update_http.h"
#include <windows.h>
#include <atomic>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <thread>

int wmain(int argc,wchar_t** argv) {
    using namespace coral::updates;
    try {
        if(argc!=5) return 2;
        const std::wstring mode=argv[1]; const std::wstring wide_url=argv[2]; std::string url;
        for(const auto character:wide_url) { if(character<0 || character>127) return 2; url+=static_cast<char>(character); }
        const std::filesystem::path file=std::filesystem::absolute(argv[3]); const auto limit=std::stoull(argv[4]);
        UpdateHttp http(mode==L"production"?HttpPolicy::kHttpsOnly:HttpPolicy::kLoopbackTest);
        std::atomic<bool> cancel{false}; std::thread canceller;
        if(mode==L"cancel") canceller=std::thread([&] { ::Sleep(150); cancel.store(true); });
        const auto begin=::GetTickCount64();
        HttpReply reply;
        if(mode==L"save") reply=http.save(url,file,limit,[&] { return cancel.load(); },[](std::uint32_t) { return true; });
        else reply=http.get(url,[&] { return cancel.load(); },mode==L"timeout"?200:5000);
        const auto elapsed=::GetTickCount64()-begin;
        if(canceller.joinable()) canceller.join();
        if(mode==L"callback") {
            auto request=MetadataRequest::start(url,[](HttpReply) { throw std::runtime_error("artificial callback exception"); });
            while(!request->finished()) ::Sleep(1);
            if(!request->callback_failed()) return 3;
        }
        std::cout<<"{\"error\":"<<static_cast<unsigned>(reply.error)<<",\"bytes\":"<<reply.bytes<<",\"body_size\":"<<reply.body.size()<<",\"elapsed_ms\":"<<elapsed<<"}\n";
    } catch(const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
    return 0;
}

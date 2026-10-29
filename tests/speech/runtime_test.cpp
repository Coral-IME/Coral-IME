#include "speech_runtime.h"
#include "candidate_theme.h"
#include "soft_canvas.h"
#include <imm.h>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <thread>
using namespace coral::speech;
using namespace coral::speech::win32;
namespace {
void require(bool condition, const char* reason) { if (!condition) throw std::runtime_error(reason); }
std::wstring text(HWND window) { wchar_t value[2048]{}; GetWindowTextW(window,value,2048); return value; }
void wait(SpeechRuntime& runtime,unsigned ms) {
    auto start=GetTickCount64();
    while(GetTickCount64()-start<ms) {
        MSG message{};
        while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE))
            if(!runtime.process_message(message)){TranslateMessage(&message);DispatchMessageW(&message);}
        MsgWaitForMultipleObjects(0,nullptr,FALSE,5,QS_ALLINPUT);
    }
}
void pump(SpeechRuntime& runtime,const std::function<bool()>& ready) {
    auto start=GetTickCount64();
    while(!ready() && GetTickCount64()-start<3500) wait(runtime,10);
    require(ready(),"Speech UI wait timed out");
}
void save(HWND window,const std::filesystem::path& path) {
    RECT rect{};GetClientRect(window,&rect);
    coral::win32::soft::Canvas canvas(rect.right,rect.bottom);
    SendMessageW(window,WM_PRINTCLIENT,reinterpret_cast<WPARAM>(canvas.dc()),PRF_CLIENT);GdiFlush();
    coral::win32::SkinImage image;image.width=canvas.width();image.height=canvas.height();
    image.pixels.assign(canvas.pixels(),canvas.pixels()+canvas.count());
    for(auto& pixel:image.pixels)pixel|=0xff000000;
    require(coral::win32::SaveSkinImagePng(image,path.wstring()),"Cannot save speech preview");
}
}
int main(int argc,char** argv) {
    try {
        SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        const bool installed_ime=argc>2&&std::string(argv[2])=="interactive-ime";
        // 默认只验证语音适配，避免加载现用输入法或改动其用户数据；兼容性检查单独显式运行。
        if(!installed_ime)require(ImmDisableIME(GetCurrentThreadId())!=FALSE,"Cannot isolate installed IME from owned fixtures");
        CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
        auto output=std::filesystem::path(argc>1?argv[1]:"speech-renders");std::filesystem::create_directories(output);
        if(argc>2 && std::string(argv[2])=="microphone") {
            auto info=query_microphone();
            std::wcout<<(info.available?L"Microphone available: ":L"Microphone unavailable: ")<<info.message<<'\n';
            if(info.available){std::atomic<bool> stop{false},cancelled{false};auto result=capture_microphone(stop,cancelled,1,[](CaptureProgress){});
                require(result.error.empty()&&!result.audio.bytes.empty(),"Real microphone capture failed");}
            CoUninitialize();return 0;
        }
        bool interactive=installed_ime||(argc>2 && std::string(argv[2])=="interactive");
        SpeechSettings settings;TencentCredentials credentials;
        std::atomic<unsigned> captures{0},recognitions{0};std::atomic<bool> recording{false},late{false},recognizing{false};
        bool microphone=true,clipboard_busy=false,empty=false,network_failure=false;std::wstring copied;unsigned copies=0;
        SpeechServices services;services.instance_name=L"CoralSpeechTest-"+std::to_wstring(GetCurrentProcessId());
        services.enable_keyboard=false;services.enable_text_insertion=interactive;
        services.settings=[&]{return settings;};services.credentials=[&]{return credentials;};
        services.copy_text=[&](const std::wstring& value){if(clipboard_busy)return false;copied=value;++copies;return true;};
        services.microphone=[&]{return MicrophoneInfo{microphone,L"Test",L"No test microphone"};};
        services.capture=[&](const std::atomic<bool>& stop,const std::atomic<bool>& cancelled,unsigned seconds,const std::function<void(CaptureProgress)>& progress){
            ++captures;recording=true;progress({1200,45});auto started=GetTickCount64();
            while(!stop&&!cancelled&&!(seconds==5&&GetTickCount64()-started>100))std::this_thread::sleep_for(std::chrono::milliseconds(5));
            CaptureResult result;result.audio.format=AudioFormat::kPcm;result.audio.bytes={0,0};result.peak_level=45;
            result.cancelled=cancelled;recording=false;return result;
        };
        services.recognize=[&](const AudioClip& audio,const TencentCredentials& key,const RequestControl& control){
            ++recognitions;recognizing=true;require(audio.sample_rate==16000&&key.secret_id=="fixture-id","Wrong Windows-to-core request");
            while(late&&!control.cancelled->load())std::this_thread::sleep_for(std::chrono::milliseconds(5));
            Result result;result.failure=network_failure?Failure::kNetwork:Failure::kNone;result.text=empty?"":"语音输入已接通。";recognizing=false;return result;
        };
        // 真实注入仅用于显式交互验收，并且只能向本测试创建的窗口发送。
        WNDCLASSW cls{};cls.hInstance=GetModuleHandleW(nullptr);cls.lpszClassName=L"Coral.Speech.EditorFixture";cls.lpfnWndProc=DefWindowProcW;RegisterClassW(&cls);
        HWND editor=CreateWindowExW(0,cls.lpszClassName,L"语音输入测试窗口",WS_OVERLAPPEDWINDOW|(interactive?WS_VISIBLE:0),40,40,360,160,nullptr,nullptr,cls.hInstance,nullptr);
        HWND edit=CreateWindowExW(0,L"EDIT",L"",WS_CHILD|WS_VISIBLE|ES_MULTILINE,10,10,300,90,editor,nullptr,cls.hInstance,nullptr);
        auto focus=[&]{if(interactive){SetForegroundWindow(editor);SetFocus(edit);require(GetForegroundWindow()==editor&&GetFocus()==edit,"Cannot activate owned editor fixture");}};
        focus();SpeechRuntime runtime(services);require(runtime.start(),"Cannot start isolated runtime");
        HWND host=FindWindowExW(HWND_MESSAGE,nullptr,kSpeechHostClass,services.instance_name.c_str());
        auto begin=[&]{focus();SendMessageW(host,kSpeechPushToTalkBeginMessage,0,0);};
        auto end=[&]{SendMessageW(host,kSpeechPushToTalkEndMessage,0,0);};
        runtime.open();require(!runtime.window()&&captures==0,"Legacy entry opened UI");
        begin();wait(runtime,80);require(captures==0,"Disabled shortcut recorded audio");settings.enabled=true;
        begin();wait(runtime,80);require(captures==0,"Missing credentials recorded audio");settings.configured=true;credentials={"fixture-id","fixture-secret"};microphone=false;
        begin();require(runtime.window()&&text(runtime.window())==L"No test microphone"&&captures==0,"Microphone gate");
        microphone=true;begin();pump(runtime,[&]{return recording.load();});HWND window=runtime.window();
        require((GetWindowLongPtrW(window,GWL_EXSTYLE)&WS_EX_NOACTIVATE)&&!GetWindow(window,GW_CHILD),"Widget has focusable controls");
        RECT rect{};GetClientRect(window,&rect);require(rect.right<=112&&rect.bottom<=112,"Widget is not compact");
        if(interactive)require(GetForegroundWindow()==editor&&GetFocus()==edit,"Widget stole input focus");
        auto count=captures.load();begin();wait(runtime,100);require(captures==count&&recognitions==0,"Hold repeated or recognized before release");save(window,output/"recording.png");
        late=true;end();pump(runtime,[&]{return recognizing.load();});wait(runtime,80);require(copies==0,"Recognition delivered prematurely");save(window,output/"recognizing.png");late=false;
        pump(runtime,[&]{return !runtime.busy();});
        if(interactive){pump(runtime,[&]{return text(edit)==L"语音输入已接通。";});require(copies==0&&!IsWindowVisible(window),"Automatic insertion failed");SetWindowTextW(edit,L"");SendMessageW(edit,EM_SETREADONLY,TRUE,0);
            begin();pump(runtime,[&]{return recording.load();});end();pump(runtime,[&]{return copies==1;});require(text(edit).empty(),"Read-only edit received text");}
        else require(copies==1&&copied==L"语音输入已接通。","No caret did not copy");
        require(text(window).find(L"剪贴板")!=std::wstring::npos,"Clipboard toast missing");save(window,output/"clipboard-toast.png");
        if(interactive){
            SendMessageW(edit,EM_SETREADONLY,FALSE,0);late=true;
            begin();pump(runtime,[&]{return recording.load();});end();pump(runtime,[&]{return recognizing.load();});
            HWND other=CreateWindowExW(0,cls.lpszClassName,L"焦点变化测试窗口",WS_OVERLAPPEDWINDOW|WS_VISIBLE,50,50,300,120,nullptr,nullptr,cls.hInstance,nullptr);
            SetForegroundWindow(other);require(GetForegroundWindow()==other,"Cannot activate second owned fixture");late=false;
            pump(runtime,[&]{return !runtime.busy();});require(copies==2&&text(edit).empty(),"Changed foreground received text");
            DestroyWindow(other);focus();SendMessageW(edit,EM_SETREADONLY,TRUE,0);
        }
        for(UINT dpi:{96u,120u,144u,192u}){RECT suggested{};SendMessageW(window,WM_DPICHANGED,MAKEWPARAM(dpi,dpi),reinterpret_cast<LPARAM>(&suggested));GetClientRect(window,&rect);
            require(rect.right==MulDiv(360,dpi,96)&&rect.bottom==MulDiv(56,dpi,96),"Toast DPI sizing");save(window,output/("toast-"+std::to_string(dpi)+".png"));}
        auto completed=recognitions.load();auto copy_count=copies;
        begin();pump(runtime,[&]{return recording.load();});SendMessageW(host,kSpeechCancelMessage,0,0);pump(runtime,[&]{return !runtime.busy();});require(recognitions==completed&&copies==copy_count,"Cancellation uploaded audio");
        begin();pump(runtime,[&]{return recording.load();});settings.enabled=false;pump(runtime,[&]{return !runtime.busy()&&!IsWindowVisible(window);});require(recognitions==completed,"Disable uploaded audio");
        settings.enabled=true;late=true;begin();pump(runtime,[&]{return recording.load();});end();pump(runtime,[&]{return recognizing.load();});settings.revision="rotated";
        pump(runtime,[&]{return !runtime.busy()&&!IsWindowVisible(window);});require(copies==copy_count,"Credential rotation delivered late result");late=false;
        begin();pump(runtime,[&]{return recording.load();});settings.shortcut="ctrl_alt_space";pump(runtime,[&]{return !runtime.busy();});require(!IsWindowVisible(window),"Shortcut change left recording open");
        begin();end();pump(runtime,[&]{return copies==copy_count+1;});require(!runtime.busy(),"Quick release never completed");copy_count=copies;
        empty=true;begin();pump(runtime,[&]{return recording.load();});end();pump(runtime,[&]{return !runtime.busy();});require(copies==copy_count&&text(window).find(L"没有识别到")!=std::wstring::npos,"Empty result overwrote clipboard");empty=false;
        clipboard_busy=true;begin();pump(runtime,[&]{return recording.load();});end();wait(runtime,200);require(runtime.busy()&&copies==copy_count,"Busy clipboard lost result");clipboard_busy=false;
        pump(runtime,[&]{return copies==copy_count+1;});require(!runtime.busy(),"Clipboard retry did not complete");completed=recognitions.load();
        copy_count=copies;clipboard_busy=true;begin();pump(runtime,[&]{return recording.load();});end();
        pump(runtime,[&]{return !runtime.busy()&&text(window).find(L"剪贴板不可用")!=std::wstring::npos;});
        wait(runtime,3250);require(IsWindowVisible(window),"Clipboard failure hid retained result");
        clipboard_busy=false;SendMessageW(window,WM_LBUTTONUP,0,0);pump(runtime,[&]{return copies==copy_count+1;});require(!runtime.busy(),"Copy retry retained busy state");
        copy_count=copies;network_failure=true;begin();pump(runtime,[&]{return recording.load();});end();pump(runtime,[&]{return !runtime.busy();});
        require(copies==copy_count&&text(window).find(L"网络连接失败")!=std::wstring::npos,"Recognition failure overwrote clipboard");network_failure=false;completed=recognitions.load();
        settings.enabled=false;credentials={};runtime.open(true);pump(runtime,[&]{return !runtime.busy()&&text(window).find(L"麦克风正常")!=std::wstring::npos;});require(recognitions==completed,"Microphone test uploaded audio");save(window,output/"microphone-test.png");
        wait(runtime,3250);require(!IsWindowVisible(window),"Toast did not dismiss");
        // 先让编辑控件退出文本服务焦点，再销毁自有窗口，最后结束运行时和 COM。
        if(interactive){SetFocus(nullptr);wait(runtime,100);}
        DestroyWindow(editor);UnregisterClassW(cls.lpszClassName,cls.hInstance);
        runtime.stop();require(!IsWindow(window),"Shutdown leaked window");
        CoUninitialize();std::cout<<"Speech hold runtime tests passed\n";return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}

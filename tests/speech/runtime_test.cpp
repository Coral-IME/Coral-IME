#include "speech_runtime.h"
#include "candidate_theme.h"
#include "soft_canvas.h"
#include <commctrl.h>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <thread>
using namespace coral::speech;
using namespace coral::speech::win32;
namespace {
void require(bool condition, const char* reason) { if (!condition) throw std::runtime_error(reason); }
std::wstring text(HWND window, int id) {
    wchar_t buffer[2048]{}; GetDlgItemTextW(window,id,buffer,2048); return buffer;
}
void pump(SpeechRuntime& runtime, const std::function<bool()>& ready, unsigned timeout = 3000) {
    const auto start = GetTickCount64();
    while (!ready() && GetTickCount64()-start < timeout) {
        MSG message{};
        while (PeekMessageW(&message,nullptr,0,0,PM_REMOVE)) {
            if (!runtime.process_message(message)) { TranslateMessage(&message); DispatchMessageW(&message); }
        }
        MsgWaitForMultipleObjects(0,nullptr,FALSE,10,QS_ALLINPUT);
    }
    require(ready(),"Speech UI wait timed out");
}
void wait(SpeechRuntime& runtime,unsigned milliseconds) {
    const auto start=GetTickCount64();
    while (GetTickCount64()-start<milliseconds) {
        MSG message{};
        while (PeekMessageW(&message,nullptr,0,0,PM_REMOVE)) {
            if (!runtime.process_message(message)) { TranslateMessage(&message); DispatchMessageW(&message); }
        }
        MsgWaitForMultipleObjects(0,nullptr,FALSE,10,QS_ALLINPUT);
    }
}
void render(HWND window, HWND root, coral::win32::soft::Canvas& canvas) {
    RECT rect{}; GetClientRect(window,&rect);
    POINT point{}; MapWindowPoints(window,root,&point,1);
    const int saved=SaveDC(canvas.dc());
    SetViewportOrgEx(canvas.dc(),point.x,point.y,nullptr);
    IntersectClipRect(canvas.dc(),0,0,rect.right,rect.bottom);
    SendMessageW(window,WM_PRINTCLIENT,reinterpret_cast<WPARAM>(canvas.dc()),PRF_CLIENT | PRF_ERASEBKGND);
    RestoreDC(canvas.dc(),saved);
    for (auto child=GetWindow(window,GW_CHILD);child;child=GetWindow(child,GW_HWNDNEXT))
        if (GetWindowLongPtrW(child,GWL_STYLE)&WS_VISIBLE) render(child,root,canvas);
}
void save(HWND window,const std::filesystem::path& path) {
    RECT rect{};GetClientRect(window,&rect);
    coral::win32::soft::Canvas canvas(rect.right,rect.bottom);
    canvas.surface().fill(0xfffff9f6);
    render(window,window,canvas); GdiFlush();
    coral::win32::SkinImage image;
    image.width=canvas.width();image.height=canvas.height();
    image.pixels.assign(canvas.pixels(),canvas.pixels()+canvas.count());
    require(coral::win32::SaveSkinImagePng(image,path.wstring()),"Cannot save speech preview");
}
}
int main(int argc,char** argv) {
    try {
        SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
        INITCOMMONCONTROLSEX controls{sizeof(controls),ICC_WIN95_CLASSES|ICC_PROGRESS_CLASS}; InitCommonControlsEx(&controls);
        const auto output=std::filesystem::path(argc>1?argv[1]:"speech-renders");
        const bool interactive_requested=argc>2 && std::string(argv[2])=="interactive";
        std::filesystem::create_directories(output);
        if (argc>2 && std::string(argv[2])=="microphone") {
            const auto info=query_microphone();
            std::wcout << (info.available?L"Microphone available: ":L"Microphone unavailable: ") << info.message << '\n';
            if (info.available) {
                std::atomic<bool> stop{false},cancelled{false};
                const auto result=capture_microphone(stop,cancelled,1,[](CaptureProgress){});
                std::wcout << L"Capture bytes=" << result.audio.bytes.size() << L" error=" << result.error << '\n';
                require(result.error.empty() && !result.audio.bytes.empty(),"Real microphone capture failed");
            }
            CoUninitialize(); return 0;
        }
        SpeechSettings settings;
        TencentCredentials credentials;
        std::atomic<unsigned> captures{0},recognitions{0};
        std::atomic<bool> recording{false},late{false},recognizing{false};
        bool microphone=true;
        SpeechServices services;
        services.instance_name=L"CoralSpeechTest-"+std::to_wstring(GetCurrentProcessId());
        services.enable_keyboard=false;
        services.settings=[&]{return settings;};
        services.credentials=[&]{return credentials;};
        services.microphone=[&]{return MicrophoneInfo{microphone,L"Test",L"No test microphone"};};
        services.capture=[&](const std::atomic<bool>& stop,const std::atomic<bool>& cancelled,unsigned,
            const std::function<void(CaptureProgress)>& progress){
            ++captures; recording=true;
            progress({1200,45});
            while(!stop && !cancelled) std::this_thread::sleep_for(std::chrono::milliseconds(5));
            CaptureResult result;result.audio.format=AudioFormat::kPcm;result.audio.bytes={0,0};result.peak_level=45;
            result.cancelled=cancelled;recording=false;return result;
        };
        services.recognize=[&](const AudioClip& audio,const TencentCredentials& key,const RequestControl& control){
            ++recognitions;recognizing=true;
            require(audio.format==AudioFormat::kPcm && audio.sample_rate==16000 && key.secret_id=="fixture-id","Wrong Windows-to-core request");
            while(late && !control.cancelled->load()) std::this_thread::sleep_for(std::chrono::milliseconds(5));
            Result result;result.text="语音输入已接通。";result.audio_duration_ms=1200;recognizing=false;return result;
        };
        // 只向本测试创建的输入框回填文字，不操作其他应用或用户剪贴板。
        WNDCLASSW editor_class{};editor_class.hInstance=GetModuleHandleW(nullptr);
        editor_class.lpszClassName=L"Coral.Speech.EditorFixture";editor_class.lpfnWndProc=DefWindowProcW;
        RegisterClassW(&editor_class);
        HWND editor=CreateWindowExW(0,editor_class.lpszClassName,L"语音输入测试窗口",WS_OVERLAPPEDWINDOW|(interactive_requested?WS_VISIBLE:0),
            40,40,360,160,nullptr,nullptr,editor_class.hInstance,nullptr);
        HWND editor_text=CreateWindowExW(WS_EX_CLIENTEDGE,L"EDIT",L"",WS_CHILD|WS_VISIBLE|ES_MULTILINE,
            10,10,300,90,editor,reinterpret_cast<HMENU>(1),editor_class.hInstance,nullptr);
        if (interactive_requested) { SetForegroundWindow(editor);SetFocus(editor_text); }
        SpeechRuntime runtime(services);require(runtime.start(),"Cannot start isolated runtime");
        runtime.open();auto window=runtime.window();
        require(window && IsWindowVisible(window) && text(window,0x6100).find(L"开关")!=std::wstring::npos && captures==0,"Disabled gate");
        SendMessageW(window,WM_CLOSE,0,0);
        settings.enabled=true;runtime.open();
        require(text(window,0x6100).find(L"SecretId")!=std::wstring::npos && captures==0,"Credentials gate");
        SendMessageW(window,WM_CLOSE,0,0);
        settings.configured=true;credentials={"fixture-id","fixture-secret"};microphone=false;runtime.open();
        require(text(window,0x6100)==L"No test microphone" && captures==0,"Microphone gate");
        SendMessageW(window,WM_CLOSE,0,0);
        microphone=true;
        if (interactive_requested) { SetForegroundWindow(editor);SetFocus(editor_text); }
        GUITHREADINFO editor_gui{sizeof(editor_gui)};
        const auto foreground_before_recording=GetForegroundWindow();
        const bool interactive_editor=interactive_requested && foreground_before_recording==editor &&
            GetGUIThreadInfo(GetCurrentThreadId(),&editor_gui) && editor_gui.hwndFocus==editor_text;
        runtime.open();pump(runtime,[&]{return recording.load();});
        save(window,output/"recording.png");
        SendMessageW(window,WM_COMMAND,MAKEWPARAM(0x6104,BN_CLICKED),0);
        pump(runtime,[&]{return text(window,0x6103)==L"语音输入已接通。";});
        require(recognitions==1 && IsWindowEnabled(GetDlgItem(window,0x6106)),"Recognized text/copy not ready");
        save(window,output/"recognized.png");
        if (interactive_editor) {
            require(IsWindowEnabled(GetDlgItem(window,0x6105)),"Original input target was not retained");
            SendMessageW(window,WM_COMMAND,MAKEWPARAM(0x6105,BN_CLICKED),0);
            pump(runtime,[&]{return text(editor,1)==L"语音输入已接通。";});
            require(!IsWindowVisible(window),"Successful insert left popup open");
            runtime.open();pump(runtime,[&]{return recording.load();});
            SendMessageW(window,WM_COMMAND,MAKEWPARAM(0x6104,BN_CLICKED),0);
            pump(runtime,[&]{return text(window,0x6103)==L"语音输入已接通。";});
        } else {
            if (!foreground_before_recording)
                require(!IsWindowEnabled(GetDlgItem(window,0x6105)),"Input was enabled without an original target");
            std::cout<<"No interactive foreground: external text insertion not exercised\n";
        }
        const auto completed=recognitions.load();
        for(UINT dpi:{96u,144u,192u}) {
            RECT suggested{30,30,MulDiv(480,dpi,96)+30,MulDiv(370,dpi,96)+30};
            SendMessageW(window,WM_DPICHANGED,MAKEWPARAM(dpi,dpi),reinterpret_cast<LPARAM>(&suggested));
            save(window,output/("recognized-"+std::to_string(dpi)+".png"));
        }
        SendMessageW(window,WM_CLOSE,0,0);runtime.open();pump(runtime,[&]{return recording.load();});
        SendMessageW(window,WM_CLOSE,0,0);pump(runtime,[&]{return !runtime.busy();});
        require(recognitions==completed && !IsWindowVisible(window),"Cancel uploaded audio");
        runtime.open();pump(runtime,[&]{return recording.load();});settings.enabled=false;
        pump(runtime,[&]{return !IsWindowVisible(window) && !runtime.busy();});
        require(recognitions==completed,"Disable uploaded audio");
        settings.enabled=true;runtime.open();pump(runtime,[&]{return recording.load();});late=true;
        SendMessageW(window,WM_COMMAND,MAKEWPARAM(0x6104,BN_CLICKED),0);pump(runtime,[&]{return recognizing.load();});
        settings.revision="rotated";pump(runtime,[&]{return !IsWindowVisible(window) && !runtime.busy();});
        require(!IsWindowVisible(window),"Credential rotation left old result visible");
        late=false;settings.enabled=false;credentials={};runtime.open(true);pump(runtime,[&]{return recording.load();});
        SendMessageW(window,WM_COMMAND,MAKEWPARAM(0x6104,BN_CLICKED),0);
        pump(runtime,[&]{return text(window,0x6100).find(L"麦克风正常")!=std::wstring::npos;});
        require(recognitions==completed+1,"Microphone test uploaded audio");
        save(window,output/"microphone-test.png");
        // 按下发言：显式投递按下/松开消息，验证消息顺序、状态和安全门闩。
        // 保留麦克风检测结果窗，确认按下发言会退出检测模式并开始正常识别。
        late=false;settings.enabled=true;settings.configured=true;settings.push_to_talk=true;
        credentials={"fixture-id","fixture-secret"};microphone=true;
        const auto ptt_completed=recognitions.load();
        HWND host=FindWindowExW(HWND_MESSAGE,nullptr,kSpeechHostClass,services.instance_name.c_str());
        require(host!=nullptr,"Cannot find isolated speech host");
        PostMessageW(host,kSpeechPushToTalkBeginMessage,0,0);
        pump(runtime,[&]{return recording.load();});
        require(IsWindowVisible(window) && text(window,0x6100).find(L"Ctrl + Win")!=std::wstring::npos,
            "Push-to-talk did not open the shared window");
        PostMessageW(host,kSpeechPushToTalkEndMessage,0,0);
        pump(runtime,[&]{return text(window,0x6103)==L"语音输入已接通。";});
        require(recognitions==ptt_completed+1,"Push-to-talk release did not recognize");
        // 结果窗仍打开时再次按下，应开始新一段并清空上一段文字。
        PostMessageW(host,kSpeechPushToTalkBeginMessage,0,0);
        pump(runtime,[&]{return recording.load();});
        require(text(window,0x6103).empty(),"Push-to-talk did not clear the previous result");
        PostMessageW(host,kSpeechPushToTalkEndMessage,0,0);
        pump(runtime,[&]{return text(window,0x6103)==L"语音输入已接通。";});
        require(recognitions==ptt_completed+2,"Second push-to-talk did not recognize");
        // 录音期间关闭按下发言：结束当前采集并识别，而不是继续等到 60 秒超时。
        PostMessageW(host,kSpeechPushToTalkBeginMessage,0,0);
        pump(runtime,[&]{return recording.load();});
        settings.push_to_talk=false;
        pump(runtime,[&]{return text(window,0x6103)==L"语音输入已接通。";});
        require(recognitions==ptt_completed+3,"Disabling push-to-talk did not finish recording");
        // 关闭语音开关后，迟到的按下消息不得开始采集。
        SendMessageW(window,WM_CLOSE,0,0);
        settings.enabled=false;
        PostMessageW(host,kSpeechPushToTalkBeginMessage,0,0);
        wait(runtime,200);
        require(!recording.load() && !IsWindowVisible(window),"Disabled push-to-talk started recording");
        runtime.stop();require(!IsWindow(window),"Runtime shutdown leaked window");
        DestroyWindow(editor);UnregisterClassW(editor_class.lpszClassName,editor_class.hInstance);
        CoUninitialize();std::cout<<"Speech Windows runtime tests passed\n";return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}

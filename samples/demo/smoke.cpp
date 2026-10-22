#include <windows.h>
#include <algorithm>
#include <filesystem>
#include <iostream>
#include <vector>
#include "syllable_helpers.h"
#include "core_operations.h"
#include "coral/composition/composition_session.h"
#include "test_environment.h"

int failures=0;
void check(bool ok,const char* what) { std::cout<<(ok ? "PASS " : "FAIL ")<<what<<'\n';if(!ok)++failures; }
std::vector<std::string> parse(const std::string& text, const std::string& condition = "") {
    std::vector<std::string> words,pinyins;int count=0;
    const bool parsed=coral::runtime::parse_candidates(condition.c_str(),text.c_str(),std::back_inserter(words),std::back_inserter(pinyins),0,25,512,&count,0);
    if(!parsed) std::cerr<<"Parse rejected: "<<text<<'\n';
    return words;
}
bool has(const std::string& text,const std::string& word) {
    const auto words=parse(text);return std::find(words.begin(),words.end(),word)!=words.end();
}
int main(int argc,char** argv) {
    if(argc!=3 || !std::getenv("CORAL_TEST_NAMESPACE") || !std::getenv("CORAL_USER_DATA_DIR")) {
        std::cerr<<"Run through tools/checks/run_isolated.py with demo data and a separate user directory\n";return 2;
    }
    const auto data=std::filesystem::absolute(argv[1]),user=std::filesystem::absolute(argv[2]);
    for(const auto& file:{"data/generallexicon/mainlexicon.coral","data/generallexicon/languagemodel.coral","data/generallexicon/char_lm.dat","data/generallexicon/correction_pinyin_model.dat","config/coralpinyinConfig.ini"})
        if(!std::filesystem::is_regular_file(data/file)) { std::cerr<<"Missing runtime resource: "<<file<<'\n';return 2; }
    std::filesystem::create_directories(user/"config");
    std::filesystem::copy_file(data/"config/coralpinyinConfig.ini",user/"config/coralpinyinConfig.ini",std::filesystem::copy_options::overwrite_existing);
    check(coral::testing::objectName("probe")!="probe","test namespace is active");
    const bool initialized=coral::runtime::start_runtime(user.string().c_str(),true);
    check(initialized,"existing engine initializes from demo formats");
    if(initialized) {
        int fuzzy[19]{};
        coral::runtime::apply_runtime_settings(true,true,false,false,false,false,25,0,fuzzy,19);
        check(has("nihao",u8"你好"),"ordinary pinyin");
        check(has("zhongguo",u8"中国"),"multi-syllable dictionary");
        {
            const auto baseline = parse("zongguo");
            fuzzy[0] = 1;
            coral::runtime::apply_runtime_settings(true,true,false,false,false,false,25,0,fuzzy,19);
            const auto enabled = parse("zongguo");
            check(enabled != baseline && std::find(enabled.begin(), enabled.end(), u8"中国") != enabled.end(),
                  "enabling fuzzy rules reparses the same input");
            fuzzy[0] = 0;
            coral::runtime::apply_runtime_settings(true,true,false,false,false,false,25,0,fuzzy,19);
            check(parse("zongguo") == baseline, "disabling fuzzy rules restores same-input candidates");
        }
        // 既有引擎把精确英文候选挡在 isShowInfos 之后。
        // 在此演练其支持的分支，而不改变候选排序行为。
        coral::runtime::apply_runtime_settings(true,true,false,false,false,true,25,0,fuzzy,19);
        coral::runtime::clear_parse_cache();
        check(has("hello","hello"),"English dictionary (existing show-infos branch)");
        coral::runtime::apply_runtime_settings(true,true,false,false,false,false,25,0,fuzzy,19);
        coral::runtime::clear_parse_cache();
        check(has("umumu",u8"林"),"radical dictionary");
        check(has("uhs",u8"十"),"stroke dictionary");
        check(has("uustar",u8"★"),"UU symbols");
        check(has("v1+2","3"),"V calculator");
        coral::runtime::apply_runtime_settings(false,true,true,false,false,false,25,0,fuzzy,19);
        coral::runtime::clear_parse_cache();
        const auto ordinary_u=parse("uong");
        const auto ordinary_v=parse("vong");
        coral::runtime::apply_runtime_settings(true,true,true,false,false,false,25,0,fuzzy,19);
        coral::runtime::clear_parse_cache();
        check(parse("uong")==ordinary_u,"unmatched U input uses the ordinary correction pipeline");
        check(parse("uong")==ordinary_u,"cached U fallback candidates remain stable");
        check(parse("vong")==ordinary_v,"V alphabetic input keeps its ordinary candidates");
        check(has("umumu",u8"林"),"matched U input keeps radical candidates with correction enabled");
        check(has("uhs",u8"十"),"matched U input keeps stroke candidates with correction enabled");
        check(parse("u")==std::vector<std::string>{"u"},"single U remains a mode prefix");
        check(parse("uu")==std::vector<std::string>{"uu"},"UU remains a symbol prefix");
        check(has("uustar",u8"★"),"UU symbols remain available with correction enabled");
        check(parse("uuong")==std::vector<std::string>{"uuong"},"unmatched UU stays in symbol mode");
        {
            std::vector<std::string> words,pinyins;int count=0;
            coral::runtime::parse_candidates("","Uong",std::back_inserter(words),std::back_inserter(pinyins),0,25,512,&count,1);
            check(words==std::vector<std::string>{"Uong"},"explicit doublepinyin U stays in special mode");
        }
        coral::runtime::apply_runtime_settings(true,true,false,false,false,false,25,0,fuzzy,19);
        coral::runtime::clear_parse_cache();
        check(parse("uong")==std::vector<std::string>{"uong"},"disabled correction keeps unmatched U input literal");
        {
            const auto baseline = parse("shijie");
            const auto predicted = parse("shijie", u8"你好");
            check(!predicted.empty() && predicted.front() == u8"世界", "dictionary continuation returns only the remaining text");
            const auto continued = parse("jie", u8"你好世");
            check(!continued.empty() && continued.front() == u8"界", "continuation preserves earlier context after a single character");
            check(parse("shijie") == baseline, "same pinyin with cleared context does not reuse prediction cache");
            coral::runtime::apply_runtime_settings(true,true,false,false,false,false,25,0,fuzzy,19,false);
            check(parse("shijie", u8"你好") == baseline, "disabled prediction ignores explicit condition");
            coral::runtime::apply_runtime_settings(true,true,false,false,false,false,25,0,fuzzy,19,true);
            check(parse("shijie", u8"你好") == predicted, "reenabled prediction reaches engine");
            check(parse("s", u8"你好") == parse("s"), "single initial keeps ordinary ranking");
        }
        {
            coral::composition::CompositionSession session;coral::composition::Output output;coral::composition::InputContext context;
            for(const char c:std::string("nihao")) { coral::composition::KeyEvent event;event.codepoint=c;output=session.send_key(event,context); }
            check(!output.candidates.empty(),"session exposes candidates");
            coral::composition::KeyEvent space;space.key=coral::composition::Key::kSpace;output=session.send_key(space,context);
            check(output.commit_text==u8"你好","session commits the first candidate");
        }
        coral::runtime::apply_runtime_settings(true,true,false,false,true,false,25,0,fuzzy,19);
        check(has("zhongguo",u8"中國"),"self-authored simplified/traditional conversion");
        coral::runtime::stop_runtime();
    }
    std::cout.flush();std::cerr.flush();
    // 特殊输入/UI 的清理仍使用历史上的强制退出冒烟路径；正常生命周期覆盖见其他测试。
    ::TerminateProcess(::GetCurrentProcess(),failures ? 1 : 0);
    return failures ? 1 : 0;
}

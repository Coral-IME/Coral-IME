#include "coral/composition/composition_session.h"
#undef NDEBUG
#include <cassert>
#include <limits>
#include <stdexcept>

using namespace coral::composition;
class ManualSource final : public CandidateSource
{
public:
    bool fail = false;
    bool learning_error = false;
    bool query_error = false;
    int queries = 0;
    int remembered = 0;
    std::string previous_condition;
    bool query_candidates(const std::string& condition, const std::string& pinyin,
        int offset, int page_size, int, CandidatePage& page) override
    {
        ++queries; previous_condition = condition;
        if (query_error) throw std::runtime_error("manual query error");
        page.candidates = {{1,"wrong-prefix-on-failure","", ""}};
        if (fail) return false;
        page.offset = offset; page.has_next_page = offset == 0;
        if (pinyin == "ni'hao") page.candidates = {{2,u8"你","ni",u8"你(nǐ)"}};
        else if (pinyin == "hao") page.candidates = {{3,u8"好","hao",""}};
        else page.candidates = {{4,pinyin,std::string(pinyin),""}};
        assert(page_size == 7);
        return true;
    }
    void remember_selection(const Candidate&) override
    {
        if (learning_error) throw std::runtime_error("manual learning error");
        ++remembered;
    }
};
int main()
{
    bool rejected_null = false;
    try { CompositionSession invalid(nullptr); } catch (const std::invalid_argument&) { rejected_null = true; }
    assert(rejected_null);
    auto source = std::make_shared<ManualSource>();
    CompositionSession session(source);
    assert(session.set_page_size(7));
    assert(!session.set_page_size(0) && !session.set_page_size(513) && session.page_size() == 7);
    auto output = session.query_page("", "ni'hao", 0);
    assert(output.composition == "ni'hao" && output.candidates_visible);
    const auto invalid_selection = session.select_index(9);
    assert(!invalid_selection.consumed && invalid_selection.composition == "ni'hao");
    source->query_error = true;
    bool query_failed = false;
    try { session.query_page("new", "new", 0, kModeDoublepinyin); }
    catch (const std::runtime_error&) { query_failed = true; }
    assert(query_failed && session.pinyin() == "ni'hao" && session.condition().empty());
    source->query_error = false;
    source->learning_error = true;
    bool learning_failed = false;
    try { session.select_index(0); } catch (const std::runtime_error&) { learning_failed = true; }
    assert(learning_failed && session.pinyin() == "ni'hao" && source->remembered == 0);
    source->learning_error = false;
    output = session.select_index(0);
    assert(output.commit_text == u8"你" && output.composition == "hao" && output.candidates_visible);
    assert(output.candidates.candidates[0].word == u8"好" && session.condition() == u8"你");
    assert(source->previous_condition == u8"你");
    output = session.select_index(0);
    assert(output.commit_text == u8"好" && output.composition.empty() && output.candidates.empty());
    assert(!output.candidates_visible && session.condition().empty() && source->remembered == 2);
    session.query_page("", "abc", 0);
    const int before = source->queries;
    output = session.page_candidates(1);
    assert(output.candidates.offset == 7 && source->queries == before + 1);
    session.page_candidates(1);
    assert(source->queries == before + 1);
    session.page_candidates(-1);
    assert(source->queries == before + 2);
    output = session.query_page("changed", "changed", std::numeric_limits<int>::max());
    assert(output.composition == "abc" && session.condition().empty());
    session.reset();
    assert(session.page_size() == 7 && !session.has_input());
    for (char letter : std::string("abc"))
    {
        KeyEvent event; event.codepoint = static_cast<unsigned char>(letter);
        output = session.send_key(event, {});
        assert(output.consumed);
    }
    KeyEvent modifier; modifier.codepoint = 'x'; modifier.ctrl = true;
    output = session.send_key(modifier, {});
    assert(!output.consumed && output.composition == "abc");
    KeyEvent back; back.key = Key::kBackspace;
    assert(session.send_key(back, {}).composition == "ab");
    source->fail = true;
    KeyEvent letter; letter.codepoint = 'd';
    output = session.send_key(letter, {});
    assert(output.composition == "abd" && output.candidates.empty() && !output.candidates_visible);
    source->fail = false;
    KeyEvent escape; escape.key = Key::kEscape;
    output = session.send_key(escape, {});
    assert(output.consumed && output.composition.empty() && output.candidates.empty() && session.page_size() == 7);
    return 0;
}

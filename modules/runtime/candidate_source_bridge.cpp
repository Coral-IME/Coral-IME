#if defined(_WIN32)
#include <Windows.h>
#endif
#include "syllable_helpers.h"
#include "core_operations.h"
#include "prefix_stability.h"
#include "script_conversion.h"
#include "coral/composition/composition_session.h"
#include <algorithm>

namespace coral::runtime
{
namespace
{
class RuntimeCandidateSource final : public composition::CandidateSource
{
public:
    bool query_candidates(const std::string& condition, const std::string& pinyin,
        int offset, int page_size, int mode, composition::CandidatePage& page) override
    {
        const auto& anchors = stability_.prepare(condition, pinyin, mode);
        phonetics::PrefixStabilityScope stability_scope(anchors);
        // 首屏的会话锚点也是查询身份；条件相同也不得借用另一会话的缓存。
        if (offset == 0) coral::runtime::clear_parse_cache();
        std::vector<std::string> words, pinyins;
        int count = 0;
        if (!coral::runtime::parse_candidates(condition.c_str(), pinyin.c_str(), std::back_inserter(words),
            std::back_inserter(pinyins), offset, page_size, 256, &count, mode)) {
            stability_.reset(); return false;
        }
        composition::CandidatePage result;
        result.offset = offset;
        coral::runtime::read_next_page_state(result.has_next_page);
        const auto limit = (std::min)({static_cast<std::size_t>((std::max)(count, 0)), words.size(), pinyins.size()});
        for (std::size_t index = 0; index < limit; ++index)
        {
            composition::Candidate candidate;
            candidate.word = trim(words[index]); candidate.pinyin = trim(pinyins[index]);
            candidate.display_text = coral::runtime::candidate_display_label(offset + static_cast<int>(index));
            result.candidates.push_back(std::move(candidate));
        }
        if (offset == 0) {
            if (result.candidates.empty()) stability_.reset();
            else {
                const auto& first = result.candidates.front();
                const auto word = coral::runtime::runtime_traditional()
                    ? phonetics::ScriptConversion::instance().ConvertText(hlp::str2wstr(first.word), "t2s.json")
                    : first.word;
                stability_.observe(word, first.pinyin);
            }
        }
        page = std::move(result);
        return true;
    }
    void remember_selection(const composition::Candidate& candidate) override
    {
        std::uint32_t condition_id = 0;
        coral::runtime::remember_candidate(candidate.pinyin, candidate.word, condition_id);
    }
    void reset_composition() override { stability_.reset(); }
private:
    phonetics::PrefixStability stability_;
    static std::string trim(const std::string& value)
    {
        const auto begin = value.find_first_not_of(" \t\r\n");
        if (begin == std::string::npos) return {};
        return value.substr(begin, value.find_last_not_of(" \t\r\n") - begin + 1);
    }
};
}
}

coral::composition::CompositionSession::CompositionSession()
    : CompositionSession(std::make_shared<coral::runtime::RuntimeCandidateSource>()) {}

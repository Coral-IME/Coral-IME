#include "coral/composition/candidate_capacity.h"
#include <iostream>
#include <stdexcept>
#include <string>

int main() {
    try {
        const auto require = [](bool ok) { if (!ok) throw std::runtime_error("Unexpected composed row capacity"); };
        coral::composition::ComposedCandidateCapacity four;
        for (int i = 0; i < 5; ++i) require(four.try_append("今天下雨"));
        require(!four.try_append("明天晴天"));
        require(!four.try_append("好"));
        coral::composition::ComposedCandidateCapacity three;
        for (int i = 0; i < 6; ++i) require(three.try_append("下雨了"));
        require(!three.try_append("明天见"));
        coral::composition::ComposedCandidateCapacity long_first;
        std::string text;
        for (int i = 0; i < 100; ++i) text += "长";
        require(long_first.try_append(text));
        require(!long_first.try_append("第二条"));
        coral::composition::CandidateRowMetrics narrow;
        narrow.available_width = 0;
        coral::composition::ComposedCandidateCapacity unavailable(narrow);
        require(unavailable.try_append("保留首选"));
        require(!unavailable.try_append("其次"));
        return 0;
    } catch (const std::exception& failure) {
        std::cerr << failure.what() << '\n'; return 1;
    }
}

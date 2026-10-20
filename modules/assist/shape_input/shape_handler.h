#pragma once
#include "shape_lexicon.h"
#include "candidate_result.h"

namespace coral::assist {
class ShapeHandler {
public:
    struct File { std::filesystem::path path; std::string identifier; };
    ShapeHandler(File strokeTrie, File strokeData, File radicalTrie, File radicalData,
        std::filesystem::path base, std::string version);
    bool parse(const std::string& input, std::vector<coral::phonetics::CandidateResult>& sents);
    bool unavailable() const { return failed_; }
private:
    struct Resource { File trie, data; std::unique_ptr<coral::assist::ShapeLexicon> lexicon; bool attempted = false; };
    bool query(Resource& resource, const std::string& input, std::vector<coral::phonetics::CandidateResult>& sents);
    Resource stroke_, radical_;
    std::filesystem::path base_;
    std::string version_;
    bool failed_ = false;
};
} // 命名空间 coral::assist 结束（批次 2 辅助输入）


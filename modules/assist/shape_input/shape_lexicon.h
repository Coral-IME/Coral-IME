/*
* @author: s_yanxiaoren@coral.cn
* @date 2025-06-23 10:04:30
*/
#pragma once
#ifndef CORAL_MODULES_ASSIST_SHAPE_INPUT_SHAPE_LEXICON_H_INCLUDED
#define CORAL_MODULES_ASSIST_SHAPE_INPUT_SHAPE_LEXICON_H_INCLUDED
#include <filesystem>
#include "shape_entry.h"
#include "shape_lexicon_view.hpp"
#include <marisa.h>
#include "candidate_result.h"

namespace coral::assist {
class ShapeLexicon
{
public:
	ShapeLexicon() {};
    bool load(const std::pair<std::filesystem::path, std::string>& triePath,
        const std::pair<std::filesystem::path, std::string>& dataPath,
        const std::filesystem::path& baseDir,
        const std::string& versionId);
    void append_candidates(std::string_view prefix, const std::string& input, std::vector<coral::phonetics::CandidateResult>& sents, int limit);
    bool build_lexicon(const std::filesystem::path& csvPath, const std::filesystem::path& lexicon, const std::filesystem::path& lexiconData);
private:
    marisa::Trie trie_;
    coral::assist::ShapeLexiconView data_view_;
};
} // 命名空间 coral::assist 结束（批次 2 辅助输入）


#endif // UMODELEXICON_H

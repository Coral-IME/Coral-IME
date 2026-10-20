#include "shape_handler.h"
#include <algorithm>

coral::assist::ShapeHandler::ShapeHandler(File st, File sd, File rt, File rd,
    std::filesystem::path base, std::string version)
    : stroke_{std::move(st), std::move(sd)}, radical_{std::move(rt), std::move(rd)},
      base_(std::move(base)), version_(std::move(version)) {}

bool coral::assist::ShapeHandler::query(Resource& resource, const std::string& input, std::vector<coral::phonetics::CandidateResult>& sents) {
    if (!resource.attempted) {
        resource.attempted = true;
        auto lexicon = std::make_unique<coral::assist::ShapeLexicon>();
        try {
            if (lexicon->load({resource.trie.path, resource.trie.identifier},
                {resource.data.path, resource.data.identifier}, base_, version_))
                resource.lexicon = std::move(lexicon);
        } catch (const std::exception& e) {
            LOG_ERROR(std::string("U-mode resource: ") + e.what());
        }
    }
    if (!resource.lexicon) { failed_ = true; return false; }
    const auto before = sents.size();
    resource.lexicon->append_candidates(std::string_view(input).substr(1), input, sents, 100);
    return sents.size() != before;
}

bool coral::assist::ShapeHandler::parse(const std::string& input, std::vector<coral::phonetics::CandidateResult>& sents) {
    failed_ = false;
    if (input.size() < 2 || input.size() > 129) return false;
    const auto body = std::string_view(input).substr(1);
    if (body.find_first_not_of("abcdefghijklmnopqrstuvwxyz") != body.npos) return false;
    bool ok = false;
    if (body.find_first_not_of("hspnz") == body.npos) ok = query(stroke_, input, sents);
    // 一些部首前缀也由笔画字母组成。按原有的先笔画后部首顺序
    // 保留两类结果。
    return query(radical_, input, sents) || ok;
}

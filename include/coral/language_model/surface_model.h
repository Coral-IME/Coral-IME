#ifndef CORAL_INCLUDE_CORAL_LANGUAGE_MODEL_SURFACE_MODEL_H_INCLUDED
#define CORAL_INCLUDE_CORAL_LANGUAGE_MODEL_SURFACE_MODEL_H_INCLUDED

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace coral::language_model {

// 状态零表示没有前文；表面词状态从一开始，读音由输入法词格约束。
struct PhraseScore {
    double log_probability = 0;
    uint32_t next_state = 0;
    bool first_arc_hit = false;
};

class SurfaceModel {
public:
    using Digest = std::array<uint8_t, 32>;
    // 完整验证后提交；失败保留此前可用模型，错误不含语料正文。
    bool load(const uint8_t* data, size_t size, const Digest& lexicon_digest, std::string& error);
    bool load_file(const std::filesystem::path& model, const std::filesystem::path& lexicon,
        std::string& error);
    bool valid() const noexcept { return !tokens_.empty(); }
    void reset() { *this = SurfaceModel{}; }
    uint32_t token_count() const { return static_cast<uint32_t>(tokens_.size()); }
    uint32_t arc_count() const { return static_cast<uint32_t>(arcs_.size()); }
    uint32_t phrase_count() const { return static_cast<uint32_t>(phrases_.size()); }
    uint32_t unknown_state() const { return unknown_state_; }
    double score_token(uint32_t context, uint32_t token, bool* hit = nullptr) const;
    bool score_phrase(uint32_t context, uint32_t phrase_id, PhraseScore& result) const;
    uint32_t phrase_end(uint32_t phrase_id) const;
    double phrase_upper_bound(uint32_t phrase_id) const;

private:
    struct Token { double initial, lower, backoff; uint32_t offset, count; };
    struct Arc { uint32_t target; double weight; };
    struct Phrase { uint32_t id, offset, count; double internal_weight, upper_bound; };
    const Phrase* find_phrase(uint32_t phrase_id) const;
    std::vector<Token> tokens_;
    std::vector<Arc> arcs_;
    std::vector<Phrase> phrases_;
    std::vector<uint32_t> sequences_;
    uint32_t unknown_state_ = 0;
};

} // 命名空间 coral::language_model 结束
#endif

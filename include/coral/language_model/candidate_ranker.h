#ifndef CORAL_INCLUDE_CORAL_LANGUAGE_MODEL_CANDIDATE_RANKER_H_INCLUDED
#define CORAL_INCLUDE_CORAL_LANGUAGE_MODEL_CANDIDATE_RANKER_H_INCLUDED

#include <array>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <ostream>
#include <string>
#include <string_view>
#include <vector>

namespace coral::language_model {

constexpr std::size_t kRankingFeatureCount = 32;
constexpr uint32_t kRankingFeatureVersion = 1;
using RankingFeatures = std::array<double, kRankingFeatureCount>;

struct RankingPathStats {
    double words = 0;
    double single_words = 0;
    double longest_word = 0;
    double pair_bonus = 0;
    double pair_hits = 0;
    double covered_words = 0;
    bool corrected = false;
    bool pair_available = false;
};

struct CandidateRankingInput {
    double base = 0;
    double neural = std::numeric_limits<double>::quiet_NaN();
    double legacy = 0;
    std::size_t characters = 0;
    std::size_t letters = 0;
    RankingPathStats path;
};

// 特征只来自候选与实际评分，不接收目标文本、文件名或标注信息。
std::vector<RankingFeatures> make_ranking_features(
    const std::vector<CandidateRankingInput>& inputs, std::size_t context_characters);
void write_ranking_trace(std::ostream& out, std::string_view pinyin, std::string_view context,
    std::size_t rank, std::string_view text, const RankingFeatures& features);

// 接收自有训练流程导出的数值树；不依赖 Python 或动态推理库。
class CandidateRanker {
public:
    bool load_file(const std::filesystem::path& path, std::string& error);
    bool valid() const noexcept { return !trees_.empty(); }
    bool predict(const RankingFeatures& features, double& score) const noexcept;
private:
    struct Node {
        int32_t feature, left, right;
        uint32_t flags;
        double threshold, value;
    };
    struct Tree { uint32_t start, count; };
    std::vector<Node> nodes_;
    std::vector<Tree> trees_;
};

} // 命名空间 coral::language_model 结束
#endif

#include "coral/language_model/candidate_ranker.h"
#include "picosha2.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <numeric>
#include <stdexcept>

namespace coral::language_model {
namespace {
uint32_t u32(const uint8_t* p) {
    return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
}
double f64(const uint8_t* p) {
    uint64_t bits = uint64_t(u32(p)) | uint64_t(u32(p + 4)) << 32;
    double value;
    static_assert(sizeof(value) == 8 && std::numeric_limits<double>::is_iec559, "binary64 required");
    std::memcpy(&value, &bits, 8);
    return value;
}
void require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
void quoted(std::ostream& out, std::string_view text) {
    out << '"';
    for (unsigned char c : text) {
        if (c == '"' || c == '\\') out << '\\' << char(c);
        else if (c < 0x20) {
            const char* hex = "0123456789abcdef";
            out << "\\u00" << hex[c >> 4] << hex[c & 15];
        } else out << char(c);
    }
    out << '"';
}
}

std::vector<RankingFeatures> make_ranking_features(
    const std::vector<CandidateRankingInput>& inputs, std::size_t context_characters) {
    if (inputs.empty()) return {};
    const auto count = inputs.size();
    std::vector<std::size_t> neural_order(count), legacy_order(count);
    std::iota(neural_order.begin(), neural_order.end(), 0);
    std::iota(legacy_order.begin(), legacy_order.end(), 0);
    const auto neural_value = [&](std::size_t i) {
        return std::isfinite(inputs[i].neural) ? inputs[i].neural : -std::numeric_limits<double>::infinity();
    };
    std::stable_sort(neural_order.begin(), neural_order.end(), [&](auto a, auto b) { return neural_value(a) > neural_value(b); });
    std::stable_sort(legacy_order.begin(), legacy_order.end(), [&](auto a, auto b) { return inputs[a].legacy > inputs[b].legacy; });
    std::vector<double> neural_rank(count), legacy_rank(count);
    std::size_t available = 0;
    for (std::size_t i = 0; i < count; ++i) {
        neural_rank[neural_order[i]] = double(i);
        legacy_rank[legacy_order[i]] = double(i);
        available += std::isfinite(inputs[i].neural);
    }
    const auto difference = [](double a, double b) {
        return std::isfinite(a) && std::isfinite(b) ? a - b : 0.;
    };
    std::vector<RankingFeatures> result;
    result.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        const auto& in = inputs[i];
        const auto& path = in.path;
        const double characters = double(in.characters), letters = double(in.letters);
        const bool valid = std::isfinite(in.neural);
        const double neural = valid ? in.neural : 0;
        RankingFeatures f{{in.base, in.base * (1 + letters), double(i), in.base - inputs[0].base,
            count > 1 ? in.base - inputs[1].base : 0,
            neural, neural / std::max(1., characters), neural / (1 + letters), neural_rank[i],
            difference(in.neural, neural_value(neural_order[0])), difference(in.neural, inputs[0].neural),
            in.legacy, legacy_rank[i], in.legacy - inputs[legacy_order[0]].legacy,
            characters, letters, path.words, path.single_words / std::max(1., path.words),
            path.longest_word, characters / std::max(1., path.words), path.pair_bonus,
            path.pair_hits / std::max(1., path.words - 1), path.covered_words / std::max(1., path.words),
            path.corrected ? 1. : 0., valid ? 0. : 1., double(available),
            count > 1 ? inputs[0].base - inputs[1].base : 0,
            available > 1 ? difference(neural_value(neural_order[0]), neural_value(neural_order[1])) : 0,
            double(count), neural_order[0] == 0 ? 1. : 0., double(context_characters), path.pair_available ? 1. : 0.}};
        // 同一版本的输入与缺失标志确定有限值；拒绝将非数写入训练文件。
        for (double& value : f) if (!std::isfinite(value)) value = 0;
        result.push_back(f);
    }
    return result;
}

void write_ranking_trace(std::ostream& out, std::string_view pinyin, std::string_view context,
    std::size_t rank, std::string_view text, const RankingFeatures& features) {
    out << "{\"pinyin\":"; quoted(out, pinyin);
    out << ",\"context\":"; quoted(out, context);
    out << ",\"rank\":" << rank << ",\"text\":"; quoted(out, text);
    out << ",\"feature_version\":" << kRankingFeatureVersion << ",\"features\":[" << std::setprecision(17);
    for (std::size_t i = 0; i < features.size(); ++i) out << (i ? "," : "") << features[i];
    out << "]}\n";
}

bool CandidateRanker::load_file(const std::filesystem::path& path, std::string& error) {
    try {
        std::ifstream input(path, std::ios::binary | std::ios::ate);
        require(bool(input), "Cannot open candidate ranker");
        const auto length = input.tellg();
        require(length >= 80 && length <= 2 * 1024 * 1024, "Invalid candidate ranker size");
        std::vector<uint8_t> bytes(static_cast<std::size_t>(length));
        input.seekg(0); input.read(reinterpret_cast<char*>(bytes.data()), bytes.size());
        require(bool(input), "Incomplete candidate ranker");
        const auto* data = bytes.data();
        require(std::memcmp(data, "CRNK0001", 8) == 0 && u32(data + 8) == 1 && u32(data + 12) == 80,
            "Unsupported candidate ranker format");
        require((uint64_t(u32(data + 16)) | uint64_t(u32(data + 20)) << 32) == bytes.size(), "Invalid ranker length");
        require(u32(data + 24) == kRankingFeatureCount && u32(data + 28) == kRankingFeatureVersion,
            "Incompatible ranking features");
        const uint32_t trees = u32(data + 32), nodes = u32(data + 36);
        require(trees && trees <= 512 && nodes && nodes <= 65536 &&
            80ull + trees * 8ull + nodes * 32ull == bytes.size(), "Invalid ranker sections");
        require(u32(data + 72) == 0 && u32(data + 76) == 0, "Unknown ranker flags");
        std::array<uint8_t, 32> digest{};
        picosha2::hash256(bytes.begin() + 80, bytes.end(), digest.begin(), digest.end());
        require(std::equal(digest.begin(), digest.end(), data + 40), "Candidate ranker checksum mismatch");
        CandidateRanker candidate;
        for (uint32_t i = 0; i < nodes; ++i) {
            const auto* p = data + 80 + trees * 8 + i * 32;
            Node node{int32_t(u32(p)), int32_t(u32(p + 4)), int32_t(u32(p + 8)), u32(p + 12), f64(p + 16), f64(p + 24)};
            require(std::isfinite(node.threshold) && std::isfinite(node.value) && node.flags <= 3, "Invalid ranker node");
            require(node.feature >= -1 && node.feature < int32_t(kRankingFeatureCount), "Invalid ranker feature");
            if (node.feature == -1) require(node.left == -1 && node.right == -1 && node.flags == 0 && node.threshold == 0,
                "Invalid ranker leaf");
            else require(node.value == 0, "Unexpected split value");
            candidate.nodes_.push_back(node);
        }
        uint32_t next = 0;
        for (uint32_t i = 0; i < trees; ++i) {
            const auto* p = data + 80 + i * 8;
            Tree tree{u32(p), u32(p + 4)};
            require(tree.start == next && tree.count && uint64_t(tree.start) + tree.count <= nodes, "Invalid ranker tree bounds");
            std::vector<bool> seen(tree.count, false);
            std::vector<std::pair<uint32_t, unsigned>> pending{{0, 0}};
            while (!pending.empty()) {
                const auto item = pending.back(); pending.pop_back();
                require(item.first < tree.count && !seen[item.first] && item.second <= 64, "Invalid ranker tree graph");
                seen[item.first] = true;
                const auto& node = candidate.nodes_[tree.start + item.first];
                if (node.feature != -1) {
                    require(node.left >= 0 && node.right >= 0, "Invalid ranker child");
                    pending.push_back({uint32_t(node.left), item.second + 1});
                    pending.push_back({uint32_t(node.right), item.second + 1});
                }
            }
            require(std::all_of(seen.begin(), seen.end(), [](bool hit) { return hit; }), "Unreachable ranker nodes");
            candidate.trees_.push_back(tree); next += tree.count;
        }
        require(next == nodes, "Unreferenced ranker nodes");
        *this = std::move(candidate); error.clear(); return true;
    } catch (const std::exception& failure) { error = failure.what(); return false; }
}

bool CandidateRanker::predict(const RankingFeatures& features, double& score) const noexcept {
    if (!valid()) return false;
    for (double value : features) if (std::isinf(value)) return false;
    double total = 0;
    for (const auto& tree : trees_) {
        uint32_t index = 0;
        while (true) {
            const auto& node = nodes_[tree.start + index];
            if (node.feature == -1) { total += node.value; break; }
            double value = features[node.feature];
            bool left;
            if (std::isnan(value) && (node.flags & 2)) left = (node.flags & 1) != 0;
            else {
                if (std::isnan(value)) value = 0;
                left = value <= node.threshold;
            }
            index = uint32_t(left ? node.left : node.right);
        }
    }
    if (!std::isfinite(total)) return false;
    score = total; return true;
}
} // 命名空间 coral::language_model 结束

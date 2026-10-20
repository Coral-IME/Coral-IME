#include "coral/language_model/surface_model.h"
#include "picosha2.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <limits>
#include <stdexcept>

namespace coral::language_model {
namespace {
constexpr uint64_t kMaxBytes = 2ull * 1024 * 1024 * 1024;
constexpr size_t kHeaderBytes = 128;
uint32_t read_u32(const uint8_t* p) {
    return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
}
uint64_t read_u64(const uint8_t* p) {
    return uint64_t(read_u32(p)) | uint64_t(read_u32(p + 4)) << 32;
}
double read_double(const uint8_t* p) {
    const uint64_t bits = read_u64(p);
    double result;
    static_assert(sizeof(result) == sizeof(bits) && std::numeric_limits<double>::is_iec559,
        "IEEE 754 binary64 required");
    std::memcpy(&result, &bits, sizeof(result));
    return result;
}
void require(bool condition, const char* error) {
    if (!condition) throw std::runtime_error(error);
}
bool probability_log(double value) { return std::isfinite(value) && value <= 0; }
SurfaceModel::Digest hash_bytes(const uint8_t* first, const uint8_t* last) {
    SurfaceModel::Digest digest{};
    picosha2::hash256(first, last, digest.begin(), digest.end());
    return digest;
}
}

bool SurfaceModel::load(const uint8_t* data, size_t size, const Digest& lexicon_digest, std::string& error) {
    try {
        require(data && size >= kHeaderBytes && size <= kMaxBytes, "Invalid model size");
        require(std::memcmp(data, "CSLM0001", 8) == 0 && read_u32(data + 8) == 1 &&
            read_u32(data + 12) == kHeaderBytes, "Unsupported surface model format");
        require(read_u64(data + 16) == size, "Model length mismatch");
        const uint32_t token_count = read_u32(data + 24), arc_count = read_u32(data + 28);
        const uint32_t phrase_count = read_u32(data + 32), sequence_count = read_u32(data + 36);
        const uint32_t unknown_state = read_u32(data + 40);
        require(token_count > 0 && token_count <= 2000000 && arc_count <= 50000000 &&
            phrase_count <= 5000000 && sequence_count <= 16000000 &&
            unknown_state > 0 && unknown_state <= token_count, "Model limits exceeded");
        require(read_u32(data + 44) == 0 && std::all_of(data + 112, data + 128,
            [](uint8_t b) { return b == 0; }), "Unknown model flags");
        require(std::any_of(lexicon_digest.begin(), lexicon_digest.end(), [](uint8_t b) { return b != 0; }) &&
            std::equal(lexicon_digest.begin(), lexicon_digest.end(), data + 48), "Lexicon identity mismatch");
        const uint64_t expected = kHeaderBytes + uint64_t(token_count) * 32 + uint64_t(arc_count) * 12 +
            uint64_t(phrase_count) * 12 + uint64_t(sequence_count) * 4;
        require(expected == size, "Noncanonical model sections");
        const auto digest = hash_bytes(data + kHeaderBytes, data + size);
        require(std::equal(digest.begin(), digest.end(), data + 80), "Model checksum mismatch");
        SurfaceModel candidate;
        candidate.unknown_state_ = unknown_state;
        candidate.tokens_.reserve(token_count);
        candidate.arcs_.reserve(arc_count);
        candidate.phrases_.reserve(phrase_count);
        candidate.sequences_.reserve(sequence_count);
        const uint8_t* p = data + kHeaderBytes;
        uint64_t offset = 0;
        double initial_mass = 0, lower_mass = 0;
        for (uint32_t i = 0; i < token_count; ++i, p += 32) {
            Token token{read_double(p), read_double(p + 8), read_double(p + 16),
                read_u32(p + 24), read_u32(p + 28)};
            require(probability_log(token.initial) && probability_log(token.lower) &&
                std::isfinite(token.backoff) && std::isfinite(std::exp(token.backoff)) &&
                std::exp(token.backoff) > 0 && std::exp(token.initial) > 0 && std::exp(token.lower) > 0,
                "Invalid token probability");
            require(token.offset == offset && offset + token.count <= arc_count, "Invalid arc section");
            offset += token.count;
            initial_mass += std::exp(token.initial);
            lower_mass += std::exp(token.lower);
            candidate.tokens_.push_back(token);
        }
        require(offset == arc_count && std::abs(initial_mass - 1) <= 1e-7 &&
            std::abs(lower_mass - 1) <= 1e-7, "Unnormalized unigram distribution");
        for (uint32_t i = 0; i < arc_count; ++i, p += 12) {
            Arc arc{read_u32(p), read_double(p + 4)};
            require(arc.target > 0 && arc.target <= token_count && probability_log(arc.weight) &&
                std::exp(arc.weight) > 0, "Invalid conditional arc");
            candidate.arcs_.push_back(arc);
        }
        double max_backoff = -std::numeric_limits<double>::infinity();
        std::vector<double> best(token_count);
        for (uint32_t i = 0; i < token_count; ++i) {
            const auto& token = candidate.tokens_[i];
            double kept = 0, kept_lower = 0;
            uint32_t previous = 0;
            for (uint32_t j = 0; j < token.count; ++j) {
                const auto& arc = candidate.arcs_[token.offset + j];
                require(arc.target > previous, "Duplicate or unsorted conditional arc");
                previous = arc.target;
                kept += std::exp(arc.weight);
                kept_lower += std::exp(candidate.tokens_[arc.target - 1].lower);
            }
            const double mass = kept + std::exp(token.backoff) * (1 - kept_lower);
            require(kept <= 1 + 1e-7 && kept_lower <= 1 + 1e-7 && std::abs(mass - 1) <= 1e-7,
                "Unnormalized conditional distribution");
            max_backoff = std::max(max_backoff, token.backoff);
        }
        // 未命中的全局上界允许宽松，不能低估任何实际条件概率。
        for (uint32_t i = 0; i < token_count; ++i)
            best[i] = std::max(candidate.tokens_[i].initial,
                std::min(0.0, max_backoff + candidate.tokens_[i].lower));
        for (const auto& arc : candidate.arcs_)
            best[arc.target - 1] = std::max(best[arc.target - 1], arc.weight);
        offset = 0;
        uint32_t previous_id = 0;
        for (uint32_t i = 0; i < phrase_count; ++i, p += 12) {
            Phrase phrase{read_u32(p), read_u32(p + 4), read_u32(p + 8), 0, 0};
            require((i == 0 || phrase.id > previous_id) && phrase.id < 0x80000000u &&
                phrase.offset == offset && phrase.count > 0 && phrase.count <= 64 &&
                offset + phrase.count <= sequence_count, "Invalid phrase mapping");
            previous_id = phrase.id;
            offset += phrase.count;
            candidate.phrases_.push_back(phrase);
        }
        require(offset == sequence_count, "Unreferenced token sequence");
        for (uint32_t i = 0; i < sequence_count; ++i, p += 4) {
            const uint32_t state = read_u32(p);
            require(state > 0 && state <= token_count, "Invalid phrase token");
            candidate.sequences_.push_back(state);
        }
        for (auto& phrase : candidate.phrases_) {
            for (uint32_t i = 1; i < phrase.count; ++i)
                phrase.internal_weight += candidate.score_token(candidate.sequences_[phrase.offset + i - 1],
                    candidate.sequences_[phrase.offset + i]);
            phrase.upper_bound = best[candidate.sequences_[phrase.offset] - 1] + phrase.internal_weight;
        }
        *this = std::move(candidate);
        error.clear();
        return true;
    } catch (const std::exception& failure) {
        error = failure.what();
        return false;
    }
}

bool SurfaceModel::load_file(const std::filesystem::path& model, const std::filesystem::path& lexicon,
    std::string& error) {
    try {
        std::ifstream lexicon_input(lexicon, std::ios::binary);
        require(bool(lexicon_input), "Cannot open bound lexicon");
        picosha2::hash256_one_by_one hasher;
        std::array<char, 65536> chunk;
        while (lexicon_input.read(chunk.data(), chunk.size()) || lexicon_input.gcount() > 0)
            hasher.process(chunk.begin(), chunk.begin() + lexicon_input.gcount());
        require(lexicon_input.eof() && !lexicon_input.bad(), "Cannot read bound lexicon");
        hasher.finish();
        Digest digest{};
        hasher.get_hash_bytes(digest.begin(), digest.end());
        std::ifstream input(model, std::ios::binary | std::ios::ate);
        require(bool(input), "Cannot open surface model");
        const auto length = input.tellg();
        require(length >= std::streamoff(kHeaderBytes) && uint64_t(length) <= kMaxBytes,
            "Invalid surface model file size");
        std::vector<uint8_t> bytes(static_cast<size_t>(length));
        input.seekg(0);
        input.read(reinterpret_cast<char*>(bytes.data()), bytes.size());
        require(bool(input) && input.peek() == std::char_traits<char>::eof(), "Cannot read complete surface model");
        return load(bytes.data(), bytes.size(), digest, error);
    } catch (const std::exception& failure) {
        error = failure.what();
        return false;
    }
}

double SurfaceModel::score_token(uint32_t context, uint32_t token, bool* hit) const {
    if (hit) *hit = false;
    if (token == 0 || token > tokens_.size() || context > tokens_.size())
        return -std::numeric_limits<double>::infinity();
    if (context == 0) return tokens_[token - 1].initial;
    const auto& previous = tokens_[context - 1];
    const auto begin = arcs_.begin() + previous.offset, end = begin + previous.count;
    const auto arc = std::lower_bound(begin, end, token,
        [](const Arc& a, uint32_t target) { return a.target < target; });
    if (arc != end && arc->target == token) {
        if (hit) *hit = true;
        return arc->weight;
    }
    return previous.backoff + tokens_[token - 1].lower;
}

const SurfaceModel::Phrase* SurfaceModel::find_phrase(uint32_t phrase_id) const {
    const auto item = std::lower_bound(phrases_.begin(), phrases_.end(), phrase_id,
        [](const Phrase& phrase, uint32_t id) { return phrase.id < id; });
    return item != phrases_.end() && item->id == phrase_id ? &*item : nullptr;
}

bool SurfaceModel::score_phrase(uint32_t context, uint32_t phrase_id, PhraseScore& result) const {
    const auto* phrase = find_phrase(phrase_id);
    if (!phrase || context > tokens_.size()) return false;
    PhraseScore candidate;
    candidate.log_probability = score_token(context, sequences_[phrase->offset], &candidate.first_arc_hit) +
        phrase->internal_weight;
    candidate.next_state = sequences_[phrase->offset + phrase->count - 1];
    result = candidate;
    return true;
}

uint32_t SurfaceModel::phrase_end(uint32_t phrase_id) const {
    const auto* phrase = find_phrase(phrase_id);
    return phrase ? sequences_[phrase->offset + phrase->count - 1] : 0;
}

double SurfaceModel::phrase_upper_bound(uint32_t phrase_id) const {
    const auto* phrase = find_phrase(phrase_id);
    return phrase ? phrase->upper_bound : -std::numeric_limits<double>::infinity();
}
} // 命名空间 coral::language_model 结束

#include "coral/language_model/pair_bonus_model.h"
#include "picosha2.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <fstream>
#include <limits>
#include <stdexcept>

namespace coral::language_model {
namespace {
constexpr uint64_t kMaxBytes = 512ull * 1024 * 1024;
uint32_t u32(const uint8_t* p) {
    return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
}
void require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
bool valid_word(std::string_view word) {
    if (word.empty() || word.size() > 4096) return false;
    for (size_t i = 0; i < word.size();) {
        const auto lead = static_cast<uint8_t>(word[i++]);
        if (lead < 0x80) { if (lead < 0x20 || lead == 0x7f) return false; continue; }
        const int extra = lead >= 0xc2 && lead <= 0xdf ? 1 : lead >= 0xe0 && lead <= 0xef ? 2 :
            lead >= 0xf0 && lead <= 0xf4 ? 3 : -1;
        if (extra < 0 || i + extra > word.size()) return false;
        uint32_t value = lead & (extra == 1 ? 31 : extra == 2 ? 15 : 7);
        for (int j = 0; j < extra; ++j) {
            const auto next = static_cast<uint8_t>(word[i++]);
            if ((next & 0xc0) != 0x80) return false;
            value = (value << 6) | (next & 63);
        }
        if ((extra == 2 && value < 0x800) || (extra == 3 && value < 0x10000) ||
            (value >= 0xd800 && value <= 0xdfff) || value > 0x10ffff) return false;
    }
    return true;
}
}

bool PairBonusModel::load(const uint8_t* data, size_t size, std::string& error) {
    try {
        require(data && size >= 80 && size <= kMaxBytes, "Invalid pair bonus size");
        require(std::memcmp(data, "CBGN0001", 8) == 0 && u32(data + 8) == 1 && u32(data + 12) == 80,
            "Unsupported pair bonus format");
        const uint64_t length = uint64_t(u32(data + 16)) | uint64_t(u32(data + 20)) << 32;
        const uint32_t words = u32(data + 24), pairs = u32(data + 28), pool_size = u32(data + 32);
        require(length == size && words <= 2000000 && pairs <= 20000000 &&
            80ull + 8ull * words + 12ull * pairs + pool_size == size, "Invalid pair bonus sections");
        require(u32(data + 36) == 0 && u32(data + 72) == 0 && u32(data + 76) == 0,
            "Unknown pair bonus flags");
        std::array<uint8_t, 32> digest{};
        picosha2::hash256(data + 80, data + size, digest.begin(), digest.end());
        require(std::equal(digest.begin(), digest.end(), data + 40), "Pair bonus checksum mismatch");
        PairBonusModel candidate;
        candidate.words_.reserve(words);
        candidate.pairs_.reserve(pairs);
        candidate.best_.assign(size_t(words) + 1, 0);
        const auto* pool = data + 80 + size_t(words) * 8 + size_t(pairs) * 12;
        uint64_t offset = 0;
        for (uint32_t i = 0; i < words; ++i) {
            const auto* p = data + 80 + size_t(i) * 8;
            const uint32_t start = u32(p), count = u32(p + 4);
            require(start == offset && offset + count <= pool_size, "Invalid pair bonus word bounds");
            std::string word(reinterpret_cast<const char*>(pool + start), count);
            require(valid_word(word) && candidate.words_.emplace(std::move(word), i + 1).second,
                "Invalid or duplicate pair bonus word");
            offset += count;
        }
        require(offset == pool_size, "Unreferenced pair bonus strings");
        uint64_t previous_key = 0;
        for (uint32_t i = 0; i < pairs; ++i) {
            const auto* p = data + 80 + size_t(words) * 8 + size_t(i) * 12;
            const auto previous = u32(p), next = u32(p + 4), bits = u32(p + 8);
            float bonus;
            static_assert(sizeof(bonus) == 4 && std::numeric_limits<float>::is_iec559, "binary32 required");
            std::memcpy(&bonus, &bits, 4);
            const uint64_t key = uint64_t(previous) << 32 | next;
            require(previous && previous <= words && next && next <= words && key > previous_key &&
                std::isfinite(bonus) && bonus >= 0 && bonus <= 64, "Invalid pair bonus entry");
            previous_key = key;
            candidate.pairs_.push_back({key, bonus});
            candidate.best_[next] = std::max(candidate.best_[next], bonus);
            candidate.max_bonus_ = std::max(candidate.max_bonus_, double(bonus));
        }
        *this = std::move(candidate);
        error.clear();
        return true;
    } catch (const std::exception& failure) { error = failure.what(); return false; }
}

bool PairBonusModel::load_file(const std::filesystem::path& path, std::string& error) {
    try {
        std::ifstream input(path, std::ios::binary | std::ios::ate);
        require(bool(input), "Cannot open pair bonus model");
        const auto length = input.tellg();
        require(length >= 80 && uint64_t(length) <= kMaxBytes, "Invalid pair bonus file size");
        std::vector<uint8_t> bytes(static_cast<size_t>(length));
        input.seekg(0);
        input.read(reinterpret_cast<char*>(bytes.data()), bytes.size());
        require(bool(input) && input.peek() == std::char_traits<char>::eof(), "Incomplete pair bonus file");
        return load(bytes.data(), bytes.size(), error);
    } catch (const std::exception& failure) { error = failure.what(); return false; }
}

uint32_t PairBonusModel::word_state(std::string_view word) const {
    const auto found = words_.find(std::string(word));
    return found == words_.end() ? 0 : found->second;
}
double PairBonusModel::bonus(uint32_t previous, uint32_t next) const {
    if (!previous || !next) return 0;
    const uint64_t key = uint64_t(previous) << 32 | next;
    const auto found = std::lower_bound(pairs_.begin(), pairs_.end(), key,
        [](const Pair& pair, uint64_t wanted) { return pair.key < wanted; });
    return found != pairs_.end() && found->key == key ? found->bonus : 0;
}
double PairBonusModel::best_bonus(uint32_t next) const { return next < best_.size() ? best_[next] : 0; }
}

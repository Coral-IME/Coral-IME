#ifndef CORAL_INCLUDE_CORAL_LANGUAGE_MODEL_PAIR_BONUS_MODEL_H_INCLUDED
#define CORAL_INCLUDE_CORAL_LANGUAGE_MODEL_PAIR_BONUS_MODEL_H_INCLUDED
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace coral::language_model {
// 独立词面增益表：状态零表示无上下文，未命中严格返回零，不包含一元或回退。
class PairBonusModel {
public:
    bool load(const uint8_t* data, size_t size, std::string& error);
    bool load_file(const std::filesystem::path& path, std::string& error);
    uint32_t word_state(std::string_view word) const;
    double bonus(uint32_t previous, uint32_t next) const;
    double best_bonus(uint32_t next) const;
    double max_bonus() const { return max_bonus_; }
    size_t pair_count() const { return pairs_.size(); }
    void reset() { *this = PairBonusModel{}; }
private:
    struct Pair { uint64_t key; float bonus; };
    std::unordered_map<std::string, uint32_t> words_;
    std::vector<Pair> pairs_;
    std::vector<float> best_;
    double max_bonus_ = 0;
};
}
#endif

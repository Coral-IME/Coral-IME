#ifndef CORAL_INCLUDE_CORAL_LANGUAGE_MODEL_CHARACTER_MODEL_H_INCLUDED
#define CORAL_INCLUDE_CORAL_LANGUAGE_MODEL_CHARACTER_MODEL_H_INCLUDED

#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace coral::language_model {

// 外置字符因果模型；实例拥有权重，单次评分的前缀缓存不跨请求存活。
class CharacterModel {
public:
    CharacterModel();
    ~CharacterModel();
    CharacterModel(CharacterModel&&) noexcept;
    CharacterModel& operator=(CharacterModel&&) noexcept;
    CharacterModel(const CharacterModel&) = delete;
    CharacterModel& operator=(const CharacterModel&) = delete;
    bool load_file(const std::filesystem::path& path, std::string& error);
    bool valid() const noexcept;
    // 常驻权重数组的字节数，不包含词表哈希表与单次评分工作区。
    std::size_t weight_bytes() const noexcept;
    // 不支持的候选返回 NaN；资源预算失败返回 false，调用方保留原排序。
    bool score_candidates(std::string_view context, const std::vector<std::string_view>& texts,
        std::vector<double>& scores, const std::function<bool()>& cancelled = {}) const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // 命名空间 coral::language_model 结束
#endif

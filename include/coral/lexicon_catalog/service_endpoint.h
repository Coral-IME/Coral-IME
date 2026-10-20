#pragma once
#include <cstdint>
#include <string>

namespace coral::lexicon_catalog {
// 空的 base 表示禁用服务。非法的非空配置抛出 invalid_argument。
class LexiconService {
public:
    explicit LexiconService(std::string base = {});
    bool available() const noexcept { return !base_.empty(); }
    const std::string& base() const noexcept { return base_; }
    std::string catalog_url() const;
    std::string location_url(std::uint32_t id) const;
private:
    std::string base_;
};
const LexiconService& configured_lexicon_service();
}

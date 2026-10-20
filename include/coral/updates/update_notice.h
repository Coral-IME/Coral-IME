#pragma once
#include "coral/updates/release_metadata.h"
#include <optional>

namespace coral::updates {
// 版本 1 保留发布页通知；版本 2 携带固定安装包的校验元数据。
struct UpdateNotice {
    ReleaseVersion version;
    std::string description;
    std::string release_url;
    std::optional<ReleaseMetadata> package;
};
bool is_release_page_url(std::string_view url) noexcept;
// 事务式 UTF-8 解析；版本 2 的安装包字段必须完整且有效。
bool parse_update_notice(std::string_view json, UpdateNotice& result);
bool auto_check_due(std::int64_t now_utc, std::int64_t last_attempt_utc) noexcept;
}

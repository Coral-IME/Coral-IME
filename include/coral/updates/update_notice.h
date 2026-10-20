#pragma once
#include "coral/updates/release_metadata.h"

namespace coral::updates {
// 仅检测协议。绝不包含可执行路径或下载任务。
struct UpdateNotice {
    ReleaseVersion version;
    std::string description;
    std::string release_url;
};
bool is_release_page_url(std::string_view url) noexcept;
// schema_version=1，data.version/description/release_url；事务式 UTF-8 解析。
bool parse_update_notice(std::string_view json, UpdateNotice& result);
bool auto_check_due(std::int64_t now_utc, std::int64_t last_attempt_utc) noexcept;
}

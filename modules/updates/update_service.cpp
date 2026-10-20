#include "coral/updates/update_service.h"
#include "update_service_config.h"

namespace coral::updates {
bool update_service_available() noexcept { return kUpdateServiceBase[0] != '\0'; }
std::string release_check_url() {
    return update_service_available() ? std::string(kUpdateServiceBase) + "/api/v1/version/latest" : std::string{};
}
std::string download_location_url(std::uint32_t release_id) {
    if (!update_service_available() || !release_id || release_id > INT32_MAX) return {};
    return std::string(kUpdateServiceBase) + "/api/v1/version/getDownloadUrl?Id=" + std::to_string(release_id);
}
}

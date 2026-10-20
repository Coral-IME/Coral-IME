#pragma once
#include <cstdint>
#include <string>

namespace coral::updates {
bool update_service_available() noexcept;
std::string release_check_url();
std::string download_location_url(std::uint32_t release_id);
}

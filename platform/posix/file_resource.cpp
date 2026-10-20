#include "resource_mapping_backend.h"
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <limits>
#include <cstdint>

namespace coral::storage::detail {
namespace {
struct File {
    int descriptor;
    ~File() { if (descriptor >= 0) close(descriptor); }
};
struct Lease {
    void* address = MAP_FAILED;
    std::size_t size = 0;
    ~Lease() { if (address != MAP_FAILED) munmap(address, size); }
};
} // namespace

std::optional<Mapping> open_mapping(const std::wstring&, const std::filesystem::path& path,
    const std::string& checksum, ResourceAccess) {
    File file{open(path.c_str(), O_RDONLY)};
    struct stat metadata{};
    if (file.descriptor < 0 || fstat(file.descriptor, &metadata) != 0 ||
        !S_ISREG(metadata.st_mode) || metadata.st_size <= 0 ||
        static_cast<std::uintmax_t>(metadata.st_size) > std::numeric_limits<std::size_t>::max())
        return std::nullopt;
    const auto size = static_cast<std::size_t>(metadata.st_size);
    auto lease = std::make_shared<Lease>();
    void* address = mmap(nullptr, size, PROT_READ, MAP_PRIVATE, file.descriptor, 0);
    if (address == MAP_FAILED) return std::nullopt;
    lease->address = address;
    lease->size = size;
    if (!matches_checksum(address, size, checksum)) return std::nullopt;
    return Mapping{lease, address, size};
}
} // 命名空间 coral::storage::detail 结束

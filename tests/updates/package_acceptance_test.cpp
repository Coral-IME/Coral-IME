#include "package_acceptance.h"
#include <windows.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool value) { if (!value) throw std::runtime_error("Package acceptance fixture failed"); }
void write(const std::filesystem::path& path, const std::string& data) {
    std::ofstream file(path, std::ios::binary); file << data; file.close(); require(!file.fail());
}
std::string read(const std::filesystem::path& path) {
    std::ifstream file(path,std::ios::binary); return std::string(std::istreambuf_iterator<char>(file),{});
}
}
int wmain(int argc, wchar_t** argv) {
    using namespace coral::updates;
    try {
        require(argc == 2);
        const auto root = std::filesystem::absolute(argv[1]) / (L"人工 校验_" + std::to_wstring(::GetCurrentProcessId()) +
            L"_" + std::to_wstring(::GetTickCount64()));
        std::filesystem::create_directories(root);
        const auto source = root / L"pending.tmp", destination = root / L"package.exe";
        ReleaseMetadata metadata;
        require(parse_release_metadata("{\"data\":{\"id\":7,\"version\":\"1.2.3.4\",\"fileSize\":3,\"fileSha256\":\""
            "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad\"}}", metadata));
        const auto expected = metadata.package;
        require(verify_package(source, expected) == PackageStatus::kOpenFailed);
        require(verify_package("relative.tmp", expected) == PackageStatus::kInvalidRequest);
        auto invalid = expected; invalid.byte_count = 0;
        require(verify_package(source, invalid) == PackageStatus::kInvalidRequest);
        write(source, "ab"); require(verify_package(source, expected) == PackageStatus::kSizeMismatch);
        write(source, "abd"); require(verify_package(source, expected) == PackageStatus::kHashMismatch);
        require(publish_package(source, destination, expected) == PackageStatus::kHashMismatch);
        require(read(source) == "abd" && !std::filesystem::exists(destination));
        write(source, "abc"); require(verify_package(source, expected) == PackageStatus::kReady);
        require(verify_package(source, expected, [] { return true; }) == PackageStatus::kCancelled);
        require(publish_package(source, destination, expected, [] { return true; }) == PackageStatus::kCancelled);
        require(read(source) == "abc" && !std::filesystem::exists(destination));
        HANDLE writer = ::CreateFileW(source.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        require(writer != INVALID_HANDLE_VALUE);
        const auto locked = verify_package(source, expected);
        ::CloseHandle(writer); require(locked == PackageStatus::kOpenFailed);
        write(destination, "keep existing");
        require(publish_package(source, destination, expected) == PackageStatus::kPublishFailed);
        require(read(source) == "abc" && read(destination) == "keep existing");
        const auto ready = root / L"新 发布.exe";
        require(publish_package(source, ready, expected) == PackageStatus::kReady);
        require(!std::filesystem::exists(source) && read(ready) == "abc");
        require(verify_package(ready, expected) == PackageStatus::kReady);
        require(publish_package(ready, ready, expected) == PackageStatus::kInvalidRequest);
        require(publish_package(ready, root.parent_path() / "outside.exe", expected) == PackageStatus::kInvalidRequest);
        write(ready, "abd"); require(verify_package(ready, expected) == PackageStatus::kHashMismatch);
        require(read(destination) == "keep existing");
        std::cout << "Actual CNG digest, write sharing, same-handle publication and existing-file preservation passed\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
    return 0;
}

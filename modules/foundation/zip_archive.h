#pragma once
#include <filesystem>
#include <string>
#include <vector>

namespace coral::archive {
struct Entry { std::string name; std::filesystem::path source; };
// UTF-8 条目名。发布完全定稿的压缩包，保留既有目标文件。
bool writeZip(const std::filesystem::path& destination,
              const std::vector<Entry>& entries, std::string& error);
}

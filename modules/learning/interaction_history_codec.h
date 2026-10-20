#pragma once

#include "interaction_history_types.h"

#include <cstdint>
#include <string>
#include <vector>

namespace coral::history {

struct DecodedHistory {
    std::uint64_t generation = 0;
    std::vector<Entry> entries;
};

class Codec final {
public:
    static bool Encode(std::uint64_t generation, const std::vector<Entry>& entries,
                       std::string& output, std::string& error);
    static bool Decode(const std::string& input, DecodedHistory& output, std::string& error);
};

class LegacyCodec final {
public:
    static bool ReadFile(const std::string& filename, Partition partition,
                         std::vector<Entry>& output, std::string& error);
};

}  // 命名空间 coral::history 结束

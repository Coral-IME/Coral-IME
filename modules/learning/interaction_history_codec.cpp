#include "interaction_history_codec.h"

#include <array>
#include <algorithm>
#include <cstring>
#include <fstream>
#include <limits>
#include <type_traits>

namespace coral::history {
namespace {

constexpr std::array<char, 8> kMagic{{'C', 'R', 'L', 'H', 'I', 'S', 'T', '\0'}};
constexpr std::uint16_t kSchemaMajor = 2;
constexpr std::uint16_t kSchemaMinor = 0;
constexpr std::uint32_t kHeaderSize = 64;
constexpr std::size_t kMaxPayloadSize = 256u * 1024u * 1024u;
constexpr std::size_t kMaxStringSize = 1024u * 1024u;

std::uint32_t Crc32(const char* data, std::size_t size) {
    std::uint32_t crc = 0xffffffffu;
    for (std::size_t i = 0; i < size; ++i) {
        crc ^= static_cast<unsigned char>(data[i]);
        for (int bit = 0; bit < 8; ++bit) {
            crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
        }
    }
    return ~crc;
}

template <typename T>
void Append(std::string& output, T value) {
    static_assert(std::is_integral_v<T>);
    using U = std::make_unsigned_t<T>;
    U bits = static_cast<U>(value);
    for (std::size_t i = 0; i < sizeof(T); ++i) {
        output.push_back(static_cast<char>((bits >> (i * 8)) & 0xffu));
    }
}

void AppendFloat(std::string& output, float value) {
    std::uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    Append(output, bits);
}

template <typename T>
bool Read(const std::string& input, std::size_t& pos, T& value) {
    static_assert(std::is_integral_v<T>);
    if (pos > input.size() || input.size() - pos < sizeof(T)) {
        return false;
    }
    using U = std::make_unsigned_t<T>;
    U bits = 0;
    for (std::size_t i = 0; i < sizeof(T); ++i) {
        bits |= static_cast<U>(static_cast<unsigned char>(input[pos + i])) << (i * 8);
    }
    value = static_cast<T>(bits);
    pos += sizeof(T);
    return true;
}

bool ReadFloat(const std::string& input, std::size_t& pos, float& value) {
    std::uint32_t bits = 0;
    if (!Read(input, pos, bits)) {
        return false;
    }
    std::memcpy(&value, &bits, sizeof(value));
    return true;
}

bool ValidPartition(std::uint8_t partition) {
    return partition <= static_cast<std::uint8_t>(Partition::TraditionalPolicy);
}

bool ReadBytes(const std::string& input, std::size_t& pos, std::uint32_t size, std::string& value) {
    if (size > kMaxStringSize || pos > input.size() || input.size() - pos < size) {
        return false;
    }
    value.assign(input.data() + pos, size);
    pos += size;
    return true;
}

bool LoadBytes(const std::string& filename, std::string& bytes) {
    std::ifstream input(filename, std::ios::binary);
    if (!input) {
        return false;
    }
    input.seekg(0, std::ios::end);
    const auto end = input.tellg();
    if (end < 0 || static_cast<std::uint64_t>(end) > kMaxPayloadSize + kHeaderSize) {
        return false;
    }
    bytes.resize(static_cast<std::size_t>(end));
    input.seekg(0, std::ios::beg);
    if (!bytes.empty()) {
        input.read(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    }
    return input.good() || input.eof();
}

}  // namespace

bool Codec::Encode(std::uint64_t generation, const std::vector<Entry>& entries,
                   std::string& output, std::string& error) {
    error.clear();
    std::string payload;
    for (const Entry& entry : entries) {
        if (entry.lookup_key.size() > kMaxStringSize || entry.full_pinyin.size() > kMaxStringSize ||
            entry.phrase.size() > kMaxStringSize) {
            error = "user history string is too large";
            return false;
        }
        const std::uint64_t recordSize64 = 40ull + entry.lookup_key.size() + entry.full_pinyin.size() + entry.phrase.size();
        if (recordSize64 > std::numeric_limits<std::uint32_t>::max()) {
            error = "user history record is too large";
            return false;
        }
        Append(payload, static_cast<std::uint32_t>(recordSize64));
        Append(payload, static_cast<std::uint8_t>(entry.partition));
        Append(payload, entry.flags);
        Append(payload, static_cast<std::uint8_t>(0));
        Append(payload, entry.commit_count);
        Append(payload, entry.last_used_sequence);
        AppendFloat(payload, entry.score_state);
        Append(payload, entry.candidate_position);
        Append(payload, static_cast<std::uint32_t>(entry.lookup_key.size()));
        Append(payload, static_cast<std::uint32_t>(entry.full_pinyin.size()));
        Append(payload, static_cast<std::uint32_t>(entry.phrase.size()));
        payload.append(entry.lookup_key);
        payload.append(entry.full_pinyin);
        payload.append(entry.phrase);
        if (payload.size() > kMaxPayloadSize) {
            error = "user history payload exceeds safety limit";
            return false;
        }
    }

    output.assign(kMagic.data(), kMagic.size());
    Append(output, kSchemaMajor);
    Append(output, kSchemaMinor);
    Append(output, kHeaderSize);
    Append(output, generation);
    Append(output, static_cast<std::uint64_t>(0));  // v2.0 中创建时间可选
    Append(output, static_cast<std::uint64_t>(entries.size()));
    Append(output, static_cast<std::uint64_t>(payload.size()));
    Append(output, Crc32(payload.data(), payload.size()));
    Append(output, static_cast<std::uint32_t>(0));  // 文件头 CRC 占位
    Append(output, static_cast<std::uint32_t>(0));
    while (output.size() < kHeaderSize) {
        output.push_back('\0');
    }
    const std::uint32_t headerCrc = Crc32(output.data(), output.size());
    for (std::size_t i = 0; i < sizeof(headerCrc); ++i) {
        output[52 + i] = static_cast<char>((headerCrc >> (i * 8)) & 0xffu);
    }
    output.append(payload);
    return true;
}

bool Codec::Decode(const std::string& input, DecodedHistory& output, std::string& error) {
    error.clear();
    output = {};
    if (input.size() < kHeaderSize || !std::equal(kMagic.begin(), kMagic.end(), input.begin())) {
        error = "invalid user history magic";
        return false;
    }
    std::size_t pos = kMagic.size();
    std::uint16_t major = 0, minor = 0;
    std::uint32_t headerSize = 0;
    std::uint64_t created = 0, recordCount = 0, payloadSize = 0;
    std::uint32_t payloadCrc = 0, headerCrc = 0, flags = 0;
    if (!Read(input, pos, major) || !Read(input, pos, minor) || !Read(input, pos, headerSize) ||
        !Read(input, pos, output.generation) || !Read(input, pos, created) ||
        !Read(input, pos, recordCount) || !Read(input, pos, payloadSize) ||
        !Read(input, pos, payloadCrc) || !Read(input, pos, headerCrc) || !Read(input, pos, flags)) {
        error = "truncated user history header";
        return false;
    }
    if (major != kSchemaMajor || headerSize < kHeaderSize || headerSize > input.size() ||
        payloadSize > kMaxPayloadSize || input.size() - headerSize != payloadSize || recordCount > 100000) {
        error = "unsupported or invalid user history header";
        return false;
    }
    std::string header(input.data(), headerSize);
    std::fill(header.begin() + 52, header.begin() + 56, '\0');
    if (Crc32(header.data(), header.size()) != headerCrc ||
        Crc32(input.data() + headerSize, static_cast<std::size_t>(payloadSize)) != payloadCrc) {
        error = "user history checksum mismatch";
        return false;
    }
    pos = headerSize;
    output.entries.reserve(static_cast<std::size_t>(recordCount));
    for (std::uint64_t i = 0; i < recordCount; ++i) {
        const std::size_t recordStart = pos;
        std::uint32_t recordSize = 0;
        std::uint8_t partition = 0, reserved = 0;
        Entry entry;
        std::uint32_t lookupSize = 0, fullSize = 0, phraseSize = 0;
        if (!Read(input, pos, recordSize) || recordSize < 40 || !Read(input, pos, partition) ||
            !Read(input, pos, entry.flags) || !Read(input, pos, reserved) ||
            !Read(input, pos, entry.commit_count) || !Read(input, pos, entry.last_used_sequence) ||
            !ReadFloat(input, pos, entry.score_state) || !Read(input, pos, entry.candidate_position) ||
            !Read(input, pos, lookupSize) || !Read(input, pos, fullSize) || !Read(input, pos, phraseSize) ||
            !ValidPartition(partition) || !ReadBytes(input, pos, lookupSize, entry.lookup_key) ||
            !ReadBytes(input, pos, fullSize, entry.full_pinyin) || !ReadBytes(input, pos, phraseSize, entry.phrase) ||
            pos - recordStart != recordSize) {
            error = "invalid user history record";
            return false;
        }
        entry.partition = static_cast<Partition>(partition);
        entry.entry_id = EntryId(IdentityOf(entry));
        if (entry.lookup_key.empty() || entry.phrase.empty()) {
            error = "empty user history identity";
            return false;
        }
        output.entries.push_back(std::move(entry));
    }
    if (pos != input.size()) {
        error = "trailing user history data";
        return false;
    }
    return true;
}

bool LegacyCodec::ReadFile(const std::string& filename, Partition partition,
                           std::vector<Entry>& output, std::string& error) {
    error.clear();
    std::string input;
    if (!LoadBytes(filename, input)) {
        error = "cannot open legacy user history";
        return false;
    }
    // 旧文件头为 float 版本、int32 tick、int32 数量、uint32 载荷 CRC。
    if (input.size() < 16) {
        error = "legacy user history is truncated";
        return false;
    }
    std::int32_t globalTick = 0, count = 0;
    std::uint32_t storedCrc = 0;
    std::memcpy(&globalTick, input.data() + 4, 4);
    std::memcpy(&count, input.data() + 8, 4);
    std::memcpy(&storedCrc, input.data() + 12, 4);
    if (count < 0 || count > 100000 || Crc32(input.data() + 16, input.size() - 16) != storedCrc) {
        error = "invalid legacy user history header";
        return false;
    }
    if (count == 0) {
        return true;
    }
    std::size_t pos = 16;
    auto readBlock = [&](std::string& joined, std::vector<std::uint8_t>& sizes) {
        const auto newline = input.find('\n', pos);
        if (newline == std::string::npos) return false;
        joined.assign(input.data() + pos, newline - pos);
        pos = newline + 1;
        if (input.size() - pos < static_cast<std::size_t>(count)) return false;
        sizes.assign(input.begin() + pos, input.begin() + pos + count);
        pos += static_cast<std::size_t>(count);
        return true;
    };
    std::string keys, words, full;
    std::vector<std::uint8_t> keySizes, wordSizes, fullSizes;
    if (!readBlock(keys, keySizes) || !readBlock(words, wordSizes) || !readBlock(full, fullSizes)) {
        error = "invalid legacy string blocks";
        return false;
    }
    const std::size_t arraysSize = static_cast<std::size_t>(count) * (4 + 4 + 4 + 4);
    if (input.size() - pos < arraysSize) {
        error = "invalid legacy numeric blocks";
        return false;
    }
    const char* commits = input.data() + pos;
    const char* ticks = commits + count * 4;
    const char* scores = ticks + count * 4;
    const char* positions = scores + count * 4;
    std::size_t keyOffset = 0, wordOffset = 0, fullOffset = 0;
    for (int i = 0; i < count; ++i) {
        if (keyOffset + keySizes[i] > keys.size() || wordOffset + wordSizes[i] > words.size() ||
            fullOffset + fullSizes[i] > full.size()) {
            error = "invalid legacy string offsets";
            return false;
        }
        Entry entry;
        entry.partition = partition;
        entry.lookup_key.assign(keys.data() + keyOffset, keySizes[i]);
        entry.phrase.assign(words.data() + wordOffset, wordSizes[i]);
        entry.full_pinyin.assign(full.data() + fullOffset, fullSizes[i]);
        std::memcpy(&entry.commit_count, commits + i * 4, 4);
        std::uint32_t tick = 0;
        std::memcpy(&tick, ticks + i * 4, 4);
        entry.last_used_sequence = tick;
        std::memcpy(&entry.score_state, scores + i * 4, 4);
        std::memcpy(&entry.candidate_position, positions + i * 4, 4);
        if (partition == Partition::SimplifiedPolicy || partition == Partition::TraditionalPolicy) {
            if (entry.candidate_position == 0) entry.flags |= kDeleted;
            if (entry.candidate_position == 1) entry.flags |= kFixedCandidate;
        }
        entry.entry_id = EntryId(IdentityOf(entry));
        output.push_back(std::move(entry));
        keyOffset += keySizes[i];
        wordOffset += wordSizes[i];
        fullOffset += fullSizes[i];
    }
    static_cast<void>(globalTick);
    return true;
}

}  // 命名空间 coral::history 结束

#include "candidate_rules_codec.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <type_traits>

namespace coral::candidate_policy {
namespace {

constexpr std::array<char, 8> kMagic{{'C', 'R', 'L', 'P', 'O', 'L', 'Y', '\0'}};
constexpr std::uint16_t kSchemaMajor = 1;
constexpr std::uint16_t kSchemaMinor = 0;
constexpr std::uint32_t kHeaderSize = 64;
constexpr std::size_t kMaxPayloadSize = 256u * 1024u * 1024u;
constexpr std::size_t kMaxStringSize = 1024u * 1024u;
constexpr std::uint64_t kMaxRecordCount = 1000000;

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

template <typename T>
bool Read(const std::string& input, std::size_t& pos, T& value) {
    static_assert(std::is_integral_v<T>);
    if (pos > input.size() || input.size() - pos < sizeof(T)) return false;
    using U = std::make_unsigned_t<T>;
    U bits = 0;
    for (std::size_t i = 0; i < sizeof(T); ++i) {
        bits |= static_cast<U>(static_cast<unsigned char>(input[pos + i])) << (i * 8);
    }
    value = static_cast<T>(bits);
    pos += sizeof(T);
    return true;
}

bool ReadBytes(const std::string& input, std::size_t& pos, std::uint32_t size, std::string& value) {
    if (size > kMaxStringSize || pos > input.size() || input.size() - pos < size) return false;
    value.assign(input.data() + pos, size);
    pos += size;
    return true;
}

bool ValidVariant(std::uint8_t value) {
    return value <= static_cast<std::uint8_t>(LanguageVariant::Traditional);
}

bool ValidKind(std::uint8_t value) {
    return value <= static_cast<std::uint8_t>(RuleKind::Disable);
}

}  // namespace

bool Codec::Encode(std::uint64_t generation, const std::vector<Rule>& rules,
                   std::string& output, std::string& error) {
    error.clear();
    std::string payload;
    for (const Rule& rule : rules) {
        if (rule.lookup_key.empty() || rule.phrase.empty() ||
            rule.lookup_key.size() > kMaxStringSize || rule.full_pinyin.size() > kMaxStringSize ||
            rule.phrase.size() > kMaxStringSize) {
            error = "invalid candidate policy string";
            return false;
        }
        const std::uint64_t recordSize64 = 40ull + rule.lookup_key.size() +
                                           rule.full_pinyin.size() + rule.phrase.size();
        if (recordSize64 > std::numeric_limits<std::uint32_t>::max()) {
            error = "candidate policy record is too large";
            return false;
        }
        Append(payload, static_cast<std::uint32_t>(recordSize64));
        Append(payload, static_cast<std::uint8_t>(rule.variant));
        Append(payload, static_cast<std::uint8_t>(rule.kind));
        Append(payload, static_cast<std::uint8_t>(rule.pinned ? 1 : 0));
        Append(payload, static_cast<std::uint8_t>(0));
        // 为兼容保留 V1 磁盘槽位；所有提升的规则都排第一。
        Append(payload, static_cast<std::int32_t>(rule.kind == RuleKind::Disable ? 0 : 1));
        Append(payload, rule.updated_sequence);
        Append(payload, static_cast<std::uint64_t>(0));
        Append(payload, static_cast<std::uint32_t>(rule.lookup_key.size()));
        Append(payload, static_cast<std::uint32_t>(rule.full_pinyin.size()));
        Append(payload, static_cast<std::uint32_t>(rule.phrase.size()));
        payload.append(rule.lookup_key);
        payload.append(rule.full_pinyin);
        payload.append(rule.phrase);
        if (payload.size() > kMaxPayloadSize) {
            error = "candidate policy payload exceeds safety limit";
            return false;
        }
    }

    output.assign(kMagic.data(), kMagic.size());
    Append(output, kSchemaMajor);
    Append(output, kSchemaMinor);
    Append(output, kHeaderSize);
    Append(output, generation);
    Append(output, static_cast<std::uint64_t>(0));
    Append(output, static_cast<std::uint64_t>(rules.size()));
    Append(output, static_cast<std::uint64_t>(payload.size()));
    Append(output, Crc32(payload.data(), payload.size()));
    Append(output, static_cast<std::uint32_t>(0));
    Append(output, static_cast<std::uint32_t>(0));
    while (output.size() < kHeaderSize) output.push_back('\0');
    const std::uint32_t headerCrc = Crc32(output.data(), output.size());
    for (std::size_t i = 0; i < sizeof(headerCrc); ++i) {
        output[52 + i] = static_cast<char>((headerCrc >> (i * 8)) & 0xffu);
    }
    output.append(payload);
    return true;
}

bool Codec::Decode(const std::string& input, DecodedPolicy& output, std::string& error) {
    error.clear();
    output = {};
    if (input.size() < kHeaderSize || !std::equal(kMagic.begin(), kMagic.end(), input.begin())) {
        error = "invalid candidate policy magic";
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
        error = "truncated candidate policy header";
        return false;
    }
    if (major != kSchemaMajor || headerSize < kHeaderSize || headerSize > input.size() ||
        payloadSize > kMaxPayloadSize || input.size() - headerSize != payloadSize ||
        recordCount > kMaxRecordCount) {
        error = "unsupported or invalid candidate policy header";
        return false;
    }
    std::string header(input.data(), headerSize);
    std::fill(header.begin() + 52, header.begin() + 56, '\0');
    if (Crc32(header.data(), header.size()) != headerCrc ||
        Crc32(input.data() + headerSize, static_cast<std::size_t>(payloadSize)) != payloadCrc) {
        error = "candidate policy checksum mismatch";
        return false;
    }

    pos = headerSize;
    output.rules.reserve(static_cast<std::size_t>(recordCount));
    for (std::uint64_t i = 0; i < recordCount; ++i) {
        const std::size_t recordStart = pos;
        std::uint32_t recordSize = 0;
        std::uint8_t variant = 0, kind = 0, pinned = 0, reserved = 0;
        std::uint64_t reserved64 = 0;
        std::int32_t legacyPosition = 0;
        std::uint32_t lookupSize = 0, fullSize = 0, phraseSize = 0;
        Rule rule;
        if (!Read(input, pos, recordSize) || recordSize < 40 ||
            !Read(input, pos, variant) || !Read(input, pos, kind) ||
            !Read(input, pos, pinned) || !Read(input, pos, reserved) ||
            !Read(input, pos, legacyPosition) || !Read(input, pos, rule.updated_sequence) ||
            !Read(input, pos, reserved64) || !Read(input, pos, lookupSize) ||
            !Read(input, pos, fullSize) || !Read(input, pos, phraseSize) ||
            !ValidVariant(variant) || !ValidKind(kind) || pinned > 1 ||
            !ReadBytes(input, pos, lookupSize, rule.lookup_key) ||
            !ReadBytes(input, pos, fullSize, rule.full_pinyin) ||
            !ReadBytes(input, pos, phraseSize, rule.phrase) ||
            pos - recordStart != recordSize || rule.lookup_key.empty() || rule.phrase.empty()) {
            error = "invalid candidate policy record";
            return false;
        }
        rule.variant = static_cast<LanguageVariant>(variant);
        rule.kind = static_cast<RuleKind>(kind);
        rule.pinned = pinned != 0;
        if (rule.kind == RuleKind::Disable) {
            rule.pinned = false;
        } else if (legacyPosition < 1) {
            error = "invalid legacy promoted candidate position";
            return false;
        }
        // 大于 1 的旧位置现在具有第一位置的语义。
        rule.rule_id = RuleId(IdentityOf(rule));
        output.rules.push_back(std::move(rule));
    }
    if (pos != input.size()) {
        error = "trailing candidate policy data";
        return false;
    }
    return true;
}

}  // 命名空间 coral::candidate_policy 结束

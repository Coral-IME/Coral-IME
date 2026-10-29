// CSTPACK1安装端还原：分值按位保留，完整校验后再替换固定名称的资源对。
#include <windows.h>
#include <bcrypt.h>
#include <algorithm>
#include <array>
#include <cstdint>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
#include "xz.h"

namespace {
namespace fs = std::filesystem;
using Bytes = std::vector<uint8_t>;
using Digest = std::array<uint8_t, 32>;
constexpr uint64_t kMaxBytes = uint64_t{1} << 30;
constexpr size_t kHeaderSize = 144;

void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}

uint64_t little(const uint8_t* p, size_t width) {
    uint64_t value = 0;
    for (size_t j = 0; j < width; ++j) value |= uint64_t(p[j]) << (j * 8);
    return value;
}

void store(uint8_t* p, uint64_t value, size_t width) {
    for (size_t j = 0; j < width; ++j) p[j] = uint8_t(value >> (j * 8));
}

struct Hash {
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    Bytes object;
    ~Hash() {
        if (hash) BCryptDestroyHash(hash);
        if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0);
    }
};

Digest sha256(const uint8_t* data, size_t size) {
    Hash context;
    require(BCryptOpenAlgorithmProvider(&context.algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) >= 0, "Cannot open SHA256 provider");
    DWORD length = 0, copied = 0;
    require(BCryptGetProperty(context.algorithm, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&length), sizeof(length), &copied, 0) >= 0, "Cannot query SHA256 state");
    context.object.resize(length);
    require(BCryptCreateHash(context.algorithm, &context.hash, context.object.data(), length, nullptr, 0, 0) >= 0, "Cannot create SHA256 state");
    for (size_t offset = 0; offset < size;) {
        const auto count = static_cast<ULONG>((std::min)(size - offset, size_t{1} << 20));
        require(BCryptHashData(context.hash, const_cast<PUCHAR>(data + offset), count, 0) >= 0, "Cannot hash payload");
        offset += count;
    }
    Digest result{};
    require(BCryptFinishHash(context.hash, result.data(), static_cast<ULONG>(result.size()), 0) >= 0, "Cannot finish SHA256");
    return result;
}

Digest sha256(const Bytes& data) { return sha256(data.data(), data.size()); }

bool digest_matches(const Digest& actual, const uint8_t* expected) {
    return std::equal(actual.begin(), actual.end(), expected);
}

Bytes read_file(const fs::path& path) {
    const auto size = fs::file_size(path);
    require(size <= kMaxBytes, "File exceeds size limit");
    Bytes data(static_cast<size_t>(size));
    std::ifstream stream(path, std::ios::binary);
    require(bool(stream), "Cannot open file");
    stream.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(data.size()));
    require(bool(stream), "Cannot read complete file");
    return data;
}

Bytes decompress(const fs::path& path) {
    std::ifstream file(path, std::ios::binary);
    require(bool(file), "Cannot open compact bundle");
    xz_crc32_init(); xz_crc64_init();
    struct Decoder {
        xz_dec* value = xz_dec_init(XZ_DYNALLOC, 64u << 20);
        ~Decoder() { if (value) xz_dec_end(value); }
    } decoder;
    require(decoder.value != nullptr, "Cannot allocate XZ decoder");
    std::array<uint8_t, 65536> input{};
    std::array<uint8_t, 262144> output{};
    xz_buf b{}; b.in = input.data(); b.out = output.data(); b.out_size = output.size();
    Bytes decoded;
    for (;;) {
        if (b.in_pos == b.in_size) {
            file.read(reinterpret_cast<char*>(input.data()), static_cast<std::streamsize>(input.size()));
            require(!file.bad(), "Cannot read compact bundle");
            b.in_size = static_cast<size_t>(file.gcount()); b.in_pos = 0;
        }
        const auto code = xz_dec_run(decoder.value, &b);
        require(decoded.size() + b.out_pos <= kMaxBytes, "Decoded bundle exceeds size limit");
        decoded.insert(decoded.end(), output.begin(), output.begin() + b.out_pos); b.out_pos = 0;
        if (code == XZ_STREAM_END) {
            require(b.in_pos == b.in_size && file.peek() == std::ifstream::traits_type::eof(), "Trailing or concatenated XZ data");
            return decoded;
        }
        require(code == XZ_OK, "Invalid, truncated or unsupported XZ stream");
    }
}

struct Cursor {
    const uint8_t* p;
    size_t remaining;
    const uint8_t* take(size_t size) {
        require(size <= remaining, "Truncated compact section");
        const auto result = p; p += size; remaining -= size; return result;
    }
    uint64_t number() {
        uint64_t value = 0;
        for (unsigned i = 0; i < 10; ++i) {
            const auto b = *take(1);
            require(i != 9 || b <= 1, "Varint overflow");
            value |= uint64_t(b & 127) << (i * 7);
            if (!(b & 128)) {
                require(i == 0 || b != 0, "Noncanonical varint");
                return value;
            }
        }
        throw std::runtime_error("Varint overflow");
    }
};

struct Field { size_t offset, width; bool delta; };

void unplane(Cursor& source, Bytes& target, uint64_t offset, uint64_t count, size_t stride, std::initializer_list<Field> fields) {
    require(count <= kMaxBytes / stride && offset <= target.size() && count * stride <= target.size() - offset, "Record block exceeds output");
    size_t schema = 0;
    for (const auto field : fields) {
        require(field.offset + field.width <= stride, "Invalid record schema");
        const auto column = source.take(static_cast<size_t>(count * field.width));
        uint64_t previous = 0;
        for (size_t i = 0; i < count; ++i) {
            uint64_t value = 0;
            for (size_t j = 0; j < field.width; ++j) value |= uint64_t(column[j * count + i]) << (j * 8);
            if (field.delta) { value += previous; if (field.width == 4) value &= 0xffffffffu; previous = value; }
            store(target.data() + offset + i * stride + field.offset, value, field.width);
        }
        schema += field.width;
    }
    require(schema == stride, "Incomplete record schema");
}

std::string front_string(Cursor& source, const std::string& previous, uint64_t limit) {
    const auto prefix = source.number(), suffix = source.number();
    require(prefix <= previous.size() && suffix <= limit && prefix + suffix <= limit, "Invalid string prefix or length");
    const auto text = source.take(static_cast<size_t>(suffix));
    require(std::find(text, text + suffix, uint8_t{0}) == text + suffix, "NUL in compact string");
    std::string value = previous.substr(0, static_cast<size_t>(prefix));
    value.append(reinterpret_cast<const char*>(text), static_cast<size_t>(suffix));
    return value;
}

struct StringRef { uint32_t offset, size; };

Bytes restore_lexicon(Cursor source, size_t expected_size) {
    const auto h = source.take(64);
    const auto get = [h](size_t index) { return little(h + index * 4 + 8, 4); };
    require(std::memcmp(h, "KNAPRGB1", 8) == 0 && get(0) == 2 && get(7) == expected_size, "Invalid lexicon header");
    for (size_t i = 8; i < 14; ++i) require(get(i) == 0, "Unsupported lexicon flags");
    const auto keys = get(1), entries = get(2), pool = get(3), key_off = get(4), entry_off = get(5), pool_off = get(6);
    const auto align = [](uint64_t n) { return (n + 63) / 64 * 64; };
    require(keys && entries && key_off == 64 && entry_off == align(64 + keys * 24) && pool_off == align(entry_off + entries * 32)
            && pool_off + pool == expected_size, "Noncanonical lexicon layout");
    const auto counts = source.take(8);
    const auto prototype_count = little(counts, 4), string_count = little(counts + 4, 4);
    require(prototype_count && prototype_count <= entries && string_count && string_count <= prototype_count * 2
            && string_count <= pool && string_count <= source.remaining / 2, "Invalid lexicon dictionary counts");
    Bytes strings;
    std::vector<StringRef> refs; refs.reserve(static_cast<size_t>(string_count));
    std::string previous;
    for (size_t i = 0; i < string_count; ++i) {
        auto value = front_string(source, previous, pool);
        require(i == 0 || value > previous, "String dictionary is not strictly sorted");
        require(strings.size() + value.size() <= pool, "String dictionary exceeds original pool");
        refs.push_back({static_cast<uint32_t>(strings.size()), static_cast<uint32_t>(value.size())});
        strings.insert(strings.end(), value.begin(), value.end()); previous = std::move(value);
    }
    require(prototype_count * 24 + entries * 4 + keys * 3 <= source.remaining, "Truncated lexicon prototype/key data");
    Bytes prototypes(static_cast<size_t>(prototype_count * 24), 0);
    unplane(source, prototypes, 0, prototype_count, 24, {{0,4,true},{4,4,false},{8,4,true},{12,4,false},{16,4,false},{20,4,false}});
    struct Key { StringRef text; uint32_t count; };
    std::vector<Key> searches; searches.reserve(static_cast<size_t>(keys));
    Bytes search_strings;
    previous.clear(); uint64_t total_entries = 0;
    for (size_t i = 0; i < keys; ++i) {
        auto key = front_string(source, previous, pool);
        const auto count = source.number();
        require((i == 0 || key > previous) && count <= entries - total_entries, "Invalid search key order or count");
        require(search_strings.size() + key.size() <= pool, "Search strings exceed original pool");
        searches.push_back({{static_cast<uint32_t>(search_strings.size()), static_cast<uint32_t>(key.size())}, static_cast<uint32_t>(count)});
        search_strings.insert(search_strings.end(), key.begin(), key.end()); previous = std::move(key); total_entries += count;
    }
    require(total_entries == entries, "Search keys do not cover entries");
    const auto aliases = source.take(static_cast<size_t>(entries * 4));
    Bytes output(expected_size, 0); std::copy(h, h + 64, output.begin());
    uint64_t written = 0, current_entry = 0;
    const auto append = [&](const Bytes& dictionary, StringRef ref, uint8_t* record) {
        require(written < pool && ref.size < pool - written, "Restored strings exceed pool");
        store(record, written, 4); store(record + 4, ref.size, 4);
        if (ref.size) std::memcpy(output.data() + pool_off + written, dictionary.data() + ref.offset, ref.size);
        written += ref.size; output[static_cast<size_t>(pool_off + written++)] = 0;
    };
    for (size_t i = 0; i < searches.size(); ++i) {
        const auto key = searches[i]; auto k = output.data() + key_off + i * 24;
        uint64_t prefix = 0;
        for (size_t j = 0; j < 8; ++j) prefix = (prefix << 8) | (j < key.text.size ? search_strings[key.text.offset + j] : 0);
        store(k, prefix, 8); append(search_strings, key.text, k + 8);
        store(k + 16, current_entry, 4); store(k + 20, key.count, 4);
        for (size_t j = 0; j < key.count; ++j, ++current_entry) {
            uint32_t id = 0;
            for (size_t b = 0; b < 4; ++b) id |= uint32_t(aliases[b * entries + current_entry]) << (b * 8);
            require(id < prototype_count, "Entry prototype reference exceeds dictionary");
            const auto p = prototypes.data() + id * 24; const auto word = little(p, 4), reading = little(p + 4, 4);
            require(word < refs.size() && reading < refs.size(), "Prototype string reference exceeds dictionary");
            auto e = output.data() + entry_off + current_entry * 32;
            append(strings, refs[static_cast<size_t>(word)], e); append(strings, refs[static_cast<size_t>(reading)], e + 8);
            std::copy(p + 8, p + 24, e + 16);
        }
    }
    require(written == pool, "Restored string pool size mismatch");
    require(source.remaining == 0, "Trailing lexicon section data");
    return output;
}

Bytes restore_model(Cursor source, size_t expected_size, const Digest& lexicon_digest) {
    const auto h = source.take(128);
    const auto n = [h](size_t index) { return little(h + 24 + index * 4, 4); };
    require(std::memcmp(h, "CSLM0001", 8) == 0 && little(h + 8, 4) == 1 && little(h + 12, 4) == 128
            && little(h + 16, 8) == expected_size && n(0) > 0 && n(4) > 0 && n(4) <= n(0) && n(5) == 0, "Invalid model header");
    require(std::all_of(h + 112, h + 128, [](uint8_t b) { return b == 0; }), "Unsupported model flags");
    require(digest_matches(lexicon_digest, h + 48), "Model dictionary binding mismatch");
    require(128 + n(0) * 32 + n(1) * 12 + n(2) * 12 + n(3) * 4 == expected_size, "Invalid model section sizes");
    Bytes output(expected_size, 0); std::copy(h, h + 128, output.begin());
    const auto value_count = little(source.take(4), 4);
    require(value_count > 0 && value_count <= n(0) * 3 + n(1)
            && value_count * 8 + n(0) * 20 + n(1) * 8 + n(2) * 12 + n(3) * 4 == source.remaining, "Invalid probability dictionary sizes");
    Bytes probabilities(static_cast<size_t>(value_count * 8), 0);
    unplane(source, probabilities, 0, value_count, 8, {{0,8,true}});
    uint64_t previous = 0;
    for (size_t i = 0; i < value_count; ++i) {
        const auto bits = little(probabilities.data() + i * 8, 8);
        double value = 0; std::memcpy(&value, probabilities.data() + i * 8, 8);
        require(std::isfinite(value) && (i == 0 || bits > previous), "Invalid probability bit-pattern dictionary");
        previous = bits;
    }
    const auto probability = [&](uint8_t* dest, uint64_t id) {
        require(id < value_count, "Probability reference exceeds dictionary");
        std::copy_n(probabilities.data() + id * 8, 8, dest);
    };
    Bytes token_refs(static_cast<size_t>(n(0) * 20), 0);
    unplane(source, token_refs, 0, n(0), 20, {{0,4,false},{4,4,false},{8,4,false},{12,4,true},{16,4,false}});
    uint64_t offset = 128;
    for (size_t i = 0; i < n(0); ++i) {
        const auto r = token_refs.data() + i * 20; auto dest = output.data() + offset + i * 32;
        for (size_t j = 0; j < 3; ++j) probability(dest + j * 8, little(r + j * 4, 4));
        std::copy(r + 12, r + 20, dest + 24);
    }
    offset += n(0) * 32;
    Bytes arc_refs(static_cast<size_t>(n(1) * 8), 0);
    unplane(source, arc_refs, 0, n(1), 8, {{0,4,true},{4,4,false}});
    for (size_t i = 0; i < n(1); ++i) {
        const auto r = arc_refs.data() + i * 8; auto dest = output.data() + offset + i * 12;
        std::copy(r, r + 4, dest); probability(dest + 4, little(r + 4, 4));
    }
    offset += n(1) * 12;
    unplane(source, output, offset, n(2), 12, {{0,4,true},{4,4,true},{8,4,false}}); offset += n(2) * 12;
    unplane(source, output, offset, n(3), 4, {{0,4,false}});
    require(source.remaining == 0, "Trailing model section data");
    require(digest_matches(sha256(output.data() + 128, output.size() - 128), h + 80), "Model body checksum mismatch");
    return output;
}

struct Pair { Bytes lexicon, model; };

Pair decode(const Bytes& raw) {
    require(raw.size() >= kHeaderSize, "Compact header is truncated");
    const auto h = raw.data();
    require(std::memcmp(h, "CSTPACK1", 8) == 0 && little(h + 8, 4) == 1 && little(h + 12, 4) == 3, "Unsupported compact header");
    const auto ls = little(h + 16, 8), ms = little(h + 24, 8), le = little(h + 32, 8), me = little(h + 40, 8);
    require(ls >= 64 && ms >= 128 && ls <= kMaxBytes && ms <= kMaxBytes - ls
            && le >= 64 && le <= raw.size() - kHeaderSize && me == raw.size() - kHeaderSize - le, "Invalid compact section sizes");
    require(digest_matches(sha256(h + kHeaderSize, raw.size() - kHeaderSize), h + 112), "Compact body checksum mismatch");
    Pair result;
    result.lexicon = restore_lexicon({h + kHeaderSize, static_cast<size_t>(le)}, static_cast<size_t>(ls));
    const auto lex_hash = sha256(result.lexicon);
    require(digest_matches(lex_hash, h + 48), "Restored lexicon checksum mismatch");
    result.model = restore_model({h + kHeaderSize + le, static_cast<size_t>(me)}, static_cast<size_t>(ms), lex_hash);
    require(digest_matches(sha256(result.model), h + 80), "Restored model checksum mismatch");
    return result;
}

void write_file(const fs::path& path, const Bytes& data) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    require(bool(out), "Cannot create staged table");
    out.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
    out.close(); require(bool(out), "Cannot write complete staged table");
    require(sha256(read_file(path)) == sha256(data), "Staged table checksum mismatch");
}

void move_file(const fs::path& source, const fs::path& target) {
    require(MoveFileExW(source.c_str(), target.c_str(), MOVEFILE_WRITE_THROUGH) != 0, "Cannot publish resource file");
}

void publish(const fs::path& directory, const Pair& pair, bool replace) {
    fs::create_directories(directory);
    require(!fs::is_symlink(directory) && fs::is_directory(directory), "Output must be a regular directory");
    const std::array<fs::path,2> paths = {directory / L"mainlexicon.coral", directory / L"languagemodel.coral"};
    for (const auto& path : paths) {
        require(!fs::is_symlink(path) && (!fs::exists(path) || fs::is_regular_file(path)), "Invalid output target");
        require(replace || !fs::exists(path), "Output exists; use --replace explicitly");
    }
    const auto stage = directory / (L".coral-tables-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64()));
    require(fs::create_directory(stage), "Cannot create unique staging directory");
    std::array<bool,2> backed{}, installed{};
    bool preserve = false;
    try {
        write_file(stage / L"new-lexicon", pair.lexicon); write_file(stage / L"new-model", pair.model);
        const std::array<fs::path,2> fresh = {stage / L"new-lexicon", stage / L"new-model"};
        const std::array<fs::path,2> backups = {stage / L"old-lexicon", stage / L"old-model"};
        try {
            for (size_t i = 0; i < 2; ++i) if (fs::exists(paths[i])) { move_file(paths[i], backups[i]); backed[i] = true; }
            for (size_t i = 0; i < 2; ++i) { move_file(fresh[i], paths[i]); installed[i] = true; }
        } catch (...) {
            try {
                for (size_t i = 0; i < 2; ++i) if (installed[i]) move_file(paths[i], fresh[i]);
                for (size_t i = 0; i < 2; ++i) if (backed[i]) move_file(backups[i], paths[i]);
            } catch (...) { preserve = true; std::wcerr << L"Rollback incomplete; backups retained in " << stage << L'\n'; }
            throw;
        }
    } catch (...) {
        if (!preserve) { std::error_code ec; fs::remove_all(stage, ec); }
        throw;
    }
    std::error_code ec; fs::remove_all(stage, ec);
}
} // 匿名命名空间结束。

int wmain(int argc, wchar_t** argv) {
    if (argc != 3 && !(argc == 4 && std::wstring(argv[3]) == L"--replace")) {
        std::cerr << "usage: coral_table_restore <surface_tables.cstpack.xz> <output-directory> [--replace]\n"; return 2;
    }
    try {
        const auto raw = decompress(fs::path(argv[1]));
        const auto pair = decode(raw);
        publish(fs::absolute(argv[2]).lexically_normal(), pair, argc == 4);
        std::cout << "Verified and restored paired tables: " << pair.lexicon.size() << " + " << pair.model.size() << " bytes\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << "ERROR: " << error.what() << '\n'; return 1; }
}

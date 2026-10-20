#include "interaction_history_storage.h"

#include "interaction_history_codec.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <fstream>
#include <functional>
#include <unordered_map>

#if defined(_WIN32)
#include <Windows.h>
#endif

namespace coral::history {
namespace {

std::atomic<std::uint64_t> g_tempCounter{0};

struct IdentityHash {
    std::size_t operator()(const EntryIdentity& identity) const noexcept {
        return static_cast<std::size_t>(EntryId(identity));
    }
};

class ProcessLock final {
public:
    explicit ProcessLock(const std::filesystem::path& file) {
#if defined(_WIN32)
        const std::wstring name = L"Local\\CoralPinyin.UserHistory." + std::to_wstring(std::hash<std::wstring>{}(file.wstring()));
        handle_ = CreateMutexW(nullptr, FALSE, name.c_str());
        acquired_ = handle_ && WaitForSingleObject(handle_, 30000) == WAIT_OBJECT_0;
#else
        static_cast<void>(file);
        acquired_ = true;
#endif
    }
    ~ProcessLock() {
#if defined(_WIN32)
        if (acquired_) ReleaseMutex(handle_);
        if (handle_) CloseHandle(handle_);
#endif
    }
    bool acquired() const { return acquired_; }

private:
    bool acquired_ = false;
#if defined(_WIN32)
    HANDLE handle_ = nullptr;
#endif
};

bool ReadWholeFile(const std::filesystem::path& path, std::string& bytes) {
    std::ifstream input(path, std::ios::binary);
    if (!input) return false;
    input.seekg(0, std::ios::end);
    const auto end = input.tellg();
    if (end < 0 || static_cast<std::uint64_t>(end) > 256ull * 1024ull * 1024ull + 64ull) return false;
    bytes.resize(static_cast<std::size_t>(end));
    input.seekg(0, std::ios::beg);
    if (!bytes.empty()) input.read(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    return input.good() || input.eof();
}

std::filesystem::path LegacyPath(const std::filesystem::path& base, const char* suffix) {
    std::filesystem::path result = base;
    result.replace_extension(".coral");
    if (*suffix) {
        result.replace_filename(result.stem().string() + suffix + result.extension().string());
    }
    return result;
}

bool IsPolicy(const Entry& entry) {
    return entry.partition == Partition::SimplifiedPolicy || entry.partition == Partition::TraditionalPolicy;
}

double RetentionScore(const Entry& entry, std::uint64_t newest) {
    const double frequency = std::log2(static_cast<double>(entry.commit_count) + 1.0);
    const double age = newest > entry.last_used_sequence
        ? static_cast<double>(newest - entry.last_used_sequence) : 0.0;
    return frequency + 1.0 / (1.0 + age / 2048.0);
}

}  // namespace

Storage::Storage(std::filesystem::path file, Options options)
    : file_(std::move(file)), options_(options) {
    legacy_base_ = file_;
    if (file_.extension() != ".coral2") {
        legacy_base_ = file_;
        file_ = V2PathFromLegacyBase(file_);
    } else {
        legacy_base_ = file_.parent_path() / "userlexicon.coral";
    }
}

std::filesystem::path Storage::V2PathFromLegacyBase(const std::filesystem::path& legacyBase) {
    return legacyBase.parent_path() / "user_history.coral2";
}

bool Storage::LoadV2(const std::filesystem::path& path, std::shared_ptr<const Snapshot>& snapshot,
                     std::string& error) const {
    std::string bytes;
    if (!ReadWholeFile(path, bytes)) {
        error = "cannot read user history file: " + path.string();
        return false;
    }
    DecodedHistory decoded;
    if (!Codec::Decode(bytes, decoded, error)) return false;
    // 策略记录由过渡期的 v2.0 实现存入历史文件。
    // CandidatePolicyStorage 会在本管理器启动前导入它们；
    // 历史统计、评分与检查点不得再计入这些记录。
    decoded.entries.erase(std::remove_if(decoded.entries.begin(), decoded.entries.end(), IsPolicy),
                          decoded.entries.end());
    EnforceCapacity(decoded.entries);
    snapshot = Snapshot::Build(decoded.generation, std::move(decoded.entries));
    return true;
}

bool Storage::LoadLegacy(std::shared_ptr<const Snapshot>& snapshot, std::string& error) const {
    struct Source { const char* suffix; Partition partition; };
    constexpr Source sources[] = {
        {"", Partition::Simplified},
        {"_traditional", Partition::Traditional},
    };
    std::vector<Entry> entries;
    bool found = false;
    for (const Source& source : sources) {
        const auto path = LegacyPath(legacy_base_, source.suffix);
        if (!std::filesystem::exists(path)) continue;
        found = true;
        std::string localError;
        if (!LegacyCodec::ReadFile(path.string(), source.partition, entries, localError)) {
            error = path.string() + ": " + localError;
            return false;
        }
    }
    if (!found) {
        snapshot = Snapshot::Build(0, {});
        return true;
    }
    EnforceCapacity(entries);
    snapshot = Snapshot::Build(0, std::move(entries));
    return true;
}

bool Storage::Load(std::shared_ptr<const Snapshot>& snapshot, bool& migrated, std::string& error) const {
    migrated = false;
    if (std::filesystem::exists(file_)) {
        if (LoadV2(file_, snapshot, error)) return true;
        const auto backup = std::filesystem::path(file_.wstring() + L".bak");
        std::string backupError;
        if (std::filesystem::exists(backup) && LoadV2(backup, snapshot, backupError)) {
            migrated = true;  // 调度主文件的修复
            return true;
        }
        return false;
    }
    if (!LoadLegacy(snapshot, error)) return false;
    migrated = true;
    return true;
}

std::vector<Entry> Storage::ApplyMutations(const std::vector<Entry>& base,
                                           const std::vector<Mutation>& mutations) const {
    std::unordered_map<EntryIdentity, Entry, IdentityHash> table;
    table.reserve(base.size() + mutations.size());
    for (const Entry& entry : base) table[IdentityOf(entry)] = entry;
    for (const Mutation& mutation : mutations) {
        if (mutation.kind == MutationKind::ClearAll) {
            table.clear();
            continue;
        }
        if (mutation.kind == MutationKind::Remove) {
            table.erase(mutation.identity);
            continue;
        }
        if (mutation.kind == MutationKind::Upsert && mutation.entry) {
            Entry entry = *mutation.entry;
            entry.entry_id = EntryId(IdentityOf(entry));
            table[IdentityOf(entry)] = std::move(entry);
            continue;
        }
        if (mutation.kind == MutationKind::Learn) {
            auto it = table.find(mutation.identity);
            if (it == table.end()) {
                Entry entry;
                if (mutation.entry) entry = *mutation.entry;
                entry.partition = mutation.identity.partition;
                entry.lookup_key = mutation.identity.lookup_key;
                entry.phrase = mutation.identity.phrase;
                entry.commit_count = mutation.commit_delta;
                entry.last_used_sequence = mutation.last_used_sequence;
                entry.score_state = mutation.score_state;
                entry.entry_id = EntryId(mutation.identity);
                table.emplace(mutation.identity, std::move(entry));
            } else {
                Entry& entry = it->second;
                entry.commit_count += mutation.commit_delta;
                entry.last_used_sequence = std::max(entry.last_used_sequence, mutation.last_used_sequence);
                entry.score_state = mutation.score_state;
                if (mutation.entry && !mutation.entry->full_pinyin.empty()) {
                    entry.full_pinyin = mutation.entry->full_pinyin;
                    entry.flags |= mutation.entry->flags;
                }
            }
        }
    }
    std::vector<Entry> result;
    result.reserve(table.size());
    for (auto& item : table) result.push_back(std::move(item.second));
    EnforceCapacity(result);
    return result;
}

void Storage::EnforceCapacity(std::vector<Entry>& entries) const {
    entries.erase(std::remove_if(entries.begin(), entries.end(), IsPolicy), entries.end());
    if (entries.size() <= options_.max_entries) return;
    std::uint64_t newest = 0;
    for (const Entry& entry : entries) newest = std::max(newest, entry.last_used_sequence);
    std::stable_sort(entries.begin(), entries.end(), [newest](const Entry& lhs, const Entry& rhs) {
        return RetentionScore(lhs, newest) > RetentionScore(rhs, newest);
    });
    entries.resize(options_.max_entries);
}

bool Storage::AtomicWrite(const std::string& bytes, std::string& error) const {
    std::error_code ec;
    std::filesystem::create_directories(file_.parent_path(), ec);
    if (ec) {
        error = "cannot create user history directory: " + ec.message();
        return false;
    }
    const std::uint64_t serial = ++g_tempCounter;
#if defined(_WIN32)
    const auto temp = std::filesystem::path(file_.wstring() + L".tmp." + std::to_wstring(GetCurrentProcessId()) + L"." + std::to_wstring(serial));
    HANDLE handle = CreateFileW(temp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                                FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH, nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        error = "cannot create user history temporary file";
        return false;
    }
    bool ok = true;
    std::size_t offset = 0;
    while (offset < bytes.size()) {
        const DWORD chunk = static_cast<DWORD>(std::min<std::size_t>(bytes.size() - offset, 1u << 20));
        DWORD written = 0;
        if (!WriteFile(handle, bytes.data() + offset, chunk, &written, nullptr) || written != chunk) {
            ok = false;
            break;
        }
        offset += written;
    }
    if (ok) ok = FlushFileBuffers(handle) != FALSE;
    CloseHandle(handle);
    if (!ok) {
        DeleteFileW(temp.c_str());
        error = "cannot flush user history temporary file";
        return false;
    }
    const auto backup = std::filesystem::path(file_.wstring() + L".bak");
    if (std::filesystem::exists(file_)) {
        ok = ReplaceFileW(file_.c_str(), temp.c_str(), backup.c_str(), REPLACEFILE_WRITE_THROUGH, nullptr, nullptr) != FALSE;
    } else {
        ok = MoveFileExW(temp.c_str(), file_.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE;
    }
    if (!ok) {
        DeleteFileW(temp.c_str());
        error = "cannot atomically replace user history file";
        return false;
    }
#else
    const auto temp = std::filesystem::path(file_.string() + ".tmp." + std::to_string(serial));
    {
        std::ofstream output(temp, std::ios::binary | std::ios::trunc);
        output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        output.flush();
        if (!output) {
            error = "cannot write user history temporary file";
            std::filesystem::remove(temp, ec);
            return false;
        }
    }
    std::filesystem::rename(temp, file_, ec);
    if (ec) {
        error = "cannot replace user history file: " + ec.message();
        return false;
    }
#endif
    return true;
}

bool Storage::Checkpoint(const std::vector<Mutation>& mutations,
                         std::shared_ptr<const Snapshot>& snapshot,
                         std::string& error) const {
    ProcessLock lock(file_);
    if (!lock.acquired()) {
        error = "timed out acquiring user history checkpoint lock";
        return false;
    }
    const bool hadV2 = std::filesystem::exists(file_);
    std::shared_ptr<const Snapshot> latest;
    bool migrated = false;
    if (!Load(latest, migrated, error)) return false;
    auto merged = ApplyMutations(latest->entries(), mutations);
    const std::uint64_t generation = latest->generation() + 1;
    std::string bytes;
    if (!Codec::Encode(generation, merged, bytes, error) || !AtomicWrite(bytes, error)) return false;
    if (!hadV2) {
        // 手动候选策略的旧文件由候选策略存储负责持有与迁移。
        // 不得从历史代码归档它们。
        constexpr const char* suffixes[] = {"", "_traditional"};
        for (const char* suffix : suffixes) {
            const auto legacy = LegacyPath(legacy_base_, suffix);
            if (!std::filesystem::exists(legacy)) continue;
            const auto archive = std::filesystem::path(legacy.wstring() + L".legacy.bak");
            std::error_code archiveError;
            if (!std::filesystem::exists(archive)) std::filesystem::rename(legacy, archive, archiveError);
        }
    }
    snapshot = Snapshot::Build(generation, std::move(merged));
    return true;
}

}  // 命名空间 coral::history 结束

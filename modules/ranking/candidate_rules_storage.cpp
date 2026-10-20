#include "candidate_rules_storage.h"

#include "candidate_rules_codec.h"
#include "../learning/interaction_history_codec.h"

#include <algorithm>
#include <atomic>
#include <fstream>
#include <unordered_map>

#if defined(_WIN32)
#include <Windows.h>
#endif

namespace coral::candidate_policy {
namespace {

std::atomic<std::uint64_t> g_tempCounter{0};

struct IdentityHash {
    std::size_t operator()(const RuleIdentity& identity) const {
        return static_cast<std::size_t>(RuleId(identity));
    }
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
    auto result = base;
    result.replace_filename(result.stem().string() + suffix + result.extension().string());
    return result;
}

Rule FromHistoryEntry(const history::Entry& entry) {
    Rule rule;
    rule.variant = entry.partition == history::Partition::TraditionalPolicy
        ? LanguageVariant::Traditional : LanguageVariant::Simplified;
    rule.kind = ((entry.flags & history::kDeleted) || entry.candidate_position == 0)
        ? RuleKind::Disable : RuleKind::Promote;
    rule.pinned = rule.kind == RuleKind::Promote &&
                  (entry.flags & history::kFixedCandidate) != 0;
    rule.updated_sequence = entry.last_used_sequence;
    rule.lookup_key = entry.lookup_key;
    rule.full_pinyin = entry.full_pinyin;
    rule.phrase = entry.phrase;
    rule.rule_id = RuleId(IdentityOf(rule));
    return rule;
}

#if defined(_WIN32)
class ProcessLock final {
public:
    explicit ProcessLock(const std::filesystem::path& file) {
        const std::wstring name = L"Local\\CoralPinyin.CandidatePolicy." +
            std::to_wstring(std::hash<std::wstring>{}(file.wstring()));
        handle_ = CreateMutexW(nullptr, FALSE, name.c_str());
        if (handle_) {
            const DWORD result = WaitForSingleObject(handle_, 5000);
            acquired_ = result == WAIT_OBJECT_0 || result == WAIT_ABANDONED;
        }
    }
    ~ProcessLock() {
        if (acquired_) ReleaseMutex(handle_);
        if (handle_) CloseHandle(handle_);
    }
    bool acquired() const { return acquired_; }
private:
    HANDLE handle_ = nullptr;
    bool acquired_ = false;
};
#else
class ProcessLock final {
public:
    explicit ProcessLock(const std::filesystem::path&) {}
    bool acquired() const { return true; }
};
#endif

}  // namespace

Storage::Storage(std::filesystem::path legacyBase, Options options)
    : legacy_base_(std::move(legacyBase)), options_(options) {
    if (legacy_base_.extension() == ".coral2") {
        legacy_base_ = legacy_base_.parent_path() / "userlexicon.coral";
    }
    file_ = PolicyPathFromLegacyBase(legacy_base_);
}

std::filesystem::path Storage::PolicyPathFromLegacyBase(
    const std::filesystem::path& legacyBase) {
    return legacyBase.parent_path() / "user_candidate_policy.coral2";
}

bool Storage::LoadV1(const std::filesystem::path& path,
                     std::shared_ptr<const Snapshot>& snapshot,
                     std::string& error) const {
    std::string bytes;
    if (!ReadWholeFile(path, bytes)) {
        error = "cannot read candidate policy file: " + path.string();
        return false;
    }
    DecodedPolicy decoded;
    if (!Codec::Decode(bytes, decoded, error)) return false;
    if (decoded.rules.size() > options_.max_rules) {
        error = "candidate policy exceeds configured capacity";
        return false;
    }
    snapshot = Snapshot::Build(decoded.generation, std::move(decoded.rules));
    return true;
}

bool Storage::ImportExistingPolicies(std::shared_ptr<const Snapshot>& snapshot,
                                     bool& found, std::string& error) const {
    found = false;
    std::vector<history::Entry> oldEntries;
    const auto historyPath = legacy_base_.parent_path() / "user_history.coral2";
    if (std::filesystem::exists(historyPath)) {
        std::string bytes;
        history::DecodedHistory decoded;
        if (!ReadWholeFile(historyPath, bytes) || !history::Codec::Decode(bytes, decoded, error)) {
            const auto backup = std::filesystem::path(historyPath.wstring() + L".bak");
            bytes.clear();
            if (!std::filesystem::exists(backup) || !ReadWholeFile(backup, bytes) ||
                !history::Codec::Decode(bytes, decoded, error)) {
                return false;
            }
        }
        for (const history::Entry& entry : decoded.entries) {
            if (entry.partition == history::Partition::SimplifiedPolicy ||
                entry.partition == history::Partition::TraditionalPolicy) {
                oldEntries.push_back(entry);
            }
        }
        found = !oldEntries.empty();
    }

    if (!found) {
        struct Source { const char* suffix; history::Partition partition; };
        constexpr Source sources[] = {
            {"_simplified_fixed", history::Partition::SimplifiedPolicy},
            {"_traditional_fixed", history::Partition::TraditionalPolicy},
        };
        for (const Source& source : sources) {
            const auto path = LegacyPath(legacy_base_, source.suffix);
            if (!std::filesystem::exists(path)) continue;
            found = true;
            std::string localError;
            if (!history::LegacyCodec::ReadFile(path.string(), source.partition, oldEntries, localError)) {
                error = path.string() + ": " + localError;
                return false;
            }
        }
    }

    std::vector<Rule> rules;
    rules.reserve(oldEntries.size());
    for (const history::Entry& entry : oldEntries) rules.push_back(FromHistoryEntry(entry));
    if (rules.size() > options_.max_rules) {
        error = "migrated candidate policy exceeds configured capacity";
        return false;
    }
    snapshot = Snapshot::Build(0, std::move(rules));
    return true;
}

bool Storage::Load(std::shared_ptr<const Snapshot>& snapshot, bool& migrated,
                   std::string& error) const {
    migrated = false;
    if (std::filesystem::exists(file_)) {
        if (LoadV1(file_, snapshot, error)) return true;
        const auto backup = std::filesystem::path(file_.wstring() + L".bak");
        std::string backupError;
        if (std::filesystem::exists(backup) && LoadV1(backup, snapshot, backupError)) {
            migrated = true;
            return true;
        }
        return false;
    }
    bool found = false;
    if (!ImportExistingPolicies(snapshot, found, error)) return false;
    migrated = found;
    if (!snapshot) snapshot = Snapshot::Build(0, {});
    return true;
}

std::vector<Rule> Storage::ApplyMutations(const std::vector<Rule>& base,
                                          const std::vector<Mutation>& mutations) {
    std::unordered_map<RuleIdentity, Rule, IdentityHash> table;
    table.reserve(base.size() + mutations.size());
    for (const Rule& rule : base) table[IdentityOf(rule)] = rule;
    for (const Mutation& mutation : mutations) {
        if (mutation.kind == MutationKind::ClearAll) {
            table.clear();
        } else if (mutation.kind == MutationKind::Remove) {
            table.erase(mutation.identity);
        } else if (mutation.kind == MutationKind::Set && mutation.rule) {
            Rule rule = *mutation.rule;
            rule.rule_id = RuleId(IdentityOf(rule));
            if (rule.kind == RuleKind::Promote && rule.pinned) {
                for (auto it = table.begin(); it != table.end();) {
                    const Rule& existing = it->second;
                    if (existing.variant == rule.variant && existing.lookup_key == rule.lookup_key &&
                        existing.pinned && existing.phrase != rule.phrase) {
                        it = table.erase(it);
                    } else {
                        ++it;
                    }
                }
            }
            table[IdentityOf(rule)] = std::move(rule);
        }
    }
    std::vector<Rule> result;
    result.reserve(table.size());
    for (auto& item : table) result.push_back(std::move(item.second));
    return result;
}

bool Storage::AtomicWrite(const std::string& bytes, std::string& error) const {
    std::error_code ec;
    std::filesystem::create_directories(file_.parent_path(), ec);
    if (ec) {
        error = "cannot create candidate policy directory: " + ec.message();
        return false;
    }
    const std::uint64_t serial = ++g_tempCounter;
#if defined(_WIN32)
    const auto temp = std::filesystem::path(file_.wstring() + L".tmp." +
        std::to_wstring(GetCurrentProcessId()) + L"." + std::to_wstring(serial));
    HANDLE handle = CreateFileW(temp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                                FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH, nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        error = "cannot create candidate policy temporary file";
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
        error = "cannot flush candidate policy temporary file";
        return false;
    }
    const auto backup = std::filesystem::path(file_.wstring() + L".bak");
    if (std::filesystem::exists(file_)) {
        ok = ReplaceFileW(file_.c_str(), temp.c_str(), backup.c_str(),
                          REPLACEFILE_WRITE_THROUGH, nullptr, nullptr) != FALSE;
    } else {
        ok = MoveFileExW(temp.c_str(), file_.c_str(),
                         MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE;
    }
    if (!ok) {
        DeleteFileW(temp.c_str());
        error = "cannot atomically replace candidate policy file";
        return false;
    }
#else
    const auto temp = std::filesystem::path(file_.string() + ".tmp." + std::to_string(serial));
    {
        std::ofstream output(temp, std::ios::binary | std::ios::trunc);
        output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        output.flush();
        if (!output) {
            error = "cannot write candidate policy temporary file";
            std::filesystem::remove(temp, ec);
            return false;
        }
    }
    std::filesystem::rename(temp, file_, ec);
    if (ec) {
        error = "cannot replace candidate policy file: " + ec.message();
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
        error = "timed out acquiring candidate policy checkpoint lock";
        return false;
    }
    std::shared_ptr<const Snapshot> latest;
    bool migrated = false;
    if (!Load(latest, migrated, error)) return false;
    auto merged = ApplyMutations(latest->rules(), mutations);
    if (merged.size() > options_.max_rules) {
        error = "candidate policy capacity exceeded";
        return false;
    }
    const std::uint64_t generation = latest->generation() + 1;
    std::string bytes;
    if (!Codec::Encode(generation, merged, bytes, error) || !AtomicWrite(bytes, error)) return false;
    snapshot = Snapshot::Build(generation, std::move(merged));
    return true;
}

}  // 命名空间 coral::candidate_policy 结束

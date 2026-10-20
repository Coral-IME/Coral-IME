#include "interaction_history_codec.h"
#include "interaction_history_snapshot.h"
#include "interaction_history_storage.h"
#include "candidate_rules_codec.h"
#include "candidate_rules_snapshot.h"
#include "candidate_rules_storage.h"
#include "candidate_rules_manager.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>
#include <Windows.h>

namespace {
std::uint32_t Crc32(const char* data, std::size_t size) {
    std::uint32_t crc = 0xffffffffu;
    for (std::size_t i = 0; i < size; ++i) {
        crc ^= static_cast<unsigned char>(data[i]);
        for (int bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}
void Write32(std::string& bytes, std::size_t pos, std::uint32_t value) {
    for (std::size_t i = 0; i < 4; ++i) bytes[pos + i] = static_cast<char>(value >> (i * 8));
}
void SetLegacyPosition(std::string& bytes, std::int32_t position) {
    // V1 夹具：64 字节文件头，位置在首记录偏移 8 处。
    Write32(bytes, 72, static_cast<std::uint32_t>(position));
    Write32(bytes, 48, Crc32(bytes.data() + 64, bytes.size() - 64));
    Write32(bytes, 52, 0);
    Write32(bytes, 52, Crc32(bytes.data(), 64));
}
} // namespace

int main() {
    if (coral::CanDisableCandidate("") || coral::CanDisableCandidate(u8"中") ||
        coral::CanDisableCandidate(u8"國") || coral::CanDisableCandidate(u8"\U00020000") ||
        coral::CanDisableCandidate(u8"中\uFE00") ||
        coral::CanDisableCandidate(u8"\U00020000\U000E0100") ||
        coral::CanDisableCandidate("\xe4\xb8") || coral::CanDisableCandidate("\xc0\xaf\xc0\xaf") ||
        !coral::CanDisableCandidate(u8"中国") || !coral::CanDisableCandidate(u8"中國") ||
        !coral::CanDisableCandidate(u8"\U00020000国") || !coral::CanDisableCandidate("hello")) return 38;
    using namespace coral::history;
    Entry learned;
    learned.partition = Partition::Simplified;
    learned.flags = kShortPinyin;
    learned.lookup_key = "bj";
    learned.full_pinyin = "bei'jing";
    learned.phrase = "北京";
    learned.commit_count = 42;
    learned.last_used_sequence = 99;
    learned.score_state = 12.5f;

    Entry legacyPolicy;
    legacyPolicy.partition = Partition::SimplifiedPolicy;
    legacyPolicy.flags = kFixedCandidate;
    legacyPolicy.lookup_key = "beijing";
    legacyPolicy.full_pinyin = "bei'jing";
    legacyPolicy.phrase = "北京";
    legacyPolicy.commit_count = 1;
    legacyPolicy.last_used_sequence = 100;
    legacyPolicy.score_state = 1.0f;
    legacyPolicy.candidate_position = 1;

    std::string encoded;
    std::string error;
    if (!Codec::Encode(7, {learned, legacyPolicy}, encoded, error)) {
        std::cerr << error << '\n';
        return 1;
    }
    DecodedHistory decoded;
    if (!Codec::Decode(encoded, decoded, error) || decoded.generation != 7 || decoded.entries.size() != 2) {
        std::cerr << "round-trip failed: " << error << '\n';
        return 2;
    }
    const auto snapshot = Snapshot::Build(decoded.generation, decoded.entries);
    const auto matches = snapshot->Lookup(Partition::Simplified, "bj");
    if (matches.size() != 1 || matches[0].phrase != "北京" || matches[0].commit_count != 42) {
        std::cerr << "snapshot lookup failed\n";
        return 3;
    }
    encoded.back() ^= 0x01;
    if (Codec::Decode(encoded, decoded, error)) {
        std::cerr << "checksum corruption was accepted\n";
        return 4;
    }

    const auto testDir = std::filesystem::temp_directory_path() /
        ("coral-user-history-test-" + std::to_string(static_cast<unsigned long long>(::GetCurrentProcessId())));
    std::error_code ec;
    std::filesystem::remove_all(testDir, ec);
    std::filesystem::create_directories(testDir, ec);
    if (ec) return 5;
    Storage storage(testDir / "user_history.coral2");
    auto learn = [](std::string key, std::string phrase, std::uint64_t sequence) {
        Mutation mutation;
        mutation.kind = MutationKind::Learn;
        mutation.identity = {Partition::Simplified, std::move(key), std::move(phrase)};
        mutation.commit_delta = 1;
        mutation.last_used_sequence = sequence;
        mutation.score_state = 5.0f;
        Entry seed;
        seed.partition = mutation.identity.partition;
        seed.lookup_key = mutation.identity.lookup_key;
        seed.full_pinyin = mutation.identity.lookup_key;
        seed.phrase = mutation.identity.phrase;
        mutation.entry = std::move(seed);
        return mutation;
    };
    std::shared_ptr<const Snapshot> persisted;
    if (!storage.Checkpoint({learn("bei'jing", "北京", 1)}, persisted, error) || persisted->generation() != 1) {
        std::cerr << "first checkpoint failed: " << error << '\n';
        return 6;
    }
    if (!storage.Checkpoint({learn("shang'hai", "上海", 2)}, persisted, error) ||
        persisted->generation() != 2 || persisted->entries().size() != 2) {
        std::cerr << "incremental merge failed: " << error << '\n';
        return 7;
    }
    if (!storage.Checkpoint({learn("guang'zhou", "广州", 3)}, persisted, error) ||
        persisted->generation() != 3 || persisted->entries().size() != 3) {
        std::cerr << "checkpoint with existing backup failed: " << error << '\n';
        return 8;
    }
    std::shared_ptr<const Snapshot> reloaded;
    bool migrated = false;
    if (!storage.Load(reloaded, migrated, error) || reloaded->entries().size() != 3) {
        std::cerr << "checkpoint reload failed: " << error << '\n';
        return 9;
    }

    {
        namespace policy = coral::candidate_policy;
        policy::Rule pinned;
        pinned.variant = policy::LanguageVariant::Simplified;
        pinned.kind = policy::RuleKind::Promote;
        pinned.pinned = true;
        pinned.lookup_key = "beijing";
        pinned.full_pinyin = "bei'jing";
        pinned.phrase = "北京";

        std::string policyBytes;
        if (!policy::Codec::Encode(4, {pinned}, policyBytes, error)) {
            std::cerr << "policy encode failed: " << error << '\n';
            return 10;
        }
        policy::DecodedPolicy decodedPolicy;
        if (!policy::Codec::Decode(policyBytes, decodedPolicy, error) ||
            decodedPolicy.generation != 4 || decodedPolicy.rules.size() != 1 ||
            !decodedPolicy.rules[0].pinned) {
            std::cerr << "policy round-trip failed: " << error << '\n';
            return 11;
        }

        // 读取带过时位置的已安装 V1 规则；只重写第一条。
        auto oldPolicyBytes = policyBytes;
        SetLegacyPosition(oldPolicyBytes, 7);
        if (!policy::Codec::Decode(oldPolicyBytes, decodedPolicy, error) ||
            !decodedPolicy.rules[0].pinned) return 20;
        std::string normalizedBytes;
        if (!policy::Codec::Encode(4, decodedPolicy.rules, normalizedBytes, error) ||
            normalizedBytes != policyBytes) return 21;
        SetLegacyPosition(oldPolicyBytes, -1);
        if (policy::Codec::Decode(oldPolicyBytes, decodedPolicy, error)) return 22;

        policy::Storage policyStorage(testDir / "userlexicon.coral");
        policy::Mutation set;
        set.kind = policy::MutationKind::Set;
        set.identity = policy::IdentityOf(pinned);
        set.rule = pinned;
        std::shared_ptr<const policy::Snapshot> policySnapshot;
        if (!policyStorage.Checkpoint({set}, policySnapshot, error) ||
            policySnapshot->Lookup(policy::LanguageVariant::Simplified, "beijing").size() != 1) {
            std::cerr << "policy checkpoint failed: " << error << '\n';
            return 12;
        }

        policy::Rule disabled = pinned;
        disabled.kind = policy::RuleKind::Disable;
        disabled.pinned = false;
        policy::Mutation replace;
        replace.kind = policy::MutationKind::Set;
        replace.identity = policy::IdentityOf(disabled);
        replace.rule = disabled;
        if (!policyStorage.Checkpoint({replace}, policySnapshot, error)) {
            std::cerr << "policy replace failed: " << error << '\n';
            return 13;
        }
        const auto policyMatches = policySnapshot->Lookup(policy::LanguageVariant::Simplified, "beijing");
        if (policyMatches.size() != 1 || policyMatches[0].kind != policy::RuleKind::Disable) {
            std::cerr << "policy identity replacement failed\n";
            return 14;
        }

        policy::Rule first = pinned;
        first.phrase = "背景";
        policy::Rule second = pinned;
        second.phrase = "北京";
        policy::Mutation pinSecond;
        pinSecond.kind = policy::MutationKind::Set;
        pinSecond.identity = policy::IdentityOf(second);
        pinSecond.rule = second;
        const auto uniquePinned = policy::Storage::ApplyMutations({first}, {pinSecond});
        if (uniquePinned.size() != 1 || uniquePinned[0].phrase != second.phrase) {
            std::cerr << "multiple pinned-first policies were retained for one key\n";
            return 15;
        }

        const auto migrationDir = testDir / "migration";
        std::filesystem::create_directories(migrationDir, ec);
        std::string mixedBytes;
        if (!coral::history::Codec::Encode(9, {learned, legacyPolicy}, mixedBytes, error)) return 16;
        {
            std::ofstream mixedFile(migrationDir / "user_history.coral2", std::ios::binary);
            mixedFile.write(mixedBytes.data(), static_cast<std::streamsize>(mixedBytes.size()));
            if (!mixedFile) return 17;
        }
        policy::Storage migratingPolicy(migrationDir / "userlexicon.coral");
        std::shared_ptr<const policy::Snapshot> migratedPolicy;
        bool policyMigrated = false;
        if (!migratingPolicy.Load(migratedPolicy, policyMigrated, error) || !policyMigrated ||
            migratedPolicy->rules().size() != 1) {
            std::cerr << "embedded policy migration failed: " << error << '\n';
            return 18;
        }
        coral::history::Storage migratingHistory(migrationDir / "user_history.coral2");
        std::shared_ptr<const coral::history::Snapshot> migratedHistory;
        bool historyMigrated = false;
        if (!migratingHistory.Load(migratedHistory, historyMigrated, error) ||
            migratedHistory->entries().size() != 1 ||
            migratedHistory->entries()[0].partition != coral::history::Partition::Simplified) {
            std::cerr << "history did not filter embedded policy: " << error << '\n';
            return 19;
        }
    }
    {
        namespace policy = coral::candidate_policy;
        policy::Manager manager;
        const auto base = testDir / "manager" / "userlexicon.coral";
        if (!manager.Start(base, error)) return 23;
        policy::Rule simplified;
        simplified.variant = coral::LanguageVariant::Simplified;
        simplified.kind = policy::RuleKind::Promote;
        simplified.pinned = true;
        simplified.lookup_key = "beijing";
        simplified.full_pinyin = "bei'jing";
        simplified.phrase = u8"北京";
        auto traditional = simplified;
        traditional.variant = coral::LanguageVariant::Traditional;
        if (!manager.SetRule(simplified) || !manager.SetRule(traditional)) return 24;
        auto replacement = simplified;
        replacement.phrase = u8"背景";
        if (!manager.SetRule(replacement)) return 25;
        auto matches = manager.Lookup(simplified.variant, simplified.lookup_key);
        if (matches.size() != 1 || matches[0].phrase != replacement.phrase) return 26;
        matches = manager.Lookup(traditional.variant, traditional.lookup_key);
        if (matches.size() != 1 || matches[0].phrase != traditional.phrase) return 27;
        // 自定义短语可以与置顶候选共存。
        auto custom = simplified;
        custom.phrase = u8"常用语";
        custom.pinned = false;
        if (!manager.SetRule(custom) || !manager.Remove(policy::IdentityOf(replacement))) return 28;
        matches = manager.Lookup(simplified.variant, simplified.lookup_key);
        if (matches.size() != 1 || matches[0].pinned || matches[0].phrase != custom.phrase) return 29;
        auto disabled = traditional;
        disabled.kind = policy::RuleKind::Disable;
        if (!manager.SetRule(disabled)) return 30;
        auto single = disabled;
        single.phrase = u8"中";
        if (manager.SetRule(single)) return 39;
        const auto legacyRules = policy::Snapshot::Build(0, {single, disabled});
        if (legacyRules->rules().size() != 1 || legacyRules->rules()[0].phrase != disabled.phrase) return 40;
        matches = manager.Lookup(traditional.variant, traditional.lookup_key);
        if (matches.size() != 1 || matches[0].kind != policy::RuleKind::Disable || matches[0].pinned) return 31;
        if (!manager.Lookup(traditional.variant, "bj").empty()) return 32;
        auto invalid = simplified;
        invalid.variant = static_cast<coral::LanguageVariant>(3);
        if (manager.SetRule(invalid)) return 33;
        if (!manager.Flush(error)) return 34;
        manager.Shutdown();
        if (!manager.Start(base, error)) return 35;
        matches = manager.Lookup(traditional.variant, traditional.lookup_key);
        if (matches.size() != 1 || matches[0].kind != policy::RuleKind::Disable) return 36;
        if (!manager.Remove(policy::IdentityOf(disabled)) || !manager.Flush(error)) return 37;
        manager.Shutdown();
    }
    std::filesystem::remove_all(testDir, ec);
    return 0;
}

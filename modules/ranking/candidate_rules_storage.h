#pragma once

#include "candidate_rules_snapshot.h"

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace coral::candidate_policy {

class Storage final {
public:
    explicit Storage(std::filesystem::path legacyBase, Options options = {});

    const std::filesystem::path& path() const { return file_; }
    bool Load(std::shared_ptr<const Snapshot>& snapshot, bool& migrated,
              std::string& error) const;
    bool Checkpoint(const std::vector<Mutation>& mutations,
                    std::shared_ptr<const Snapshot>& snapshot,
                    std::string& error) const;

    static std::filesystem::path PolicyPathFromLegacyBase(
        const std::filesystem::path& legacyBase);
    static std::vector<Rule> ApplyMutations(const std::vector<Rule>& base,
                                             const std::vector<Mutation>& mutations);

private:
    bool LoadV1(const std::filesystem::path& path,
                std::shared_ptr<const Snapshot>& snapshot,
                std::string& error) const;
    bool ImportExistingPolicies(std::shared_ptr<const Snapshot>& snapshot,
                                bool& found, std::string& error) const;
    bool AtomicWrite(const std::string& bytes, std::string& error) const;

    std::filesystem::path file_;
    std::filesystem::path legacy_base_;
    Options options_;
};

}  // 命名空间 coral::candidate_policy 结束

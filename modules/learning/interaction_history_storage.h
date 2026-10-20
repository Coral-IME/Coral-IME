#pragma once

#include "interaction_history_snapshot.h"

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace coral::history {

class Storage final {
public:
    explicit Storage(std::filesystem::path file, Options options = {});

    const std::filesystem::path& path() const { return file_; }
    bool Load(std::shared_ptr<const Snapshot>& snapshot, bool& migrated, std::string& error) const;
    bool Checkpoint(const std::vector<Mutation>& mutations,
                    std::shared_ptr<const Snapshot>& snapshot,
                    std::string& error) const;

    static std::filesystem::path V2PathFromLegacyBase(const std::filesystem::path& legacyBase);

private:
    bool LoadV2(const std::filesystem::path& path, std::shared_ptr<const Snapshot>& snapshot,
                std::string& error) const;
    bool LoadLegacy(std::shared_ptr<const Snapshot>& snapshot, std::string& error) const;
    bool AtomicWrite(const std::string& bytes, std::string& error) const;
    std::vector<Entry> ApplyMutations(const std::vector<Entry>& base,
                                      const std::vector<Mutation>& mutations) const;
    void EnforceCapacity(std::vector<Entry>& entries) const;

    std::filesystem::path file_;
    std::filesystem::path legacy_base_;
    Options options_;
};

}  // 命名空间 coral::history 结束

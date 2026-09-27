// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "shader-browser-override-store.hh"

#include <mutex>
#include <string>
#include <unordered_set>
#include <vector>

namespace xemu::shader_browser {

struct ReplacementLibraryError {
    std::string path;
    std::string message;
};

struct ReplacementPackageInfo {
    std::string logical_id;
    std::string directory;
    ReplacementDescriptor descriptor;
};

struct ReplacementLibrarySnapshot {
    uint64_t generation = 0;
    std::string root_path;
    std::vector<ReplacementPackageInfo> packages;
    std::vector<ReplacementLibraryError> errors;
};

class ReplacementLibrary
{
public:
    void Configure(const std::string &base_path);
    bool EnsureRoot(std::string *error);
    bool Reload(OverrideStore *store, std::string *error);
    void CopySnapshot(ReplacementLibrarySnapshot *snapshot) const;
    std::string RootPath() const;

private:
    bool LoadPackage(const std::string &directory,
                     ReplacementPayload *payload,
                     ReplacementPackageInfo *info,
                     std::string *error) const;

    mutable std::mutex mutex_;
    uint64_t generation_ = 0;
    std::string root_path_;
    std::vector<ReplacementPackageInfo> packages_;
    std::vector<ReplacementLibraryError> errors_;
    std::unordered_set<uint64_t> managed_ids_;
};

ReplacementLibrary &GetReplacementLibrary();

} // namespace xemu::shader_browser

// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "shader-browser-details-model.hh"

#include <cstdint>
#include <mutex>
#include <string>

namespace xemu::shader_browser {

class DetailStore
{
public:
    uint64_t Request(const ShaderKey &key, DetailBackend backend,
                     uint32_t flags = DetailRequestAll);

    // Only the active renderer owner thread should claim a request. The store
    // retains one newest request, so rapid selection cannot build a queue.
    bool TryClaim(DetailBackend renderer, DetailRequest *request);

    // Returns false for invalid or stale completions. Stale data is discarded
    // rather than replacing the currently selected shader's details.
    bool Complete(DetailResult result, std::string *error);

    // Ends a claimed request if its renderer was reset or switched before
    // completion. A newer request cannot be abandoned by an old claimant.
    bool Abandon(uint64_t request_id, DetailBackend renderer,
                 const std::string &reason);

    bool CopySnapshot(DetailSnapshot *snapshot) const;
    bool CopySnapshotIfChanged(uint64_t known_generation,
                               DetailSnapshot *snapshot) const;
    void Clear();

private:
    mutable std::mutex mutex_;
    uint64_t next_request_id_ = 1;
    bool request_claimed_ = false;
    DetailBackend claimed_backend_ = DetailBackend::Unknown;
    DetailSnapshot snapshot_;
};

// Process-lifetime singleton. It stores copied values only and never owns or
// exposes renderer cache objects.
DetailStore &GetDetailStore();

} // namespace xemu::shader_browser

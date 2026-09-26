// SPDX-License-Identifier: GPL-2.0-or-later
#include "shader-browser-details-store.hh"

#include <algorithm>

namespace xemu::shader_browser {

uint64_t DetailStore::Request(const ShaderKey &key, DetailBackend backend,
                              uint32_t flags)
{
    std::lock_guard<std::mutex> lock(mutex_);

    const uint64_t request_id = next_request_id_++;
    snapshot_.generation++;
    snapshot_.request.request_id = request_id;
    snapshot_.request.key = key;
    snapshot_.request.backend = backend;
    snapshot_.request.flags = flags & DetailRequestAll;
    snapshot_.state = DetailState::Pending;
    snapshot_.backend = DetailBackend::Unknown;
    snapshot_.status.clear();
    snapshot_.sources.clear();
    snapshot_.variants.clear();
    snapshot_.lifecycle.clear();
    snapshot_.dropped_lifecycle_events = 0;
    request_claimed_ = false;
    return request_id;
}

bool DetailStore::TryClaim(DetailBackend renderer, DetailRequest *request)
{
    if (!request) {
        return false;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    if (snapshot_.state != DetailState::Pending || request_claimed_ ||
        !DetailBackendMatches(snapshot_.request.backend, renderer)) {
        return false;
    }

    request_claimed_ = true;
    *request = snapshot_.request;
    return true;
}

bool DetailStore::Complete(const DetailResult &result, std::string *error)
{
    if (!ValidateDetailResult(result, error)) {
        return false;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    if (snapshot_.state != DetailState::Pending ||
        result.request_id != snapshot_.request.request_id ||
        result.key != snapshot_.request.key) {
        if (error) {
            *error = "stale shader detail completion";
        }
        return false;
    }

    snapshot_.state = result.state;
    snapshot_.backend = result.backend;
    snapshot_.status = result.status;
    snapshot_.sources = result.sources;
    snapshot_.variants = result.variants;
    snapshot_.dropped_lifecycle_events = result.dropped_lifecycle_events;

    const size_t lifecycle_count = result.lifecycle.size();
    const size_t first = lifecycle_count > kMaxLifecycleEvents
                             ? lifecycle_count - kMaxLifecycleEvents
                             : 0;
    snapshot_.lifecycle.assign(result.lifecycle.begin() + first,
                               result.lifecycle.end());
    snapshot_.dropped_lifecycle_events += first;
    snapshot_.generation++;
    request_claimed_ = false;
    return true;
}

bool DetailStore::CopySnapshot(DetailSnapshot *snapshot) const
{
    if (!snapshot) {
        return false;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    *snapshot = snapshot_;
    return true;
}

void DetailStore::Clear()
{
    std::lock_guard<std::mutex> lock(mutex_);
    const uint64_t generation = snapshot_.generation + 1;
    snapshot_ = {};
    snapshot_.generation = generation;
    request_claimed_ = false;
}

DetailStore &GetDetailStore()
{
    static DetailStore store;
    return store;
}

} // namespace xemu::shader_browser

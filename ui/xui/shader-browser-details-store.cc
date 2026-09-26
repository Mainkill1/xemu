// SPDX-License-Identifier: GPL-2.0-or-later
#include "shader-browser-details-store.hh"

#include <algorithm>
#include <iterator>
#include <utility>

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
    claimed_backend_ = DetailBackend::Unknown;
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
    claimed_backend_ = renderer;
    *request = snapshot_.request;
    return true;
}

bool DetailStore::Complete(DetailResult result, std::string *error)
{
    DetailRequest expected{};
    DetailBackend claimant = DetailBackend::Unknown;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (snapshot_.state != DetailState::Pending || !request_claimed_ ||
            result.request_id != snapshot_.request.request_id ||
            result.key != snapshot_.request.key) {
            if (error) {
                *error = "stale or unclaimed shader detail completion";
            }
            return false;
        }
        expected = snapshot_.request;
        claimant = claimed_backend_;
    }
    if (result.backend != claimant) {
        if (error) {
            *error = "shader detail completion backend mismatch";
        }
        return false;
    }
    if (!(expected.flags & DetailRequestSources) && !result.sources.empty()) {
        if (error) {
            *error = "unrequested shader source payload";
        }
        return false;
    }
    if (!(expected.flags & DetailRequestVariants) && !result.variants.empty()) {
        if (error) {
            *error = "unrequested shader variant payload";
        }
        return false;
    }
    if (!(expected.flags & DetailRequestLifecycle) &&
        !result.lifecycle.empty()) {
        if (error) {
            *error = "unrequested shader lifecycle payload";
        }
        return false;
    }
    if (!ValidateDetailResult(result, error)) {
        return false;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    if (snapshot_.state != DetailState::Pending || !request_claimed_ ||
        result.request_id != snapshot_.request.request_id ||
        result.key != snapshot_.request.key ||
        claimed_backend_ != claimant) {
        if (error) {
            *error = "stale shader detail completion";
        }
        return false;
    }

    snapshot_.state = result.state;
    snapshot_.backend = result.backend;
    snapshot_.status = std::move(result.status);
    snapshot_.sources = std::move(result.sources);
    snapshot_.variants = std::move(result.variants);
    snapshot_.dropped_lifecycle_events = result.dropped_lifecycle_events;

    const size_t lifecycle_count = result.lifecycle.size();
    const size_t first = lifecycle_count > kMaxLifecycleEvents
                             ? lifecycle_count - kMaxLifecycleEvents
                             : 0;
    snapshot_.lifecycle.assign(
        std::make_move_iterator(result.lifecycle.begin() + first),
        std::make_move_iterator(result.lifecycle.end()));
    snapshot_.dropped_lifecycle_events += first;
    snapshot_.generation++;
    request_claimed_ = false;
    claimed_backend_ = DetailBackend::Unknown;
    return true;
}

bool DetailStore::Abandon(uint64_t request_id, DetailBackend renderer,
                          const std::string &reason)
{
    if (reason.empty() || reason.size() > kMaxDetailTextBytes ||
        reason.find('\0') != std::string::npos) {
        return false;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    if (snapshot_.state != DetailState::Pending || !request_claimed_ ||
        snapshot_.request.request_id != request_id ||
        claimed_backend_ != renderer) {
        return false;
    }
    snapshot_.state = DetailState::Unavailable;
    snapshot_.backend = renderer;
    snapshot_.status = reason;
    snapshot_.generation++;
    request_claimed_ = false;
    claimed_backend_ = DetailBackend::Unknown;
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

bool DetailStore::CopySnapshotIfChanged(uint64_t known_generation,
                                        DetailSnapshot *snapshot) const
{
    if (!snapshot) {
        return false;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    if (snapshot_.generation == known_generation) {
        return false;
    }
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
    claimed_backend_ = DetailBackend::Unknown;
}

DetailStore &GetDetailStore()
{
    static DetailStore store;
    return store;
}

} // namespace xemu::shader_browser

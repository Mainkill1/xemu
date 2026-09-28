// SPDX-License-Identifier: GPL-2.0-or-later
#include "asset-browser-live.hh"
#include <algorithm>
#include <chrono>
namespace xemu::asset_browser {
AssetLiveCapture::AssetLiveCapture(capture::CaptureSession &session)
    : session_(session)
{
}
AssetLiveCapture::~AssetLiveCapture()
{
    Disable();
    if (job_.valid()) {
        try {
            job_.get();
        } catch (...) {
        }
    }
}
bool AssetLiveCapture::Enable(const capture::CaptureSessionContext &context,
                              uint64_t now, const AssetLiveSettings &settings)
{
    Disable();
    if (job_.valid() || !now ||
        settings.sample_interval_ns < UINT64_C(33000000) ||
        !settings.progress_timeout_ns) {
        message_ = "Previous asset job is still finishing or sampling settings "
                   "are invalid";
        return false;
    }
    settings_ = settings;
    settings_.capture.mode = capture::CaptureSessionMode::NextFrame;
    settings_.capture.maximum_evidence = false;
    settings_.capture.event_budget =
        std::min<uint32_t>(settings_.capture.event_budget, 32768);
    context_ = context;
    context_.generation = now;
    if (!session_.TryStart(context_, settings_.capture, &owned_)) {
        message_ = "Another capture or readback owns the recorder; stop it "
                   "before live discovery";
        return false;
    }
    enabled_ = true;
    started_ = now;
    next_ = 0;
    message_ = "Recording a bounded game frame without pausing";
    return true;
}
void AssetLiveCapture::Disable()
{
    enabled_ = false;
    if (owned_)
        session_.StopIfCurrent(owned_);
    owned_ = 0;
}
void AssetLiveCapture::Tick(const capture::CaptureSessionContext &context,
                            uint64_t now, AssetController &controller)
{
    if (job_.valid() &&
        job_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        try {
            auto result = job_.get();
            if (enabled_ && result.claim_generation == owned_ &&
                result.generation == controller.Generation()) {
                uint64_t current = 0;
                session_.Context(&current);
                if (!result.valid || current != owned_) {
                    owned_ = 0;
                    enabled_ = false;
                    message_ =
                        "Capture owner changed; stale asset result discarded";
                    return;
                }
                if (result.pending) {
                    message_ = "Waiting for captured GPU resources";
                    next_ = now + UINT64_C(50000000);
                } else {
                    const bool exhausted = result.catalog.budget_exceeded;
                    if (controller.Publish(std::move(result.catalog),
                                           result.generation))
                        last_capture_ = now;
                    owned_ = 0;
                    next_ = now + settings_.sample_interval_ns;
                    message_ = controller.Catalog().reason;
                    if (exhausted) {
                        enabled_ = false;
                        message_ = "Capture budget reached; retained partial "
                                   "frame is available. Reduce scope or "
                                   "increase the budget before restarting";
                    }
                }
            }
        } catch (const std::exception &error) {
            Disable();
            message_ = std::string("Asset decode failed: ") + error.what();
        }
    }
    if (!enabled_)
        return;
    if (!SameAssetContext(context_, context)) {
        Disable();
        controller.Invalidate();
        message_ = "Game or renderer changed; live acquisition stopped";
        return;
    }
    if (controller.Frozen()) {
        Disable();
        return;
    }
    if (owned_) {
        uint64_t current = 0;
        session_.Context(&current);
        if (current != owned_) {
            owned_ = 0;
            enabled_ = false;
            message_ = "Another capture replaced live discovery; retained "
                       "asset stays owned";
            return;
        }
        if (now >= started_ && now - started_ > settings_.progress_timeout_ns) {
            Disable();
            message_ = "Capture made no complete frame progress; live "
                       "acquisition stopped";
            return;
        }
        if (session_.Active() || job_.valid() || now < next_)
            return;
        const uint64_t claim = owned_, generation = controller.Generation();
        const auto limits = settings_.assets;
        try {
            job_ = std::async(std::launch::async, [this, claim, generation,
                                                   limits] {
                Result result;
                result.generation = generation;
                result.claim_generation = claim;
                uint64_t worker_generation = 0;
                session_.Context(&worker_generation);
                if (worker_generation != claim)
                    return result;
                const auto snapshot = session_.Snapshot();
                session_.Context(&worker_generation);
                if (worker_generation != claim)
                    return result;
                result.valid = true;
                result.pending =
                    snapshot.pending_events != 0 ||
                    snapshot.state == capture::CaptureSessionState::Finalizing;
                if (!result.pending)
                    result.catalog = BuildAssetCatalog(snapshot, limits);
                return result;
            });
        } catch (const std::exception &error) {
            Disable();
            message_ =
                std::string("Asset worker could not start: ") + error.what();
        }
    } else if (!job_.valid() && now >= next_) {
        context_ = context;
        context_.generation = now;
        if (!session_.TryStart(context_, settings_.capture, &owned_)) {
            enabled_ = false;
            message_ =
                "Another capture owns the recorder; live acquisition stopped";
            return;
        }
        started_ = now;
        message_ = "Recording the next bounded game frame";
    }
}
bool AssetLiveCapture::Enabled() const
{
    return enabled_;
}
const std::string &AssetLiveCapture::Message() const
{
    return message_;
}
uint64_t AssetLiveCapture::LastCaptureNs() const
{
    return last_capture_;
}
uint64_t AssetLiveCapture::OwnedGeneration() const
{
    return owned_;
}
} // namespace xemu::asset_browser

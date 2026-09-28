// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <algorithm>
#include <cstdint>
#include <mutex>

namespace xemu::shader_browser {

enum class CaptureFilePhase {
    Preparing,
    IndexingEvents,
    WritingResources,
    WritingMetadata,
    ReadingManifest,
    ReadingResources,
    ReadingEvents,
    Validating,
    Publishing,
    Complete,
};

struct CaptureFileProgress {
    CaptureFilePhase phase = CaptureFilePhase::Preparing;
    uint64_t completed = 0, total = 0;
    bool cancel_requested = false, publication_started = false;
    bool finished = false, success = false;
};

// One control belongs to one file operation. Cancellation and the final
// publication boundary are ordered under the same lock: an accepted
// cancellation cannot publish a package or replace the inspected recording
// afterward.
class CaptureFileControl {
public:
    bool RequestCancel()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (progress_.publication_started || progress_.finished)
            return false;
        progress_.cancel_requested = true;
        return true;
    }

    bool Checkpoint(CaptureFilePhase phase, uint64_t completed = 0,
                    uint64_t total = 0)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (progress_.cancel_requested || progress_.finished)
            return false;
        progress_.phase = phase;
        progress_.completed = total ? std::min(completed, total) : completed;
        progress_.total = total;
        return true;
    }

    bool BeginPublication()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (progress_.cancel_requested || progress_.publication_started ||
            progress_.finished)
            return false;
        progress_.publication_started = true;
        progress_.phase = CaptureFilePhase::Publishing;
        progress_.completed = progress_.total = 0;
        return true;
    }

    void Finish(bool success)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (progress_.finished)
            return;
        progress_.success = success && !progress_.cancel_requested;
        progress_.finished = true;
        progress_.phase = CaptureFilePhase::Complete;
    }

    CaptureFileProgress Progress() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return progress_;
    }

private:
    mutable std::mutex mutex_;
    CaptureFileProgress progress_;
};

} // namespace xemu::shader_browser

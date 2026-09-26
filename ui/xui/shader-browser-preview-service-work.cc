// SPDX-License-Identifier: GPL-2.0-or-later
#include "shader-browser-preview-service.hh"

namespace xemu::shader_browser {

static bool SameInteractionInputs(const PreviewResultKey &lhs,
                                  const PreviewResultKey &rhs)
{
    return lhs.compile == rhs.compile &&
           lhs.input_revision == rhs.input_revision &&
           lhs.view_revision == rhs.view_revision &&
           lhs.width == rhs.width && lhs.height == rhs.height &&
           lhs.packet_kind == rhs.packet_kind &&
           lhs.replay_class == rhs.replay_class &&
           lhs.fixture_digest == rhs.fixture_digest;
}

void PreviewService::ApplyPressureRecoveryLocked(uint64_t now_ns)
{
    if (!health_valid_ || recovery_candidate_ >= effective_pressure_ ||
        now_ns < recovery_candidate_since_ns_ ||
        now_ns - recovery_candidate_since_ns_ < kPreviewPressureRecoveryNs) {
        return;
    }
    effective_pressure_ = recovery_candidate_;
    ++generation_;
}

uint64_t PreviewService::IntervalForPressureLocked() const
{
    switch (effective_pressure_) {
    case PreviewPressure::Normal: return kPreviewNormalIntervalNs;
    case PreviewPressure::Elevated: return kPreviewElevatedIntervalNs;
    case PreviewPressure::High: return kPreviewHighIntervalNs;
    case PreviewPressure::Critical: return 0;
    }
    return 0;
}

uint32_t PreviewService::UpdateHzLocked() const
{
    switch (effective_pressure_) {
    case PreviewPressure::Normal: return 15;
    case PreviewPressure::Elevated: return 8;
    case PreviewPressure::High: return 4;
    case PreviewPressure::Critical: return 0;
    }
    return 0;
}

bool PreviewService::IsPreparedLocked() const
{
    return prepared_ && pending_.packet &&
           prepared_key_ == BuildPreviewCompileKey(*pending_.packet);
}

bool PreviewService::IsCurrentRequestLocked(
    uint64_t request_id, const PreviewResultKey *result_key) const
{
    if (!pending_.packet || request_id != latest_request_id_ ||
        request_id != pending_.request_id) {
        return false;
    }
    return !result_key || *result_key == BuildPreviewResultKey(*pending_.packet);
}

int PreviewService::FindFreeSlotLocked() const
{
    for (size_t i = 0; i < slots_.size(); ++i) {
        if (slots_[i].state == PreviewSlotState::Free) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

int PreviewService::FindNewestReadySlotLocked() const
{
    int best = -1;
    uint64_t best_sequence = 0;
    for (size_t i = 0; i < slots_.size(); ++i) {
        if (slots_[i].state == PreviewSlotState::Ready &&
            (best < 0 || slots_[i].ready_sequence > best_sequence)) {
            best = static_cast<int>(i);
            best_sequence = slots_[i].ready_sequence;
        }
    }
    return best;
}

bool PreviewService::TryClaimWork(uint64_t now_ns, PreviewWorkItem *work,
                                  PreviewBackend backend_filter)
{
    if (!work) {
        return false;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    *work = {};
    if (!enabled_) {
        SetStateLocked(PreviewState::Disabled, "Preview is disabled");
        return false;
    }
    if (ExpireVisibilityLocked(now_ns)) {
        return false;
    }
    if (!visible_) {
        SetStateLocked(PreviewState::Hidden, "Live Preview is not visible");
        return false;
    }
    if (!has_selection_) {
        SetStateLocked(PreviewState::NoSelection,
                       "Select a shader to preview it");
        return false;
    }
    if (!pending_.packet) {
        SetStateLocked(PreviewState::WaitingForInputs,
                       "Waiting for an immutable preview packet");
        return false;
    }
    if (backend_filter != PreviewBackend::Unknown &&
        pending_.packet->selection.backend != backend_filter) {
        return false;
    }
    if (active_) {
        return false;
    }
    if (now_ns < selection_changed_ns_ ||
        now_ns - selection_changed_ns_ < kPreviewSelectionDebounceNs) {
        SetStateLocked(PreviewState::Debouncing,
                       "Waiting for shader selection to settle");
        return false;
    }

    if (preparation_requested_) {
        if (!guest_paused_) {
            preparation_requested_ = false;
            SetStateLocked(PreviewState::NeedsPreparation,
                           "Preparation cancelled because the guest resumed");
            return false;
        }
        active_work_ = {};
        active_work_.token = next_token_++;
        active_work_.request_id = pending_.request_id;
        active_work_.kind = PreviewWorkKind::Prepare;
        active_work_.packet = pending_.packet;
        active_work_.compile_key =
            BuildPreviewCompileKey(*pending_.packet);
        active_ = true;
        preparation_requested_ = false;
        *work = active_work_;
        SetStateLocked(PreviewState::Preparing,
                       "Preparing private preview resources");
        return true;
    }

    if (unsupported_ &&
        unsupported_key_ == BuildPreviewCompileKey(*pending_.packet)) {
        SetStateLocked(PreviewState::Unsupported, unsupported_reason_);
        return false;
    }

    if (!IsPreparedLocked()) {
        SetStateLocked(PreviewState::NeedsPreparation,
                       guest_paused_ ?
                           "Request preparation for the selected shader" :
                           "Pause the guest before preparing preview resources");
        return false;
    }

    ApplyPressureRecoveryLocked(now_ns);
    if (!guest_paused_) {
        if (!health_valid_ || now_ns < health_.sampled_ns ||
            now_ns - health_.sampled_ns > kPreviewHealthStaleNs ||
            !health_.game_progressing) {
            ++dropped_stale_health_;
            SetStateLocked(PreviewState::Frozen,
                           "Preview frozen because gameplay health is stale");
            return false;
        }
        if (effective_pressure_ == PreviewPressure::Critical) {
            ++dropped_pressure_;
            SetStateLocked(PreviewState::Frozen,
                           "Preview frozen because gameplay has priority");
            return false;
        }
    }

    PreviewResultKey result_key = BuildPreviewResultKey(*pending_.packet);
    if (!pending_.packet->animated && last_result_valid_ &&
        last_result_key_ == result_key) {
        SetStateLocked(PreviewState::Ready,
                       "Static preview is already current");
        return false;
    }

    uint64_t interval = guest_paused_ ? kPreviewPausedIntervalNs :
                                       IntervalForPressureLocked();
    const bool immediate_paused_update =
        guest_paused_ &&
        (!last_attempt_valid_ ||
         !SameInteractionInputs(last_attempt_result_key_, result_key));
    if (last_render_start_ns_ && now_ns >= last_render_start_ns_ &&
        now_ns - last_render_start_ns_ < interval &&
        !immediate_paused_update) {
        SetStateLocked(PreviewState::Throttled,
                       "Preview update rate is bounded");
        return false;
    }

    int slot_index = FindFreeSlotLocked();
    if (slot_index < 0) {
        ++dropped_no_slot_;
        SetStateLocked(PreviewState::Throttled,
                       "Preview update dropped; all output slots are owned");
        return false;
    }

    Slot &slot = slots_[slot_index];
    slot.state = PreviewSlotState::Rendering;
    ++slot.generation;
    slot.ready_sequence = 0;
    slot.result_key = result_key;
    slot.width = pending_.packet->width;
    slot.height = pending_.packet->height;

    active_work_ = {};
    active_work_.token = next_token_++;
    active_work_.request_id = pending_.request_id;
    active_work_.kind = PreviewWorkKind::Render;
    active_work_.packet = pending_.packet;
    active_work_.compile_key = BuildPreviewCompileKey(*pending_.packet);
    active_work_.result_key = result_key;
    active_work_.slot = static_cast<uint32_t>(slot_index);
    active_work_.slot_generation = slot.generation;
    active_ = true;
    last_render_start_ns_ = now_ns;
    last_attempt_valid_ = true;
    last_attempt_result_key_ = result_key;
    *work = active_work_;
    SetStateLocked(PreviewState::Rendering,
                   "Rendering a private bounded preview update");
    return true;
}

bool PreviewService::CompletePreparation(uint64_t token,
                                         PreviewPreparationOutcome outcome,
                                         const std::string &status,
                                         uint64_t now_ns)
{
    std::lock_guard<std::mutex> lock(mutex_);
    ExpireVisibilityLocked(now_ns);
    if (!active_ || active_work_.token != token ||
        active_work_.kind != PreviewWorkKind::Prepare) {
        return false;
    }
    bool current = IsCurrentRequestLocked(active_work_.request_id, nullptr);
    if (outcome == PreviewPreparationOutcome::Succeeded && current) {
        prepared_ = true;
        prepared_key_ = active_work_.compile_key;
        unsupported_ = false;
        unsupported_reason_.clear();
        SetStateLocked(PreviewState::Ready,
                       status.empty() ? "Preview resources prepared" : status);
    } else if (!current) {
        ++stale_completions_;
        SetRestingStateLocked("Discarded obsolete preparation result");
    } else if (outcome == PreviewPreparationOutcome::Unsupported) {
        prepared_ = false;
        unsupported_ = true;
        unsupported_key_ = active_work_.compile_key;
        unsupported_reason_ = status.empty() ?
            "Selected shader interface is unsupported by synthetic preview" :
            status;
        SetStateLocked(PreviewState::Unsupported, unsupported_reason_);
    } else {
        prepared_ = false;
        unsupported_ = false;
        unsupported_reason_.clear();
        SetStateLocked(PreviewState::Failed,
                       status.empty() ? "Preview preparation failed" : status);
    }
    active_ = false;
    active_work_ = {};
    return true;
}

bool PreviewService::CompleteRender(uint64_t token, bool success,
                                    const std::string &status,
                                    uint64_t now_ns)
{
    std::lock_guard<std::mutex> lock(mutex_);
    ExpireVisibilityLocked(now_ns);
    if (!active_ || active_work_.token != token ||
        active_work_.kind != PreviewWorkKind::Render ||
        active_work_.slot >= slots_.size()) {
        return false;
    }
    Slot &slot = slots_[active_work_.slot];
    if (slot.generation != active_work_.slot_generation ||
        slot.state != PreviewSlotState::Rendering) {
        active_ = false;
        active_work_ = {};
        return false;
    }
    bool current = IsCurrentRequestLocked(active_work_.request_id,
                                          &active_work_.result_key);
    if (success && current) {
        slot.ready_sequence = next_ready_sequence_++;
        slot.state = PreviewSlotState::Ready;
        last_result_valid_ = true;
        last_result_key_ = active_work_.result_key;
        SetStateLocked(PreviewState::Ready,
                       status.empty() ? "Preview image ready" : status);
    } else {
        slot.state = PreviewSlotState::Free;
        slot.ready_sequence = 0;
        if (!current) {
            ++stale_completions_;
            SetRestingStateLocked("Discarded obsolete preview result");
        } else {
            SetStateLocked(PreviewState::Failed,
                           status.empty() ? "Preview rendering failed" : status);
        }
    }
    active_ = false;
    active_work_ = {};
    return true;
}

} // namespace xemu::shader_browser

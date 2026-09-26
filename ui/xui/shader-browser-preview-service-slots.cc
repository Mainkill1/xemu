// SPDX-License-Identifier: GPL-2.0-or-later
#include "shader-browser-preview-service.hh"

namespace xemu::shader_browser {

bool PreviewService::TryAcquireReadyFrame(PreviewFrameRef *frame,
                                          uint64_t now_ns)
{
    if (!frame) {
        return false;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    if (ExpireVisibilityLocked(now_ns) || !enabled_ || !visible_) {
        return false;
    }
    int ready = FindNewestReadySlotLocked();
    if (ready < 0) {
        return false;
    }
    for (size_t i = 0; i < slots_.size(); ++i) {
        Slot &slot = slots_[i];
        // Current and Reference remain leased until the consumer retires them.
        if (slot.state == PreviewSlotState::Ready &&
            static_cast<int>(i) != ready) {
            // Completed frames that were never sampled have no consumer lease
            // and may be reclaimed immediately when a newer frame is chosen.
            slot.state = PreviewSlotState::Free;
            slot.ready_sequence = 0;
        }
    }
    Slot &slot = slots_[ready];
    slot.state = PreviewSlotState::DisplayLeased;
    frame->result_key = slot.result_key;
    frame->slot = static_cast<uint32_t>(ready);
    frame->slot_generation = slot.generation;
    frame->width = slot.width;
    frame->height = slot.height;
    ++generation_;
    return true;
}

bool PreviewService::ReleaseDisplayLease(uint32_t slot_index,
                                         uint64_t slot_generation)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (slot_index >= slots_.size()) {
        return false;
    }
    Slot &slot = slots_[slot_index];
    if (slot.generation != slot_generation ||
        (slot.state != PreviewSlotState::DisplayLeased &&
         slot.state != PreviewSlotState::Retiring)) {
        return false;
    }
    slot.state = PreviewSlotState::Retiring;
    if (enabled_ && visible_ && !failed_ && !unsupported_) {
        SetStateLocked(PreviewState::Retiring,
                       "Waiting for HUD sampling to retire");
    }
    return true;
}

bool PreviewService::CompleteDisplayRetirement(
    uint32_t slot_index, uint64_t slot_generation)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (slot_index >= slots_.size()) {
        return false;
    }
    Slot &slot = slots_[slot_index];
    if (slot.generation != slot_generation ||
        slot.state != PreviewSlotState::Retiring) {
        return false;
    }
    slot.state = PreviewSlotState::Free;
    slot.ready_sequence = 0;
    if (last_result_valid_ && slot.result_key == last_result_key_) {
        bool retained = false;
        for (const Slot &candidate : slots_) {
            if (candidate.state != PreviewSlotState::Free &&
                candidate.result_key == last_result_key_) {
                retained = true;
                break;
            }
        }
        if (!retained) {
            last_result_valid_ = false;
        }
    }
    SetRestingStateLocked("Preview output slot retired");
    return true;
}

void PreviewService::CopyStatus(PreviewStatus *status) const
{
    if (!status) {
        return;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    *status = {};
    status->clock = clock_.State();
    status->generation = generation_;
    status->state = state_;
    status->requested_mode = requested_mode_;
    status->pressure = effective_pressure_;
    status->enabled = enabled_;
    status->visible = visible_;
    status->guest_paused = guest_paused_;
    status->has_selection = has_selection_;
    status->has_packet = static_cast<bool>(pending_.packet);
    status->prepared = IsPreparedLocked();
    status->preparation_requested = preparation_requested_;
    status->work_active = active_;
    status->update_hz = guest_paused_ ? 30 : UpdateHzLocked();
    for (const Slot &slot : slots_) {
        switch (slot.state) {
        case PreviewSlotState::Free: ++status->free_slots; break;
        case PreviewSlotState::Ready: ++status->ready_slots; break;
        case PreviewSlotState::DisplayLeased:
        case PreviewSlotState::Retiring: ++status->leased_slots; break;
        case PreviewSlotState::Rendering: break;
        }
    }
    status->submitted_requests = submitted_requests_;
    status->superseded_requests = superseded_requests_;
    status->stale_completions = stale_completions_;
    status->dropped_no_slot = dropped_no_slot_;
    status->dropped_pressure = dropped_pressure_;
    status->dropped_stale_health = dropped_stale_health_;
    status->has_attempt = has_attempt_;
    status->attempted_compile = attempted_compile_;
    status->message = message_;
}

void PreviewService::ResetLocked()
{
    generation_ = 1;
    enabled_ = false;
    visible_ = false;
    visible_heartbeat_ns_ = 0;
    guest_paused_ = false;
    has_selection_ = false;
    selection_ = {};
    requested_mode_ = PreviewMode::Normal;
    selection_changed_ns_ = 0;
    channel_ = PreviewChannel::FinalRGBA;
    scene_ = {};
    scene_revision_ = 0;
    clock_ = {};
    clock_edit_revision_ = 0;
    clock_suspended_ = true;
    pending_ = {};
    next_request_id_ = 1;
    latest_request_id_ = 0;
    preparation_requested_ = false;
    failed_ = false;
    failed_render_ = false;
    failed_compile_ = {};
    failed_result_ = {};
    failure_message_.clear();
    has_attempt_ = false;
    attempted_compile_ = {};
    prepared_ = false;
    prepared_key_ = {};
    unsupported_ = false;
    unsupported_key_ = {};
    unsupported_reason_.clear();
    active_ = false;
    active_work_ = {};
    next_token_ = 1;
    health_valid_ = false;
    health_ = {};
    effective_pressure_ = PreviewPressure::Critical;
    recovery_candidate_ = PreviewPressure::Critical;
    recovery_candidate_since_ns_ = 0;
    slots_ = {};
    next_ready_sequence_ = 1;
    last_result_valid_ = false;
    last_result_key_ = {};
    last_attempt_valid_ = false;
    last_attempt_result_key_ = {};
    last_render_start_ns_ = 0;
    state_ = PreviewState::Disabled;
    message_ = "Preview is disabled";
    submitted_requests_ = 0;
    superseded_requests_ = 0;
    stale_completions_ = 0;
    dropped_no_slot_ = 0;
    dropped_pressure_ = 0;
    dropped_stale_health_ = 0;
}

void PreviewService::BackendDestroyed()
{
    std::lock_guard<std::mutex> lock(mutex_);
    // This is an epoch invalidation, never an ordinary fence retirement.
    // The backend has discarded all texture names and can only allocate new
    // objects. Keep lease generations monotonic across a subsequent restart.
    enabled_ = false;
    InvalidatePendingLocked(PreviewState::Disabled,
                            "Private preview backend closed");
    active_ = false;
    active_work_ = {};
    for (Slot &slot : slots_) {
        const uint64_t generation = slot.generation + 1;
        slot = {};
        slot.generation = generation;
    }
}

void PreviewService::ResetForTest()
{
    std::lock_guard<std::mutex> lock(mutex_);
    ResetLocked();
}

PreviewService &GetPreviewService()
{
    static PreviewService service;
    return service;
}

} // namespace xemu::shader_browser

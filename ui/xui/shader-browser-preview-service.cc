// SPDX-License-Identifier: GPL-2.0-or-later
#include "shader-browser-preview-service.hh"

#include <algorithm>
#include <limits>
#include <unordered_set>

namespace xemu::shader_browser {
namespace {

bool CheckedAdd(size_t lhs, size_t rhs, size_t *result)
{
    if (std::numeric_limits<size_t>::max() - lhs < rhs) {
        return false;
    }
    *result = lhs + rhs;
    return true;
}

} // namespace

PreviewService::PreviewService()
{
    ResetLocked();
}

void PreviewService::SetStateLocked(PreviewState state,
                                    const std::string &message)
{
    if (state_ == state && message_ == message) {
        return;
    }
    state_ = state;
    message_ = message;
    ++generation_;
}

void PreviewService::SetRestingStateLocked(const std::string &message)
{
    if (!enabled_) {
        SetStateLocked(PreviewState::Disabled, "Preview is disabled");
    } else if (!visible_) {
        SetStateLocked(PreviewState::Hidden, "Live Preview is not visible");
    } else if (!has_selection_) {
        SetStateLocked(PreviewState::NoSelection,
                       "Select a shader to preview it");
    } else if (!pending_.packet) {
        SetStateLocked(PreviewState::WaitingForInputs,
                       "Waiting for an immutable preview packet");
    } else if (!IsPreparedLocked()) {
        SetStateLocked(PreviewState::NeedsPreparation, message.empty() ?
                           "Preview preparation is required" : message);
    } else {
        SetStateLocked(PreviewState::Ready, message.empty() ?
                           "Preview inputs are prepared" : message);
    }
}

void PreviewService::InvalidatePendingLocked(PreviewState state,
                                             const std::string &message)
{
    latest_request_id_ = next_request_id_++;
    pending_ = {};
    preparation_requested_ = false;
    prepared_ = false;
    last_result_valid_ = false;
    for (Slot &slot : slots_) {
        if (slot.state == PreviewSlotState::Ready) {
            slot.state = PreviewSlotState::Free;
            slot.ready_sequence = 0;
        } else if (slot.state == PreviewSlotState::DisplayLeased) {
            slot.state = PreviewSlotState::Retiring;
        }
    }
    SetStateLocked(state, message);
}

bool PreviewService::ExpireVisibilityLocked(uint64_t now_ns)
{
    if (!visible_ || now_ns < visible_heartbeat_ns_ ||
        now_ns - visible_heartbeat_ns_ <= kPreviewVisibilityStaleNs) {
        return false;
    }
    visible_ = false;
    InvalidatePendingLocked(PreviewState::Hidden,
                            "Live Preview visibility heartbeat expired");
    return true;
}

void PreviewService::SetEnabled(bool enabled)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (enabled_ == enabled) {
        return;
    }
    enabled_ = enabled;
    if (!enabled) {
        InvalidatePendingLocked(PreviewState::Disabled,
                                "Preview is disabled");
    } else if (!visible_) {
        SetStateLocked(PreviewState::Hidden,
                       "Open the Live Preview tab to admit work");
    } else if (!has_selection_) {
        SetStateLocked(PreviewState::NoSelection,
                       "Select a shader to preview it");
    } else {
        SetStateLocked(PreviewState::WaitingForInputs,
                       "Waiting for an immutable preview packet");
    }
}

void PreviewService::SetVisible(bool visible, uint64_t now_ns)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (visible) {
        visible_heartbeat_ns_ = now_ns;
    }
    if (visible_ == visible) {
        return;
    }
    visible_ = visible;
    if (!visible) {
        InvalidatePendingLocked(PreviewState::Hidden,
                                "Live Preview is not visible");
        return;
    }
    selection_changed_ns_ = now_ns;
    if (!enabled_) {
        SetStateLocked(PreviewState::Disabled, "Preview is disabled");
    } else if (!has_selection_) {
        SetStateLocked(PreviewState::NoSelection,
                       "Select a shader to preview it");
    } else {
        SetStateLocked(PreviewState::WaitingForInputs,
                       "Waiting for an immutable preview packet");
    }
}

void PreviewService::SetGuestPaused(bool paused)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (guest_paused_ == paused) {
        return;
    }
    guest_paused_ = paused;
    ++generation_;
    if (!paused && state_ == PreviewState::NeedsPreparation) {
        message_ = "Pause the guest before preparing preview resources";
    }
}

void PreviewService::SetRequestedMode(PreviewMode mode)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (requested_mode_ == mode) {
        return;
    }
    requested_mode_ = mode;
    if (has_selection_ && selection_.mode != mode) {
        selection_.mode = mode;
        InvalidatePendingLocked(
            enabled_ && visible_ ? PreviewState::WaitingForInputs : state_,
            "Preview mode changed; waiting for matching inputs");
    } else {
        ++generation_;
    }
}

void PreviewService::SetSelection(const PreviewSelection &selection,
                                  uint64_t now_ns)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (has_selection_ && selection_ == selection) {
        return;
    }
    has_selection_ = true;
    selection_ = selection;
    requested_mode_ = selection.mode;
    selection_changed_ns_ = now_ns;
    InvalidatePendingLocked(
        enabled_ && visible_ ? PreviewState::WaitingForInputs :
        enabled_ ? PreviewState::Hidden : PreviewState::Disabled,
        "Selection changed; waiting for matching preview inputs");
}

void PreviewService::ClearSelection()
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (!has_selection_) {
        return;
    }
    has_selection_ = false;
    selection_ = {};
    InvalidatePendingLocked(PreviewState::NoSelection,
                            "Select a shader to preview it");
}

size_t PreviewService::AggregatePacketBytesLocked(
    const std::shared_ptr<const PreviewPacket> &candidate,
    bool *overflow) const
{
    std::unordered_set<const PreviewPacket *> seen;
    size_t total = 0;
    bool valid = true;
    auto include = [&](const std::shared_ptr<const PreviewPacket> &packet) {
        if (!valid || !packet || !seen.insert(packet.get()).second) {
            return;
        }
        bool packet_overflow = false;
        size_t bytes = PreviewPacketOwnedBytes(*packet, &packet_overflow);
        size_t next = 0;
        if (packet_overflow || !CheckedAdd(total, bytes, &next)) {
            valid = false;
            return;
        }
        total = next;
    };
    include(candidate);
    include(active_work_.packet);
    if (overflow) {
        *overflow = !valid;
    }
    return valid ? total : std::numeric_limits<size_t>::max();
}

bool PreviewService::SubmitPacket(PreviewPacket packet,
                                  uint64_t now_ns, std::string *error)
{
    std::string validation_error;
    if (!ValidatePreviewPacket(packet, &validation_error)) {
        if (error) *error = validation_error;
        return false;
    }
    auto owned = std::make_shared<const PreviewPacket>(std::move(packet));

    std::lock_guard<std::mutex> lock(mutex_);
    if (!has_selection_ || owned->selection != selection_) {
        if (error) *error = "Preview packet does not match the active selection";
        return false;
    }
    bool overflow = false;
    size_t aggregate = AggregatePacketBytesLocked(owned, &overflow);
    if (overflow || aggregate > kPreviewMaxOwnedPacketBytes) {
        if (error) {
            *error = "Active and pending preview packets exceed 32 MiB";
        }
        return false;
    }
    if (pending_.packet) {
        ++superseded_requests_;
    }
    pending_.request_id = next_request_id_++;
    pending_.packet = std::move(owned);
    latest_request_id_ = pending_.request_id;
    ++submitted_requests_;
    (void)now_ns;
    preparation_requested_ = false;
    if (!prepared_ ||
        prepared_key_ != BuildPreviewCompileKey(*pending_.packet)) {
        prepared_ = false;
    }
    if (!enabled_) {
        SetStateLocked(PreviewState::Disabled, "Preview is disabled");
    } else if (!visible_) {
        SetStateLocked(PreviewState::Hidden, "Live Preview is not visible");
    } else if (!IsPreparedLocked()) {
        SetStateLocked(PreviewState::NeedsPreparation,
                       guest_paused_ ?
                           "Request preparation for the selected shader" :
                           "Pause the guest before preparing preview resources");
    } else {
        SetStateLocked(PreviewState::Ready, "Preview inputs are prepared");
    }
    if (error) error->clear();
    return true;
}

bool PreviewService::RequestPreparation(std::string *error)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (!enabled_ || !visible_) {
        if (error) *error = "Enable and open Live Preview before preparation";
        return false;
    }
    if (!guest_paused_) {
        if (error) *error = "The guest must already be paused for preparation";
        return false;
    }
    if (!pending_.packet) {
        if (error) *error = "No validated preview packet is available";
        return false;
    }
    if (active_) {
        if (error) *error = "Preview work is already active";
        return false;
    }
    if (IsPreparedLocked()) {
        if (error) *error = "Preview resources are already prepared";
        return false;
    }
    preparation_requested_ = true;
    SetStateLocked(PreviewState::NeedsPreparation,
                   "Preparation requested while the guest is paused");
    if (error) error->clear();
    return true;
}

void PreviewService::UpdateHealth(const PreviewHealth &health)
{
    std::lock_guard<std::mutex> lock(mutex_);
    health_ = health;
    if (!health_valid_) {
        effective_pressure_ = health.pressure;
        recovery_candidate_ = health.pressure;
        recovery_candidate_since_ns_ = health.sampled_ns;
        health_valid_ = true;
        ++generation_;
        return;
    }
    if (health.pressure > effective_pressure_) {
        effective_pressure_ = health.pressure;
        recovery_candidate_ = health.pressure;
        recovery_candidate_since_ns_ = health.sampled_ns;
    } else if (health.pressure < effective_pressure_) {
        if (recovery_candidate_ != health.pressure) {
            recovery_candidate_ = health.pressure;
            recovery_candidate_since_ns_ = health.sampled_ns;
        } else if (health.sampled_ns >= recovery_candidate_since_ns_ &&
                   health.sampled_ns - recovery_candidate_since_ns_ >=
                       kPreviewPressureRecoveryNs) {
            effective_pressure_ = health.pressure;
        }
    } else {
        recovery_candidate_ = health.pressure;
        recovery_candidate_since_ns_ = health.sampled_ns;
    }
    ++generation_;
}

} // namespace xemu::shader_browser

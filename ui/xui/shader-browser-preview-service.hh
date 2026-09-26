// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "shader-browser-preview-backend.hh"

#include <array>
#include <memory>
#include <mutex>
#include <string>

namespace xemu::shader_browser {

struct PreviewStatus {
    uint64_t generation = 0;
    PreviewState state = PreviewState::Disabled;
    PreviewMode requested_mode = PreviewMode::Normal;
    PreviewPressure pressure = PreviewPressure::Critical;
    bool enabled = false;
    bool visible = false;
    bool guest_paused = false;
    bool has_selection = false;
    bool has_packet = false;
    bool prepared = false;
    bool preparation_requested = false;
    bool work_active = false;
    uint32_t update_hz = 0;
    size_t free_slots = 0;
    size_t ready_slots = 0;
    size_t leased_slots = 0;
    uint64_t submitted_requests = 0;
    uint64_t superseded_requests = 0;
    uint64_t stale_completions = 0;
    uint64_t dropped_no_slot = 0;
    uint64_t dropped_pressure = 0;
    uint64_t dropped_stale_health = 0;
    std::string message;
};

enum class PreviewPreparationOutcome : uint8_t {
    Succeeded,
    Failed,
    Unsupported,
};

class PreviewService
{
public:
    PreviewService();

    void SetEnabled(bool enabled);
    void SetVisible(bool visible, uint64_t now_ns);
    void SetGuestPaused(bool paused);
    void SetRequestedMode(PreviewMode mode);
    void SetSelection(const PreviewSelection &selection, uint64_t now_ns);
    void ClearSelection();

    bool SubmitPacket(PreviewPacket packet,
                      uint64_t now_ns, std::string *error);
    bool RequestPreparation(std::string *error);
    void UpdateHealth(const PreviewHealth &health);

    bool TryClaimWork(uint64_t now_ns, PreviewWorkItem *work,
                      PreviewBackend backend_filter = PreviewBackend::Unknown);
    bool CompletePreparation(uint64_t token, PreviewPreparationOutcome outcome,
                             const std::string &status, uint64_t now_ns);
    bool CompletePreparation(uint64_t token, bool success,
                             const std::string &status, uint64_t now_ns)
    {
        return CompletePreparation(token,
            success ? PreviewPreparationOutcome::Succeeded :
                      PreviewPreparationOutcome::Failed,
            status, now_ns);
    }
    bool CompleteRender(uint64_t token, bool success,
                        const std::string &status, uint64_t now_ns);

    bool TryAcquireReadyFrame(PreviewFrameRef *frame, uint64_t now_ns);
    bool ReleaseDisplayLease(uint32_t slot, uint64_t slot_generation);
    bool CompleteDisplayRetirement(uint32_t slot,
                                   uint64_t slot_generation);

    void CopyStatus(PreviewStatus *status) const;
    void ResetForTest();

private:
    struct PendingRequest {
        uint64_t request_id = 0;
        std::shared_ptr<const PreviewPacket> packet;
    };

    struct Slot {
        PreviewSlotState state = PreviewSlotState::Free;
        uint64_t generation = 0;
        uint64_t ready_sequence = 0;
        PreviewResultKey result_key;
        uint32_t width = 0;
        uint32_t height = 0;
    };

    bool IsCurrentRequestLocked(uint64_t request_id,
                                const PreviewResultKey *result_key) const;
    bool IsPreparedLocked() const;
    size_t AggregatePacketBytesLocked(
        const std::shared_ptr<const PreviewPacket> &candidate,
        bool *overflow) const;
    uint64_t IntervalForPressureLocked() const;
    uint32_t UpdateHzLocked() const;
    void ApplyPressureRecoveryLocked(uint64_t now_ns);
    void InvalidatePendingLocked(PreviewState state,
                                 const std::string &message);
    bool ExpireVisibilityLocked(uint64_t now_ns);
    void SetStateLocked(PreviewState state, const std::string &message);
    void SetRestingStateLocked(const std::string &message);
    int FindFreeSlotLocked() const;
    int FindNewestReadySlotLocked() const;
    void ResetLocked();

    mutable std::mutex mutex_;
    uint64_t generation_ = 0;
    bool enabled_ = false;
    bool visible_ = false;
    uint64_t visible_heartbeat_ns_ = 0;
    bool guest_paused_ = false;
    bool has_selection_ = false;
    PreviewSelection selection_;
    PreviewMode requested_mode_ = PreviewMode::Normal;
    uint64_t selection_changed_ns_ = 0;

    PendingRequest pending_;
    uint64_t next_request_id_ = 1;
    uint64_t latest_request_id_ = 0;
    bool preparation_requested_ = false;

    bool prepared_ = false;
    PreviewCompileKey prepared_key_;
    bool unsupported_ = false;
    PreviewCompileKey unsupported_key_;
    std::string unsupported_reason_;

    bool active_ = false;
    PreviewWorkItem active_work_;
    uint64_t next_token_ = 1;

    bool health_valid_ = false;
    PreviewHealth health_;
    PreviewPressure effective_pressure_ = PreviewPressure::Critical;
    PreviewPressure recovery_candidate_ = PreviewPressure::Critical;
    uint64_t recovery_candidate_since_ns_ = 0;

    std::array<Slot, kPreviewSlotCount> slots_{};
    uint64_t next_ready_sequence_ = 1;
    bool last_result_valid_ = false;
    PreviewResultKey last_result_key_;
    uint64_t last_render_start_ns_ = 0;

    PreviewState state_ = PreviewState::Disabled;
    std::string message_ = "Preview is disabled";

    uint64_t submitted_requests_ = 0;
    uint64_t superseded_requests_ = 0;
    uint64_t stale_completions_ = 0;
    uint64_t dropped_no_slot_ = 0;
    uint64_t dropped_pressure_ = 0;
    uint64_t dropped_stale_health_ = 0;
};

PreviewService &GetPreviewService();

} // namespace xemu::shader_browser

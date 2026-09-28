// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "shader-browser-capture-session.hh"
#include "shader-browser-preview-model.hh"

#include <functional>

namespace xemu::shader_browser {

enum class CaptureComparisonScope : uint8_t {
    SelectedOccurrence,
    MatchingFrame,
    MatchingRange,
};
enum class CaptureComparisonDependencies : uint8_t {
    FrozenInputs,
    ProducerSuffix,
};
enum class CaptureComparisonOutcome : uint8_t {
    Pending,
    Completed,
    CompileFailed,
    Unsupported,
    Incomplete,
    Cancelled,
};
enum class CaptureComparisonPhase : uint8_t { Original, Replacement };
enum class CaptureComparisonState : uint8_t {
    Idle,
    Running,
    Ready,
    Cancelled,
    Failed
};
struct CaptureComparisonSettings {
    uint32_t maximum_rows = 8192;
    uint32_t maximum_jobs = 8192;
    uint64_t packet_byte_budget = 64U * 1024U * 1024U;
    uint64_t image_byte_budget = 64U * 1024U * 1024U;
};
struct CaptureComparisonEdit {
    uint64_t id = 0, revision = 0, submission_id = 0;
    std::string source;
};
struct CaptureComparisonRequest {
    PreviewSelection selection;
    CaptureComparisonScope scope = CaptureComparisonScope::MatchingRange;
    CaptureComparisonDependencies dependencies =
        CaptureComparisonDependencies::FrozenInputs;
    uint64_t selected_event = 0, first_frame = 0, last_frame = UINT64_MAX;
    CaptureComparisonEdit edit;
    PreviewScene scene;
    PreviewRenderState render_state;
    // Zero requests the exact captured native viewport. No clamping occurs.
    uint32_t width = 0, height = 0;
    bool profile_draw = false;
};
struct CaptureComparisonIdentity {
    uint64_t request_id = 0, token = 0, event_id = 0;
    // Original revision is the immutable captured source block ID. For an
    // unmodified suffix consumer, replacement revision is that same block ID.
    uint64_t original_revision = 0, replacement_revision = 0;
    PreviewDigest original_digest{}, replacement_digest{};
    bool operator==(const CaptureComparisonIdentity &) const;
};
struct CaptureComparisonImage {
    uint32_t width = 0, height = 0;
    std::vector<uint8_t> rgba;
};
using SharedComparisonImage = std::shared_ptr<const CaptureComparisonImage>;
struct CaptureComparisonDifference {
    uint64_t compared_bytes = 0, compared_pixels = 0;
    uint64_t changed_bytes = 0, changed_pixels = 0;
    uint8_t maximum_difference = 0;
    double mean_difference = 0;
};
struct CaptureComparisonResult {
    CaptureComparisonIdentity identity;
    uint64_t frame = 0;
    // Suffix results also include downstream draws whose source is unchanged.
    bool edited_seed = true;
    CaptureComparisonOutcome outcome = CaptureComparisonOutcome::Pending;
    uint32_t capture_limitations = 0;
    PreviewReplayClass replay_class = PreviewReplayClass::Unsupported;
    bool has_packet_identity = false;
    PreviewResultKey original_key, replacement_key;
    SharedComparisonImage original, replacement;
    CaptureComparisonDifference difference;
    PreviewDrawTiming original_timing, replacement_timing;
    std::string message;
};
struct CaptureComparisonJob {
    CaptureComparisonIdentity identity;
    CaptureComparisonPhase phase = CaptureComparisonPhase::Original;
    std::shared_ptr<const CaptureOccurrence> occurrence;
    std::shared_ptr<const PreviewPacket> packet;
    PreviewResultKey expected_result;
    // True only when this phase actually uses the requested edited source.
    bool edited_seed = false;
};
struct CaptureComparisonCompletion {
    CaptureComparisonIdentity identity;
    CaptureComparisonPhase phase = CaptureComparisonPhase::Original;
    PreviewResultKey result;
    CaptureComparisonOutcome outcome = CaptureComparisonOutcome::Completed;
    CaptureComparisonImage image;
    // Profiled completed images require a terminal exact-result timing status.
    PreviewDrawTiming draw_timing;
    std::string message;
};
struct CaptureComparisonSnapshot {
    CaptureComparisonState state = CaptureComparisonState::Idle;
    uint64_t request_id = 0, revision = 0;
    uint64_t matched_events = 0, omitted_events = 0, admitted_jobs = 0;
    uint64_t stale_completions = 0, packet_bytes = 0, image_bytes = 0;
    uint64_t replay_steps = 0, replay_plan_bytes = 0, replay_value_bytes = 0;
    std::array<uint64_t, 6> outcomes{};
    std::string message;
    std::vector<CaptureComparisonResult> results;
    PreviewDrawTimingDistribution original_timing, replacement_timing;
};
enum class CaptureComparisonBuildOutcome : uint8_t {
    Ready,
    Unsupported,
    Incomplete
};
using CaptureComparisonPacketBuilder =
    std::function<CaptureComparisonBuildOutcome(
        const CaptureComparisonRequest &, const CaptureOccurrence &,
        PreviewPacket *original, PreviewPacket *replacement,
        std::string *error)>;

// Uses the existing preview adapter and original captured vertex pipeline.
// Frozen input comparisons expose partial material/raster/destination fidelity.
bool GetCapturedPreviewExtent(const OwnedDrawInputs &, PreviewBackend,
                              uint32_t *width, uint32_t *height,
                              std::string *error);
CaptureComparisonBuildOutcome BuildCaptureComparisonPackets(
    const CaptureComparisonRequest &, const CaptureOccurrence &,
    PreviewPacket *original, PreviewPacket *replacement, std::string *error);

// No renderer, worker or runtime replacement rule lives in this controller.
// The owner claims one phase at a time and drives the existing preview service
// only while its comparison panel is visible and execution is permitted.
class CaptureComparisonController {
public:
    CaptureComparisonController();
    ~CaptureComparisonController();
    uint64_t Start(const CaptureComparisonRequest &,
                   const CaptureSessionSnapshot &,
                   const CaptureComparisonSettings & = {});
    bool TryClaimJob(CaptureComparisonJob *,
                     const CaptureComparisonPacketBuilder & = {});
    bool StillCurrent(const CaptureComparisonIdentity &,
                      CaptureComparisonPhase) const;
    bool Complete(CaptureComparisonCompletion);
    void Cancel();
    void Invalidate(const PreviewSelection &);
    CaptureComparisonSnapshot Snapshot() const;
    uint64_t Revision() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace xemu::shader_browser

// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "shader-browser-draw-request.hh"
#include "shader-browser-capture-resources.hh"
#include "shader-browser-capture-replay-description.hh"
#include "shader-browser-capture-file-control.hh"

#include <filesystem>
#include <memory>
#include <string>

namespace xemu::shader_browser {

enum class CaptureSessionMode : uint8_t {
    NextFrame,
    RollingAnimation,
    // Owned draw inputs for live inspection; omits ordered non-draw
    // dependencies.
    LiveDrawInputs,
};
enum class CaptureSessionState : uint8_t {
    Recording,
    Triggered,
    Finalizing,
    Ready,
    Cancelled,
    BudgetExceeded,
    Failed,
};
enum class CaptureEventType : uint8_t {
    Draw,
    Clear,
    Copy,
    Upload,
    Resolve,
    FrameBoundary,
    StateWrite,
    AllocationBoundary,
    ResetBoundary,
    TitleBoundary,
    SaveStateBoundary,
};
enum CaptureSessionLimitation : uint32_t {
    CaptureMissingRawStreams = 1U << 0,
    CaptureMissingPipelineState = 1U << 1,
    CaptureMissingDependencies = 1U << 2,
    CaptureUnsupported = 1U << 3,
    CaptureMalformed = 1U << 4,
    CaptureSuppressed = 1U << 5,
    CaptureReadbackFailed = 1U << 6,
    CaptureInvalidated = 1U << 7,
};
struct CaptureSessionSettings {
    CaptureSessionMode mode = CaptureSessionMode::NextFrame;
    uint64_t cpu_byte_budget = 256U * 1024U * 1024U;
    uint64_t per_event_byte_budget = kDrawInputBudget;
    uint64_t disk_byte_budget = 512U * 1024U * 1024U;
    uint32_t event_budget = 8192;
    uint32_t history_frames = 60;
    uint32_t post_frames = 30;
    // Rolling acquisition leaves this percentage of total byte/event capacity
    // unused until Mark. It does not promise a number of post-trigger frames.
    uint32_t post_trigger_reserve_percent = 25;
    bool maximum_evidence = false;
    // Only LiveDrawInputs accepts a filter. Empty means discover all draws.
    // Each set describes every bound stage of one accepted pipeline pairing.
    std::vector<std::vector<ShaderKey>> live_stage_sets;
    // Explicit live inspection only: retain a bounded frame window while
    // acquiring the next frame. Forensic recordings never use this policy.
    bool live_continuous = false;
};
struct CaptureSessionCapacityLimits {
    uint64_t cpu_bytes = 0;
    uint32_t events = 0;
};
inline CaptureSessionCapacityLimits
CaptureSessionPreTriggerLimits(const CaptureSessionSettings &settings)
{
    const auto percent =
        settings.mode == CaptureSessionMode::RollingAnimation &&
                settings.post_frames ?
            settings.post_trigger_reserve_percent :
            0;
    if (percent > 99)
        return {};
    const auto reserve = [percent](uint64_t total) {
        // ceil(total * percent / 100), without multiplying a large total.
        return total / 100 * percent + (total % 100 * percent + 99) / 100;
    };
    return { settings.cpu_byte_budget - reserve(settings.cpu_byte_budget),
             uint32_t(settings.event_budget - reserve(settings.event_budget)) };
}
struct CaptureSessionContext {
    ShaderScope scope;
    uint64_t scope_generation = 0;
    uint64_t session_epoch = 0;
    uint64_t renderer_epoch = 0;
    uint64_t generation = 0;
    uint64_t current_frame = 0;
    uint32_t backend = 0;
};
struct CaptureImmutableBlock {
    uint64_t id = 0;
    CaptureDigest digest{};
    std::vector<uint8_t> bytes;
};
using SharedCaptureBlock = std::shared_ptr<const CaptureImmutableBlock>;
struct RawOwnedDrawGeometry {
    SharedCaptureBlock positions, indices;
    size_t position_count = 0, index_count = 0;
};
struct CaptureOwnedImage {
    uint32_t width = 0, height = 0;
    SharedCaptureBlock rgba;
};
struct CaptureOwnedTextureImage {
    uint32_t mip_level = 0, face = 0;
    CaptureOwnedImage image;
};
struct CaptureOwnedTexture {
    bool described = false;
    XemuShaderDrawTexture metadata{};
    std::vector<CaptureOwnedTextureImage> images;
};
struct CaptureOwnedUniform {
    uint32_t stage = 0, type = 0, components = 0, count = 0;
    std::string name;
    SharedCaptureBlock data;
};
struct CaptureOwnedBlob {
    std::string name;
    uint32_t slot = 0, format = 0, components = 0, stride = 0, count = 0;
    uint64_t offset = 0;
    uint32_t normalized = 0, integer = 0;
    SharedCaptureBlock data;
};
struct RawOwnedDrawInputs {
    std::array<CaptureOwnedTexture, 4> textures;
    std::vector<CaptureOwnedUniform> uniforms;
    std::array<SharedCaptureBlock, 5> sources;
    std::vector<OwnedDrawRegister> registers;
    std::vector<CaptureOwnedBlob> blobs;
    CaptureOwnedImage before, after;
    bool complete = false;
};
enum class CaptureCommandPhase : uint8_t { HostPreparation, Auxiliary, Main };
enum class CaptureBatchOutcome : uint8_t {
    None,
    Pending,
    Submitted,
    SubmissionFailed,
    Aborted,
    Detached
};
enum class CaptureBatchCompletion : uint8_t {
    None,
    Pending,
    Completed,
    Failed
};
enum class CaptureCommandKind : uint8_t {
    Unknown,
    CpuUpload,
    BufferCopy,
    Draw,
    Clear,
    ImageCopy,
    Resolve,
    Checkpoint
};
// Handles and backend objects are runtime-only. This description references
// the immutable resource accesses of its occurrence, in read/write order.
struct CaptureCommandDescription {
    CaptureCommandKind kind = CaptureCommandKind::Unknown;
    uint64_t source_offset = 0, destination_offset = 0, bytes = 0;
};
struct CaptureOccurrence {
    uint64_t event_id = 0;
    CaptureEventType type = CaptureEventType::Draw;
    DrawCaptureSummary summary;
    RawOwnedDrawGeometry geometry;
    RawOwnedDrawInputs inputs;
    SharedCaptureResourceEvent resource_evidence;
    uint32_t limitations = CaptureMissingRawStreams |
                           CaptureMissingPipelineState |
                           CaptureMissingDependencies;
    bool finished = false, emitted = false, pending = true;
    bool observed_checkpoint = false;
    uint64_t batch_id = 0, command_ordinal = 0, queue_ordinal = 0;
    CaptureCommandPhase command_phase = CaptureCommandPhase::Main;
    CaptureCommandDescription command;
    SharedCaptureReplayDescription replay_description;
    CaptureBatchOutcome submission = CaptureBatchOutcome::None;
    CaptureBatchCompletion completion = CaptureBatchCompletion::None;
    bool command_recorded = false, resource_finalized = true;
    int32_t backend_result = 0, completion_result = 0;
    uint64_t payload_bytes = 0;
    uint64_t host_timestamp_ns = 0;
    std::string failure;

    OwnedDrawInputs CopyInputs() const;
    OwnedDrawGeometry CopyGeometry() const;
};
struct CaptureSessionSnapshot {
    CaptureSessionState state = CaptureSessionState::Cancelled;
    CaptureSessionSettings settings;
    CaptureSessionContext context;
    uint64_t first_frame = 0, last_frame = 0, trigger_frame = 0;
    uint64_t cpu_bytes = 0, reserved_bytes = 0, unique_blocks = 0;
    uint64_t total_events = 0;
    uint64_t resource_domain = 0;
    // Legacy archives contain observation ordering, not verified queue order.
    bool execution_order_complete = true;
    // True only when acquisition reached its requested terminating guest-frame
    // boundary, or for an owned completed-frame view of continuous live input
    // acquisition. Ready also permits a manually stopped, partial frame.
    bool frame_window_complete = false;
    size_t pending_events = 0;
    bool has_frame_range = false;
    // Frames containing retained evidence; the min/max span can contain holes.
    std::vector<uint64_t> retained_frames;
    std::string reason;
    // Versioned inspection annotations belong to this recording. The
    // inspection controller validates its schema and stable event references.
    std::string annotations;
    std::vector<std::shared_ptr<const CaptureOccurrence>> events;
};
// Retained descriptor capacities, excluding immutable byte payload blocks.
uint64_t CaptureOccurrenceDescriptorBytes(const CaptureOccurrence &);

// The independent all-shader recorder owns many simultaneous readbacks. A
// preview request and shader filtering never retarget or rearm this session.
class CaptureSession {
public:
    CaptureSession();
    ~CaptureSession();
    CaptureSession(const CaptureSession &) = delete;
    CaptureSession &operator=(const CaptureSession &) = delete;
    bool Start(const CaptureSessionContext &,
               const CaptureSessionSettings & = {});
    // Atomically leave an active recorder or pending readback owner untouched.
    bool TryStart(const CaptureSessionContext &, const CaptureSessionSettings &,
                   uint64_t *claim_generation);
    bool StopIfCurrent(uint64_t claim_generation);
    bool Active() const;
    bool ReadbackPressure(uint64_t headroom) const;
    // The claim generation identifies a Start incarnation, independently of
    // the caller's context generation. Read both under the recorder mutex.
    CaptureSessionContext Context(uint64_t *claim_generation = nullptr) const;
    uint64_t Revision() const;
    // This boundary begins the supplied guest frame. NextFrame ignores draws
    // until the first boundary after Start and stops at the following boundary.
    void GuestFrameBoundary(uint64_t frame, uint64_t expected_generation = 0);
    bool Mark();
    void Stop();
    void Cancel();
    // Allocation-free failure path for the renderer's C bridge. Pending tokens
    // become stale, but already retained evidence remains inspectable.
    void AbortAllocationFailure(uint64_t expected_generation = 0,
                                uint64_t token = 0) noexcept;
    void Invalidate(uint64_t scope_generation, uint64_t session_epoch,
                    uint64_t renderer_epoch, uint64_t expected_generation = 0);
    uint64_t
    BeginOccurrence(const DrawCaptureSummary &,
                    CaptureEventType = CaptureEventType::Draw,
                    uint32_t limitations = CaptureMissingRawStreams |
                                           CaptureMissingPipelineState |
                                           CaptureMissingDependencies,
                    uint64_t expected_generation = 0,
                    uint64_t expected_scope_generation = 0);
    // Handles identify one recorder incarnation. GPU events remain pending
    // through submission and retirement; proven CPU writes finish immediately.
    // An allocation failure propagates for the guarded C bridge to abort.
    uint64_t BeginBatch(uint64_t expected_generation = 0);
    bool BatchCurrent(uint64_t batch) const;
    bool HoldForBatch(uint64_t batch, uint64_t token);
    bool DescribeCommand(uint64_t token, const CaptureCommandDescription &);
    bool DescribeReplay(uint64_t token,
                        const XemuShaderCaptureReplayDescription &);
    bool RecordBatchCommand(uint64_t batch, uint64_t token, CaptureCommandPhase,
                            uint64_t ordinal);
    bool SubmitBatch(uint64_t batch, bool accepted, uint64_t queue_ordinal,
                     int32_t backend_result = 0);
    bool RetireBatch(uint64_t batch, bool completed,
                     int32_t backend_result = 0);
    bool AbortBatch(uint64_t batch,
                    CaptureBatchOutcome = CaptureBatchOutcome::Aborted,
                    int32_t backend_result = 0);
    bool WantsInputs(uint64_t token) const;
    bool WantsImages(uint64_t token) const;
    bool HasGeometry(uint64_t token) const;
    // Reserve before allocating asynchronous CPU readback buffers. Staging
    // consumes reservations; unused bytes are released by InputsComplete.
    bool ReservePayload(uint64_t token, size_t bytes);
    bool StageGeometry(uint64_t token, const OwnedDrawGeometry &);
    bool StageImage(uint64_t token, bool before, const XemuShaderDrawImage &);
    bool StageTexture(uint64_t token, const XemuShaderDrawTexture &);
    bool StageUniform(uint64_t token, const XemuShaderDrawUniform &);
    bool StageSource(uint64_t token, uint32_t stage, const char *,
                     size_t bytes);
    bool StageRegister(uint64_t token, const char *, uint32_t value);
    bool StageBlob(uint64_t token, const XemuShaderDrawBlob &);
    // Committed synchronously by Finish in command order. Draw writes require
    // emitted=true; readbacks retiring later cannot reorder resource versions.
    bool StageResource(uint64_t token, const XemuShaderCaptureResource &,
                       const char *snapshot_blob = nullptr);
    void ReleaseResource(uint64_t owner, uint64_t byte_size, uint32_t kind,
                         uint32_t flags, uint64_t expected_generation = 0);
    bool AttachResourceReadSnapshot(uint64_t token, uint32_t kind,
                                    uint32_t slot, const char *blob_name);
    bool Finish(uint64_t token, bool emitted, uint32_t primitive_mode,
                uint32_t vertex_count, uint32_t index_count);
    bool InputsComplete(uint64_t token);
    void Fail(uint64_t token, const std::string &reason);
    void BudgetExceeded(uint64_t token, const std::string &reason);
    CaptureSessionSnapshot Snapshot() const;
    bool SnapshotCompletedLiveFrame(uint64_t after_frame,
                                    CaptureSessionSnapshot *out,
                                    uint64_t expected_generation) const;
    std::vector<uint64_t> Uses(const ShaderKey &, uint64_t first_frame = 0,
                               uint64_t last_frame = UINT64_MAX) const;
    std::shared_ptr<const CaptureOccurrence> Find(uint64_t event_id) const;
    bool Save(const std::filesystem::path &, std::string *error = nullptr,
              CaptureFileControl *control = nullptr) const;
    static bool SaveSnapshot(const CaptureSessionSnapshot &,
                             const std::filesystem::path &,
                             std::string *error = nullptr,
                             CaptureFileControl *control = nullptr);
    static bool Reopen(const std::filesystem::path &, CaptureSessionSnapshot *,
                       std::string *error = nullptr,
                       CaptureFileControl *control = nullptr);

private:
    bool StartInternal(const CaptureSessionContext &, const CaptureSessionSettings &,
                        bool idle_only, uint64_t *claim_generation);
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
CaptureSession &GetCaptureSession();
constexpr uint64_t kCaptureSessionTokenBit = UINT64_C(1) << 63;
CaptureResourceLimits
CaptureSessionResourceLimits(const CaptureSessionSettings &);
CaptureResourceGraph
BuildCaptureSessionResourceGraph(const CaptureSessionSnapshot &,
                                 uint64_t byte_budget = 0);

} // namespace xemu::shader_browser

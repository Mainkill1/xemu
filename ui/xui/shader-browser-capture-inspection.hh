// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "shader-browser-capture-session.hh"

namespace xemu::shader_browser {

// Assigned by the archive owner, never inferred from shader/resource contents.
struct CaptureInspectionRecording {
    uint64_t recording_id = 0;
    CaptureSessionContext context;
};
struct CaptureInspectionEventRef {
    uint64_t recording_id = 0, event_id = 0, frame = 0;
    DrawEventKey key;
    bool operator==(const CaptureInspectionEventRef &) const;
};
enum class CaptureInspectionCategory : uint8_t {
    StageSource,
    Uniform,
    RawState,
    VertexInput,
    Texture,
    RasterState,
    EventMetadata
};
// Scalars use canonical little-endian raw bits. Blocks retain exact bytes and
// immutable archive IDs. No float conversion or preview sanitization occurs.
struct CaptureInspectionValue {
    bool present = false;
    SharedCaptureBlock block;
    std::vector<uint8_t> bits;
    uint64_t Size() const;
};
enum class CaptureInspectionChange : uint8_t {
    Changed,
    Added,
    Removed,
    Unchanged,
    Uncompared
};
struct CaptureInspectionField {
    std::string path;
    CaptureInspectionCategory category = CaptureInspectionCategory::RawState;
    CaptureInspectionChange change = CaptureInspectionChange::Unchanged;
    CaptureInspectionValue before, after;
};
enum class CaptureInspectionRelation : uint8_t {
    WithinFrame,
    PriorShaderUse,
    ConfirmedTrack
};
enum class CaptureTrackFrameState : uint8_t {
    Confirmed,
    Candidate,
    Ambiguous,
    Missing,
    Incomplete
};
enum CaptureTrackEvidence : uint32_t {
    CaptureTrackSameGeometry = 1U << 0,
    CaptureTrackSameUniformBits = 1U << 1,
    CaptureTrackSameTextureBits = 1U << 2,
    CaptureTrackSameRasterBits = 1U << 3,
};
struct CaptureTrackCandidate {
    CaptureInspectionEventRef event;
    uint32_t evidence = 0;
    bool evidence_retained = true;
};
struct CaptureTrackFrame {
    uint64_t frame = 0;
    CaptureTrackFrameState state = CaptureTrackFrameState::Missing;
    CaptureInspectionEventRef selected;
    std::vector<CaptureTrackCandidate> candidates;
    std::string note;
    bool evidence_retained = true;
};
struct CaptureTemporalTrack {
    uint64_t track_id = 0, parent_track_id = 0, branch_frame = 0, revision = 0;
    CaptureInspectionRecording recording;
    std::string label;
    std::vector<CaptureTrackFrame> frames;
};
struct CaptureInspectionTrendPoint {
    uint64_t frame = 0;
    CaptureTrackFrameState state = CaptureTrackFrameState::Missing;
    CaptureInspectionEventRef event;
    CaptureInspectionValue value;
    bool comparable_to_previous = false, changed_from_previous = false;
};
enum class CaptureInspectionJobKind : uint8_t { Difference, Candidates, Trend };
enum class CaptureInspectionJobState : uint8_t {
    Running,
    Ready,
    Incomplete,
    Cancelled,
    Failed
};
struct CaptureInspectionSettings {
    uint32_t maximum_events = 1000000, maximum_jobs = 16;
    uint32_t maximum_fields = 8192, maximum_results = 4096;
    uint32_t maximum_tracks = 128, maximum_track_frames = 1024;
    uint32_t maximum_track_candidates = 8192;
    uint32_t maximum_candidates = 256, maximum_page = 128;
    uint64_t comparison_byte_budget = 64U * 1024U * 1024U;
};
struct CaptureInspectionPage {
    uint64_t job_id = 0, track_id = 0, track_revision = 0;
    CaptureInspectionJobKind kind = CaptureInspectionJobKind::Difference;
    CaptureInspectionJobState state = CaptureInspectionJobState::Failed;
    CaptureInspectionRelation relation = CaptureInspectionRelation::WithinFrame;
    CaptureInspectionEventRef before, after;
    uint64_t processed = 0, total = 0, result_count = 0, offset = 0;
    uint64_t compared_bytes = 0, changed_fields = 0, omitted_results = 0;
    bool has_more = false;
    std::string message;
    std::vector<CaptureInspectionField> fields;
    std::vector<CaptureTrackCandidate> candidates;
    std::vector<CaptureInspectionTrendPoint> trend;
};

// CPU-only, bounded work. Results/DTOs retain immutable evidence references;
// callers choose a worker and serialize tracks independently of capture data.
class CaptureInspectionController {
public:
    CaptureInspectionController();
    ~CaptureInspectionController();
    bool Open(uint64_t recording_id, const CaptureSessionSnapshot &,
              const CaptureInspectionSettings & = {});
    // Same recording refresh preserves manual annotations, releases old payload
    // jobs and marks refs whose evidence was evicted. It never pins old events.
    bool Refresh(uint64_t recording_id, const CaptureSessionSnapshot &);
    CaptureInspectionEventRef Reference(uint64_t event_id) const;
    uint64_t BeginWithinFrameComparison(uint64_t before_event,
                                        uint64_t after_event,
                                        bool changed_only = true);
    uint64_t BeginPriorShaderUseComparison(uint64_t event_id, const ShaderKey &,
                                           bool changed_only = true);
    uint64_t BeginConfirmedTrackComparison(uint64_t track_id, uint64_t frame,
                                           bool changed_only = true);
    uint64_t CreateTrack(uint64_t seed_event, const std::string &label,
                         bool user_confirmed);
    bool Confirm(uint64_t track_id, uint64_t event_id, bool user_confirmed);
    // A different confirmed event in the same frame requires an explicit fork.
    uint64_t Branch(uint64_t track_id, uint64_t event_id,
                    const std::string &label, bool user_confirmed);
    uint64_t BeginCandidates(uint64_t track_id, uint64_t frame);
    uint64_t BeginTrend(uint64_t track_id, const std::string &field_path,
                        uint64_t first_frame = 0,
                        uint64_t last_frame = UINT64_MAX);
    void Run(uint64_t job_id, uint32_t maximum_work = 128);
    CaptureInspectionPage Page(uint64_t job_id, uint64_t offset = 0,
                               uint32_t count = 128) const;
    void Cancel(uint64_t job_id);
    void Drop(uint64_t job_id);
    std::vector<CaptureTemporalTrack> Tracks() const;
    // Restores archived user annotations after validating every recording/event
    // reference and all bounds. Candidate evidence never becomes confirmation.
    bool RestoreTracks(const std::vector<CaptureTemporalTrack> &);
    bool ExportAnnotations(std::string *json,
                           std::string *error = nullptr) const;
    bool RestoreAnnotations(const std::string &json,
                            std::string *error = nullptr);
    std::string Error() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace xemu::shader_browser

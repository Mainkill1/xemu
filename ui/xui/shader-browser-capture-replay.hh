// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "shader-browser-capture-replay-description.hh"
#include "shader-browser-capture-session.hh"
#include "shader-browser-preview-model.hh"
#include <map>

namespace xemu::shader_browser {
enum class CaptureReplayStatus : uint8_t {
    Ready,
    Running,
    Completed,
    Unsupported,
    BudgetExceeded,
    Cancelled,
    Failed
};
struct CaptureReplayLimits {
    size_t maximum_steps = 8192, maximum_bindings = 262144;
    uint64_t plan_byte_budget = 64U * 1024U * 1024U;
    uint64_t value_byte_budget = 64U * 1024U * 1024U;
};
struct CaptureReplayVersionKey {
    uint64_t domain = 0, allocation = 0, reset = 0, version = 0;
    CaptureResourceRange range;
    bool operator<(const CaptureReplayVersionKey &) const;
};
struct CaptureReplayBinding {
    XemuShaderCaptureReplayBinding description{};
    uint32_t access_index = 0;
    std::vector<CaptureResourceVersion> versions;
    SharedCaptureBlock checkpoint;
};
struct CaptureReplayStep {
    std::shared_ptr<const CaptureOccurrence> occurrence;
    SharedCaptureReplayDescription description;
    std::vector<CaptureReplayBinding> bindings;
    bool edited_seed = false;
};
struct CaptureReplayPlan {
    CaptureReplayStatus status = CaptureReplayStatus::Unsupported;
    std::string reason;
    std::vector<CaptureReplayStep> steps;
    uint64_t retained_bytes = 0;
    // Raw allocation coverage/gaps remain unchanged. This plan proves only
    // explicitly described canonical logical images and exact buffer ranges.
    bool logical_closure_complete = false;
};
using CaptureReplayDescriptions =
    std::map<uint64_t, SharedCaptureReplayDescription>;
CaptureReplayPlan BuildCaptureReplayPlan(const CaptureSessionSnapshot &,
                                         const CaptureReplayDescriptions &,
                                         const std::vector<uint64_t> &seeds,
                                         const CaptureReplayLimits & = {});
CaptureReplayPlan BuildCaptureReplayPlan(const CaptureSessionSnapshot &,
                                         const std::vector<uint64_t> &seeds,
                                         const CaptureReplayLimits & = {});

struct CaptureReplayValue {
    XemuShaderCaptureReplayBinding description{};
    std::shared_ptr<const std::vector<uint8_t>> bytes;
};
struct CaptureReplayIdentity {
    uint64_t run = 0, token = 0, event_id = 0, sequence = 0;
    uint32_t branch = 0; // 0 original, 1 edited
    bool operator==(const CaptureReplayIdentity &) const;
};
struct CaptureReplayDraw {
    CaptureReplayIdentity identity;
    std::shared_ptr<const CaptureOccurrence> occurrence;
    bool edited_seed = false;
    std::vector<CaptureReplayValue> inputs;
};
// Replaces every described read input, including the color destination.
// Canonical values are never fetched from descendant captured old snapshots.
bool RebindCaptureReplayPacket(const CaptureReplayDraw &, const PreviewPacket &,
                               PreviewPacket *, std::string *error);

// One draw is claimed at a time. The owner uses the existing preview compiler
// and worker, registers its exact result key, then supplies the actual owned
// native result. CPU copies and supported color clears are reconstructed
// canonical operations; no replay GPU copy/clear timing is reported.
// Start incarnations reject stale callbacks.
class CaptureReplaySequence {
public:
    CaptureReplaySequence();
    ~CaptureReplaySequence();
    uint64_t Start(std::shared_ptr<const CaptureReplayPlan>,
                   const CaptureReplayLimits & = {});
    bool TryClaimDraw(CaptureReplayDraw *, std::string *error);
    bool ExpectResult(const CaptureReplayIdentity &, const PreviewResultKey &);
    bool Complete(const CaptureReplayIdentity &, const PreviewResultKey &,
                  OwnedDrawImage, std::string *error);
    void Cancel();
    CaptureReplayStatus Status() const;
    uint64_t RetainedValueBytes() const;
    std::string Reason() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace xemu::shader_browser

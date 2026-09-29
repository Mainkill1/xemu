// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "shader-browser-draw-capture.hh"
#include "shader-browser-draw-inputs.hh"
#include "shader-browser-draw-request.h"

#include <atomic>
#include <memory>
#include <mutex>

namespace xemu::shader_browser {
struct CaptureOccurrence;
struct CaptureSessionContext;

enum class DrawRequestState : uint8_t {
    Idle,
    Armed,
    Capturing,
    Ready,
    Cancelled,
    Failed,
};

struct DrawRequestTarget {
    ShaderKey shader;
    ShaderScope scope;
    uint64_t scope_generation = 0;
    uint64_t session_epoch = 0;
    uint64_t renderer_epoch = 0;
    bool require_geometry = false;
    bool capture_inputs = false;
    XemuShaderCaptureGoal goal = XEMU_SHADER_CAPTURE_ANY_MATCHING_DRAW;
};

struct DrawRequestStatus {
    uint64_t request_id = 0;
    DrawRequestState state = DrawRequestState::Idle;
    DrawEventKey draw;
    uint64_t skipped_draws = 0;
    uint64_t matching_draws = 0;
    std::array<uint64_t, XEMU_SHADER_CAPTURE_REJECT_COUNT> rejection_counts{};
    XemuShaderCaptureRejectReason last_rejection =
        XEMU_SHADER_CAPTURE_REJECT_NONE;
};

struct OwnedDrawGeometry {
    std::vector<std::array<float, 4>> positions;
    std::vector<uint32_t> indices;
};

// One request searches for one usable submitted draw. Begin reserves a matching
// attempt with its own token while
// the renderer still owns its inputs. Completion publishes owned data only;
// cancellation and rearming invalidate the old token.
class DrawCaptureRequest {
public:
    uint64_t Arm(const DrawRequestTarget &target);
    bool Begin(uint64_t scope_generation, uint64_t renderer_epoch,
               const ShaderKey *shaders, size_t shader_count, uint64_t frame,
               uint32_t draw, uint64_t *token, uint64_t submission = 0);
    bool StageGeometry(uint64_t token, OwnedDrawGeometry geometry);
    bool Finish(uint64_t token, bool emitted, uint32_t primitive_mode,
                uint32_t vertex_count, uint32_t index_count);
    bool Complete(uint64_t token, const DrawCaptureSummary &capture,
                  const OwnedDrawGeometry &geometry = {});
    bool WantsInputs(uint64_t token) const;
    bool HasGeometry(uint64_t token) const;
    bool NoteRejection(uint64_t token, XemuShaderCaptureRejectReason reason);
    bool StageImage(uint64_t token, bool before,
                    const XemuShaderDrawImage &image);
    bool StageTexture(uint64_t token, const XemuShaderDrawTexture &texture);
    bool StageUniform(uint64_t token, const XemuShaderDrawUniform &uniform);
    bool StageSource(uint64_t token, uint32_t stage, const char *source,
                     size_t bytes);
    bool StageRegister(uint64_t token, const char *name, uint32_t value);
    bool StageBlob(uint64_t token, const XemuShaderDrawBlob &blob);
    bool InputsComplete(uint64_t token);
    OwnedDrawInputs CopyInputs() const;
    std::shared_ptr<const CaptureOccurrence>
    CopyOccurrence(CaptureSessionContext *context = nullptr) const;
    void Fail(uint64_t token);
    void Cancel();
    bool Armed() const
    {
        return armed_.load(std::memory_order_acquire);
    }
    DrawRequestStatus Status() const;
    DrawRequestTarget Target() const;
    DrawCaptureSummary CopyCaptured() const;
    OwnedDrawGeometry CopyGeometry() const;

private:
    mutable std::mutex mutex_;
    std::atomic<bool> armed_{ false };
    uint64_t next_id_ = 1;
    uint64_t active_token_ = 0;
    bool emitted_ = false;
    XemuShaderCaptureRejectReason candidate_rejection_ =
        XEMU_SHADER_CAPTURE_REJECT_NONE;
    bool NeedsGeometry() const;
    void RejectLocked(XemuShaderCaptureRejectReason reason);
    void CompleteEmittedLocked();
    size_t input_bytes_ = 0;
    OwnedDrawInputs inputs_;
    DrawRequestTarget target_;
    DrawRequestStatus status_;
    DrawCaptureSummary captured_;
    OwnedDrawGeometry geometry_;
};

DrawCaptureRequest &GetDrawCaptureRequest();

} // namespace xemu::shader_browser

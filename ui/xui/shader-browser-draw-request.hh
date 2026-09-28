// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "shader-browser-draw-capture.hh"

#include <atomic>
#include <mutex>

namespace xemu::shader_browser {

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
};

struct DrawRequestStatus {
    uint64_t request_id = 0;
    DrawRequestState state = DrawRequestState::Idle;
    DrawEventKey draw;
    uint64_t skipped_draws = 0;
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
    DrawRequestTarget target_;
    DrawRequestStatus status_;
    DrawCaptureSummary captured_;
    OwnedDrawGeometry geometry_;
};

DrawCaptureRequest &GetDrawCaptureRequest();

} // namespace xemu::shader_browser

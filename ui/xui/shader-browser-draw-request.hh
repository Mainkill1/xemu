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
};

struct DrawRequestStatus {
    uint64_t request_id = 0;
    DrawRequestState state = DrawRequestState::Idle;
    DrawEventKey draw;
};

struct OwnedDrawGeometry {
    std::vector<std::array<float, 4>> positions;
    std::vector<uint32_t> indices;
};

// One request, one final submitted draw. Begin reserves a matching draw while
// the renderer still owns its inputs. Completion publishes owned data only;
// cancellation and rearming invalidate the old token.
class DrawCaptureRequest
{
public:
    uint64_t Arm(const DrawRequestTarget &target);
    bool Wants(uint64_t scope_generation, uint64_t renderer_epoch,
               const ShaderKey *shaders, size_t shader_count) const;
    bool Begin(uint64_t scope_generation, uint64_t renderer_epoch,
               const ShaderKey *shaders, size_t shader_count, uint64_t frame,
               uint32_t draw, uint64_t *token);
    bool Complete(uint64_t token, const DrawCaptureSummary &capture,
                  const OwnedDrawGeometry &geometry = {});
    void Fail(uint64_t token);
    void Cancel();
    bool Armed() const { return armed_.load(std::memory_order_acquire); }
    DrawRequestStatus Status() const;
    DrawRequestTarget Target() const;
    DrawCaptureSummary CopyCaptured() const;
    OwnedDrawGeometry CopyGeometry() const;

private:
    mutable std::mutex mutex_;
    std::atomic<bool> armed_{false};
    uint64_t next_id_ = 1;
    DrawRequestTarget target_;
    DrawRequestStatus status_;
    DrawCaptureSummary captured_;
    OwnedDrawGeometry geometry_;
};

DrawCaptureRequest &GetDrawCaptureRequest();

} // namespace xemu::shader_browser

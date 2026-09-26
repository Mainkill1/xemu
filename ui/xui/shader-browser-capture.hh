// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "shader-browser-capture-bridge.h"

#include <array>
#include <atomic>
#include <mutex>
#include <string>
#include <vector>

namespace xemu::shader_browser {

enum class CaptureReplayClass { Unsupported, Approximate, Complete };
enum CaptureSubstitution : uint32_t {
    CaptureDestinationClear = 1U << 0,
    CaptureDiagnosticTextures = 1U << 1,
    CaptureOutputRgba8 = 1U << 2,
    CaptureBlendOnClear = 1U << 3,
};

enum class CaptureStatusKind { Idle, Armed, Captured, Unsupported, Expired };

struct CaptureStatus {
    CaptureStatusKind kind = CaptureStatusKind::Idle;
    std::string reason;
};

struct CapturedDraw {
    uint32_t version = 1;
    CaptureReplayClass replay_class = CaptureReplayClass::Unsupported;
    uint32_t substitution_flags = 0;
    std::array<std::array<uint8_t, 4>, 4> substitute_texels{};
    std::array<uint8_t, 32> digest{};
    std::string substitution;
    XemuShaderCaptureDraw header{};
    std::array<std::vector<float>, XEMU_SHADER_CAPTURE_MAX_ATTRIBUTES> attributes;
    std::vector<uint8_t> shader_state;
    std::vector<uint8_t> vertex_uniforms;
    std::vector<uint8_t> pixel_uniforms;
    std::string vertex_source;
    std::string geometry_source;
    std::string pixel_source;
};

bool SealCapturedDraw(CapturedDraw *capture, std::string *reason);
bool ValidateCapturedDraw(const CapturedDraw &capture, std::string *reason);

class CaptureStore {
public:
    void Request(const XemuShaderCaptureRequest &request);
    void Cancel();
    bool Armed() const { return armed_.load(std::memory_order_acquire); }
    bool CopyRequest(XemuShaderCaptureRequest *request) const;
    void Expire(uint64_t now_ns);
    bool Submitted(const XemuShaderCaptureDraw &draw, uint64_t now_ns);
    bool Take(CapturedDraw *capture);
    CaptureStatus Status() const;

private:
    mutable std::mutex mutex_;
    std::atomic<bool> armed_{false};
    XemuShaderCaptureRequest request_{};
    CapturedDraw capture_;
    CaptureStatus status_;
    bool has_capture_ = false;
};

CaptureStore &GetCaptureStore();

} // namespace xemu::shader_browser

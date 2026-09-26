// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "shader-browser-capture-bridge.h"
#include "shader-browser-model.hh"
#include "shader-browser-session-provider.hh"

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
    CaptureDiagnosticSamplers = 1U << 4,
};

enum class CaptureStatusKind { Idle, Armed, Captured, Unsupported, Expired };

struct CaptureStatus {
    CaptureStatusKind kind = CaptureStatusKind::Idle;
    std::string reason;
    uint64_t duration_ns = 0;
};

struct CapturedDraw {
    uint32_t version = 1;
    CaptureReplayClass replay_class = CaptureReplayClass::Unsupported;
    uint32_t substitution_flags = 0;
    std::array<std::array<uint8_t, 4>, 4> substitute_texels{};
    std::array<uint8_t, 4> substitute_sampler_kind{}; // 1: 2D nearest/clamp
    std::array<float, 4> substitute_tex_scale{};
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
bool FindCurrentCaptureScope(const Entry &entry,
                             const XemuShaderBrowserScope &live,
                             ShaderScope *scope, std::string *reason);

class CaptureStore {
public:
    void Request(const XemuShaderCaptureRequest &request);
    void Cancel();
    bool Armed() const { return armed_.load(std::memory_order_acquire); }
    uint64_t SampleNonce() const {
        return armed_nonce_.load(std::memory_order_acquire);
    }
    bool CopyRequest(XemuShaderCaptureRequest *request) const;
    void Expire(uint64_t now_ns);
    bool Submitted(const XemuShaderCaptureDraw &draw, uint64_t now_ns);
    bool Take(CapturedDraw *capture);
    CaptureStatus Status() const;
#ifdef XEMU_SHADER_CAPTURE_TESTING
    void HoldLockForTest(std::atomic<bool> *entered, unsigned milliseconds);
#endif

private:
    mutable std::mutex mutex_;
    std::atomic<bool> armed_{false};
    std::atomic<uint64_t> armed_nonce_{0};
    uint64_t next_nonce_ = 0;
    XemuShaderCaptureRequest request_{};
    CapturedDraw capture_;
    CaptureStatus status_;
    bool has_capture_ = false;
};

CaptureStore &GetCaptureStore();

} // namespace xemu::shader_browser

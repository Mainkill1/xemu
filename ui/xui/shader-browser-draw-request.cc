// SPDX-License-Identifier: GPL-2.0-or-later
#include "shader-browser-draw-request.hh"
#include "shader-browser-draw-request.h"

#include <algorithm>
#include <cstring>
#include <utility>

namespace xemu::shader_browser {

uint64_t DrawCaptureRequest::Arm(const DrawRequestTarget &target)
{
    std::lock_guard<std::mutex> lock(mutex_);
    armed_.store(false, std::memory_order_release);
    target_ = target;
    captured_ = {};
    geometry_ = {};
    status_ = {};
    status_.request_id = next_id_++;
    if (!target.scope_generation || !target.session_epoch ||
        !target.renderer_epoch || !target.shader.hash.version ||
        target.shader.stage == Stage::Unknown) {
        status_.state = DrawRequestState::Failed;
        return 0;
    }
    status_.state = DrawRequestState::Armed;
    armed_.store(true, std::memory_order_release);
    return status_.request_id;
}

bool DrawCaptureRequest::Wants(uint64_t scope_generation,
                               uint64_t renderer_epoch,
                               const ShaderKey *shaders,
                               size_t shader_count) const
{
    if (!Armed() || !shaders) return false;
    std::lock_guard<std::mutex> lock(mutex_);
    if (status_.state != DrawRequestState::Armed ||
        scope_generation != target_.scope_generation ||
        renderer_epoch != target_.renderer_epoch) return false;
    for (size_t i = 0; i < shader_count; ++i) {
        if (SameShaderIdentity(shaders[i], target_.shader)) return true;
    }
    return false;
}

bool DrawCaptureRequest::Begin(uint64_t scope_generation,
                               uint64_t renderer_epoch,
                               const ShaderKey *shaders, size_t shader_count,
                               uint64_t frame, uint32_t draw,
                               uint64_t *token)
{
    if (!Armed() || !shaders || !shader_count || !token) return false;
    std::lock_guard<std::mutex> lock(mutex_);
    if (status_.state != DrawRequestState::Armed) return false;
    if (scope_generation != target_.scope_generation ||
        renderer_epoch != target_.renderer_epoch) {
        status_.state = DrawRequestState::Cancelled;
        armed_.store(false, std::memory_order_release);
        return false;
    }
    bool matched = false;
    for (size_t i = 0; i < shader_count; ++i) {
        if (SameShaderIdentity(shaders[i], target_.shader)) {
            matched = true;
            break;
        }
    }
    if (!matched) return false;
    status_.draw = {target_.session_epoch, target_.renderer_epoch, frame, draw};
    status_.state = DrawRequestState::Capturing;
    armed_.store(false, std::memory_order_release);
    *token = status_.request_id;
    return true;
}

bool DrawCaptureRequest::Complete(uint64_t token,
                                  const DrawCaptureSummary &capture,
                                  const OwnedDrawGeometry &geometry)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (status_.state != DrawRequestState::Capturing ||
        token != status_.request_id || capture.key != status_.draw ||
        capture.scope.title_id != target_.scope.title_id ||
        capture.scope.executable_fingerprint_version !=
            target_.scope.executable_fingerprint_version ||
        capture.scope.executable_fingerprint !=
            target_.scope.executable_fingerprint) {
        return false;
    }
    captured_ = capture;
    geometry_ = geometry;
    if (!geometry_.positions.empty() && !geometry_.indices.empty()) {
        captured_.completeness = CaptureCompleteness::GeometrySnapshot;
        captured_.domain = DrawDomain::Geometry;
        captured_.primitive_count = geometry_.indices.size() / 3;
    }
    status_.state = DrawRequestState::Ready;
    return true;
}

void DrawCaptureRequest::Fail(uint64_t token)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (status_.state == DrawRequestState::Capturing &&
        token == status_.request_id) {
        status_.state = DrawRequestState::Failed;
    }
}

void DrawCaptureRequest::Cancel()
{
    std::lock_guard<std::mutex> lock(mutex_);
    armed_.store(false, std::memory_order_release);
    captured_ = {};
    geometry_ = {};
    status_.state = DrawRequestState::Cancelled;
}

DrawRequestStatus DrawCaptureRequest::Status() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return status_;
}

DrawRequestTarget DrawCaptureRequest::Target() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return target_;
}

DrawCaptureSummary DrawCaptureRequest::CopyCaptured() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return status_.state == DrawRequestState::Ready ? captured_ :
        DrawCaptureSummary{};
}

OwnedDrawGeometry DrawCaptureRequest::CopyGeometry() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return status_.state == DrawRequestState::Ready ? geometry_ :
        OwnedDrawGeometry{};
}

DrawCaptureRequest &GetDrawCaptureRequest()
{
    static DrawCaptureRequest request;
    return request;
}

} // namespace xemu::shader_browser

namespace {
xemu::shader_browser::DrawCaptureRequest &draw_request =
    xemu::shader_browser::GetDrawCaptureRequest();

xemu::shader_browser::Stage ConvertStage(uint32_t stage)
{
    using xemu::shader_browser::Stage;
    switch (stage) {
    case XEMU_SHADER_BROWSER_STAGE_VERTEX: return Stage::Vertex;
    case XEMU_SHADER_BROWSER_STAGE_PIXEL: return Stage::Pixel;
    case XEMU_SHADER_BROWSER_STAGE_GEOMETRY: return Stage::Geometry;
    case XEMU_SHADER_BROWSER_STAGE_FIXED_FUNCTION: return Stage::FixedFunction;
    default: return Stage::Unknown;
    }
}
}

extern "C" uint64_t xemu_shader_draw_request_arm(
    const XemuShaderDrawRequestSpec *spec)
{
    if (!spec) return 0;
    xemu::shader_browser::DrawRequestTarget target{};
    target.shader.stage = ConvertStage(spec->stage);
    std::copy_n(spec->identity_hash, target.shader.hash.bytes.size(),
                target.shader.hash.bytes.begin());
    target.scope.title_id = spec->scope.title_id;
    target.scope.executable_fingerprint_version =
        spec->scope.executable_fingerprint_version;
    std::copy_n(spec->scope.executable_fingerprint,
                target.scope.executable_fingerprint.size(),
                target.scope.executable_fingerprint.begin());
    target.scope_generation = spec->scope_generation;
    target.session_epoch = spec->session_epoch;
    target.renderer_epoch = spec->renderer_epoch;
    return draw_request.Arm(target);
}

extern "C" void xemu_shader_draw_request_cancel(void)
{
    draw_request.Cancel();
}

extern "C" int xemu_shader_draw_request_is_armed(void)
{
    return draw_request.Armed() ? 1 : 0;
}

extern "C" int xemu_shader_draw_request_wants(
    uint64_t scope_generation, uint64_t renderer_epoch,
    const XemuShaderDrawIdentity *identities, size_t identity_count)
{
    using namespace xemu::shader_browser;
    if (!draw_request.Armed() || !identities ||
        identity_count > kCapturedShaderSlots) return 0;
    ShaderKey shaders[kCapturedShaderSlots]{};
    for (size_t i = 0; i < identity_count; ++i) {
        shaders[i].stage = ConvertStage(identities[i].stage);
        std::copy_n(identities[i].identity_hash,
                    shaders[i].hash.bytes.size(), shaders[i].hash.bytes.begin());
    }
    return draw_request.Wants(scope_generation, renderer_epoch, shaders,
                              identity_count) ? 1 : 0;
}

extern "C" int xemu_shader_draw_request_copy_status(
    XemuShaderDrawRequestStatus *status)
{
    if (!status) return 0;
    const auto current = draw_request.Status();
    status->request_id = current.request_id;
    status->state = static_cast<XemuShaderDrawRequestState>(current.state);
    status->session_epoch = current.draw.session_epoch;
    status->renderer_epoch = current.draw.renderer_epoch;
    status->frame = current.draw.frame;
    status->draw = current.draw.draw;
    return 1;
}

extern "C" int xemu_shader_draw_request_submitted(
    uint64_t scope_generation, uint64_t renderer_epoch,
    const XemuShaderDrawIdentity *identities, size_t identity_count,
    uint64_t frame, uint32_t draw, uint32_t primitive_mode,
    uint32_t vertex_count, uint32_t index_count,
    const XemuShaderDrawGeometry *geometry)
{
    using namespace xemu::shader_browser;
    if (!draw_request.Armed() || !identities ||
        identity_count > kCapturedShaderSlots) return 0;
    ShaderKey shaders[kCapturedShaderSlots]{};
    for (size_t i = 0; i < identity_count; ++i) {
        shaders[i].stage = ConvertStage(identities[i].stage);
        std::copy_n(identities[i].identity_hash,
                    shaders[i].hash.bytes.size(), shaders[i].hash.bytes.begin());
    }
    uint64_t token = 0;
    if (!draw_request.Begin(scope_generation, renderer_epoch, shaders,
                            identity_count, frame, draw, &token)) return 0;
    const DrawRequestTarget target = draw_request.Target();
    const DrawRequestStatus status = draw_request.Status();
    DrawCaptureSummary capture{};
    capture.key = status.draw;
    capture.scope = target.scope;
    capture.shader_count = static_cast<uint8_t>(identity_count);
    std::copy_n(shaders, identity_count, capture.shaders.begin());
    capture.primitive_mode = primitive_mode;
    capture.vertex_count = vertex_count;
    capture.index_count = index_count;
    capture.completeness = CaptureCompleteness::MetadataOnly;
    OwnedDrawGeometry owned;
    if (geometry) {
        if (!geometry->positions || !geometry->position_count ||
            geometry->position_count > 4096 ||
            (geometry->index_count && !geometry->indices) ||
            geometry->index_count > 12288) {
            draw_request.Fail(token);
            return 0;
        }
        owned.positions.resize(geometry->position_count);
        for (size_t i = 0; i < geometry->position_count; ++i) {
            std::copy_n(geometry->positions + 4 * i, 4,
                        owned.positions[i].begin());
        }
        if (geometry->index_count)
            owned.indices.assign(geometry->indices,
                                 geometry->indices + geometry->index_count);
        for (uint32_t index : owned.indices) {
            if (index >= owned.positions.size()) {
                draw_request.Fail(token);
                return 0;
            }
        }
    }
    return draw_request.Complete(token, capture, owned) ? 1 : 0;
}

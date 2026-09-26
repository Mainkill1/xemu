// SPDX-License-Identifier: GPL-2.0-or-later
#include "shader-browser-capture.hh"

#include <chrono>
#include <cstring>

namespace xemu::shader_browser {
namespace {
bool SameIdentity(const XemuShaderCaptureIdentity &a,
                  const XemuShaderCaptureIdentity &b)
{
    return a.stage == b.stage &&
           std::memcmp(a.hash, b.hash, sizeof(a.hash)) == 0;
}

bool SameRequest(const XemuShaderCaptureRequest &a,
                 const XemuShaderCaptureRequest &b)
{
    return a.title_id == b.title_id &&
           a.fingerprint_version == b.fingerprint_version &&
           std::memcmp(a.fingerprint, b.fingerprint,
                       sizeof(a.fingerprint)) == 0 &&
           SameIdentity(a.selected, b.selected) && a.backend == b.backend &&
           a.session_epoch == b.session_epoch &&
           a.renderer_epoch == b.renderer_epoch;
}

bool CopyBytes(const void *source, size_t size, size_t *budget,
               std::vector<uint8_t> *target)
{
    if (size > *budget || (size && !source)) return false;
    *budget -= size;
    if (!size) {
        target->clear();
        return true;
    }
    const auto *bytes = static_cast<const uint8_t *>(source);
    target->assign(bytes, bytes + size);
    return true;
}

bool CopySource(const char *source, size_t *budget, std::string *target)
{
    if (!source) return true;
    size_t size = 0;
    while (size <= XEMU_SHADER_CAPTURE_MAX_SOURCE && source[size]) ++size;
    if (size > XEMU_SHADER_CAPTURE_MAX_SOURCE || size > *budget) return false;
    *budget -= size;
    target->assign(source, size);
    return true;
}
} // namespace

static std::array<uint8_t, 32> CaptureDigest(const CapturedDraw &capture)
{
    std::array<uint8_t, 32> result{};
    for (unsigned pass = 0; pass < 4; ++pass) {
        uint64_t state = UINT64_C(14695981039346656037) ^
                         (UINT64_C(0x58454d5543415031) + pass);
        auto update = [&state](const void *data, size_t size) {
            const auto *bytes = static_cast<const uint8_t *>(data);
            for (size_t i = 0; i < size; ++i) {
                state ^= bytes[i];
                state *= UINT64_C(1099511628211);
            }
        };
        auto update_size = [&update](size_t size) {
            uint64_t length = size;
            update(&length, sizeof(length));
        };
        const auto &h = capture.header;
        update(&capture.version, sizeof(capture.version));
        update(&capture.substitution_flags, sizeof(capture.substitution_flags));
        update(capture.substitute_texels.data(),
               sizeof(capture.substitute_texels));
        update(&h.context.title_id, sizeof(h.context.title_id));
        update(&h.context.fingerprint_version,
               sizeof(h.context.fingerprint_version));
        update(h.context.fingerprint, sizeof(h.context.fingerprint));
        update(&h.context.selected.stage, sizeof(h.context.selected.stage));
        update(h.context.selected.hash, sizeof(h.context.selected.hash));
        update(&h.context.backend, sizeof(h.context.backend));
        update(&h.context.session_epoch, sizeof(h.context.session_epoch));
        update(&h.context.renderer_epoch, sizeof(h.context.renderer_epoch));
        update(&h.stage_count, sizeof(h.stage_count));
        for (uint32_t i = 0; i < h.stage_count && i < 3; ++i)
            update(&h.stages[i], sizeof(h.stages[i]));
        update(&h.primitive, sizeof(h.primitive));
        update(&h.vertex_count, sizeof(h.vertex_count));
        update(&h.attribute_mask, sizeof(h.attribute_mask));
        update(h.constant_attributes, sizeof(h.constant_attributes));
        update(&h.control_0, sizeof(h.control_0));
        update(&h.control_1, sizeof(h.control_1));
        update(&h.blend, sizeof(h.blend));
        update(&h.blend_color, sizeof(h.blend_color));
        update(&h.setup_raster, sizeof(h.setup_raster));
        update(&h.width, sizeof(h.width));
        update(&h.height, sizeof(h.height));
        update(&h.color_format, sizeof(h.color_format));
        update(&h.viewport_width, sizeof(h.viewport_width));
        update(&h.viewport_height, sizeof(h.viewport_height));
        update(&h.scissor_x, sizeof(h.scissor_x));
        update(&h.scissor_y, sizeof(h.scissor_y));
        update(&h.scissor_width, sizeof(h.scissor_width));
        update(&h.scissor_height, sizeof(h.scissor_height));
        update(&h.texture_mask, sizeof(h.texture_mask));
        update(&h.route, sizeof(h.route));
        for (const auto &attribute : capture.attributes) {
            update_size(attribute.size());
            update(attribute.data(), attribute.size() * sizeof(float));
        }
        update_size(capture.shader_state.size());
        update(capture.shader_state.data(), capture.shader_state.size());
        update_size(capture.vertex_uniforms.size());
        update(capture.vertex_uniforms.data(), capture.vertex_uniforms.size());
        update_size(capture.pixel_uniforms.size());
        update(capture.pixel_uniforms.data(), capture.pixel_uniforms.size());
        update_size(capture.vertex_source.size());
        update(capture.vertex_source.data(), capture.vertex_source.size());
        update_size(capture.geometry_source.size());
        update(capture.geometry_source.data(), capture.geometry_source.size());
        update_size(capture.pixel_source.size());
        update(capture.pixel_source.data(), capture.pixel_source.size());
        update_size(capture.substitution.size());
        update(capture.substitution.data(), capture.substitution.size());
        for (unsigned i = 0; i < 8; ++i)
            result[pass * 8 + i] = static_cast<uint8_t>(state >> (i * 8));
    }
    return result;
}

bool ValidateCapturedDraw(const CapturedDraw &capture, std::string *reason)
{
    auto fail = [reason](const char *message) {
        if (reason) *reason = message;
        return false;
    };
    const auto &h = capture.header;
    if (capture.version != 1 ||
        capture.replay_class == CaptureReplayClass::Unsupported)
        return fail("Unsupported or unknown captured packet version");
    if (h.context.backend != 1 && h.context.backend != 2)
        return fail("Capture backend is unknown");
    if ((h.stage_count != 2 && h.stage_count != 3) ||
        h.stages[1].stage != 2 ||
        (h.stages[0].stage != 1 && h.stages[0].stage != 4))
        return fail("Capture requires exact vertex and pixel stage identities");
    if (h.stage_count == 3 && h.stages[2].stage != 3)
        return fail("Capture geometry stage identity is inconsistent");
    bool selected_present = false;
    for (uint32_t i = 0; i < h.stage_count; ++i)
        selected_present |= SameIdentity(h.context.selected, h.stages[i]);
    if (!selected_present) return fail("Captured stage differs from request");
    if (h.vertex_count == 0 || h.vertex_count > XEMU_SHADER_CAPTURE_MAX_VERTICES ||
        !h.attribute_mask || !h.width || !h.height ||
        h.width > 16384 || h.height > 16384)
        return fail("Capture geometry or surface extent is out of bounds");
    if (!h.viewport_width || !h.viewport_height ||
        !h.scissor_width || !h.scissor_height ||
        h.viewport_width > 16384 || h.viewport_height > 16384 ||
        h.scissor_x > 16384 || h.scissor_y > 16384 ||
        h.scissor_width > 16384 || h.scissor_height > 16384)
        return fail("Viewport or scissor state is missing or out of bounds");
    const bool basic_primitive = h.context.backend == 1 ?
        (h.primitive >= 1 && h.primitive <= 6) :
        (h.primitive >= 1 && h.primitive <= 5);
    const bool geometry_primitive = h.stage_count == 3 &&
        (h.context.backend == 1 ?
            (h.primitive == 10 || h.primitive == 11) :
            (h.primitive == 6 || h.primitive == 7));
    if (!basic_primitive && !geometry_primitive)
        return fail("Draw primitive is not representable by captured stages");
    if (h.route != 1)
        return fail("Active shader route is not the captured specialized source");
    size_t bytes = capture.shader_state.capacity() +
                   capture.vertex_uniforms.capacity() +
                   capture.pixel_uniforms.capacity() +
                   capture.vertex_source.capacity() +
                   capture.geometry_source.capacity() +
                   capture.pixel_source.capacity() +
                   capture.substitution.capacity();
    for (size_t i = 0; i < XEMU_SHADER_CAPTURE_MAX_ATTRIBUTES; ++i) {
        bytes += capture.attributes[i].capacity() * sizeof(float);
        if (h.attributes[i]) return fail("Packet retains a mutable vertex pointer");
        size_t expected = (h.attribute_mask & (1U << i)) ?
                          static_cast<size_t>(h.vertex_count) * 4 : 0;
        if (capture.attributes[i].size() != expected)
            return fail("Captured vertex attribute length is inconsistent");
    }
    if (bytes > 32U * 1024U * 1024U ||
        capture.vertex_source.size() > XEMU_SHADER_CAPTURE_MAX_SOURCE ||
        capture.geometry_source.size() > XEMU_SHADER_CAPTURE_MAX_SOURCE ||
        capture.pixel_source.size() > XEMU_SHADER_CAPTURE_MAX_SOURCE)
        return fail("Captured packet exceeds ownership cap");
    if (h.shader_state || h.vertex_uniforms || h.pixel_uniforms ||
        h.vertex_source || h.geometry_source || h.pixel_source)
        return fail("Packet retains a renderer pointer");
    if (capture.shader_state.empty() || capture.vertex_uniforms.empty() ||
        capture.pixel_uniforms.empty() || capture.vertex_source.empty() ||
        capture.pixel_source.empty())
        return fail("Captured stage source, state or uniforms are missing");
    if ((h.stage_count == 3 && capture.geometry_source.empty()) ||
        (h.stage_count == 2 && !capture.geometry_source.empty()))
        return fail("Captured geometry source does not match stage identity");
    if (h.shader_state_size != capture.shader_state.size() ||
        h.vertex_uniforms_size != capture.vertex_uniforms.size() ||
        h.pixel_uniforms_size != capture.pixel_uniforms.size())
        return fail("Captured state length is inconsistent");
    if (h.texture_mask &&
        !(capture.substitution_flags & CaptureDiagnosticTextures))
        return fail("Texture input is not owned or explicitly substituted");
    for (unsigned i = 0; i < 4; ++i) {
        if ((h.texture_mask & (1U << i)) &&
            capture.substitute_texels[i][3] != 255)
            return fail("Substituted texture texel must be opaque RGBA8");
    }
    if (!(capture.substitution_flags & CaptureDestinationClear))
        return fail("Destination color is not owned or explicitly substituted");
    if (!(capture.substitution_flags & CaptureOutputRgba8))
        return fail("Destination color format is not owned or substituted");
    if (capture.substitution_flags &
        ~(CaptureDestinationClear | CaptureDiagnosticTextures |
          CaptureOutputRgba8 | CaptureBlendOnClear))
        return fail("Unknown replay substitution flag");
    if (h.blend & (1U << 16))
        return fail("Destination logic operation is unsupported");
    if ((h.blend & (1U << 3)) &&
        !(capture.substitution_flags & CaptureBlendOnClear))
        return fail("Blend requires owned or substituted destination color");
    if (h.control_0 & ((1U << 14) | (1U << 12)))
        return fail("Depth or alpha test is unsupported");
    if (h.control_1 & 1U) return fail("Stencil test is unsupported");
    if (h.setup_raster & (1U << 28)) return fail("Culling is unsupported");
    if (capture.replay_class == CaptureReplayClass::Approximate &&
        capture.substitution.empty())
        return fail("Approximate replay needs an exact substitution warning");
    if (capture.replay_class == CaptureReplayClass::Complete)
        return fail("Destination color was not captured for Complete replay");
    if (capture.digest != CaptureDigest(capture))
        return fail("Captured content digest does not match owned bytes");
    if (reason) reason->clear();
    return true;
}

bool SealCapturedDraw(CapturedDraw *capture, std::string *reason)
{
    if (!capture) return false;
    capture->replay_class = CaptureReplayClass::Approximate;
    capture->substitution_flags =
        CaptureDestinationClear | CaptureOutputRgba8;
    capture->substitute_texels = {};
    capture->substitution =
        "Original destination color is GPU-only; replay clears it to "
        "transparent and represents target color format " +
        std::to_string(capture->header.color_format) + " as RGBA8.";
    if (capture->header.texture_mask) {
        capture->substitution_flags |= CaptureDiagnosticTextures;
        constexpr uint8_t colors[4][4] = {
            { 255, 0, 255, 255 }, { 0, 255, 255, 255 },
            { 255, 255, 0, 255 }, { 255, 255, 255, 255 },
        };
        for (unsigned i = 0; i < 4; ++i) {
            if (capture->header.texture_mask & (1U << i)) {
                std::memcpy(capture->substitute_texels[i].data(), colors[i], 4);
                capture->substitution += " GPU-only texture stage " +
                    std::to_string(i) + " uses a constant 1x1 RGBA8 (" +
                    std::to_string(colors[i][0]) + "," +
                    std::to_string(colors[i][1]) + "," +
                    std::to_string(colors[i][2]) + ",255) texel.";
            }
        }
    }
    if (capture->header.blend & (1U << 3)) {
        capture->substitution_flags |= CaptureBlendOnClear;
        capture->substitution +=
            " Destination-dependent blend uses the substituted transparent "
            "RGBA8 clear color and captured blend register/color.";
    }
    if ((capture->header.control_0 & (15U << 26)) != (15U << 26)) {
        capture->substitution +=
            " Untouched color channels retain the substituted transparent "
            "RGBA8 clear color; the captured write mask is preserved.";
    }
    capture->digest = CaptureDigest(*capture);
    if (!ValidateCapturedDraw(*capture, reason)) {
        capture->replay_class = CaptureReplayClass::Unsupported;
        return false;
    }
    return true;
}

void CaptureStore::Request(const XemuShaderCaptureRequest &request)
{
    std::lock_guard<std::mutex> lock(mutex_);
    armed_.store(false, std::memory_order_release);
    request_ = request;
    capture_ = {};
    has_capture_ = false;
    status_ = { CaptureStatusKind::Armed, "Waiting for a matching submitted draw" };
    armed_.store(true, std::memory_order_release);
}

void CaptureStore::Cancel()
{
    std::lock_guard<std::mutex> lock(mutex_);
    armed_.store(false, std::memory_order_release);
    capture_ = {};
    has_capture_ = false;
    status_ = {};
}

bool CaptureStore::CopyRequest(XemuShaderCaptureRequest *request) const
{
    if (!Armed() || !request) return false;
    std::lock_guard<std::mutex> lock(mutex_);
    if (!Armed()) return false;
    *request = request_;
    return true;
}

void CaptureStore::Expire(uint64_t now_ns)
{
    if (!Armed()) return;
    std::lock_guard<std::mutex> lock(mutex_);
    if (Armed() && now_ns > request_.deadline_ns) {
        armed_.store(false, std::memory_order_release);
        status_ = { CaptureStatusKind::Expired, "Capture request timed out" };
    }
}

bool CaptureStore::Submitted(const XemuShaderCaptureDraw &draw,
                             uint64_t now_ns)
{
    if (!Armed()) return false;
    const auto copy_start = std::chrono::steady_clock::now();
    std::lock_guard<std::mutex> lock(mutex_);
    if (!Armed()) return false;
    if (now_ns > request_.deadline_ns) {
        armed_.store(false, std::memory_order_release);
        status_ = { CaptureStatusKind::Expired, "Capture request timed out" };
        return false;
    }
    if (!SameRequest(request_, draw.context)) return false;
    bool selected_present = false;
    if (draw.stage_count > 3) return false;
    for (uint32_t i = 0; i < draw.stage_count; ++i)
        selected_present |= SameIdentity(request_.selected, draw.stages[i]);
    if (!selected_present) return false;

    armed_.store(false, std::memory_order_release);
    auto unsupported = [this](const char *reason) {
        status_ = { CaptureStatusKind::Unsupported, reason };
        return false;
    };
    if (draw.vertex_count == 0 ||
        draw.vertex_count > XEMU_SHADER_CAPTURE_MAX_VERTICES ||
        draw.width == 0 || draw.height == 0)
        return unsupported("Inline vertex count or surface extent exceeds capture limits");
    constexpr uint64_t kMaxCaptureNs = UINT64_C(2000000);
    if (draw.capture_started_ns &&
        (now_ns < draw.capture_started_ns ||
         now_ns - draw.capture_started_ns > kMaxCaptureNs))
        return unsupported("Draw-boundary capture exceeded the 2 ms time cap");

    size_t budget = 1U * 1024U * 1024U;
    CapturedDraw owned{};
    owned.header = draw;
    for (size_t i = 0; i < XEMU_SHADER_CAPTURE_MAX_ATTRIBUTES; ++i) {
        owned.header.attributes[i] = nullptr;
        if (!(draw.attribute_mask & (1U << i))) continue;
        size_t size = static_cast<size_t>(draw.vertex_count) * 4 * sizeof(float);
        if (!draw.attributes[i] || size > budget)
            return unsupported("Inline vertex attributes exceed capture limit");
        budget -= size;
        owned.attributes[i].assign(draw.attributes[i],
                                   draw.attributes[i] + draw.vertex_count * 4);
    }
    if (!CopyBytes(draw.shader_state, draw.shader_state_size, &budget,
                   &owned.shader_state) ||
        !CopyBytes(draw.vertex_uniforms, draw.vertex_uniforms_size, &budget,
                   &owned.vertex_uniforms) ||
        !CopyBytes(draw.pixel_uniforms, draw.pixel_uniforms_size, &budget,
                   &owned.pixel_uniforms) ||
        !CopySource(draw.vertex_source, &budget, &owned.vertex_source) ||
        !CopySource(draw.geometry_source, &budget, &owned.geometry_source) ||
        !CopySource(draw.pixel_source, &budget, &owned.pixel_source))
        return unsupported("Shader state, uniforms or source exceed capture limit");
    owned.header.shader_state = nullptr;
    owned.header.vertex_uniforms = nullptr;
    owned.header.pixel_uniforms = nullptr;
    owned.header.vertex_source = nullptr;
    owned.header.geometry_source = nullptr;
    owned.header.pixel_source = nullptr;
    const auto copied_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now() - copy_start).count();
    const uint64_t pre_copy_ns = draw.capture_started_ns ?
        now_ns - draw.capture_started_ns : 0;
    if (copied_ns < 0 ||
        static_cast<uint64_t>(copied_ns) + pre_copy_ns > kMaxCaptureNs)
        return unsupported("Draw-boundary capture exceeded the 2 ms time cap");
    capture_ = std::move(owned);
    has_capture_ = true;
    status_ = { CaptureStatusKind::Captured, "Submitted draw captured" };
    return true;
}

bool CaptureStore::Take(CapturedDraw *capture)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (!has_capture_ || !capture) return false;
    *capture = std::move(capture_);
    has_capture_ = false;
    return true;
}

CaptureStatus CaptureStore::Status() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return status_;
}

CaptureStore &GetCaptureStore()
{
    static CaptureStore store;
    return store;
}
} // namespace xemu::shader_browser

extern "C" {
void xemu_shader_capture_request(const XemuShaderCaptureRequest *request)
{
    if (request) xemu::shader_browser::GetCaptureStore().Request(*request);
}
void xemu_shader_capture_cancel(void)
{
    xemu::shader_browser::GetCaptureStore().Cancel();
}
int xemu_shader_capture_armed(void)
{
    return xemu::shader_browser::GetCaptureStore().Armed();
}
int xemu_shader_capture_copy_request(XemuShaderCaptureRequest *request)
{
    return xemu::shader_browser::GetCaptureStore().CopyRequest(request);
}
int xemu_shader_capture_submitted(const XemuShaderCaptureDraw *draw,
                                  uint64_t now_ns)
{
    return draw && xemu::shader_browser::GetCaptureStore().Submitted(*draw,
                                                                       now_ns);
}
}

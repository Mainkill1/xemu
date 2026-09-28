// SPDX-License-Identifier: GPL-2.0-or-later
#include "shader-browser-draw-request.hh"
#include "shader-browser-draw-request.h"
#include "shader-browser-capture-session.hh"

#include <algorithm>
#include <cstring>
#include <cmath>
#include <utility>
#include <new>

namespace xemu::shader_browser {

namespace {
XemuShaderCaptureRejectReason
GeometryRejection(const OwnedDrawGeometry &geometry)
{
    if (geometry.positions.empty() || geometry.indices.size() < 3)
        return XEMU_SHADER_CAPTURE_REJECT_EMPTY;
    if (geometry.indices.size() % 3)
        return XEMU_SHADER_CAPTURE_REJECT_INDICES;
    bool nondegenerate = false;
    for (size_t i = 0; i < geometry.indices.size(); i += 3) {
        double edges[2][3];
        for (size_t vertex = 0; vertex < 3; ++vertex) {
            const auto index = geometry.indices[i + vertex];
            if (index >= geometry.positions.size())
                return XEMU_SHADER_CAPTURE_REJECT_INDICES;
            const auto &position = geometry.positions[index];
            for (float value : position)
                if (!std::isfinite(value) || std::abs(value) > 1.0e9f)
                    return XEMU_SHADER_CAPTURE_REJECT_NONFINITE;
            if (vertex)
                for (size_t axis = 0; axis < 3; ++axis)
                    edges[vertex - 1][axis] =
                        double(position[axis]) -
                        geometry.positions[geometry.indices[i]][axis];
        }
        for (size_t axis = 0; axis < 3; ++axis) {
            const auto next = (axis + 1) % 3, last = (axis + 2) % 3;
            if (edges[0][next] * edges[1][last] !=
                edges[0][last] * edges[1][next])
                nondegenerate = true;
        }
    }
    return nondegenerate ? XEMU_SHADER_CAPTURE_REJECT_NONE :
                           XEMU_SHADER_CAPTURE_REJECT_DEGENERATE;
}
} // namespace

bool DrawCaptureRequest::NeedsGeometry() const
{
    return target_.require_geometry ||
           target_.goal == XEMU_SHADER_CAPTURE_PREVIEWABLE_GEOMETRY;
}

void DrawCaptureRequest::RejectLocked(XemuShaderCaptureRejectReason reason)
{
    ++status_.skipped_draws;
    ++status_.rejection_counts[reason];
    status_.last_rejection = reason;
    captured_ = {};
    geometry_ = {};
    inputs_ = {};
    input_bytes_ = 0;
    emitted_ = false;
    status_.draw = {};
    status_.state = DrawRequestState::Armed;
    armed_.store(true, std::memory_order_release);
}

void DrawCaptureRequest::CompleteEmittedLocked()
{
    if (NeedsGeometry()) {
        auto reason = GeometryRejection(geometry_);
        if (reason != XEMU_SHADER_CAPTURE_REJECT_NONE) {
            if (candidate_rejection_ != XEMU_SHADER_CAPTURE_REJECT_NONE)
                reason = candidate_rejection_;
            else if (captured_.primitive_mode < 5 ||
                     captured_.primitive_mode > 10)
                reason = XEMU_SHADER_CAPTURE_REJECT_TOPOLOGY;
            else if (geometry_.positions.empty() &&
                     (captured_.vertex_count || captured_.index_count))
                reason = XEMU_SHADER_CAPTURE_REJECT_POSITION_UNAVAILABLE;
            // A supplied invalid stream already has a specific diagnosis.
            RejectLocked(reason);
            return;
        }
    }
    status_.state = DrawRequestState::Ready;
}

uint64_t DrawCaptureRequest::Arm(const DrawRequestTarget &target)
{
    std::lock_guard<std::mutex> lock(mutex_);
    armed_.store(false, std::memory_order_release);
    target_ = target;
    captured_ = {};
    geometry_ = {};
    inputs_ = {};
    input_bytes_ = 0;
    emitted_ = false;
    candidate_rejection_ = XEMU_SHADER_CAPTURE_REJECT_NONE;
    status_ = {};
    status_.request_id = next_id_++;
    active_token_ = 0;
    if (!target.scope_generation || !target.session_epoch ||
        !target.renderer_epoch || !target.shader.hash.version ||
        target.shader.stage == Stage::Unknown ||
        target.goal < XEMU_SHADER_CAPTURE_ANY_MATCHING_DRAW ||
        target.goal >= XEMU_SHADER_CAPTURE_REPLAYABLE_DRAW) {
        status_.state = DrawRequestState::Failed;
        return 0;
    }
    status_.state = DrawRequestState::Armed;
    armed_.store(true, std::memory_order_release);
    return status_.request_id;
}

bool DrawCaptureRequest::Begin(uint64_t scope_generation,
                               uint64_t renderer_epoch,
                               const ShaderKey *shaders, size_t shader_count,
                               uint64_t frame, uint32_t draw, uint64_t *token,
                               uint64_t submission)
{
    if (!Armed() || !shaders || !shader_count ||
        shader_count > kCapturedShaderSlots || !token)
        return false;
    std::lock_guard<std::mutex> lock(mutex_);
    if (status_.state != DrawRequestState::Armed)
        return false;
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
    if (!matched)
        return false;
    status_.draw = { target_.session_epoch, target_.renderer_epoch, frame, draw,
                     submission };
    captured_ = {};
    geometry_ = {};
    inputs_ = {};
    input_bytes_ = 0;
    emitted_ = false;
    candidate_rejection_ = XEMU_SHADER_CAPTURE_REJECT_NONE;
    ++status_.matching_draws;
    captured_.key = status_.draw;
    captured_.scope = target_.scope;
    captured_.shader_count = static_cast<uint8_t>(shader_count);
    std::copy_n(shaders, shader_count, captured_.shaders.begin());
    status_.state = DrawRequestState::Capturing;
    armed_.store(false, std::memory_order_release);
    active_token_ = active_token_ ? next_id_++ : status_.request_id;
    *token = active_token_;
    return true;
}

bool DrawCaptureRequest::Complete(uint64_t token,
                                  const DrawCaptureSummary &capture,
                                  const OwnedDrawGeometry &geometry)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (status_.state != DrawRequestState::Capturing ||
        token != active_token_ || capture.key != status_.draw ||
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
    CompleteEmittedLocked();
    return true;
}

bool DrawCaptureRequest::StageGeometry(uint64_t token,
                                       OwnedDrawGeometry geometry)
{
    if (geometry.positions.size() > 4096 || geometry.indices.size() > 12288) {
        NoteRejection(token, XEMU_SHADER_CAPTURE_REJECT_BUDGET);
        return false;
    }
    const auto rejection = GeometryRejection(geometry);
    if (rejection != XEMU_SHADER_CAPTURE_REJECT_NONE &&
        !(rejection == XEMU_SHADER_CAPTURE_REJECT_EMPTY &&
          geometry.indices.empty())) {
        NoteRejection(token, rejection);
        return false;
    }
    std::vector<bool> referenced(geometry.positions.size());
    for (uint32_t index : geometry.indices) {
        if (index >= geometry.positions.size())
            return false;
        referenced[index] = true;
    }
    for (size_t i = 0; i < geometry.positions.size(); ++i) {
        auto &position = geometry.positions[i];
        if (!referenced[i]) {
            // Sparse buffer holes are not submitted vertices. Neutralize them
            // so unrelated bytes cannot reject or contaminate the packet.
            position = { 0, 0, 0, 1 };
            continue;
        }
        for (float value : position) {
            if (!std::isfinite(value) || std::abs(value) > 1.0e9f)
                return false;
        }
    }
    std::lock_guard<std::mutex> lock(mutex_);
    if (status_.state != DrawRequestState::Capturing || token != active_token_)
        return false;
    geometry_ = std::move(geometry);
    candidate_rejection_ = XEMU_SHADER_CAPTURE_REJECT_NONE;
    return true;
}

bool DrawCaptureRequest::Finish(uint64_t token, bool emitted,
                                uint32_t primitive_mode, uint32_t vertex_count,
                                uint32_t index_count)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (status_.state != DrawRequestState::Capturing ||
        token != active_token_ || emitted_)
        return false;
    if (!emitted) {
        RejectLocked(XEMU_SHADER_CAPTURE_REJECT_NOT_EMITTED);
        return true;
    }
    captured_.primitive_mode = primitive_mode;
    captured_.vertex_count = vertex_count;
    captured_.index_count = index_count;
    if (!geometry_.positions.empty() && !geometry_.indices.empty()) {
        captured_.completeness = CaptureCompleteness::GeometrySnapshot;
        captured_.domain = DrawDomain::Geometry;
        captured_.primitive_count = geometry_.indices.size() / 3;
    }
    emitted_ = true;
    if (!target_.capture_inputs || inputs_.complete)
        CompleteEmittedLocked();
    return true;
}

bool DrawCaptureRequest::WantsInputs(uint64_t token) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return status_.state == DrawRequestState::Capturing &&
           token == active_token_ && target_.capture_inputs;
}

bool DrawCaptureRequest::HasGeometry(uint64_t token) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return status_.state == DrawRequestState::Capturing &&
           token == active_token_ && !geometry_.positions.empty() &&
           geometry_.indices.size() >= 3 && geometry_.indices.size() % 3 == 0;
}

bool DrawCaptureRequest::NoteRejection(uint64_t token,
                                       XemuShaderCaptureRejectReason reason)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (status_.state != DrawRequestState::Capturing ||
        token != active_token_ || reason <= XEMU_SHADER_CAPTURE_REJECT_NONE ||
        reason >= XEMU_SHADER_CAPTURE_REJECT_COUNT)
        return false;
    candidate_rejection_ = reason;
    return true;
}

namespace {
bool ValidImage(const XemuShaderDrawImage &image)
{
    return image.width && image.height && image.width <= 2048 &&
           image.height <= 2048 && image.rgba &&
           image.byte_count == size_t(image.width) * image.height * 4;
}
size_t BoundedNameLength(const char *name)
{
    if (!name)
        return 0;
    size_t count = 0;
    while (count < 64 && name[count])
        ++count;
    return count < 64 ? count : 0;
}
OwnedDrawImage CopyImage(const XemuShaderDrawImage &image)
{
    return { image.width, image.height,
             std::vector<uint8_t>(image.rgba, image.rgba + image.byte_count) };
}
} // namespace

bool DrawCaptureRequest::StageImage(uint64_t token, bool before,
                                    const XemuShaderDrawImage &image)
{
    if (!ValidImage(image))
        return false;
    std::lock_guard<std::mutex> lock(mutex_);
    if (status_.state != DrawRequestState::Capturing ||
        token != active_token_ || !target_.capture_inputs)
        return false;
    auto &destination = before ? inputs_.before : inputs_.after;
    size_t remaining = input_bytes_ - destination.rgba.size();
    if (image.byte_count > kDrawInputBudget - remaining)
        return false;
    destination = CopyImage(image);
    input_bytes_ = remaining + image.byte_count;
    return true;
}

bool DrawCaptureRequest::StageTexture(uint64_t token,
                                      const XemuShaderDrawTexture &texture)
{
    if (texture.slot >= 4 || texture.width > 4096 || texture.height > 4096 ||
        texture.depth > 4096 || texture.mip_levels > 16 ||
        texture.face_count > 6)
        return false;
    bool has_image = texture.image.byte_count != 0;
    if (has_image && (!texture.bound || !ValidImage(texture.image) ||
                      texture.mip_level >= texture.mip_levels ||
                      texture.face >= texture.face_count))
        return false;
    std::lock_guard<std::mutex> lock(mutex_);
    if (status_.state != DrawRequestState::Capturing ||
        token != active_token_ || !target_.capture_inputs)
        return false;
    auto &owned = inputs_.textures[texture.slot];
    if (has_image) {
        auto old =
            std::find_if(owned.images.begin(), owned.images.end(),
                         [&](const OwnedDrawTextureImage &item) {
                             return item.mip_level == texture.mip_level &&
                                    item.face == texture.face;
                         });
        size_t previous =
            old == owned.images.end() ? 0 : old->image.rgba.size();
        size_t remaining = input_bytes_ - previous;
        if (texture.image.byte_count > kDrawInputBudget - remaining)
            return false;
        OwnedDrawTextureImage copy{ texture.mip_level, texture.face,
                                    CopyImage(texture.image) };
        if (old == owned.images.end())
            owned.images.push_back(std::move(copy));
        else
            *old = std::move(copy);
        input_bytes_ = remaining + texture.image.byte_count;
    }
    owned.described = true;
    owned.metadata = texture;
    owned.metadata.image = {};
    return true;
}

bool DrawCaptureRequest::StageUniform(uint64_t token,
                                      const XemuShaderDrawUniform &uniform)
{
    size_t length = BoundedNameLength(uniform.name);
    if (!length || uniform.stage > 4 ||
        uniform.type < XEMU_SHADER_DRAW_UNIFORM_FLOAT ||
        uniform.type > XEMU_SHADER_DRAW_UNIFORM_MAT4 ||
        (uniform.type <= XEMU_SHADER_DRAW_UNIFORM_UINT &&
         uniform.components > 4) ||
        (uniform.type == XEMU_SHADER_DRAW_UNIFORM_MAT2 &&
         uniform.components != 4) ||
        (uniform.type == XEMU_SHADER_DRAW_UNIFORM_MAT4 &&
         uniform.components != 16) ||
        !uniform.components || uniform.components > 16 || !uniform.count ||
        uniform.count > 4096 || uniform.byte_count > 65536 || !uniform.data ||
        uniform.byte_count != size_t(uniform.components) * uniform.count * 4)
        return false;
    std::lock_guard<std::mutex> lock(mutex_);
    if (status_.state != DrawRequestState::Capturing ||
        token != active_token_ || !target_.capture_inputs)
        return false;
    if (inputs_.uniforms.size() >= 256 ||
        uniform.byte_count > kDrawInputBudget - input_bytes_)
        return false;
    OwnedDrawUniform copy;
    copy.stage = uniform.stage;
    copy.name.assign(uniform.name, length);
    copy.type = uniform.type;
    copy.components = uniform.components;
    copy.count = uniform.count;
    const auto *bytes = static_cast<const uint8_t *>(uniform.data);
    copy.data.assign(bytes, bytes + uniform.byte_count);
    inputs_.uniforms.push_back(std::move(copy));
    input_bytes_ += uniform.byte_count;
    return true;
}

bool DrawCaptureRequest::StageSource(uint64_t token, uint32_t stage,
                                     const char *source, size_t bytes)
{
    if (!stage || stage >= inputs_.sources.size() || !source || !bytes ||
        bytes > 4 * 1024 * 1024 || std::memchr(source, 0, bytes))
        return false;
    std::lock_guard<std::mutex> lock(mutex_);
    if (status_.state != DrawRequestState::Capturing ||
        token != active_token_ || !target_.capture_inputs)
        return false;
    size_t remaining = input_bytes_ - inputs_.sources[stage].size();
    if (bytes > kDrawInputBudget - remaining)
        return false;
    inputs_.sources[stage].assign(source, bytes);
    input_bytes_ = remaining + bytes;
    return true;
}

bool DrawCaptureRequest::StageRegister(uint64_t token, const char *name,
                                       uint32_t value)
{
    size_t length = BoundedNameLength(name);
    if (!length)
        return false;
    std::lock_guard<std::mutex> lock(mutex_);
    if (status_.state != DrawRequestState::Capturing ||
        token != active_token_ || !target_.capture_inputs ||
        inputs_.registers.size() >= 256)
        return false;
    inputs_.registers.push_back({ std::string(name, length), value });
    return true;
}

bool DrawCaptureRequest::StageBlob(uint64_t token,
                                   const XemuShaderDrawBlob &blob)
{
    const size_t length = BoundedNameLength(blob.name);
    if (!length || !blob.data || !blob.byte_count ||
        blob.byte_count > 16U * 1024U * 1024U)
        return false;
    std::lock_guard<std::mutex> lock(mutex_);
    if (status_.state != DrawRequestState::Capturing ||
        token != active_token_ || !target_.capture_inputs ||
        inputs_.blobs.size() >= 128 ||
        blob.byte_count + length > kDrawInputBudget - input_bytes_)
        return false;
    OwnedDrawBlob owned{};
    owned.name.assign(blob.name, length);
    const auto *bytes = static_cast<const uint8_t *>(blob.data);
    owned.bytes.assign(bytes, bytes + blob.byte_count);
    owned.slot = blob.slot;
    owned.format = blob.format;
    owned.components = blob.components;
    owned.stride = blob.stride;
    owned.count = blob.count;
    owned.offset = blob.offset;
    owned.normalized = blob.normalized;
    owned.integer = blob.integer;
    inputs_.blobs.push_back(std::move(owned));
    input_bytes_ += blob.byte_count + length;
    return true;
}

bool DrawCaptureRequest::InputsComplete(uint64_t token)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (status_.state != DrawRequestState::Capturing ||
        token != active_token_ || !target_.capture_inputs)
        return false;
    inputs_.complete = true;
    if (emitted_)
        CompleteEmittedLocked();
    return true;
}

OwnedDrawInputs DrawCaptureRequest::CopyInputs() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return status_.state == DrawRequestState::Ready ? inputs_ :
                                                      OwnedDrawInputs{};
}

void DrawCaptureRequest::Fail(uint64_t token)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (status_.state == DrawRequestState::Capturing &&
        token == active_token_) {
        status_.state = DrawRequestState::Failed;
    }
}

void DrawCaptureRequest::Cancel()
{
    std::lock_guard<std::mutex> lock(mutex_);
    armed_.store(false, std::memory_order_release);
    captured_ = {};
    geometry_ = {};
    inputs_ = {};
    input_bytes_ = 0;
    emitted_ = false;
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
    case XEMU_SHADER_BROWSER_STAGE_VERTEX:
        return Stage::Vertex;
    case XEMU_SHADER_BROWSER_STAGE_PIXEL:
        return Stage::Pixel;
    case XEMU_SHADER_BROWSER_STAGE_GEOMETRY:
        return Stage::Geometry;
    case XEMU_SHADER_BROWSER_STAGE_FIXED_FUNCTION:
        return Stage::FixedFunction;
    default:
        return Stage::Unknown;
    }
}
} // namespace

namespace {
bool SessionToken(uint64_t token)
{
    return (token & xemu::shader_browser::kCaptureSessionTokenBit) != 0;
}
void CaptureAllocationFailed(uint64_t token)
{
    if (SessionToken(token)) {
        auto &session = xemu::shader_browser::GetCaptureSession();
        session.AbortAllocationFailure(0, token);
    } else {
        draw_request.Fail(token);
    }
}
} // namespace

extern "C" uint64_t
xemu_shader_draw_request_arm(const XemuShaderDrawRequestSpec *spec)
{
    if (!spec)
        return 0;
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
    target.require_geometry = spec->require_geometry != 0;
    target.capture_inputs = spec->capture_inputs != 0;
    target.goal = spec->goal;
    return draw_request.Arm(target);
}

extern "C" void xemu_shader_draw_request_cancel(void)
{
    draw_request.Cancel();
}

extern "C" int xemu_shader_draw_request_is_armed(void)
{
    return draw_request.Armed() ||
           xemu::shader_browser::GetCaptureSession().Active();
}

extern "C" uint64_t xemu_shader_draw_request_claim(
    uint64_t scope_generation, uint64_t renderer_epoch,
    const XemuShaderDrawIdentity *identities, size_t identity_count,
    uint64_t frame, uint32_t draw, uint64_t submission)
{
    using namespace xemu::shader_browser;
    if ((!draw_request.Armed() && !GetCaptureSession().Active()) ||
        !identities || identity_count > kCapturedShaderSlots)
        return 0;
    ShaderKey shaders[kCapturedShaderSlots]{};
    for (size_t i = 0; i < identity_count; ++i) {
        shaders[i].stage = ConvertStage(identities[i].stage);
        std::copy_n(identities[i].identity_hash, shaders[i].hash.bytes.size(),
                    shaders[i].hash.bytes.begin());
    }
    auto &session = GetCaptureSession();
    if (session.Active()) {
        uint64_t claim_generation = 0;
        try {
            const auto context = session.Context(&claim_generation);
            DrawCaptureSummary summary{};
            summary.scope = context.scope;
            summary.key = { context.session_epoch, renderer_epoch, frame, draw,
                            submission };
            summary.shader_count = static_cast<uint8_t>(identity_count);
            std::copy_n(shaders, identity_count, summary.shaders.begin());
            return session.BeginOccurrence(
                summary, CaptureEventType::Draw,
                CaptureMissingRawStreams | CaptureMissingPipelineState |
                    CaptureMissingDependencies,
                claim_generation, scope_generation);
        } catch (const std::bad_alloc &) {
            session.AbortAllocationFailure(claim_generation);
            return 0;
        }
    }
    uint64_t token = 0;
    return draw_request.Begin(scope_generation, renderer_epoch, shaders,
                              identity_count, frame, draw, &token, submission) ?
               token :
               0;
}

extern "C" int
xemu_shader_draw_request_copy_status(XemuShaderDrawRequestStatus *status)
{
    if (!status)
        return 0;
    const auto current = draw_request.Status();
    status->request_id = current.request_id;
    status->state = static_cast<XemuShaderDrawRequestState>(current.state);
    status->session_epoch = current.draw.session_epoch;
    status->renderer_epoch = current.draw.renderer_epoch;
    status->frame = current.draw.frame;
    status->draw = current.draw.draw;
    status->submission = current.draw.submission;
    status->skipped_draws = current.skipped_draws;
    status->matching_draws = current.matching_draws;
    std::copy(current.rejection_counts.begin(), current.rejection_counts.end(),
              status->rejection_counts);
    status->last_rejection = current.last_rejection;
    return 1;
}

extern "C" int
xemu_shader_draw_request_note_rejection(uint64_t token,
                                        XemuShaderCaptureRejectReason reason)
{
    if (SessionToken(token))
        return 0;
    return draw_request.NoteRejection(token, reason);
}

extern "C" int
xemu_shader_draw_request_stage_geometry(uint64_t token,
                                        const XemuShaderDrawGeometry *geometry)
{
    try {
        using namespace xemu::shader_browser;
        if (!geometry || !geometry->positions || !geometry->position_count ||
            geometry->position_count > 4096 ||
            (geometry->index_count && !geometry->indices) ||
            geometry->index_count > 12288) {
            if (!SessionToken(token))
                draw_request.NoteRejection(
                    token,
                    geometry && (geometry->position_count > 4096 ||
                                 geometry->index_count > 12288) ?
                        XEMU_SHADER_CAPTURE_REJECT_BUDGET :
                    geometry && geometry->index_count && !geometry->indices ?
                        XEMU_SHADER_CAPTURE_REJECT_INDICES :
                        XEMU_SHADER_CAPTURE_REJECT_POSITION_UNAVAILABLE);
            return 0;
        }
        OwnedDrawGeometry owned;
        owned.positions.resize(geometry->position_count);
        for (size_t i = 0; i < geometry->position_count; ++i) {
            std::copy_n(geometry->positions + 4 * i, 4,
                        owned.positions[i].begin());
        }
        if (geometry->index_count)
            owned.indices.assign(geometry->indices,
                                 geometry->indices + geometry->index_count);
        return SessionToken(token) ?
                   GetCaptureSession().StageGeometry(token, owned) :
                   draw_request.StageGeometry(token, std::move(owned));

    } catch (const std::bad_alloc &) {
        CaptureAllocationFailed(token);
        return 0;
    }
}

extern "C" int xemu_shader_draw_request_finish(uint64_t token, int emitted,
                                               uint32_t primitive_mode,
                                               uint32_t vertex_count,
                                               uint32_t index_count)
{
    try {
        if (SessionToken(token))
            return xemu::shader_browser::GetCaptureSession().Finish(
                token, emitted != 0, primitive_mode, vertex_count, index_count);
        return draw_request.Finish(token, emitted != 0, primitive_mode,
                                   vertex_count, index_count) ?
                   1 :
                   0;
    } catch (const std::bad_alloc &) {
        CaptureAllocationFailed(token);
        return 0;
    }
}

extern "C" int xemu_shader_draw_request_wants_inputs(uint64_t token)
{
    return (SessionToken(token) ?
                xemu::shader_browser::GetCaptureSession().WantsInputs(token) :
                draw_request.WantsInputs(token));
}
extern "C" int xemu_shader_draw_request_has_geometry(uint64_t token)
{
    return (SessionToken(token) ?
                xemu::shader_browser::GetCaptureSession().HasGeometry(token) :
                draw_request.HasGeometry(token));
}
extern "C" int
xemu_shader_draw_request_stage_image(uint64_t token, int before,
                                     const XemuShaderDrawImage *image)
{
    try {
        return image &&
               (SessionToken(token) ?
                    xemu::shader_browser::GetCaptureSession().StageImage(
                        token, before != 0, *image) :
                    draw_request.StageImage(token, before != 0, *image));
    } catch (const std::bad_alloc &) {
        CaptureAllocationFailed(token);
        return 0;
    }
}
extern "C" int
xemu_shader_draw_request_stage_texture(uint64_t token,
                                       const XemuShaderDrawTexture *texture)
{
    try {
        return texture &&
               (SessionToken(token) ?
                    xemu::shader_browser::GetCaptureSession().StageTexture(
                        token, *texture) :
                    draw_request.StageTexture(token, *texture));
    } catch (const std::bad_alloc &) {
        CaptureAllocationFailed(token);
        return 0;
    }
}
extern "C" int
xemu_shader_draw_request_stage_uniform(uint64_t token,
                                       const XemuShaderDrawUniform *uniform)
{
    try {
        return uniform &&
               (SessionToken(token) ?
                    xemu::shader_browser::GetCaptureSession().StageUniform(
                        token, *uniform) :
                    draw_request.StageUniform(token, *uniform));
    } catch (const std::bad_alloc &) {
        CaptureAllocationFailed(token);
        return 0;
    }
}
extern "C" int xemu_shader_draw_request_stage_source(uint64_t token,
                                                     uint32_t stage,
                                                     const char *source,
                                                     size_t bytes)
{
    try {
        return (SessionToken(token) ?
                    xemu::shader_browser::GetCaptureSession().StageSource(
                        token, stage, source, bytes) :
                    draw_request.StageSource(token, stage, source, bytes));
    } catch (const std::bad_alloc &) {
        CaptureAllocationFailed(token);
        return 0;
    }
}
extern "C" int xemu_shader_draw_request_stage_register(uint64_t token,
                                                       const char *name,
                                                       uint32_t value)
{
    try {
        return (SessionToken(token) ?
                    xemu::shader_browser::GetCaptureSession().StageRegister(
                        token, name, value) :
                    draw_request.StageRegister(token, name, value));
    } catch (const std::bad_alloc &) {
        CaptureAllocationFailed(token);
        return 0;
    }
}
extern "C" int xemu_shader_draw_request_inputs_complete(uint64_t token)
{
    try {
        return (SessionToken(token) ?
                    xemu::shader_browser::GetCaptureSession().InputsComplete(
                        token) :
                    draw_request.InputsComplete(token));
    } catch (const std::bad_alloc &) {
        CaptureAllocationFailed(token);
        return 0;
    }
}

extern "C" int
xemu_shader_draw_request_stage_blob(uint64_t token,
                                    const XemuShaderDrawBlob *blob)
{
    try {
        return blob && (SessionToken(token) ?
                            xemu::shader_browser::GetCaptureSession().StageBlob(
                                token, *blob) :
                            draw_request.StageBlob(token, *blob));
    } catch (const std::bad_alloc &) {
        CaptureAllocationFailed(token);
        return 0;
    }
}

extern "C" int xemu_shader_capture_session_token(uint64_t token)
{
    return SessionToken(token);
}
extern "C" int xemu_shader_capture_session_snapshots(uint64_t token)
{
    return SessionToken(token) ?
               xemu::shader_browser::GetCaptureSession().WantsImages(token) :
               1;
}
extern "C" void xemu_shader_capture_session_fail_budget(uint64_t token,
                                                        const char *reason)
{
    try {
        if (SessionToken(token))
            xemu::shader_browser::GetCaptureSession().BudgetExceeded(
                token, reason ? reason : "Capture staging budget exceeded");
        else
            draw_request.Fail(token);
    } catch (const std::bad_alloc &) {
        CaptureAllocationFailed(token);
    }
}
extern "C" int xemu_shader_capture_session_reserve(uint64_t token, size_t bytes)
{
    try {
        return !SessionToken(token) ||
               xemu::shader_browser::GetCaptureSession().ReservePayload(token,
                                                                        bytes);
    } catch (const std::bad_alloc &) {
        CaptureAllocationFailed(token);
        return 0;
    }
}
extern "C" void xemu_shader_capture_session_fail(uint64_t token,
                                                  const char *reason)
{
    try {
        xemu::shader_browser::GetCaptureSession().Fail(
            token, reason ? reason : "Native capture command admission failed");
    } catch (const std::bad_alloc &) {
        CaptureAllocationFailed(token);
    }
}
extern "C" void xemu_shader_capture_session_frame(uint64_t frame,
                                                  uint64_t scope_generation,
                                                  uint64_t renderer_epoch)
{
    uint64_t claim_generation = 0;
    try {
        auto &session = xemu::shader_browser::GetCaptureSession();
        if (!session.Active())
            return;
        const auto context = session.Context(&claim_generation);
        session.Invalidate(scope_generation, context.session_epoch,
                           renderer_epoch, claim_generation);
        session.GuestFrameBoundary(frame, claim_generation);
    } catch (const std::bad_alloc &) {
        xemu::shader_browser::GetCaptureSession().AbortAllocationFailure(
            claim_generation);
    }
}
extern "C" void
xemu_shader_capture_session_invalidate(uint64_t scope_generation,
                                       uint64_t renderer_epoch)
{
    uint64_t claim_generation = 0;
    try {
        auto &session = xemu::shader_browser::GetCaptureSession();
        const auto context = session.Context(&claim_generation);
        session.Invalidate(scope_generation, context.session_epoch,
                           renderer_epoch, claim_generation);
    } catch (const std::bad_alloc &) {
        xemu::shader_browser::GetCaptureSession().AbortAllocationFailure(
            claim_generation);
    }
}

extern "C" int xemu_shader_capture_session_active(void)
{
    return xemu::shader_browser::GetCaptureSession().Active();
}

extern "C" uint64_t xemu_shader_capture_session_begin_event(
    uint32_t kind, uint64_t frame, uint32_t draw, uint64_t submission,
    uint64_t scope_generation, uint64_t renderer_epoch)
{
    using namespace xemu::shader_browser;
    auto &session = GetCaptureSession();
    if (!session.Active() || kind > XEMU_SHADER_CAPTURE_SAVE_STATE)
        return 0;
    uint64_t claim_generation = 0;
    try {
        const auto context = session.Context(&claim_generation);
        DrawCaptureSummary summary{};
        summary.scope = context.scope;
        summary.key = { context.session_epoch, renderer_epoch, frame, draw,
                        submission };
        return session.BeginOccurrence(
            summary, static_cast<CaptureEventType>(kind),
            CaptureMissingRawStreams | CaptureMissingPipelineState |
                CaptureMissingDependencies,
            claim_generation, scope_generation);
    } catch (const std::bad_alloc &) {
        session.AbortAllocationFailure(claim_generation);
        return 0;
    }
}

namespace {
template <typename Result, typename Call> Result CaptureBatchCall(Call call)
{
    uint64_t generation = 0;
    auto &session = xemu::shader_browser::GetCaptureSession();
    try {
        session.Context(&generation);
        return call(session, generation);
    } catch (const std::bad_alloc &) {
        session.AbortAllocationFailure(generation);
        return Result{};
    }
}
} // namespace

extern "C" uint64_t
xemu_shader_capture_session_batch_begin(uint64_t scope_generation,
                                        uint64_t renderer_epoch)
{
    return CaptureBatchCall<uint64_t>([&](auto &session, uint64_t generation) {
        const auto context = session.Context();
        session.Invalidate(scope_generation, context.session_epoch,
                           renderer_epoch, generation);
        return session.BeginBatch(generation);
    });
}
extern "C" int xemu_shader_capture_session_batch_current(uint64_t batch)
{
    return CaptureBatchCall<int>(
        [&](auto &session, uint64_t) { return session.BatchCurrent(batch); });
}
extern "C" int xemu_shader_capture_session_batch_hold(uint64_t batch,
                                                      uint64_t token)
{
    return CaptureBatchCall<int>([&](auto &session, uint64_t) {
        return session.HoldForBatch(batch, token);
    });
}
extern "C" int xemu_shader_capture_session_batch_record(uint64_t batch,
                                                        uint64_t token,
                                                        uint32_t phase,
                                                        uint64_t ordinal)
{
    if (phase > XEMU_SHADER_CAPTURE_MAIN)
        return 0;
    return CaptureBatchCall<int>([&](auto &session, uint64_t) {
        return session.RecordBatchCommand(
            batch, token,
            static_cast<xemu::shader_browser::CaptureCommandPhase>(phase),
            ordinal);
    });
}
extern "C" int xemu_shader_capture_session_batch_submit(uint64_t batch,
                                                        int accepted,
                                                        uint64_t queue_ordinal,
                                                        int32_t backend_result)
{
    return CaptureBatchCall<int>([&](auto &session, uint64_t) {
        return session.SubmitBatch(batch, accepted != 0, queue_ordinal,
                                   backend_result);
    });
}
extern "C" int xemu_shader_capture_session_batch_retire(uint64_t batch,
                                                        int completed,
                                                        int32_t backend_result)
{
    return CaptureBatchCall<int>([&](auto &session, uint64_t) {
        return session.RetireBatch(batch, completed != 0, backend_result);
    });
}
extern "C" int xemu_shader_capture_session_batch_abort(uint64_t batch,
                                                       uint32_t outcome,
                                                       int32_t backend_result)
{
    if (outcome < XEMU_SHADER_CAPTURE_BATCH_SUBMISSION_FAILED ||
        outcome > XEMU_SHADER_CAPTURE_BATCH_DETACHED)
        return 0;
    return CaptureBatchCall<int>([&](auto &session, uint64_t) {
        return session.AbortBatch(
            batch,
            static_cast<xemu::shader_browser::CaptureBatchOutcome>(outcome),
            backend_result);
    });
}
extern "C" int xemu_shader_capture_session_describe_command(
    uint64_t token, uint32_t kind, uint64_t source_offset,
    uint64_t destination_offset, uint64_t bytes)
{
    if (kind > XEMU_SHADER_CAPTURE_COMMAND_CHECKPOINT)
        return 0;
    return CaptureBatchCall<int>([&](auto &session, uint64_t) {
        xemu::shader_browser::CaptureCommandDescription description{};
        description.kind =
            static_cast<xemu::shader_browser::CaptureCommandKind>(kind);
        description.source_offset = source_offset;
        description.destination_offset = destination_offset;
        description.bytes = bytes;
        return session.DescribeCommand(token, description);
    });
}

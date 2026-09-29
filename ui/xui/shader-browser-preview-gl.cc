// SPDX-License-Identifier: GPL-2.0-or-later
#include "shader-browser-preview-gl.hh"
#include "shader-browser-preview-gl-channel.hh"

#include "shader-browser-preview-adapter.hh"
#include "shader-browser-preview-alpha.hh"
#include "shader-browser-preview-service.hh"
#include "shader-browser-preview-vk.hh"

#include <SDL3/SDL.h>
#include <epoxy/gl.h>
#include <imgui.h>
#include <glib.h>
#include "shader-browser-workbench-scene-ui.inc"

#include <array>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <future>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

namespace xemu::shader_browser {
namespace {

uint64_t NowNs()
{
    return static_cast<uint64_t>(g_get_monotonic_time()) * UINT64_C(1000);
}

bool Compile(GLenum type, const std::string &source, GLuint *shader,
             std::string *error)
{
    *shader = glCreateShader(type);
    if (!*shader) {
        *error = "Preview GL shader allocation failed";
        return false;
    }
    const char *text = source.c_str();
    glShaderSource(*shader, 1, &text, nullptr);
    glCompileShader(*shader);
    GLint compiled = GL_FALSE;
    glGetShaderiv(*shader, GL_COMPILE_STATUS, &compiled);
    if (compiled == GL_TRUE) return true;
    std::array<char, 1024> log{};
    glGetShaderInfoLog(*shader, static_cast<GLsizei>(log.size()), nullptr,
                       log.data());
    *error = std::string("Preview GL shader compilation failed: ") + log.data();
    glDeleteShader(*shader);
    *shader = 0;
    return false;
}

using Vertex = PreviewSceneVertex;

bool IsSyntheticUniformType(GLenum type)
{
    switch (type) {
    case GL_FLOAT: case GL_FLOAT_VEC2: case GL_FLOAT_VEC3: case GL_FLOAT_VEC4:
    case GL_INT: case GL_INT_VEC2: case GL_INT_VEC3: case GL_INT_VEC4:
    case GL_UNSIGNED_INT: case GL_UNSIGNED_INT_VEC2:
    case GL_UNSIGNED_INT_VEC3: case GL_UNSIGNED_INT_VEC4:
    case GL_BOOL: case GL_BOOL_VEC2: case GL_BOOL_VEC3: case GL_BOOL_VEC4:
    case GL_FLOAT_MAT2: case GL_FLOAT_MAT3: case GL_FLOAT_MAT4:
    case GL_FLOAT_MAT2x3: case GL_FLOAT_MAT2x4:
    case GL_FLOAT_MAT3x2: case GL_FLOAT_MAT3x4:
    case GL_FLOAT_MAT4x2: case GL_FLOAT_MAT4x3:
        return true;
    default:
        return false;
    }
}

GLenum CapturedGlUniformType(const OwnedDrawUniform &uniform)
{
    static const GLenum floats[] = { 0, GL_FLOAT, GL_FLOAT_VEC2, GL_FLOAT_VEC3,
                                     GL_FLOAT_VEC4 };
    static const GLenum integers[] = { 0, GL_INT, GL_INT_VEC2, GL_INT_VEC3,
                                       GL_INT_VEC4 };
    static const GLenum uints[] = { 0, GL_UNSIGNED_INT, GL_UNSIGNED_INT_VEC2,
                                    GL_UNSIGNED_INT_VEC3,
                                    GL_UNSIGNED_INT_VEC4 };
    if (uniform.type == XEMU_SHADER_DRAW_UNIFORM_MAT2)
        return GL_FLOAT_MAT2;
    if (uniform.type == XEMU_SHADER_DRAW_UNIFORM_MAT4)
        return GL_FLOAT_MAT4;
    if (uniform.components > 4)
        return 0;
    if (uniform.type == XEMU_SHADER_DRAW_UNIFORM_FLOAT)
        return floats[uniform.components];
    if (uniform.type == XEMU_SHADER_DRAW_UNIFORM_INT)
        return integers[uniform.components];
    if (uniform.type == XEMU_SHADER_DRAW_UNIFORM_UINT)
        return uints[uniform.components];
    return 0;
}

size_t ApplyCapturedGlUniforms(GLuint program,
                               const std::vector<OwnedDrawUniform> &uniforms,
                               bool original_pipeline = false)
{
    size_t unapplied = 0;
    for (const auto &uniform : uniforms) {
        if (!original_pipeline && !PreviewCapturedUniformAllowed(uniform))
            continue;
        const char *name = uniform.name.c_str();
        GLuint index = GL_INVALID_INDEX;
        glGetUniformIndices(program, 1, &name, &index);
        GLint location = glGetUniformLocation(program, name);
        if (index == GL_INVALID_INDEX || location < 0) {
            ++unapplied;
            continue;
        }
        GLint type, count;
        glGetActiveUniformsiv(program, 1, &index, GL_UNIFORM_TYPE, &type);
        glGetActiveUniformsiv(program, 1, &index, GL_UNIFORM_SIZE, &count);
        GLenum expected = CapturedGlUniformType(uniform);
        if (!expected || expected != GLenum(type) || count <= 0) {
            ++unapplied;
            continue;
        }
        if (uniform.count > uint32_t(count))
            ++unapplied;
        count = std::min(uint32_t(count), uniform.count);
        const auto *f = reinterpret_cast<const GLfloat *>(uniform.data.data());
        const auto *i = reinterpret_cast<const GLint *>(uniform.data.data());
        const auto *u = reinterpret_cast<const GLuint *>(uniform.data.data());
        if (uniform.type == XEMU_SHADER_DRAW_UNIFORM_MAT2)
            glUniformMatrix2fv(location, count, GL_FALSE, f);
        else if (uniform.type == XEMU_SHADER_DRAW_UNIFORM_MAT4)
            glUniformMatrix4fv(location, count, GL_FALSE, f);
        else if (uniform.type == XEMU_SHADER_DRAW_UNIFORM_FLOAT) {
            switch (uniform.components) {
            case 1:
                glUniform1fv(location, count, f);
                break;
            case 2:
                glUniform2fv(location, count, f);
                break;
            case 3:
                glUniform3fv(location, count, f);
                break;
            case 4:
                glUniform4fv(location, count, f);
                break;
            }
        } else if (uniform.type == XEMU_SHADER_DRAW_UNIFORM_INT) {
            switch (uniform.components) {
            case 1:
                glUniform1iv(location, count, i);
                break;
            case 2:
                glUniform2iv(location, count, i);
                break;
            case 3:
                glUniform3iv(location, count, i);
                break;
            case 4:
                glUniform4iv(location, count, i);
                break;
            }
        } else {
            switch (uniform.components) {
            case 1:
                glUniform1uiv(location, count, u);
                break;
            case 2:
                glUniform2uiv(location, count, u);
                break;
            case 3:
                glUniform3uiv(location, count, u);
                break;
            case 4:
                glUniform4uiv(location, count, u);
                break;
            }
        }
    }
    return unapplied;
}

GLint CapturedGlWrap(uint32_t wrap)
{
    return wrap == GL_REPEAT || wrap == GL_MIRRORED_REPEAT ||
                   wrap == GL_CLAMP_TO_EDGE ?
               GLint(wrap) :
               GL_CLAMP_TO_EDGE;
}

bool CheckSyntheticUniformInterface(
    GLuint program, std::string *error,
    const PreviewCapturedPipeline *pipeline = nullptr)
{
    GLint count = 0;
    GLint max_name = 0;
    glGetProgramiv(program, GL_ACTIVE_UNIFORM_BLOCKS, &count);
    if (count != 0) {
        *error = "Unsupported preview shader uniform block";
        return false;
    }
    // Storage blocks are a program interface, not ordinary active uniforms.
    // In particular, a linked shader can have no GL_ACTIVE_UNIFORMS while
    // still reading unowned SSBO data through this interface.
    if (epoxy_gl_version() >= 43 ||
        (epoxy_has_gl_extension("GL_ARB_shader_storage_buffer_object") &&
         epoxy_has_gl_extension("GL_ARB_program_interface_query"))) {
        glGetProgramInterfaceiv(program, GL_SHADER_STORAGE_BLOCK,
                                GL_ACTIVE_RESOURCES, &count);
        if (count != 0) {
            *error = "Unsupported preview shader shader-storage block";
            return false;
        }
        glGetProgramInterfaceiv(program, GL_ATOMIC_COUNTER_BUFFER,
                                GL_ACTIVE_RESOURCES, &count);
        if (count != 0) {
            *error = "Unsupported preview shader atomic-counter buffer";
            return false;
        }
    }
    if (epoxy_gl_version() >= 40) {
        for (GLenum stage : { GL_VERTEX_SHADER, GL_FRAGMENT_SHADER }) {
            glGetProgramStageiv(program, stage, GL_ACTIVE_SUBROUTINE_UNIFORMS,
                                &count);
            if (count != 0) {
                *error = "Unsupported preview shader subroutine input";
                return false;
            }
        }
    }
    glGetProgramiv(program, GL_ACTIVE_UNIFORMS, &count);
    glGetProgramiv(program, GL_ACTIVE_UNIFORM_MAX_LENGTH, &max_name);
    if (count == 0) return true;
    if (count < 0 || max_name <= 0 || max_name > 1024) {
        *error = "Unsupported preview shader uniform interface";
        return false;
    }
    std::vector<GLchar> name(static_cast<size_t>(max_name));
    for (GLint i = 0; i < count; ++i) {
        GLsizei length = 0;
        GLint size = 0;
        GLenum type = 0;
        glGetActiveUniform(program, static_cast<GLuint>(i), max_name,
                           &length, &size, &type, name.data());
        if (length <= 0 || length >= max_name) {
            *error = "Unsupported preview shader uniform name";
            return false;
        }
        const GLuint index = static_cast<GLuint>(i);
        GLint block = -1;
        glGetActiveUniformsiv(program, 1, &index, GL_UNIFORM_BLOCK_INDEX,
                             &block);
        const std::string uniform(name.data(), static_cast<size_t>(length));
        if (block != -1) {
            *error = "Unsupported preview shader uniform block: " + uniform;
            return false;
        }
        if ((type == GL_SAMPLER_2D || type == GL_SAMPLER_CUBE) && size == 1 &&
            (uniform == "texSamp0" || uniform == "texSamp1" ||
             uniform == "texSamp2" || uniform == "texSamp3")) {
            continue;
        }
        if (pipeline) {
            const auto found = std::find_if(
                pipeline->uniforms.begin(), pipeline->uniforms.end(),
                [&](const OwnedDrawUniform &captured) {
                    return captured.name == uniform &&
                           CapturedGlUniformType(captured) == type &&
                           captured.count >= uint32_t(size);
                });
            if (found == pipeline->uniforms.end()) {
                *error = "Original camera uniform is unavailable: " + uniform;
                return false;
            }
            continue;
        }
        const bool bound =
            (uniform == "alphaRef" && type == GL_INT && size == 1) ||
            (uniform == "fogColor" && type == GL_FLOAT_VEC4 && size == 1) ||
            (uniform == "clipRange" && type == GL_FLOAT_VEC4 && size == 1) ||
            (uniform == "surfaceScale" && type == GL_INT_VEC2 && size == 1) ||
            (uniform == "clipRegion[0]" && type == GL_INT_VEC4 && size <= 8) ||
            (uniform == "consts[0]" && type == GL_FLOAT_VEC4 && size <= 18) ||
            ((uniform == "texScale[0]" || uniform == "bumpOffset[0]" ||
              uniform == "bumpScale[0]") &&
             type == GL_FLOAT && size <= 4) ||
            ((uniform == "colorKey[0]" || uniform == "colorKeyMask[0]") &&
             type == GL_UNSIGNED_INT && size <= 4) ||
            (uniform == "bumpMat[0]" && type == GL_FLOAT_MAT2 && size <= 4) ||
            ((uniform == "depthFactor" || uniform == "depthOffset") &&
             type == GL_FLOAT && size == 1);
        if (!bound || !IsSyntheticUniformType(type)) {
            *error = "Unsupported preview shader uniform input: " + uniform;
            return false;
        }
    }
    return true;
}

bool CheckOriginalGlTextures(GLuint program, const PreviewPacket &packet,
                             std::string *error)
{
    if (!packet.captured_pipeline)
        return true;
    for (size_t slot = 0; slot < 4; ++slot) {
        const std::string name = "texSamp" + std::to_string(slot);
        const char *pointer = name.c_str();
        GLuint index = GL_INVALID_INDEX;
        glGetUniformIndices(program, 1, &pointer, &index);
        if (index == GL_INVALID_INDEX)
            continue;
        GLint type = 0;
        glGetActiveUniformsiv(program, 1, &index, GL_UNIFORM_TYPE, &type);
        if (!PreviewCapturedTexture(packet, slot, type == GL_SAMPLER_CUBE)) {
            *error =
                "Original camera texture base image is unavailable: " + name;
            return false;
        }
    }
    return true;
}

bool GlDepthClampSupported()
{
    return epoxy_gl_version() >= 32 ||
           epoxy_has_gl_extension("GL_ARB_depth_clamp");
}

void ApplyOriginalGlRaster(const PreviewCapturedRaster &r, uint32_t height)
{
    const GLenum factors[] = { GL_ZERO,
                               GL_ONE,
                               GL_SRC_COLOR,
                               GL_ONE_MINUS_SRC_COLOR,
                               GL_DST_COLOR,
                               GL_ONE_MINUS_DST_COLOR,
                               GL_SRC_ALPHA,
                               GL_ONE_MINUS_SRC_ALPHA,
                               GL_DST_ALPHA,
                               GL_ONE_MINUS_DST_ALPHA,
                               GL_CONSTANT_COLOR,
                               GL_ONE_MINUS_CONSTANT_COLOR,
                               GL_CONSTANT_ALPHA,
                               GL_ONE_MINUS_CONSTANT_ALPHA,
                               GL_SRC_ALPHA_SATURATE };
    const GLenum ops[] = { GL_FUNC_ADD, GL_FUNC_SUBTRACT,
                           GL_FUNC_REVERSE_SUBTRACT, GL_MIN, GL_MAX };
    const GLenum compares[] = { GL_NEVER,   GL_LESS,     GL_EQUAL,  GL_LEQUAL,
                                GL_GREATER, GL_NOTEQUAL, GL_GEQUAL, GL_ALWAYS };
    const GLenum stencil_ops[] = { GL_KEEP,      GL_ZERO,     GL_REPLACE,
                                   GL_INCR,      GL_DECR,     GL_INVERT,
                                   GL_INCR_WRAP, GL_DECR_WRAP };
    if (r.scissor_enabled) {
        glEnable(GL_SCISSOR_TEST);
        glScissor(r.scissor_x, int64_t(height) - r.scissor_y - r.scissor_height,
                  r.scissor_width, r.scissor_height);
    } else
        glDisable(GL_SCISSOR_TEST);
    glColorMask(r.color_write & 1, r.color_write & 2, r.color_write & 4,
                r.color_write & 8);
    if (r.blend_enabled)
        glEnable(GL_BLEND);
    else
        glDisable(GL_BLEND);
    glBlendFuncSeparate(factors[r.src_rgb], factors[r.dst_rgb],
                        factors[r.src_alpha], factors[r.dst_alpha]);
    glBlendEquationSeparate(ops[r.blend_rgb], ops[r.blend_alpha]);
    glBlendColor(r.blend_color[0], r.blend_color[1], r.blend_color[2],
                 r.blend_color[3]);
    if (r.depth_test)
        glEnable(GL_DEPTH_TEST);
    else
        glDisable(GL_DEPTH_TEST);
    glDepthMask(r.depth_write);
    glDepthFunc(compares[r.depth_compare]);
    glDepthRange(r.depth_min, r.depth_max);
    if (GlDepthClampSupported()) {
        if (r.depth_clamp)
            glEnable(GL_DEPTH_CLAMP);
        else
            glDisable(GL_DEPTH_CLAMP);
    }
    if (r.stencil_test)
        glEnable(GL_STENCIL_TEST);
    else
        glDisable(GL_STENCIL_TEST);
    auto stencil = [&](GLenum face, const PreviewCapturedStencil &s) {
        glStencilFuncSeparate(face, compares[s.compare], s.reference,
                              s.read_mask);
        glStencilMaskSeparate(face, s.write_mask);
        glStencilOpSeparate(face, stencil_ops[s.fail],
                            stencil_ops[s.depth_fail], stencil_ops[s.pass]);
    };
    stencil(GL_FRONT, r.front_stencil);
    stencil(GL_BACK, r.back_stencil);
    glFrontFace(r.front_ccw ? GL_CCW : GL_CW);
    if (r.cull_mode) {
        glEnable(GL_CULL_FACE);
        glCullFace(r.cull_mode == 1 ? GL_FRONT :
                   r.cull_mode == 2 ? GL_BACK :
                                      GL_FRONT_AND_BACK);
    } else
        glDisable(GL_CULL_FACE);
    if (r.depth_bias) {
        glEnable(GL_POLYGON_OFFSET_FILL);
        glPolygonOffset(r.bias_slope, r.bias_constant);
    } else
        glDisable(GL_POLYGON_OFFSET_FILL);
}

} // namespace

struct PreviewGlExecutor::Impl {
    struct Slot {
        GLuint texture = 0;
        uint32_t width = 0;
        uint32_t height = 0;
        uint64_t generation = 0;
        PreviewResultKey published_result;
        PreviewDrawTiming published_timing;
        PreviewFrameRef copy_frame;
        bool copy_pending = false, copy_ready = false,
             copy_pixels_ready = false;
        std::vector<uint8_t> copy_pixels;
        PreviewDrawTiming copy_timing;
        std::string copy_error;
    };
    struct Retirement {
        PreviewFrameRef frame;
        GLsync fence = nullptr;
        bool fence_failed = false;
    };

    SDL_Window *window = nullptr;
    SDL_GLContext context = nullptr;
    ContextBinder bind_context = nullptr;
    std::thread worker;
    std::atomic<bool> stop{false};
    std::mutex slots_mutex;
    std::array<Slot, kPreviewSlotCount> slots{};
    bool CopyPending(const PreviewFrameRef &frame)
    {
        std::lock_guard<std::mutex> lock(slots_mutex);
        return frame.slot < slots.size() && slots[frame.slot].copy_pending &&
               slots[frame.slot].copy_frame.slot_generation ==
                   frame.slot_generation;
    }

    void PublishTiming(const PreviewWorkItem &work,
                       const PreviewDrawTiming &timing)
    {
        std::lock_guard<std::mutex> lock(slots_mutex);
        if (work.slot >= slots.size())
            return;
        auto &slot = slots[work.slot];
        if (slot.generation != work.slot_generation ||
            slot.published_result != work.result_key)
            return;
        slot.published_timing = timing;
        if (slot.copy_pending &&
            slot.copy_frame.slot_generation == work.slot_generation &&
            slot.copy_frame.result_key == work.result_key)
            slot.copy_timing = timing;
    }

    // Comparison readback belongs to the producing context. A consumer lease
    // protects each requested generation until its owned pixels and timing are
    // ready; normal live preview never requests this download.
    void PumpOutputCopies()
    {
        for (size_t index = 0; index < slots.size(); ++index) {
            PreviewFrameRef frame;
            GLuint texture = 0;
            {
                std::lock_guard<std::mutex> lock(slots_mutex);
                auto &slot = slots[index];
                if (!slot.copy_pending)
                    continue;
                if (slot.copy_pixels_ready) {
                    if (slot.copy_timing.status !=
                        PreviewDrawTimingStatus::Pending) {
                        slot.copy_pending = false;
                        slot.copy_ready = true;
                    }
                    continue;
                }
                frame = slot.copy_frame;
                if (slot.generation != frame.slot_generation ||
                    slot.published_result != frame.result_key) {
                    slot.copy_error = "Comparison generation changed before "
                                      "owned readback";
                    slot.copy_pending = false;
                    slot.copy_ready = true;
                    continue;
                }
                texture = slot.texture;
            }
            std::vector<uint8_t> pixels(size_t(frame.width) * frame.height * 4);
            const GLenum parameters[] = {
                GL_PACK_ALIGNMENT,    GL_PACK_ROW_LENGTH, GL_PACK_SKIP_ROWS,
                GL_PACK_SKIP_PIXELS,  GL_PACK_SWAP_BYTES, GL_PACK_LSB_FIRST,
                GL_PACK_IMAGE_HEIGHT, GL_PACK_SKIP_IMAGES
            };
            GLint values[8], pack_buffer, binding;
            for (size_t i = 0; i < 8; ++i)
                glGetIntegerv(parameters[i], &values[i]);
            glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING, &pack_buffer);
            glGetIntegerv(GL_TEXTURE_BINDING_2D, &binding);
            glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
            for (size_t i = 0; i < 8; ++i)
                glPixelStorei(parameters[i], i == 0 ? 1 : 0);
            glBindTexture(GL_TEXTURE_2D, texture);
            glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                          pixels.data());
            const GLenum result = glGetError();
            glBindTexture(GL_TEXTURE_2D, binding);
            for (size_t i = 0; i < 8; ++i)
                glPixelStorei(parameters[i], values[i]);
            glBindBuffer(GL_PIXEL_PACK_BUFFER, pack_buffer);
            if (!PreviewChannelIsDiagnostic(frame.result_key.channel)) {
                const size_t row = size_t(frame.width) * 4;
                for (size_t y = 0; y < frame.height / 2; ++y)
                    std::swap_ranges(pixels.begin() + y * row,
                                     pixels.begin() + (y + 1) * row,
                                     pixels.begin() +
                                         (frame.height - 1 - y) * row);
            }
            // GL scalar views use sampling swizzles, which glGetTexImage
            // excludes. Vulkan has already applied its channel conversion
            // before uploading this worker texture.
            if (frame.result_key.compile.selection.backend ==
                PreviewBackend::OpenGL)
                ApplyPreviewOutputChannel(frame.result_key.channel, &pixels);
            std::lock_guard<std::mutex> lock(slots_mutex);
            auto &slot = slots[index];
            if (!slot.copy_pending ||
                slot.copy_frame.slot_generation != frame.slot_generation ||
                slot.copy_frame.result_key != frame.result_key ||
                slot.generation != frame.slot_generation ||
                slot.published_result != frame.result_key)
                continue;
            if (result != GL_NO_ERROR) {
                slot.copy_error = "Comparison worker readback failed with GL "
                                  "error " +
                                  std::to_string(result);
                slot.copy_pending = false;
                slot.copy_ready = true;
            } else {
                slot.copy_pixels = std::move(pixels);
                slot.copy_pixels_ready = true;
                if (slot.copy_timing.status !=
                    PreviewDrawTimingStatus::Pending) {
                    slot.copy_pending = false;
                    slot.copy_ready = true;
                }
            }
        }
    }

    struct DrawQueries {
        GLuint objects[2]{};
        PreviewWorkItem work;
        PreviewDrawTiming timing;
        bool pending = false;
        uint64_t issued_ns = 0;
    };
    std::array<DrawQueries, kPreviewSlotCount> draw_queries{};

    bool PollDrawQuery(DrawQueries &query)
    {
        if (!query.pending)
            return false;
        GLint available[2]{};
        glGetQueryObjectiv(query.objects[0], GL_QUERY_RESULT_AVAILABLE,
                           &available[0]);
        glGetQueryObjectiv(query.objects[1], GL_QUERY_RESULT_AVAILABLE,
                           &available[1]);
        GLenum error = glGetError();
        if (!error && (!available[0] || !available[1]) &&
            NowNs() - query.issued_ns <= kPreviewMaxDrawTimingNs)
            return false;
        query.pending = false;
        auto &timing = query.timing;
        timing.status = PreviewDrawTimingStatus::Failed;
        if (error || !available[0] || !available[1]) {
            timing.message =
                "GL timestamp availability failed or exceeded 10 s";
            return true;
        }
        GLuint64 start = 0, finish = 0;
        glGetQueryObjectui64v(query.objects[0], GL_QUERY_RESULT, &start);
        glGetQueryObjectui64v(query.objects[1], GL_QUERY_RESULT, &finish);
        GLint disjoint = GL_FALSE;
        if (epoxy_has_gl_extension("GL_EXT_disjoint_timer_query"))
            glGetIntegerv(GL_GPU_DISJOINT_EXT, &disjoint);
        error = glGetError();
        if (error || disjoint) {
            timing.message =
                "GL timestamp result failed or GPU clock was disjoint";
            return true;
        }
        if (ComputePreviewDrawInterval(start, finish,
                                       timing.timestamp_valid_bits, 1.0,
                                       &timing.nanoseconds, &timing.message))
            timing.status = PreviewDrawTimingStatus::Measured;
        return true;
    }
    void BeginDrawQuery(const PreviewWorkItem &work, uint32_t commands)
    {
        auto &query = draw_queries[work.slot];
        query.pending = false;
        query.work = work;
        query.work.packet.reset(); // Query metadata never retains a packet.
        query.timing = {};
        auto &timing = query.timing;
        timing.result = work.result_key;
        timing.backend = PreviewBackend::OpenGL;
        if (!work.packet->profile_draw)
            return;
        timing.actual_draw_commands = commands;
        timing.provenance =
            work.packet->packet_kind == PreviewPacketKind::Replay ?
                PreviewDrawTimingProvenance::ReplayInstrumented :
                PreviewDrawTimingProvenance::SelectedPreviewInstrumented;
        timing.status = PreviewDrawTimingStatus::Unsupported;
        if (epoxy_gl_version() < 33 &&
            !epoxy_has_gl_extension("GL_ARB_timer_query")) {
            timing.message = "GL timestamp queries are unavailable";
            return;
        }
        GLint bits = 0;
        glGetQueryiv(GL_TIMESTAMP, GL_QUERY_COUNTER_BITS, &bits);
        if (bits <= 0 || bits > 64) {
            timing.message = "GL timestamp counter has no supported valid bits";
            return;
        }
        timing.timestamp_valid_bits = bits;
        timing.timestamp_period_ns = 1;
        if (!query.objects[0])
            glGenQueries(2, query.objects);
        if (!query.objects[0] || !query.objects[1]) {
            timing.status = PreviewDrawTimingStatus::Failed;
            timing.message = "GL timestamp pair allocation failed";
            return;
        }
        timing.status = PreviewDrawTimingStatus::Pending;
        query.issued_ns = NowNs();
        query.pending = true;
        glQueryCounter(query.objects[0], GL_TIMESTAMP);
    }
    void EndDrawQuery(uint32_t slot)
    {
        auto &query = draw_queries[slot];
        if (query.pending)
            glQueryCounter(query.objects[1], GL_TIMESTAMP);
    }
    GLuint program = 0;
    GLuint reference_program = 0;
    GLuint vao = 0;
    GLuint vbo = 0;
    GLuint fbo = 0;
    GLuint depth_buffer = 0;
    uint32_t depth_width = 0;
    uint32_t depth_height = 0;
    GLuint fixture_texture[4]{};
    GLenum fixture_targets[4]{ GL_TEXTURE_2D, GL_TEXTURE_2D, GL_TEXTURE_2D,
                               GL_TEXTURE_2D };
    PreviewCompileKey program_key;
    PreviewDigest uploaded_material_digest{};
    bool material_uploaded = false;
    std::array<GLuint, 16> raw_attribute_buffers{};
    GLuint raw_index_buffer = 0;
    PreviewDigest uploaded_pipeline_digest{};
    bool pipeline_uploaded = false;
    bool has_program = false;
    PreviewFrameRef displayed;
    GLuint displayed_texture = 0;
    GLsync displayed_fence = nullptr;
    bool displayed_fence_failed = false;
    bool has_displayed = false;
    bool sampled_this_frame = false;
    PreviewFrameRef frozen;
    GLuint frozen_texture = 0;
    GLsync frozen_fence = nullptr;
    bool frozen_fence_failed = false;
    bool has_frozen = false;
    bool frozen_sampled_this_frame = false;
    std::vector<Retirement> retirements;
    std::vector<PreviewFrameRef> copied_frames;

    bool PrepareReference(std::string *error)
    {
        if (reference_program) return true;
        constexpr const char *vertex_source =
            "#version 400\nlayout(location=0) in vec4 position;\n"
            "layout(location=1) in vec4 vertexColor;\n"
            "out vec4 referenceColor;\n"
            "void main(){gl_Position=position;referenceColor=vertexColor;}\n";
        constexpr const char *fragment_source =
            "#version 400\nin vec4 referenceColor;\n"
            "out vec4 color;\nvoid main(){color=referenceColor;}\n";
        GLuint vertex = 0, fragment = 0;
        if (!Compile(GL_VERTEX_SHADER, vertex_source, &vertex, error))
            return false;
        if (!Compile(GL_FRAGMENT_SHADER, fragment_source, &fragment, error)) {
            glDeleteShader(vertex);
            return false;
        }
        GLuint next = glCreateProgram();
        if (!next) {
            glDeleteShader(vertex);
            glDeleteShader(fragment);
            *error = "Preview GL reference program allocation failed";
            return false;
        }
        glAttachShader(next, vertex);
        glAttachShader(next, fragment);
        glLinkProgram(next);
        glDeleteShader(vertex);
        glDeleteShader(fragment);
        GLint linked = GL_FALSE;
        glGetProgramiv(next, GL_LINK_STATUS, &linked);
        if (linked != GL_TRUE) {
            glDeleteProgram(next);
            *error = "Preview GL reference program link failed";
            return false;
        }
        reference_program = next;
        return true;
    }

    bool Prepare(const PreviewWorkItem &work, std::string *error,
                 bool *unsupported)
    {
        *unsupported = false;
        if (!work.packet ||
            work.packet->selection.backend != PreviewBackend::OpenGL ||
            work.packet->source.empty() ||
            work.packet->partner_source.empty()) {
            *error = "Preview OpenGL preview requires copied fragment and "
                     "vertex source";
            return false;
        }
        if (work.packet->captured_pipeline &&
            work.packet->captured_pipeline->raster.depth_clamp &&
            !GlDepthClampSupported()) {
            *unsupported = true;
            *error = "Captured raster state requires OpenGL depth clamp "
                     "support";
            return false;
        }
        if (has_program && program_key == work.compile_key && vao && vbo &&
            fbo && depth_buffer && reference_program && fixture_texture[0])
            return true;
        GLuint vertex = 0;
        GLuint fragment = 0;
        GLuint geometry = 0;
        if (!Compile(GL_VERTEX_SHADER, work.packet->partner_source, &vertex,
                     error)) return false;
        if (!Compile(GL_FRAGMENT_SHADER, work.packet->source, &fragment,
                     error)) {
            glDeleteShader(vertex);
            return false;
        }
        GLuint next = glCreateProgram();
        if (!next) {
            glDeleteShader(vertex);
            glDeleteShader(fragment);
            *error = "Preview GL program allocation failed";
            return false;
        }
        glAttachShader(next, vertex);
        glAttachShader(next, fragment);
        if (work.packet->captured_pipeline &&
            !work.packet->captured_pipeline->geometry_source.empty()) {
            if (!Compile(GL_GEOMETRY_SHADER,
                         work.packet->captured_pipeline->geometry_source,
                         &geometry, error)) {
                glDeleteShader(vertex);
                glDeleteShader(fragment);
                glDeleteProgram(next);
                return false;
            }
            glAttachShader(next, geometry);
        }
        glLinkProgram(next);
        glDeleteShader(vertex);
        glDeleteShader(fragment);
        if (geometry)
            glDeleteShader(geometry);
        GLint linked = GL_FALSE;
        glGetProgramiv(next, GL_LINK_STATUS, &linked);
        if (linked != GL_TRUE) {
            std::array<char, 1024> log{};
            glGetProgramInfoLog(next, static_cast<GLsizei>(log.size()),
                                nullptr, log.data());
            *error =
                std::string("Preview GL program link failed: ") + log.data();
            glDeleteProgram(next);
            return false;
        }
        if (geometry) {
            const auto topology = work.packet->captured_pipeline->host_topology;
            GLenum expected_input = 0;
            switch (topology) {
            case GL_TRIANGLES:
            case GL_TRIANGLE_STRIP:
            case GL_TRIANGLE_FAN:
                expected_input = GL_TRIANGLES;
                break;
            case GL_LINES_ADJACENCY:
            case GL_LINE_STRIP_ADJACENCY:
                expected_input = GL_LINES_ADJACENCY;
                break;
            }
            GLint geometry_input = 0;
            glGetProgramiv(next, GL_GEOMETRY_INPUT_TYPE, &geometry_input);
            if (!expected_input || geometry_input != GLint(expected_input)) {
                glDeleteProgram(next);
                *unsupported = true;
                *error = "Captured OpenGL geometry shader input does not match "
                         "the captured host topology";
                return false;
            }
        }
        if (!CheckSyntheticUniformInterface(
                next, error, work.packet->captured_pipeline.get()) ||
            !CheckOriginalGlTextures(next, *work.packet, error)) {
            glDeleteProgram(next);
            *unsupported = true;
            return false;
        }
        if (!PrepareReference(error)) {
            glDeleteProgram(next);
            return false;
        }
        if (program) glDeleteProgram(program);
        has_program = false;
        program = next;
        program_key = work.compile_key;
        if (!vao) glGenVertexArrays(1, &vao);
        if (!vbo) glGenBuffers(1, &vbo);
        if (!fbo) glGenFramebuffers(1, &fbo);
        if (!depth_buffer) glGenRenderbuffers(1, &depth_buffer);
        if (fixture_texture[0])
            glDeleteTextures(4, fixture_texture);
        glGenTextures(4, fixture_texture);
        material_uploaded = false;
        pipeline_uploaded = false;
        if (work.packet->captured_pipeline) {
            GLint active = 0;
            glGetProgramiv(program, GL_ACTIVE_ATTRIBUTES, &active);
            for (GLint index = 0; index < active; ++index) {
                std::array<char, 256> name{};
                GLsizei length;
                GLint count;
                GLenum type;
                glGetActiveAttrib(program, index, name.size(), &length, &count,
                                  &type, name.data());
                const GLint location =
                    glGetAttribLocation(program, name.data());
                if (location < 0 || location >= 16 || count != 1 ||
                    work.packet->captured_pipeline->attributes[location]
                        .stream.name.empty() ||
                    (type != GL_FLOAT && type != GL_FLOAT_VEC2 &&
                     type != GL_FLOAT_VEC3 && type != GL_FLOAT_VEC4 &&
                     type != GL_INT && type != GL_INT_VEC2 &&
                     type != GL_INT_VEC3 && type != GL_INT_VEC4 &&
                     type != GL_UNSIGNED_INT && type != GL_UNSIGNED_INT_VEC2 &&
                     type != GL_UNSIGNED_INT_VEC3 &&
                     type != GL_UNSIGNED_INT_VEC4)) {
                    *unsupported = true;
                    *error = "Original camera active vertex attribute is "
                             "unavailable or unsupported";
                    return false;
                }
                const bool integer =
                    type == GL_INT || type == GL_INT_VEC2 ||
                    type == GL_INT_VEC3 || type == GL_INT_VEC4 ||
                    type == GL_UNSIGNED_INT || type == GL_UNSIGNED_INT_VEC2 ||
                    type == GL_UNSIGNED_INT_VEC3 ||
                    type == GL_UNSIGNED_INT_VEC4;
                const auto &attribute =
                    work.packet->captured_pipeline->attributes[location];
                if ((attribute.enabled &&
                     attribute.stream.integer != integer) ||
                    (!attribute.enabled && integer)) {
                    *unsupported = true;
                    *error = "Original camera vertex attribute numeric "
                             "interpretation is unsupported";
                    return false;
                }
            }
            if (!raw_attribute_buffers[0])
                glGenBuffers(16, raw_attribute_buffers.data());
            if (!raw_index_buffer)
                glGenBuffers(1, &raw_index_buffer);
        }
        for (int i = 0; i < 4; ++i) {
            std::string name = "texSamp" + std::to_string(i);
            const char *ptr = name.c_str();
            GLuint index = GL_INVALID_INDEX;
            glGetUniformIndices(program, 1, &ptr, &index);
            GLint type = GL_SAMPLER_2D;
            if (index != GL_INVALID_INDEX)
                glGetActiveUniformsiv(program, 1, &index, GL_UNIFORM_TYPE,
                                      &type);
            fixture_targets[i] =
                type == GL_SAMPLER_CUBE ? GL_TEXTURE_CUBE_MAP : GL_TEXTURE_2D;
        }
        has_program = vao && vbo && fbo && depth_buffer &&
                      fixture_texture[0];
        if (!has_program)
            *error = "Preview GL scene resource allocation failed";
        return has_program;
    }

    bool Render(const PreviewWorkItem &work, std::string *error)
    {
        if (!work.packet || !has_program ||
            work.compile_key != program_key ||
            work.slot >= slots.size()) {
            *error = "Preview GL preview inputs are incomplete";
            return false;
        }
        const PreviewPacket &packet = *work.packet;
        auto &initial_timing = draw_queries[work.slot];
        initial_timing.pending = false;
        initial_timing.timing = {};
        initial_timing.timing.result = work.result_key;
        initial_timing.timing.backend = PreviewBackend::OpenGL;
        if (packet.profile_draw) {
            initial_timing.timing.status = PreviewDrawTimingStatus::Unsupported;
            initial_timing.timing.provenance =
                packet.packet_kind == PreviewPacketKind::Replay ?
                    PreviewDrawTimingProvenance::ReplayInstrumented :
                    PreviewDrawTimingProvenance::SelectedPreviewInstrumented;
            initial_timing.timing.message =
                "This channel emits no GPU draw interval";
        }
        if (packet.captured_pipeline &&
            !ValidatePreviewCapturedPipeline(*packet.captured_pipeline, error))
            return false;
        if (packet.captured_pipeline &&
            !packet.captured_pipeline->color_before.rgba.empty() &&
            (packet.captured_pipeline->color_before.width != packet.width ||
             packet.captured_pipeline->color_before.height != packet.height)) {
            *error =
                "Owned before destination does not match native preview extent";
            return false;
        }
        if (packet.captured_pipeline &&
            !CheckSyntheticUniformInterface(program, error,
                                            packet.captured_pipeline.get()))
            return false;
        if (!CheckOriginalGlTextures(program, packet, error))
            return false;
        PreviewSyntheticFixture fixture{};
        if (!DecodePreviewSyntheticFixture(packet.fixture_bytes, &fixture,
                                           error)) {
            return false;
        }
        ApplyPreviewDeclaredBindings(&fixture, packet,
                                     work.result_key.time_seconds);
        if (!PreviewChannelAvailable(work.result_key.channel)) {
            *error = PreviewChannelProvenance(work.result_key.channel);
            return false;
        }
        if (PreviewChannelIsDiagnostic(work.result_key.channel)) {
            if (packet.captured_pipeline) {
                *error = "Native captured pipeline cannot use synthetic "
                         "fixture diagnostic channels";
                return false;
            }
            std::vector<uint8_t> pixels;
            return RenderPreviewDiagnostic(work.result_key.channel, fixture,
                                           packet.width, packet.height, &pixels,
                                           error) &&
                   UploadPixels(work, pixels, error);
        }
        Slot &slot = slots[work.slot];
        if (!slot.texture) glGenTextures(1, &slot.texture);
        if (!slot.texture) {
            *error = "Preview GL output texture allocation failed";
            return false;
        }
        glBindTexture(GL_TEXTURE_2D, slot.texture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        if (slot.width != packet.width || slot.height != packet.height) {
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8,
                         static_cast<GLsizei>(packet.width),
                         static_cast<GLsizei>(packet.height), 0, GL_RGBA,
                         GL_UNSIGNED_BYTE, nullptr);
            slot.width = packet.width;
            slot.height = packet.height;
        }
        const bool seeded =
            packet.captured_pipeline &&
            !packet.captured_pipeline->color_before.rgba.empty();
        if (seeded) {
            const auto &before = packet.captured_pipeline->color_before;
            std::vector<uint8_t> pixels(before.rgba.size());
            CopyPreviewCapturedTextureRows(before, pixels.data(), true);
            glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, packet.width, packet.height,
                            GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
        }
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                               GL_TEXTURE_2D, slot.texture, 0);
        if (depth_width != packet.width || depth_height != packet.height) {
            glBindRenderbuffer(GL_RENDERBUFFER, depth_buffer);
            glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8,
                                  static_cast<GLsizei>(packet.width),
                                  static_cast<GLsizei>(packet.height));
            if (glGetError() != GL_NO_ERROR) {
                *error = "Preview GL depth attachment allocation failed";
                return false;
            }
            depth_width = packet.width;
            depth_height = packet.height;
        }
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT,
                                  GL_RENDERBUFFER, depth_buffer);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) !=
            GL_FRAMEBUFFER_COMPLETE) {
            *error = "Preview GL framebuffer is incomplete";
            return false;
        }
        const bool original_pipeline = bool(packet.captured_pipeline);
        auto frame =
            original_pipeline ?
                PreviewSceneFrame{} :
            packet.packet_kind == PreviewPacketKind::Replay ?
                BuildPreviewCapturedFrame(work.result_key.scene,
                                          packet.captured_mesh,
                                          float(packet.width) / packet.height) :
                BuildPreviewSceneFrame(work.result_key.scene,
                                       float(packet.width) / packet.height);
        if (original_pipeline)
            frame.draw_count = 1;
        if ((!original_pipeline && frame.draw_count != kPreviewMaxSceneDraws) ||
            frame.vertices.size() > kPreviewMaxSceneVertices) {
            *error = "Preview GL scene geometry is invalid";
            return false;
        }
        std::array<bool, 4> cube_stages{};
        for (size_t i = 0; i < 4; ++i)
            cube_stages[i] = fixture_targets[i] == GL_TEXTURE_CUBE_MAP;
        const auto &target_draw = frame.draws[frame.draw_count - 1];
        std::vector<Vertex> target_vertices(
            frame.vertices.begin() + target_draw.first_vertex,
            frame.vertices.begin() + target_draw.first_vertex +
                target_draw.vertex_count);
        ApplyPreviewSyntheticFixture(fixture, target_vertices, cube_stages);
        std::copy(target_vertices.begin(), target_vertices.end(),
                  frame.vertices.begin() + target_draw.first_vertex);
        const PreviewRenderState render_state =
            ClampPreviewRenderState(packet.render_state);
        if (!original_pipeline && !AdmitPreviewBakedAlphaTest(packet, error))
            return false;
        glViewport(0, 0, static_cast<GLsizei>(packet.width),
                   static_cast<GLsizei>(packet.height));
        glDisable(GL_BLEND);
        glDisable(GL_CULL_FACE);
        glDisable(GL_SCISSOR_TEST);
        glDisable(GL_STENCIL_TEST);
        glDisable(GL_POLYGON_OFFSET_FILL);
        glDisable(GL_SAMPLE_ALPHA_TO_COVERAGE);
        glDisable(GL_SAMPLE_ALPHA_TO_ONE);
        glDisable(GL_SAMPLE_COVERAGE);
        glDisable(GL_SAMPLE_MASK);
        if (GlDepthClampSupported())
            glDisable(GL_DEPTH_CLAMP);
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        glDepthRange(0, 1);
        glFrontFace(GL_CCW);
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LESS);
        glDepthMask(GL_TRUE);
        glClearDepth(1.0);
        glClearColor(render_state.clear_color[0], render_state.clear_color[1],
                     render_state.clear_color[2], render_state.clear_color[3]);
        if (original_pipeline)
            glClearColor(0, 0, 0, 1);
        glStencilMask(UINT32_MAX);
        glClearStencil(0);
        glClear((seeded ? 0 : GL_COLOR_BUFFER_BIT) | GL_DEPTH_BUFFER_BIT |
                GL_STENCIL_BUFFER_BIT);
        glUseProgram(program);
        // Resident xemu fragment sources expect the same clipping and
        // coordinate uniforms as the game renderer. Give the private quad
        // an explicit synthetic viewport instead of GL's zero defaults,
        // which would discard every fragment in the window-clip loop.
        std::array<GLint, 8 * 4> clip_regions{};
        for (size_t i = 0; i < 8; ++i) {
            clip_regions[i * 4 + 2] = static_cast<GLint>(packet.width);
            clip_regions[i * 4 + 3] = static_cast<GLint>(packet.height);
        }
        GLint location = glGetUniformLocation(program, "clipRegion[0]");
        if (location >= 0) {
            glUniform4iv(location, 8, clip_regions.data());
        }
        location = glGetUniformLocation(program, "clipRange");
        if (location >= 0) {
            glUniform4f(location, 0.0f, 0.0f, -1.0e9f, 1.0e9f);
        }
        location = glGetUniformLocation(program, "surfaceScale");
        if (location >= 0) glUniform2i(location, 1, 1);
        const GLfloat texture_scales[4] = { 8.0f, 8.0f, 8.0f, 8.0f };
        location = glGetUniformLocation(program, "texScale[0]");
        if (location >= 0) glUniform1fv(location, 4, texture_scales);
        for (size_t i = 0; i < 18; ++i) {
            std::string name = "consts[" + std::to_string(i) + "]";
            location = glGetUniformLocation(program, name.c_str());
            if (location >= 0) {
                glUniform4fv(location, 1, fixture.constant_color.data());
            }
        }
        location = glGetUniformLocation(program, "fogColor");
        if (location >= 0) glUniform4fv(location, 1, fixture.fog_color.data());
        location = glGetUniformLocation(program, "alphaRef");
        if (location >= 0) glUniform1i(location, render_state.alpha_reference);
        const bool captured = packet.packet_kind == PreviewPacketKind::Replay &&
                              packet.captured_material;
        std::string material_status;
        if (captured) {
            material_status =
                DescribePreviewCapturedMaterial(*packet.captured_material);
            const size_t unapplied = ApplyCapturedGlUniforms(
                program, packet.captured_material->uniforms);
            if (unapplied)
                material_status += "; " + std::to_string(unapplied) +
                                   " reflected uniforms unapplied";
        }
        if (original_pipeline) {
            material_status =
                DescribePreviewCapturedRaster(*packet.captured_pipeline) +
                "; " + material_status;
            const size_t unapplied = ApplyCapturedGlUniforms(
                program, packet.captured_pipeline->uniforms, true);
            if (unapplied)
                material_status += "; " + std::to_string(unapplied) +
                                   " captured uniforms inactive or unapplied";
        }
        glBindVertexArray(vao);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferData(GL_ARRAY_BUFFER,
                     frame.vertices.size() * sizeof(Vertex),
                     frame.vertices.data(), GL_STREAM_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                              reinterpret_cast<const void *>(0));
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(
            1, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex),
            reinterpret_cast<const void *>(offsetof(Vertex, color)));
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(
            2, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex),
            reinterpret_cast<const void *>(offsetof(Vertex, uv)));
        for (int i = 0; i < 4; ++i) {
            glEnableVertexAttribArray(3 + i);
            glVertexAttribPointer(3 + i, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                                  reinterpret_cast<const void *>(
                                      offsetof(Vertex, colors) + i * 16));
        }
        glEnableVertexAttribArray(7);
        glVertexAttribPointer(
            7, 1, GL_FLOAT, GL_FALSE, sizeof(Vertex),
            reinterpret_cast<const void *>(offsetof(Vertex, fog)));
        glEnableVertexAttribArray(8);
        glVertexAttribPointer(
            8, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex),
            reinterpret_cast<const void *>(offsetof(Vertex, direction)));
        glEnableVertexAttribArray(9);
        glVertexAttribPointer(
            9, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex),
            reinterpret_cast<const void *>(offsetof(Vertex, cube_stages)));
        const GLint filter = fixture.linear_filter ? GL_LINEAR : GL_NEAREST;
        const GLint wrap = fixture.repeat_wrap ? GL_REPEAT : GL_CLAMP_TO_EDGE;
        size_t zero_textures = 0;
        for (int unit = 0; unit < 4; ++unit) {
            glActiveTexture(GL_TEXTURE0 + unit);
            const GLenum target = fixture_targets[unit];
            glBindTexture(target, fixture_texture[unit]);
            const auto *texture = PreviewCapturedTexture(
                packet, unit, target == GL_TEXTURE_CUBE_MAP);
            if (!captured || !material_uploaded ||
                uploaded_material_digest != packet.material_digest) {
                if (const auto *storage =
                        PreviewCapturedTextureStorage(packet, unit)) {
                    std::vector<uint8_t> pixels(storage->bytes.size());
                    CopyPreviewCapturedStorageRows(
                        *storage, texture->metadata.width,
                        texture->metadata.height, pixels.data(), true);
                    glTexImage2D(target, 0, GL_R16, texture->metadata.width,
                                 texture->metadata.height, 0, GL_RED,
                                 GL_UNSIGNED_SHORT, pixels.data());
                } else if (captured && texture) {
                    for (const auto &image : texture->images) {
                        std::vector<uint8_t> pixels(image.image.rgba.size());
                        CopyPreviewCapturedTextureRows(image.image,
                                                       pixels.data(), true);
                        glTexImage2D(
                            target == GL_TEXTURE_CUBE_MAP ?
                                GL_TEXTURE_CUBE_MAP_POSITIVE_X + image.face :
                                target,
                            0, GL_RGBA8, image.image.width, image.image.height,
                            0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
                    }
                } else {
                    const auto pixels =
                        captured ? PreviewTexturePixels{} :
                                   GeneratePreviewTexture(fixture, unit);
                    for (int face = 0;
                         face < (target == GL_TEXTURE_CUBE_MAP ? 6 : 1); ++face)
                        glTexImage2D(
                            target == GL_TEXTURE_CUBE_MAP ?
                                GL_TEXTURE_CUBE_MAP_POSITIVE_X + face :
                                target,
                            0, GL_RGBA8, 8, 8, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                            pixels.data() + face * kPreviewTextureFaceBytes);
                }
            }
            GLint min_filter = filter, mag_filter = filter, wrap_s = wrap,
                  wrap_t = wrap, wrap_r = GL_CLAMP_TO_EDGE;
            if (captured) {
                min_filter = mag_filter = GL_NEAREST;
                wrap_s = wrap_t = GL_CLAMP_TO_EDGE;
                if (texture) {
                    const auto &meta = texture->metadata;
                    if (meta.min_filter == GL_NEAREST ||
                        meta.min_filter == GL_LINEAR ||
                        (meta.min_filter >= GL_NEAREST_MIPMAP_NEAREST &&
                         meta.min_filter <= GL_LINEAR_MIPMAP_LINEAR))
                        min_filter = meta.min_filter;
                    if (meta.mag_filter == GL_NEAREST ||
                        meta.mag_filter == GL_LINEAR)
                        mag_filter = meta.mag_filter;
                    wrap_s = CapturedGlWrap(meta.wrap_s);
                    wrap_t = CapturedGlWrap(meta.wrap_t);
                    wrap_r = CapturedGlWrap(meta.wrap_r);
                }
            }
            std::array<GLint, 4> swizzle{ GL_RED, GL_GREEN, GL_BLUE, GL_ALPHA };
            if (PreviewCapturedTextureStorage(packet, unit))
                std::copy(
                    packet.captured_material->texture_storage_swizzle[unit]
                        .begin(),
                    packet.captured_material->texture_storage_swizzle[unit]
                        .end(),
                    swizzle.begin());
            glTexParameteriv(target, GL_TEXTURE_SWIZZLE_RGBA, swizzle.data());
            glTexParameteri(target, GL_TEXTURE_BASE_LEVEL, 0);
            glTexParameteri(target, GL_TEXTURE_MAX_LEVEL, 0);
            glTexParameteri(target, GL_TEXTURE_MIN_FILTER, min_filter);
            glTexParameteri(target, GL_TEXTURE_MAG_FILTER, mag_filter);
            glTexParameteri(target, GL_TEXTURE_WRAP_S, wrap_s);
            glTexParameteri(target, GL_TEXTURE_WRAP_T, wrap_t);
            glTexParameteri(target, GL_TEXTURE_WRAP_R, wrap_r);
            std::string name = "texSamp" + std::to_string(unit);
            GLint sampler_location = glGetUniformLocation(program, name.c_str());
            if (captured && !texture && sampler_location >= 0)
                ++zero_textures;
            if (sampler_location >= 0) glUniform1i(sampler_location, unit);
        }
        material_uploaded = captured;
        uploaded_material_digest = packet.material_digest;
        if (captured && zero_textures)
            material_status += "; " + std::to_string(zero_textures) +
                               " texture slots use zero";
        glUseProgram(reference_program);
        for (size_t i = 0; i < frame.draw_count - 1; ++i) {
            const auto &draw = frame.draws[i];
            if (draw.vertex_count)
                glDrawArrays(GL_TRIANGLES, draw.first_vertex,
                             static_cast<GLsizei>(draw.vertex_count));
        }
        glUseProgram(program);
        if (render_state.depth_test) glEnable(GL_DEPTH_TEST);
        else glDisable(GL_DEPTH_TEST);
        glDepthMask(render_state.depth_write ? GL_TRUE : GL_FALSE);
        if (render_state.blend == PreviewBlendMode::Alpha) {
            glEnable(GL_BLEND);
            glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA,
                                GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
        } else {
            glDisable(GL_BLEND);
        }
        if (render_state.cull != PreviewCullMode::None) {
            glEnable(GL_CULL_FACE);
            glCullFace(render_state.cull == PreviewCullMode::Back ?
                           GL_BACK : GL_FRONT);
        }
        if (original_pipeline) {
            const auto &pipeline = *packet.captured_pipeline;
            ApplyOriginalGlRaster(pipeline.raster, packet.height);
            const bool upload =
                !pipeline_uploaded ||
                uploaded_pipeline_digest != packet.pipeline_digest;
            for (size_t attribute = 0; attribute < 16; ++attribute) {
                const auto &captured_attribute = pipeline.attributes[attribute];
                const auto &stream = captured_attribute.stream;
                if (!captured_attribute.enabled) {
                    glDisableVertexAttribArray(attribute);
                    if (!stream.bytes.empty()) {
                        std::array<float, 4> current;
                        std::memcpy(current.data(), stream.bytes.data(),
                                    sizeof(current));
                        glVertexAttrib4fv(attribute, current.data());
                    }
                    continue;
                }
                glBindBuffer(GL_ARRAY_BUFFER, raw_attribute_buffers[attribute]);
                if (upload) {
                    const size_t prefix =
                        size_t(pipeline.first_vertex) * stream.stride;
                    std::vector<uint8_t> bytes(prefix + stream.bytes.size());
                    std::copy(stream.bytes.begin(), stream.bytes.end(),
                              bytes.begin() + prefix);
                    glBufferData(GL_ARRAY_BUFFER, bytes.size(), bytes.data(),
                                 GL_STATIC_DRAW);
                }
                glEnableVertexAttribArray(attribute);
                if (stream.integer)
                    glVertexAttribIPointer(attribute, stream.components,
                                           stream.format, stream.stride,
                                           nullptr);
                else
                    glVertexAttribPointer(attribute, stream.components,
                                          stream.format, stream.normalized,
                                          stream.stride, nullptr);
            }
            if (!pipeline.indices.empty()) {
                glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, raw_index_buffer);
                if (upload)
                    glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                                 pipeline.indices.size() * 4,
                                 pipeline.indices.data(), GL_STATIC_DRAW);
                BeginDrawQuery(work, 1);
                glDrawElements(pipeline.host_topology, pipeline.indices.size(),
                               GL_UNSIGNED_INT, nullptr);
            } else {
                BeginDrawQuery(work, pipeline.ranges.size());
                for (const auto &range : pipeline.ranges)
                    glDrawArrays(pipeline.host_topology, range[0], range[1]);
            }
            EndDrawQuery(work.slot);
            pipeline_uploaded = true;
            uploaded_pipeline_digest = packet.pipeline_digest;
        } else {
            BeginDrawQuery(work, 1);
            glDrawArrays(GL_TRIANGLES, target_draw.first_vertex,
                         static_cast<GLsizei>(target_draw.vertex_count));
            EndDrawQuery(work.slot);
        }
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, slot.texture);
        SetPreviewGlOutputChannel(work.result_key.channel);
        GLenum gl_error = glGetError();
        if (gl_error != GL_NO_ERROR) {
            *error = "Preview GL preview draw failed with error " +
                     std::to_string(gl_error);
            return false;
        }
        GLsync producer = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
        if (!producer) {
            *error = "Preview GL producer fence allocation failed";
            return false;
        }
        glFlush();
        while (!stop.load(std::memory_order_acquire)) {
            GLenum result = glClientWaitSync(producer, 0, 0);
            if (result == GL_ALREADY_SIGNALED ||
                result == GL_CONDITION_SATISFIED) {
                glDeleteSync(producer);
                PollDrawQuery(draw_queries[work.slot]);
                std::lock_guard<std::mutex> lock(slots_mutex);
                slot.generation = work.slot_generation;
                if (captured || original_pipeline)
                    *error = material_status;
                return true;
            }
            if (result == GL_WAIT_FAILED) {
                glDeleteSync(producer);
                *error = "Preview GL producer fence wait failed";
                return false;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        glDeleteSync(producer);
        *error = "Preview GL preview stopped";
        return false;
    }

    bool UploadPixels(const PreviewWorkItem &work,
                      const std::vector<uint8_t> &rgba, std::string *error)
    {
        if (!work.packet || work.slot >= slots.size() ||
            rgba.size() != size_t(work.packet->width) * work.packet->height * 4) {
            *error = "Preview presentation data is incomplete";
            return false;
        }
        Slot &slot = slots[work.slot];
        if (!slot.texture) glGenTextures(1, &slot.texture);
        glBindTexture(GL_TEXTURE_2D, slot.texture);
        // CPU diagnostics and Vulkan bytes have already selected their channel.
        SetPreviewGlOutputChannel(PreviewChannel::FinalRGBA);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, work.packet->width,
                     work.packet->height, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                     rgba.data());
        slot.width = work.packet->width;
        slot.height = work.packet->height;
        if (glGetError() != GL_NO_ERROR) {
            *error = "Preview GL presentation upload failed";
            return false;
        }
        GLsync producer = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
        if (!producer) {
            *error = "Preview GL presentation fence failed";
            return false;
        }
        glFlush();
        while (!stop.load(std::memory_order_acquire)) {
            GLenum result = glClientWaitSync(producer, 0, 0);
            if (result == GL_ALREADY_SIGNALED ||
                result == GL_CONDITION_SATISFIED) {
                glDeleteSync(producer);
                std::lock_guard<std::mutex> lock(slots_mutex);
                slot.generation = work.slot_generation;
                return true;
            }
            if (result == GL_WAIT_FAILED) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        glDeleteSync(producer);
        *error = "Preview GL presentation stopped or failed";
        return false;
    }

    void Run(std::promise<std::string> startup)
    {
        if (!bind_context(window, context)) {
            startup.set_value(
                std::string("Preview GL worker context bind failed: ") +
                SDL_GetError());
            return;
        }
        startup.set_value({});
        PreviewService &service = GetPreviewService();
        PreviewVkExecutor vulkan;
        std::vector<uint8_t> completed_pixels;
        while (!stop.load(std::memory_order_acquire)) {
            for (auto &query : draw_queries)
                if (PollDrawQuery(query)) {
                    PublishTiming(query.work, query.timing);
                    service.CompleteDrawTiming(query.work, query.timing);
                }
            PreviewWorkItem timed_work;
            PreviewDrawTiming available_timing;
            while (vulkan.PollDrawTiming(&timed_work, &available_timing)) {
                PublishTiming(timed_work, available_timing);
                service.CompleteDrawTiming(timed_work, available_timing);
            }
            PumpOutputCopies();
            PreviewWorkItem work{};
            if (!service.TryClaimWork(NowNs(), &work,
                                      PreviewBackend::OpenGL) &&
                !service.TryClaimWork(NowNs(), &work,
                                      PreviewBackend::Vulkan)) {
                SDL_DelayNS(UINT64_C(1000000));
                continue;
            }
            std::string error;
            if (work.kind == PreviewWorkKind::Prepare) {
                bool unsupported = false;
                bool cancelled = false;
                bool ok = work.packet->selection.backend == PreviewBackend::Vulkan ?
                    vulkan.Prepare(work, &error, &unsupported, &cancelled,
                                   [&service, &work] {
                        return service.PreparationStillAllowed(work.token);
                    }) :
                    Prepare(work, &error, &unsupported);
                const PreviewPreparationOutcome outcome = ok ?
                    PreviewPreparationOutcome::Succeeded :
                    (cancelled ? PreviewPreparationOutcome::Cancelled :
                     unsupported ? PreviewPreparationOutcome::Unsupported :
                                   PreviewPreparationOutcome::Failed);
                service.CompletePreparation(work.token, outcome, error,
                                            NowNs());
            } else if (work.kind == PreviewWorkKind::Render) {
                PreviewDrawTiming timing;
                bool ok =
                    work.packet->selection.backend == PreviewBackend::Vulkan ?
                        (vulkan.Render(work, stop, &completed_pixels, &error,
                                       &timing) &&
                         UploadPixels(work, completed_pixels, &error)) :
                        Render(work, &error);
                if (work.packet->selection.backend == PreviewBackend::OpenGL)
                    timing = draw_queries[work.slot].timing;
                if (ok) {
                    std::lock_guard<std::mutex> lock(slots_mutex);
                    slots[work.slot].published_result = work.result_key;
                    slots[work.slot].published_timing = timing;
                }
                service.CompleteRender(work.token, ok, error, NowNs(), &timing);
            }
        }
        // Output textures belong to the HUD until its last sampling retires.
        // Shutdown deletes them on the consumer context after joining us.
        for (auto &query : draw_queries) {
            if (query.objects[0])
                glDeleteQueries(2, query.objects);
            query = {};
        }
        if (fixture_texture[0])
            glDeleteTextures(4, fixture_texture);
        if (depth_buffer) glDeleteRenderbuffers(1, &depth_buffer);
        if (fbo) glDeleteFramebuffers(1, &fbo);
        if (vbo) glDeleteBuffers(1, &vbo);
        if (raw_attribute_buffers[0])
            glDeleteBuffers(16, raw_attribute_buffers.data());
        if (raw_index_buffer)
            glDeleteBuffers(1, &raw_index_buffer);
        if (vao) glDeleteVertexArrays(1, &vao);
        if (program) glDeleteProgram(program);
        if (reference_program) glDeleteProgram(reference_program);
        SDL_GL_MakeCurrent(nullptr, nullptr);
    }

    void RetireDisplayed()
    {
        if (!has_displayed) return;
        GetPreviewService().ReleaseDisplayLease(displayed.slot,
                                                displayed.slot_generation);
        retirements.push_back(
            { displayed, displayed_fence, displayed_fence_failed });
        displayed_fence_failed = false;
        displayed_fence = nullptr;
        displayed_texture = 0;
        has_displayed = false;
    }

    void RetireFrozen()
    {
        if (!has_frozen) return;
        GetPreviewService().ReleaseDisplayLease(frozen.slot,
                                                frozen.slot_generation);
        retirements.push_back({ frozen, frozen_fence, frozen_fence_failed });
        frozen_fence_failed = false;
        frozen_fence = nullptr;
        frozen_texture = 0;
        has_frozen = false;
    }

    void PollRetirements()
    {
        auto it = retirements.begin();
        while (it != retirements.end()) {
            // A failed fence allocation is not retirement proof. Keep this
            // bounded slot owned until terminal teardown rather than reuse it.
            if (it->fence_failed || CopyPending(it->frame)) {
                ++it;
                continue;
            }
            GLenum result = it->fence ? glClientWaitSync(it->fence, 0, 0) :
                                        GL_ALREADY_SIGNALED;
            if (result != GL_ALREADY_SIGNALED &&
                result != GL_CONDITION_SATISFIED) {
                ++it;
                continue;
            }
            if (it->fence) glDeleteSync(it->fence);
            GetPreviewService().CompleteDisplayRetirement(
                it->frame.slot, it->frame.slot_generation);
            it = retirements.erase(it);
        }
    }
};

bool MakePreviewHudContextCurrent(SDL_Window *window, void *context)
{
    if (!window || !context) {
        SDL_SetError("HUD cleanup requires a real GL window and context");
        return false;
    }
    auto gl_context = static_cast<SDL_GLContext>(context);
    if (!SDL_GL_MakeCurrent(window, gl_context))
        return false;
    if (SDL_GL_GetCurrentWindow() != window ||
        SDL_GL_GetCurrentContext() != gl_context) {
        SDL_SetError("HUD cleanup GL context switch did not become current");
        return false;
    }
    return true;
}

PreviewGlExecutor::PreviewGlExecutor(ContextBinder bind_context) :
    impl_(new Impl)
{
    impl_->bind_context = bind_context ? bind_context :
        [](SDL_Window *window, void *context) {
            return SDL_GL_MakeCurrent(window,
                                      static_cast<SDL_GLContext>(context));
        };
}
PreviewGlExecutor::~PreviewGlExecutor() { Shutdown(); delete impl_; }

bool PreviewGlExecutor::StartWhilePaused(std::string *error)
{
    Impl &impl = *impl_;
    if (impl.worker.joinable()) return true;
    SDL_Window *original_window = SDL_GL_GetCurrentWindow();
    SDL_GLContext original_context = SDL_GL_GetCurrentContext();
    if (!original_window || !original_context) {
        const std::string failure = "HUD OpenGL context is unavailable";
        GetPreviewService().ReportWorkerStartupFailure(failure, NowNs());
        if (error) *error = failure;
        return false;
    }
    SDL_GL_SetAttribute(SDL_GL_SHARE_WITH_CURRENT_CONTEXT, 1);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,
                        SDL_GL_CONTEXT_PROFILE_CORE);
    impl.window = SDL_CreateWindow("xemu preview shader preview", 16, 16,
                                   SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
    if (impl.window) impl.context = SDL_GL_CreateContext(impl.window);
    const std::string creation_error = SDL_GetError();
    SDL_GL_SetAttribute(SDL_GL_SHARE_WITH_CURRENT_CONTEXT, 0);
    const bool restored = SDL_GL_MakeCurrent(original_window,
                                             original_context);
    if (!impl.window || !impl.context || !restored) {
        if (impl.context) SDL_GL_DestroyContext(impl.context);
        if (impl.window) SDL_DestroyWindow(impl.window);
        impl.context = nullptr;
        impl.window = nullptr;
        const std::string failure =
            "Preview GL context creation failed: " + creation_error;
        GetPreviewService().ReportWorkerStartupFailure(failure, NowNs());
        if (error) *error = failure;
        return false;
    }
    impl.stop.store(false, std::memory_order_release);
    std::promise<std::string> startup;
    auto ready = startup.get_future();
    impl.worker = std::thread([&impl, signal = std::move(startup)]() mutable {
        impl.Run(std::move(signal));
    });
    const std::string startup_error = ready.get();
    if (!startup_error.empty()) {
        impl.worker.join();
        SDL_GL_DestroyContext(impl.context);
        SDL_DestroyWindow(impl.window);
        impl.context = nullptr;
        impl.window = nullptr;
        GetPreviewService().ReportWorkerStartupFailure(startup_error,
                                                       NowNs());
        if (error) *error = startup_error;
        return false;
    }
    if (error) error->clear();
    return true;
}

void PreviewGlExecutor::DrawImage(float max_width, float max_height,
                                  const PreviewSelection *selection,
                                  uint64_t now_ns,
                                  PreviewViewSettings *view,
                                  PreviewScene *scene)
{
    Impl &impl = *impl_;
    impl.PollRetirements();
    PreviewStatus status;
    GetPreviewService().CopyStatus(&status);
    if (!selection || !impl.worker.joinable() || !status.enabled ||
        !status.visible) {
        impl.RetireDisplayed();
        impl.RetireFrozen();
        const ImVec2 origin = ImGui::GetCursorScreenPos();
        const ImVec2 extent(std::max(1.0f, max_width),
                            std::max(1.0f, max_height));
        ImGui::InvisibleButton("##preview-inactive", extent);
        ImDrawList *draw = ImGui::GetWindowDrawList();
        const ImVec2 end(origin.x + extent.x, origin.y + extent.y);
        draw->AddRectFilled(origin, end, IM_COL32(24, 37, 45, 255));
        for (float x = origin.x; x < end.x; x += 32.0f)
            draw->AddLine(ImVec2(x, origin.y), ImVec2(x, end.y),
                          IM_COL32(61, 82, 91, 90));
        for (float y = origin.y; y < end.y; y += 32.0f)
            draw->AddLine(ImVec2(origin.x, y), ImVec2(end.x, y),
                          IM_COL32(61, 82, 91, 90));
        const char *title = "PREVIEW INACTIVE";
        const char *hint = selection ? "Start preview to render this shader" :
                                       "Select a pixel shader to begin";
        const ImVec2 title_size = ImGui::CalcTextSize(title);
        const ImVec2 hint_size = ImGui::CalcTextSize(hint);
        const float center_x = origin.x + extent.x * 0.5f;
        const float center_y = origin.y + extent.y * 0.5f;
        draw->AddText(ImVec2(center_x - title_size.x * 0.5f,
                             center_y - title_size.y),
                      IM_COL32(194, 221, 205, 255), title);
        draw->AddText(ImVec2(center_x - hint_size.x * 0.5f,
                             center_y + 8.0f),
                      IM_COL32(145, 169, 178, 255), hint);
        return;
    }
    if (impl.has_frozen &&
        !SamePreviewDisplayScope(impl.frozen.result_key.compile.selection,
                                 *selection)) {
        impl.RetireFrozen();
    }
    PreviewFrameRef ready{};
    if (GetPreviewService().TryAcquireReadyFrame(&ready, now_ns)) {
        GLuint texture = 0;
        {
            std::lock_guard<std::mutex> lock(impl.slots_mutex);
            const Impl::Slot &slot = impl.slots[ready.slot];
            if (slot.generation == ready.slot_generation) {
                texture = slot.texture;
            }
        }
        if (!texture) {
            GetPreviewService().ReleaseDisplayLease(ready.slot,
                                                     ready.slot_generation);
            GetPreviewService().CompleteDisplayRetirement(
                ready.slot, ready.slot_generation);
        } else {
            impl.RetireDisplayed();
            impl.displayed = ready;
            impl.displayed_texture = texture;
            impl.has_displayed = true;
        }
    }
    if (impl.has_displayed &&
        !SamePreviewDisplayScope(impl.displayed.result_key.compile.selection,
                                 *selection)) {
        impl.RetireDisplayed();
    }
    if (!impl.has_displayed && !impl.has_frozen) {
        ImGui::TextDisabled("Waiting for a preview result");
        return;
    }

    PreviewViewSettings default_view{};
    if (!view) view = &default_view;
    const float zoom = std::clamp(view->zoom, 1.0f, 8.0f);
    const float half = 0.5f / zoom;
    view->center[0] = std::clamp(view->center[0], half, 1.0f - half);
    view->center[1] = std::clamp(view->center[1], half, 1.0f - half);
    const ImVec2 uv0(view->center[0] - half, view->center[1] + half);
    const ImVec2 uv1(view->center[0] + half, view->center[1] - half);
    const float lane_width = impl.has_frozen ?
        std::max(1.0f, (max_width - 8.0f) * 0.5f) :
        std::max(1.0f, max_width);
    auto draw = [&](const char *label, const PreviewFrameRef &frame,
                    GLuint texture, bool *sampled, bool pannable) {
        const float aspect = float(std::max(1U, frame.height)) /
                             float(std::max(1U, frame.width));
        const float image_width = std::max(1.0f, std::min(
            lane_width, max_height / aspect));
        const float image_height = image_width * aspect;
        const PreviewChannel channel = frame.result_key.channel;
        const auto &origin = frame.result_key.compile;
        const bool stale =
            pannable &&
            (!status.has_packet || status.state == PreviewState::Failed ||
             status.state == PreviewState::Unsupported ||
             (status.has_attempt && origin != status.attempted_compile));
        ImGui::BeginGroup();
        ImGui::Text("%s%s: %s", label, stale ? " — STALE" : "",
                    PreviewModeLabel(origin.selection.mode));
        const std::string full_source = PreviewSourceIdentity(origin);
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Source: %s\nChannel: %s\n%s",
                full_source.c_str(), PreviewChannelLabel(channel),
                PreviewChannelProvenance(channel));
        }
        const ImVec2 position = ImGui::GetCursorScreenPos();
        ImDrawList *list = ImGui::GetWindowDrawList();
        const float tile_x = image_width / 8.0f;
        const float tile_y = image_height / 8.0f;
        for (int y = 0; y < 8; ++y) {
            for (int x = 0; x < 8; ++x) {
                list->AddRectFilled(
                    ImVec2(position.x + x * tile_x,
                           position.y + y * tile_y),
                    ImVec2(position.x + (x + 1) * tile_x,
                           position.y + (y + 1) * tile_y),
                    (x + y) & 1 ? IM_COL32(170, 170, 170, 255) :
                                  IM_COL32(90, 90, 90, 255));
            }
        }
        ImGui::Image((ImTextureID)(intptr_t)texture,
                     ImVec2(image_width, image_height), uv0, uv1);
        *sampled = true;
        if (pannable && scene &&
            HandleWorkbenchViewportGesture(scene, position,
                                           ImVec2(image_width, image_height))) {
            GetPreviewService().EditScene(*scene);
        }
        if (!scene && pannable && ImGui::IsItemHovered() &&
            ImGui::IsMouseDown(ImGuiMouseButton_Left) && zoom > 1.0f) {
            const ImVec2 delta = ImGui::GetIO().MouseDelta;
            view->center[0] = std::clamp(
                view->center[0] - delta.x / (image_width * zoom),
                half, 1.0f - half);
            view->center[1] = std::clamp(
                view->center[1] + delta.y / (image_height * zoom),
                half, 1.0f - half);
        }
        ImGui::EndGroup();
    };
    if (impl.has_frozen) {
        draw("Reference (frozen)", impl.frozen, impl.frozen_texture,
             &impl.frozen_sampled_this_frame, false);
        ImGui::SameLine();
    }
    if (impl.has_displayed) {
        draw("Current", impl.displayed, impl.displayed_texture,
             &impl.sampled_this_frame, true);
    } else if (impl.has_frozen) {
        // Freeze transfers one lease; until a new Current arrives both views
        // sample the same last-good texture, covered by the Reference fence.
        draw("Current", impl.frozen, impl.frozen_texture,
             &impl.frozen_sampled_this_frame, true);
    }
}

bool PreviewGlExecutor::HasDisplayed() const
{
    return impl_->has_displayed;
}

bool PreviewGlExecutor::HasFrozen() const
{
    return impl_->has_frozen;
}

bool PreviewGlExecutor::NeedsRetirementPump() const
{
    return impl_->has_displayed || impl_->has_frozen ||
           !impl_->retirements.empty() || !impl_->copied_frames.empty();
}

bool PreviewGlExecutor::CopyReadyImage(const PreviewResultKey &expected,
                                       uint32_t *width, uint32_t *height,
                                       std::vector<uint8_t> *rgba,
                                       std::string *error,
                                       PreviewDrawTiming *draw_timing)
{
    if (error)
        error->clear();
    if (!width || !height || !rgba)
        return false;
    Impl &impl = *impl_;
    auto cached = [&]() {
        std::lock_guard<std::mutex> lock(impl.slots_mutex);
        for (const auto &slot : impl.slots) {
            if (!slot.copy_ready || slot.copy_frame.result_key != expected)
                continue;
            if (!slot.copy_error.empty()) {
                if (error)
                    *error = slot.copy_error;
                return false;
            }
            *width = slot.copy_frame.width;
            *height = slot.copy_frame.height;
            *rgba = slot.copy_pixels;
            if (draw_timing)
                *draw_timing = slot.copy_timing;
            return true;
        }
        return false;
    };
    if (cached())
        return true;
    if (error && !error->empty())
        return false;
    PreviewFrameRef frame;
    if (impl.has_displayed && impl.displayed.result_key == expected) {
        frame = impl.displayed;
        impl.sampled_this_frame = true;
    } else {
        auto existing = std::find_if(
            impl.copied_frames.begin(), impl.copied_frames.end(),
            [&](const auto &copy) { return copy.result_key == expected; });
        if (existing != impl.copied_frames.end()) {
            frame = *existing;
        } else {
            if (!GetPreviewService().TryAcquireReadyFrame(&frame, NowNs()))
                return false;
            impl.copied_frames.push_back(frame);
            if (frame.result_key != expected)
                return false;
        }
    }
    if (!PreviewExtentWithinLimits(frame.width, frame.height, true) ||
        !impl.worker.joinable()) {
        if (error)
            *error = "Comparison readback requires a live producing worker";
        return false;
    }
    std::lock_guard<std::mutex> lock(impl.slots_mutex);
    if (frame.slot >= impl.slots.size())
        return false;
    auto &slot = impl.slots[frame.slot];
    if (slot.generation != frame.slot_generation || !slot.texture ||
        slot.published_result != expected)
        return false;
    if (!slot.copy_pending || slot.copy_frame.result_key != expected) {
        slot.copy_frame = frame;
        slot.copy_pending = true;
        slot.copy_ready = slot.copy_pixels_ready = false;
        slot.copy_pixels.clear();
        slot.copy_error.clear();
        slot.copy_timing = slot.published_timing;
    }
    return false;
}

bool PreviewGlExecutor::FreezeDisplayed()
{
    Impl &impl = *impl_;
    if (!impl.has_displayed) return false;
    impl.RetireFrozen();
    impl.frozen = impl.displayed;
    impl.frozen_texture = impl.displayed_texture;
    impl.frozen_fence = impl.displayed_fence;
    impl.frozen_fence_failed = impl.displayed_fence_failed;
    impl.displayed_fence_failed = false;
    impl.has_frozen = true;
    impl.frozen_sampled_this_frame = false;
    impl.displayed_fence = nullptr;
    impl.displayed_texture = 0;
    impl.has_displayed = false;
    impl.sampled_this_frame = false;
    GetPreviewService().RequestCurrentFrame();
    return true;
}

void PreviewGlExecutor::ClearFrozen()
{
    Impl &impl = *impl_;
    if (!impl.has_displayed && impl.has_frozen) {
        // Both views currently sample this one last-good lease. Clearing the
        // comparison must not discard Current when the attempted edit failed.
        impl.displayed = impl.frozen;
        impl.displayed_texture = impl.frozen_texture;
        impl.displayed_fence = impl.frozen_fence;
        impl.displayed_fence_failed = impl.frozen_fence_failed;
        impl.has_displayed = true;
        impl.sampled_this_frame = impl.frozen_sampled_this_frame;
        impl.frozen_texture = 0;
        impl.frozen_fence = nullptr;
        impl.frozen_fence_failed = false;
        impl.has_frozen = false;
        impl.frozen_sampled_this_frame = false;
    } else {
        impl.RetireFrozen();
    }
}

void PreviewGlExecutor::AfterHudRender()
{
    Impl &impl = *impl_;
    auto copied = impl.copied_frames.begin();
    bool released_copy = false;
    while (copied != impl.copied_frames.end()) {
        if (impl.CopyPending(*copied)) {
            ++copied;
            continue;
        }
        GLsync fence = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
        GetPreviewService().ReleaseDisplayLease(copied->slot,
                                                copied->slot_generation);
        impl.retirements.push_back({ *copied, fence, !fence });
        copied = impl.copied_frames.erase(copied);
        released_copy = true;
    }
    if (released_copy)
        glFlush();
    if (impl.has_displayed && impl.sampled_this_frame) {
        if (impl.displayed_fence) glDeleteSync(impl.displayed_fence);
        impl.displayed_fence = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
        impl.displayed_fence_failed = !impl.displayed_fence;
        glFlush();
    } else if (impl.has_displayed) {
        impl.RetireDisplayed();
    }
    impl.sampled_this_frame = false;
    if (impl.has_frozen && impl.frozen_sampled_this_frame) {
        if (impl.frozen_fence) glDeleteSync(impl.frozen_fence);
        impl.frozen_fence = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
        impl.frozen_fence_failed = !impl.frozen_fence;
        glFlush();
    } else if (impl.has_frozen) {
        impl.RetireFrozen();
    }
    impl.frozen_sampled_this_frame = false;
    impl.PollRetirements();
}

void PreviewGlExecutor::Shutdown(bool have_shared_context)
{
    Impl &impl = *impl_;
    impl.stop.store(true, std::memory_order_release);
    if (impl.worker.joinable()) impl.worker.join();
    impl.RetireDisplayed();
    impl.RetireFrozen();
    for (const auto &frame : impl.copied_frames) {
        GetPreviewService().ReleaseDisplayLease(frame.slot,
                                                frame.slot_generation);
        impl.retirements.push_back({ frame, nullptr, false });
    }
    impl.copied_frames.clear();
    const bool can_delete = have_shared_context && SDL_GL_GetCurrentContext();
    if (impl.context && !can_delete) {
        g_printerr("Preview: no usable shared HUD GL context; "
                   "leaving output objects to terminal SDL teardown\n");
    }
    // Terminal HUD cleanup only. Ordinary tab close/disable uses zero-time
    // PollRetirements and never waits. Bound the aggregate terminal fence wait.
    const uint64_t deadline = NowNs() + UINT64_C(1000000000);
    for (Impl::Retirement &retirement : impl.retirements) {
        if (can_delete && retirement.fence) {
            const uint64_t now = NowNs();
            glClientWaitSync(retirement.fence, GL_SYNC_FLUSH_COMMANDS_BIT,
                             now < deadline ? deadline - now : 0);
            glDeleteSync(retirement.fence);
        }
    }
    impl.retirements.clear();
    // GL 4.5 section 5.1.3: deletion does not destroy objects still used by
    // queued commands. Delete in the consuming context, including on timeout;
    // never recycle these texture objects or claim a fence has signaled.
    const bool had_backend = impl.context != nullptr;
    for (Impl::Slot &slot : impl.slots) {
        if (can_delete && slot.texture)
            glDeleteTextures(1, &slot.texture);
        slot = {};
    }
    if (had_backend)
        GetPreviewService().BackendDestroyed();
    impl.program = impl.reference_program = impl.vao = impl.vbo =
        impl.fbo = impl.depth_buffer = 0;
    impl.depth_width = impl.depth_height = 0;
    std::fill(std::begin(impl.fixture_texture), std::end(impl.fixture_texture),
              0);
    impl.has_program = false;
    impl.raw_attribute_buffers.fill(0);
    impl.raw_index_buffer = 0;
    impl.pipeline_uploaded = false;
    impl.program_key = {};
    impl.sampled_this_frame = impl.frozen_sampled_this_frame = false;
    if (impl.context) SDL_GL_DestroyContext(impl.context);
    if (impl.window) SDL_DestroyWindow(impl.window);
    impl.context = nullptr;
    impl.window = nullptr;
}

PreviewGlExecutor &GetPreviewGlExecutor()
{
    static PreviewGlExecutor executor;
    return executor;
}

} // namespace xemu::shader_browser

// SPDX-License-Identifier: GPL-2.0-or-later
#include "shader-browser-preview-gl.hh"

#include "shader-browser-preview-adapter.hh"
#include "shader-browser-preview-service.hh"

#include <SDL3/SDL.h>
#include <epoxy/gl.h>
#include <imgui.h>
#include <glib.h>

#include <array>
#include <algorithm>
#include <atomic>
#include <chrono>
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
        *error = "Private GL shader allocation failed";
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
    *error = std::string("Private GL shader compilation failed: ") +
             log.data();
    glDeleteShader(*shader);
    *shader = 0;
    return false;
}

struct Vertex {
    float position[2];
    float color[4];
    float uv[2];
};

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

bool CheckSyntheticUniformInterface(GLuint program, std::string *error)
{
    GLint count = 0;
    GLint max_name = 0;
    glGetProgramiv(program, GL_ACTIVE_UNIFORMS, &count);
    glGetProgramiv(program, GL_ACTIVE_UNIFORM_MAX_LENGTH, &max_name);
    if (count == 0) return true;
    if (count < 0 || max_name <= 0 || max_name > 1024) {
        *error = "Unsupported private shader uniform interface";
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
            *error = "Unsupported private shader uniform name";
            return false;
        }
        const GLuint index = static_cast<GLuint>(i);
        GLint block = -1;
        glGetActiveUniformsiv(program, 1, &index, GL_UNIFORM_BLOCK_INDEX,
                             &block);
        const std::string uniform(name.data(), static_cast<size_t>(length));
        if (block != -1) {
            *error = "Unsupported private shader uniform block: " + uniform;
            return false;
        }
        if (type == GL_SAMPLER_2D && size == 1 &&
            (uniform == "texSamp0" || uniform == "texSamp1" ||
             uniform == "texSamp2" || uniform == "texSamp3")) {
            continue;
        }
        if (!IsSyntheticUniformType(type)) {
            *error = "Unsupported private shader uniform input: " + uniform;
            return false;
        }
    }
    return true;
}

} // namespace

struct PreviewGlExecutor::Impl {
    struct Slot {
        GLuint texture = 0;
        uint32_t width = 0;
        uint32_t height = 0;
        uint64_t generation = 0;
    };
    struct Retirement {
        PreviewFrameRef frame;
        GLsync fence = nullptr;
    };

    SDL_Window *window = nullptr;
    SDL_GLContext context = nullptr;
    std::thread worker;
    std::atomic<bool> stop{false};
    std::mutex slots_mutex;
    std::array<Slot, kPreviewSlotCount> slots{};
    GLuint program = 0;
    GLuint vao = 0;
    GLuint vbo = 0;
    GLuint fbo = 0;
    GLuint fixture_texture = 0;
    PreviewCompileKey program_key;
    bool has_program = false;
    PreviewFrameRef displayed;
    GLuint displayed_texture = 0;
    GLsync displayed_fence = nullptr;
    bool has_displayed = false;
    bool sampled_this_frame = false;
    PreviewFrameRef frozen;
    GLuint frozen_texture = 0;
    GLsync frozen_fence = nullptr;
    bool has_frozen = false;
    bool frozen_sampled_this_frame = false;
    std::vector<Retirement> retirements;

    bool Prepare(const PreviewWorkItem &work, std::string *error,
                 bool *unsupported)
    {
        *unsupported = false;
        if (!work.packet ||
            work.packet->selection.backend != PreviewBackend::OpenGL ||
            work.packet->source.empty() ||
            work.packet->partner_source.empty()) {
            *error = "Private OpenGL preview requires copied fragment and vertex source";
            return false;
        }
        if (has_program && program_key == work.compile_key) return true;
        GLuint vertex = 0;
        GLuint fragment = 0;
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
            *error = "Private GL program allocation failed";
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
            std::array<char, 1024> log{};
            glGetProgramInfoLog(next, static_cast<GLsizei>(log.size()),
                                nullptr, log.data());
            *error = std::string("Private GL program link failed: ") +
                     log.data();
            glDeleteProgram(next);
            return false;
        }
        if (!CheckSyntheticUniformInterface(next, error)) {
            glDeleteProgram(next);
            *unsupported = true;
            return false;
        }
        if (program) glDeleteProgram(program);
        program = next;
        program_key = work.compile_key;
        has_program = true;
        if (!vao) glGenVertexArrays(1, &vao);
        if (!vbo) glGenBuffers(1, &vbo);
        if (!fbo) glGenFramebuffers(1, &fbo);
        if (!fixture_texture) {
            const uint8_t texels[] = {
                255, 255, 255, 255, 64, 64, 64, 255,
                64, 64, 64, 255, 255, 255, 255, 255
            };
            glGenTextures(1, &fixture_texture);
            glBindTexture(GL_TEXTURE_2D, fixture_texture);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 2, 2, 0, GL_RGBA,
                         GL_UNSIGNED_BYTE, texels);
        }
        return vao && vbo && fbo && fixture_texture;
    }

    bool Render(const PreviewWorkItem &work, std::string *error)
    {
        if (!work.packet || !has_program ||
            work.compile_key != program_key ||
            work.slot >= slots.size()) {
            *error = "Private GL preview inputs are incomplete";
            return false;
        }
        const PreviewPacket &packet = *work.packet;
        PreviewSyntheticFixture fixture{};
        if (!DecodePreviewSyntheticFixture(packet.fixture_bytes, &fixture,
                                           error)) {
            return false;
        }
        Slot &slot = slots[work.slot];
        if (!slot.texture) glGenTextures(1, &slot.texture);
        if (!slot.texture) {
            *error = "Private GL output texture allocation failed";
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
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                               GL_TEXTURE_2D, slot.texture, 0);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) !=
            GL_FRAMEBUFFER_COMPLETE) {
            *error = "Private GL framebuffer is incomplete";
            return false;
        }
        auto make_vertex = [&](float x, float y, float u, float v,
                               size_t corner) {
            Vertex vertex{};
            vertex.position[0] = x;
            vertex.position[1] = y;
            vertex.uv[0] = u * fixture.uv_scale[0] + fixture.uv_offset[0];
            vertex.uv[1] = v * fixture.uv_scale[1] + fixture.uv_offset[1];
            for (size_t c = 0; c < 4; ++c) {
                vertex.color[c] = fixture.corner_colors[corner][c] / 255.0f;
            }
            return vertex;
        };
        const Vertex tl = make_vertex(-1, 1, 0, 1, 0);
        const Vertex tr = make_vertex(1, 1, 1, 1, 1);
        const Vertex bl = make_vertex(-1, -1, 0, 0, 2);
        const Vertex br = make_vertex(1, -1, 1, 0, 3);
        const Vertex vertices[] = { tl, bl, tr, tr, bl, br };
        glViewport(0, 0, static_cast<GLsizei>(packet.width),
                   static_cast<GLsizei>(packet.height));
        glDisable(GL_BLEND);
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_CULL_FACE);
        glClearColor(0.08f, 0.08f, 0.08f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
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
        const GLfloat texture_scales[4] = {2.0f, 2.0f, 2.0f, 2.0f};
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
        if (location >= 0) glUniform1i(location, fixture.alpha_reference);
        glBindVertexArray(vao);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices,
                     GL_STREAM_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                              reinterpret_cast<const void *>(0));
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                              reinterpret_cast<const void *>(sizeof(float) * 2));
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                              reinterpret_cast<const void *>(sizeof(float) * 6));
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, fixture_texture);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 2, 2, GL_RGBA,
                        GL_UNSIGNED_BYTE, fixture.texture_texels.data());
        const GLint filter = fixture.linear_filter ? GL_LINEAR : GL_NEAREST;
        const GLint wrap = fixture.repeat_wrap ? GL_REPEAT : GL_CLAMP_TO_EDGE;
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrap);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrap);
        for (int unit = 0; unit < 4; ++unit) {
            glActiveTexture(GL_TEXTURE0 + unit);
            glBindTexture(GL_TEXTURE_2D, fixture_texture);
            std::string name = "texSamp" + std::to_string(unit);
            GLint sampler_location = glGetUniformLocation(program, name.c_str());
            if (sampler_location >= 0) glUniform1i(sampler_location, unit);
        }
        glDrawArrays(GL_TRIANGLES, 0, 6);
        GLenum gl_error = glGetError();
        if (gl_error != GL_NO_ERROR) {
            *error = "Private GL preview draw failed with error " +
                     std::to_string(gl_error);
            return false;
        }
        GLsync producer = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
        if (!producer) {
            *error = "Private GL producer fence allocation failed";
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
            if (result == GL_WAIT_FAILED) {
                glDeleteSync(producer);
                *error = "Private GL producer fence wait failed";
                return false;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        glDeleteSync(producer);
        *error = "Private GL preview stopped";
        return false;
    }

    void Run()
    {
        if (!SDL_GL_MakeCurrent(window, context)) return;
        PreviewService &service = GetPreviewService();
        while (!stop.load(std::memory_order_acquire)) {
            PreviewWorkItem work{};
            if (!service.TryClaimWork(NowNs(), &work,
                                      PreviewBackend::OpenGL)) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                continue;
            }
            std::string error;
            if (work.kind == PreviewWorkKind::Prepare) {
                bool unsupported = false;
                bool ok = Prepare(work, &error, &unsupported);
                const PreviewPreparationOutcome outcome = ok ?
                    PreviewPreparationOutcome::Succeeded :
                    (unsupported ? PreviewPreparationOutcome::Unsupported :
                                   PreviewPreparationOutcome::Failed);
                service.CompletePreparation(work.token, outcome, error,
                                            NowNs());
            } else if (work.kind == PreviewWorkKind::Render) {
                bool ok = Render(work, &error);
                service.CompleteRender(work.token, ok, error, NowNs());
            }
        }
        for (Slot &slot : slots) {
            if (slot.texture) glDeleteTextures(1, &slot.texture);
        }
        if (fixture_texture) glDeleteTextures(1, &fixture_texture);
        if (fbo) glDeleteFramebuffers(1, &fbo);
        if (vbo) glDeleteBuffers(1, &vbo);
        if (vao) glDeleteVertexArrays(1, &vao);
        if (program) glDeleteProgram(program);
        SDL_GL_MakeCurrent(nullptr, nullptr);
    }

    void RetireDisplayed()
    {
        if (!has_displayed) return;
        GetPreviewService().ReleaseDisplayLease(displayed.slot,
                                                displayed.slot_generation);
        retirements.push_back({displayed, displayed_fence});
        displayed_fence = nullptr;
        displayed_texture = 0;
        has_displayed = false;
    }

    void RetireFrozen()
    {
        if (!has_frozen) return;
        GetPreviewService().ReleaseDisplayLease(frozen.slot,
                                                frozen.slot_generation);
        retirements.push_back({frozen, frozen_fence});
        frozen_fence = nullptr;
        frozen_texture = 0;
        has_frozen = false;
    }

    void PollRetirements()
    {
        auto it = retirements.begin();
        while (it != retirements.end()) {
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

PreviewGlExecutor::PreviewGlExecutor() : impl_(new Impl) {}
PreviewGlExecutor::~PreviewGlExecutor() { Shutdown(); delete impl_; }

bool PreviewGlExecutor::StartWhilePaused(std::string *error)
{
    Impl &impl = *impl_;
    if (impl.worker.joinable()) return true;
    SDL_Window *original_window = SDL_GL_GetCurrentWindow();
    SDL_GLContext original_context = SDL_GL_GetCurrentContext();
    if (!original_window || !original_context) {
        if (error) *error = "HUD OpenGL context is unavailable";
        return false;
    }
    SDL_GL_SetAttribute(SDL_GL_SHARE_WITH_CURRENT_CONTEXT, 1);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,
                        SDL_GL_CONTEXT_PROFILE_CORE);
    impl.window = SDL_CreateWindow("xemu private shader preview", 16, 16,
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
        if (error) *error = "Private GL context creation failed: " +
                            creation_error;
        return false;
    }
    impl.stop.store(false, std::memory_order_release);
    impl.worker = std::thread([&impl] { impl.Run(); });
    if (error) error->clear();
    return true;
}

void PreviewGlExecutor::DrawImage(float side,
                                  const PreviewSelection *selection,
                                  uint64_t now_ns,
                                  PreviewViewSettings *view)
{
    Impl &impl = *impl_;
    impl.PollRetirements();
    if (!selection || selection->backend != PreviewBackend::OpenGL ||
        !impl.worker.joinable()) {
        impl.RetireDisplayed();
        impl.RetireFrozen();
        ImGui::TextDisabled("Private OpenGL preview is not prepared");
        return;
    }
    if (impl.has_frozen &&
        impl.frozen.result_key.compile.selection != *selection) {
        impl.RetireFrozen();
    }
    PreviewFrameRef ready{};
    if (GetPreviewService().TryAcquireReadyFrame(&ready, now_ns)) {
        impl.RetireDisplayed();
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
            impl.displayed = ready;
            impl.displayed_texture = texture;
            impl.has_displayed = true;
        }
    }
    if (impl.has_displayed &&
        impl.displayed.result_key.compile.selection != *selection) {
        impl.RetireDisplayed();
    }
    if (!impl.has_displayed && !impl.has_frozen) {
        ImGui::TextDisabled("Waiting for a private OpenGL result");
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
    ImVec4 tint(1.0f, 1.0f, 1.0f, 1.0f);
    switch (view->channel) {
    case 1: tint = ImVec4(1.0f, 0.0f, 0.0f, 1.0f); break;
    case 2: tint = ImVec4(0.0f, 1.0f, 0.0f, 1.0f); break;
    case 3: tint = ImVec4(0.0f, 0.0f, 1.0f, 1.0f); break;
    case 4: tint = ImVec4(0.0f, 0.0f, 0.0f, 1.0f); break;
    default: break;
    }
    const float image_side = impl.has_displayed && impl.has_frozen ?
        std::max(1.0f, (side - 8.0f) * 0.5f) : std::max(1.0f, side);
    auto draw = [&](const char *label, GLuint texture, bool *sampled,
                    bool pannable) {
        ImGui::BeginGroup();
        ImGui::TextDisabled("%s", label);
        const ImVec2 position = ImGui::GetCursorScreenPos();
        ImDrawList *list = ImGui::GetWindowDrawList();
        const float tile = image_side / 8.0f;
        for (int y = 0; y < 8; ++y) {
            for (int x = 0; x < 8; ++x) {
                list->AddRectFilled(
                    ImVec2(position.x + x * tile, position.y + y * tile),
                    ImVec2(position.x + (x + 1) * tile,
                           position.y + (y + 1) * tile),
                    (x + y) & 1 ? IM_COL32(170, 170, 170, 255) :
                                  IM_COL32(90, 90, 90, 255));
            }
        }
        ImGui::Image((ImTextureID)(intptr_t)texture,
                     ImVec2(image_side, image_side), uv0, uv1, tint);
        *sampled = true;
        if (pannable && ImGui::IsItemHovered() &&
            ImGui::IsMouseDown(ImGuiMouseButton_Left) && zoom > 1.0f) {
            const ImVec2 delta = ImGui::GetIO().MouseDelta;
            view->center[0] = std::clamp(
                view->center[0] - delta.x / (image_side * zoom),
                half, 1.0f - half);
            view->center[1] = std::clamp(
                view->center[1] + delta.y / (image_side * zoom),
                half, 1.0f - half);
        }
        ImGui::EndGroup();
    };
    if (impl.has_frozen) {
        draw("Frozen", impl.frozen_texture,
             &impl.frozen_sampled_this_frame, false);
        if (impl.has_displayed) ImGui::SameLine();
    }
    if (impl.has_displayed) {
        draw("Current", impl.displayed_texture,
             &impl.sampled_this_frame, true);
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
           !impl_->retirements.empty();
}

bool PreviewGlExecutor::FreezeDisplayed()
{
    Impl &impl = *impl_;
    if (!impl.has_displayed) return false;
    impl.RetireFrozen();
    impl.frozen = impl.displayed;
    impl.frozen_texture = impl.displayed_texture;
    impl.frozen_fence = impl.displayed_fence;
    impl.has_frozen = true;
    impl.frozen_sampled_this_frame = false;
    impl.displayed_fence = nullptr;
    impl.displayed_texture = 0;
    impl.has_displayed = false;
    impl.sampled_this_frame = false;
    return true;
}

void PreviewGlExecutor::ClearFrozen()
{
    impl_->RetireFrozen();
}

void PreviewGlExecutor::AfterHudRender()
{
    Impl &impl = *impl_;
    if (impl.has_displayed && impl.sampled_this_frame) {
        if (impl.displayed_fence) glDeleteSync(impl.displayed_fence);
        impl.displayed_fence = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
        glFlush();
    } else if (impl.has_displayed) {
        impl.RetireDisplayed();
    }
    impl.sampled_this_frame = false;
    if (impl.has_frozen && impl.frozen_sampled_this_frame) {
        if (impl.frozen_fence) glDeleteSync(impl.frozen_fence);
        impl.frozen_fence = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
        glFlush();
    } else if (impl.has_frozen) {
        impl.RetireFrozen();
    }
    impl.frozen_sampled_this_frame = false;
    impl.PollRetirements();
}

void PreviewGlExecutor::Shutdown()
{
    Impl &impl = *impl_;
    impl.stop.store(true, std::memory_order_release);
    if (impl.worker.joinable()) impl.worker.join();
    impl.RetireDisplayed();
    impl.RetireFrozen();
    for (Impl::Retirement &retirement : impl.retirements) {
        if (retirement.fence) glDeleteSync(retirement.fence);
        GetPreviewService().CompleteDisplayRetirement(
            retirement.frame.slot, retirement.frame.slot_generation);
    }
    impl.retirements.clear();
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

// SPDX-License-Identifier: GPL-2.0-or-later
#include "asset-browser-viewport.hh"
#include "asset-browser-material.hh"
#include "asset-browser-stage-renderer.hh"
#include "asset-browser-stage-source.hh"
#include "asset-browser-placement.hh"
#include <epoxy/gl.h>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <list>

namespace xemu::asset_browser {
namespace {
constexpr uint64_t kGpuBudget = 256U * 1024U * 1024U;
struct GlState {
    GLint draw_fbo, read_fbo, renderbuffer, program, vao, buffer, active;
    std::array<GLint, 4> texture{}, cube{}, sampler{};
    GLint blend[6]{}, front_face = 0, cull_face = 0;
    GLfloat blend_color[4]{}, polygon_factor = 0, polygon_units = 0;
    GLint stencil[2][7]{};
    GLint unpack_buffer, unpack_alignment, viewport[4], scissor[4];
    GLint polygon[2], depth_func, clear_stencil = 0;
    GLfloat clear_color[4];
    GLdouble clear_depth;
    GLboolean depth_mask, color_mask[4];
    static constexpr GLenum capabilities[] = {
        GL_BLEND,
        GL_DEPTH_TEST,
        GL_CULL_FACE,
        GL_SCISSOR_TEST,
        GL_STENCIL_TEST,
        GL_RASTERIZER_DISCARD,
        GL_FRAMEBUFFER_SRGB,
        GL_POLYGON_OFFSET_FILL,
        GL_POLYGON_OFFSET_LINE,
        GL_PRIMITIVE_RESTART,
        GL_PRIMITIVE_RESTART_FIXED_INDEX
    };
    std::array<GLboolean, 11> enabled{};
    GLint unpack[7]{}, clip_origin = GL_LOWER_LEFT,
                       clip_depth = GL_NEGATIVE_ONE_TO_ONE;
    GLdouble depth_range[2]{};
    static constexpr GLenum unpack_names[] = {
        GL_UNPACK_ROW_LENGTH,  GL_UNPACK_IMAGE_HEIGHT, GL_UNPACK_SKIP_ROWS,
        GL_UNPACK_SKIP_PIXELS, GL_UNPACK_SKIP_IMAGES,  GL_UNPACK_SWAP_BYTES,
        GL_UNPACK_LSB_FIRST
    };
    static constexpr GLenum extras[] = { GL_COLOR_LOGIC_OP,
                                         GL_DEPTH_CLAMP,
                                         GL_SAMPLE_ALPHA_TO_COVERAGE,
                                         GL_SAMPLE_COVERAGE,
                                         GL_SAMPLE_MASK,
                                         GL_CLIP_DISTANCE0,
                                         GL_CLIP_DISTANCE1,
                                         GL_CLIP_DISTANCE2,
                                         GL_CLIP_DISTANCE3,
                                         GL_CLIP_DISTANCE4,
                                         GL_CLIP_DISTANCE5,
                                         GL_CLIP_DISTANCE6,
                                         GL_CLIP_DISTANCE7,
                                         GL_TEXTURE_CUBE_MAP_SEAMLESS };
    std::array<GLboolean, 14> extra_enabled{};
    bool clip_control = false;
    bool fixed_restart = false;
    GlState()
    {
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &draw_fbo);
        glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &read_fbo);
        glGetIntegerv(GL_RENDERBUFFER_BINDING, &renderbuffer);
        glGetIntegerv(GL_CURRENT_PROGRAM, &program);
        glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &vao);
        glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &buffer);
        glGetIntegerv(GL_ACTIVE_TEXTURE, &active);
        for (size_t unit = 0; unit < 4; ++unit) {
            glActiveTexture(GL_TEXTURE0 + unit);
            glGetIntegerv(GL_TEXTURE_BINDING_2D, &texture[unit]);
            glGetIntegerv(GL_TEXTURE_BINDING_CUBE_MAP, &cube[unit]);
            glGetIntegeri_v(GL_SAMPLER_BINDING, unit, &sampler[unit]);
            glBindSampler(unit, 0);
        }
        glActiveTexture(GL_TEXTURE0);
        const GLenum blend_names[] = {
            GL_BLEND_SRC_RGB,   GL_BLEND_DST_RGB,      GL_BLEND_SRC_ALPHA,
            GL_BLEND_DST_ALPHA, GL_BLEND_EQUATION_RGB, GL_BLEND_EQUATION_ALPHA
        };
        for (size_t i = 0; i < 6; ++i)
            glGetIntegerv(blend_names[i], &blend[i]);
        glGetFloatv(GL_BLEND_COLOR, blend_color);
        glGetIntegerv(GL_FRONT_FACE, &front_face);
        glGetIntegerv(GL_CULL_FACE_MODE, &cull_face);
        glGetFloatv(GL_POLYGON_OFFSET_FACTOR, &polygon_factor);
        glGetFloatv(GL_POLYGON_OFFSET_UNITS, &polygon_units);
        const GLenum stencil_names[2][7] = {
            { GL_STENCIL_FUNC, GL_STENCIL_REF, GL_STENCIL_VALUE_MASK,
              GL_STENCIL_WRITEMASK, GL_STENCIL_FAIL, GL_STENCIL_PASS_DEPTH_FAIL,
              GL_STENCIL_PASS_DEPTH_PASS },
            { GL_STENCIL_BACK_FUNC, GL_STENCIL_BACK_REF,
              GL_STENCIL_BACK_VALUE_MASK, GL_STENCIL_BACK_WRITEMASK,
              GL_STENCIL_BACK_FAIL, GL_STENCIL_BACK_PASS_DEPTH_FAIL,
              GL_STENCIL_BACK_PASS_DEPTH_PASS }
        };
        for (size_t face = 0; face < 2; ++face)
            for (size_t i = 0; i < 7; ++i)
                glGetIntegerv(stencil_names[face][i], &stencil[face][i]);
        glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING, &unpack_buffer);
        glGetIntegerv(GL_UNPACK_ALIGNMENT, &unpack_alignment);
        glGetIntegerv(GL_VIEWPORT, viewport);
        glGetIntegerv(GL_SCISSOR_BOX, scissor);
        glGetIntegerv(GL_POLYGON_MODE, polygon);
        glGetIntegerv(GL_DEPTH_FUNC, &depth_func);
        glGetIntegerv(GL_STENCIL_CLEAR_VALUE, &clear_stencil);
        glGetFloatv(GL_COLOR_CLEAR_VALUE, clear_color);
        glGetDoublev(GL_DEPTH_CLEAR_VALUE, &clear_depth);
        glGetBooleanv(GL_DEPTH_WRITEMASK, &depth_mask);
        glGetBooleanv(GL_COLOR_WRITEMASK, color_mask);
        fixed_restart = epoxy_gl_version() >= 43 ||
                        epoxy_has_gl_extension("GL_ARB_ES3_compatibility");
        for (size_t i = 0; i < enabled.size(); ++i) {
            if (i == 10 && !fixed_restart)
                continue;
            enabled[i] = glIsEnabled(capabilities[i]);
            glDisable(capabilities[i]);
        }
        glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        for (size_t i = 0; i < 7; ++i) {
            glGetIntegerv(unpack_names[i], &unpack[i]);
            glPixelStorei(unpack_names[i], 0);
        }
        for (size_t i = 0; i < extra_enabled.size(); ++i) {
            extra_enabled[i] = glIsEnabled(extras[i]);
            glDisable(extras[i]);
        }
        glGetDoublev(GL_DEPTH_RANGE, depth_range);
        glDepthRange(0, 1);
        clip_control = epoxy_gl_version() >= 45 ||
                       epoxy_has_gl_extension("GL_ARB_clip_control");
        if (clip_control) {
            glGetIntegerv(GL_CLIP_ORIGIN, &clip_origin);
            glGetIntegerv(GL_CLIP_DEPTH_MODE, &clip_depth);
            glClipControl(GL_LOWER_LEFT, GL_NEGATIVE_ONE_TO_ONE);
        }
    }
    ~GlState()
    {
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, draw_fbo);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, read_fbo);
        glBindRenderbuffer(GL_RENDERBUFFER, renderbuffer);
        glUseProgram(program);
        glBindVertexArray(vao);
        glBindBuffer(GL_ARRAY_BUFFER, buffer);
        // Resizing/configuration may delete a previously returned viewport
        // texture that a caller still had bound. Never rebind its dead name.
        for (size_t unit = 0; unit < 4; ++unit) {
            glActiveTexture(GL_TEXTURE0 + unit);
            glBindTexture(GL_TEXTURE_2D,
                          texture[unit] && !glIsTexture(texture[unit]) ?
                              0 :
                              texture[unit]);
            glBindTexture(GL_TEXTURE_CUBE_MAP,
                          cube[unit] && !glIsTexture(cube[unit]) ? 0 :
                                                                   cube[unit]);
            glBindSampler(unit, sampler[unit]);
        }
        glActiveTexture(active);
        glBlendFuncSeparate(blend[0], blend[1], blend[2], blend[3]);
        glBlendEquationSeparate(blend[4], blend[5]);
        glBlendColor(blend_color[0], blend_color[1], blend_color[2],
                     blend_color[3]);
        glFrontFace(front_face);
        glCullFace(cull_face);
        glPolygonOffset(polygon_factor, polygon_units);
        for (size_t face = 0; face < 2; ++face) {
            const GLenum f = face ? GL_BACK : GL_FRONT;
            const auto *s = stencil[face];
            glStencilFuncSeparate(f, s[0], s[1], s[2]);
            glStencilMaskSeparate(f, s[3]);
            glStencilOpSeparate(f, s[4], s[5], s[6]);
        }
        glBindBuffer(GL_PIXEL_UNPACK_BUFFER, unpack_buffer);
        glPixelStorei(GL_UNPACK_ALIGNMENT, unpack_alignment);
        for (size_t i = 0; i < 7; ++i)
            glPixelStorei(unpack_names[i], unpack[i]);
        for (size_t i = 0; i < extra_enabled.size(); ++i) {
            if (extra_enabled[i])
                glEnable(extras[i]);
            else
                glDisable(extras[i]);
        }
        glDepthRange(depth_range[0], depth_range[1]);
        if (clip_control)
            glClipControl(clip_origin, clip_depth);
        glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
        glScissor(scissor[0], scissor[1], scissor[2], scissor[3]);
        glPolygonMode(GL_FRONT_AND_BACK, polygon[0]);
        glDepthFunc(depth_func);
        glClearColor(clear_color[0], clear_color[1], clear_color[2],
                     clear_color[3]);
        glClearDepth(clear_depth);
        glClearStencil(clear_stencil);
        glDepthMask(depth_mask);
        glColorMask(color_mask[0], color_mask[1], color_mask[2], color_mask[3]);
        for (size_t i = 0; i < enabled.size(); ++i) {
            if (i == 10 && !fixed_restart)
                continue;
            if (enabled[i])
                glEnable(capabilities[i]);
            else
                glDisable(capabilities[i]);
        }
    }
};
struct Target {
    GLuint fbo = 0, color = 0, depth = 0;
    uint32_t width = 0, height = 0;
    void Destroy()
    {
        glDeleteFramebuffers(1, &fbo);
        glDeleteTextures(1, &color);
        glDeleteRenderbuffers(1, &depth);
        *this = {};
    }
    bool Resize(uint32_t w, uint32_t h)
    {
        if (width == w && height == h && fbo)
            return true;
        Destroy();
        width = w;
        height = h;
        glGenFramebuffers(1, &fbo);
        glGenTextures(1, &color);
        glGenRenderbuffers(1, &depth);
        glBindTexture(GL_TEXTURE_2D, color);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA,
                     GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glBindRenderbuffer(GL_RENDERBUFFER, depth);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, w, h);
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                               GL_TEXTURE_2D, color, 0);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT,
                                  GL_RENDERBUFFER, depth);
        return glCheckFramebufferStatus(GL_FRAMEBUFFER) ==
               GL_FRAMEBUFFER_COMPLETE;
    }
};
GLuint Shader(GLenum type, const char *source, std::string &error)
{
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);
    GLint okay = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &okay);
    if (!okay) {
        char log[2048]{};
        glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
        error = log;
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}
} // namespace
struct AssetViewport::Impl {
    struct Mesh {
        std::weak_ptr<const AssetPart> part;
        GLuint vao = 0, vertices = 0, indices = 0, texture = 0;
        uint64_t bytes = 0, used = 0;
        int slot = -1;
        void Destroy()
        {
            glDeleteVertexArrays(1, &vao);
            glDeleteBuffers(1, &vertices);
            glDeleteBuffers(1, &indices);
            glDeleteTextures(1, &texture);
        }
    };
    struct Thumb {
        std::vector<std::weak_ptr<const AssetPart>> parts;
        Target target;
        AssetViewportFrame frame;
    };
    std::list<Mesh> meshes;
    std::list<Thumb> thumbs;
    struct Image {
        std::weak_ptr<const capture::CaptureOccurrence> event;
        int slot = 0;
        AssetViewportFrame frame;
    };
    std::list<Image> images;
    uint64_t image_bytes = 0;
    Target target, display_target;
    GLuint program = 0, background_vao = 0;
    AssetStageRenderer stages;
    uint64_t gpu_bytes = 0, tick = 0;
    uint64_t mesh_budget = kGpuBudget / 2, requested_budget = kGpuBudget;
    size_t thumbnail_capacity = 24;
    std::string error;
    bool Init()
    {
        if (program)
            return true;
        const char *vs = R"(#version 330 core
layout(location=0) in vec3 position;layout(location=1) in vec2 uv;
layout(location=2) in vec4 color;
uniform vec3 center;uniform float radius;uniform vec4 camera;
uniform vec2 pan;uniform bool background;uniform bool display;
uniform mat4 anchor_from_local;
out vec2 texcoord;out vec4 vertexcolor;
void main(){
if(background||display){vec2 p=vec2((gl_VertexID<<1)&2,gl_VertexID&2);
gl_Position=vec4(p*2.-1.,0,1);texcoord=p;vertexcolor=vec4(1);return;}
vec4 placed=anchor_from_local*vec4(position,1);
vec3 p=(placed.xyz/placed.w-center)/radius;
float cy=cos(camera.x),sy=sin(camera.x),cp=cos(camera.y),sp=sin(camera.y);
p=vec3(cy*p.x+sy*p.z,p.y,-sy*p.x+cy*p.z);
p=vec3(p.x,cp*p.y-sp*p.z,sp*p.y+cp*p.z);
gl_Position=vec4(p.x*camera.z+pan.x,p.y*camera.w+pan.y,-p.z*.25,1);
texcoord=uv;vertexcolor=color;})";
        const char *ps = R"(#version 330 core
in vec2 texcoord;in vec4 vertexcolor;uniform sampler2D image;
uniform int textured;uniform int colored;uniform bool background;
uniform bool display;uniform uint palette[256];
out vec4 outputColor;
void main(){
if(display){vec4 c=texture(image,texcoord);uvec3 i=uvec3(clamp(c.rgb,0.,1.)*255.);
outputColor=vec4(float(palette[i.r]&255u),float((palette[i.g]>>8)&255u),
float((palette[i.b]>>16)&255u),c.a*255.)/255.;return;}
if(background){float tile=mod(floor(gl_FragCoord.x/32.)+floor(gl_FragCoord.y/32.),2.);
outputColor=vec4(mix(vec3(.20,.035,.29),vec3(.66,.12,.88),tile),1);return;}
vec4 c=textured!=0?texture(image,texcoord):vec4(.72,.76,.82,1);
if(colored!=0)c*=vertexcolor;outputColor=vec4(c.rgb,1);})";
        GLuint vertex = Shader(GL_VERTEX_SHADER, vs, error),
               pixel = Shader(GL_FRAGMENT_SHADER, ps, error);
        if (!vertex || !pixel) {
            if (vertex)
                glDeleteShader(vertex);
            if (pixel)
                glDeleteShader(pixel);
            return false;
        }
        program = glCreateProgram();
        glAttachShader(program, vertex);
        glAttachShader(program, pixel);
        glLinkProgram(program);
        glDeleteShader(vertex);
        glDeleteShader(pixel);
        GLint okay = 0;
        glGetProgramiv(program, GL_LINK_STATUS, &okay);
        if (!okay) {
            char log[2048]{};
            glGetProgramInfoLog(program, sizeof(log), nullptr, log);
            error = log;
            glDeleteProgram(program);
            program = 0;
        } else {
            glGenVertexArrays(1, &background_vao);
        }
        return okay;
    }
    Mesh *Get(const SharedAssetPart &part, uint32_t backend, int slot)
    {
        for (auto &mesh : meshes)
            if (mesh.part.lock() == part && mesh.slot == slot) {
                mesh.used = ++tick;
                return &mesh;
            }
        if (part->vertices.empty() || part->indices.empty() ||
            part->indices.size() > INT32_MAX)
            return nullptr;
        auto texture = DecodeAssetTexture(*part, backend, slot);
        if (slot < -1)
            texture.rgba.clear();
        const uint64_t bytes = part->vertices.size() * sizeof(AssetVertex) +
                               part->indices.size() * sizeof(uint32_t) +
                               texture.rgba.size();
        if (bytes > mesh_budget) {
            error = "Part exceeds the asset GPU budget";
            return nullptr;
        }
        while (!meshes.empty() &&
               (gpu_bytes + bytes > mesh_budget || meshes.size() >= 256)) {
            auto victim = std::min_element(
                meshes.begin(), meshes.end(),
                [](const Mesh &a, const Mesh &b) { return a.used < b.used; });
            gpu_bytes -= victim->bytes;
            victim->Destroy();
            meshes.erase(victim);
        }
        meshes.emplace_back();
        auto &mesh = meshes.back();
        mesh.part = part;
        mesh.slot = slot;
        mesh.bytes = bytes;
        mesh.used = ++tick;
        glGenVertexArrays(1, &mesh.vao);
        glGenBuffers(1, &mesh.vertices);
        glGenBuffers(1, &mesh.indices);
        glBindVertexArray(mesh.vao);
        glBindBuffer(GL_ARRAY_BUFFER, mesh.vertices);
        glBufferData(GL_ARRAY_BUFFER,
                     part->vertices.size() * sizeof(AssetVertex),
                     part->vertices.data(), GL_STATIC_DRAW);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mesh.indices);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                     part->indices.size() * sizeof(uint32_t),
                     part->indices.data(), GL_STATIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(
            0, 3, GL_FLOAT, GL_FALSE, sizeof(AssetVertex),
            reinterpret_cast<void *>(offsetof(AssetVertex, position)));
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(
            1, 2, GL_FLOAT, GL_FALSE, sizeof(AssetVertex),
            reinterpret_cast<void *>(offsetof(AssetVertex, uv)));
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(
            2, 4, GL_FLOAT, GL_FALSE, sizeof(AssetVertex),
            reinterpret_cast<void *>(offsetof(AssetVertex, color)));
        if (!texture.rgba.empty()) {
            // OpenGL readback is stored top-down; restore the original storage
            // row order for the captured OpenGL UVs. Vulkan UV0 already
            // addresses the first captured row.
            if (backend == 1) {
                const size_t row = size_t(texture.width) * 4;
                for (size_t y = 0; y < texture.height / 2; ++y)
                    std::swap_ranges(texture.rgba.begin() + y * row,
                                     texture.rgba.begin() + (y + 1) * row,
                                     texture.rgba.begin() +
                                         (texture.height - 1 - y) * row);
            }
            glGenTextures(1, &mesh.texture);
            glBindTexture(GL_TEXTURE_2D, mesh.texture);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, texture.width,
                         texture.height, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                         texture.rgba.data());
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                            texture.min_filter);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER,
                            texture.mag_filter);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, texture.wrap_s);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, texture.wrap_t);
        }
        gpu_bytes += bytes;
        return &mesh;
    }
    AssetViewportFrame
    Draw(Target &output, const std::shared_ptr<const AssetAssembly> &assembly,
         const AssetCamera &camera, uint32_t w, uint32_t h, int slot, bool wire,
         bool captured = false, bool projected_output = false)
    {
        AssetViewportFrame frame;
        if (!assembly || !assembly->bounds.valid) {
            frame.message = "Select captured geometry to inspect";
            return frame;
        }
        if (!w || !h || w > 2048 || h > 2048 || slot < -3 || slot > 3) {
            frame.message = "Viewport settings exceed the supported limits";
            return frame;
        }
        if (!std::isfinite(camera.yaw) || !std::isfinite(camera.pitch) ||
            !std::isfinite(camera.zoom) || !std::isfinite(camera.pan_x) ||
            !std::isfinite(camera.pan_y)) {
            frame.message = "Camera contains non-finite values";
            return frame;
        }
        GlState saved;
        if (!Init() || !output.Resize(w, h)) {
            frame.message =
                error.empty() ? "Asset framebuffer unavailable" : error;
            return frame;
        }
        glBindFramebuffer(GL_FRAMEBUFFER, output.fbo);
        glViewport(0, 0, w, h);
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        glDepthMask(GL_TRUE);
        glDepthFunc(GL_LEQUAL);
        glEnable(GL_DEPTH_TEST);
        glClearColor(.035f, .045f, .065f, 1);
        glClearDepth(1);
        glStencilMask(~0U);
        glClearStencil(0);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT |
                GL_STENCIL_BUFFER_BIT);
        // One fullscreen draw supplies contrast for dark captured materials in
        // both the inspector and thumbnails. It does not change mesh colors.
        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
        glUseProgram(program);
        glUniform1i(glGetUniformLocation(program, "display"), 0);
        glDisable(GL_DEPTH_TEST);
        glUniform1i(glGetUniformLocation(program, "background"), 1);
        glBindVertexArray(background_vao);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        glUniform1i(glGetUniformLocation(program, "background"), 0);
        glEnable(GL_DEPTH_TEST);
        glPolygonMode(GL_FRONT_AND_BACK, wire ? GL_LINE : GL_FILL);
        if (captured) {
            const bool projected =
                projected_output || !assembly->captured_placement;
            auto bounds = assembly->bounds;
            if (projected) {
                bounds = {};
                for (const auto &part : assembly->parts) {
                    capture::Bounds3 output_bounds;
                    if (!part ||
                        !stages.OutputBounds(*part, assembly->context.backend,
                                             &output_bounds, &frame.message)) {
                        frame.message =
                            "Cannot frame captured shader output: " +
                            frame.message;
                        return frame;
                    }
                    for (size_t axis = 0; axis < 3; ++axis) {
                        bounds.minimum[axis] =
                            bounds.valid ?
                                std::min(bounds.minimum[axis],
                                         output_bounds.minimum[axis]) :
                                output_bounds.minimum[axis];
                        bounds.maximum[axis] =
                            bounds.valid ?
                                std::max(bounds.maximum[axis],
                                         output_bounds.maximum[axis]) :
                                output_bounds.maximum[axis];
                    }
                    bounds.valid = true;
                }
            }
            const auto from_anchor = BuildAssetCameraMatrix(
                bounds, camera.yaw, camera.pitch, camera.zoom, camera.pan_x,
                camera.pan_y, float(w) / h);
            const auto from_clip =
                projected ?
                    from_anchor :
                    MultiplyAssetMatrices(from_anchor,
                                          assembly->local_from_captured_clip);
            for (const auto &part : assembly->parts) {
                if (!part || part->status != AssetStatus::Ready ||
                    !stages.DrawPart(*part, assembly->context.backend,
                                     from_clip, w, h, &frame.message,
                                     projected)) {
                    frame.message =
                        "Captured-stage view incomplete: " + frame.message;
                    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
                    glDepthMask(GL_TRUE);
                    glDisable(GL_SCISSOR_TEST);
                    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
                    return frame;
                }
                ++frame.captured_parts;
            }
            frame.drawn_parts = frame.captured_parts;
            frame.projected_output = projected;
            frame.texture = output.color;
            const capture::SharedCaptureBlock *palette = nullptr;
            size_t present = 0;
            for (const auto &part : assembly->parts) {
                size_t count = 0;
                for (const auto &blob : part->occurrence->inputs.blobs)
                    if (blob.name == "display.dac_palette") {
                        ++count;
                        if (!blob.data || blob.data->bytes.size() != 768 ||
                            (palette &&
                             (*palette)->bytes != blob.data->bytes)) {
                            frame.texture = 0;
                            frame.message =
                                "Captured display palette is invalid "
                                "or differs between assembly parts";
                            return frame;
                        }
                        palette = &blob.data;
                    }
                if (count > 1) {
                    frame.texture = 0;
                    frame.message = "Duplicate captured display palette";
                    return frame;
                }
                present += count;
            }
            if (present && present != assembly->parts.size()) {
                frame.texture = 0;
                frame.message =
                    "Some assembly parts lack a captured display palette";
                return frame;
            }
            if (palette) {
                if (!display_target.Resize(w, h)) {
                    frame.texture = 0;
                    frame.message = "Captured display target is unavailable";
                    return frame;
                }
                std::array<uint32_t, 256> entries;
                const auto &bytes = (*palette)->bytes;
                for (size_t i = 0; i < entries.size(); ++i)
                    entries[i] = uint32_t(bytes[i * 3]) |
                                 uint32_t(bytes[i * 3 + 1]) << 8 |
                                 uint32_t(bytes[i * 3 + 2]) << 16;
                glBindFramebuffer(GL_FRAMEBUFFER, display_target.fbo);
                glDisable(GL_DEPTH_TEST);
                glDisable(GL_STENCIL_TEST);
                glDisable(GL_BLEND);
                glDisable(GL_CULL_FACE);
                glDisable(GL_SCISSOR_TEST);
                glDisable(GL_POLYGON_OFFSET_FILL);
                glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
                glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
                glUseProgram(program);
                glUniform1i(glGetUniformLocation(program, "display"), 1);
                glUniform1i(glGetUniformLocation(program, "image"), 0);
                glUniform1uiv(glGetUniformLocation(program, "palette"), 256,
                              entries.data());
                glActiveTexture(GL_TEXTURE0);
                glBindSampler(0, 0);
                glBindTexture(GL_TEXTURE_2D, output.color);
                glBindVertexArray(background_vao);
                glDrawArrays(GL_TRIANGLES, 0, 3);
                frame.texture = display_target.color;
            }
            frame.width = w;
            frame.height = h;
            frame.gpu_bytes = gpu_bytes + stages.GpuBytes();
            frame.message = "Captured VS/PS/material inputs in one assembly "
                            "target. Inspection camera, window clipping and "
                            "raster depth are overridden; scene/destination "
                            "dependencies are not reproduced.";
            frame.message +=
                palette ? " Captured display palette applied after blending." :
                          " Display palette missing; colors are before display "
                          "correction.";
            if (projected)
                frame.message = "Post-transform projected view fitted to "
                                "captured VS output. "
                                "Depth is projected, not world space; this is "
                                "not an original-camera replay. " +
                                frame.message;
            return frame;
        }
        float center[3], radius = 0;
        for (size_t i = 0; i < 3; ++i) {
            center[i] =
                (assembly->bounds.minimum[i] + assembly->bounds.maximum[i]) *
                .5f;
            float extent =
                (assembly->bounds.maximum[i] - assembly->bounds.minimum[i]) *
                .5f;
            radius += extent * extent;
        }
        radius = std::max(std::sqrt(radius), 1e-6f);
        if (!std::isfinite(radius)) {
            frame.message = "Assembly bounds cannot be framed";
            return frame;
        }
        const float scale = .8f * std::clamp(camera.zoom, .1f, 20.f),
                    aspect = float(w) / h;
        glUniform3fv(glGetUniformLocation(program, "center"), 1, center);
        glUniform1f(glGetUniformLocation(program, "radius"), radius);
        glUniform4f(glGetUniformLocation(program, "camera"), camera.yaw,
                    camera.pitch, scale / std::max(aspect, 1.f),
                    scale * std::min(aspect, 1.f));
        glUniform2f(glGetUniformLocation(program, "pan"), camera.pan_x,
                    camera.pan_y);
        glUniform1i(glGetUniformLocation(program, "image"), 0);
        for (size_t part_index = 0; part_index < assembly->parts.size();
             ++part_index) {
            const auto &part = assembly->parts[part_index];
            if (!part || part->status != AssetStatus::Ready)
                continue;
            auto *mesh = Get(part, assembly->context.backend, slot);
            if (!mesh)
                continue;
            static const AssetMatrix identity{ 1, 0, 0, 0, 0, 1, 0, 0,
                                               0, 0, 1, 0, 0, 0, 0, 1 };
            const auto &placement =
                assembly->captured_placement &&
                        assembly->anchor_from_local.size() ==
                            assembly->parts.size() ?
                    assembly->anchor_from_local[part_index] :
                    identity;
            glUniformMatrix4fv(
                glGetUniformLocation(program, "anchor_from_local"), 1, GL_TRUE,
                placement.data());
            glBindVertexArray(mesh->vao);
            glBindTexture(GL_TEXTURE_2D, mesh->texture);
            glUniform1i(glGetUniformLocation(program, "textured"),
                        mesh->texture != 0);
            glUniform1i(glGetUniformLocation(program, "colored"),
                        slot == -2 && part->has_color);
            glDrawElements(GL_TRIANGLES, part->indices.size(), GL_UNSIGNED_INT,
                           nullptr);
            ++frame.drawn_parts;
            if (mesh->texture)
                ++frame.textured_parts;
        }
        frame.texture = output.color;
        frame.width = w;
        frame.height = h;
        frame.gpu_bytes = gpu_bytes + stages.GpuBytes();
        frame.message = "Captured vertex inputs and base textures; diagnostic "
                        "view. Original VS/PS, skinning, generated UVs, "
                        "blending and destination are not replayed";
        if (frame.drawn_parts < assembly->parts.size())
            frame.message += ". Some parts could not be displayed";
        return frame;
    }
};
AssetViewport::AssetViewport() : impl_(std::make_unique<Impl>())
{
}
AssetViewport::~AssetViewport() = default;
AssetViewportFrame
AssetViewport::Render(std::shared_ptr<const AssetAssembly> assembly,
                      const AssetCamera &camera, uint32_t w, uint32_t h,
                      int slot, bool wire, bool captured_stages,
                      bool projected_output)
{
    return impl_->Draw(impl_->target, assembly, camera, w, h, slot, wire,
                       captured_stages, projected_output);
}
AssetViewportFrame
AssetViewport::Thumbnail(std::shared_ptr<const AssetAssembly> assembly)
{
    if (!assembly)
        return {};
    for (auto it = impl_->thumbs.begin(); it != impl_->thumbs.end(); ++it) {
        bool same = it->parts.size() == assembly->parts.size();
        for (size_t i = 0; same && i < it->parts.size(); ++i)
            same = it->parts[i].lock() == assembly->parts[i];
        if (same) {
            auto frame = it->frame;
            impl_->thumbs.splice(impl_->thumbs.end(), impl_->thumbs, it);
            return frame;
        }
    }
    if (impl_->thumbs.size() >= impl_->thumbnail_capacity) {
        GlState saved;
        impl_->thumbs.front().target.Destroy();
        impl_->thumbs.pop_front();
    }
    impl_->thumbs.emplace_back();
    auto &thumb = impl_->thumbs.back();
    for (const auto &part : assembly->parts)
        thumb.parts.push_back(part);
    AssetCamera camera;
    camera.yaw = .45f;
    camera.pitch = -.25f;
    const bool captured = std::all_of(
        assembly->parts.begin(), assembly->parts.end(), [](const auto &p) {
            return p && p->occurrence && p->occurrence->inputs.complete;
        });
    if (captured) {
        // The game camera's projected output is easier to recognize than an
        // arbitrary orbit of unprocessed skinning inputs.
        thumb.frame = impl_->Draw(thumb.target, assembly, {}, 96, 72, -1, false,
                                  true, true);
    }
    if (!thumb.frame.texture) {
        const auto missing = thumb.frame.message;
        thumb.frame =
            impl_->Draw(thumb.target, assembly, camera, 96, 72, -1, false);
        thumb.frame.message = "Raw vertex-input thumbnail. " + missing;
    }
    return thumb.frame;
}
size_t AssetViewport::ThumbnailCount() const
{
    return impl_->thumbs.size();
}
size_t AssetViewport::ThumbnailCapacity() const
{
    return impl_->thumbnail_capacity;
}
bool AssetViewport::Configure(uint64_t mesh_byte_budget,
                              size_t thumbnail_capacity)
{
    if (mesh_byte_budget < 16U * 1024U * 1024U ||
        mesh_byte_budget > kGpuBudget || thumbnail_capacity < 4 ||
        thumbnail_capacity > 64)
        return false;
    if (mesh_byte_budget != impl_->requested_budget ||
        thumbnail_capacity != impl_->thumbnail_capacity) {
        Shutdown();
        impl_->requested_budget = mesh_byte_budget;
        impl_->mesh_budget = mesh_byte_budget / 2;
        impl_->stages.Configure(
            std::min<uint64_t>(mesh_byte_budget / 2, 64U * 1024U * 1024U));
        impl_->thumbnail_capacity = thumbnail_capacity;
    }
    return true;
}
AssetViewportFrame AssetViewport::TextureImage(SharedAssetPart part,
                                               uint32_t backend, int slot)
{
    if (!part || !part->occurrence)
        return {};
    for (auto it = impl_->images.begin(); it != impl_->images.end(); ++it)
        if (it->event.lock() == part->occurrence && it->slot == slot) {
            auto frame = it->frame;
            impl_->images.splice(impl_->images.end(), impl_->images, it);
            return frame;
        }
    AssetPart input;
    input.occurrence = part->occurrence;
    input.has_uv = true;
    auto decoded = DecodeAssetTexture(input, backend, slot);
    if (decoded.rgba.empty()) {
        AssetViewportFrame frame;
        frame.message = decoded.reason;
        return frame;
    }
    GlState saved;
    while (!impl_->images.empty() &&
           (impl_->image_bytes + decoded.rgba.size() > 64U * 1024U * 1024U ||
            impl_->images.size() >= 24)) {
        auto &first = impl_->images.front();
        impl_->image_bytes -=
            uint64_t(first.frame.width) * first.frame.height * 4;
        glDeleteTextures(1, &first.frame.texture);
        impl_->images.pop_front();
    }
    Impl::Image image;
    image.event = part->occurrence;
    image.slot = slot;
    image.frame.width = decoded.width;
    image.frame.height = decoded.height;
    image.frame.message = "Owned host-decoded base texture, not a recovered "
                          "engine material asset";
    glGenTextures(1, &image.frame.texture);
    glBindTexture(GL_TEXTURE_2D, image.frame.texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, decoded.width, decoded.height, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, decoded.rgba.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    impl_->image_bytes += decoded.rgba.size();
    impl_->images.push_back(image);
    return image.frame;
}
void AssetViewport::Shutdown()
{
    GlState saved;
    for (auto &mesh : impl_->meshes)
        mesh.Destroy();
    impl_->meshes.clear();
    impl_->gpu_bytes = 0;
    for (auto &thumb : impl_->thumbs)
        thumb.target.Destroy();
    impl_->thumbs.clear();
    impl_->target.Destroy();
    impl_->display_target.Destroy();
    for (auto &image : impl_->images)
        glDeleteTextures(1, &image.frame.texture);
    impl_->images.clear();
    impl_->image_bytes = 0;
    if (impl_->program)
        glDeleteProgram(impl_->program);
    impl_->program = 0;
    impl_->stages.Shutdown();
    glDeleteVertexArrays(1, &impl_->background_vao);
    impl_->background_vao = 0;
}
} // namespace xemu::asset_browser

// SPDX-License-Identifier: GPL-2.0-or-later
#include "shader-browser-preview-adapter.hh"
#include "shader-browser-preview-gl.hh"
#include "shader-browser-preview-service.hh"
#include "shader-browser-draw-request.hh"
#include "shader-browser-capture-comparison.hh"
#include "hw/xbox/nv2a/pgraph/gl/shader-browser-capture.h"
#include <SDL3/SDL.h>
#include <epoxy/gl.h>
#include <imgui.h>
#include <imgui_impl_opengl3.h>
#include <glib.h>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <vector>

using namespace xemu::shader_browser;
static void Check(bool condition, const char *expression, int line)
{
    if (!condition) {
        std::fprintf(stderr, "Lifecycle check failed at line %d: %s\n", line,
                     expression);
        std::exit(EXIT_FAILURE);
    }
}
#define CHECK(condition) Check(bool(condition), #condition, __LINE__)

static void ArchiveCheck(bool condition, const std::string &message)
{
    if (!condition) {
        std::fprintf(stderr, "Actual archive acceptance failed: %s\n",
                     message.c_str());
        std::exit(EXIT_FAILURE);
    }
}

static PreviewBackend backend = PreviewBackend::OpenGL;
static std::atomic<bool> fail_first_worker_bind{true};
static bool BindWorkerContext(SDL_Window *window, void *context)
{
    if (fail_first_worker_bind.exchange(false)) {
        SDL_SetError("injected first worker bind failure");
        return false;
    }
    return SDL_GL_MakeCurrent(window, static_cast<SDL_GLContext>(context));
}
static uint64_t Now()
{
    return g_get_monotonic_time() * UINT64_C(1000);
}
static PreviewPacket Packet(PreviewMode mode, bool bad, uint64_t revision)
{
    PreviewPacket p;
    p.selection.scope.title_id = 1;
    p.selection.session_epoch = 1;
    p.selection.renderer_epoch = 1;
    p.selection.backend = backend;
    p.selection.mode = mode;
    p.selection.shader.stage = Stage::Pixel;
    p.recipe_format_version = 1;
    p.recipe.resize(312);
    p.recipe[0] = 'N';
    p.recipe[1] = 'V';
    p.recipe[2] = '2';
    p.recipe[3] = 'A';
    p.recipe[4] = 2;
    p.selection.shader.hash =
        ComputeShaderHash(1, Stage::Pixel, 1, p.recipe.data(), p.recipe.size());
    p.generator_abi = p.interface_abi = 1;
    p.width = p.height = 160;
    p.source = backend == PreviewBackend::Vulkan ?
                   "#version 450\nlayout(location=0) out vec4 color;\n" :
                   "#version 400\nout vec4 color;\n";
    p.source += bad ? "invalid shader" :
                mode == PreviewMode::Normal ?
                      "void main(){color=vec4(1,0,0,1);}" :
                      "void main(){color=vec4(0,1,0,1);}";
    p.partner_source =
        BuildPreviewSyntheticVertexSource(p.source, p.selection.backend);
    p.source_digest = ComputePreviewDigest(
        reinterpret_cast<const uint8_t *>(p.source.data()), p.source.size());
    p.partner_digest = ComputePreviewDigest(
        reinterpret_cast<const uint8_t *>(p.partner_source.data()),
        p.partner_source.size());
    p.fixture_bytes = EncodePreviewSyntheticFixture(PreviewSyntheticFixture{});
    p.fixture_digest =
        ComputePreviewDigest(p.fixture_bytes.data(), p.fixture_bytes.size());
    if (mode == PreviewMode::Replacement) {
        p.replacement_id = 4;
        p.replacement_revision = revision;
    }
    return p;
}

// Exercise the production OpenGL source adapter with real host buffers rather
// than handing the preview an already-constructed mesh packet.
static void TestGLSubmissionSources()
{
    GLuint vao, vertex, element, unrelated;
    glGenVertexArrays(1, &vao);
    glBindVertexArray(vao);
    GLuint buffers[3];
    glGenBuffers(3, buffers);
    vertex = buffers[0];
    element = buffers[1];
    unrelated = buffers[2];
    float guest[] = { 0, 0, 0, 1, 1, 0, 0, 1, 0, 1, 0, 1 };
    glBindBuffer(GL_ARRAY_BUFFER, vertex);
    glBufferData(GL_ARRAY_BUFFER, sizeof(guest), guest, GL_STREAM_DRAW);
    glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, 0, nullptr);
    glEnableVertexAttribArray(0);
    const uint32_t indices[] = { 0, 1, 2 };
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, element);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices,
                 GL_STREAM_DRAW);
    glBindBuffer(GL_COPY_READ_BUFFER, unrelated);
    guest[0] =
        1000; // Mutable guest/inline storage is no longer this generation.
    XemuShaderDrawRequestSpec spec{};
    spec.identity_hash[0] = 1;
    spec.stage = XEMU_SHADER_BROWSER_STAGE_PIXEL;
    spec.scope.title_id = 1;
    spec.scope_generation = 8;
    spec.session_epoch = 1;
    spec.renderer_epoch = 2;
    XemuShaderDrawIdentity identity{};
    identity.identity_hash[0] = 1;
    identity.stage = spec.stage;
    for (bool indexed : { false, true }) {
        auto token = xemu_shader_draw_request_arm(&spec);
        CHECK(xemu_shader_draw_request_claim(8, 2, &identity, 1, 1, 1, 1) ==
              token);
        XemuShaderDrawLayout layout{};
        if (indexed) {
            g_autofree uint8_t *copy =
                xemu_shader_draw_gl_read_buffer(element, 0, sizeof(indices));
            CHECK(copy && xemu_shader_draw_layout_elements(&layout, copy, 3));
        } else {
            const int32_t start = 0, count = 3;
            CHECK(xemu_shader_draw_layout_arrays(&layout, &start, &count, 1));
        }
        CHECK(xemu_shader_draw_gl_stage_bound(token, &layout));
        CHECK(GetDrawCaptureRequest().CopyGeometry().positions.empty());
        CHECK(xemu_shader_draw_request_finish(token, 1, 5, 3, indexed ? 3 : 0));
        auto geometry = GetDrawCaptureRequest().CopyGeometry();
        CHECK(geometry.positions.size() == 3 && geometry.positions[0][0] == 0);
        CHECK(geometry.indices == std::vector<uint32_t>({ 0, 1, 2 }));
        GLint bound;
        glGetIntegerv(GL_COPY_READ_BUFFER_BINDING, &bound);
        CHECK(static_cast<GLuint>(bound) == unrelated);
        glGetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING, &bound);
        CHECK(static_cast<GLuint>(bound) == element);
    }
    CHECK(!xemu_shader_draw_gl_read_buffer(vertex, sizeof(guest), 1));
    CHECK(glGetError() == GL_NO_ERROR);
    xemu_shader_draw_request_cancel();
    glBindBuffer(GL_COPY_READ_BUFFER, 0);
    glBindVertexArray(0);
    glDeleteBuffers(3, buffers);
    glDeleteVertexArrays(1, &vao);
    std::puts("Production GL submission source: indexed host generation and "
              "expanded inline float4 passed");
}

// This is a deterministic native preview/session integration fixture. It does
// not exercise title capture adapters or claim complete NV2A raster replay.
static void AddRasterEvidence(OwnedDrawInputs &inputs, uint32_t extent)
{
    const bool vk = backend == PreviewBackend::Vulkan;
    auto blob = [&](const char *name, const void *data, size_t bytes) {
        OwnedDrawBlob value;
        value.name = name;
        const auto *begin = static_cast<const uint8_t *>(data);
        value.bytes.assign(begin, begin + bytes);
        inputs.blobs.push_back(std::move(value));
    };
    auto reg = [&](const char *name, uint32_t value) {
        inputs.registers.push_back({ name, value });
    };
    if (!vk) {
        const int32_t viewport[]{ 0, 0, int32_t(extent), int32_t(extent) };
        const int32_t scissor[]{ 32, int32_t(extent) - 48 - 64, 64, 64 };
        const double range[]{ 0, 1 };
        const uint8_t mask[]{ 1, 0, 1, 1 };
        const float blend[4]{};
        const float sample_coverage = 0.125f;
        blob("host.viewport", viewport, sizeof(viewport));
        blob("host.scissor", scissor, sizeof(scissor));
        blob("host.depth_range", range, sizeof(range));
        blob("host.color_write", mask, sizeof(mask));
        blob("host.blend_color", blend, sizeof(blend));
        // Disabled coverage modifiers make their retained non-default values
        // inert on this known single-sample target.
        blob("host.sample_coverage_value", &sample_coverage,
             sizeof(sample_coverage));
        reg("host.scissor_enabled", 1);
        reg("host.blend_enabled", 1);
        reg("host.blend_src_rgb", GL_SRC_ALPHA);
        reg("host.blend_dst_rgb", GL_ONE_MINUS_SRC_ALPHA);
        reg("host.blend_src_alpha", GL_ZERO);
        reg("host.blend_dst_alpha", GL_ONE);
        reg("host.blend_equation_rgb", GL_FUNC_ADD);
        reg("host.blend_equation_alpha", GL_FUNC_ADD);
        reg("host.depth_enabled", 0);
        reg("host.depth_write", 0);
        reg("host.depth_func", GL_LESS);
        reg("host.depth_clamp_enabled", 1);
        reg("host.sample_buffers", 0);
        reg("host.samples", 0);
        reg("host.multisample_enabled", 1);
        reg("host.sample_alpha_to_coverage", 0);
        reg("host.sample_alpha_to_one", 0);
        reg("host.sample_coverage_enabled", 0);
        reg("host.sample_mask_enabled", 0);
        reg("host.sample_coverage_invert", 1);
        reg("host.sample_mask_value0", 0);
        reg("host.stencil_enabled", 0);
        for (const char *prefix : { "host.stencil_", "host.stencil_back_" }) {
            const std::string p = prefix;
            inputs.registers.push_back({ p + "func", GL_ALWAYS });
            inputs.registers.push_back({ p + "ref", 0 });
            inputs.registers.push_back({ p + "read_mask", UINT32_MAX });
            inputs.registers.push_back({ p + "write_mask", UINT32_MAX });
            inputs.registers.push_back({ p + "fail", GL_KEEP });
            inputs.registers.push_back({ p + "depth_fail", GL_KEEP });
            inputs.registers.push_back({ p + "pass", GL_KEEP });
        }
        reg("host.cull_enabled", 0);
        reg("host.cull_face", GL_BACK);
        reg("host.front_face", GL_CCW);
        reg("host.polygon_offset", 0);
    } else {
        const float viewport[]{ 0, 0, float(extent), float(extent), 0, 1 };
        const int32_t scissor[]{ 32, 48, 64, 64 };
        blob("vk.viewport", viewport, sizeof(viewport));
        blob("vk.scissor", scissor, sizeof(scissor));
        reg("capture.vk.pipeline_abi", 1);
        reg("capture.vk.dynamic_line_width", 0);
        reg("capture.vk.dynamic_blend_constant_mask", 0);
        auto wire =
            [&](const char *name, size_t words, uint32_t kind,
                std::initializer_list<std::pair<size_t, uint32_t>> values) {
                std::vector<uint32_t> data(words);
                data[0] = kind;
                for (const auto &value : values)
                    data[value.first] = value.second;
                blob(name, data.data(), data.size() * 4);
            };
        wire("vk.pipeline.assembly", 8, 20, { { 5, 3 } });
        wire("vk.pipeline.raster", 16, 23, { { 5, 1 }, { 14, 0x3f800000 } });
        wire("vk.pipeline.multisample", 12, 24, { { 5, 1 } });
        wire("vk.pipeline.depth_stencil", 26, 25,
             { { 7, 1 },
               { 13, 7 },
               { 14, UINT32_MAX },
               { 15, UINT32_MAX },
               { 20, 7 },
               { 21, UINT32_MAX },
               { 22, UINT32_MAX } });
        wire("vk.pipeline.blend", 14, 26, { { 7, 1 } });
        const uint32_t attachment[]{ 1, 6, 7, 0, 0, 1, 0, 13 };
        blob("vk.pipeline.blend_attachment", attachment, sizeof(attachment));
    }
    inputs.before.width = inputs.before.height = extent;
    inputs.before.rgba.resize(size_t(extent) * extent * 4);
    for (uint32_t y = 0; y < extent; ++y)
        for (uint32_t x = 0; x < extent; ++x) {
            const size_t p = (y * extent + x) * 4;
            inputs.before.rgba[p] = 20 + y / 4;
            inputs.before.rgba[p + 1] = 40 + y / 8;
            inputs.before.rgba[p + 2] = 60 + y / 2;
            inputs.before.rgba[p + 3] = 255;
        }
}

static void TestNativeRasterCheckpoints(PreviewGlExecutor &executor,
                                        PreviewPacket &packet,
                                        OwnedDrawInputs inputs,
                                        const std::function<void()> &draw)
{
    constexpr uint32_t extent = 160;
    AddRasterEvidence(inputs, extent);
    // The giant triangle covers the whole scissor. Each topology fixture below
    // must produce the same square without changing raster or destination data.
    const float full_triangle[]{ -1, -1, 0, 1, 3, -1, 0, 1, -1, 3, 0, 1 };
    if (backend == PreviewBackend::OpenGL)
        for (auto &blob : inputs.blobs)
            if (blob.name == "vertex.attribute0")
                std::memcpy(blob.bytes.data(), full_triangle,
                            sizeof(full_triangle));
    std::string error;
    auto pipeline = BuildPreviewCapturedPipeline(inputs, backend, 5, &error);
    CHECK(pipeline && pipeline->raster.blend_enabled &&
          pipeline->raster.color_write == 13);
    CHECK(pipeline->raster.available == 1023);
    CHECK((pipeline->raster.available & PreviewRasterDepthClamp) &&
          pipeline->raster.depth_clamp);
    if (backend == PreviewBackend::OpenGL) {
        for (const char *name :
             { "host.sample_buffers", "host.samples",
               "host.sample_alpha_to_coverage", "host.sample_alpha_to_one",
               "host.sample_coverage_enabled", "host.sample_mask_enabled" }) {
            auto missing = inputs;
            missing.registers.erase(std::remove_if(missing.registers.begin(),
                                                   missing.registers.end(),
                                                   [&](const auto &reg) {
                                                       return reg.name == name;
                                                   }),
                                    missing.registers.end());
            auto partial =
                BuildPreviewCapturedPipeline(missing, backend, 5, &error);
            CHECK(partial &&
                  partial->raster.available == (1023 & ~PreviewRasterCoverage));
            CHECK(DescribePreviewCapturedRaster(*partial).find(
                      "multisample coverage") != std::string::npos);
            CHECK(DescribePreviewCapturedRaster(*partial).find(
                      "preview defaults") != std::string::npos);
            for (const uint32_t value : { 1U, 2U }) {
                auto unsupported = inputs;
                for (auto &reg : unsupported.registers)
                    if (reg.name == name)
                        reg.value = value;
                CHECK(!BuildPreviewCapturedPipeline(unsupported, backend, 5,
                                                    &error));
                CHECK(!error.empty());
            }
            auto duplicate = inputs;
            duplicate.registers.push_back({ name, 0 });
            CHECK(!BuildPreviewCapturedPipeline(duplicate, backend, 5, &error));
        }
        std::puts("Native GL single-sample raster: full evidence, inert raw "
                  "coverage values, missing tags and unsupported states "
                  "checked");
    }
    auto &service = GetPreviewService();
    service.UsePacketView();
    service.EditChannel(PreviewChannel::FinalRGBA);
    auto render = [&](std::shared_ptr<const PreviewCapturedPipeline> state,
                      bool edited, bool expect_unsupported = false) {
        packet = Packet(PreviewMode::Normal, false, 0);
        packet.packet_kind = PreviewPacketKind::Replay;
        packet.replay_class = PreviewReplayClass::Approximate;
        // Native source owns alpha discard; private fixture controls cannot
        // reject or override captured pipeline behavior.
        packet.render_state.alpha_test = true;
        packet.captured_pipeline = state;
        packet.pipeline_digest = ComputePreviewCapturedPipelineDigest(*state);
        packet.pipeline_layout_digest =
            ComputePreviewCapturedPipelineDigest(*state, true);
        packet.partner_source = inputs.sources[1];
        packet.partner_digest = ComputePreviewDigest(
            reinterpret_cast<const uint8_t *>(packet.partner_source.data()),
            packet.partner_source.size());
        packet.source = backend == PreviewBackend::Vulkan ?
                            "#version 450\nlayout(location=0) out vec4 color;" :
                            "#version 400\nout vec4 color;";
        packet.source += edited ? "void main(){color=vec4(0,0,1,.5);}" :
                                  "void main(){color=vec4(1,0,0,.5);}";
        packet.source_digest = ComputePreviewDigest(
            reinterpret_cast<const uint8_t *>(packet.source.data()),
            packet.source.size());
        if (edited) {
            packet.source_variant = PreviewSourceVariant::Edited;
            packet.draft_id = 7;
            packet.draft_revision = 1;
            packet.draft_submission_id = 1;
        }
        const auto expected = BuildPreviewResultKey(packet);
        service.SetVisible(true, Now());
        service.SetSelection(packet.selection, Now());
        CHECK(service.SubmitPacket(packet, Now(), &error));
        uint32_t width = 0, height = 0;
        std::vector<uint8_t> pixels;
        PreviewDrawTiming owned_timing;
        const auto deadline = Now() + UINT64_C(10000000000);
        do {
            draw();
            if (executor.CopyReadyImage(expected, &width, &height, &pixels,
                                        &error, &owned_timing))
                break;
            CHECK(error.empty());
            PreviewStatus status;
            service.CopyStatus(&status);
            if (expect_unsupported &&
                status.state == PreviewState::Unsupported) {
                CHECK(status.message.find("geometry shader input") !=
                      std::string::npos);
                executor.AfterHudRender();
                return std::vector<uint8_t>{};
            }
            if (status.state == PreviewState::Failed ||
                status.state == PreviewState::Unsupported)
                std::fprintf(stderr, "Raster checkpoint: %s\n",
                             status.message.c_str());
            CHECK(status.state != PreviewState::Failed &&
                  status.state != PreviewState::Unsupported);
            SDL_Delay(5);
        } while (Now() < deadline);
        CHECK(!expect_unsupported);
        CHECK(width == extent && height == extent &&
              pixels.size() == size_t(extent) * extent * 4);
        CHECK(owned_timing.result == expected &&
              owned_timing.status == PreviewDrawTimingStatus::Disarmed &&
              !owned_timing.nanoseconds);
        const auto fidelity = DescribePreviewCapturedRaster(*state);
        CHECK(fidelity.find("seeded from owned before") != std::string::npos);
        CHECK(fidelity.find("depth/stencil destination unavailable") !=
              std::string::npos);
        executor.AfterHudRender();
        return pixels;
    };
    const auto original = render(pipeline, false);
    const auto edited = render(pipeline, true);
    if (backend == PreviewBackend::OpenGL) {
        const float strip_positions[]{ -1, -1, 0, 1, 1, -1, 0, 1,
                                       -1, 1,  0, 1, 1, 1,  0, 1 };
        const float fan_positions[]{ -1, -1, 0, 1, 1,  -1, 0, 1,
                                     1,  1,  0, 1, -1, 1,  0, 1 };
        // Local owned adjacency shaders use the same filled vertex order as
        // production glsl/geom.c. No diagnostic triangulation replaces raw4.
        const std::string adjacency_head =
            "#version 400\nlayout(lines_adjacency) in;"
            "layout(triangle_strip,max_vertices=6) out;\n"
            "void vertex(int "
            "i){gl_Position=gl_in[i].gl_Position;EmitVertex();}\n";
        const std::string quad_source =
            adjacency_head +
            "void main(){vertex(1);vertex(2);vertex(0);EndPrimitive();"
            "vertex(2);vertex(3);vertex(0);EndPrimitive();}";
        const std::string quad_strip_source =
            adjacency_head + "void main(){if((gl_PrimitiveIDIn&1)!=0)return;"
                             "vertex(0);vertex(1);vertex(2);EndPrimitive();"
                             "vertex(2);vertex(1);vertex(3);EndPrimitive();}";
        const GLenum host_modes[]{ GL_TRIANGLE_STRIP, GL_TRIANGLE_FAN,
                                   GL_LINES_ADJACENCY, GL_LINE_STRIP_ADJACENCY,
                                   GL_TRIANGLE_FAN };
        for (uint32_t primitive = 6; primitive <= 10; ++primitive) {
            for (bool indexed : { true, false }) {
                auto topology =
                    std::make_shared<PreviewCapturedPipeline>(*pipeline);
                topology->guest_primitive_mode = primitive;
                topology->host_topology = host_modes[primitive - 6];
                topology->host_topology_captured = true;
                topology->vertex_count = indexed ? 4 : 6;
                topology->indices = indexed ?
                                        std::vector<uint32_t>{ 1, 2, 3, 4 } :
                                        std::vector<uint32_t>{};
                topology->ranges =
                    indexed ? std::vector<std::array<uint32_t, 2>>{} :
                              std::vector<std::array<uint32_t, 2>>{ { 1, 4 },
                                                                    { 5, 2 } };
                if (primitive == 8)
                    topology->geometry_source = quad_source;
                if (primitive == 9)
                    topology->geometry_source = quad_strip_source;
                const float *positions = primitive == 6 || primitive == 9 ?
                                             strip_positions :
                                             fan_positions;
                for (uint32_t slot = 0; slot < 2; ++slot) {
                    auto &stream = topology->attributes[slot].stream;
                    stream.count = topology->vertex_count;
                    stream.bytes.resize(stream.count * 16);
                    if (!slot)
                        std::memcpy(stream.bytes.data(), positions, 64);
                    for (size_t offset = slot ? 48 : 64;
                         offset < stream.bytes.size(); offset += 16)
                        std::copy_n(stream.bytes.data(), 16,
                                    stream.bytes.data() + offset);
                }
                CHECK(ValidatePreviewCapturedPipeline(*topology, &error));
                CHECK(render(topology, false) == original);
                CHECK(render(topology, true) == edited);
                CHECK(topology->indices.size() == (indexed ? 4U : 0U));
                CHECK(topology->ranges.size() == (indexed ? 0U : 2U));
                std::printf("Native GL topology%u %s raw4: all %zu RGBA bytes "
                            "match original and edited baselines\n",
                            primitive, indexed ? "indexed" : "array4+2",
                            original.size());
            }
        }
        auto mismatch = std::make_shared<PreviewCapturedPipeline>(*pipeline);
        mismatch->geometry_source = quad_source;
        CHECK(render(mismatch, false, true).empty());
        CHECK(render(pipeline, false) == original);
    }
    const size_t center = (80 * extent + 80) * 4;
    const std::array<uint8_t, 4> red{ 148, 50, 50, 255 },
        blue{ 20, 50, 178, 255 };
    // Blending into UNORM8 permits implementation-dependent arithmetic and
    // conversion precision. Mesa rounds these half values up; NVIDIA rounds
    // down. Allow one unit only in blended RGB channels, while retained green,
    // alpha, scissor-excluded pixels and repeated results remain byte-exact.
    auto blended_matches = [&](const std::vector<uint8_t> &pixels,
                               const std::array<uint8_t, 4> &expected) {
        for (size_t channel = 0; channel < 4; ++channel) {
            const int tolerance = channel == 0 || channel == 2 ? 1 : 0;
            if (std::abs(int(pixels[center + channel]) - expected[channel]) >
                tolerance)
                return false;
        }
        return true;
    };
    CHECK(blended_matches(original, red));
    CHECK(blended_matches(edited, blue));
    for (auto point : { std::array<uint32_t, 2>{ 100, 80 },
                        std::array<uint32_t, 2>{ 90, 120 },
                        std::array<uint32_t, 2>{ 0, 0 } }) {
        const size_t p = (point[1] * extent + point[0]) * 4;
        CHECK(std::equal(inputs.before.rgba.begin() + p,
                         inputs.before.rgba.begin() + p + 4,
                         original.begin() + p));
        CHECK(std::equal(inputs.before.rgba.begin() + p,
                         inputs.before.rgba.begin() + p + 4,
                         edited.begin() + p));
    }
    auto culled = std::make_shared<PreviewCapturedPipeline>(*pipeline);
    culled->raster.cull_mode = 3;
    CHECK(render(culled, false) == inputs.before.rgba);
    CHECK(render(pipeline, false) == original);
    for (const float outside_depth : { -2.0f, 2.0f }) {
        auto outside = std::make_shared<PreviewCapturedPipeline>(*pipeline);
        auto &stream = outside->attributes[0].stream;
        CHECK(stream.components == 4 && stream.stride == 16);
        for (size_t offset = 8; offset + 4 <= stream.bytes.size();
             offset += stream.stride)
            std::memcpy(stream.bytes.data() + offset, &outside_depth, 4);
        outside->raster.depth_clamp = false;
        CHECK(render(outside, false) == inputs.before.rgba);
        outside->raster.depth_clamp = true;
        const auto clamped = render(outside, false);
        CHECK(blended_matches(clamped, red));
    }
    std::puts("Native lifecycle depth clamp: both clip planes reject unclamped "
              "vertices and preserve clamped source color");
    CHECK(render(pipeline, false) == original);
    const auto seed_compile = BuildPreviewCompileKey(packet);
    auto new_seed = std::make_shared<PreviewCapturedPipeline>(*pipeline);
    for (size_t p = 0; p < new_seed->color_before.rgba.size(); p += 4) {
        new_seed->color_before.rgba[p] += 10;
        new_seed->color_before.rgba[p + 1] += 7;
    }
    CHECK(ComputePreviewCapturedPipelineDigest(*new_seed, true) ==
          ComputePreviewCapturedPipelineDigest(*pipeline, true));
    const auto updated = render(new_seed, false);
    CHECK(BuildPreviewCompileKey(packet) == seed_compile);
    const std::array<uint8_t, 4> changed{ 153, 57, 50, 255 };
    CHECK(blended_matches(updated, changed));
    std::puts("Native owned-before blending/scissor/masks/cull, source edit "
              "and seed-only checkpoint updates passed");
}

// Synthetic owned recording, executed by the production native preview worker.
// This proves reconstructed dependency propagation, not end-to-end title
// capture or replay timing/fidelity for the canonical CPU image copy between
// draws.
static void TestScalarReadbacks(PreviewGlExecutor &executor,
                               PreviewPacket &packet,
                               const std::function<void()> &draw)
{
    auto &service = GetPreviewService();
    service.UsePacketView();
    packet = Packet(PreviewMode::Normal, false, 0);
    packet.source = backend == PreviewBackend::Vulkan ?
                        "#version 450\nlayout(location=0) out vec4 color;" :
                        "#version 400\nout vec4 color;";
    packet.source += "void main(){color=vec4(.6,.8,0,.25);}";
    packet.source_digest = ComputePreviewDigest(
        reinterpret_cast<const uint8_t *>(packet.source.data()),
        packet.source.size());
    std::string error;
    auto render = [&](PreviewChannel channel) {
        service.EditChannel(channel);
        service.SetVisible(true, Now());
        service.SetSelection(packet.selection, Now());
        CHECK(service.SubmitPacket(packet, Now(), &error));
        auto expected = BuildPreviewResultKey(packet);
        expected.channel = channel;
        uint32_t width = 0, height = 0;
        std::vector<uint8_t> pixels;
        const auto deadline = Now() + UINT64_C(10000000000);
        do {
            draw();
            if (executor.CopyReadyImage(expected, &width, &height, &pixels,
                                        &error))
                break;
            CHECK(error.empty());
            SDL_Delay(5);
        } while (Now() < deadline);
        CHECK(width == packet.width && height == packet.height);
        CHECK(pixels.size() == size_t(width) * height * 4);
        return pixels;
    };
    const auto original = render(PreviewChannel::FinalRGBA);
    for (auto channel : { PreviewChannel::Red, PreviewChannel::Green,
                          PreviewChannel::Blue, PreviewChannel::Alpha }) {
        const auto pixels = render(channel);
        const size_t component = size_t(channel) - size_t(PreviewChannel::Red);
        for (size_t p = 0; p < pixels.size(); p += 4) {
            CHECK(pixels[p] == original[p + component]);
            CHECK(pixels[p + 1] == original[p + component]);
            CHECK(pixels[p + 2] == original[p + component]);
            CHECK(pixels[p + 3] == 255);
        }
    }
    service.EditChannel(PreviewChannel::FinalRGBA);
    std::puts("Native scalar readbacks: all four channel views match owned "
              "RGBA components and opaque displayed alpha");
}

static void TestNativeDependencyComparison(PreviewGlExecutor &executor,
                                           PreviewPacket &packet,
                                           const std::function<void()> &draw)
{
    constexpr uint32_t extent = 64;
    const bool vk = backend == PreviewBackend::Vulkan;
    const auto base = Packet(PreviewMode::Normal, false, 0);
    const std::string vertex =
        (vk ? "#version 450\n" : "#version 400\n") +
        std::string("layout(location=0) in vec4 position;\n"
                    "void main(){gl_Position=position;}");
    const std::string pixel_header =
        vk ? "#version 450\nlayout(location=0) out vec4 color;\n" :
             "#version 400\nout vec4 color;\n";
    const std::string original_source =
        pixel_header + "void main(){color=vec4(1,0,0,1);}";
    const std::string edited_source =
        pixel_header + "void main(){color=vec4(0,1,0,1);}";
    const std::string consumer_source =
        pixel_header +
        (vk ? "layout(binding=2) uniform sampler2D texSamp0;\n" :
              "uniform sampler2D texSamp0;\n") +
        "void main(){color=texture(texSamp0,vec2(.5));}";
    auto consumer_recipe = base.recipe;
    consumer_recipe.back() ^= 1;
    const auto consumer_hash = ComputeShaderHash(
        1, Stage::Pixel, 1, consumer_recipe.data(), consumer_recipe.size());
    CHECK(consumer_hash != base.selection.shader.hash);
    CaptureSessionContext context;
    context.scope = base.selection.scope;
    context.scope_generation = 43;
    context.session_epoch = base.selection.session_epoch;
    context.renderer_epoch = base.selection.renderer_epoch;
    context.generation = 2;
    context.backend = uint32_t(backend);
    CaptureSession session;
    CHECK(session.Start(context));
    session.GuestFrameBoundary(20);
    OwnedDrawGeometry mesh;
    mesh.positions = { { { -1, -1, 0, 1 } },
                       { { 3, -1, 0, 1 } },
                       { { -1, 3, 0, 1 } } };
    mesh.indices = { 0, 1, 2 };
    for (uint32_t i = 0; i < 2; ++i) {
        DrawCaptureSummary summary;
        summary.scope = context.scope;
        summary.key = { context.session_epoch, context.renderer_epoch, 20, i,
                        i + 1 };
        summary.shaders[0] = base.selection.shader;
        if (i)
            summary.shaders[0].hash = consumer_hash;
        summary.shader_count = 1;
        const auto token = session.BeginOccurrence(summary);
        CHECK(token && session.StageGeometry(token, mesh));
        OwnedDrawInputs owned;
        AddRasterEvidence(owned, extent);
        // Complete raster evidence with an unblended full color write.
        for (auto &reg : owned.registers)
            if (reg.name == "host.blend_enabled" ||
                reg.name == "host.scissor_enabled")
                reg.value = 0;
        for (auto &blob : owned.blobs) {
            if (blob.name == "host.color_write")
                blob.bytes.assign(4, 1);
            if (blob.name == "vk.scissor") {
                const int32_t scissor[]{ 0, 0, extent, extent };
                std::memcpy(blob.bytes.data(), scissor, sizeof(scissor));
            }
            if (blob.name == "vk.pipeline.blend_attachment") {
                const uint32_t attachment[]{ 0, 1, 0, 0, 1, 0, 0, 15 };
                std::memcpy(blob.bytes.data(), attachment, sizeof(attachment));
            }
        }
        std::fill(owned.before.rgba.begin(), owned.before.rgba.end(), 0);
        for (size_t p = 3; p < owned.before.rgba.size(); p += 4)
            owned.before.rgba[p] = 255;
        for (const auto &reg : owned.registers)
            CHECK(session.StageRegister(token, reg.name.c_str(), reg.value));
        auto blob = [&](const char *name, const void *data, size_t bytes,
                        uint32_t format = 0, uint32_t count = 0) {
            XemuShaderDrawBlob value{};
            value.name = name;
            value.data = data;
            value.byte_count = bytes;
            value.format = format;
            value.components = format ? 4 : 0;
            value.stride = format ? 16 : 0;
            value.count = count;
            CHECK(session.StageBlob(token, value));
        };
        for (const auto &value : owned.blobs)
            blob(value.name.c_str(), value.bytes.data(), value.bytes.size());
        blob("vertex.attribute0", mesh.positions.data(), 48,
             vk ? 109 : GL_FLOAT, 3);
        blob(vk ? "vertex.indices" : "geometry.host_indices",
             mesh.indices.data(), 12, 0, 3);
        const auto &recipe = i ? consumer_recipe : base.recipe;
        blob("recipe.stage2", recipe.data(), recipe.size());
        CHECK(session.StageRegister(
            token, vk ? "capture.vertices.first" : "capture.first_vertex", 0));
        CHECK(session.StageRegister(
            token, vk ? "capture.vertices.count" : "capture.last_vertex",
            vk ? 3 : 2));
        CHECK(session.StageRegister(
            token, vk ? "capture.vertex.0.enabled" : "vertex.enabled0", 1));
        CHECK(session.StageRegister(token, "capture.route",
                                    uint32_t(Route::Specialized)));
        CHECK(session.StageRegister(token, "capture.generator_abi", 1));
        CHECK(session.StageRegister(token, "capture.interface_abi", 1));
        XemuShaderDrawImage before{ extent, extent, owned.before.rgba.data(),
                                    owned.before.rgba.size() };
        CHECK(session.StageImage(token, true, before));
        for (uint32_t slot = 0; slot < 4; ++slot) {
            XemuShaderDrawTexture texture{};
            texture.slot = slot;
            std::vector<uint8_t> blue(size_t(extent) * extent * 4);
            if (i && !slot) {
                texture.bound = 1;
                texture.width = texture.height = extent;
                texture.depth = texture.face_count = texture.mip_levels = 1;
                texture.min_filter = texture.mag_filter = vk ? 0 : GL_NEAREST;
                texture.wrap_s = texture.wrap_t = texture.wrap_r =
                    vk ? 2 : GL_CLAMP_TO_EDGE;
                for (size_t p = 0; p < blue.size(); p += 4)
                    blue[p + 2] = blue[p + 3] = 255;
                texture.image = { extent, extent, blue.data(), blue.size() };
            }
            CHECK(session.StageTexture(token, texture));
        }
        const auto &source = i ? consumer_source : original_source;
        CHECK(session.StageSource(token, 1, vertex.data(), vertex.size()));
        CHECK(session.StageSource(token, 2, source.data(), source.size()));
        CHECK(session.Finish(token, true, 5, 3, 3));
        CHECK(session.InputsComplete(token));
    }
    session.Stop();
    auto snapshot = session.Snapshot();
    std::vector<std::shared_ptr<const CaptureOccurrence>> draws;
    for (const auto &event : snapshot.events)
        if (event->type == CaptureEventType::Draw)
            draws.push_back(event);
    CHECK(draws.size() == 2);
    auto producer = std::make_shared<CaptureOccurrence>(*draws[0]);
    auto consumer = std::make_shared<CaptureOccurrence>(*draws[1]);
    producer->event_id = 10;
    consumer->event_id = 12;
    auto copy = std::make_shared<CaptureOccurrence>();
    copy->event_id = 2;
    copy->type = CaptureEventType::Copy;
    copy->finished = copy->inputs.complete = copy->emitted = true;
    copy->pending = false;
    const uint64_t color_bytes = uint64_t(extent) * extent * 4;
    CaptureResourceTimeline timeline;
    std::array<uint64_t, 5> allocations{}, views{};
    std::vector<CaptureResourceOperation> initial;
    auto operation = [&](CaptureResourceOperationType type, uint32_t resource,
                         uint32_t kind, uint32_t slot = 0) {
        CaptureResourceOperation op;
        op.type = type;
        op.allocation_id = allocations[resource];
        op.view_id = views[resource];
        op.byte_size = resource < 3 ? color_bytes : resource == 3 ? 48 : 12;
        op.range = { 0, op.byte_size };
        op.kind = kind;
        op.slot = slot;
        return op;
    };
    for (uint32_t i = 0; i < allocations.size(); ++i) {
        allocations[i] = timeline.ReserveAllocationId();
        views[i] = timeline.ReserveViewId();
        initial.push_back(operation(CaptureResourceOperationType::Allocate, i,
                                    XEMU_SHADER_CAPTURE_RESOURCE_COLOR));
        auto alias = operation(CaptureResourceOperationType::Alias, i,
                               XEMU_SHADER_CAPTURE_RESOURCE_COLOR);
        alias.aliases = { { allocations[i], alias.range } };
        initial.push_back(std::move(alias));
    }
    auto baseline = std::make_shared<CaptureOccurrence>();
    baseline->event_id = 1;
    baseline->type = CaptureEventType::AllocationBoundary;
    baseline->finished = baseline->inputs.complete = true;
    baseline->pending = false;
    baseline->resource_evidence = timeline.Apply(1, initial);
    auto draw_operations = [&](const CaptureOccurrence &event, uint32_t color,
                               bool textured) {
        std::vector<CaptureResourceOperation> ops;
        if (textured)
            ops.push_back(operation(CaptureResourceOperationType::Read, 1,
                                    XEMU_SHADER_CAPTURE_RESOURCE_TEXTURE));
        ops.push_back(operation(CaptureResourceOperationType::Read, color,
                                XEMU_SHADER_CAPTURE_RESOURCE_COLOR));
        for (uint32_t resource = 3; resource < 5; ++resource) {
            const char *name = resource == 3 ? "vertex.attribute0" :
                               vk            ? "vertex.indices" :
                                               "geometry.host_indices";
            auto op = operation(CaptureResourceOperationType::Read, resource,
                                XEMU_SHADER_CAPTURE_RESOURCE_BUFFER,
                                resource == 3 ? 0 : 16);
            for (const auto &blob : event.inputs.blobs)
                if (blob.name == name)
                    op.snapshot_block = blob.data->id;
            CHECK(op.snapshot_block);
            ops.push_back(std::move(op));
        }
        ops.push_back(operation(CaptureResourceOperationType::UncertainWrite,
                                color, XEMU_SHADER_CAPTURE_RESOURCE_COLOR));
        return ops;
    };
    auto evidence = timeline.ApplyBatch(
        { { 10, draw_operations(*producer, 0, false) },
          { 2,
            { operation(CaptureResourceOperationType::Read, 0,
                        XEMU_SHADER_CAPTURE_RESOURCE_COLOR),
              operation(CaptureResourceOperationType::Read, 1,
                        XEMU_SHADER_CAPTURE_RESOURCE_TEXTURE),
              operation(CaptureResourceOperationType::UncertainWrite, 1,
                        XEMU_SHADER_CAPTURE_RESOURCE_TEXTURE) } },
          { 12, draw_operations(*consumer, 2, true) } });
    CHECK(evidence.size() == 3);
    const std::array<std::shared_ptr<CaptureOccurrence>, 3> commands{
        producer, copy, consumer
    };
    for (size_t i = 0; i < commands.size(); ++i) {
        commands[i]->resource_evidence = evidence[i];
        commands[i]->resource_finalized = true;
        commands[i]->batch_id = commands[i]->queue_ordinal = 1;
        commands[i]->command_phase = CaptureCommandPhase::Main;
        commands[i]->command_ordinal = i + 1;
        commands[i]->command_recorded = true;
        commands[i]->submission = CaptureBatchOutcome::Submitted;
        commands[i]->completion = CaptureBatchCompletion::Completed;
        commands[i]->command.kind =
            i == 1 ? CaptureCommandKind::ImageCopy : CaptureCommandKind::Draw;
    }
    auto binding = [&](uint32_t role, uint32_t kind, bool write,
                       uint32_t slot = 0) {
        XemuShaderCaptureReplayBinding out{};
        out.role = role;
        out.resource = { kind, slot, write, 0 };
        out.slot = slot;
        if (kind != XEMU_SHADER_CAPTURE_RESOURCE_BUFFER) {
            out.image.width = out.image.height = extent;
            out.image.mip_levels = out.image.layers = out.image.samples = 1;
            out.image.format = XEMU_SHADER_CAPTURE_REPLAY_RGBA8_UNORM;
            for (uint32_t i = 0; i < 4; ++i)
                out.image.storage_to_rgba[i] = out.image.sample_swizzle[i] = i;
        }
        return out;
    };
    auto describe_draw = [&](bool textured) {
        auto description = std::make_shared<CaptureReplayDescription>();
        description->kind = uint32_t(CaptureCommandKind::Draw);
        if (textured)
            description->bindings.push_back(
                binding(XEMU_SHADER_CAPTURE_REPLAY_TEXTURE,
                        XEMU_SHADER_CAPTURE_RESOURCE_TEXTURE, false));
        auto color = binding(XEMU_SHADER_CAPTURE_REPLAY_COLOR,
                             XEMU_SHADER_CAPTURE_RESOURCE_COLOR, false);
        color.checkpoint = 1;
        description->bindings.push_back(color);
        for (uint32_t slot : { 0U, 16U }) {
            auto stream =
                binding(slot ? XEMU_SHADER_CAPTURE_REPLAY_INDICES :
                               XEMU_SHADER_CAPTURE_REPLAY_VERTEX_STREAM,
                        XEMU_SHADER_CAPTURE_RESOURCE_BUFFER, false, slot);
            stream.checkpoint = 1;
            std::strcpy(stream.blob_name,
                        slot ? vk ? "vertex.indices" : "geometry.host_indices" :
                               "vertex.attribute0");
            description->bindings.push_back(stream);
        }
        description->bindings.push_back(
            binding(XEMU_SHADER_CAPTURE_REPLAY_COLOR,
                    XEMU_SHADER_CAPTURE_RESOURCE_COLOR, true));
        return description;
    };
    producer->replay_description = describe_draw(false);
    consumer->replay_description = describe_draw(true);
    auto copy_description = std::make_shared<CaptureReplayDescription>();
    copy_description->kind = uint32_t(CaptureCommandKind::ImageCopy);
    copy_description->width = copy_description->height = extent;
    copy_description->bindings = {
        binding(XEMU_SHADER_CAPTURE_REPLAY_COPY_SOURCE,
                XEMU_SHADER_CAPTURE_RESOURCE_COLOR, false),
        binding(XEMU_SHADER_CAPTURE_REPLAY_COPY_DESTINATION,
                XEMU_SHADER_CAPTURE_RESOURCE_TEXTURE, false),
        binding(XEMU_SHADER_CAPTURE_REPLAY_COPY_DESTINATION,
                XEMU_SHADER_CAPTURE_RESOURCE_TEXTURE, true)
    };
    copy->replay_description = copy_description;
    snapshot.resource_domain = timeline.DomainId();
    // Observation order and IDs differ from the execution sequence.
    snapshot.events = { baseline, copy, producer, consumer };
    CaptureComparisonRequest request;
    request.selection = base.selection;
    request.dependencies = CaptureComparisonDependencies::ProducerSuffix;
    request.scope = CaptureComparisonScope::SelectedOccurrence;
    request.selected_event = 10;
    request.edit = { 78, 1, 1, edited_source };
    request.profile_draw = true;
    CaptureComparisonController controller;
    CHECK(controller.Start(request, snapshot));
    CHECK(controller.Snapshot().matched_events == 1);
    auto &service = GetPreviewService();
    service.UsePacketView();
    service.EditChannel(PreviewChannel::FinalRGBA);
    CaptureComparisonJob job;
    uint32_t jobs = 0;
    while (controller.TryClaimJob(&job)) {
        CHECK(job.occurrence->event_id == (jobs % 2 ? 12 : 10));
        CHECK(job.phase == (jobs < 2 ? CaptureComparisonPhase::Original :
                                       CaptureComparisonPhase::Replacement));
        CHECK(job.edited_seed == (jobs == 2));
        CHECK(job.packet->source == (jobs % 2  ? consumer_source :
                                     jobs == 2 ? edited_source :
                                                 original_source));
        packet = *job.packet;
        CHECK(packet.captured_pipeline && packet.captured_material);
        CHECK(packet.captured_pipeline->raster.available == 1023);
        if (jobs % 2) {
            const auto &pixels =
                packet.captured_material->textures[0].images[0].image.rgba;
            CHECK(pixels[(extent / 2 * extent + extent / 2) * 4 +
                         (jobs < 2 ? 0 : 1)] == 255);
            CHECK(pixels[(extent / 2 * extent + extent / 2) * 4 + 2] == 0);
        }
        service.SetVisible(true, Now());
        service.SetSelection(packet.selection, Now());
        std::string error;
        CHECK(service.SubmitPacket(packet, Now(), &error));
        CaptureComparisonCompletion completion;
        completion.identity = job.identity;
        completion.phase = job.phase;
        completion.result = job.expected_result;
        const auto deadline = Now() + UINT64_C(10000000000);
        bool copied = false;
        do {
            draw();
            copied = executor.CopyReadyImage(
                job.expected_result, &completion.image.width,
                &completion.image.height, &completion.image.rgba, &error,
                &completion.draw_timing);
            if (copied && completion.draw_timing.status !=
                              PreviewDrawTimingStatus::Pending)
                break;
            CHECK(error.empty());
            PreviewStatus status;
            service.CopyStatus(&status);
            if (status.state == PreviewState::Failed ||
                status.state == PreviewState::Unsupported)
                std::fprintf(stderr, "Dependency native worker: %s\n",
                             status.message.c_str());
            CHECK(status.state != PreviewState::Failed &&
                  status.state != PreviewState::Unsupported);
            SDL_Delay(5);
        } while (Now() < deadline);
        CHECK(copied && completion.image.width == extent &&
              completion.image.height == extent);
        CHECK(ValidatePreviewDrawTiming(completion.draw_timing,
                                        job.expected_result));
        const std::array<uint8_t, 4> expected =
            jobs < 2 ? std::array<uint8_t, 4>{ 255, 0, 0, 255 } :
                       std::array<uint8_t, 4>{ 0, 255, 0, 255 };
        const auto center = (extent / 2 * extent + extent / 2) * 4;
        CHECK(std::equal(expected.begin(), expected.end(),
                         completion.image.rgba.begin() + center));
        CHECK(completion.draw_timing.actual_draw_commands == 1);
        executor.AfterHudRender();
        CHECK(controller.Complete(std::move(completion)));
        ++jobs;
    }
    const auto result = controller.Snapshot();
    if (result.state != CaptureComparisonState::Ready || jobs != 4) {
        std::fprintf(stderr, "Native dependency comparison: jobs %u: %s\n",
                     jobs, result.message.c_str());
        for (const auto &row : result.results)
            std::fprintf(stderr, "Dependency event %llu outcome %u: %s\n",
                         static_cast<unsigned long long>(row.identity.event_id),
                         uint32_t(row.outcome), row.message.c_str());
    }
    CHECK(jobs == 4 && result.state == CaptureComparisonState::Ready);
    CHECK(result.results.size() == 2 && result.replay_steps == 3);
    CHECK(result.results[0].edited_seed && !result.results[1].edited_seed);
    const std::array<uint8_t, 4> red{ 255, 0, 0, 255 }, green{ 0, 255, 0, 255 };
    const auto center = (extent / 2 * extent + extent / 2) * 4;
    for (const auto &row : result.results) {
        CHECK(row.outcome == CaptureComparisonOutcome::Completed);
        CHECK(row.original && row.replacement && row.difference.changed_pixels);
        CHECK(std::equal(red.begin(), red.end(),
                         row.original->rgba.begin() + center));
        CHECK(std::equal(green.begin(), green.end(),
                         row.replacement->rgba.begin() + center));
    }
    CHECK(
        consumer->inputs.textures[0].images[0].image.rgba->bytes[center + 2] ==
        255);
    std::puts("Synthetic owned recording/native GPU replay: original red and "
              "edited green producer -> canonical CPU copy -> distinct "
              "consumer passed; captured old blue remained unchanged");
}

static void TestSavedEveryUseComparison(PreviewGlExecutor &executor,
                                        PreviewPacket &packet,
                                        const std::function<void()> &draw)
{
    constexpr uint32_t count = 32, extent = 64;
    const bool vk = backend == PreviewBackend::Vulkan;
    const auto base = Packet(PreviewMode::Normal, false, 0);
    std::string vertex = vk ? "#version 450\nlayout(binding=0,std140) uniform "
                              "V {vec4 cameraOffset;};\n" :
                              "#version 400\nuniform vec4 cameraOffset;\n";
    vertex += "layout(location=0) in vec4 position;";
    vertex +=
        vk ? "layout(location=0) out vec2 vertexUV;\n" : "out vec2 vertexUV;\n";
    vertex +=
        "void main(){gl_Position=position+cameraOffset;vertexUV=vec2(.5);}";
    std::string geometry =
        vk ? "#version 450\nlayout(location=0) in vec2 "
             "vertexUV[];layout(location=0) out vec2 geometryUV;\n" :
             "#version 400\nin vec2 vertexUV[];out vec2 geometryUV;\n";
    geometry +=
        "layout(triangles) in;layout(triangle_strip,max_vertices=3) out;"
        "void main(){for(int "
        "i=0;i<3;++i){gl_Position=gl_in[i].gl_Position;geometryUV=vertexUV[i];"
        "EmitVertex();}EndPrimitive();}";
    const std::string pixel_header =
        vk ? "#version 450\nlayout(location=0) in vec2 "
             "geometryUV;layout(location=0) out vec4 color;"
             "layout(binding=1,std140) uniform F {vec4 "
             "fogColor;};layout(binding=2) uniform sampler2D texSamp0;"
             "layout(binding=3) uniform sampler2D texSamp1;\n" :
             "#version 400\nin vec2 geometryUV;out vec4 color;uniform vec4 "
             "fogColor;"
             "uniform sampler2D texSamp0;uniform sampler2D texSamp1;\n";
    const std::string original_source =
        pixel_header + "void "
                       "main(){color=vec4(texture(texSamp0,geometryUV).r,"
                       "fogColor.g,texture(texSamp1,geometryUV).b,1);}";
    const std::string edited_source =
        pixel_header + "void "
                       "main(){color=vec4(texture(texSamp1,geometryUV).b,"
                       "texture(texSamp0,geometryUV).r,fogColor.g,1);}";
    CaptureSessionContext context;
    context.scope = base.selection.scope;
    context.scope_generation = 42;
    context.session_epoch = base.selection.session_epoch;
    context.renderer_epoch = base.selection.renderer_epoch;
    context.generation = 1;
    context.backend = uint32_t(backend);
    CaptureSession session;
    CHECK(session.Start(context));
    session.GuestFrameBoundary(10);
    for (uint32_t i = 0; i < count; ++i) {
        DrawCaptureSummary summary;
        summary.scope = context.scope;
        summary.key = { context.session_epoch, context.renderer_epoch, 10, i,
                        i + 1 };
        summary.shaders[0] = base.selection.shader;
        summary.shader_count = 1;
        const uint64_t token = session.BeginOccurrence(summary);
        CHECK(token);
        const float scale = .65f + float(i) / 256;
        OwnedDrawGeometry mesh;
        mesh.positions = { { { -scale, -scale, 0, 1 } },
                           { { scale, -scale, 0, 1 } },
                           { { 0, scale, 0, 1 } } };
        mesh.indices = { 0, 1, 2 };
        CHECK(session.StageGeometry(token, mesh));
        auto blob = [&](const char *name, const void *data, size_t bytes,
                        uint32_t format = 0, uint32_t elements = 0) {
            XemuShaderDrawBlob value{};
            value.name = name;
            value.data = data;
            value.byte_count = bytes;
            value.format = format;
            value.components = format ? 4 : 0;
            value.stride = format ? 16 : 0;
            value.count = elements;
            CHECK(session.StageBlob(token, value));
        };
        blob("vertex.attribute0", mesh.positions.data(),
             mesh.positions.size() * 16, vk ? 109 : GL_FLOAT, 3);
        blob(vk ? "vertex.indices" : "geometry.host_indices",
             mesh.indices.data(), 12, 0, 3);
        blob("recipe.stage2", base.recipe.data(), base.recipe.size());
        const std::array<int32_t, 4> gl_viewport{ 0, 0, extent, extent };
        const std::array<float, 6> vk_viewport{
            0, 0, float(extent), float(extent), 0, 1
        };
        if (vk)
            blob("vk.viewport", vk_viewport.data(), sizeof(vk_viewport));
        else
            blob("host.viewport", gl_viewport.data(), sizeof(gl_viewport));
        CHECK(session.StageRegister(
            token, vk ? "capture.vertices.first" : "capture.first_vertex", 0));
        CHECK(session.StageRegister(
            token, vk ? "capture.vertices.count" : "capture.last_vertex",
            vk ? 3 : 2));
        CHECK(session.StageRegister(
            token, vk ? "capture.vertex.0.enabled" : "vertex.enabled0", 1));
        CHECK(session.StageRegister(token, "capture.route",
                                    uint32_t(Route::Specialized)));
        CHECK(session.StageRegister(token, "capture.generator_abi", 1));
        CHECK(session.StageRegister(token, "capture.interface_abi", 1));
        const std::array<float, 4> camera{ (float(i) - 16) / 256, 0, 0, 0 };
        const std::array<float, 4> fog{ 0, float(20 + 3 * i) / 255, 0, 1 };
        const XemuShaderDrawUniform camera_uniform{
            vk ? 1U : 0U,  "cameraOffset", XEMU_SHADER_DRAW_UNIFORM_FLOAT, 4, 1,
            camera.data(), sizeof(camera)
        };
        const XemuShaderDrawUniform fog_uniform{
            vk ? 2U : 0U, "fogColor", XEMU_SHADER_DRAW_UNIFORM_FLOAT, 4, 1,
            fog.data(),   sizeof(fog)
        };
        CHECK(session.StageUniform(token, camera_uniform));
        CHECK(session.StageUniform(token, fog_uniform));
        for (uint32_t slot = 0; slot < 4; ++slot) {
            XemuShaderDrawTexture texture{};
            texture.slot = slot;
            std::array<uint8_t, 16> pixels{};
            if (slot < 2) {
                texture.bound = 1;
                texture.width = texture.height = 2;
                texture.depth = texture.face_count = texture.mip_levels = 1;
                texture.min_filter = texture.mag_filter = vk ? 0 : GL_NEAREST;
                texture.wrap_s = texture.wrap_t = texture.wrap_r =
                    vk ? 2 : GL_CLAMP_TO_EDGE;
                for (size_t p = 0; p < pixels.size(); p += 4) {
                    pixels[p] = slot == 0 ? uint8_t(10 + 5 * i) : 0;
                    pixels[p + 2] = slot == 1 ? uint8_t(255 - 4 * i) : 0;
                    pixels[p + 3] = 255;
                }
                texture.image = { 2, 2, pixels.data(), pixels.size() };
            }
            CHECK(session.StageTexture(token, texture));
        }
        CHECK(session.StageSource(token, 1, vertex.data(), vertex.size()));
        CHECK(session.StageSource(token, 2, original_source.data(),
                                  original_source.size()));
        CHECK(session.StageSource(token, 3, geometry.data(), geometry.size()));
        CHECK(session.Finish(token, true, 5, 3, 3));
        CHECK(session.InputsComplete(token));
    }
    session.Stop();
    const auto directory =
        std::filesystem::temp_directory_path() /
        (std::string("xemu-native-every-use-") +
         std::to_string(context.backend) + "-" + std::to_string(Now()));
    std::string error;
    CHECK(session.Save(directory, &error));
    CaptureSessionSnapshot reopened;
    CHECK(CaptureSession::Reopen(directory, &reopened, &error));
    std::filesystem::remove_all(directory);
    session.Cancel();
    CaptureComparisonRequest request;
    request.selection = base.selection;
    request.edit = { 77, 1, 1, edited_source };
    request.first_frame = request.last_frame = 10;
    request.render_state.clear_color = { 0, 0, 0, 1 };
    request.profile_draw = true;
    CaptureComparisonController controller;
    CHECK(controller.Start(request, reopened));
    CHECK(controller.Snapshot().matched_events == count);
    auto &service = GetPreviewService();
    service.UsePacketView();
    service.EditChannel(PreviewChannel::FinalRGBA);
    std::set<PreviewDigest> original_images, edited_images, pipeline_digests,
        material_digests;
    uint32_t completed = 0;
    CaptureComparisonJob job;
    while (controller.TryClaimJob(&job)) {
        packet = *job.packet;
        CHECK(packet.captured_pipeline && packet.captured_material);
        CHECK(packet.partner_source == vertex &&
              packet.captured_pipeline->geometry_source == geometry);
        CHECK(packet.width == extent && packet.height == extent);
        service.SetVisible(true, Now());
        service.SetSelection(packet.selection, Now());
        CHECK(service.SubmitPacket(packet, Now(), &error));
        CaptureComparisonCompletion completion;
        completion.identity = job.identity;
        completion.phase = job.phase;
        completion.result = job.expected_result;
        const uint64_t deadline = Now() + UINT64_C(10000000000);
        bool copied = false;
        do {
            draw();
            copied = executor.CopyReadyImage(
                job.expected_result, &completion.image.width,
                &completion.image.height, &completion.image.rgba, &error,
                &completion.draw_timing);
            if (copied && completion.draw_timing.status !=
                              PreviewDrawTimingStatus::Pending)
                break;
            CHECK(error.empty());
            PreviewStatus status;
            service.CopyStatus(&status);
            if (status.state == PreviewState::Failed ||
                status.state == PreviewState::Unsupported)
                std::fprintf(stderr, "Every-use native preview failure: %s\n",
                             status.message.c_str());
            CHECK(status.state != PreviewState::Failed &&
                  status.state != PreviewState::Unsupported);
            SDL_Delay(5);
        } while (Now() < deadline);
        CHECK(copied);
        CHECK(ValidatePreviewDrawTiming(completion.draw_timing,
                                        job.expected_result));
        CHECK(completion.draw_timing.provenance ==
              PreviewDrawTimingProvenance::ReplayInstrumented);
        CHECK(completion.draw_timing.actual_draw_commands == 1);
        if (completion.draw_timing.status == PreviewDrawTimingStatus::Measured)
            CHECK(completion.draw_timing.nanoseconds > 0 &&
                  completion.draw_timing.nanoseconds <=
                      kPreviewMaxDrawTimingNs);
        else {
            CHECK(completion.draw_timing.status ==
                  PreviewDrawTimingStatus::Unsupported);
            CHECK(!completion.draw_timing.nanoseconds &&
                  !completion.draw_timing.message.empty());
        }
        executor.AfterHudRender();
        const uint32_t i = job.occurrence->summary.key.draw;
        const std::array<uint8_t, 4> expected =
            job.phase == CaptureComparisonPhase::Original ?
                std::array<uint8_t, 4>{ uint8_t(10 + 5 * i),
                                        uint8_t(20 + 3 * i),
                                        uint8_t(255 - 4 * i), 255 } :
                std::array<uint8_t, 4>{ uint8_t(255 - 4 * i),
                                        uint8_t(10 + 5 * i),
                                        uint8_t(20 + 3 * i), 255 };
        const size_t center = (extent / 2 * extent + extent / 2) * 4;
        CHECK(std::equal(expected.begin(), expected.end(),
                         completion.image.rgba.begin() + center));
        CHECK(completion.image.rgba[0] == 0 && completion.image.rgba[1] == 0 &&
              completion.image.rgba[2] == 0);
        const float scale = .65f + float(i) / 256;
        const float offset = (float(i) - 16) / 256;
        const std::array<uint8_t, 4> clear{ 0, 0, 0, 255 };
        for (uint32_t y = 4; y < extent; y += 8) {
            const float clip_y = vk ? 2 * (float(y) + .5f) / extent - 1 :
                                      1 - 2 * (float(y) + .5f) / extent;
            for (uint32_t x = 4; x < extent; x += 8) {
                const float clip_x = 2 * (float(x) + .5f) / extent - 1;
                const float side = (scale - clip_y) / 2;
                const bool inside = clip_y > -scale + .04f &&
                                    clip_y < scale - .04f &&
                                    std::abs(clip_x - offset) < side - .04f;
                const bool outside = clip_y < -scale - .04f ||
                                     clip_y > scale + .04f ||
                                     std::abs(clip_x - offset) > side + .04f;
                if (!inside && !outside)
                    continue; // Raster edge rules are outside this fixture's
                              // admission.
                const auto &sample = inside ? expected : clear;
                const size_t pixel = (y * extent + x) * 4;
                if (!std::equal(sample.begin(), sample.end(),
                                completion.image.rgba.begin() + pixel))
                    std::fprintf(
                        stderr,
                        "Every-use geometry %u at %u,%u (%f,%f), inside %d: "
                        "expected %u,%u,%u,%u actual %u,%u,%u,%u\n",
                        i, x, y, clip_x, clip_y, inside, sample[0], sample[1],
                        sample[2], sample[3], completion.image.rgba[pixel],
                        completion.image.rgba[pixel + 1],
                        completion.image.rgba[pixel + 2],
                        completion.image.rgba[pixel + 3]);
                CHECK(std::equal(sample.begin(), sample.end(),
                                 completion.image.rgba.begin() + pixel));
            }
        }
        const auto digest = ComputePreviewDigest(completion.image.rgba.data(),
                                                 completion.image.rgba.size());
        if (job.phase == CaptureComparisonPhase::Original) {
            original_images.insert(digest);
            pipeline_digests.insert(packet.pipeline_digest);
            material_digests.insert(packet.material_digest);
        } else
            edited_images.insert(digest);
        CHECK(controller.Complete(std::move(completion)));
        ++completed;
    }
    const auto result = controller.Snapshot();
    if (result.state != CaptureComparisonState::Ready ||
        completed != count * 2) {
        std::fprintf(stderr,
                     "Every-use comparison: state %u, jobs %u, message %s\n",
                     uint32_t(result.state), completed, result.message.c_str());
        for (const auto &row : result.results)
            std::fprintf(stderr, "Occurrence %llu outcome %u: %s\n",
                         (unsigned long long)row.identity.event_id,
                         uint32_t(row.outcome), row.message.c_str());
    }
    CHECK(result.state == CaptureComparisonState::Ready &&
          completed == count * 2);
    CHECK(result.outcomes[uint32_t(CaptureComparisonOutcome::Completed)] ==
          count);
    CHECK(original_images.size() == count && edited_images.size() == count &&
          pipeline_digests.size() == count && material_digests.size() == count);
    CHECK(result.original_timing.requested == count &&
          result.replacement_timing.requested == count);
    CHECK(result.original_timing.measured +
                  result.original_timing.unsupported ==
              count &&
          result.replacement_timing.measured +
                  result.replacement_timing.unsupported ==
              count);
    CHECK(!result.original_timing.failed && !result.replacement_timing.failed);
    std::printf(
        "Backend %u replay draw timestamps: original %llu/%u measured, "
        "edited %llu/%u measured; median/p95 %.0f/%.0f and %.0f/%.0f ns\n",
        unsigned(backend), (unsigned long long)result.original_timing.measured,
        count, (unsigned long long)result.replacement_timing.measured, count,
        result.original_timing.median_ns, result.original_timing.p95_ns,
        result.replacement_timing.median_ns, result.replacement_timing.p95_ns);
    for (const auto &row : result.results) {
        CHECK(row.original && row.replacement && row.difference.changed_pixels);
        CHECK(row.replay_class == PreviewReplayClass::Approximate);
        CHECK(row.original_timing.result == row.original_key &&
              row.replacement_timing.result == row.replacement_key);
    }
    std::puts("32 saved/reopened native occurrences: original/edited worker "
              "checkpoints, constants, geometry and two textures passed");
}

struct ActualArchiveAcceptance {
    CaptureSessionSnapshot capture;
    PreviewSelection selection;
    std::vector<std::shared_ptr<const CaptureOccurrence>> uses;
    uint64_t frame = 0;
    std::string edited_source;
};

static std::string CapturedText(const SharedCaptureBlock &block)
{
    return block ? std::string(block->bytes.begin(), block->bytes.end()) : "";
}

static ActualArchiveAcceptance
LoadActualArchive(const std::filesystem::path &path)
{
    ActualArchiveAcceptance result;
    std::string error;
    const bool opened = CaptureSession::Reopen(path, &result.capture, &error);
    ArchiveCheck(opened, "cannot reopen " + path.string() + ": " + error);
    ArchiveCheck(result.capture.context.backend == uint32_t(backend),
                 "CLI renderer does not match the archive renderer");
    using Group = std::pair<uint64_t, ShaderKey>;
    std::map<Group, std::vector<std::shared_ptr<const CaptureOccurrence>>>
        groups;
    for (const auto &event : result.capture.events) {
        if (!event || event->type != CaptureEventType::Draw)
            continue;
        for (size_t i = 0; i < event->summary.shader_count; ++i) {
            const auto &shader = event->summary.shaders[i];
            if (shader.stage == Stage::Pixel)
                groups[{ event->summary.key.frame, shader }].push_back(event);
        }
    }
    for (const auto &group : groups) {
        if (group.second.size() != 32)
            continue;
        result.frame = group.first.first;
        result.selection.shader = group.first.second;
        result.uses = group.second;
        break;
    }
    ArchiveCheck(
        result.uses.size() == 32,
        "archive has no captured frame with exactly 32 shared-PS draws");
    result.selection.scope = result.capture.context.scope;
    result.selection.session_epoch = result.capture.context.session_epoch;
    result.selection.renderer_epoch = result.capture.context.renderer_epoch;
    result.selection.backend = backend;
    std::set<std::pair<uint32_t, uint32_t>> geometry_ranges;
    std::set<PreviewDigest> vertex_constants, pixel_constants, texture_contents;
    std::set<uint64_t> events;
    std::map<uint32_t, unsigned> topology_counts;
    std::map<uint32_t, std::string> geometry_sources;
    const auto &first = *result.uses.front();
    const auto vertex = CapturedText(first.inputs.sources[1]);
    const auto pixel = CapturedText(first.inputs.sources[2]);
    const auto geometry = CapturedText(first.inputs.sources[3]);
    ArchiveCheck(
        !vertex.empty() && !pixel.empty() && !geometry.empty(),
        "authored fixture requires retained actual VS, PS and host GS");
    for (const auto &event : result.uses) {
        const auto label = "event " + std::to_string(event->event_id);
        ArchiveCheck(events.insert(event->event_id).second,
                     "duplicate " + label);
        ArchiveCheck(event->finished && event->emitted && !event->pending &&
                         event->inputs.complete,
                     label + " is not an emitted, finalized owned draw: " +
                         event->failure);
        ArchiveCheck(event->summary.key.session_epoch ==
                             result.selection.session_epoch &&
                         event->summary.key.renderer_epoch ==
                             result.selection.renderer_epoch,
                     label + " has inconsistent ownership epochs");
        bool has_vertex = false, has_geometry = false;
        for (size_t i = 0; i < event->summary.shader_count; ++i) {
            has_vertex |= event->summary.shaders[i].stage == Stage::Vertex;
            has_geometry |= event->summary.shaders[i].stage == Stage::Geometry;
        }
        ArchiveCheck(has_vertex && has_geometry,
                     label + " lacks bound VS/host-GS identity");
        ArchiveCheck(CapturedText(event->inputs.sources[1]) == vertex &&
                         CapturedText(event->inputs.sources[2]) == pixel,
                     label + " changed its paired VS or PS source");
        const uint32_t primitive = event->summary.primitive_mode;
        ArchiveCheck(primitive >= 5 && primitive <= 10,
                     label + " has an unexpected authored topology");
        ++topology_counts[primitive];
        const std::string host_geometry =
            CapturedText(event->inputs.sources[3]);
        ArchiveCheck(!host_geometry.empty(),
                     label + " has no retained host GS source");
        const auto source = geometry_sources.emplace(primitive, host_geometry);
        ArchiveCheck(source.second || source.first->second == host_geometry,
                     label + " changed its host GS within one topology");
        const auto inputs = event->CopyInputs();
        const auto reg = [&](const char *name) {
            for (const auto &value : inputs.registers)
                if (value.name == name)
                    return value.value;
            return UINT32_MAX;
        };
        const auto first_vertex =
            reg(backend == PreviewBackend::Vulkan ? "capture.vertices.first" :
                                                    "capture.first_vertex");
        const auto end =
            reg(backend == PreviewBackend::Vulkan ? "capture.vertices.count" :
                                                    "capture.last_vertex");
        ArchiveCheck(first_vertex != UINT32_MAX && end != UINT32_MAX,
                     label + " lacks the captured geometry range");
        geometry_ranges.insert({ first_vertex, end });
        for (const auto &uniform : inputs.uniforms) {
            const auto digest =
                ComputePreviewDigest(uniform.data.data(), uniform.data.size());
            if (uniform.name == "c" || uniform.name == "c[0]")
                vertex_constants.insert(digest);
            if (uniform.name == "consts" || uniform.name == "consts[0]")
                pixel_constants.insert(digest);
        }
        bool has_texture = false;
        for (const auto &texture : inputs.textures) {
            if (!texture.described || !texture.metadata.bound)
                continue;
            ArchiveCheck(!texture.images.empty(),
                         label + " has a bound texture without owned pixels");
            for (const auto &image : texture.images) {
                ArchiveCheck(!image.image.rgba.empty(),
                             label + " has an empty texture snapshot");
                texture_contents.insert(ComputePreviewDigest(
                    image.image.rgba.data(), image.image.rgba.size()));
                has_texture = true;
            }
        }
        ArchiveCheck(has_texture, label + " has no authored texture snapshot");
    }
    ArchiveCheck(geometry_ranges.size() == 32 &&
                     vertex_constants.size() == 32 &&
                     pixel_constants.size() == 32,
                 "all 32 uses must retain distinct geometry ranges, VS "
                 "constants and PS constants");
    ArchiveCheck(
        texture_contents.size() == 2,
        "authored fixture must retain exactly two distinct texture contents");
    const std::map<uint32_t, unsigned> triangle_fixture = { { 5, 32 } };
    const std::map<uint32_t, unsigned> filled_fixture = { { 5, 6 }, { 6, 6 },
                                                          { 7, 5 }, { 8, 5 },
                                                          { 9, 5 }, { 10, 5 } };
    ArchiveCheck(topology_counts == triangle_fixture ||
                     topology_counts == filled_fixture,
                 "authored fixture must retain all32 list draws or all32 "
                 "mixed filled-topology draws with their original modes");
    const auto main = pixel.find("void main()");
    ArchiveCheck(
        main != std::string::npos &&
            pixel.find("void main()", main + 1) == std::string::npos &&
            pixel.find("out vec4 fragColor") != std::string::npos,
        "captured generated PS has an unrecognized main/output interface");
    result.edited_source = pixel;
    result.edited_source.replace(main + 5, 4, "archive_original_main");
    result.edited_source += "\nvoid "
                            "main(){archive_original_main();fragColor.rgb=vec3("
                            "1.0)-fragColor.rgb;}\n";
    std::printf("Actual archive: frame %llu, PS %s, 32 distinct geometry/VS/PS "
                "constants, "
                "two texture contents, retained paired VS and host GS\n",
                (unsigned long long)result.frame,
                PortableShaderHash(result.selection.shader.hash).c_str());
    std::fflush(stdout);
    return result;
}

static bool CheckActualCheckpoint(const CaptureOccurrence &event,
                                  const PreviewPacket &packet,
                                  const CaptureComparisonImage &rendered)
{
    const auto inputs = event.CopyInputs();
    if (inputs.before.rgba.empty() || inputs.after.rgba.empty())
        return false;
    const auto &pipeline = *packet.captured_pipeline;
    const auto &raster = pipeline.raster;
    const bool supported = raster.available == 1023 && !raster.depth_test &&
                           !raster.stencil_test &&
                           pipeline.color_before.rgba == inputs.before.rgba;
    ArchiveCheck(rendered.width == inputs.after.width &&
                     rendered.height == inputs.after.height &&
                     rendered.rgba.size() == inputs.after.rgba.size(),
                 "event " + std::to_string(event.event_id) +
                     " checkpoint extent mismatch");
    uint64_t total_difference = 0, changed_pixels = 0;
    uint8_t maximum = 0;
    for (size_t p = 0; p < rendered.rgba.size(); p += 4) {
        bool changed = false;
        for (size_t channel = 0; channel < 4; ++channel) {
            const auto difference =
                uint8_t(std::abs(int(rendered.rgba[p + channel]) -
                                 int(inputs.after.rgba[p + channel])));
            maximum = std::max(maximum, difference);
            total_difference += difference;
            changed |= difference != 0;
        }
        changed_pixels += changed;
    }
    const double mean = double(total_difference) / rendered.rgba.size();
    std::printf(
        "Actual event %llu observed checkpoint: changed pixels %llu, "
        "max channel error %u, mean %.6f; raster bits 0x%X, owned seed %s, "
        "depth/stencil tests %u/%u\n",
        (unsigned long long)event.event_id, (unsigned long long)changed_pixels,
        unsigned(maximum), mean, raster.available,
        pipeline.color_before.rgba.empty() ? "unavailable" : "retained",
        raster.depth_test, raster.stencil_test);
    std::fflush(stdout);
    if (!supported) {
        std::fprintf(stderr,
                     "Actual event %llu exact checkpoint unsupported: %s\n",
                     (unsigned long long)event.event_id,
                     DescribePreviewCapturedRaster(pipeline).c_str());
        return false;
    }
    ArchiveCheck(maximum <= 1,
                 "event " + std::to_string(event.event_id) +
                     " original replay diverges from its observed checkpoint "
                     "(tolerance 1 RGBA8 unit)");
    return true;
}

static void TestActualArchive(ActualArchiveAcceptance &archive,
                              PreviewGlExecutor &executor,
                              PreviewPacket &packet,
                              const std::function<void()> &draw)
{
    CaptureComparisonRequest request;
    request.selection = archive.selection;
    request.scope = CaptureComparisonScope::MatchingFrame;
    request.first_frame = request.last_frame = archive.frame;
    request.edit = { 901, 1, 1, archive.edited_source };
    request.profile_draw = true;
    CaptureComparisonSettings settings;
    settings.image_byte_budget = 128U * 1024U * 1024U;
    CaptureComparisonController controller;
    ArchiveCheck(controller.Start(request, archive.capture, settings) != 0,
                 "comparison request rejected: " +
                     controller.Snapshot().message);
    ArchiveCheck(controller.Snapshot().matched_events == 32,
                 "comparison request did not select all 32 actual uses");
    auto &service = GetPreviewService();
    service.UsePacketView();
    service.EditChannel(PreviewChannel::FinalRGBA);
    std::set<uint64_t> original_events, edited_events;
    std::set<PreviewDigest> original_images, edited_images;
    std::map<PreviewDigest, std::vector<uint64_t>> original_image_events,
        edited_image_events;
    uint32_t jobs = 0, checkpoints = 0, checkpoint_pairs = 0;
    CaptureComparisonJob job;
    while (controller.TryClaimJob(&job)) {
        packet = *job.packet;
        const std::string label =
            "event " + std::to_string(job.identity.event_id) +
            (job.phase == CaptureComparisonPhase::Original ? " original" :
                                                             " edited");
        ArchiveCheck(packet.packet_kind == PreviewPacketKind::Replay &&
                         packet.captured_pipeline && packet.captured_material &&
                         packet.profile_draw,
                     label + " is not a profiled native pipeline replay");
        ArchiveCheck(packet.partner_source ==
                             CapturedText(job.occurrence->inputs.sources[1]) &&
                         packet.captured_pipeline->geometry_source ==
                             CapturedText(job.occurrence->inputs.sources[3]),
                     label + " replaced captured VS/host-GS evidence");
        ArchiveCheck(BuildPreviewResultKey(packet) == job.expected_result,
                     label + " packet/result identity mismatch");
        const auto expected_source =
            job.phase == CaptureComparisonPhase::Original ?
                CapturedText(job.occurrence->inputs.sources[2]) :
                archive.edited_source;
        ArchiveCheck(packet.source == expected_source,
                     label + " used a substitute PS source");
        service.SetVisible(true, Now());
        service.SetSelection(packet.selection, Now());
        std::string error;
        const bool submitted = service.SubmitPacket(packet, Now(), &error);
        ArchiveCheck(submitted, label + ": " + error);
        CaptureComparisonCompletion completion;
        completion.identity = job.identity;
        completion.phase = job.phase;
        completion.result = job.expected_result;
        bool copied = false;
        const auto deadline = Now() + UINT64_C(30000000000);
        do {
            draw();
            copied = executor.CopyReadyImage(
                job.expected_result, &completion.image.width,
                &completion.image.height, &completion.image.rgba, &error,
                &completion.draw_timing);
            if (copied && completion.draw_timing.status !=
                              PreviewDrawTimingStatus::Pending)
                break;
            ArchiveCheck(error.empty(), label + " image copy: " + error);
            PreviewStatus status;
            service.CopyStatus(&status);
            ArchiveCheck(status.state != PreviewState::Failed &&
                             status.state != PreviewState::Unsupported,
                         label + " worker: " + status.message);
            SDL_Delay(5);
        } while (Now() < deadline);
        ArchiveCheck(copied, label + " timed out waiting for exact result");
        ArchiveCheck(ValidatePreviewDrawTiming(completion.draw_timing,
                                               job.expected_result) &&
                         completion.draw_timing.provenance ==
                             PreviewDrawTimingProvenance::ReplayInstrumented &&
                         (completion.draw_timing.status ==
                              PreviewDrawTimingStatus::Measured ||
                          completion.draw_timing.status ==
                              PreviewDrawTimingStatus::Unsupported),
                     label + " lacks terminal exact-result replay timing");
        const auto digest = ComputePreviewDigest(completion.image.rgba.data(),
                                                 completion.image.rgba.size());
        if (job.phase == CaptureComparisonPhase::Original) {
            ArchiveCheck(original_events.insert(job.identity.event_id).second,
                         label + " executed twice");
            original_images.insert(digest);
            original_image_events[digest].push_back(job.identity.event_id);
            if (job.occurrence->inputs.before.rgba &&
                job.occurrence->inputs.after.rgba) {
                ++checkpoint_pairs;
                checkpoints += CheckActualCheckpoint(*job.occurrence, packet,
                                                     completion.image);
            }
        } else {
            ArchiveCheck(edited_events.insert(job.identity.event_id).second,
                         label + " executed twice");
            edited_images.insert(digest);
            edited_image_events[digest].push_back(job.identity.event_id);
        }
        executor.AfterHudRender();
        ArchiveCheck(controller.Complete(std::move(completion)),
                     label + " completion rejected");
        ++jobs;
        std::printf("Actual archive %u/64: %s\n", jobs, label.c_str());
        std::fflush(stdout);
    }
    const auto result = controller.Snapshot();
    for (const auto &row : result.results)
        if (row.outcome != CaptureComparisonOutcome::Completed)
            std::fprintf(stderr, "Actual occurrence %llu outcome %u: %s\n",
                         (unsigned long long)row.identity.event_id,
                         unsigned(row.outcome), row.message.c_str());
    ArchiveCheck(
        result.state == CaptureComparisonState::Ready && jobs == 64 &&
            original_events.size() == 32 && edited_events == original_events &&
            result.outcomes[unsigned(CaptureComparisonOutcome::Completed)] ==
                32,
        "all 32 actual original/edited uses must execute; unsupported draws "
        "are failures");
    if (original_images.size() != 32 || edited_images.size() != 32) {
        std::fprintf(stderr,
                     "Actual image uniqueness: original %zu/32, "
                     "edited %zu/32\n",
                     original_images.size(), edited_images.size());
        for (const auto *groups :
             { &original_image_events, &edited_image_events }) {
            for (const auto &group : *groups) {
                if (group.second.size() < 2)
                    continue;
                std::fprintf(stderr, "%s repeated image events:",
                             groups == &original_image_events ? "Original" :
                                                                "Edited");
                for (const auto event : group.second)
                    std::fprintf(stderr, " %llu", (unsigned long long)event);
                std::fprintf(stderr, "\n");
            }
        }
    }
    ArchiveCheck(
        original_images.size() == 32 && edited_images.size() == 32,
        "actual geometry/material uses collapsed to repeated output images");
    for (const auto &row : result.results)
        ArchiveCheck(row.has_packet_identity && row.original &&
                         row.replacement && row.difference.changed_pixels &&
                         row.original_timing.result == row.original_key &&
                         row.replacement_timing.result == row.replacement_key &&
                         row.replacement_key.compile.draft_id ==
                             request.edit.id &&
                         row.replacement_key.compile.draft_revision ==
                             request.edit.revision,
                     "event " + std::to_string(row.identity.event_id) +
                         " lost exact keys, shared edit revision, or a visible "
                         "source effect");
    for (const auto *timing :
         { &result.original_timing, &result.replacement_timing })
        ArchiveCheck(
            timing->requested == 32 &&
                timing->measured + timing->unsupported == 32 &&
                !timing->pending && !timing->failed,
            "GPU timing coverage does not account for all 32 actual uses");
    std::printf(
        "Actual archive timing: original/edited measured %llu/%llu; "
        "median/p95 %.0f/%.0f and %.0f/%.0f ns; checkpoints %u/%u supported\n",
        (unsigned long long)result.original_timing.measured,
        (unsigned long long)result.replacement_timing.measured,
        result.original_timing.median_ns, result.original_timing.p95_ns,
        result.replacement_timing.median_ns, result.replacement_timing.p95_ns,
        checkpoints, checkpoint_pairs);
    ArchiveCheck(checkpoint_pairs && checkpoints == checkpoint_pairs,
                 "observed checkpoint acceptance requires retained raster/seed "
                 "evidence and "
                 "disabled depth/stencil tests; missing checkpoint evidence is "
                 "not a pass");
    std::puts("Actual saved NV2A archive: all 32 paired original/edited uses "
              "and observed checkpoints passed");
}

int main(int argc, char **argv)
{
    std::optional<ActualArchiveAcceptance> archive;
    ArchiveCheck(argc == 1 || argc == 2 || argc == 4,
                 "usage: preview-lifecycle [opengl|vulkan] [--capture "
                 "archive-directory]");
    if (argc > 1)
        ArchiveCheck(std::string(argv[1]) == "vulkan" ||
                         std::string(argv[1]) == "opengl",
                     "renderer must be opengl or vulkan");
    if (argc > 1 && std::string(argv[1]) == "vulkan")
        backend = PreviewBackend::Vulkan;
    if (argc == 4) {
        ArchiveCheck(std::string(argv[2]) == "--capture",
                     "expected --capture archive-directory");
        archive = LoadActualArchive(std::filesystem::u8path(argv[3]));
    }
    CHECK(SDL_Init(SDL_INIT_VIDEO));
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION,
                         backend == PreviewBackend::OpenGL ? 3 : 0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,
                        SDL_GL_CONTEXT_PROFILE_CORE);
    auto *window = SDL_CreateWindow("Preview lifecycle", 800, 600,
                                    SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
    CHECK(window);
    auto context = SDL_GL_CreateContext(window);
    CHECK(context);
    // Model the main/external HUD share group. All image sampling below uses
    // context; cleanup must switch back to that consumer before Shutdown.
    SDL_GL_SetAttribute(SDL_GL_SHARE_WITH_CURRENT_CONTEXT, 1);
    auto *main_window = SDL_CreateWindow("Main HUD context", 64, 64,
                                         SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
    CHECK(main_window);
    auto main_context = SDL_GL_CreateContext(main_window);
    CHECK(main_context);
    SDL_GL_SetAttribute(SDL_GL_SHARE_WITH_CURRENT_CONTEXT, 0);
    CHECK(SDL_GL_MakeCurrent(window, context));
    fprintf(stderr, "Lifecycle GL context: %s\n",
            glGetString(GL_VERSION));
    TestGLSubmissionSources();
    if (backend == PreviewBackend::OpenGL)
        CHECK(epoxy_gl_version() >= 43);
    ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr;
    ImGui::GetIO().DisplaySize = ImVec2(800, 600);
    CHECK(ImGui_ImplOpenGL3_Init("#version 400"));
    auto &service = GetPreviewService();
    PreviewGlExecutor executor(BindWorkerContext);
    auto packet = Packet(PreviewMode::Normal, false, 0);
    service.SetEnabled(true);
    service.SetVisible(true, Now());
    service.SetGuestPaused(true);
    std::string error;
    auto submit = [&] {
        fprintf(stderr, "Submit mode %d epoch %llu\n",
                int(packet.selection.mode),
                (unsigned long long)packet.selection.renderer_epoch);
        service.SetVisible(true, Now());
        service.SetSelection(packet.selection, Now());
        CHECK(service.SubmitPacket(packet, Now(), &error));
    };
    submit();
    CHECK(SDL_GL_MakeCurrent(nullptr, nullptr));
    CHECK(service.RequestPreparation(&error));
    CHECK(!executor.StartWhilePaused(&error));
    CHECK(error == "HUD OpenGL context is unavailable");
    PreviewStatus missing_hud;
    service.CopyStatus(&missing_hud);
    CHECK(!missing_hud.preparation_requested &&
          missing_hud.state == PreviewState::NeedsPreparation);
    CHECK(SDL_GL_MakeCurrent(window, context));
    CHECK(service.RequestPreparation(&error));
    CHECK(!executor.StartWhilePaused(&error));
    CHECK(error.find("injected first worker bind failure") !=
          std::string::npos);
    PreviewStatus startup_failure;
    service.CopyStatus(&startup_failure);
    CHECK(!startup_failure.preparation_requested &&
          !startup_failure.work_active &&
          startup_failure.state == PreviewState::NeedsPreparation);
    std::set<GLuint> textures;
    auto draw = [&] {
        service.SetVisible(true, Now());
        if (service.RequestAutomaticPreparation(Now()))
            CHECK(executor.StartWhilePaused(&error));
        ImGui_ImplOpenGL3_NewFrame();
        ImGui::NewFrame();
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImVec2(800, 600));
        ImGui::Begin("Preview");
        executor.DrawImage(600, 600, &packet.selection, Now(), nullptr);
        ImGui::End();
        ImGui::Render();
        textures.clear();
        auto *data = ImGui::GetDrawData();
        const auto font = ImGui::GetIO().Fonts->TexID;
        for (int i = 0; i < data->CmdListsCount; ++i)
            for (const auto &cmd : data->CmdLists[i]->CmdBuffer)
                if (cmd.GetTexID() != font && cmd.GetTexID())
                    textures.insert(static_cast<GLuint>(cmd.GetTexID()));
        glViewport(0, 0, 800, 600);
        ImGui_ImplOpenGL3_RenderDrawData(data);
        executor.AfterHudRender();
        const GLenum gl_error = glGetError();
        if (gl_error)
            fprintf(stderr, "HUD GL error: %x\n", gl_error);
        CHECK(gl_error == GL_NO_ERROR);
    };
    if (archive) {
        TestActualArchive(*archive, executor, packet, draw);
        service.SetEnabled(false);
        draw();
        executor.Shutdown();
        ImGui_ImplOpenGL3_Shutdown();
        ImGui::DestroyContext();
        SDL_GL_DestroyContext(main_context);
        SDL_DestroyWindow(main_window);
        SDL_GL_DestroyContext(context);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return EXIT_SUCCESS;
    }
    auto await = [&](auto predicate) {
        const auto deadline = Now() + UINT64_C(10000000000);
        do {
            draw();
            if (predicate())
                return;
            SDL_Delay(10);
        } while (Now() < deadline);
        PreviewStatus status;
        service.CopyStatus(&status);
        fprintf(stderr, "Timeout: %s\n", status.message.c_str());
        CHECK(false);
    };
    auto colors = [&] {
        std::set<unsigned> result;
        for (GLuint texture : textures) {
            glBindTexture(GL_TEXTURE_2D, texture);
            std::vector<uint8_t> rgba(160 * 160 * 4);
            glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                          rgba.data());
            const size_t center = (80 * 160 + 80) * 4;
            result.insert((unsigned(rgba[center]) << 16) |
                          (unsigned(rgba[center + 1]) << 8) | rgba[center + 2]);
        }
        return result;
    };
    await([&] { return executor.HasDisplayed(); });
    CHECK(colors() == std::set<unsigned>{ 0xff0000 });
    packet.packet_kind = PreviewPacketKind::Replay;
    packet.replay_class = PreviewReplayClass::Approximate;
    packet.captured_mesh.positions = {
        {-1, -1, 0, 1}, {1, -1, 0, 1}, {0, 1, 0, 1}};
    packet.captured_mesh.indices = {0, 1, 2};
    packet.mesh_digest = ComputeCapturedMeshDigest(packet.captured_mesh);
    submit();
    await([&] {
        PreviewStatus current;
        service.CopyStatus(&current);
        return current.has_displayed_source &&
               current.displayed_result.packet_kind == PreviewPacketKind::Replay &&
               current.displayed_result.mesh_digest == packet.mesh_digest;
    });
    CHECK(colors() == std::set<unsigned>{ 0xff0000 });
    const PreviewPacket geometry_only_packet = packet;
    packet.source = backend == PreviewBackend::OpenGL ?
                        "#version 400\nuniform sampler2D texSamp0; uniform "
                        "vec4 fogColor; out vec4 color;\n" :
                        "#version 450\nlayout(binding=3) uniform sampler2D "
                        "texSamp0; layout(binding=1,std140) uniform U {vec4 "
                        "fogColor;}; layout(location=0) out vec4 color;\n";
    packet.source += "void main(){color=texture(texSamp0,vec2(0.5))+fogColor;}";
    packet.partner_source =
        BuildPreviewSyntheticVertexSource(packet.source, backend);
    packet.source_digest = ComputePreviewDigest(
        reinterpret_cast<const uint8_t *>(packet.source.data()),
        packet.source.size());
    packet.partner_digest = ComputePreviewDigest(
        reinterpret_cast<const uint8_t *>(packet.partner_source.data()),
        packet.partner_source.size());
    OwnedDrawInputs material_inputs;
    material_inputs.complete = true;
    auto &captured_texture = material_inputs.textures[0];
    captured_texture.described = true;
    captured_texture.metadata.bound = true;
    captured_texture.metadata.width = captured_texture.metadata.height = 2;
    captured_texture.metadata.depth = captured_texture.metadata.face_count =
        captured_texture.metadata.mip_levels = 1;
    captured_texture.metadata.min_filter =
        captured_texture.metadata.mag_filter = 0x2600;
    captured_texture.metadata.wrap_s = captured_texture.metadata.wrap_t =
        captured_texture.metadata.wrap_r = 0x812f;
    OwnedDrawImage blue_image{ 2, 2, std::vector<uint8_t>(16) };
    for (size_t i = 0; i < 16; i += 4)
        blue_image.rgba[i + 2] = blue_image.rgba[i + 3] = 255;
    captured_texture.images.push_back({ 0, 0, std::move(blue_image) });
    std::array<float, 4> captured_fog{ 0, 1, 0, 0 };
    const auto *fog_bytes =
        reinterpret_cast<const uint8_t *>(captured_fog.data());
    material_inputs.uniforms.push_back(
        { 2, "fogColor", XEMU_SHADER_DRAW_UNIFORM_FLOAT, 4, 1,
          std::vector<uint8_t>(fog_bytes, fog_bytes + sizeof(captured_fog)) });
    packet.captured_material = BuildPreviewCapturedMaterial(material_inputs);
    packet.material_digest =
        ComputePreviewCapturedMaterialDigest(*packet.captured_material);
    submit();
    await([&] {
        PreviewStatus current;
        service.CopyStatus(&current);
        return current.has_displayed_source &&
               current.displayed_result.material_digest ==
                   packet.material_digest;
    });
    CHECK(colors() == std::set<unsigned>{ 0x00ffff });
    const auto material_compile = BuildPreviewCompileKey(packet);
    captured_fog = { 1, 0, 0, 0 };
    material_inputs.uniforms[0].data.assign(fog_bytes,
                                            fog_bytes + sizeof(captured_fog));
    packet.captured_material = BuildPreviewCapturedMaterial(material_inputs);
    packet.material_digest =
        ComputePreviewCapturedMaterialDigest(*packet.captured_material);
    CHECK(BuildPreviewCompileKey(packet) == material_compile);
    submit();
    await([&] {
        PreviewStatus current;
        service.CopyStatus(&current);
        return current.has_displayed_source &&
               current.displayed_result.material_digest ==
                   packet.material_digest;
    });
    CHECK(colors() == std::set<unsigned>{ 0xff00ff });
    captured_texture.images.clear();
    packet.captured_material = BuildPreviewCapturedMaterial(material_inputs);
    packet.material_digest =
        ComputePreviewCapturedMaterialDigest(*packet.captured_material);
    submit();
    await([&] {
        PreviewStatus current;
        service.CopyStatus(&current);
        return current.has_displayed_source &&
               current.displayed_result.material_digest ==
                   packet.material_digest;
    });
    CHECK(DescribePreviewCapturedMaterial(*packet.captured_material)
              .find("missing texture") != std::string::npos);
    PreviewStatus missing_material_status;
    service.CopyStatus(&missing_material_status);
    CHECK(missing_material_status.material_fidelity.find("missing texture") !=
          std::string::npos);
    CHECK(missing_material_status.material_fidelity.find(
              "texture slots use zero") != std::string::npos);
    CHECK(colors() == std::set<unsigned>{ 0xff0000 });
    OwnedDrawInputs native_inputs;
    native_inputs.complete = true;
    const bool native_vk = backend == PreviewBackend::Vulkan;
    native_inputs.sources[1] =
        native_vk ?
            "#version 450\nlayout(binding=0,std140) uniform V {vec4 "
            "cameraOffset;};\nlayout(location=0) out vec4 vertexColor;\n" :
            "#version 400\nuniform vec4 cameraOffset; out vec4 vertexColor;\n";
    native_inputs.sources[1] +=
        "layout(location=0) in vec4 position; layout(location=1) in vec4 "
        "rawColor;\n"
        "void main(){gl_Position=position+cameraOffset;vertexColor=rawColor;}";
    native_inputs.sources[3] =
        native_vk ?
            "#version 450\nlayout(location=0) in vec4 vertexColor[]; "
            "layout(location=0) out vec4 geometryColor;\n" :
            "#version 400\nin vec4 vertexColor[]; out vec4 geometryColor;\n";
    native_inputs.sources[3] +=
        "layout(triangles) in; layout(triangle_strip,max_vertices=3) out;\n"
        "void main(){for(int "
        "i=0;i<3;i++){gl_Position=gl_in[i].gl_Position;geometryColor="
        "vertexColor[i]*vec4(0,1,1,1);EmitVertex();}EndPrimitive();}";
    if (native_vk) {
        native_inputs.registers = { { "capture.vertices.first", 1 },
                                    { "capture.vertices.count", 3 },
                                    { "capture.vertex.0.enabled", 1 },
                                    { "capture.vertex.1.enabled", 1 } };
    } else {
        native_inputs.registers = { { "capture.first_vertex", 1 },
                                    { "capture.last_vertex", 3 },
                                    { "vertex.enabled0", 1 },
                                    { "vertex.enabled1", 1 } };
    }
    const std::array<float, 12> native_positions{ -0.8f, -0.8f, 0, 1,
                                                  0.8f,  -0.8f, 0, 1,
                                                  0,     0.8f,  0, 1 };
    const std::array<float, 12> native_colors{ 1, 1, 0, 1, 1, 1,
                                               0, 1, 1, 1, 0, 1 };
    for (uint32_t slot = 0; slot < 2; ++slot) {
        const auto *bytes = reinterpret_cast<const uint8_t *>(
            slot ? native_colors.data() : native_positions.data());
        OwnedDrawBlob stream;
        stream.name = "vertex.attribute" + std::to_string(slot);
        stream.bytes.assign(bytes, bytes + 48);
        stream.slot = slot;
        stream.format = native_vk ? 109 : GL_FLOAT;
        stream.components = 4;
        stream.stride = 16;
        stream.count = 3;
        native_inputs.blobs.push_back(std::move(stream));
    }
    const std::array<uint32_t, 3> native_indices{ 1, 2, 3 };
    OwnedDrawBlob index_blob;
    index_blob.name = native_vk ? "vertex.indices" : "geometry.host_indices";
    const auto *index_bytes =
        reinterpret_cast<const uint8_t *>(native_indices.data());
    index_blob.bytes.assign(index_bytes, index_bytes + 12);
    index_blob.count = 3;
    native_inputs.blobs.push_back(std::move(index_blob));
    const std::array<float, 4> native_offset{ 0.2f, 0, 0, 0 };
    const auto *offset_bytes =
        reinterpret_cast<const uint8_t *>(native_offset.data());
    native_inputs.uniforms.push_back(
        { native_vk ? 1U : 0U, "cameraOffset", XEMU_SHADER_DRAW_UNIFORM_FLOAT,
          4, 1, std::vector<uint8_t>(offset_bytes, offset_bytes + 16) });
    packet.captured_pipeline =
        BuildPreviewCapturedPipeline(native_inputs, backend, 5, &error);
    CHECK(packet.captured_pipeline);
    packet.pipeline_digest =
        ComputePreviewCapturedPipelineDigest(*packet.captured_pipeline);
    packet.pipeline_layout_digest =
        ComputePreviewCapturedPipelineDigest(*packet.captured_pipeline, true);
    packet.partner_source = native_inputs.sources[1];
    packet.source = native_vk ?
                        "#version 450\nlayout(location=0) in vec4 "
                        "geometryColor;layout(location=0) out vec4 color;" :
                        "#version 400\nin vec4 geometryColor;out vec4 color;";
    packet.source += "void main(){color=geometryColor;}";
    packet.source_digest = ComputePreviewDigest(
        reinterpret_cast<const uint8_t *>(packet.source.data()),
        packet.source.size());
    packet.partner_digest = ComputePreviewDigest(
        reinterpret_cast<const uint8_t *>(packet.partner_source.data()),
        packet.partner_source.size());
    packet.captured_mesh = {};
    packet.mesh_digest = {};
    packet.scene.yaw = 80;
    packet.scene.distance = 20;
    packet.scene.pan = { 1, 1 };
    submit();
    await([&] {
        PreviewStatus current;
        service.CopyStatus(&current);
        return current.has_displayed_source &&
               current.displayed_result.pipeline_digest ==
                   packet.pipeline_digest;
    });
    CHECK(colors() == std::set<unsigned>{ 0x00ff00 });
    PreviewStatus native_status;
    service.CopyStatus(&native_status);
    CHECK(native_status.material_fidelity.find("Original VS/GS camera") !=
          std::string::npos);
    std::vector<uint8_t> native_checkpoint;
    uint32_t checkpoint_width = 0, checkpoint_height = 0;
    GLuint pack_buffer, preserved_texture;
    glGenBuffers(1, &pack_buffer);
    glBindBuffer(GL_PIXEL_PACK_BUFFER, pack_buffer);
    glPixelStorei(GL_PACK_ROW_LENGTH, 173);
    glPixelStorei(GL_PACK_SKIP_ROWS, 2);
    glGetIntegerv(GL_TEXTURE_BINDING_2D,
                  reinterpret_cast<GLint *>(&preserved_texture));
    CHECK(!executor.CopyReadyImage(native_status.displayed_result,
                                   &checkpoint_width, &checkpoint_height,
                                   &native_checkpoint, &error));
    CHECK(error.empty() && native_checkpoint.empty());
    await([&] {
        const bool ready = executor.CopyReadyImage(
            native_status.displayed_result, &checkpoint_width,
            &checkpoint_height, &native_checkpoint, &error);
        CHECK(error.empty());
        return ready;
    });
    CHECK(checkpoint_width == 160 && checkpoint_height == 160 &&
          native_checkpoint.size() == 160 * 160 * 4);
    GLint preserved;
    glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING, &preserved);
    CHECK(GLuint(preserved) == pack_buffer);
    glGetIntegerv(GL_PACK_ROW_LENGTH, &preserved);
    CHECK(preserved == 173);
    glGetIntegerv(GL_PACK_SKIP_ROWS, &preserved);
    CHECK(preserved == 2);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &preserved);
    CHECK(GLuint(preserved) == preserved_texture);
    glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
    glDeleteBuffers(1, &pack_buffer);
    glPixelStorei(GL_PACK_ROW_LENGTH, 0);
    glPixelStorei(GL_PACK_SKIP_ROWS, 0);
    CHECK(native_checkpoint[(80 * 160 + 80) * 4 + 1] == 255);
    auto unexpected_key = native_status.displayed_result;
    unexpected_key.pipeline_digest[0] ^= 1;
    CHECK(!executor.CopyReadyImage(unexpected_key, &checkpoint_width,
                                   &checkpoint_height, &native_checkpoint,
                                   &error));
    CHECK(error.empty());
    executor.AfterHudRender();
    executor.AfterHudRender();
    service.RequestCurrentFrame();
    const uint64_t checkpoint_deadline = Now() + UINT64_C(10000000000);
    PreviewStatus checkpoint_status;
    do {
        service.SetVisible(true, Now());
        service.CopyStatus(&checkpoint_status);
        if (checkpoint_status.ready_slots)
            break;
        SDL_Delay(10);
    } while (Now() < checkpoint_deadline);
    CHECK(checkpoint_status.ready_slots);
    const auto ready_before_cached_copy = checkpoint_status.ready_slots;
    const auto leased_before_cached_copy = checkpoint_status.leased_slots;
    const auto owned_checkpoint = native_checkpoint;
    CHECK(executor.CopyReadyImage(native_status.displayed_result,
                                  &checkpoint_width, &checkpoint_height,
                                  &native_checkpoint, &error));
    service.CopyStatus(&checkpoint_status);
    CHECK(native_checkpoint == owned_checkpoint &&
          checkpoint_status.ready_slots == ready_before_cached_copy &&
          checkpoint_status.leased_slots == leased_before_cached_copy);
    executor.AfterHudRender();
    std::puts("Original camera raw attributes + vertex constants + GS and "
              "explicit owned checkpoint passed");
    // PGR2 submits to a 1280 x 480 target. Preserve that target and its owned
    // destination rather than clamping the captured viewport to a fixture.
    const auto small_pipeline = packet.captured_pipeline;
    auto wide_inputs = native_inputs;
    OwnedDrawBlob wide_viewport;
    wide_viewport.name = native_vk ? "vk.viewport" : "host.viewport";
    if (native_vk) {
        const float viewport[] = { 0, 0, 1280, 480, 0, 1 };
        const auto *begin = reinterpret_cast<const uint8_t *>(viewport);
        wide_viewport.bytes.assign(begin, begin + sizeof(viewport));
    } else {
        const int32_t viewport[] = { 0, 0, 1280, 480 };
        const auto *begin = reinterpret_cast<const uint8_t *>(viewport);
        wide_viewport.bytes.assign(begin, begin + sizeof(viewport));
    }
    wide_inputs.blobs.push_back(std::move(wide_viewport));
    wide_inputs.before.width = 1280;
    wide_inputs.before.height = 480;
    wide_inputs.before.rgba.resize(1280 * 480 * 4);
    for (size_t pixel = 0; pixel < wide_inputs.before.rgba.size(); pixel += 4) {
        wide_inputs.before.rgba[pixel] = 17;
        wide_inputs.before.rgba[pixel + 1] = 29;
        wide_inputs.before.rgba[pixel + 2] = 61;
        wide_inputs.before.rgba[pixel + 3] = 255;
    }
    packet.captured_pipeline =
        BuildPreviewCapturedPipeline(wide_inputs, backend, 5, &error);
    CHECK(packet.captured_pipeline);
    packet.pipeline_digest =
        ComputePreviewCapturedPipelineDigest(*packet.captured_pipeline);
    packet.pipeline_layout_digest =
        ComputePreviewCapturedPipelineDigest(*packet.captured_pipeline, true);
    packet.width = 1280;
    packet.height = 480;
    submit();
    await([&] {
        service.CopyStatus(&native_status);
        return native_status.has_displayed_source &&
               native_status.displayed_result.width == 1280 &&
               native_status.displayed_result.height == 480 &&
               native_status.displayed_result.pipeline_digest ==
                   packet.pipeline_digest;
    });
    std::vector<uint8_t> wide_checkpoint;
    await([&] {
        const bool ready = executor.CopyReadyImage(
            native_status.displayed_result, &checkpoint_width,
            &checkpoint_height, &wide_checkpoint, &error);
        CHECK(error.empty());
        return ready;
    });
    CHECK(checkpoint_width == 1280 && checkpoint_height == 480 &&
          wide_checkpoint.size() == 1280 * 480 * 4);
    const size_t wide_center = (240 * 1280 + 640) * 4;
    CHECK(wide_checkpoint[wide_center] == 0 &&
          wide_checkpoint[wide_center + 1] == 255 &&
          wide_checkpoint[wide_center + 2] == 0 &&
          wide_checkpoint[wide_center + 3] == 255);
    CHECK(wide_checkpoint[0] == 17 && wide_checkpoint[1] == 29 &&
          wide_checkpoint[2] == 61 && wide_checkpoint[3] == 255);
    std::puts("Native captured 1280 x 480 viewport, destination and owned "
              "readback PASS");
    packet.captured_pipeline = small_pipeline;
    packet.pipeline_digest =
        ComputePreviewCapturedPipelineDigest(*small_pipeline);
    packet.pipeline_layout_digest =
        ComputePreviewCapturedPipelineDigest(*small_pipeline, true);
    packet.width = packet.height = 160;
    packet.source = native_vk ?
                        "#version 450\nlayout(binding=2) uniform sampler2D "
                        "texSamp0;layout(location=0) in vec4 "
                        "geometryColor;layout(location=0) out vec4 color;" :
                        "#version 400\nuniform sampler2D texSamp0;in vec4 "
                        "geometryColor;out vec4 color;";
    packet.source +=
        "void main(){color=texture(texSamp0,vec2(.5))+geometryColor;}";
    packet.source_digest = ComputePreviewDigest(
        reinterpret_cast<const uint8_t *>(packet.source.data()),
        packet.source.size());
    packet.captured_material.reset();
    packet.material_digest = {};
    submit();
    await([&] {
        PreviewStatus current;
        service.CopyStatus(&current);
        return current.state == PreviewState::Unsupported;
    });
    PreviewStatus missing_native_texture;
    service.CopyStatus(&missing_native_texture);
    CHECK(missing_native_texture.message.find(
              "texture base image is unavailable: texSamp0") !=
          std::string::npos);
    TestNativeRasterCheckpoints(executor, packet, native_inputs, draw);
    TestNativeDependencyComparison(executor, packet, draw);
    TestScalarReadbacks(executor, packet, draw);
    TestSavedEveryUseComparison(executor, packet, draw);
    packet = geometry_only_packet;
    submit();
    await([&] {
        PreviewStatus current;
        service.CopyStatus(&current);
        return current.has_displayed_source &&
               current.displayed_result.compile.source_digest ==
                   packet.source_digest &&
               current.displayed_result.material_digest == PreviewDigest{};
    });
    CHECK(colors() == std::set<unsigned>{ 0xff0000 });
    packet.width = 640;
    packet.height = 480;
    packet.update_policy = PreviewUpdatePolicy::Continuous;
    service.EditClock(PreviewClockAction::Play, 0, Now());
    submit();
    std::set<uint64_t> displayed_frames;
    const uint64_t cadence_start = Now();
    while (Now() - cadence_start < UINT64_C(2000000000)) {
        draw();
        PreviewStatus current;
        service.CopyStatus(&current);
        if (current.has_displayed_source &&
            current.displayed_result.packet_kind == PreviewPacketKind::Replay &&
            current.displayed_result.width == 640 &&
            current.displayed_result.height == 480)
            displayed_frames.insert(current.displayed_result.clock_revision);
        SDL_Delay(5);
    }
    std::printf("Backend %d captured mesh 640x480 display cadence: %.1f fps\n",
                int(backend), displayed_frames.size() / 2.0);
    for (auto pressure : { PreviewPressure::Normal, PreviewPressure::High }) {
        service.SetGuestPaused(false);
        displayed_frames.clear();
        const auto start = Now();
        while (Now() - start < UINT64_C(2000000000)) {
            service.UpdateHealth({ Now(), pressure, true });
            draw();
            PreviewStatus current;
            service.CopyStatus(&current);
            if (current.has_displayed_source &&
                current.displayed_result.width == 640 &&
                current.displayed_result.height == 480)
                displayed_frames.insert(
                    current.displayed_result.clock_revision);
            SDL_Delay(5);
        }
        std::printf("Backend %d running captured mesh 640x480 %s pressure: "
                    "%.1f fps\n",
                    int(backend), pressure == PreviewPressure::Normal ?
                                      "normal" : "high",
                    displayed_frames.size() / 2.0);
        CHECK(displayed_frames.size() > 1);
        service.SetGuestPaused(true);
    }
    service.EditClock(PreviewClockAction::Pause, 0, Now());
    packet.width = packet.height = 160;
    packet.update_policy = PreviewUpdatePolicy::OnDirty;
    packet.packet_kind = PreviewPacketKind::Synthetic;
    packet.replay_class = PreviewReplayClass::Synthetic;
    packet.captured_mesh = {};
    packet.mesh_digest = {};
    submit();
    await([&] {
        PreviewStatus current;
        service.CopyStatus(&current);
        return current.has_displayed_source &&
               current.displayed_result.packet_kind == PreviewPacketKind::Synthetic;
    });
    if (backend == PreviewBackend::OpenGL) {
        packet = Packet(PreviewMode::Replacement, false, 1);
        packet.source =
            "#version 430\n"
            "layout(std430, binding=0) buffer UnownedInput { vec4 payload; };\n"
            "out vec4 color;\n"
            "void main(){color=payload;}\n";
        packet.partner_source = BuildPreviewSyntheticVertexSource(
            packet.source, packet.selection.backend);
        packet.source_digest = ComputePreviewDigest(
            reinterpret_cast<const uint8_t *>(packet.source.data()),
            packet.source.size());
        packet.partner_digest = ComputePreviewDigest(
            reinterpret_cast<const uint8_t *>(packet.partner_source.data()),
            packet.partner_source.size());
        submit();
        await([&] {
            PreviewStatus status;
            service.CopyStatus(&status);
            return status.state == PreviewState::Unsupported;
        });
        PreviewStatus unsupported;
        service.CopyStatus(&unsupported);
        CHECK(unsupported.message.find("shader-storage") != std::string::npos);
        CHECK(unsupported.leased_slots == 1 &&
              unsupported.free_slots == kPreviewSlotCount - 1);
        CHECK(colors() == std::set<unsigned>{ 0xff0000 });
        packet = Packet(PreviewMode::Normal, false, 0);
        submit();
        await([&] { return executor.HasDisplayed(); });
    }
    // Clearing a Reference must preserve the sole last-good frame when the
    // failed attempt cannot produce an independent Current.
    packet = Packet(PreviewMode::Replacement, true, 2);
    submit();
    await([&] {
        PreviewStatus status;
        service.CopyStatus(&status);
        return status.state == PreviewState::Failed;
    });
    PreviewStatus failed_status;
    service.CopyStatus(&failed_status);
    CHECK(failed_status.leased_slots == 1 &&
          failed_status.free_slots == kPreviewSlotCount - 1);
    CHECK(failed_status.attempted_compile.replacement_revision == 2);
    CHECK(executor.FreezeDisplayed());
    draw();
    CHECK(executor.HasFrozen() && !executor.HasDisplayed());
    CHECK(colors() == std::set<unsigned>{ 0xff0000 });
    executor.ClearFrozen();
    CHECK(executor.HasDisplayed() && !executor.HasFrozen());
    draw();
    CHECK(textures.size() == 1);
    CHECK(colors() == std::set<unsigned>{ 0xff0000 });
    PreviewStatus cleared_status;
    service.CopyStatus(&cleared_status);
    CHECK(cleared_status.state == PreviewState::Failed);
    CHECK(cleared_status.message == failed_status.message);
    CHECK(cleared_status.attempted_compile == failed_status.attempted_compile);
    CHECK(cleared_status.leased_slots == 1 &&
          cleared_status.free_slots == kPreviewSlotCount - 1);
    packet = Packet(PreviewMode::Normal, false, 0);
    submit();
    await([&] {
        PreviewStatus status;
        service.CopyStatus(&status);
        return status.state == PreviewState::Ready;
    });
    CHECK(executor.FreezeDisplayed());
    await([&] { return executor.HasDisplayed() && executor.HasFrozen(); });
    CHECK(textures.size() == 2);
    packet = Packet(PreviewMode::Replacement, true, 2);
    submit();
    await([&] {
        PreviewStatus status;
        service.CopyStatus(&status);
        return status.state == PreviewState::Failed;
    });
    CHECK(executor.HasDisplayed() && executor.HasFrozen());
    CHECK(textures.size() == 2);
    CHECK(colors() == std::set<unsigned>{ 0xff0000 });
    packet = Packet(PreviewMode::Replacement, false, 3);
    submit();
    await([&] { return colors() == std::set<unsigned>{ 0xff0000, 0x00ff00 }; });
    packet = Packet(PreviewMode::Uber, true, 0);
    submit();
    await([&] {
        PreviewStatus status;
        service.CopyStatus(&status);
        return status.state == PreviewState::Failed;
    });
    CHECK(colors() == std::set<unsigned>({ 0xff0000, 0x00ff00 }));
    packet = Packet(PreviewMode::Uber, false, 0);
    submit();
    await([&] {
        PreviewStatus status;
        service.CopyStatus(&status);
        return status.state == PreviewState::Ready && status.prepared;
    });
    draw();
    CHECK(executor.HasFrozen());
    // A malformed private fixture fails during rendering, preserving both
    // images.
    packet.fixture_bytes = { 0 };
    packet.fixture_digest = ComputePreviewDigest(packet.fixture_bytes.data(),
                                                 packet.fixture_bytes.size());
    submit();
    await([&] {
        PreviewStatus status;
        service.CopyStatus(&status);
        return status.state == PreviewState::Failed;
    });
    CHECK(colors() == std::set<unsigned>({ 0xff0000, 0x00ff00 }));
    packet = Packet(PreviewMode::Uber, false, 0);
    submit();
    await([&] {
        PreviewStatus status;
        service.CopyStatus(&status);
        return status.state == PreviewState::Ready;
    });
    // A renderer epoch ends both comparisons even before new preparation.
    ++packet.selection.renderer_epoch;
    submit();
    draw();
    CHECK(!executor.HasDisplayed() && !executor.HasFrozen());
    await([&] { return executor.HasDisplayed(); });
    CHECK(executor.FreezeDisplayed());
    await([&] { return executor.HasDisplayed(); });
    draw(); // Submit actual HUD sampling immediately before terminal shutdown.
    CHECK(SDL_GL_MakeCurrent(main_window, main_context));
    CHECK(SDL_GL_GetCurrentContext() == main_context);
    CHECK(SDL_GL_MakeCurrent(window, context));
    executor.Shutdown();
    CHECK(SDL_GL_GetCurrentContext() == context);
    CHECK(SDL_GL_MakeCurrent(main_window, main_context));
    CHECK(SDL_GL_GetCurrentContext() == main_context);
    CHECK(glGetError() == GL_NO_ERROR);
    CHECK(SDL_GL_MakeCurrent(window, context));
    CHECK(!executor.HasDisplayed() && !executor.HasFrozen());
    CHECK(!executor.NeedsRetirementPump());
    // Reuse the owner after a terminal reset; old GL object names cannot
    // escape.
    service.SetEnabled(true);
    ++packet.selection.renderer_epoch;
    packet.profile_draw = true;
    submit();
    await([&] { return executor.HasDisplayed(); });
    await([&] {
        PreviewDrawTiming timing;
        return service.CopyDrawTiming(BuildPreviewResultKey(packet), &timing) &&
               timing.status == PreviewDrawTimingStatus::Measured &&
               timing.nanoseconds;
    });
    // If the consumer switch fails, terminal cleanup can use the explicitly
    // restored shared main context while preserving queued consumer references.
    CHECK(SDL_GL_MakeCurrent(main_window, main_context));
    executor.Shutdown();
    CHECK(SDL_GL_GetCurrentContext() == main_context);
    CHECK(!executor.NeedsRetirementPump());
    CHECK(glGetError() == GL_NO_ERROR);
    CHECK(SDL_GL_MakeCurrent(window, context));
    service.SetEnabled(true);
    ++packet.selection.renderer_epoch;
    submit();
    await([&] { return executor.HasDisplayed(); });
    // No context on entry: an external switch may succeed, but restoring
    // the saved null pair must fail rather than treat SDL unbind as success.
    CHECK(SDL_GL_MakeCurrent(nullptr, nullptr));
    auto *missing_window = SDL_GL_GetCurrentWindow();
    auto missing_context = SDL_GL_GetCurrentContext();
    CHECK(!missing_window && !missing_context);
    CHECK(MakePreviewHudContextCurrent(window, context));
    executor.Shutdown();
    CHECK(!executor.NeedsRetirementPump());
    CHECK(!MakePreviewHudContextCurrent(missing_window, missing_context));
    CHECK(SDL_GL_GetCurrentContext() == context);
    CHECK(!MakePreviewHudContextCurrent(window, nullptr));
    CHECK(!MakePreviewHudContextCurrent(nullptr, context));
    CHECK(SDL_GL_GetCurrentContext() == context);
    CHECK(MakePreviewHudContextCurrent(main_window, main_context));
    CHECK(SDL_GL_GetCurrentContext() == main_context);
    CHECK(MakePreviewHudContextCurrent(window, context));
    service.SetEnabled(true);
    ++packet.selection.renderer_epoch;
    submit();
    await([&] { return executor.HasDisplayed(); });
    // Terminal context failure must stop the worker without issuing HUD GL
    // deletion. The shared objects remain for destruction of the share group.
    const auto abandoned_textures = textures;
    CHECK(!abandoned_textures.empty());
    CHECK(SDL_GL_MakeCurrent(nullptr, nullptr));
    executor.Shutdown(false);
    CHECK(SDL_GL_GetCurrentContext() == nullptr);
    CHECK(!executor.NeedsRetirementPump());
    CHECK(SDL_GL_MakeCurrent(window, context));
    for (GLuint texture : abandoned_textures) {
        CHECK(glIsTexture(texture));
    }
    CHECK(glGetError() == GL_NO_ERROR);
    service.SetEnabled(true);
    ++packet.selection.renderer_epoch;
    submit();
    await([&] { return executor.HasDisplayed(); });
    service.SetEnabled(false);
    draw();
    CHECK(!executor.HasDisplayed() && !executor.HasFrozen());
    executor.Shutdown();
    CHECK(glGetError() == GL_NO_ERROR);
    ImGui_ImplOpenGL3_Shutdown();
    ImGui::DestroyContext();
    SDL_GL_DestroyContext(main_context);
    SDL_DestroyWindow(main_window);
    SDL_GL_DestroyContext(context);
    SDL_DestroyWindow(window);
    SDL_Quit();
    printf("Backend %d: ", int(backend));
    puts("Production preview worker/HUD lifecycle: last-good pixels, "
         "cross-mode reference, auto paused preparation, epoch reset, disable, "
         "shutdown/restart passed");
}

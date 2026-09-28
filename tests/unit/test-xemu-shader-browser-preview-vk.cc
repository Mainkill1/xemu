// SPDX-License-Identifier: GPL-2.0-or-later
#include "shader-browser-preview-vk.hh"
#include "shader-browser-preview-adapter.hh"
#include "shader-browser-preview-alpha.hh"
#include <SDL3/SDL.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>

using namespace xemu::shader_browser;
#define CHECK(x)                                                             \
    do {                                                                     \
        if (!(x)) {                                                          \
            std::fprintf(stderr, "%s:%d: %s (%s)\n", __FILE__, __LINE__, #x, \
                         error.c_str());                                     \
            std::abort();                                                    \
        }                                                                    \
    } while (0)

int main(int argc, char **)
{
    std::string error;
    PreviewVkExecutor executor;
    PreviewWorkItem work{};
    auto packet = std::make_shared<PreviewPacket>();
    packet->selection.backend = PreviewBackend::Vulkan;
    packet->source_variant = PreviewSourceVariant::Edited;
    packet->width = packet->height = 32;
    packet->source = "#version 450\nlayout(location=0) in vec4 vtxD0;\n"
                     "layout(location=0) out vec4 color;\n"
                     "void main(){color=vtxD0;}\n";
    packet->partner_source = BuildPreviewSyntheticVertexSource(
        packet->source, PreviewBackend::Vulkan);
    PreviewSyntheticFixture fixture{};
    for (auto &color : fixture.corner_colors)
        color = { 255, 0, 0, 73 };
    packet->fixture_bytes = EncodePreviewSyntheticFixture(fixture);
    work.packet = packet;
    PreviewPacket alpha_packet{};
    alpha_packet.selection.shader.stage = Stage::Pixel;
    alpha_packet.recipe_format_version = 1;
    alpha_packet.recipe.resize(312);
    alpha_packet.recipe[0] = 'N';
    alpha_packet.recipe[1] = 'V';
    alpha_packet.recipe[2] = '2';
    alpha_packet.recipe[3] = 'A';
    alpha_packet.recipe[4] = 2;
    auto set_recipe_alpha = [&](bool enabled, uint32_t function) {
        // Encoder v1 puts alpha_test and alpha_func after shadow_depth_func.
        alpha_packet.recipe[295] = enabled ? 1 : 0;
        alpha_packet.recipe[296] = static_cast<uint8_t>(function);
        alpha_packet.selection.shader.hash = ComputeShaderHash(
            1, Stage::Pixel, 1, alpha_packet.recipe.data(),
            alpha_packet.recipe.size());
    };
    alpha_packet.render_state.alpha_test = true;
    set_recipe_alpha(true, 0); // NEVER: unconditional discard, no fragAlpha.
    CanonicalRecipe canonical_alpha{};
    canonical_alpha.key = alpha_packet.selection.shader;
    canonical_alpha.recipe_format_version = 1;
    canonical_alpha.bytes = alpha_packet.recipe;
    bool selected_alpha = false;
    CHECK(PreviewCanonicalAlphaTest(canonical_alpha, &selected_alpha));
    CHECK(selected_alpha);
    CHECK(AdmitPreviewBakedAlphaTest(alpha_packet, &error));
    set_recipe_alpha(true, 7); // ALWAYS: no generated discard branch.
    CHECK(AdmitPreviewBakedAlphaTest(alpha_packet, &error));
    set_recipe_alpha(false, 7);
    canonical_alpha.key = alpha_packet.selection.shader;
    canonical_alpha.bytes = alpha_packet.recipe;
    CHECK(PreviewCanonicalAlphaTest(canonical_alpha, &selected_alpha));
    CHECK(!selected_alpha);
    CHECK(!AdmitPreviewBakedAlphaTest(alpha_packet, &error));
    alpha_packet.source_variant = PreviewSourceVariant::Edited;
    set_recipe_alpha(true, 7);
    CHECK(AdmitPreviewBakedAlphaTest(alpha_packet, &error));
    alpha_packet.source_variant = PreviewSourceVariant::Original;
    alpha_packet.selection.mode = PreviewMode::Replacement;
    CHECK(AdmitPreviewBakedAlphaTest(alpha_packet, &error));
    alpha_packet.selection.mode = PreviewMode::Normal;
    alpha_packet.recipe.clear();
    CHECK(!AdmitPreviewBakedAlphaTest(alpha_packet, &error));
    CHECK(error.find("canonical pixel recipe") != std::string::npos);
    alpha_packet.source_variant = PreviewSourceVariant::Edited;
    CHECK(AdmitPreviewBakedAlphaTest(alpha_packet, &error));
    bool unsupported = false;
    if (argc > 1) {
        CHECK(!executor.Prepare(work, &error, &unsupported));
        CHECK(!unsupported);
        CHECK(!error.empty());
        CHECK(!executor.Prepare(work, &error, &unsupported));
        std::puts("private Vulkan unavailable-device retry: PASS");
        return 0;
    }
    CHECK(executor.Prepare(work, &error, &unsupported));
    ++work.compile_key.interface_abi;
    bool early_cancelled = false;
    unsigned early_checks = 0;
    CHECK(!executor.Prepare(work, &error, &unsupported, &early_cancelled,
                            [&] { return ++early_checks < 2; }));
    CHECK(early_cancelled && !unsupported);
    bool cancelled = false;
    unsigned pause_checks = 0;
    const auto before_cancel = executor.PipelineCreationCountForTest();
    CHECK(!executor.Prepare(work, &error, &unsupported, &cancelled,
                            [&] { return ++pause_checks < 6; }));
    CHECK(cancelled && !unsupported);
    CHECK(executor.PipelineCreationCountForTest() - before_cancel < 24);
    CHECK(executor.Prepare(work, &error, &unsupported));
    const auto prepared_pipeline_count =
        executor.PipelineCreationCountForTest();
    std::vector<uint8_t> pixels;
    std::atomic<bool> stop{ false };
    PreviewDrawTiming draw_timing;
    CHECK(executor.Render(work, stop, &pixels, &error, &draw_timing));
    CHECK(draw_timing.status == PreviewDrawTimingStatus::Disarmed &&
          !draw_timing.nanoseconds);
    CHECK(pixels.size() == 32 * 32 * 4);
    CHECK(pixels[4 * (16 * 32 + 16)] == 255);
    CHECK(pixels[4 * (16 * 32 + 16) + 1] == 0);
    CHECK(pixels[4 * (16 * 32 + 16) + 3] == 73);
    const auto disarmed_result = work.result_key;
    packet->profile_draw = true;
    work.result_key = BuildPreviewResultKey(*packet);
    const auto timing_render_started = SDL_GetTicksNS();
    CHECK(executor.Render(work, stop, &pixels, &error, &draw_timing));
    const auto timing_render_finished = SDL_GetTicksNS();
    const auto timing_after_render = draw_timing;
    uint32_t timing_poll_calls = 0, timing_poll_results = 0;
    bool timing_poll_key_matches = true;
    const auto timestamp_deadline =
        timing_render_finished + kPreviewMaxDrawTimingNs;
    while (draw_timing.status == PreviewDrawTimingStatus::Pending &&
           SDL_GetTicksNS() < timestamp_deadline) {
        PreviewWorkItem timed_work;
        ++timing_poll_calls;
        if (executor.PollDrawTiming(&timed_work, &draw_timing)) {
            ++timing_poll_results;
            timing_poll_key_matches = timed_work.result_key == work.result_key;
            if (!timing_poll_key_matches)
                break;
        } else
            SDL_Delay(1);
    }
    const auto timing_poll_finished = SDL_GetTicksNS();
    if (!timing_poll_key_matches ||
        !ValidatePreviewDrawTiming(draw_timing, work.result_key) ||
        (draw_timing.status != PreviewDrawTimingStatus::Measured &&
         draw_timing.status != PreviewDrawTimingStatus::Unsupported)) {
        auto dump = [&](const char *phase, const PreviewDrawTiming &timing) {
            std::fprintf(
                stderr,
                "Vulkan timing diagnostic %s: status=%u (%s) "
                "provenance=%u (%s) backend=%u ns=%llu valid_bits=%u "
                "period_ns=%.17g actual_draw_commands=%u exact_key=%u "
                "valid=%u message=\"%s\"\n",
                phase, unsigned(timing.status),
                PreviewDrawTimingStatusLabel(timing.status),
                unsigned(timing.provenance),
                PreviewDrawTimingProvenanceLabel(timing.provenance),
                unsigned(timing.backend),
                (unsigned long long)timing.nanoseconds,
                timing.timestamp_valid_bits, timing.timestamp_period_ns,
                timing.actual_draw_commands,
                unsigned(timing.result == work.result_key),
                unsigned(ValidatePreviewDrawTiming(timing, work.result_key)),
                timing.message.c_str());
        };
        std::fprintf(
            stderr,
            "Vulkan timing diagnostic request: width=%u height=%u "
            "slot=%u generation=%llu packet_profile=%u result_profile=%u "
            "result_compile_matches_work=%u render_ns=%llu "
            "poll_ns=%llu poll_calls=%u poll_results=%u "
            "poll_key_matches=%u deadline_reached=%u render_error=\"%s\"\n",
            packet->width, packet->height, work.slot,
            (unsigned long long)work.slot_generation,
            unsigned(packet->profile_draw),
            unsigned(work.result_key.profile_draw),
            unsigned(work.result_key.compile == work.compile_key),
            (unsigned long long)(timing_render_finished -
                                 timing_render_started),
            (unsigned long long)(timing_poll_finished - timing_render_finished),
            timing_poll_calls, timing_poll_results,
            unsigned(timing_poll_key_matches),
            unsigned(timing_poll_finished >= timestamp_deadline),
            error.c_str());
        dump("after-render", timing_after_render);
        dump("after-poll", draw_timing);
        std::fflush(stderr);
    }
    CHECK(timing_poll_key_matches);
    CHECK(ValidatePreviewDrawTiming(draw_timing, work.result_key));
    CHECK(draw_timing.status == PreviewDrawTimingStatus::Measured ||
          draw_timing.status == PreviewDrawTimingStatus::Unsupported);
    CHECK(draw_timing.actual_draw_commands == 1 &&
          draw_timing.provenance ==
              PreviewDrawTimingProvenance::SelectedPreviewInstrumented);
    std::printf("Private Vulkan reused pipeline draw interval: %llu ns (%s)\n",
                (unsigned long long)draw_timing.nanoseconds,
                PreviewDrawTimingStatusLabel(draw_timing.status));
    work.result_key.channel = PreviewChannel::D0;
    CHECK(executor.Render(work, stop, &pixels, &error, &draw_timing));
    CHECK(ValidatePreviewDrawTiming(draw_timing, work.result_key));
    CHECK(draw_timing.status == PreviewDrawTimingStatus::Unsupported &&
          !draw_timing.nanoseconds && !draw_timing.actual_draw_commands &&
          !draw_timing.message.empty());
    CHECK(executor.PipelineCreationCountForTest() == prepared_pipeline_count);
    packet->profile_draw = false;
    work.result_key = disarmed_result;
    packet->render_state.alpha_test = true;
    CHECK(executor.Render(work, stop, &pixels, &error));
    CHECK(pixels[4 * (16 * 32 + 16)] == 255);
    packet->render_state.alpha_test = false;
    CHECK(executor.Render(work, stop, &pixels, &error));
    auto find_rgb = [&](int red, int green, int blue) {
        for (size_t i = 0; i < pixels.size(); i += 4)
            if (std::abs(int(pixels[i]) - red) <= 4 &&
                std::abs(int(pixels[i + 1]) - green) <= 4 &&
                std::abs(int(pixels[i + 2]) - blue) <= 4)
                return i;
        return pixels.size();
    };
    CHECK(find_rgb(41, 209, 214) < pixels.size());
    CHECK(find_rgb(242, 184, 48) < pixels.size()); // intersection
    CHECK(find_rgb(69, 74, 79) < pixels.size());   // ground
    CHECK(find_rgb(41, 69, 94) < pixels.size());   // backdrop
    const auto with_blocker = pixels;
    work.result_key.scene.references[3].visible = false;
    CHECK(executor.Render(work, stop, &pixels, &error));
    size_t blocker_pixel = pixels.size();
    for (size_t i = 0; i < pixels.size(); i += 4)
        if (std::abs(int(with_blocker[i]) - 41) <= 4 &&
            std::abs(int(with_blocker[i + 1]) - 209) <= 4 &&
            pixels[i] == 255 && pixels[i + 1] == 0) {
            blocker_pixel = i;
            break;
        }
    CHECK(blocker_pixel < pixels.size());
    work.result_key.scene.references[3].visible = true;
    CHECK(executor.Render(work, stop, &pixels, &error));
    CHECK(std::abs(int(pixels[blocker_pixel]) - 41) <= 4);
    packet->render_state.depth_test = false;
    CHECK(executor.Render(work, stop, &pixels, &error));
    CHECK(pixels[blocker_pixel] == 255);
    packet->render_state.depth_test = true;
    packet->render_state.blend = PreviewBlendMode::Alpha;
    CHECK(executor.Render(work, stop, &pixels, &error));
    CHECK(pixels[4 * (16 * 32 + 16)] < 255);
    packet->render_state.blend = PreviewBlendMode::Opaque;
    packet->render_state.cull = PreviewCullMode::Front;
    CHECK(executor.Render(work, stop, &pixels, &error));
    CHECK(pixels[4 * (16 * 32 + 16)] != 255);
    packet->render_state.cull = PreviewCullMode::None;
    CHECK(executor.Render(work, stop, &pixels, &error));
    CHECK(pixels[4 * (16 * 32 + 16)] == 255);
    CHECK(executor.PipelineCreationCountForTest() ==
          prepared_pipeline_count);
    std::puts("Vulkan shared-depth references and per-result clear PASS");
    const auto final_pixels = pixels;
    for (auto channel : { PreviewChannel::Red, PreviewChannel::Green,
                          PreviewChannel::Blue, PreviewChannel::Alpha }) {
        work.result_key.channel = channel;
        CHECK(executor.Render(work, stop, &pixels, &error));
        const auto value =
            final_pixels[4 * (16 * 32 + 16) + PreviewChannelComponent(channel)];
        for (int c = 0; c < 3; ++c)
            CHECK(pixels[4 * (16 * 32 + 16) + c] == value);
        CHECK(pixels[4 * (16 * 32 + 16) + 3] == 255);
    }
    fixture.fog = 0.25f;
    fixture.alpha_reference = 63;
    packet->fixture_bytes = EncodePreviewSyntheticFixture(fixture);
    for (auto channel :
         { PreviewChannel::UV, PreviewChannel::D0, PreviewChannel::T0,
           PreviewChannel::Fog, PreviewChannel::DepthRamp,
           PreviewChannel::FixtureAlphaMask }) {
        work.result_key.channel = channel;
        CHECK(executor.Render(work, stop, &pixels, &error));
        const auto center = 4 * (16 * 32 + 16);
        const uint8_t expected = channel == PreviewChannel::UV ||
                                         channel == PreviewChannel::DepthRamp ?
                                     131 :
                                 channel == PreviewChannel::Fog ? 64 :
                                 channel == PreviewChannel::T0  ? 0 :
                                                                  255;
        CHECK(pixels[center] == expected);
        CHECK(pixels[center + 3] == 255);
    }
    work.result_key.channel = PreviewChannel::ShaderDiscard;
    CHECK(!executor.Render(work, stop, &pixels, &error));
    CHECK(error.find("Unsupported") != std::string::npos);
    work.result_key.channel = PreviewChannel::FinalRGBA;
    std::puts("Vulkan final scalar channels and fixture charts / unsupported "
              "discard PASS");
    // Mesh and camera edits reuse this prepared pipeline.
    std::vector<std::vector<uint8_t>> mesh_pixels;
    for (auto kind :
         { PreviewMesh::Quad, PreviewMesh::Sphere, PreviewMesh::Cube }) {
        work.result_key.scene = {};
        work.result_key.scene.mesh = kind;
        work.result_key.scene.yaw = 30;
        work.result_key.scene.pitch = 20;
        work.result_key.scene.distance = 4;
        CHECK(executor.Render(work, stop, &pixels, &error));
        mesh_pixels.push_back(pixels);
        packet->update_policy = PreviewUpdatePolicy::Continuous;
        packet->binding_count = 1;
        packet->bindings[0] = { PreviewInputTarget::D0RGB, true, 1.0f,
                                -0.5f, 10.0f };
        work.result_key.time_seconds = 4;
        CHECK(executor.Render(work, stop, &pixels, &error));
        CHECK(pixels != mesh_pixels.back());
        packet->update_policy = PreviewUpdatePolicy::OnDirty;
        packet->binding_count = 0;
        work.result_key.time_seconds = 0;
        pixels = mesh_pixels.back();
        size_t coverage = 0;
        for (size_t i = 0; i < pixels.size(); i += 4)
            coverage += pixels[i] == 255;
        CHECK(coverage > 20 && coverage < 900);
        std::fprintf(stderr, "Vulkan mesh %d: %zu red pixels\n", int(kind),
                     coverage);
        work.result_key.scene.yaw = -40;
        work.result_key.scene.pan[0] = 0.5f;
        CHECK(executor.Render(work, stop, &pixels, &error));
        CHECK(pixels != mesh_pixels.back());
    }
    CHECK(mesh_pixels[0] != mesh_pixels[1]);
    CHECK(mesh_pixels[1] != mesh_pixels[2]);
    CHECK(mesh_pixels[0] != mesh_pixels[2]);
    work.result_key.scene = {};
    auto source = [&](const std::string &text) {
        packet->source = text;
        packet->partner_source =
            BuildPreviewSyntheticVertexSource(text, PreviewBackend::Vulkan);
        ++work.compile_key.interface_abi;
    };
    auto render = [&] {
        CHECK(executor.Prepare(work, &error, &unsupported));
        CHECK(!unsupported);
        CHECK(executor.Render(work, stop, &pixels, &error));
        CHECK(pixels.size() == size_t(packet->width) * packet->height * 4);
    };
    source("#version 450\nlayout(location=0) out vec4 color;"
           "void main(){discard;}\n");
    render();
    CHECK(pixels[4 * (16 * 32 + 16)] != 255);
    CHECK(find_rgb(41, 209, 214) < pixels.size());
    CHECK(find_rgb(242, 184, 48) < pixels.size());
    std::puts("Vulkan target discard preserves reference scene PASS");
    source("#version 450\nlayout(binding=3) uniform sampler2D texSamp0;\n"
           "layout(location=5) in vec4 vtxT0;\n"
           "layout(location=0) out vec4 color;\n"
           "void main(){color=texture(texSamp0,vtxT0.xy);}\n");
    for (auto &texel : fixture.texture_texels)
        texel = { 0, 255, 0, 255 };
    packet->fixture_bytes = EncodePreviewSyntheticFixture(fixture);
    render();
    CHECK(pixels[4 * (16 * 32 + 16)] == 0);
    CHECK(pixels[4 * (16 * 32 + 16) + 1] == 255);
    // A failed sampler edit must leave the old descriptor and cached settings
    // intact. Inspect before retrying so a broken implementation cannot submit
    // a descriptor referring to a destroyed sampler to the native device.
    fixture.texture_texels = { { { 255, 0, 0, 255 },
                                 { 0, 255, 0, 255 },
                                 { 255, 0, 0, 255 },
                                 { 0, 255, 0, 255 } } };
    fixture.uv_scale = { 0, 0 };
    fixture.uv_offset = { 1.25f, 0.25f };
    packet->fixture_bytes = EncodePreviewSyntheticFixture(fixture);
    render();
    CHECK(pixels[4 * (16 * 32 + 16) + 1] == 255); // clamped right texel
    const bool old_linear = fixture.linear_filter;
    const bool old_repeat = fixture.repeat_wrap;
    fixture.linear_filter = !old_linear;
    fixture.repeat_wrap = !old_repeat;
    packet->fixture_bytes = EncodePreviewSyntheticFixture(fixture);
    for (unsigned attempt = 0; attempt < 2; ++attempt) {
        executor.FailNextSamplerCreationForTest();
        CHECK(!executor.Render(work, stop, &pixels, &error));
        CHECK(executor.HasSamplerSettingsForTest(old_linear, old_repeat));
    }
    CHECK(executor.Render(work, stop, &pixels, &error));
    CHECK(executor.HasSamplerSettingsForTest(!old_linear, !old_repeat));
    CHECK(pixels[4 * (16 * 32 + 16)] == 255); // repeated left texel
    CHECK(pixels[4 * (16 * 32 + 16) + 1] == 0);
    std::fprintf(stderr, "private Vulkan sampler failure/retry: PASS\n");
    fixture.uv_scale = { 1, 1 };
    fixture.uv_offset = { 0, 0 };
    // Same compile identity, updated fixture, sampler and extent.
    for (auto &texel : fixture.texture_texels)
        texel = { 0, 0, 255, 255 };
    fixture.linear_filter = fixture.repeat_wrap = 1;
    packet->fixture_bytes = EncodePreviewSyntheticFixture(fixture);
    packet->width = packet->height = 160;
    render();
    CHECK(pixels[4 * (80 * 160 + 80) + 2] == 255);
    packet->width = packet->height = 320;
    render();
    CHECK(pixels[4 * (160 * 320 + 160) + 2] == 255);
    packet->width = 640;
    packet->height = 480;
    render();
    CHECK(pixels.size() == 640U * 480U * 4U);
    CHECK(pixels[4 * (240 * 640 + 320) + 2] == 255);
    packet->width = kPreviewMaxWidth + 1;
    PreviewVkExecutor invalid_fixture_extent;
    CHECK(!invalid_fixture_extent.Prepare(work, &error, &unsupported));
    CHECK(unsupported && error.find("extent") != std::string::npos);
    CHECK(invalid_fixture_extent.PipelineCreationCountForTest() == 0);
    CHECK(!executor.Prepare(work, &error, &unsupported));
    CHECK(unsupported);
    CHECK(!executor.Render(work, stop, &pixels, &error));
    packet->width = packet->height = 320;
    source("#version 450\nlayout(binding=1,std140) uniform PshUniforms {\n"
           "int alphaRef; mat2 bumpMat[4]; float bumpOffset[4]; float "
           "bumpScale[4];\n"
           "vec4 clipRange; ivec4 clipRegion[8]; uint colorKey[4]; uint "
           "colorKeyMask[4];\n"
           "vec4 consts[18]; float depthFactor; float depthOffset; vec4 "
           "fogColor;\n"
           "ivec2 surfaceScale; float texScale[4]; };\n"
           "layout(location=0) out vec4 color;\n"
           "void main(){ if (gl_FragCoord.x >= clipRegion[0].z) discard;\n"
           "color=vec4(consts[0].r,fogColor.g,consts[0].b,float(alphaRef)/"
           "255.0); }\n");
    fixture.constant_color = { 0, 1, 0, 1 };
    fixture.fog_color = { 0, 1, 0, 1 };
    fixture.alpha_reference = 128;
    packet->render_state.alpha_reference = 128;
    packet->fixture_bytes = EncodePreviewSyntheticFixture(fixture);
    render();
    CHECK(pixels[4 * (160 * 320 + 160) + 1] == 255);
    CHECK(pixels[4 * (160 * 320 + 160) + 3] == 128);
    fixture.constant_color = { 1, 0, 0, 1 };
    fixture.alpha_reference = 64;
    packet->render_state.alpha_reference = 64;
    fixture.fog_color[1] = 0.4f;
    packet->fixture_bytes = EncodePreviewSyntheticFixture(fixture);
    CHECK(executor.Render(work, stop, &pixels, &error));
    CHECK(pixels[4 * (160 * 320 + 160)] == 255 &&
          pixels[4 * (160 * 320 + 160) + 3] == 64);
    CHECK(pixels[4 * (160 * 320 + 160) + 1] == 102);
    std::puts(
        "Vulkan constant, fog color and alpha edits without prepare PASS");
    OwnedDrawInputs captured_inputs;
    captured_inputs.complete = true;
    auto capture_uniform = [&](const char *name, uint32_t type,
                               uint32_t components, uint32_t count,
                               const void *data, size_t bytes) {
        OwnedDrawUniform value{ 2, name, type, components, count, {} };
        const auto *begin = static_cast<const uint8_t *>(data);
        value.data.assign(begin, begin + bytes);
        captured_inputs.uniforms.push_back(std::move(value));
    };
    std::array<float, 18 * 4> captured_constants{};
    for (size_t i = 0; i < 18; ++i) {
        captured_constants[4 * i] = 0.25f;
        captured_constants[4 * i + 3] = 1;
    }
    std::array<float, 4> captured_fog{ 0, 0.6f, 0, 1 };
    int32_t captured_alpha = 192;
    std::array<int32_t, 8 * 4>
        game_clip{}; // Must not replace preview viewport.
    capture_uniform("consts[0]", XEMU_SHADER_DRAW_UNIFORM_FLOAT, 4, 18,
                    captured_constants.data(), sizeof(captured_constants));
    capture_uniform("fogColor", XEMU_SHADER_DRAW_UNIFORM_FLOAT, 4, 1,
                    captured_fog.data(), sizeof(captured_fog));
    capture_uniform("alphaRef", XEMU_SHADER_DRAW_UNIFORM_INT, 1, 1,
                    &captured_alpha, sizeof(captured_alpha));
    capture_uniform("clipRegion[0]", XEMU_SHADER_DRAW_UNIFORM_INT, 4, 8,
                    game_clip.data(), sizeof(game_clip));
    packet->packet_kind = PreviewPacketKind::Replay;
    packet->replay_class = PreviewReplayClass::Approximate;
    packet->captured_mesh.positions = { { -1, -1, 0, 1 },
                                        { 1, -1, 0, 1 },
                                        { 0, 1, 0, 1 } };
    packet->captured_mesh.indices = { 0, 1, 2 };
    packet->captured_material = BuildPreviewCapturedMaterial(captured_inputs);
    packet->material_digest =
        ComputePreviewCapturedMaterialDigest(*packet->captured_material);
    const auto material_pipeline_count =
        executor.PipelineCreationCountForTest();
    CHECK(executor.Render(work, stop, &pixels, &error));
    CHECK(pixels[4 * (160 * 320 + 160)] == 64);
    CHECK(pixels[4 * (160 * 320 + 160) + 1] == 153);
    CHECK(pixels[4 * (160 * 320 + 160) + 3] == 192);
    CHECK(error.find("Partial captured material") != std::string::npos);
    CHECK(executor.PipelineCreationCountForTest() == material_pipeline_count);
    source("#version 450\nlayout(binding=3) uniform sampler2D texSamp0;\n"
           "layout(location=0) out vec4 color;\n"
           "void main(){color=texture(texSamp0,vec2(0.5));}\n");
    auto &actual_texture = captured_inputs.textures[0];
    actual_texture.described = true;
    actual_texture.metadata.bound = true;
    actual_texture.metadata.width = 4;
    actual_texture.metadata.height = 2;
    actual_texture.metadata.depth = 1;
    actual_texture.metadata.face_count = actual_texture.metadata.mip_levels = 1;
    actual_texture.metadata.min_filter = actual_texture.metadata.mag_filter =
        0x2600;
    actual_texture.metadata.wrap_s = actual_texture.metadata.wrap_t =
        actual_texture.metadata.wrap_r = 0x812f;
    OwnedDrawImage captured_image{ 4, 2, std::vector<uint8_t>(4 * 2 * 4) };
    for (size_t i = 0; i < captured_image.rgba.size(); i += 4) {
        captured_image.rgba[i + 2] = captured_image.rgba[i + 3] = 255;
    }
    actual_texture.images.push_back({ 0, 0, std::move(captured_image) });
    packet->captured_material = BuildPreviewCapturedMaterial(captured_inputs);
    packet->material_digest =
        ComputePreviewCapturedMaterialDigest(*packet->captured_material);
    render();
    CHECK(pixels[4 * (160 * 320 + 160) + 2] == 255);
    actual_texture.images.clear();
    packet->captured_material = BuildPreviewCapturedMaterial(captured_inputs);
    packet->material_digest =
        ComputePreviewCapturedMaterialDigest(*packet->captured_material);
    CHECK(executor.Render(work, stop, &pixels, &error));
    CHECK(pixels[4 * (160 * 320 + 160) + 2] == 0);
    CHECK(error.find("missing texture") != std::string::npos);
    source(
        "#version 450\nlayout(binding=1,std140) uniform U {mat2 bumpMat[4];};\n"
        "layout(location=0) out vec4 color;\n"
        "void main(){color=vec4(bumpMat[0]*vec2(1),0,1);}\n");
    std::array<float, 16> captured_matrices{};
    for (size_t i = 0; i < 4; ++i) {
        captured_matrices[4 * i] = 0.2f;
        captured_matrices[4 * i + 1] = 0.3f;
        captured_matrices[4 * i + 2] = 0.4f;
        captured_matrices[4 * i + 3] = 0.5f;
    }
    capture_uniform("bumpMat[0]", XEMU_SHADER_DRAW_UNIFORM_MAT2, 4, 4,
                    captured_matrices.data(), sizeof(captured_matrices));
    packet->captured_material = BuildPreviewCapturedMaterial(captured_inputs);
    packet->material_digest =
        ComputePreviewCapturedMaterialDigest(*packet->captured_material);
    render();
    CHECK(pixels[4 * (160 * 320 + 160)] == 153);
    CHECK(pixels[4 * (160 * 320 + 160) + 1] == 204);
    source("#version 450\nlayout(binding=1,std140,row_major) uniform U {mat2 "
           "bumpMat[4];};\n"
           "layout(location=0) out vec4 color;\n"
           "void main(){color=vec4(bumpMat[0]*vec2(1),0,1);}\n");
    CHECK(!executor.Prepare(work, &error, &unsupported));
    CHECK(unsupported);
    packet->captured_material.reset();
    packet->material_digest = {};
    OwnedDrawInputs native_pipeline_inputs;
    native_pipeline_inputs.complete = true;
    native_pipeline_inputs.sources[1] =
        "#version 450\nlayout(location=0) in vec4 position;\n"
        "layout(push_constant) uniform P {vec4 inlineValue[1];};\n"
        "layout(binding=0,std140) uniform V {vec4 cameraOffset;};\n"
        "layout(location=0) out vec4 vertexColor;\n"
        "void "
        "main(){gl_Position=position+cameraOffset;vertexColor=inlineValue[0];}";
    native_pipeline_inputs.sources[3] =
        "#version 450\nlayout(triangles) "
        "in;layout(triangle_strip,max_vertices=3) out;\n"
        "layout(location=0) in vec4 vertexColor[];layout(location=0) out vec4 "
        "geometryColor;\n"
        "void main(){for(int "
        "i=0;i<3;i++){gl_Position=gl_in[i].gl_Position;geometryColor="
        "vertexColor[i];EmitVertex();}EndPrimitive();}";
    native_pipeline_inputs.registers = { { "capture.vertices.first", 0 },
                                         { "capture.vertices.count", 3 },
                                         { "capture.vertices.uniform_mask", 2 },
                                         { "capture.vertex.0.enabled", 1 } };
    const std::array<float, 12> raw_positions{ -1, -1, 0, 1, 1, -1,
                                               0,  1,  0, 1, 0, 1 };
    auto native_blob = [&](const char *name, uint32_t slot, const void *data,
                           size_t bytes, uint32_t count) {
        OwnedDrawBlob blob;
        blob.name = name;
        blob.slot = slot;
        blob.format = 109;
        blob.components = 4;
        blob.stride = 16;
        blob.count = count;
        const auto *begin = static_cast<const uint8_t *>(data);
        blob.bytes.assign(begin, begin + bytes);
        native_pipeline_inputs.blobs.push_back(std::move(blob));
    };
    native_blob("vertex.attribute0", 0, raw_positions.data(),
                sizeof(raw_positions), 3);
    const std::array<float, 4> current_color{ 0, 0.5f, 0, 1 }, camera_offset{},
        pixel_fog{ 0.5f, 0, 0, 0 };
    native_blob("vertex.current1", 1, current_color.data(),
                sizeof(current_color), 1);
    auto native_uniform = [&](uint32_t stage, const char *name,
                              const std::array<float, 4> &value) {
        const auto *begin = reinterpret_cast<const uint8_t *>(value.data());
        native_pipeline_inputs.uniforms.push_back(
            { stage, name, XEMU_SHADER_DRAW_UNIFORM_FLOAT, 4, 1,
              std::vector<uint8_t>(begin, begin + sizeof(value)) });
    };
    native_uniform(1, "cameraOffset", camera_offset);
    native_uniform(2, "fogColor", pixel_fog);
    packet->captured_pipeline = BuildPreviewCapturedPipeline(
        native_pipeline_inputs, PreviewBackend::Vulkan, 5, &error);
    CHECK(packet->captured_pipeline);
    packet->pipeline_digest =
        ComputePreviewCapturedPipelineDigest(*packet->captured_pipeline);
    packet->pipeline_layout_digest =
        ComputePreviewCapturedPipelineDigest(*packet->captured_pipeline, true);
    source(
        "#version 450\nlayout(location=0) in vec4 "
        "geometryColor;\nlayout(binding=1,std140) uniform F {vec4 fogColor;};\n"
        "layout(location=0) out vec4 color;void "
        "main(){color=geometryColor+fogColor;}");
    packet->partner_source = native_pipeline_inputs.sources[1];
    work.compile_key.pipeline_layout_digest = packet->pipeline_layout_digest;
    auto check_native_camera_color = [&](const char *phase, size_t sample,
                                         bool covered,
                                         bool always_log = false) {
        // This source emits (.5,.5,0,1). Only the fractional UNORM8 channels
        // allow one conversion unit; zero/one endpoints remain exact.
        const std::array<int, 4> expected =
            covered ? std::array<int, 4>{ 128, 128, 0, 255 } :
                      std::array<int, 4>{ 0, 0, 0, 255 };
        bool exact = true, acceptable = true;
        for (size_t channel = 0; channel < 4; ++channel) {
            const int difference =
                std::abs(int(pixels[sample + channel]) - expected[channel]);
            exact &= difference == 0;
            acceptable &= difference <= (covered && channel < 2 ? 1 : 0);
        }
        if (always_log || !exact) {
            std::fprintf(
                stderr,
                "Vulkan original-camera color %s: pixel=%zu "
                "RGBA=%u,%u,%u,%u expected=%d,%d,%d,%d "
                "tolerance=%u,%u,0,0 acceptable=%u guest_mode=%u "
                "host_topology=%u error=\"%s\"\n",
                phase, sample / 4, unsigned(pixels[sample]),
                unsigned(pixels[sample + 1]), unsigned(pixels[sample + 2]),
                unsigned(pixels[sample + 3]), expected[0], expected[1],
                expected[2], expected[3], unsigned(covered), unsigned(covered),
                unsigned(acceptable),
                packet->captured_pipeline->guest_primitive_mode,
                packet->captured_pipeline->host_topology, error.c_str());
            std::fflush(stderr);
        }
        CHECK(acceptable);
    };
    render();
    check_native_camera_color("initial", 4 * (160 * 320 + 160), true, true);
    CHECK(error.find("Original VS/GS camera") != std::string::npos);
    const auto complete_native_pipeline = packet->captured_pipeline;
    // Extent changes reuse the prepared program, including its framebuffer
    // and transfer buffers. Captured replay has a larger bound than fixtures.
    const auto before_wide = executor.PipelineCreationCountForTest();
    packet->width = 1280;
    packet->height = 480;
    render();
    CHECK(executor.PipelineCreationCountForTest() == before_wide);
    check_native_camera_color("1280x480 reused program", 4 * (240 * 1280 + 640),
                              true);
    packet->width = kPreviewMaxCapturedWidth;
    packet->height = kPreviewMaxCapturedHeight;
    render();
    CHECK(executor.PipelineCreationCountForTest() == before_wide);
    check_native_camera_color(
        "captured maximum extent",
        4 * (size_t(packet->height / 2) * packet->width + packet->width / 2),
        true);
    packet->width = 1280;
    packet->height = 480;
    auto wide_pipeline =
        std::make_shared<PreviewCapturedPipeline>(*complete_native_pipeline);
    wide_pipeline->raster.available |= PreviewRasterViewport;
    wide_pipeline->raster.width = packet->width;
    wide_pipeline->raster.height = packet->height;
    auto &wide_before = wide_pipeline->color_before;
    wide_before.width = packet->width;
    wide_before.height = packet->height;
    wide_before.rgba.resize(size_t(packet->width) * packet->height * 4);
    for (uint32_t y = 0; y < packet->height; ++y)
        for (uint32_t x = 0; x < packet->width; ++x) {
            const size_t offset = 4 * (size_t(y) * packet->width + x);
            wide_before.rgba[offset] = x % 251;
            wide_before.rgba[offset + 1] = 37;
            wide_before.rgba[offset + 2] = 209;
            wide_before.rgba[offset + 3] = 83;
        }
    CHECK(ValidatePreviewCapturedPipeline(*wide_pipeline, &error));
    packet->captured_pipeline = wide_pipeline;
    packet->pipeline_digest =
        ComputePreviewCapturedPipelineDigest(*wide_pipeline);
    packet->pipeline_layout_digest =
        ComputePreviewCapturedPipelineDigest(*wide_pipeline, true);
    work.compile_key.pipeline_layout_digest = packet->pipeline_layout_digest;
    render();
    check_native_camera_color("1280x480 owned destination",
                              4 * (240 * 1280 + 640), true);
    for (uint32_t x : { 0U, packet->width - 1 }) {
        const size_t offset = 4 * x;
        CHECK(std::memcmp(pixels.data() + offset,
                          wide_before.rgba.data() + offset, 4) == 0);
    }
    for (const auto extent : { std::array<uint32_t, 2>{ 0, 480 },
                               { kPreviewMaxCapturedWidth + 1, 480 },
                               { 1280, kPreviewMaxCapturedHeight + 1 } }) {
        packet->width = extent[0];
        packet->height = extent[1];
        PreviewVkExecutor invalid_extent;
        CHECK(!invalid_extent.Prepare(work, &error, &unsupported));
        CHECK(unsupported && error.find("extent") != std::string::npos);
        CHECK(invalid_extent.PipelineCreationCountForTest() == 0);
        CHECK(!executor.Prepare(work, &error, &unsupported));
        CHECK(unsupported);
        CHECK(!executor.Render(work, stop, &pixels, &error));
    }
    packet->width = packet->height = 320;
    packet->captured_pipeline = complete_native_pipeline;
    packet->pipeline_digest =
        ComputePreviewCapturedPipelineDigest(*complete_native_pipeline);
    packet->pipeline_layout_digest =
        ComputePreviewCapturedPipelineDigest(*complete_native_pipeline, true);
    work.compile_key.pipeline_layout_digest = packet->pipeline_layout_digest;
    render();
    std::puts("Vulkan captured 1280x480 owned replay, program reuse and extent "
              "bounds PASS");
    for (float outside_z : { -2.0f, 2.0f }) {
        for (bool clamp : { false, true }) {
            auto clipped_pipeline = std::make_shared<PreviewCapturedPipeline>(
                *complete_native_pipeline);
            auto positions = raw_positions;
            for (size_t vertex = 0; vertex < 3; ++vertex)
                positions[vertex * 4 + 2] = outside_z;
            std::memcpy(clipped_pipeline->attributes[0].stream.bytes.data(),
                        positions.data(), sizeof(positions));
            clipped_pipeline->raster.available |= PreviewRasterDepthClamp;
            clipped_pipeline->raster.depth_clamp = clamp;
            packet->captured_pipeline = clipped_pipeline;
            packet->pipeline_digest =
                ComputePreviewCapturedPipelineDigest(*clipped_pipeline);
            packet->pipeline_layout_digest =
                ComputePreviewCapturedPipelineDigest(*clipped_pipeline, true);
            work.compile_key.pipeline_layout_digest =
                packet->pipeline_layout_digest;
            render();
            check_native_camera_color(outside_z < 0 ? "negative clip plane" :
                                                      "positive clip plane",
                                      4 * (160 * 320 + 160), clamp);
            if (clamp) {
                PreviewVkExecutor without_depth_clamp;
                without_depth_clamp.DisableDepthClampForTest();
                CHECK(!without_depth_clamp.Prepare(work, &error,
                                                   &unsupported));
                CHECK(unsupported &&
                      error.find("requires Vulkan depthClamp support") !=
                          std::string::npos);
                clipped_pipeline->raster.depth_clamp = false;
                packet->pipeline_digest =
                    ComputePreviewCapturedPipelineDigest(*clipped_pipeline);
                packet->pipeline_layout_digest =
                    ComputePreviewCapturedPipelineDigest(*clipped_pipeline,
                                                         true);
                work.compile_key.pipeline_layout_digest =
                    packet->pipeline_layout_digest;
                CHECK(without_depth_clamp.Prepare(work, &error,
                                                  &unsupported));
                CHECK(!unsupported);
            }
        }
    }
    packet->captured_pipeline = complete_native_pipeline;
    packet->pipeline_digest =
        ComputePreviewCapturedPipelineDigest(*complete_native_pipeline);
    packet->pipeline_layout_digest =
        ComputePreviewCapturedPipelineDigest(*complete_native_pipeline, true);
    work.compile_key.pipeline_layout_digest = packet->pipeline_layout_digest;
    render();
    std::puts("Vulkan captured depth clamp at both clip planes and unsupported "
              "feature rejection PASS");
    // Original camera replay keeps native strip/fan and quad adjacency
    // assembly. Four indices are not a triangle-list count, and the quad GS
    // must receive four raw vertices without diagnostic mesh conversion.
    for (const auto mode_host : { std::array<uint32_t, 2>{ 6, 4 },
                                  { 7, 5 },
                                  { 8, 6 },
                                  { 9, 7 },
                                  { 10, 5 } }) {
        auto topology_inputs = native_pipeline_inputs;
        topology_inputs.registers[1].value = 4;
        topology_inputs.registers.push_back({ "capture.vk.pipeline_abi", 1 });
        std::array<float, 16> positions =
            mode_host[0] == 6 || mode_host[0] == 9 ?
                std::array<float, 16>{ -1, -1, 0, 1, 1, -1, 0, 1,
                                       -1, 1,  0, 1, 1, 1,  0, 1 } :
                std::array<float, 16>{ -1, -1, 0, 1, 1,  -1, 0, 1,
                                       1,  1,  0, 1, -1, 1,  0, 1 };
        auto &stream = topology_inputs.blobs[0];
        stream.count = 4;
        stream.bytes.resize(sizeof(positions));
        std::memcpy(stream.bytes.data(), positions.data(), sizeof(positions));
        std::array<uint32_t, 8> assembly{};
        assembly[0] = 20; // Vk pipeline input assembly ABI1.
        assembly[5] = mode_host[1];
        OwnedDrawBlob assembly_blob;
        assembly_blob.name = "vk.pipeline.assembly";
        assembly_blob.bytes.resize(sizeof(assembly));
        std::memcpy(assembly_blob.bytes.data(), assembly.data(),
                    sizeof(assembly));
        topology_inputs.blobs.push_back(assembly_blob);
        const std::array<uint32_t, 4> indices{ 0, 1, 2, 3 };
        OwnedDrawBlob index_blob;
        index_blob.name = "vertex.indices";
        index_blob.count = indices.size();
        index_blob.bytes.resize(sizeof(indices));
        std::memcpy(index_blob.bytes.data(), indices.data(), sizeof(indices));
        topology_inputs.blobs.push_back(index_blob);
        if (mode_host[0] == 8 || mode_host[0] == 9) {
            topology_inputs.sources[3] =
                "#version 450\nlayout(lines_adjacency) in;"
                "layout(triangle_strip,max_vertices=6) out;\n"
                "layout(location=0) in vec4 vertexColor[];"
                "layout(location=0) out vec4 geometryColor;\n"
                "void emit(int i){gl_Position=gl_in[i].gl_Position;"
                "geometryColor=vertexColor[i];EmitVertex();}\n"
                "void main(){" +
                std::string(mode_host[0] == 8 ?
                                "emit(1);emit(2);emit(0);EndPrimitive();"
                                "emit(2);emit(3);emit(0);EndPrimitive();" :
                                "emit(0);emit(1);emit(2);EndPrimitive();"
                                "emit(2);emit(1);emit(3);EndPrimitive();") +
                "}";
        }
        auto topology_pipeline = BuildPreviewCapturedPipeline(
            topology_inputs, PreviewBackend::Vulkan, mode_host[0], &error);
        CHECK(topology_pipeline && topology_pipeline->indices ==
                                       std::vector<uint32_t>({ 0, 1, 2, 3 }));
        packet->captured_pipeline = topology_pipeline;
        packet->pipeline_digest =
            ComputePreviewCapturedPipelineDigest(*topology_pipeline);
        packet->pipeline_layout_digest =
            ComputePreviewCapturedPipelineDigest(*topology_pipeline, true);
        work.compile_key.pipeline_layout_digest =
            packet->pipeline_layout_digest;
        render();
        check_native_camera_color("topology indexed", 4 * (100 * 320 + 240),
                                  true);
        const auto indexed_pixels = pixels;
        topology_inputs.blobs.pop_back(); // Exact native array count4.
        auto array_pipeline = BuildPreviewCapturedPipeline(
            topology_inputs, PreviewBackend::Vulkan, mode_host[0], &error);
        CHECK(array_pipeline && array_pipeline->indices.empty() &&
              array_pipeline->ranges.size() == 1 &&
              array_pipeline->ranges[0][0] == 0 &&
              array_pipeline->ranges[0][1] == 4);
        packet->captured_pipeline = array_pipeline;
        packet->pipeline_digest =
            ComputePreviewCapturedPipelineDigest(*array_pipeline);
        packet->pipeline_layout_digest =
            ComputePreviewCapturedPipelineDigest(*array_pipeline, true);
        work.compile_key.pipeline_layout_digest =
            packet->pipeline_layout_digest;
        render();
        CHECK(pixels == indexed_pixels);
        if (mode_host[0] == 8) {
            auto incompatible =
                std::make_shared<PreviewCapturedPipeline>(*topology_pipeline);
            incompatible->geometry_source = native_pipeline_inputs.sources[3];
            packet->captured_pipeline = incompatible;
            packet->pipeline_digest =
                ComputePreviewCapturedPipelineDigest(*incompatible);
            packet->pipeline_layout_digest =
                ComputePreviewCapturedPipelineDigest(*incompatible, true);
            work.compile_key.pipeline_layout_digest =
                packet->pipeline_layout_digest;
            CHECK(!executor.Prepare(work, &error, &unsupported));
            CHECK(unsupported &&
                  error.find("geometry input") != std::string::npos);
        }
    }
    packet->captured_pipeline = complete_native_pipeline;
    packet->pipeline_digest =
        ComputePreviewCapturedPipelineDigest(*complete_native_pipeline);
    packet->pipeline_layout_digest =
        ComputePreviewCapturedPipelineDigest(*complete_native_pipeline, true);
    work.compile_key.pipeline_layout_digest = packet->pipeline_layout_digest;
    render();
    std::puts("Vulkan original strip/fan/quad adjacency assembly and GS "
              "compatibility PASS");
    auto missing_native_uniform =
        std::make_shared<PreviewCapturedPipeline>(*complete_native_pipeline);
    missing_native_uniform->uniforms.pop_back();
    packet->captured_pipeline = missing_native_uniform;
    CHECK(!executor.Render(work, stop, &pixels, &error));
    CHECK(error.find("uniform unavailable: fogColor") != std::string::npos);
    packet->captured_pipeline = complete_native_pipeline;
    packet->captured_material.reset();
    packet->material_digest = {};
    source("#version 450\nlayout(location=0) in vec4 geometryColor;\n"
           "layout(binding=2) uniform sampler2D texSamp0;layout(location=0) "
           "out vec4 color;\n"
           "void main(){color=texture(texSamp0,vec2(.5)) + geometryColor;}");
    packet->partner_source = native_pipeline_inputs.sources[1];
    CHECK(!executor.Prepare(work, &error, &unsupported));
    CHECK(unsupported &&
          error.find("texture base image is unavailable: texSamp0") !=
              std::string::npos);
    packet->captured_pipeline.reset();
    packet->pipeline_digest = {};
    packet->pipeline_layout_digest = {};
    work.compile_key.pipeline_layout_digest = {};
    packet->captured_mesh = {};
    packet->packet_kind = PreviewPacketKind::Synthetic;
    packet->replay_class = PreviewReplayClass::Synthetic;
    std::puts("Vulkan captured constants/base texture, zero missing input and "
              "material edits PASS");
    std::puts("Vulkan original VS/GS, separate UBOs, inline constants and "
              "missing active-input rejection PASS");
    source("#version 450\nlayout(location=4,component=0) in float vtxFog;\n"
           "layout(location=0) out vec4 color;\n"
           "void main(){color=vec4(vtxFog,0,0,1);}\n");
    render(); // Explicit component zero matches the implicit partner component.
    CHECK(pixels[4 * (160 * 320 + 160) + 3] == 255);
    source("#version 450\nlayout(location=0) in vec4 vtxD0;\n"
           "layout(location=0) out vec4 color;\n"
           "void main(){color=vtxD0;}\n");
    for (auto &corner : fixture.corner_colors) corner = {255, 128, 64, 255};
    packet->fixture_bytes = EncodePreviewSyntheticFixture(fixture);
    packet->update_policy = PreviewUpdatePolicy::Continuous;
    packet->binding_count = 1;
    packet->bindings[0] = { PreviewInputTarget::D0RGB, true, 1.0f,
                            -0.5f, 10.0f };
    render();
    const auto time_zero = pixels;
    work.result_key.time_seconds = 4.0;
    // A clock sample consumes the prepared program without another Prepare.
    CHECK(executor.Render(work, stop, &pixels, &error));
    CHECK(pixels != time_zero);
    CHECK(pixels[4 * (160 * 320 + 160)] < time_zero[4 * (160 * 320 + 160)]);
    packet->update_policy = PreviewUpdatePolicy::OnDirty;
    packet->binding_count = 0;
    packet->width = packet->height = 32;
    fixture = MakePreviewFixture(PreviewFixtureProfile::Cubemap);
    packet->fixture_bytes = EncodePreviewSyntheticFixture(fixture);
    source(
        "#version 450\nlayout(binding=3) uniform samplerCube texSamp0;\n"
        "layout(location=5) in vec4 vtxT0; layout(location=0) out vec4 color;\n"
        "void main(){color=texture(texSamp0,vtxT0.xyz);}");
    render();
    CHECK(pixels[4 * (16 * 32 + 16) + 2] == 255);
    const auto cube_z = pixels;
    fixture.cube_direction = { 1, 0, 0 };
    packet->fixture_bytes = EncodePreviewSyntheticFixture(fixture);
    CHECK(executor.Render(work, stop, &pixels, &error));
    CHECK(pixels[4 * (16 * 32 + 16)] == 255);
    CHECK(pixels != cube_z);
    fixture.cube_direction = { 0, 0, 1 };
    packet->fixture_bytes = EncodePreviewSyntheticFixture(fixture);
    source(
        "#version 450\nlayout(binding=3) uniform samplerCube\n\ttexSamp0;\n"
        "layout(location=5) in vec4 vtxT0; layout(location=0) out vec4 color;"
        "void main(){color=texture(texSamp0,vtxT0.xyz);}");
    render();
    CHECK(pixels[4 * (16 * 32 + 16)] == 40 &&
          pixels[4 * (16 * 32 + 16) + 2] == 255);
    fixture.cube_direction = { 1, 0, 0 };
    fixture.uv_scale = { 0, 0 };
    fixture.uv_offset = { 0.25f, 0.25f };
    packet->fixture_bytes = EncodePreviewSyntheticFixture(fixture);
    source(
        "#version 450\n// samplerCube texSamp0\n/* samplerCube texSamp0; */\n"
        "layout(binding=3) uniform sampler2D texSamp0;\n"
        "layout(location=5) in vec4 vtxT0; layout(location=0) out vec4 color;"
        "void main(){color=vec4(vtxT0.xy,0,1)*texture(texSamp0,vec2(0.5));}");
    render();
    CHECK(pixels[4 * (16 * 32 + 16)] == 64 &&
          pixels[4 * (16 * 32 + 16) + 1] == 10);
    std::puts("Vulkan reflected cube routing: whitespace and misleading "
              "comments PASS");
    std::puts("Vulkan cube +Z blue / +X red PASS");
    fixture = MakePreviewFixture(PreviewFixtureProfile::MultiTexture);
    fixture.textures.fill(PreviewFixtureProfile::MultiTexture);
    packet->fixture_bytes = EncodePreviewSyntheticFixture(fixture);
    source("#version 450\nlayout(binding=7) uniform sampler2D texSamp0;\n"
           "layout(binding=2) uniform sampler2D texSamp1;\n"
           "layout(location=0) out vec4 color;\n"
           "void "
           "main(){color=vec4(texture(texSamp0,vec2(0.5)).r,texture(texSamp1,"
           "vec2(0.5)).g,0,1);}");
    render();
    CHECK(pixels[4 * (16 * 32 + 16)] == 255 &&
          pixels[4 * (16 * 32 + 16) + 1] == 255);
    fixture.textures[1] = PreviewFixtureProfile::Flat;
    packet->fixture_bytes = EncodePreviewSyntheticFixture(fixture);
    CHECK(executor.Render(work, stop, &pixels, &error));
    CHECK(pixels[4 * (16 * 32 + 16)] == 255 &&
          pixels[4 * (16 * 32 + 16) + 1] == 90);
    std::puts("Vulkan distinct T0/T1 and independent input edit PASS");
    source("#version 450\nlayout(location=0) in vec4 vtxD0; layout(location=1) "
           "in vec4 vtxD1;\n"
           "layout(location=2) in vec4 vtxB0; layout(location=3) in vec4 "
           "vtxB1; layout(location=4) in float vtxFog;\n"
           "layout(location=0) out vec4 color; void "
           "main(){color=vec4(vtxD0.r,vtxD1.g,vtxB0.b,vtxB1.a)*vtxFog;}");
    fixture.fog = 0.5f;
    fixture.colors[0][0] = 0.2f;
    fixture.colors[1][1] = 0.4f;
    fixture.colors[2][2] = 0.6f;
    fixture.colors[3][3] = 0.8f;
    packet->fixture_bytes = EncodePreviewSyntheticFixture(fixture);
    render();
    for (int i = 0; i < 4; ++i)
        CHECK(std::abs(int(pixels[4 * (16 * 32 + 16) + i]) -
                       int((i + 1) * 25.5f)) <= 1);
    fixture.fog = 1;
    packet->fixture_bytes = EncodePreviewSyntheticFixture(fixture);
    CHECK(executor.Render(work, stop, &pixels, &error));
    CHECK(pixels[4 * (16 * 32 + 16)] == 51);
    std::puts("Vulkan independent D0/D1/B0/B1 and fog edit PASS");
    source("#version 450\nlayout(binding=4) uniform sampler2D texSamp0; "
           "layout(location=0) out vec4 color; void "
           "main(){color=texture(texSamp0,vec2(0.3125));}");
    for (int profile = 0; profile < 9; ++profile) {
        fixture =
            MakePreviewFixture(static_cast<PreviewFixtureProfile>(profile));
        fixture.linear_filter = 0;
        packet->fixture_bytes = EncodePreviewSyntheticFixture(fixture);
        render();
        const auto expected = GeneratePreviewTexture(fixture, 0);
        for (int c = 0; c < 4; ++c)
            CHECK(pixels[4 * (16 * 32 + 16) + c] ==
                  expected[4 * (2 * 8 + 2) + c]);
    }
    std::puts("Vulkan all nine profile sample readbacks PASS");
    auto reject = [&](const std::string &declaration, const std::string &body) {
        source("#version 450\n" + declaration +
               "\nlayout(location=0) out vec4 color;\nvoid main(){" + body +
               "}\n");
        error.clear();
        CHECK(!executor.Prepare(work, &error, &unsupported));
        CHECK(unsupported);
    };
    reject("layout(binding=3) uniform samplerCube cube;",
           "color=texture(cube,vec3(1));");
    reject("layout(binding=3) uniform usampler2D texSamp0;",
           "color=vec4(texture(texSamp0,vec2(1)));");
    reject("layout(binding=3) uniform sampler2DArray texSamp0;",
           "color=texture(texSamp0,vec3(1));");
    reject("layout(binding=1,std140) uniform U {vec2 consts[18];};",
           "color=vec4(consts[0],0,1);");
    reject("layout(binding=1,std140) uniform U {vec4 unknown;};",
           "color=unknown;");
    reject("layout(push_constant) uniform U {vec4 consts;};", "color=consts;");
    reject("layout(binding=1,std430) buffer U {vec4 consts;};",
           "color=consts;");
    reject("layout(location=5) in vec3 unexpected;",
           "color=vec4(unexpected,1);");
    reject("layout(location=4,component=1) in float vtxFog;",
           "color=vec4(vtxFog);");
    reject("", "color=vec4(gl_PointCoord,0,1);");
    source("#version 450\nthis is invalid GLSL");
    CHECK(!executor.Prepare(work, &error, &unsupported));
    CHECK(!unsupported);
    source("#version 450\nlayout(location=0) out vec4 color;\nvoid "
           "main(){color=vec4(1);}");
    render(); // recover from rejected/failed preparations
    stop = true;
    CHECK(!executor.Render(work, stop, &pixels, &error));
    std::puts("private Vulkan: quad, sampler, fixture edits, 160/320 resize, "
              "uniform block, rejection, recovery and stop PASS");
}

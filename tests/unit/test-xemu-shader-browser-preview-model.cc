#include "../../ui/xui/shader-browser-preview-model.hh"

#include <cassert>
#include <cmath>
#include <cstring>
#include <iostream>
#include <memory>
#include <cstdlib>

using namespace xemu::shader_browser;

#define CHECK(expression)                                   \
    do {                                                    \
        if (!(expression)) {                                \
            std::cerr << __LINE__ << ": " #expression "\n"; \
            std::abort();                                   \
        }                                                   \
    } while (false)

static PreviewPacket MakePacket(uint32_t title_id = 0x4d530064)
{
    PreviewPacket packet{};
    packet.selection.scope.title_id = title_id;
    packet.selection.scope.executable_fingerprint_version = 1;
    packet.selection.scope.executable_fingerprint[0] = 0x44;
    packet.selection.shader.stage = Stage::Pixel;
    packet.selection.session_epoch = 7;
    packet.selection.renderer_epoch = 9;
    packet.selection.backend = PreviewBackend::OpenGL;
    packet.selection.mode = PreviewMode::Normal;
    packet.recipe_format_version = 1;
    packet.recipe = { 1, 2, 3, 4, 5, 6 };
    packet.selection.shader.hash =
        ComputeShaderHash(1, Stage::Pixel, packet.recipe_format_version,
                          packet.recipe.data(), packet.recipe.size());
    packet.generator_abi = 3;
    packet.interface_abi = 1;
    packet.input_revision = 11;
    packet.view_revision = 5;
    packet.width = 320;
    packet.height = 320;
    packet.packet_kind = PreviewPacketKind::Synthetic;
    packet.replay_class = PreviewReplayClass::Synthetic;
    packet.fixture_bytes = { 7, 8, 9 };
    packet.fixture_digest = ComputePreviewDigest(packet.fixture_bytes.data(),
                                                 packet.fixture_bytes.size());
    return packet;
}

static void TestNativeCaptureExtentLimits()
{
    auto inputs_for = [](uint32_t width, uint32_t height) {
        OwnedDrawInputs inputs;
        inputs.complete = true;
        inputs.sources[1] = "#version 450\nlayout(location=0) in vec4 position;"
                            "void main(){gl_Position=position;}";
        inputs.registers = { { "capture.vk.pipeline_abi", 1 },
                             { "capture.vertices.first", 0 },
                             { "capture.vertices.count", 3 },
                             { "capture.vertex.0.enabled", 1 } };
        const float viewport[]{ 0, 0, float(width), float(height), 0, 1 };
        const uint32_t assembly[]{ 20, 0, 0, 0, 0, 3, 0, 0 };
        const uint32_t indices[]{ 0, 1, 2 };
        auto blob = [&](const char *name, const void *data, size_t size) {
            OwnedDrawBlob value;
            value.name = name;
            const auto *bytes = static_cast<const uint8_t *>(data);
            value.bytes.assign(bytes, bytes + size);
            inputs.blobs.push_back(std::move(value));
        };
        blob("vk.viewport", viewport, sizeof(viewport));
        blob("vk.pipeline.assembly", assembly, sizeof(assembly));
        blob("vertex.indices", indices, sizeof(indices));
        inputs.blobs.back().count = 3;
        const float positions[]{ -1, -1, 0, 1, 1, -1, 0, 1, 0, 1, 0, 1 };
        blob("vertex.attribute0", positions, sizeof(positions));
        auto &stream = inputs.blobs.back();
        stream.count = 3;
        stream.format = 109;
        stream.components = 4;
        stream.stride = 16;
        inputs.before = {
            width, height, std::vector<uint8_t>(size_t(width) * height * 4, 25)
        };
        return inputs;
    };
    std::string error;
    auto native_inputs = inputs_for(1280, 480);
    auto native_pipeline = BuildPreviewCapturedPipeline(
        native_inputs, PreviewBackend::Vulkan, 5, &error);
    CHECK(native_pipeline);
    CHECK(native_pipeline->raster.width == 1280 &&
          native_pipeline->raster.height == 480 &&
          native_pipeline->color_before.rgba.size() == 1280U * 480U * 4U);
    auto packet_for =
        [&](std::shared_ptr<const PreviewCapturedPipeline> pipeline) {
            auto packet = MakePacket();
            packet.selection.backend = PreviewBackend::Vulkan;
            packet.packet_kind = PreviewPacketKind::Replay;
            packet.replay_class = PreviewReplayClass::Approximate;
            packet.width = pipeline->raster.width;
            packet.height = pipeline->raster.height;
            packet.partner_source = native_inputs.sources[1];
            packet.partner_digest = ComputePreviewDigest(
                reinterpret_cast<const uint8_t *>(packet.partner_source.data()),
                packet.partner_source.size());
            packet.captured_pipeline = std::move(pipeline);
            packet.pipeline_digest =
                ComputePreviewCapturedPipelineDigest(*packet.captured_pipeline);
            packet.pipeline_layout_digest =
                ComputePreviewCapturedPipelineDigest(*packet.captured_pipeline,
                                                     true);
            return packet;
        };
    CHECK(ValidatePreviewPacket(packet_for(native_pipeline), &error));
    auto synthetic = MakePacket();
    synthetic.width = 1280;
    synthetic.height = 480;
    CHECK(!ValidatePreviewPacket(synthetic, &error));
    const auto hd_inputs = inputs_for(1920, 1080);
    const auto hd = BuildPreviewCapturedPipeline(
        hd_inputs, PreviewBackend::Vulkan, 5, &error);
    CHECK(hd && ValidatePreviewPacket(packet_for(hd), &error));
    CHECK(!BuildPreviewCapturedPipeline(hd_inputs, PreviewBackend::Vulkan, 5,
                                        &error, 4U * 1024U * 1024U));
    for (const auto extent : { std::array<uint32_t, 2>{ 1921, 1080 },
                               std::array<uint32_t, 2>{ 1920, 1081 },
                               std::array<uint32_t, 2>{ 0, 480 },
                               std::array<uint32_t, 2>{ 1280, 0 } }) {
        const auto unsupported = inputs_for(extent[0], extent[1]);
        CHECK(!BuildPreviewCapturedPipeline(unsupported, PreviewBackend::Vulkan,
                                            5, &error));
    }
    native_inputs.before.rgba.pop_back();
    CHECK(!BuildPreviewCapturedPipeline(native_inputs, PreviewBackend::Vulkan,
                                        5, &error));
    auto retained = std::make_shared<PreviewCapturedPipeline>(*native_pipeline);
    retained->color_before.rgba.reserve(16U * 1024U * 1024U + 1);
    CHECK(!ValidatePreviewCapturedPipeline(*retained, &error));
}

int main()
{
    TestNativeCaptureExtentLimits();
    PreviewPacket base = MakePacket();
    std::string error;
    assert(ValidatePreviewPacket(base, &error));

    PreviewCompileKey compile_a = BuildPreviewCompileKey(base);
    PreviewResultKey result_a = BuildPreviewResultKey(base);

    PreviewPacket instrumented = base;
    instrumented.profile_draw = true;
    assert(BuildPreviewCompileKey(instrumented) == compile_a);
    assert(BuildPreviewResultKey(instrumented) != result_a);
    uint64_t interval = 0;
    assert(ComputePreviewDrawInterval(250, 5, 8, 2.0, &interval, &error));
    assert(interval == 22); // An 8-bit counter wrapped once.
    assert(ComputePreviewDrawInterval(UINT64_MAX - 4, 5, 64, 1.0, &interval,
                                      &error));
    assert(interval == 10);
    assert(!ComputePreviewDrawInterval(10, 10, 64, 1.0, &interval, &error));
    assert(!ComputePreviewDrawInterval(10, 9, 64, 1.0, &interval, &error));
    assert(!ComputePreviewDrawInterval(0, 1, 0, 1.0, &interval, &error));
    assert(!ComputePreviewDrawInterval(0, 1, 65, 1.0, &interval, &error));
    assert(!ComputePreviewDrawInterval(0, 1, 64, NAN, &interval, &error));
    assert(!ComputePreviewDrawInterval(0, 1, 64, 0, &interval, &error));
    assert(!ComputePreviewDrawInterval(0, kPreviewMaxDrawTimingNs + 1, 64, 1.0,
                                       &interval, &error));
    PreviewDrawTimingDistribution distribution;
    PreviewDrawTiming timing;
    timing.status = PreviewDrawTimingStatus::Measured;
    for (uint64_t n = 1; n <= 100; ++n) {
        timing.nanoseconds = n;
        AccumulatePreviewDrawTiming(&distribution, timing);
    }
    timing.status = PreviewDrawTimingStatus::Unsupported;
    AccumulatePreviewDrawTiming(&distribution, timing);
    FinalizePreviewDrawTimingDistribution(&distribution);
    assert(distribution.requested == 101 && distribution.measured == 100);
    assert(distribution.sample_count == 64 && distribution.unsupported == 1);
    assert(distribution.median_ns == 68.5 && distribution.p95_ns == 97);

    PreviewPacket input_changed = base;
    input_changed.input_revision++;
    input_changed.fixture_bytes.push_back(10);
    input_changed.fixture_digest = ComputePreviewDigest(
        input_changed.fixture_bytes.data(), input_changed.fixture_bytes.size());
    assert(BuildPreviewCompileKey(input_changed) == compile_a);
    assert(BuildPreviewResultKey(input_changed) != result_a);

    PreviewPacket binding_changed = base;
    binding_changed.binding_count = 1;
    binding_changed.bindings[0].target = PreviewInputTarget::UVOffsetU;
    binding_changed.bindings[0].enabled = true;
    binding_changed.bindings[0].base = 0.25f;
    assert(ValidatePreviewPacket(binding_changed, &error));
    assert(BuildPreviewCompileKey(binding_changed) == compile_a);
    assert(BuildPreviewResultKey(binding_changed) != result_a);
    binding_changed.bindings[0].period_seconds = 0;
    assert(!ValidatePreviewPacket(binding_changed, &error));

    PreviewPacket state_changed = base;
    state_changed.render_state.depth_write = false;
    state_changed.render_state.alpha_test = true;
    assert(ValidatePreviewPacket(state_changed, &error));
    assert(BuildPreviewCompileKey(state_changed) == compile_a);
    assert(BuildPreviewResultKey(state_changed) != result_a);
    state_changed.render_state.clear_color[0] = NAN;
    assert(!ValidatePreviewPacket(state_changed, &error));

    PreviewPacket large_view = base;
    large_view.width = 640;
    large_view.height = 480;
    assert(ValidatePreviewPacket(large_view, &error));
    large_view.width = 641;
    assert(!ValidatePreviewPacket(large_view, &error));


    PreviewPacket edited = base;
    edited.source_variant = PreviewSourceVariant::Edited;
    edited.draft_id = 9;
    edited.draft_revision = 2;
    edited.draft_submission_id = 1;
    edited.source = "void main(){}";
    edited.source_digest = ComputePreviewDigest(
        reinterpret_cast<const uint8_t *>(edited.source.data()),
        edited.source.size());
    assert(ValidatePreviewPacket(edited, &error));
    assert(BuildPreviewCompileKey(edited) != compile_a);
    PreviewCompileKey edited_compile = BuildPreviewCompileKey(edited);
    edited.draft_submission_id++;
    assert(ValidatePreviewPacket(edited, &error));
    assert(BuildPreviewCompileKey(edited) != edited_compile);
    edited.draft_submission_id = 0;
    assert(!ValidatePreviewPacket(edited, &error));
    edited.draft_submission_id = 2;
    edited_compile = BuildPreviewCompileKey(edited);
    edited.draft_revision++;
    assert(BuildPreviewCompileKey(edited) != edited_compile);
    edited.draft_id = 0;
    assert(!ValidatePreviewPacket(edited, &error));
    edited.source_variant = PreviewSourceVariant::Original;
    edited.draft_revision = 0;
    assert(!ValidatePreviewPacket(edited, &error));
    edited.draft_submission_id = 0;
    assert(ValidatePreviewPacket(edited, &error));

    auto channel_result = result_a;
    channel_result.channel = PreviewChannel::Alpha;
    assert(channel_result != result_a);
    assert(channel_result.compile == result_a.compile);
    assert(PreviewChannelAvailable(PreviewChannel::FixtureAlphaMask));
    assert(!PreviewChannelAvailable(PreviewChannel::ShaderDiscard));
    assert(!PreviewChannelAvailable(static_cast<PreviewChannel>(255)));
    assert(std::string(PreviewChannelProvenance(PreviewChannel::ShaderDiscard))
               .find("Unsupported") != std::string::npos);

    PreviewPacket other_title = base;
    other_title.selection.scope.title_id++;
    assert(BuildPreviewCompileKey(other_title) != compile_a);

    PreviewPacket replacement = base;
    replacement.selection.mode = PreviewMode::Replacement;
    replacement.replacement_id = 77;
    replacement.replacement_revision = 4;
    replacement.source = "void main(){}";
    replacement.source_digest = ComputePreviewDigest(
        reinterpret_cast<const uint8_t *>(replacement.source.data()),
        replacement.source.size());
    assert(ValidatePreviewPacket(replacement, &error));
    PreviewCompileKey replacement_key = BuildPreviewCompileKey(replacement);
    replacement.replacement_revision++;
    replacement.source += " // revision";
    replacement.source_digest = ComputePreviewDigest(
        reinterpret_cast<const uint8_t *>(replacement.source.data()),
        replacement.source.size());
    assert(BuildPreviewCompileKey(replacement) != replacement_key);

    PreviewPacket stale_source_digest = replacement;
    stale_source_digest.source += " // changed without digest";
    assert(!ValidatePreviewPacket(stale_source_digest, &error));
    assert(error.find("source digest") != std::string::npos);

    PreviewPacket partner = base;
    partner.partner_source = "preview partner stage";
    partner.partner_digest = ComputePreviewDigest(
        reinterpret_cast<const uint8_t *>(partner.partner_source.data()),
        partner.partner_source.size());
    assert(ValidatePreviewPacket(partner, &error));
    PreviewCompileKey partner_key = BuildPreviewCompileKey(partner);
    partner.partner_source += " changed";
    assert(!ValidatePreviewPacket(partner, &error));
    assert(error.find("partner digest") != std::string::npos);
    partner.partner_digest = ComputePreviewDigest(
        reinterpret_cast<const uint8_t *>(partner.partner_source.data()),
        partner.partner_source.size());
    assert(ValidatePreviewPacket(partner, &error));
    assert(BuildPreviewCompileKey(partner) != partner_key);

    PreviewPacket orphan_source_digest = base;
    orphan_source_digest.source_digest[0] = 1;
    assert(!ValidatePreviewPacket(orphan_source_digest, &error));
    assert(error.find("without source bytes") != std::string::npos);

    PreviewPacket orphan_partner_digest = base;
    orphan_partner_digest.partner_digest[0] = 1;
    assert(!ValidatePreviewPacket(orphan_partner_digest, &error));
    assert(error.find("without partner bytes") != std::string::npos);

    PreviewPacket stale_fixture_digest = base;
    stale_fixture_digest.fixture_bytes.push_back(0xff);
    assert(!ValidatePreviewPacket(stale_fixture_digest, &error));
    assert(error.find("fixture digest") != std::string::npos);

    PreviewPacket orphan_fixture_digest = base;
    orphan_fixture_digest.fixture_bytes.clear();
    assert(!ValidatePreviewPacket(orphan_fixture_digest, &error));
    assert(error.find("without fixture bytes") != std::string::npos);

    PreviewPacket wrong_hash = base;
    wrong_hash.selection.shader.hash.bytes[0] ^= 0xff;
    assert(!ValidatePreviewPacket(wrong_hash, &error));
    assert(error.find("hash") != std::string::npos);

    PreviewPacket vertex = base;
    vertex.selection.shader.stage = Stage::Vertex;
    vertex.selection.shader.hash =
        ComputeShaderHash(1, Stage::Vertex, vertex.recipe_format_version,
                          vertex.recipe.data(), vertex.recipe.size());
    assert(!ValidatePreviewPacket(vertex, &error));

    PreviewPacket unsupported_replay = base;
    unsupported_replay.packet_kind = PreviewPacketKind::Replay;
    unsupported_replay.replay_class = PreviewReplayClass::Unsupported;
    assert(!ValidatePreviewPacket(unsupported_replay, &error));

    PreviewPacket empty_replay = base;
    empty_replay.packet_kind = PreviewPacketKind::Replay;
    empty_replay.replay_class = PreviewReplayClass::Complete;
    empty_replay.fixture_bytes.clear();
    empty_replay.fixture_digest = {};
    assert(!ValidatePreviewPacket(empty_replay, &error));

    PreviewPacket game_draw = base;
    game_draw.packet_kind = PreviewPacketKind::Replay;
    game_draw.replay_class = PreviewReplayClass::Approximate;
    game_draw.captured_mesh.positions = { { -1, -1, 0, 1 },
                                          { 1, -1, 0, 1 },
                                          { 0, 1, 0, 1 } };
    game_draw.captured_mesh.indices = { 0, 1, 2 };
    game_draw.mesh_digest = ComputeCapturedMeshDigest(game_draw.captured_mesh);
    assert(ValidatePreviewPacket(game_draw, &error));
    assert(BuildPreviewCompileKey(game_draw) == compile_a);
    assert(BuildPreviewResultKey(game_draw) != result_a);
    OwnedDrawInputs captured_inputs;
    captured_inputs.complete = true;
    auto &captured_texture = captured_inputs.textures[0];
    captured_texture.described = true;
    captured_texture.metadata.bound = true;
    captured_texture.metadata.width = captured_texture.metadata.height = 2;
    captured_texture.metadata.depth = 1;
    captured_texture.metadata.face_count = 1;
    captured_texture.metadata.mip_levels = 1;
    captured_texture.images.push_back(
        { 0, 0, { 2, 2, std::vector<uint8_t>(16, 25) } });
    captured_inputs.uniforms.push_back(
        { 2, "alphaRef", XEMU_SHADER_DRAW_UNIFORM_INT, 1, 1, { 64, 0, 0, 0 } });
    auto captured_material = BuildPreviewCapturedMaterial(captured_inputs);
    assert(captured_material);
    assert(captured_material->textures[0].images[0].image.rgba[0] == 25);
    captured_inputs.textures[0].images[0].image.rgba[0] = 99;
    assert(captured_material->textures[0].images[0].image.rgba[0] == 25);
    auto swizzled_inputs = captured_inputs;
    swizzled_inputs.registers.push_back({ "capture.texture0.swizzle3", 1 });
    auto swizzled_material = BuildPreviewCapturedMaterial(swizzled_inputs);
    assert(swizzled_material->textures[0].images[0].image.rgba[3] == 255);
    assert(swizzled_inputs.textures[0].images[0].image.rgba[3] == 25);
    swizzled_inputs.textures[0].metadata.min_filter = 1;
    swizzled_inputs.textures[0].metadata.mag_filter = 0;
    swizzled_inputs.textures[0].metadata.wrap_s = 0;
    swizzled_inputs.textures[0].metadata.wrap_t = 1;
    swizzled_inputs.textures[0].metadata.wrap_r = 2;
    auto native_vk_material =
        BuildPreviewCapturedMaterial(swizzled_inputs, PreviewBackend::Vulkan);
    assert(native_vk_material->textures[0].metadata.min_filter == 0x2601);
    assert(native_vk_material->textures[0].metadata.mag_filter == 0x2600);
    assert(native_vk_material->textures[0].metadata.wrap_s == 0x2901);
    assert(native_vk_material->textures[0].metadata.wrap_t == 0x8370);
    assert(native_vk_material->textures[0].metadata.wrap_r == 0x812f);
    assert(native_vk_material->textures[0].images[0].image.rgba[3] == 25);
    auto material_draw = game_draw;
    material_draw.captured_material = captured_material;
    material_draw.material_digest =
        ComputePreviewCapturedMaterialDigest(*captured_material);
    assert(ValidatePreviewPacket(material_draw, &error));
    assert(BuildPreviewCompileKey(material_draw) == compile_a);
    assert(BuildPreviewResultKey(material_draw) !=
           BuildPreviewResultKey(game_draw));
    assert(PreviewPacketOwnedBytes(material_draw, nullptr) >
           PreviewPacketOwnedBytes(game_draw, nullptr));
    auto stale_material = material_draw;
    stale_material.material_digest[0] ^= 1;
    assert(!ValidatePreviewPacket(stale_material, &error));
    auto depth_inputs = captured_inputs;
    depth_inputs.textures[0].metadata.host_format = 70; // VK_FORMAT_R16_UNORM
    depth_inputs.textures[0].metadata.guest_format = 0x30;
    depth_inputs.blobs.push_back({ "texture.storage.0",
                                   { 0x00, 0x80, 0x01, 0x80, 0xff, 0xff, 0, 0 },
                                   0,
                                   70,
                                   1,
                                   2,
                                   4,
                                   0,
                                   1,
                                   0 });
    auto depth_material =
        BuildPreviewCapturedMaterial(depth_inputs, PreviewBackend::Vulkan);
    assert(depth_material->texture_storage[0].bytes ==
           std::vector<uint8_t>({ 0x00, 0x80, 0x01, 0x80, 0xff, 0xff, 0, 0 }));
    auto depth_packet = game_draw;
    depth_packet.captured_material = depth_material;
    depth_packet.material_digest =
        ComputePreviewCapturedMaterialDigest(*depth_material);
    assert(ValidatePreviewPacket(depth_packet, &error));
    auto invalid_storage =
        std::make_shared<PreviewCapturedMaterial>(*depth_material);
    invalid_storage->texture_storage[0].bytes.pop_back();
    depth_packet.captured_material = invalid_storage;
    depth_packet.material_digest =
        ComputePreviewCapturedMaterialDigest(*invalid_storage);
    assert(!ValidatePreviewPacket(depth_packet, &error));
    const auto depth_digest =
        ComputePreviewCapturedMaterialDigest(*depth_material);
    depth_inputs.blobs[0].bytes[0] = 1;
    auto next_depth =
        BuildPreviewCapturedMaterial(depth_inputs, PreviewBackend::Vulkan);
    assert(ComputePreviewCapturedMaterialDigest(*next_depth) != depth_digest);
    depth_inputs.blobs[0].bytes.resize(1);
    assert(BuildPreviewCapturedMaterial(depth_inputs, PreviewBackend::Vulkan)
               ->texture_storage[0]
               .bytes.empty());

    auto unavailable_material = BuildPreviewCapturedMaterial(OwnedDrawInputs{});
    assert(unavailable_material->limitations & PreviewMaterialUnavailable);
    auto missing_inputs = captured_inputs;
    missing_inputs.textures[0].images.clear();
    assert(BuildPreviewCapturedMaterial(missing_inputs)->limitations &
           PreviewMaterialMissingTexture);
    auto budget_material = BuildPreviewCapturedMaterial(captured_inputs, 1);
    assert(budget_material->limitations & PreviewMaterialBudgetLimited);
    auto unknown_uniform_inputs = captured_inputs;
    unknown_uniform_inputs.uniforms[0].name = "unrecognizedSharedInput";
    unknown_uniform_inputs.uniforms[0].stage = 0;
    assert(BuildPreviewCapturedMaterial(unknown_uniform_inputs)->limitations &
           PreviewMaterialUnappliedUniform);
    auto material_on_synthetic = base;
    material_on_synthetic.captured_material = captured_material;
    material_on_synthetic.material_digest = material_draw.material_digest;
    assert(!ValidatePreviewPacket(material_on_synthetic, &error));
    auto raw_inputs = captured_inputs;
    raw_inputs.sources[1] = "owned original vertex source";
    raw_inputs.sources[3] = "owned original geometry source";
    raw_inputs.registers = { { "capture.first_vertex", 0 },
                             { "capture.last_vertex", 2 },
                             { "vertex.enabled0", 1 } };
    OwnedDrawBlob raw_position;
    raw_position.name = "vertex.attribute0";
    raw_position.format = 0x1406;
    raw_position.components = 4;
    raw_position.stride = 16;
    raw_position.count = 3;
    raw_position.bytes.resize(48, 0x7f);
    OwnedDrawBlob raw_index;
    raw_index.name = "geometry.host_indices";
    raw_index.count = 3;
    raw_index.bytes = { 0, 0, 0, 0, 1, 0, 0, 0, 2, 0, 0, 0 };
    raw_inputs.blobs = { raw_position, raw_index };
    auto raw_pipeline = BuildPreviewCapturedPipeline(
        raw_inputs, PreviewBackend::OpenGL, 5, &error);
    assert(raw_pipeline && raw_pipeline->attributes[0].stream.bytes[0] == 0x7f);
    auto native_draw = game_draw;
    native_draw.captured_mesh = {};
    native_draw.mesh_digest = {};
    native_draw.partner_source = raw_inputs.sources[1];
    native_draw.partner_digest = ComputePreviewDigest(
        reinterpret_cast<const uint8_t *>(native_draw.partner_source.data()),
        native_draw.partner_source.size());
    native_draw.captured_pipeline = raw_pipeline;
    native_draw.pipeline_digest =
        ComputePreviewCapturedPipelineDigest(*raw_pipeline);
    native_draw.pipeline_layout_digest =
        ComputePreviewCapturedPipelineDigest(*raw_pipeline, true);
    assert(ValidatePreviewPacket(native_draw, &error));
    raw_inputs.blobs[0].bytes[0] = 0;
    auto updated_pipeline = BuildPreviewCapturedPipeline(
        raw_inputs, PreviewBackend::OpenGL, 5, &error);
    assert(raw_pipeline->attributes[0].stream.bytes[0] == 0x7f);
    assert(ComputePreviewCapturedPipelineDigest(*updated_pipeline) !=
           native_draw.pipeline_digest);
    assert(ComputePreviewCapturedPipelineDigest(*updated_pipeline, true) ==
           native_draw.pipeline_layout_digest);
    // A known single-sample framebuffer with all coverage modifiers disabled
    // is replayable. Missing evidence stays partial; active multisampling and
    // malformed enable values must never inherit the private FBO defaults.
    OwnedDrawInputs coverage_inputs;
    coverage_inputs.registers = {
        { "host.sample_buffers", 0 },
        { "host.samples", 0 },
        { "host.sample_alpha_to_coverage", 0 },
        { "host.sample_alpha_to_one", 0 },
        { "host.sample_coverage_enabled", 0 },
        { "host.sample_mask_enabled", 0 },
    };
    PreviewCapturedRaster coverage;
    assert(DecodePreviewCapturedRaster(coverage_inputs, PreviewBackend::OpenGL,
                                       &coverage, &error));
    assert(coverage.available & PreviewRasterCoverage);
    coverage_inputs.registers[0].value = 1;
    coverage_inputs.registers[1].value = 4;
    assert(!DecodePreviewCapturedRaster(coverage_inputs, PreviewBackend::OpenGL,
                                        &coverage, &error));
    coverage_inputs.registers[0].value = coverage_inputs.registers[1].value = 0;
    coverage_inputs.registers[2].value = 1;
    assert(!DecodePreviewCapturedRaster(coverage_inputs, PreviewBackend::OpenGL,
                                        &coverage, &error));
    coverage_inputs.registers[2].value = 2;
    assert(!DecodePreviewCapturedRaster(coverage_inputs, PreviewBackend::OpenGL,
                                        &coverage, &error));
    coverage_inputs.registers[2].value = 0;
    coverage_inputs.registers.pop_back();
    assert(DecodePreviewCapturedRaster(coverage_inputs, PreviewBackend::OpenGL,
                                       &coverage, &error));
    assert(!(coverage.available & PreviewRasterCoverage));
    auto seeded_inputs = raw_inputs;
    seeded_inputs.registers.push_back({ "host.depth_clamp_enabled", 1 });
    seeded_inputs.before = { 2, 2, std::vector<uint8_t>(16, 25) };
    seeded_inputs.registers.push_back({ "host.cull_enabled", 1 });
    seeded_inputs.registers.push_back({ "host.cull_face", 0x0405 });
    seeded_inputs.registers.push_back({ "host.front_face", 0x0900 });
    auto seeded_pipeline = BuildPreviewCapturedPipeline(
        seeded_inputs, PreviewBackend::OpenGL, 5, &error);
    assert(seeded_pipeline && seeded_pipeline->raster.cull_mode == 2 &&
           !seeded_pipeline->raster.front_ccw);
    assert(seeded_pipeline->raster.depth_clamp &&
           (seeded_pipeline->raster.available & PreviewRasterDepthClamp));
    assert(DescribePreviewCapturedRaster(*seeded_pipeline)
               .find("seeded from owned before") != std::string::npos);
    const auto seeded_digest =
        ComputePreviewCapturedPipelineDigest(*seeded_pipeline);
    const auto seeded_layout =
        ComputePreviewCapturedPipelineDigest(*seeded_pipeline, true);
    auto unclamped_pipeline = *seeded_pipeline;
    unclamped_pipeline.raster.depth_clamp = false;
    assert(ComputePreviewCapturedPipelineDigest(unclamped_pipeline) !=
           seeded_digest);
    assert(ComputePreviewCapturedPipelineDigest(unclamped_pipeline, true) !=
           seeded_layout);
    auto clamped_packet = native_draw;
    clamped_packet.captured_pipeline = seeded_pipeline;
    clamped_packet.pipeline_digest = seeded_digest;
    clamped_packet.pipeline_layout_digest = seeded_layout;
    auto unclamped_packet = clamped_packet;
    unclamped_packet.captured_pipeline =
        std::make_shared<PreviewCapturedPipeline>(unclamped_pipeline);
    unclamped_packet.pipeline_digest =
        ComputePreviewCapturedPipelineDigest(unclamped_pipeline);
    unclamped_packet.pipeline_layout_digest =
        ComputePreviewCapturedPipelineDigest(unclamped_pipeline, true);
    assert(BuildPreviewResultKey(clamped_packet) !=
           BuildPreviewResultKey(unclamped_packet));
    assert(BuildPreviewCompileKey(clamped_packet) !=
           BuildPreviewCompileKey(unclamped_packet));
    assert(clamped_packet.source_digest == unclamped_packet.source_digest &&
           clamped_packet.partner_digest == unclamped_packet.partner_digest);
    auto missing_clamp_evidence = *seeded_pipeline;
    missing_clamp_evidence.raster.available &= ~PreviewRasterDepthClamp;
    assert(!ValidatePreviewCapturedPipeline(missing_clamp_evidence, &error));
    auto malformed_clamp_inputs = seeded_inputs;
    malformed_clamp_inputs.registers[raw_inputs.registers.size()].value = 2;
    assert(!BuildPreviewCapturedPipeline(malformed_clamp_inputs,
                                         PreviewBackend::OpenGL, 5, &error));
    seeded_inputs.before.rgba[0] = 90;
    auto changed_seed = BuildPreviewCapturedPipeline(
        seeded_inputs, PreviewBackend::OpenGL, 5, &error);
    assert(seeded_pipeline->color_before.rgba[0] == 25 &&
           changed_seed->color_before.rgba[0] == 90);
    assert(ComputePreviewCapturedPipelineDigest(*changed_seed) !=
           seeded_digest);
    assert(ComputePreviewCapturedPipelineDigest(*changed_seed, true) ==
           seeded_layout);
    seeded_inputs.registers.back().value = 0x0901;
    assert(ComputePreviewCapturedPipelineDigest(
               *BuildPreviewCapturedPipeline(seeded_inputs,
                                             PreviewBackend::OpenGL, 5, &error),
               true) != seeded_layout);
    seeded_inputs.registers.push_back({ "host.polygon_offset", 1 });
    assert(!BuildPreviewCapturedPipeline(seeded_inputs, PreviewBackend::OpenGL,
                                         5, &error));
    OwnedDrawInputs vk_raster_inputs;
    vk_raster_inputs.registers = { { "capture.vk.pipeline_abi", 1 } };
    OwnedDrawBlob ms;
    ms.name = "vk.pipeline.multisample";
    ms.bytes.resize(48);
    ms.bytes[0] = 24;
    ms.bytes[20] = 1;
    vk_raster_inputs.blobs.push_back(ms);
    PreviewCapturedRaster decoded;
    assert(DecodePreviewCapturedRaster(vk_raster_inputs, PreviewBackend::Vulkan,
                                       &decoded, &error));
    assert(decoded.available == PreviewRasterCoverage);
    vk_raster_inputs.blobs[0].bytes[20] = 2;
    assert(!DecodePreviewCapturedRaster(
        vk_raster_inputs, PreviewBackend::Vulkan, &decoded, &error));
    vk_raster_inputs.blobs[0].bytes[20] = 1;
    vk_raster_inputs.blobs[0].bytes[8] = 1;
    assert(!DecodePreviewCapturedRaster(
        vk_raster_inputs, PreviewBackend::Vulkan, &decoded, &error));
    vk_raster_inputs.blobs.clear();
    vk_raster_inputs.registers.push_back(
        { "capture.vk.dynamic_line_width", 0 });
    vk_raster_inputs.registers.push_back(
        { "capture.vk.dynamic_blend_constant_mask", 0 });
    auto wire_blob = [&](const char *name, const std::vector<uint32_t> &words) {
        OwnedDrawBlob b;
        b.name = name;
        const auto *bytes = reinterpret_cast<const uint8_t *>(words.data());
        b.bytes.assign(bytes, bytes + words.size() * 4);
        vk_raster_inputs.blobs.push_back(std::move(b));
    };
    std::vector<uint32_t> raster_wire(16);
    raster_wire[0] = 23;
    raster_wire[5] = 1; // The real renderer enables Vulkan depth clamp.
    raster_wire[14] = 0x3f800000;
    wire_blob("vk.pipeline.raster", raster_wire);
    assert(DecodePreviewCapturedRaster(vk_raster_inputs, PreviewBackend::Vulkan,
                                       &decoded, &error));
    assert(decoded.depth_clamp &&
           (decoded.available & PreviewRasterDepthClamp));
    vk_raster_inputs.blobs.back().bytes[20] = 0;
    assert(DecodePreviewCapturedRaster(vk_raster_inputs, PreviewBackend::Vulkan,
                                       &decoded, &error));
    assert(!decoded.depth_clamp &&
           (decoded.available & PreviewRasterDepthClamp));
    vk_raster_inputs.blobs.back().bytes[20] = 2;
    assert(!DecodePreviewCapturedRaster(
        vk_raster_inputs, PreviewBackend::Vulkan, &decoded, &error));
    vk_raster_inputs.blobs.back().bytes[20] = 1;
    std::vector<uint32_t> blend_wire(14);
    blend_wire[0] = 26;
    blend_wire[7] = 1;
    for (size_t n = 10; n < 14; ++n)
        blend_wire[n] = 0x3f800000;
    wire_blob("vk.pipeline.blend", blend_wire);
    wire_blob("vk.pipeline.blend_attachment", { 0, 1, 0, 0, 1, 0, 0, 15 });
    wire_blob("vk.blend_constants",
              { 0x3e800000, 0x3e800000, 0x3e800000, 0x3e800000 });
    assert(DecodePreviewCapturedRaster(vk_raster_inputs, PreviewBackend::Vulkan,
                                       &decoded, &error));
    assert(decoded.blend_color[0] == 1);
    vk_raster_inputs.registers.back().value = UINT32_MAX;
    assert(DecodePreviewCapturedRaster(vk_raster_inputs, PreviewBackend::Vulkan,
                                       &decoded, &error));
    assert(decoded.blend_color[0] == .25f);
    auto missing_blend_destination =
        std::make_shared<PreviewCapturedPipeline>(*raw_pipeline);
    missing_blend_destination->raster.blend_enabled = true;
    assert(
        !ValidatePreviewCapturedPipeline(*missing_blend_destination, &error));
    raw_inputs.sources[3] += " edited";
    assert(ComputePreviewCapturedPipelineDigest(
               *BuildPreviewCapturedPipeline(raw_inputs, PreviewBackend::OpenGL,
                                             5, &error),
               true) != native_draw.pipeline_layout_digest);
    // Preserve the actual host strip/fan/adjacency command and its raw
    // indices. Diagnostic triangulation belongs to the geometry adapter.
    auto topology_inputs = raw_inputs;
    topology_inputs.sources[3].clear();
    topology_inputs.registers = {
        { "capture.vertices.first", 0 },
        { "capture.vertices.count", 4 },
        { "capture.vertex.0.enabled", 1 },
        { "capture.vk.pipeline_abi", 1 },
    };
    topology_inputs.blobs[0].format = 109;
    topology_inputs.blobs[0].count = 4;
    topology_inputs.blobs[0].bytes.resize(64);
    topology_inputs.blobs[1].name = "vertex.indices";
    topology_inputs.blobs[1].count = 4;
    topology_inputs.blobs[1].bytes = { 0, 0, 0, 0, 1, 0, 0, 0,
                                       2, 0, 0, 0, 3, 0, 0, 0 };
    std::vector<uint32_t> assembly_words(8);
    assembly_words[0] = 20;
    assembly_words[5] = 4; // Actual Vk triangle strip, four raw indices.
    OwnedDrawBlob assembly_blob;
    assembly_blob.name = "vk.pipeline.assembly";
    assembly_blob.bytes.resize(32);
    std::memcpy(assembly_blob.bytes.data(), assembly_words.data(), 32);
    topology_inputs.blobs.push_back(assembly_blob);
    auto strip_pipeline = BuildPreviewCapturedPipeline(
        topology_inputs, PreviewBackend::Vulkan, 6, &error);
    assert(strip_pipeline &&
           strip_pipeline->indices == std::vector<uint32_t>({ 0, 1, 2, 3 }));
    assert(strip_pipeline->guest_primitive_mode == 6 &&
           strip_pipeline->host_topology == 4 &&
           strip_pipeline->host_topology_captured &&
           !strip_pipeline->primitive_restart);
    auto fan_inputs = topology_inputs;
    fan_inputs.blobs.back().bytes[20] = 5;
    auto fan_pipeline = BuildPreviewCapturedPipeline(
        fan_inputs, PreviewBackend::Vulkan, 7, &error);
    assert(fan_pipeline && fan_pipeline->indices == strip_pipeline->indices);
    assert(ComputePreviewCapturedPipelineDigest(*strip_pipeline) !=
           ComputePreviewCapturedPipelineDigest(*fan_pipeline));
    assert(ComputePreviewCapturedPipelineDigest(*strip_pipeline, true) !=
           ComputePreviewCapturedPipelineDigest(*fan_pipeline, true));
    assert(DecodePreviewCapturedRaster(topology_inputs, PreviewBackend::Vulkan,
                                       &decoded, &error));
    for (const auto mode_host :
         { std::array<uint32_t, 2>{ 7, 5 }, { 8, 6 }, { 9, 7 }, { 10, 5 } }) {
        auto mode_inputs = topology_inputs;
        mode_inputs.blobs.back().bytes[20] = mode_host[1];
        if (mode_host[0] == 8 || mode_host[0] == 9)
            mode_inputs.sources[3] =
                "#version 450\nlayout(lines_adjacency) in;\n"
                "layout(triangle_strip,max_vertices=6) out;\n"
                "void main(){}";
        assert(BuildPreviewCapturedPipeline(mode_inputs, PreviewBackend::Vulkan,
                                            mode_host[0], &error));
    }
    auto invalid_topology = topology_inputs;
    invalid_topology.blobs.back().bytes[24] = 1; // Actual restart enabled.
    assert(!BuildPreviewCapturedPipeline(invalid_topology,
                                         PreviewBackend::Vulkan, 6, &error));
    invalid_topology = topology_inputs;
    invalid_topology.blobs.back().bytes[20] = 3; // Guest strip, host list.
    assert(!BuildPreviewCapturedPipeline(invalid_topology,
                                         PreviewBackend::Vulkan, 6, &error));
    invalid_topology.blobs.back().bytes[20] = 6; // Quads need the captured GS.
    assert(!BuildPreviewCapturedPipeline(invalid_topology,
                                         PreviewBackend::Vulkan, 8, &error));
    invalid_topology.blobs.back().bytes[20] = 10; // Wrong triangle adjacency.
    assert(!BuildPreviewCapturedPipeline(invalid_topology,
                                         PreviewBackend::Vulkan, 8, &error));
    auto incomplete_strip = topology_inputs;
    incomplete_strip.blobs.pop_back();
    assert(!BuildPreviewCapturedPipeline(incomplete_strip,
                                         PreviewBackend::Vulkan, 6, &error));
    auto raw_gl_strip = raw_inputs;
    raw_gl_strip.sources[3].clear();
    raw_gl_strip.registers.push_back({ "host.primitive_mode", 5 });
    raw_gl_strip.registers.push_back({ "host.primitive_restart", 0 });
    assert(BuildPreviewCapturedPipeline(raw_gl_strip, PreviewBackend::OpenGL, 6,
                                        &error));
    auto stale_pipeline_packet = native_draw;
    stale_pipeline_packet.pipeline_digest[0] ^= 1;
    assert(!ValidatePreviewPacket(stale_pipeline_packet, &error));
    assert(!BuildPreviewCapturedPipeline(raw_inputs, PreviewBackend::OpenGL, 6,
                                         &error));
    assert(error.find("topology") != std::string::npos);
    assert(!BuildPreviewCapturedPipeline(raw_inputs, PreviewBackend::OpenGL, 5,
                                         &error, 1));
    auto overflowing_span = raw_inputs;
    overflowing_span.registers[1].value = 4096;
    assert(!BuildPreviewCapturedPipeline(overflowing_span,
                                         PreviewBackend::OpenGL, 5, &error));
    PreviewPacket changed_draw = game_draw;
    changed_draw.captured_mesh.positions[0][0] = -2;
    assert(!ValidatePreviewPacket(changed_draw, &error));
    changed_draw.mesh_digest =
        ComputeCapturedMeshDigest(changed_draw.captured_mesh);
    assert(ValidatePreviewPacket(changed_draw, &error));
    assert(BuildPreviewResultKey(changed_draw) !=
           BuildPreviewResultKey(game_draw));
    changed_draw.captured_mesh.positions[0][0] = NAN;
    changed_draw.mesh_digest =
        ComputeCapturedMeshDigest(changed_draw.captured_mesh);
    assert(!ValidatePreviewPacket(changed_draw, &error));
    changed_draw = game_draw;
    changed_draw.captured_mesh.indices[2] = 3;
    changed_draw.mesh_digest =
        ComputeCapturedMeshDigest(changed_draw.captured_mesh);
    assert(!ValidatePreviewPacket(changed_draw, &error));

    PreviewPacket nul_source = base;
    nul_source.source.assign("abc\0def", 7);
    assert(!ValidatePreviewPacket(nul_source, &error));

    PreviewPacket oversized = base;
    oversized.fixture_bytes.resize(kPreviewMaxOwnedPacketBytes + 1);
    assert(!ValidatePreviewPacket(oversized, &error));

    PreviewPacket retained = base;
    retained.fixture_bytes.reserve(kPreviewMaxOwnedPacketBytes + 1);
    assert(!ValidatePreviewPacket(retained, &error));

    assert(std::string(PreviewModeLabel(PreviewMode::Uber)) == "Uber Shader");
    assert(std::string(PreviewStateLabel(PreviewState::NeedsPreparation)) ==
           "Needs preparation");

    std::cout << "shader browser preview model tests passed\n";
    return 0;
}

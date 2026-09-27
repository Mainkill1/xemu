#include "../../ui/xui/shader-browser-preview-model.hh"

#include <cassert>
#include <cmath>
#include <iostream>
#include <memory>

using namespace xemu::shader_browser;

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
    packet.selection.shader.hash = ComputeShaderHash(
        1, Stage::Pixel, packet.recipe_format_version,
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
    packet.fixture_digest = ComputePreviewDigest(
        packet.fixture_bytes.data(), packet.fixture_bytes.size());
    return packet;
}

int main()
{
    PreviewPacket base = MakePacket();
    std::string error;
    assert(ValidatePreviewPacket(base, &error));

    PreviewCompileKey compile_a = BuildPreviewCompileKey(base);
    PreviewResultKey result_a = BuildPreviewResultKey(base);

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
    vertex.selection.shader.hash = ComputeShaderHash(
        1, Stage::Vertex, vertex.recipe_format_version,
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
    game_draw.captured_mesh.positions = {
        {-1, -1, 0, 1}, {1, -1, 0, 1}, {0, 1, 0, 1}};
    game_draw.captured_mesh.indices = {0, 1, 2};
    game_draw.mesh_digest = ComputeCapturedMeshDigest(game_draw.captured_mesh);
    assert(ValidatePreviewPacket(game_draw, &error));
    assert(BuildPreviewCompileKey(game_draw) == compile_a);
    assert(BuildPreviewResultKey(game_draw) != result_a);
    PreviewPacket changed_draw = game_draw;
    changed_draw.captured_mesh.positions[0][0] = -2;
    assert(!ValidatePreviewPacket(changed_draw, &error));
    changed_draw.mesh_digest = ComputeCapturedMeshDigest(
        changed_draw.captured_mesh);
    assert(ValidatePreviewPacket(changed_draw, &error));
    assert(BuildPreviewResultKey(changed_draw) !=
           BuildPreviewResultKey(game_draw));
    changed_draw.captured_mesh.positions[0][0] = NAN;
    changed_draw.mesh_digest = ComputeCapturedMeshDigest(
        changed_draw.captured_mesh);
    assert(!ValidatePreviewPacket(changed_draw, &error));
    changed_draw = game_draw;
    changed_draw.captured_mesh.indices[2] = 3;
    changed_draw.mesh_digest = ComputeCapturedMeshDigest(
        changed_draw.captured_mesh);
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

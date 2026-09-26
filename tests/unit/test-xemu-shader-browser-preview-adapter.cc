#include "../../ui/xui/shader-browser-preview-adapter.hh"

#include <cassert>
#include <cmath>
#include <iostream>

using namespace xemu::shader_browser;

static PreviewPacketInputs Inputs()
{
    PreviewPacketInputs inputs{};
    inputs.selection.scope.title_id = 0x4d530064;
    inputs.selection.scope.executable_fingerprint_version = 1;
    inputs.selection.scope.executable_fingerprint[0] = 0x42;
    inputs.selection.shader.stage = Stage::Pixel;
    inputs.selection.session_epoch = 7;
    inputs.selection.renderer_epoch = 9;
    inputs.selection.backend = PreviewBackend::OpenGL;
    inputs.selection.mode = PreviewMode::Normal;
    inputs.recipe.key.stage = Stage::Pixel;
    inputs.recipe.recipe_format_version = 1;
    inputs.recipe.bytes = {1, 2, 3, 4};
    inputs.recipe.key.hash = ComputeShaderHash(
        1, Stage::Pixel, inputs.recipe.recipe_format_version,
        inputs.recipe.bytes.data(), inputs.recipe.bytes.size());
    inputs.selection.shader = inputs.recipe.key;
    inputs.recipe.scopes.push_back(inputs.selection.scope);
    inputs.source = "void main() {}";
    inputs.partner_source = "partner source";
    PreviewSyntheticFixture fixture{};
    fixture.corner_colors[0] = {255, 0, 0, 255};
    fixture.texture_texels[0] = {255, 255, 255, 255};
    inputs.fixture_bytes = EncodePreviewSyntheticFixture(fixture);
    inputs.generator_abi = 1;
    inputs.interface_abi = 1;
    inputs.input_revision = 1;
    inputs.view_revision = 1;
    inputs.width = 160;
    inputs.height = 160;
    return inputs;
}

int main()
{
    PreviewPacketInputs inputs = Inputs();
    PreviewPacket packet{};
    std::string error;
    assert(BuildPreviewPacket(inputs, &packet, &error));
    assert(ValidatePreviewPacket(packet, &error));
    assert(packet.recipe == inputs.recipe.bytes);
    assert(packet.selection == inputs.selection);
    PreviewSyntheticFixture decoded{};
    assert(DecodePreviewSyntheticFixture(packet.fixture_bytes, &decoded,
                                         &error));
    assert((decoded.corner_colors[0] ==
            std::array<uint8_t, 4>{255, 0, 0, 255}));
    assert((decoded.texture_texels[0] ==
            std::array<uint8_t, 4>{255, 255, 255, 255}));
    auto animated_fixture = decoded;
    AnimatePreviewSyntheticFixture(&animated_fixture, 2.0);
    assert(animated_fixture.uv_offset[0] == 0.25f);
    assert(animated_fixture.corner_colors[0][0] < decoded.corner_colors[0][0]);
    auto repeated_fixture = decoded;
    AnimatePreviewSyntheticFixture(&repeated_fixture, 10.0);
    assert(EncodePreviewSyntheticFixture(animated_fixture) ==
           EncodePreviewSyntheticFixture(repeated_fixture));
    const PreviewCompileKey compile = BuildPreviewCompileKey(packet);
    const PreviewResultKey result = BuildPreviewResultKey(packet);

    inputs.source += " // edited";
    inputs.partner_source += " // edited";
    inputs.fixture_bytes[0] ^= 17;
    assert(packet.source == "void main() {}");
    assert(packet.partner_source == "partner source");
    assert(packet.fixture_bytes.size() == kPreviewSyntheticFixtureBytes);
    PreviewPacket changed{};
    assert(BuildPreviewPacket(inputs, &changed, &error));
    assert(BuildPreviewCompileKey(changed) != compile);
    assert(BuildPreviewResultKey(changed) != result);

    PreviewPacketInputs wrong_recipe = Inputs();
    wrong_recipe.recipe.bytes[0] ^= 1;
    assert(!BuildPreviewPacket(wrong_recipe, &packet, &error));

    PreviewPacketInputs wrong_scope = Inputs();
    wrong_scope.selection.scope.title_id++;
    assert(!BuildPreviewPacket(wrong_scope, &packet, &error));

    PreviewPacketInputs replacement = Inputs();
    replacement.selection.mode = PreviewMode::Replacement;
    assert(!BuildPreviewPacket(replacement, &packet, &error));
    replacement.replacement_id = 77;
    replacement.replacement_revision = 1;
    assert(BuildPreviewPacket(replacement, &packet, &error));
    assert(packet.replacement_id == 77);
    assert(packet.replacement_revision == 1);

    PreviewPacketInputs no_fixture = Inputs();
    no_fixture.fixture_bytes.clear();
    assert(!BuildPreviewPacket(no_fixture, &packet, &error));

    PreviewPacketInputs malformed_fixture = Inputs();
    malformed_fixture.fixture_bytes.pop_back();
    assert(!BuildPreviewPacket(malformed_fixture, &packet, &error));
    PreviewSyntheticFixture nonfinite{};
    nonfinite.uv_scale[0] = NAN;
    malformed_fixture.fixture_bytes = EncodePreviewSyntheticFixture(nonfinite);
    assert(!BuildPreviewPacket(malformed_fixture, &packet, &error));

    PreviewPacketInputs retained = Inputs();
    retained.fixture_bytes.reserve(kPreviewMaxOwnedPacketBytes + 1);
    assert(!BuildPreviewPacket(retained, &packet, &error));

    DetailSnapshot detail{};
    detail.request.key = inputs.selection.shader;
    detail.state = DetailState::Complete;
    HostSource gl{};
    gl.backend = DetailBackend::OpenGL;
    gl.stage = HostSourceStage::Fragment;
    gl.kind = HostSourceKind::Glsl;
    gl.route = Route::Specialized;
    gl.text = "#version 400\nflat in vec4 vtxD0;\n";
    detail.sources.push_back(gl);
    std::string selected_source;
    assert(CopyPreviewFragmentSource(inputs.selection, detail,
                                     &selected_source, &error));
    assert(selected_source == gl.text);
    std::string partner = BuildPreviewSyntheticVertexSource(
        selected_source, PreviewBackend::OpenGL);
    assert(partner.find("flat out vec4 vtxD0;") != std::string::npos);
    assert(partner.find("layout(location = 0) in vec4 previewPosition;") !=
           std::string::npos);
    PreviewSelection uber = inputs.selection;
    uber.mode = PreviewMode::Uber;
    assert(!CopyPreviewFragmentSource(uber, detail,
                                      &selected_source, &error));
    assert(selected_source.empty());
    HostSource vk = gl;
    vk.backend = DetailBackend::Vulkan;
    vk.route = Route::Uber;
    vk.text = "#version 450\nlayout(location = 0) in vec4 vtxD0;\n";
    detail.sources.push_back(vk);
    uber.backend = PreviewBackend::Vulkan;
    assert(CopyPreviewFragmentSource(uber, detail,
                                     &selected_source, &error));
    assert(selected_source == vk.text);
    partner = BuildPreviewSyntheticVertexSource(selected_source,
                                                 PreviewBackend::Vulkan);
    assert(partner.find("layout(location = 0) out vec4 vtxD0;") !=
           std::string::npos);
    detail.request.key.hash.bytes[0] ^= 1;
    assert(!CopyPreviewFragmentSource(uber, detail,
                                      &selected_source, &error));

    std::cout << "shader browser preview adapter tests passed\n";
    return 0;
}

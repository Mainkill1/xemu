#include "../../ui/xui/shader-browser-preview-adapter.hh"

#include <cassert>
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
    inputs.fixture_bytes = {0, 0, 0, 255, 255, 255, 255, 255};
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
    const PreviewCompileKey compile = BuildPreviewCompileKey(packet);
    const PreviewResultKey result = BuildPreviewResultKey(packet);

    inputs.source += " // edited";
    inputs.partner_source += " // edited";
    inputs.fixture_bytes.push_back(17);
    assert(packet.source == "void main() {}");
    assert(packet.partner_source == "partner source");
    assert(packet.fixture_bytes.size() == 8);
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

    PreviewPacketInputs retained = Inputs();
    retained.fixture_bytes.reserve(kPreviewMaxOwnedPacketBytes + 1);
    assert(!BuildPreviewPacket(retained, &packet, &error));

    std::cout << "shader browser preview adapter tests passed\n";
    return 0;
}

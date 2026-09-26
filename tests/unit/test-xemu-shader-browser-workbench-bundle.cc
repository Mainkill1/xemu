#include "../../ui/xui/shader-browser-workbench-bundle.hh"
#include "../../ui/xui/shader-browser-preview-adapter.hh"

#include <cassert>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <limits>
#include <thread>

using namespace xemu::shader_browser;

int main()
{
    WorkbenchExperiment input{};
    input.recipe.key.stage = Stage::Pixel;
    input.recipe.recipe_format_version = 1;
    input.recipe.bytes.resize(312);
    input.recipe.bytes[0] = 'N';
    input.recipe.bytes[1] = 'V';
    input.recipe.bytes[2] = '2';
    input.recipe.bytes[3] = 'A';
    input.recipe.bytes[4] = 2;
    input.recipe.key.hash = ComputeShaderHash(1, Stage::Pixel, 1,
        input.recipe.bytes.data(), input.recipe.bytes.size());
    ShaderScope scope{};
    scope.title_id = 0x12345678;
    scope.executable_fingerprint_version = 1;
    scope.executable_fingerprint[0] = 0x42;
    input.recipe.scopes.push_back(scope);
    input.packet.selection.scope = scope;
    input.packet.selection.shader = input.recipe.key;
    input.packet.selection.backend = PreviewBackend::OpenGL;
    input.packet.source_route = Route::Specialized;
    input.packet.source_resident = true;
    input.packet.recipe_format_version = 1;
    input.packet.recipe = input.recipe.bytes;
    input.packet.generator_abi = 1;
    input.packet.interface_abi = 1;
    input.packet.source = "#version 330\nvoid main() {}\n";
    input.packet.source_digest = ComputePreviewDigest(
        reinterpret_cast<const uint8_t *>(input.packet.source.data()),
        input.packet.source.size());
    input.packet.partner_source = "#version 330\nvoid main() {}\n";
    input.packet.partner_digest = ComputePreviewDigest(
        reinterpret_cast<const uint8_t *>(input.packet.partner_source.data()),
        input.packet.partner_source.size());
    input.packet.fixture_bytes = EncodePreviewSyntheticFixture(
        MakePreviewFixture(PreviewFixtureProfile::Flat));
    input.packet.fixture_digest = ComputePreviewDigest(
        input.packet.fixture_bytes.data(), input.packet.fixture_bytes.size());
    input.packet.scene.distance = 3.125f;
    input.packet.scene.references[0].translation[0] = 0.125f;
    input.packet.render_state.alpha_test = true;
    input.packet.render_state.clear_color[0] = 0.12345679f;
    input.packet.binding_count = 1;
    input.packet.bindings[0].target = PreviewInputTarget::D0Alpha;
    input.packet.bindings[0].enabled = true;
    input.packet.bindings[0].base = -0.0f;
    input.packet.bindings[0].amplitude = 0.125f;
    input.packet.bindings[0].period_seconds = 3.25f;
    input.original.key = input.recipe.key;
    input.original.scope = scope;
    input.original.backend = PreviewBackend::OpenGL;
    input.original.stage = HostSourceStage::Fragment;
    input.original.route = Route::Specialized;
    input.original.resident = true;
    input.original.generator_abi = 1;
    input.original.interface_abi = 1;
    input.original.text = input.packet.source;
    input.original.digest = input.packet.source_digest;
    const auto generated_t0 = GeneratePreviewTexture(
        MakePreviewFixture(PreviewFixtureProfile::Flat), 0);
    input.owned_textures[0].assign(generated_t0.begin(), generated_t0.end());
    input.clock.loop = false;
    input.clock.time_seconds = 1.0e9;
    input.dependencies.push_back({"T0", WorkbenchDependencyStatus::Synthetic,
                                  "Generated RGBA8 fixture"});
    std::string bytes, error;
    assert(SerializeWorkbenchExperiment(input, &bytes, &error));
    assert(bytes.find("xemu.shader-workbench-test.v1") != std::string::npos);
    assert(bytes.find("Synthetic inputs") != std::string::npos);
    const char *sample_root =
        std::getenv("XEMU_SHADER_WORKBENCH_SAMPLE_ROOT");
    if (sample_root && *sample_root) {
        auto sample = input;
        sample.packet.render_state.alpha_test = false;
        sample.packet.scene.mesh = PreviewMesh::Sphere;
        sample.clock.time_seconds = 1.25;
        auto write_sample = [&](PreviewBackend backend,
                                const std::string &original,
                                const std::string &edited) {
            sample.packet.selection.backend = backend;
            sample.original.backend = backend;
            sample.packet.source = original;
            sample.packet.source_digest = ComputePreviewDigest(
                reinterpret_cast<const uint8_t *>(original.data()),
                original.size());
            sample.packet.partner_source =
                BuildPreviewSyntheticVertexSource(original, backend);
            sample.packet.partner_digest = ComputePreviewDigest(
                reinterpret_cast<const uint8_t *>(
                    sample.packet.partner_source.data()),
                sample.packet.partner_source.size());
            sample.original.text = original;
            sample.original.digest = sample.packet.source_digest;
            sample.edited_text = edited;
            sample.edit_revision = 2;
            FrozenDraftCompile compiled{};
            compiled.draft_id = 7;
            compiled.revision = 2;
            compiled.submission_id = 9;
            compiled.source = edited;
            compiled.digest = ComputePreviewDigest(
                reinterpret_cast<const uint8_t *>(edited.data()),
                edited.size());
            sample.successful_compile = std::move(compiled);
            std::filesystem::path output;
            assert(ExportWorkbenchExperiment(
                sample, std::filesystem::u8path(sample_root),
                &output, &error));
        };
        write_sample(PreviewBackend::OpenGL,
            "#version 400\nin vec4 vtxD0;\nout vec4 color;\n"
            "void main(){color=vec4(vtxD0.rgb,1.0);}\n",
            "#version 400\nin vec4 vtxD0;\nout vec4 color;\n"
            "void main(){color=vec4(vtxD0.r,0.8,0.2,1.0);}\n");
        write_sample(PreviewBackend::Vulkan,
            "#version 450\nlayout(location=0) in vec4 vtxD0;\n"
            "layout(location=0) out vec4 color;\n"
            "void main(){color=vec4(vtxD0.rgb,1.0);}\n",
            "#version 450\nlayout(location=0) in vec4 vtxD0;\n"
            "layout(location=0) out vec4 color;\n"
            "void main(){color=vec4(vtxD0.r,0.8,0.2,1.0);}\n");
    }
    WorkbenchExperiment reopened{};
    assert(ParseWorkbenchExperiment(bytes, &reopened, &error));
    assert(reopened.recipe.key == input.recipe.key);
    assert(reopened.packet.selection.scope == scope);
    assert(reopened.packet.source == input.packet.source);
    assert(reopened.packet.partner_source == input.packet.partner_source);
    assert(reopened.packet.fixture_bytes == input.packet.fixture_bytes);
    assert(reopened.packet.scene == input.packet.scene);
    assert(reopened.packet.render_state == input.packet.render_state);
    assert(reopened.packet.bindings[0] == input.packet.bindings[0]);
    assert(std::signbit(reopened.packet.bindings[0].base));
    assert(reopened.owned_textures[0] == input.owned_textures[0]);
    WorkbenchExperiment wrong_texture = input;
    wrong_texture.owned_textures[0][0] ^= 1;
    assert(!SerializeWorkbenchExperiment(wrong_texture, &bytes, &error));
    wrong_texture = input;
    wrong_texture.dependencies[0].status =
        WorkbenchDependencyStatus::Supplied;
    assert(!SerializeWorkbenchExperiment(wrong_texture, &bytes, &error));
    wrong_texture.dependencies.clear();
    assert(!SerializeWorkbenchExperiment(wrong_texture, &bytes, &error));
    assert(reopened.clock.time_seconds == input.clock.time_seconds);
    WorkbenchExperiment edited = input;
    edited.edited_text = "#version 330\nvoid main() { /* edit */ }\n";
    edited.edit_revision = 2;
    FrozenDraftCompile token{};
    token.draft_id = 7;
    token.revision = 2;
    token.submission_id = 9;
    token.source = edited.edited_text;
    token.digest = ComputePreviewDigest(
        reinterpret_cast<const uint8_t *>(token.source.data()),
        token.source.size());
    edited.successful_compile = token;
    edited.packet.source_variant = PreviewSourceVariant::Edited;
    edited.packet.draft_id = token.draft_id;
    edited.packet.draft_revision = token.revision;
    edited.packet.draft_submission_id = token.submission_id;
    edited.packet.source_resident = false;
    edited.packet.source = token.source;
    edited.packet.source_digest = token.digest;
    assert(SerializeWorkbenchExperiment(edited, &bytes, &error));
    assert(ParseWorkbenchExperiment(bytes, &reopened, &error));
    assert(reopened.successful_compile &&
           reopened.successful_compile->source == token.source);
    assert(reopened.successful_compile->submission_id == token.submission_id);
    assert(reopened.packet.draft_submission_id == token.submission_id);
    assert(reopened.original.resident && !reopened.packet.source_resident);
    assert(reopened.packet.source_variant == PreviewSourceVariant::Edited);
    assert(reopened.packet.scene == edited.packet.scene);
    assert(reopened.packet.render_state == edited.packet.render_state);
    assert(SerializeWorkbenchExperiment(input, &bytes, &error));
    const char *test_root = std::getenv("XEMU_SHADER_WORKBENCH_TEST_ROOT");
    if (test_root && *test_root) {
        const auto base = std::filesystem::u8path(test_root) /
                          "workbench-bundle-test";
        std::filesystem::remove_all(base);
        std::filesystem::path first, second;
        assert(ExportWorkbenchExperiment(input, base, &first, &error));
        assert(ExportWorkbenchExperiment(input, base, &second, &error));
        assert(first != second);
        std::filesystem::path parallel_a, parallel_b;
        std::string error_a, error_b;
        bool ok_a = false, ok_b = false;
        std::thread a([&] {
            ok_a = ExportWorkbenchExperiment(input, base, &parallel_a,
                                              &error_a);
        });
        std::thread b([&] {
            ok_b = ExportWorkbenchExperiment(input, base, &parallel_b,
                                              &error_b);
        });
        a.join();
        b.join();
        assert(ok_a && ok_b && parallel_a != parallel_b);
        assert(first.parent_path() == base / "shader-exports");
        assert(ReadWorkbenchExperiment(first, &reopened, &error));
        assert(reopened.packet.source == input.packet.source);
        {
            std::ofstream growing(first, std::ios::binary | std::ios::app);
            growing.put('x');
        }
        assert(!ReadWorkbenchExperiment(first, &reopened, &error));
        std::filesystem::remove_all(base);
    }
    std::string deep(64, '[');
    deep += '0';
    deep.append(64, ']');
    assert(!ParseWorkbenchExperiment(deep, &reopened, &error));
    assert(error.find("structure") != std::string::npos);
    std::string many = "[";
    for (int i = 0; i < 5000; ++i) many += "0,";
    many += "0]";
    assert(!ParseWorkbenchExperiment(many, &reopened, &error));
    assert(error.find("structure") != std::string::npos);
    std::string wrong = bytes;
    wrong.replace(wrong.find("xemu.shader-workbench-test.v1"),
                  sizeof("xemu.shader-workbench-test.v1") - 1,
                  "xemu.shader-workbench-test.v2");
    assert(!ParseWorkbenchExperiment(wrong, &reopened, &error));
    wrong = bytes;
    wrong.replace(wrong.find("#version 330"), 12, "#version 440");
    assert(!ParseWorkbenchExperiment(wrong, &reopened, &error));
    input.packet.scene.distance = std::numeric_limits<float>::infinity();
    assert(!SerializeWorkbenchExperiment(input, &bytes, &error));
    input.packet.scene.distance = 3.125f;
    input.packet.partner_source.clear();
    input.packet.partner_digest = {};
    assert(!SerializeWorkbenchExperiment(input, &bytes, &error));
    input.packet.partner_source = "#version 330\nvoid main() {}\n";
    input.packet.partner_digest = ComputePreviewDigest(
        reinterpret_cast<const uint8_t *>(input.packet.partner_source.data()),
        input.packet.partner_source.size());
    input.packet.selection.scope.executable_fingerprint[0] = 0x43;
    assert(!SerializeWorkbenchExperiment(input, &bytes, &error));
    input.packet.selection.scope = scope;
    input.packet.interface_abi = 2;
    assert(!SerializeWorkbenchExperiment(input, &bytes, &error));
    input.packet.interface_abi = 1;
    input.packet.selection.backend = PreviewBackend::Vulkan;
    assert(!SerializeWorkbenchExperiment(input, &bytes, &error));
    input.packet.selection.backend = PreviewBackend::OpenGL;
    input.dependencies[0].name = "../T0";
    assert(!SerializeWorkbenchExperiment(input, &bytes, &error));
    input.dependencies[0].name = "T0";
    input.packet.source_digest[0] ^= 1;
    assert(!SerializeWorkbenchExperiment(input, &bytes, &error));
    return 0;
}

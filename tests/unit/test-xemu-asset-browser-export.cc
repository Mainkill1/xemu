// SPDX-License-Identifier: GPL-2.0-or-later
#include "../../ui/xui/asset-browser-export.hh"
#include <glib.h>
#include <fstream>
#include <nlohmann/json.hpp>
#define STB_IMAGE_IMPLEMENTATION
#include "../../ui/thirdparty/stb_image/stb_image.h"
using namespace xemu::asset_browser;
static AssetCatalog Fixture(bool placed = false)
{
    capture::CaptureSession session;
    capture::CaptureSessionContext context;
    context.scope.title_id = 17;
    context.scope_generation = 1;
    context.session_epoch = 2;
    context.renderer_epoch = 3;
    context.generation = 4;
    context.backend = 2;
    g_assert_true(session.Start(context));
    session.GuestFrameBoundary(1);
    for (uint32_t draw = 0; draw < 32; ++draw) {
        capture::DrawCaptureSummary summary;
        summary.scope = context.scope;
        summary.key = { 2, 3, 1, draw, draw + 1 };
        summary.shader_count = 1;
        summary.shaders[0].stage = capture::Stage::Pixel;
        summary.shaders[0].hash.version = 1;
        summary.primitive_mode = 5;
        auto token = session.BeginOccurrence(summary);
        float constants[] = { float(draw) + .25f, float(draw) + .5f, 1, 2 };
        XemuShaderDrawUniform uniform{};
        uniform.stage = 1;
        uniform.name = "fixture_c0";
        uniform.type = XEMU_SHADER_DRAW_UNIFORM_FLOAT;
        uniform.components = 4;
        uniform.count = 1;
        uniform.data = constants;
        uniform.byte_count = sizeof(constants);
        g_assert_true(session.StageUniform(token, uniform));
        if (placed) {
            const std::string vs =
                "#define R12 oPos\nvoid main(){"
                "DP4(oPos,x,v0,c[0]);DP4(oPos,y,v0,c[1]);"
                "DP4(oPos,z,v0,c[2]);DP4(oPos,w,v0,c[3]);"
                "RCC(R1,x,R12.w);MUL(oPos,xyz,R12.xyz,c[4].xyz);"
                "MAD(oPos,xyz,R12.xyz,R1.x,c[5].xyz);"
                "oPos.xy=roundScreenCoords(oPos.xy);oPos.w=clampAwayZeroInf("
                "oPos.w);"
                "vec4 "
                "vtxPos=oPos;oPos.xy=(2.0f*oPos.xy-surfaceSize)/surfaceSize;"
                "oPos.z=oPos.z/clipRange.y;oPos.xyz*=oPos.w;gl_Position=oPos;}";
            g_assert_true(session.StageSource(token, 1, vs.data(), vs.size()));
            std::array<float, 24> c{};
            for (size_t i = 0; i < 4; ++i)
                c[i * 4 + i] = 1;
            c[3] = float(draw);
            c[16] = 320;
            c[17] = 240;
            c[18] = 65535;
            c[20] = 320;
            c[21] = 240;
            uniform.name = "c";
            uniform.count = 6;
            uniform.data = c.data();
            uniform.byte_count = sizeof(c);
            g_assert_true(session.StageUniform(token, uniform));
            const float extent[] = { 640, 480 },
                        clip[] = { 0, 65535, 0, 65535 };
            uniform.name = "surfaceSize";
            uniform.count = 1;
            uniform.components = 2;
            uniform.data = extent;
            uniform.byte_count = sizeof(extent);
            g_assert_true(session.StageUniform(token, uniform));
            uniform.name = "clipRange";
            uniform.components = 4;
            uniform.data = clip;
            uniform.byte_count = sizeof(clip);
            g_assert_true(session.StageUniform(token, uniform));
        }
        float positions[] = { float(draw), 0, 0, float(draw) + 1, 0, 0,
                              float(draw), 1, 0 };
        XemuShaderDrawBlob blob{};
        blob.name = "vertex.attribute0";
        blob.data = positions;
        blob.byte_count = sizeof(positions);
        blob.format = 106;
        blob.components = 3;
        blob.stride = 12;
        blob.count = 3;
        g_assert_true(session.StageBlob(token, blob));
        float uv[] = { 0, 0, 1, 0, 0, 1 };
        blob.name = "vertex.attribute9";
        blob.data = uv;
        blob.byte_count = sizeof(uv);
        blob.format = 103;
        blob.components = 2;
        blob.stride = 8;
        g_assert_true(session.StageBlob(token, blob));
        g_assert_true(
            session.StageRegister(token, "capture.vertices.first", 0));
        g_assert_true(
            session.StageRegister(token, "capture.vertices.count", 3));
        uint8_t rgba[] = { uint8_t(draw), 255, 0, 255, 0, 0, 255, 255 };
        XemuShaderDrawTexture texture{};
        texture.bound = 1;
        texture.width = 2;
        texture.height = texture.depth = texture.face_count =
            texture.mip_levels = 1;
        texture.image = { 2, 1, rgba, sizeof(rgba) };
        g_assert_true(session.StageTexture(token, texture));
        g_assert_true(session.InputsComplete(token));
        g_assert_true(session.Finish(token, true, 5, 3, 0));
    }
    session.GuestFrameBoundary(2);
    return BuildAssetCatalog(session.Snapshot());
}
static void TestRoundTrip()
{
    auto catalog = Fixture();
    g_assert_cmpuint(catalog.parts.size(), ==, 32);
    auto selected = MakeAssetAssembly(
        catalog, { catalog.parts[0]->id, catalog.parts[10]->id }, "My car");
    auto path =
        std::filesystem::temp_directory_path() / "xemu-asset-browser-roundtrip";
    std::filesystem::remove_all(path);
    std::string error;
    g_assert_true(SaveAssetRecording(catalog, &selected, path, &error));
    AssetCatalog reopened;
    std::shared_ptr<const AssetAssembly> restored;
    g_assert_true(ReopenAssetRecording(path, &reopened, &restored, &error));
    g_assert_cmpuint(reopened.parts.size(), ==, 32);
    g_assert_nonnull(restored.get());
    g_assert_cmpstr(restored->label.c_str(), ==, "My car");
    g_assert_cmpuint(restored->parts.size(), ==, 2);
    for (size_t i = 0; i < 32; ++i) {
        const auto &words = reopened.parts[i]->occurrence->inputs.uniforms;
        g_assert_cmpuint(words.size(), ==, 1);
        float constant;
        std::memcpy(&constant, words[0].data->bytes.data(), 4);
        g_assert_cmpfloat(constant, ==, float(i) + .25f);
        g_assert_cmpfloat(reopened.parts[i]->vertices[0].position[0], ==, i);
        g_assert_cmpuint(reopened.parts[i]
                             ->occurrence->inputs.textures[0]
                             .images[0]
                             .image.rgba->bytes[0],
                         ==, i);
    }
    auto subset_path = path.string() + "-subset";
    std::filesystem::remove_all(subset_path);
    g_assert_true(SaveAssetAssembly(selected, subset_path, &error));
    AssetAssembly subset;
    g_assert_true(ReopenAssetAssembly(subset_path, &subset, &error));
    g_assert_cmpuint(subset.parts.size(), ==, 2);
    g_assert_false(subset.complete);
    std::filesystem::remove_all(subset_path);
    capture::CaptureFileControl cancelled;
    g_assert_true(cancelled.RequestCancel());
    auto cancelled_path = path.string() + "-cancelled";
    std::filesystem::remove_all(cancelled_path);
    g_assert_false(SaveAssetRecording(catalog, &selected, cancelled_path,
                                      &error, &cancelled));
    g_assert_false(std::filesystem::exists(cancelled_path));
    auto invalid_recording = *catalog.recording;
    invalid_recording.annotations = "{}";
    auto invalid_catalog = catalog;
    invalid_catalog.recording =
        std::make_shared<const capture::CaptureSessionSnapshot>(
            invalid_recording);
    capture::CaptureFileControl invalid_control;
    g_assert_false(SaveAssetRecording(invalid_catalog, nullptr, cancelled_path,
                                      &error, &invalid_control));
    g_assert_true(invalid_control.Progress().finished);
    g_assert_false(invalid_control.Progress().success);
    auto invalid_assembly = selected;
    invalid_assembly.parts.push_back(nullptr);
    capture::CaptureFileControl assembly_control;
    g_assert_false(SaveAssetAssembly(invalid_assembly, cancelled_path, &error,
                                     &assembly_control));
    g_assert_true(assembly_control.Progress().finished);
    nlohmann::json metadata;
    std::ifstream(path / "metadata.json") >> metadata;
    auto annotations =
        nlohmann::json::parse(metadata["annotations"].get<std::string>());
    annotations["asset_browser"]["parts"][0] = UINT64_C(999999);
    metadata["annotations"] = annotations.dump();
    std::ofstream(path / "metadata.json") << metadata.dump();
    g_assert_false(ReopenAssetRecording(path, &reopened, &restored, &error));
    g_assert_true(reopened.parts.empty());
    g_assert_null(restored.get());
    std::filesystem::remove_all(path);
}
static void TestAnchorRoundTrip()
{
    auto catalog = Fixture(true);
    const auto anchor = catalog.parts[10]->id;
    auto selected = MakeAssetAssembly(catalog, { anchor, catalog.parts[0]->id },
                                      "Body anchor");
    g_assert_true(selected.captured_placement);
    g_assert_cmpuint(selected.id, ==, anchor);
    g_assert_cmpuint(selected.parts.front()->id, !=, anchor);
    auto path =
        std::filesystem::temp_directory_path() / "xemu-asset-anchor-roundtrip";
    std::filesystem::remove_all(path);
    std::string error;
    g_assert_true(SaveAssetAssembly(selected, path, &error));
    AssetAssembly restored;
    g_assert_true(ReopenAssetAssembly(path, &restored, &error));
    g_assert_cmpuint(restored.id, ==, anchor);
    g_assert_true(restored.captured_placement);
    g_assert_true(restored.local_from_captured_clip ==
                  selected.local_from_captured_clip);
    g_assert_true(restored.anchor_from_local == selected.anchor_from_local);
    nlohmann::json metadata;
    std::ifstream(path / "metadata.json") >> metadata;
    auto annotations =
        nlohmann::json::parse(metadata["annotations"].get<std::string>());
    annotations["asset_browser"]["anchor"] = UINT64_C(999999);
    metadata["annotations"] = annotations.dump();
    std::ofstream(path / "metadata.json") << metadata.dump();
    g_assert_false(ReopenAssetAssembly(path, &restored, &error));
    // Legacy packages have no explicit anchor; retain their original semantics.
    annotations["asset_browser"]["version"] = 1;
    annotations["asset_browser"].erase("anchor");
    metadata["annotations"] = annotations.dump();
    std::ofstream(path / "metadata.json") << metadata.dump();
    g_assert_true(ReopenAssetAssembly(path, &restored, &error));
    g_assert_cmpuint(restored.id, ==, selected.parts.front()->id);
    std::filesystem::remove_all(path);
}
static uint32_t U32(const std::vector<uint8_t> &bytes, size_t offset)
{
    return uint32_t(bytes[offset]) | uint32_t(bytes[offset + 1]) << 8 |
           uint32_t(bytes[offset + 2]) << 16 |
           uint32_t(bytes[offset + 3]) << 24;
}
static void TestGlb()
{
    auto catalog = Fixture();
    auto selected = MakeAssetAssembly(
        catalog, { catalog.parts[0]->id, catalog.parts[10]->id }, "My car");
    auto path = std::filesystem::temp_directory_path() /
                "xemu-asset-browser-fixture.glb";
    std::filesystem::remove(path);
    selected.captured_placement = true;
    AssetMatrix identity{ 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
    auto relative = identity;
    relative[3] = -10;
    selected.anchor_from_local = { identity, relative };
    std::string error;
    g_assert_true(ExportAssetGlb(selected, path, &error));
    std::ifstream file(path, std::ios::binary);
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(file)), {});
    g_assert_cmphex(U32(bytes, 0), ==, 0x46546c67);
    g_assert_cmpuint(U32(bytes, 4), ==, 2);
    g_assert_cmpuint(U32(bytes, 8), ==, bytes.size());
    auto length = U32(bytes, 12);
    g_assert_cmpuint(length % 4, ==, 0);
    g_assert_cmphex(U32(bytes, 16), ==, 0x4e4f534a);
    auto doc =
        nlohmann::json::parse(bytes.begin() + 20, bytes.begin() + 20 + length);
    g_assert_cmpfloat(doc["nodes"][2]["matrix"][12].get<float>(), ==, -10);
    size_t bin = 28 + length;
    g_assert_cmpuint(doc["meshes"].size(), ==, 2);
    g_assert_cmpuint(doc["images"].size(), ==, 2);
    const auto &accessor =
        doc["accessors"]
           [doc["meshes"][1]["primitives"][0]["attributes"]["POSITION"]
                .get<size_t>()];
    g_assert_cmpfloat(accessor["min"][0].get<float>(), ==, 10);
    for (const auto &view : doc["bufferViews"])
        g_assert_cmpuint(view["byteOffset"].get<size_t>() +
                             view["byteLength"].get<size_t>(),
                         <=, bytes.size() - bin);
    const auto &image =
        doc["bufferViews"][doc["images"][0]["bufferView"].get<size_t>()];
    int w, h, channels;
    auto *png = stbi_load_from_memory(
        bytes.data() + bin + image["byteOffset"].get<size_t>(),
        image["byteLength"].get<int>(), &w, &h, &channels, 4);
    g_assert_nonnull(png);
    g_assert_cmpint(w, ==, 2);
    g_assert_cmpuint(png[1], ==, 255);
    stbi_image_free(png);
    g_assert_false(ExportAssetGlb(selected, path, &error));
    capture::CaptureFileControl cancel;
    cancel.RequestCancel();
    auto cancelled = path.string() + "-cancelled";
    std::filesystem::remove(cancelled);
    g_assert_false(ExportAssetGlb(selected, cancelled, &error, &cancel));
    g_assert_false(std::filesystem::exists(cancelled));
    // Retain the small fixture for an independent external glTF importer.
}
int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, nullptr);
    g_test_add_func("/asset/export/roundtrip", TestRoundTrip);
    g_test_add_func("/asset/export/glb", TestGlb);
    g_test_add_func("/asset/export/anchor-roundtrip", TestAnchorRoundTrip);
    return g_test_run();
}

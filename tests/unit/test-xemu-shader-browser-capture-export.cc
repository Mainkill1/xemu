// SPDX-License-Identifier: GPL-2.0-or-later
#include "../../ui/xui/shader-browser-capture-export.hh"
#include "../../ui/thirdparty/fpng/fpng.h"

#include <nlohmann/json.hpp>

#include <cstdio>
#include <cstdlib>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <vector>

#define CHECK(...)                                                     \
    do {                                                               \
        if (!(__VA_ARGS__)) {                                          \
            std::fprintf(stderr, "CHECK failed at %d: %s\n", __LINE__, \
                         #__VA_ARGS__);                                \
            std::abort();                                              \
        }                                                              \
    } while (0)
using namespace xemu::shader_browser;
namespace fs = std::filesystem;

static std::vector<uint8_t> ReadBytes(const fs::path &path)
{
    std::ifstream stream(path, std::ios::binary);
    CHECK(stream);
    return { std::istreambuf_iterator<char>(stream),
             std::istreambuf_iterator<char>() };
}

static nlohmann::json ReadJson(const fs::path &path)
{
    std::ifstream stream(path);
    CHECK(stream);
    nlohmann::json document;
    stream >> document;
    return document;
}

static void CheckPng(const fs::path &path, const std::vector<uint8_t> &expected)
{
    const auto png = ReadBytes(path);
    std::vector<uint8_t> decoded;
    uint32_t width = 0, height = 0, channels = 0;
    CHECK(fpng::fpng_decode_memory(png.data(), png.size(), decoded, width,
                                   height, channels,
                                   4) == fpng::FPNG_DECODE_SUCCESS);
    CHECK(width == 2 && height == 1 && channels == 4);
    CHECK(decoded == expected);
}

static size_t DirectoryCount(const fs::path &parent)
{
    return std::distance(fs::directory_iterator(parent),
                         fs::directory_iterator());
}

int main()
{
    OwnedDrawInputs inputs;
    inputs.complete = true;
    inputs.before = { 2, 1, { 10, 100, 200, 0, 255, 1, 20, 200 } };
    inputs.after = { 2, 1, { 30, 50, 250, 8, 0, 4, 10, 0 } };
    const std::vector<uint8_t> changed = { 20, 50, 50, 255, 255, 3, 10, 255 };
    OwnedDrawImage difference;
    std::string error;
    CHECK(MakeCapturedDrawDifference(inputs.before, inputs.after, &difference,
                                     &error));
    CHECK(error.empty());
    CHECK(difference.width == 2 && difference.height == 1);
    CHECK(difference.rgba == changed);

    auto &texture = inputs.textures[2];
    texture.described = true;
    texture.metadata.slot = 2;
    texture.metadata.bound = true;
    texture.metadata.guest_format = 42;
    texture.metadata.host_format = 43;
    texture.metadata.width = 2;
    texture.metadata.height = 1;
    texture.metadata.depth = 1;
    texture.metadata.mip_levels = 2;
    texture.metadata.face_count = 6;
    texture.metadata.min_filter = 44;
    texture.metadata.mag_filter = 45;
    texture.metadata.wrap_s = 46;
    texture.metadata.wrap_t = 47;
    texture.metadata.wrap_r = 48;
    texture.metadata.coordinate_scale = 0.5f;
    texture.images.push_back({ 1, 5, { 2, 1, { 1, 2, 3, 4, 5, 6, 7, 8 } } });
    inputs.uniforms.push_back(
        { 2,
          "name/with\\path",
          3,
          4,
          1,
          { 1, 0, 0, 0, 2, 0, 0, 0, 3, 0, 0, 0, 4, 0, 0, 0 } });
    inputs.uniforms.push_back(
        { 1, "nan_payload", 1, 1, 1, { 0x45, 0x23, 0xc1, 0x7f } });
    inputs.registers.push_back({ "NV_PGRAPH_TEST", 0xfedcba98 });
    inputs.blobs.push_back(
        { "pgraph.raw/../constants", { 0x45, 0x23, 0xc1, 0x7f } });
    inputs.sources[1] = "#version 330\nvoid main() {}\n";
    inputs.sources[2] = "#version 330\nout vec4 color;\n";
    PreviewCapturedMesh mesh;
    mesh.positions = { { { 0, 0, 0, 1 } },
                       { { 1, 0, 0, 1 } },
                       { { 0, 1, 0, 1 } } };
    mesh.indices = { 2, 1, 0 };
    DrawCaptureSummary summary;
    summary.key = { 7, 8, 9, 10, 11 };
    summary.vertex_count = 3;
    summary.index_count = 3;
    summary.primitive_count = 1;
    summary.primitive_mode = 5;
    summary.domain = DrawDomain::Geometry;
    summary.completeness = CaptureCompleteness::OwnedSnapshot;
    ResourceTouch resource;
    resource.resource.kind = ResourceKind::Texture;
    resource.resource.guest = { 0xf0000000, 4096 };
    resource.resource.storage_id = 3;
    resource.resource.storage_range = { 64, 2048 };
    resource.resource.content_digest[0] = 0xcd;
    resource.slot = 2;
    resource.access = ResourceAccess::Read;
    resource.read_version = 0x100000001ULL;
    summary.resources.push_back(resource);
    DrawSegmentSummary segment;
    segment.key = { summary.key, 4 };
    segment.origin = DrawSegmentOrigin::ConnectedIndexComponent;
    segment.primitive_indices = { 0 };
    summary.segments.push_back(segment);
    CaptureExportIdentity identity;
    identity.shader.stage = Stage::Pixel;
    identity.shader.hash.bytes[0] = 0xab;
    identity.scope.title_id = 0x12345678;
    identity.backend = PreviewBackend::OpenGL;
    const auto nonce =
        std::chrono::steady_clock::now().time_since_epoch().count();
    const fs::path parent =
        fs::temp_directory_path() /
        ("xemu-capture-export-" + std::to_string(nonce) + "-unicode-\xc3\xa9");
    CHECK(fs::create_directory(parent));
    std::ofstream(parent / "keep.txt") << "preserve";
    fs::path first, second;
    CHECK(ExportCapturedDrawPackage(inputs, mesh, summary, identity, parent,
                                    &first, &error));
    CHECK(first.parent_path() == parent && fs::is_directory(first));
    auto manifest = ReadJson(first / "manifest.json");
    CHECK(manifest["schema"] == "xemu.captured-draw.v1");
    CHECK(manifest["identity"]["scope"]["title_id"] == 0x12345678);
    CHECK(manifest["identity"]["backend"] == "OpenGL");
    CHECK(manifest["draw"]["frame"] == 9);
    CHECK(manifest["draw"]["submission"] == 11);
    const auto &touch = manifest["draw"]["resources"][0];
    CHECK(touch["guest"]["address"] == 0xf0000000);
    CHECK(touch["guest"]["length"] == 4096);
    CHECK(touch["storage_range"]["address"] == 64);
    CHECK(touch["storage_id"] == 3 && touch["slot"] == 2);
    CHECK(touch["read_version"] == 0x100000001ULL);
    CHECK(touch["content_digest"] == "cd000000000000000000000000000000");
    CHECK(manifest["draw"]["segments"][0]["segment"] == 4);
    CHECK(manifest["draw"]["segments"][0]["primitive_indices"] ==
          nlohmann::json::array({ 0 }));
    CHECK(manifest["geometry"]["vertex_count"] == 3);
    CHECK(manifest["geometry"]["positions_format"] ==
          "float32x4-little-endian");
    const auto &tex = manifest["textures"][0];
    CHECK(tex["slot"] == 2 && tex["guest_format"] == 42);
    CHECK(tex["min_filter"] == 44 && tex["coordinate_scale"] == 0.5);
    CHECK(tex["images"][0]["mip_level"] == 1);
    CHECK(tex["images"][0]["face"] == 5);
    CHECK(ReadBytes(first / std::string(tex["images"][0]["raw"])) ==
          std::vector<uint8_t>({ 1, 2, 3, 4, 5, 6, 7, 8 }));
    CheckPng(first / std::string(tex["images"][0]["png"]),
             { 1, 2, 3, 4, 5, 6, 7, 8 });
    CHECK(ReadBytes(first / "geometry/indices.raw") ==
          std::vector<uint8_t>({ 2, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0 }));
    const auto positions = ReadBytes(first / "geometry/positions.raw");
    CHECK(positions.size() == 48 && positions[14] == 128 &&
          positions[15] == 63);
    auto uniforms = ReadJson(first / "state/uniforms.json");
    CHECK(uniforms[0]["name"] == "name/with\\path");
    CHECK(uniforms[0]["stage"] == 2 && uniforms[0]["count"] == 1);
    CHECK(ReadBytes(first / std::string(uniforms[0]["raw"])) ==
          inputs.uniforms[0].data);
    CHECK(ReadBytes(first / std::string(uniforms[1]["raw"])) ==
          std::vector<uint8_t>({ 0x45, 0x23, 0xc1, 0x7f }));
    auto registers = ReadJson(first / "state/registers.json");
    CHECK(registers[0]["value"] == 0xfedcba98);
    CHECK(ReadBytes(first / "shaders/vertex.glsl") ==
          std::vector<uint8_t>(inputs.sources[1].begin(),
                               inputs.sources[1].end()));
    CHECK(ReadBytes(first / "output/before.rgba.raw") == inputs.before.rgba);
    CHECK(ReadBytes(first / "output/after.rgba.raw") == inputs.after.rgba);
    CHECK(ReadBytes(first / "output/difference.rgba.raw") == changed);
    CHECK(ReadBytes(first / "raw/b0.raw") == inputs.blobs[0].bytes);
    CHECK(manifest["raw_inputs"][0]["name"] == inputs.blobs[0].name);
    CheckPng(first / "output/before.png", inputs.before.rgba);
    CheckPng(first / "output/after.png", inputs.after.rgba);
    CheckPng(first / "output/difference.png", changed);
    auto provenance = ReadJson(first / "provenance.json");
    CHECK(provenance["texture_guest_raw_bytes_captured"] == false);
    CHECK(provenance["texture_palette_captured"] == false);
    CHECK(provenance["original_vertex_outputs_captured"] == false);
    CHECK(provenance["full_replay"] == false);
    CHECK(provenance["engine_asset"] == false);
    CHECK(ExportCapturedDrawPackage(inputs, mesh, summary, identity, parent,
                                    &second, &error));
    CHECK(first != second && DirectoryCount(parent) == 3);

    auto invalid = inputs;
    invalid.after.rgba.pop_back();
    fs::path failure = first;
    CHECK(!ExportCapturedDrawPackage(invalid, mesh, summary, identity, parent,
                                     &failure, &error));
    CHECK(failure.empty() && !error.empty() && DirectoryCount(parent) == 3);
    CHECK(!MakeCapturedDrawDifference(inputs.before, invalid.after, &difference,
                                      &error));
    CHECK(difference.rgba.empty());
    invalid = inputs;
    invalid.before.width = 2049;
    CHECK(!ExportCapturedDrawPackage(invalid, mesh, summary, identity, parent,
                                     &failure, &error));
    auto bad_mesh = mesh;
    bad_mesh.indices[0] = 3;
    CHECK(!ExportCapturedDrawPackage(inputs, bad_mesh, summary, identity,
                                     parent, &failure, &error));
    bad_mesh = mesh;
    bad_mesh.positions[0][0] = std::numeric_limits<float>::infinity();
    CHECK(!ExportCapturedDrawPackage(inputs, bad_mesh, summary, identity,
                                     parent, &failure, &error));
    invalid = inputs;
    invalid.textures[2].images.push_back(invalid.textures[2].images[0]);
    CHECK(!ExportCapturedDrawPackage(invalid, mesh, summary, identity, parent,
                                     &failure, &error));
    invalid = inputs;
    invalid.uniforms[0].name = std::string(1, char(0xff));
    CHECK(!ExportCapturedDrawPackage(invalid, mesh, summary, identity, parent,
                                     &failure, &error));
    CHECK(failure.empty() && DirectoryCount(parent) == 3);
    invalid = inputs;
    invalid.complete = false;
    CHECK(!ExportCapturedDrawPackage(invalid, mesh, summary, identity, parent,
                                     &failure, &error));
    invalid = inputs;
    invalid.sources[1].assign(kDrawInputBudget, 'x');
    CHECK(!ExportCapturedDrawPackage(invalid, mesh, summary, identity, parent,
                                     &failure, &error));
    CHECK(DirectoryCount(parent) == 3 &&
          ReadBytes(parent / "keep.txt").size() == 8);
    auto partial = inputs;
    partial.before = {};
    partial.after = {};
    fs::path partial_path;
    CHECK(ExportCapturedDrawPackage(partial, mesh, summary, identity, parent,
                                    &partial_path, &error));
    const auto partial_manifest = ReadJson(partial_path / "manifest.json");
    CHECK(partial_manifest["output"]["after"]["status"] == "missing");
    CHECK(!fs::exists(partial_path / "output/after.png"));
    CHECK(ReadBytes(partial_path / "raw/b0.raw") == inputs.blobs[0].bytes);
    // Texture extraction works without a supported mesh or synthetic material.
    fs::path texture_path;
    CaptureFileControl texture_control;
    CHECK(ExportCapturedTexturePackage(texture, identity, summary.key, parent,
                                       &texture_path, &error,
                                       &texture_control));
    const auto texture_manifest = ReadJson(texture_path / "manifest.json");
    CHECK(texture_manifest["schema"] == "xemu.captured-texture.v1");
    CHECK(texture_manifest["identity"]["scope"]["title_id"] == 0x12345678);
    CHECK(texture_manifest["draw"]["submission"] == 11);
    CHECK(texture_manifest["texture"]["slot"] == 2);
    CHECK(texture_manifest["texture"]["guest_format"] == 42);
    CHECK(texture_manifest["texture"]["wrap_t"] == 47);
    CHECK(texture_manifest["texture"]["images"][0]["mip_level"] == 1);
    CHECK(texture_manifest["texture"]["images"][0]["face"] == 5);
    CHECK(texture_manifest["texture"]["guest_storage_captured"] == false);
    const auto &texture_image = texture_manifest["texture"]["images"][0];
    CheckPng(texture_path / std::string(texture_image["png"]),
             { 1, 2, 3, 4, 5, 6, 7, 8 });
    CHECK(ReadBytes(texture_path / std::string(texture_image["raw"])) ==
          texture.images[0].image.rgba);
    CHECK(texture_control.Progress().success);

    const auto files_before_cancel = DirectoryCount(parent);
    CaptureFileControl cancelled;
    CHECK(cancelled.RequestCancel());
    CHECK(!ExportCapturedTexturePackage(texture, identity, summary.key, parent,
                                        &failure, &error, &cancelled));
    CHECK(failure.empty() && !error.empty());
    CHECK(DirectoryCount(parent) == files_before_cancel);
    CHECK(cancelled.Progress().finished && !cancelled.Progress().success);
    auto invalid_texture = texture;
    invalid_texture.images.push_back(texture.images[0]);
    CHECK(!ExportCapturedTexturePackage(invalid_texture, identity, summary.key,
                                        parent, &failure, &error));
    CHECK(failure.empty() && DirectoryCount(parent) == files_before_cancel);
    fs::remove_all(parent);
    return 0;
}

// SPDX-License-Identifier: GPL-2.0-or-later
#include "shader-browser-capture-export.hh"
#include "../thirdparty/fpng/fpng.h"

#include <nlohmann/json.hpp>

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <fstream>
#include <limits>
#include <mutex>
#include <random>
#include <set>
#include <system_error>

namespace xemu::shader_browser {
namespace {
using Json = nlohmann::json;
namespace fs = std::filesystem;
constexpr uint32_t kMaxImageExtent = 2048;

bool Fail(std::string *error, const std::string &message)
{
    if (error) {
        *error = message;
    }
    return false;
}

bool ValidateImage(const OwnedDrawImage &image, std::string *error)
{
    if (!image.width || !image.height || image.width > kMaxImageExtent ||
        image.height > kMaxImageExtent ||
        image.rgba.size() != size_t(image.width) * image.height * 4) {
        return Fail(error, "Image must contain exact top-down RGBA8 pixels at "
                           "dimensions from 1 to 2048.");
    }
    return true;
}

bool AddBytes(size_t count, size_t element_size, size_t *total)
{
    if (count > (kDrawInputBudget - *total) / element_size) {
        return false;
    }
    *total += count * element_size;
    return true;
}

bool ValidateInputs(const OwnedDrawInputs &inputs,
                    const PreviewCapturedMesh &mesh,
                    const DrawCaptureSummary &summary, std::string *error)
{
    if (!inputs.complete) {
        return Fail(error, "Captured draw inputs are incomplete.");
    }
    if (!inputs.sources[0].empty()) {
        return Fail(error, "Captured shader source has an unknown stage.");
    }
    auto optional_image = [&](const OwnedDrawImage &image) {
        return (!image.width && !image.height && image.rgba.empty()) ||
               ValidateImage(image, error);
    };
    if (!optional_image(inputs.before) || !optional_image(inputs.after)) {
        return false;
    }
    if (!inputs.before.rgba.empty() && !inputs.after.rgba.empty() &&
        (inputs.before.width != inputs.after.width ||
         inputs.before.height != inputs.after.height)) {
        return Fail(error,
                    "Before and after images must have equal dimensions.");
    }
    if (summary.shader_count > summary.shaders.size() ||
        summary.resources.size() > kCaptureMaxResourcesPerDraw ||
        summary.segments.size() > kCaptureMaxAnalysisSegments) {
        return Fail(error, "Capture metadata exceeds its bounded limits.");
    }
    size_t total = sizeof(inputs) + sizeof(mesh) + sizeof(summary);
    bool budget =
        AddBytes(inputs.before.rgba.size(), 1, &total) &&
        AddBytes(inputs.after.rgba.size(), 1, &total) &&
        AddBytes(mesh.positions.size(), sizeof(mesh.positions[0]), &total) &&
        AddBytes(mesh.indices.size(), sizeof(uint32_t), &total) &&
        AddBytes(inputs.uniforms.size(), sizeof(OwnedDrawUniform), &total) &&
        AddBytes(inputs.registers.size(), sizeof(OwnedDrawRegister), &total) &&
        AddBytes(summary.resources.size(), sizeof(ResourceTouch), &total) &&
        AddBytes(summary.segments.size(), sizeof(DrawSegmentSummary), &total);
    for (const auto &source : inputs.sources) {
        budget = budget && AddBytes(source.size(), 1, &total);
    }
    for (const auto &uniform : inputs.uniforms) {
        budget = budget && AddBytes(uniform.name.size(), 1, &total) &&
                 AddBytes(uniform.data.size(), 1, &total);
    }
    for (const auto &reg : inputs.registers) {
        budget = budget && AddBytes(reg.name.size(), 1, &total);
    }
    if (inputs.blobs.size() > 128)
        return Fail(error, "Too many raw input blocks.");
    budget =
        budget && AddBytes(inputs.blobs.size(), sizeof(OwnedDrawBlob), &total);
    for (const auto &blob : inputs.blobs) {
        if (blob.name.empty() || blob.name.size() >= 64 ||
            blob.bytes.size() > 16U * 1024U * 1024U)
            return Fail(error, "Invalid raw input block.");
        budget = budget && AddBytes(blob.name.size(), 1, &total) &&
                 AddBytes(blob.bytes.size(), 1, &total);
    }
    for (size_t slot = 0; slot < inputs.textures.size(); ++slot) {
        const auto &texture = inputs.textures[slot];
        budget = budget && AddBytes(texture.images.size(),
                                    sizeof(OwnedDrawTextureImage), &total);
        if (!texture.described) {
            if (!texture.images.empty()) {
                return Fail(error,
                            "Texture images have no captured descriptor.");
            }
            continue;
        }
        if (texture.metadata.slot != slot ||
            !std::isfinite(texture.metadata.coordinate_scale)) {
            return Fail(error, "Invalid captured texture descriptor.");
        }
        std::set<std::pair<uint32_t, uint32_t>> subresources;
        for (const auto &image : texture.images) {
            if (!ValidateImage(image.image, error)) {
                return false;
            }
            if (!texture.metadata.bound ||
                image.mip_level >= texture.metadata.mip_levels ||
                image.face >= texture.metadata.face_count ||
                !subresources.emplace(image.mip_level, image.face).second) {
                return Fail(error, "Invalid or duplicate texture mip/face.");
            }
            budget = budget && AddBytes(image.image.rgba.size(), 1, &total);
        }
    }
    for (const auto &segment : summary.segments) {
        if (segment.primitive_indices.size() > kCaptureMaxSegmentationIndices) {
            return Fail(error, "Capture segment exceeds its bounded limits.");
        }
        budget = budget && AddBytes(segment.primitive_indices.size(),
                                    sizeof(uint32_t), &total);
        if (segment.bounds.valid) {
            for (size_t i = 0; i < 3; ++i) {
                if (!std::isfinite(segment.bounds.minimum[i]) ||
                    !std::isfinite(segment.bounds.maximum[i])) {
                    return Fail(error, "Capture bounds must be finite.");
                }
            }
        }
    }
    if (!budget) {
        return Fail(error, "Captured draw exceeds the 64 MiB input budget.");
    }
    for (const auto &position : mesh.positions) {
        for (const auto component : position) {
            if (!std::isfinite(component)) {
                return Fail(error, "Captured positions must be finite.");
            }
        }
    }
    for (const auto index : mesh.indices) {
        if (index >= mesh.positions.size()) {
            return Fail(error, "Captured index references a missing position.");
        }
    }
    return true;
}

template <size_t N> std::string Hex(const std::array<uint8_t, N> &bytes)
{
    constexpr char digits[] = "0123456789abcdef";
    std::string text(N * 2, '0');
    for (size_t i = 0; i < N; ++i) {
        text[i * 2] = digits[bytes[i] >> 4];
        text[i * 2 + 1] = digits[bytes[i] & 15];
    }
    return text;
}

Json Shader(const ShaderKey &shader)
{
    return { { "stage", uint32_t(shader.stage) },
             { "hash_version", shader.hash.version },
             { "hash", Hex(shader.hash.bytes) } };
}

Json Scope(const ShaderScope &scope)
{
    return { { "title_id", scope.title_id },
             { "executable_fingerprint_version",
               scope.executable_fingerprint_version },
             { "executable_fingerprint", Hex(scope.executable_fingerprint) } };
}

Json Event(const DrawEventKey &event)
{
    return { { "session_epoch", event.session_epoch },
             { "renderer_epoch", event.renderer_epoch },
             { "frame", event.frame },
             { "draw", event.draw },
             { "submission", event.submission } };
}

Json Range(const AddressRange &range)
{
    return { { "address", range.address }, { "length", range.length } };
}

Json Summary(const DrawCaptureSummary &summary)
{
    Json result = Event(summary.key);
    result["scope"] = Scope(summary.scope);
    result["domain"] = uint32_t(summary.domain);
    result["primitive_mode"] = summary.primitive_mode;
    result["vertex_count"] = summary.vertex_count;
    result["index_count"] = summary.index_count;
    result["primitive_count"] = summary.primitive_count;
    result["completeness"] = uint32_t(summary.completeness);
    result["segmentation_complete"] = summary.segmentation_complete;
    result["batched_geometry_suspected"] = summary.batched_geometry_suspected;
    result["shaders"] = Json::array();
    for (size_t i = 0; i < summary.shader_count; ++i) {
        result["shaders"].push_back(Shader(summary.shaders[i]));
    }
    result["resources"] = Json::array();
    for (const auto &touch : summary.resources) {
        const auto &resource = touch.resource;
        result["resources"].push_back(
            { { "kind", uint32_t(resource.kind) },
              { "guest", Range(resource.guest) },
              { "storage_id", resource.storage_id },
              { "storage_range", Range(resource.storage_range) },
              { "descriptor_digest", Hex(resource.descriptor_digest) },
              { "content_digest", Hex(resource.content_digest) },
              { "slot", touch.slot },
              { "access", uint32_t(touch.access) },
              { "read_version", touch.read_version },
              { "write_version", touch.write_version } });
    }
    result["segments"] = Json::array();
    for (const auto &segment : summary.segments) {
        result["segments"].push_back(
            { { "draw", Event(segment.key.draw) },
              { "segment", segment.key.segment },
              { "origin", uint32_t(segment.origin) },
              { "first_primitive", segment.first_primitive },
              { "primitive_count", segment.primitive_count },
              { "first_index", segment.first_index },
              { "index_count", segment.index_count },
              { "vertex_span", Range(segment.vertex_span) },
              { "index_span", Range(segment.index_span) },
              { "geometry_digest", Hex(segment.geometry_digest) },
              { "transform_digest", Hex(segment.transform_digest) },
              { "skinning_digest", Hex(segment.skinning_digest) },
              { "bounds_space_digest", Hex(segment.bounds_space_digest) },
              { "bounds",
                { { "valid", segment.bounds.valid },
                  { "minimum", segment.bounds.valid ?
                                   Json(segment.bounds.minimum) :
                                   Json(nullptr) },
                  { "maximum", segment.bounds.valid ?
                                   Json(segment.bounds.maximum) :
                                   Json(nullptr) } } },
              { "primitive_indices", segment.primitive_indices },
              { "confirmed_object_id", segment.confirmed_object_id } });
    }
    return result;
}

const char *BackendName(PreviewBackend backend)
{
    switch (backend) {
    case PreviewBackend::OpenGL:
        return "OpenGL";
    case PreviewBackend::Vulkan:
        return "Vulkan";
    default:
        return "Unknown";
    }
}

bool WriteBytes(const fs::path &path, const void *bytes, size_t count,
                std::string *error)
{
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    if (!stream) {
        return Fail(error, "Cannot create capture package file.");
    }
    if (count) {
        stream.write(static_cast<const char *>(bytes), count);
    }
    stream.close();
    return stream ? true : Fail(error, "Cannot write capture package file.");
}

bool WriteJson(const fs::path &path, const Json &document, std::string *error)
{
    const auto bytes = document.dump(2) + '\n';
    return WriteBytes(path, bytes.data(), bytes.size(), error);
}

bool WriteImage(const fs::path &directory, const std::string &stem,
                const OwnedDrawImage &image, Json *description,
                std::string *error)
{
    std::vector<uint8_t> png;
    static std::once_flag initialized;
    std::call_once(initialized, fpng::fpng_init);
    if (!fpng::fpng_encode_image_to_memory(image.rgba.data(), image.width,
                                           image.height, 4, png)) {
        return Fail(error, "Cannot encode capture image PNG.");
    }
    const auto raw_path = stem + ".rgba.raw";
    const auto png_path = stem + ".png";
    if (!WriteBytes(directory / fs::u8path(raw_path), image.rgba.data(),
                    image.rgba.size(), error) ||
        !WriteBytes(directory / fs::u8path(png_path), png.data(), png.size(),
                    error)) {
        return false;
    }
    *description = { { "width", image.width },
                     { "height", image.height },
                     { "format", "RGBA8" },
                     { "row_order", "top-down" },
                     { "raw", raw_path },
                     { "png", png_path },
                     { "byte_count", image.rgba.size() } };
    return true;
}

void AppendU32(std::vector<uint8_t> *bytes, uint32_t value)
{
    bytes->push_back(value & 255);
    bytes->push_back((value >> 8) & 255);
    bytes->push_back((value >> 16) & 255);
    bytes->push_back(value >> 24);
}

bool WritePackage(const fs::path &directory, const OwnedDrawInputs &inputs,
                  const PreviewCapturedMesh &mesh,
                  const DrawCaptureSummary &summary,
                  const CaptureExportIdentity &identity, std::string *error)
{
    for (const auto subdirectory : { "geometry", "shaders", "textures", "state",
                                     "uniforms", "output", "raw" }) {
        fs::create_directory(directory / subdirectory);
    }
    Json manifest = { { "schema", "xemu.captured-draw.v1" },
                      { "identity",
                        { { "shader", Shader(identity.shader) },
                          { "scope", Scope(identity.scope) },
                          { "backend", BackendName(identity.backend) } } },
                      { "draw", Summary(summary) },
                      { "inputs_complete", inputs.complete },
                      { "provenance", "provenance.json" },
                      { "textures", Json::array() },
                      { "shaders", Json::array() } };
    std::vector<uint8_t> bytes;
    manifest["raw_inputs"] = Json::array();
    for (size_t i = 0; i < inputs.blobs.size(); ++i) {
        const auto &blob = inputs.blobs[i];
        const std::string path = "raw/b" + std::to_string(i) + ".raw";
        if (!WriteBytes(directory / fs::u8path(path), blob.bytes.data(),
                        blob.bytes.size(), error))
            return false;
        manifest["raw_inputs"].push_back({ { "name", blob.name },
                                           { "path", path },
                                           { "bytes", blob.bytes.size() },
                                           { "slot", blob.slot },
                                           { "format", blob.format },
                                           { "components", blob.components },
                                           { "stride", blob.stride },
                                           { "count", blob.count },
                                           { "offset", blob.offset },
                                           { "normalized", blob.normalized },
                                           { "integer", blob.integer } });
    }
    bytes.reserve(mesh.positions.size() * 16);
    static_assert(sizeof(float) == 4 && std::numeric_limits<float>::is_iec559,
                  "Capture export requires IEEE 754 float32.");
    for (const auto &position : mesh.positions) {
        for (const float component : position) {
            uint32_t bits;
            std::memcpy(&bits, &component, sizeof(bits));
            AppendU32(&bytes, bits);
        }
    }
    if (!WriteBytes(directory / "geometry/positions.raw", bytes.data(),
                    bytes.size(), error)) {
        return false;
    }
    bytes.clear();
    for (const auto index : mesh.indices) {
        AppendU32(&bytes, index);
    }
    if (!WriteBytes(directory / "geometry/indices.raw", bytes.data(),
                    bytes.size(), error)) {
        return false;
    }
    manifest["geometry"] = {
        { "positions", "geometry/positions.raw" },
        { "positions_format", "float32x4-little-endian" },
        { "vertex_count", mesh.positions.size() },
        { "indices", "geometry/indices.raw" },
        { "indices_format", "uint32-little-endian" },
        { "index_count", mesh.indices.size() },
        { "provenance",
          "Captured submitted position attributes and triangle "
          "indices; original vertex shader outputs are unavailable." }
    };
    constexpr const char *stage_names[] = { "unknown", "vertex", "pixel",
                                            "geometry", "fixed-function" };
    for (size_t stage = 1; stage < inputs.sources.size(); ++stage) {
        const auto &source = inputs.sources[stage];
        if (source.empty()) {
            continue;
        }
        const std::string path =
            std::string("shaders/") + stage_names[stage] + ".glsl";
        if (!WriteBytes(directory / fs::u8path(path), source.data(),
                        source.size(), error)) {
            return false;
        }
        manifest["shaders"].push_back({ { "stage", stage },
                                        { "stage_name", stage_names[stage] },
                                        { "path", path },
                                        { "byte_count", source.size() } });
    }
    for (const auto &texture : inputs.textures) {
        if (!texture.described) {
            continue;
        }
        const auto &m = texture.metadata;
        Json descriptor = { { "slot", m.slot },
                            { "bound", bool(m.bound) },
                            { "guest_format", m.guest_format },
                            { "host_format", m.host_format },
                            { "width", m.width },
                            { "height", m.height },
                            { "depth", m.depth },
                            { "mip_levels", m.mip_levels },
                            { "face_count", m.face_count },
                            { "min_filter", m.min_filter },
                            { "mag_filter", m.mag_filter },
                            { "wrap_s", m.wrap_s },
                            { "wrap_t", m.wrap_t },
                            { "wrap_r", m.wrap_r },
                            { "coordinate_scale", m.coordinate_scale },
                            { "mip_level", m.mip_level },
                            { "face", m.face },
                            { "pixel_provenance", "host-decoded RGBA8" },
                            { "images", Json::array() } };
        for (const auto &image : texture.images) {
            const std::string stem = "textures/T" + std::to_string(m.slot) +
                                     "-mip" + std::to_string(image.mip_level) +
                                     "-face" + std::to_string(image.face);
            Json description;
            if (!WriteImage(directory, stem, image.image, &description,
                            error)) {
                return false;
            }
            description["mip_level"] = image.mip_level;
            description["face"] = image.face;
            descriptor["images"].push_back(std::move(description));
        }
        manifest["textures"].push_back(std::move(descriptor));
    }
    Json uniforms = Json::array();
    for (size_t i = 0; i < inputs.uniforms.size(); ++i) {
        const auto &uniform = inputs.uniforms[i];
        const std::string path = "uniforms/u" + std::to_string(i) + ".raw";
        if (!WriteBytes(directory / fs::u8path(path), uniform.data.data(),
                        uniform.data.size(), error)) {
            return false;
        }
        uniforms.push_back({ { "stage", uniform.stage },
                             { "name", uniform.name },
                             { "type", uniform.type },
                             { "components", uniform.components },
                             { "count", uniform.count },
                             { "byte_count", uniform.data.size() },
                             { "raw", path } });
    }
    Json registers = Json::array();
    for (const auto &reg : inputs.registers) {
        registers.push_back({ { "name", reg.name }, { "value", reg.value } });
    }
    if (!WriteJson(directory / "state/uniforms.json", uniforms, error) ||
        !WriteJson(directory / "state/registers.json", registers, error)) {
        return false;
    }
    manifest["uniforms"] = "state/uniforms.json";
    manifest["registers"] = "state/registers.json";
    OwnedDrawImage difference;
    if (!inputs.before.rgba.empty() && !inputs.after.rgba.empty()) {
        if (!MakeCapturedDrawDifference(inputs.before, inputs.after,
                                        &difference, error))
            return false;
    }
    const std::pair<const char *, const OwnedDrawImage *> outputs[] = {
        { "before", &inputs.before },
        { "after", &inputs.after },
        { "difference", &difference }
    };
    for (const auto &output : outputs) {
        auto &entry = manifest["output"][output.first];
        if (output.second->rgba.empty())
            entry = { { "status", "missing" } };
        else {
            if (!WriteImage(directory, std::string("output/") + output.first,
                            *output.second, &entry, error))
                return false;
            entry["status"] = "observed";
        }
    }
    if (!difference.rgba.empty()) {
        manifest["output"]["difference"]["status"] =
            "derived from observed before/after";
        manifest["output"]["difference"]["operation"] =
            "absolute RGB difference; alpha=255";
    }
    const Json provenance = {
        { "schema", "xemu.captured-draw-provenance.v1" },
        { "description", "Owned snapshot of one submitted draw's decoded host "
                         "material inputs and renderer state." },
        { "texture_guest_raw_bytes_captured", false },
        { "texture_palette_captured", false },
        { "original_vertex_outputs_captured", false },
        { "full_replay", false },
        { "engine_asset", false },
        { "uniform_byte_order", "host-native" },
        { "uniform_little_endian_host",
          [] {
              const uint32_t one = 1;
              return *reinterpret_cast<const uint8_t *>(&one) == 1;
          }() },
        { "limitations",
          { "Texture pixels are host-decoded RGBA8, without "
            "guest storage bytes or palette data.",
            "Geometry contains captured submitted positions and "
            "indices, without original vertex shader outputs or "
            "all vertex attributes.",
            "A shader may serve unrelated objects; the package "
            "does not establish engine asset or object identity.",
            "Captured state does not establish complete draw "
            "replay or independent engine material reproduction." } }
    };
    return WriteJson(directory / "provenance.json", provenance, error) &&
           WriteJson(directory / "manifest.json", manifest, error);
}

struct StagingDirectory {
    fs::path path;
    ~StagingDirectory()
    {
        if (!path.empty()) {
            std::error_code ignored;
            fs::remove_all(path, ignored);
        }
    }
};
} // namespace

bool MakeCapturedDrawDifference(const OwnedDrawImage &before,
                                const OwnedDrawImage &after,
                                OwnedDrawImage *difference, std::string *error)
{
    if (error) {
        error->clear();
    }
    if (!difference) {
        return Fail(error, "Missing difference destination.");
    }
    // Construct before replacing the destination so callers may reuse an input.
    OwnedDrawImage result;
    if (!ValidateImage(before, error) || !ValidateImage(after, error) ||
        before.width != after.width || before.height != after.height) {
        *difference = {};
        if (error && error->empty()) {
            *error = "Before and after images must have equal dimensions.";
        }
        return false;
    }
    result.width = before.width;
    result.height = before.height;
    result.rgba.resize(before.rgba.size());
    for (size_t i = 0; i < result.rgba.size(); i += 4) {
        for (size_t channel = 0; channel < 3; ++channel) {
            result.rgba[i + channel] = uint8_t(std::abs(
                int(after.rgba[i + channel]) - int(before.rgba[i + channel])));
        }
        result.rgba[i + 3] = 255;
    }
    *difference = std::move(result);
    return true;
}

bool ExportCapturedDrawPackage(const OwnedDrawInputs &inputs,
                               const PreviewCapturedMesh &mesh,
                               const DrawCaptureSummary &summary,
                               const CaptureExportIdentity &identity,
                               const fs::path &parent_directory,
                               fs::path *result_directory, std::string *error)
{
    if (error) {
        error->clear();
    }
    if (!result_directory) {
        return Fail(error, "Missing capture package destination.");
    }
    result_directory->clear();
    StagingDirectory staging;
    try {
        if (!ValidateInputs(inputs, mesh, summary, error)) {
            return false;
        }
        if (!fs::is_directory(parent_directory)) {
            return Fail(error,
                        "Capture export parent must be an existing directory.");
        }
        static std::atomic<uint64_t> sequence{ 0 };
        std::random_device entropy;
        fs::path published;
        for (unsigned attempt = 0; attempt < 64; ++attempt) {
            const auto tick =
                std::chrono::steady_clock::now().time_since_epoch().count();
            const auto name = "captured-draw-" + std::to_string(tick) + "-" +
                              std::to_string(entropy()) + "-" +
                              std::to_string(sequence.fetch_add(1));
            published = parent_directory / name;
            const auto temporary = parent_directory / ("." + name + ".tmp");
            if (fs::exists(published)) {
                continue;
            }
            if (fs::create_directory(temporary)) {
                staging.path = temporary;
                break;
            }
        }
        if (staging.path.empty()) {
            return Fail(error, "Cannot allocate a unique capture directory.");
        }
        if (!WritePackage(staging.path, inputs, mesh, summary, identity,
                          error)) {
            return false;
        }
        if (fs::exists(published)) {
            return Fail(error, "Capture destination already exists.");
        }
        *result_directory = published;
        fs::rename(staging.path, published);
        staging.path.clear();
        return true;
    } catch (const std::exception &exception) {
        result_directory->clear();
        return Fail(error,
                    std::string("Capture export failed: ") + exception.what());
    }
}

bool ExportCapturedTexturePackage(
    const OwnedDrawTexture &texture, const CaptureExportIdentity &identity,
    const DrawEventKey &event, const fs::path &parent_directory,
    fs::path *result_directory, std::string *error, CaptureFileControl *control)
{
    if (error)
        error->clear();
    if (result_directory)
        result_directory->clear();
    StagingDirectory staging;
    auto checkpoint = [&](CaptureFilePhase phase, uint64_t completed = 0,
                          uint64_t total = 0) {
        if (control && !control->Checkpoint(phase, completed, total))
            throw std::runtime_error("Texture export cancelled.");
    };
    try {
        checkpoint(CaptureFilePhase::Preparing);
        const auto &m = texture.metadata;
        if (!result_directory || !texture.described || !m.bound ||
            m.slot >= 4 || !m.mip_levels || m.mip_levels > 32 ||
            (m.face_count != 1 && m.face_count != 6) ||
            !std::isfinite(m.coordinate_scale) || texture.images.size() > 192 ||
            !fs::is_directory(parent_directory))
            throw std::runtime_error(
                "Invalid captured texture or export parent.");
        std::set<std::pair<uint32_t, uint32_t>> subresources;
        size_t bytes = sizeof(texture);
        for (const auto &image : texture.images) {
            if (image.mip_level >= m.mip_levels || image.face >= m.face_count ||
                !subresources.emplace(image.mip_level, image.face).second ||
                !ValidateImage(image.image, error) ||
                !AddBytes(image.image.rgba.size(), 1, &bytes))
                throw std::runtime_error(
                    "Invalid, duplicate or oversized texture image.");
        }
        static std::atomic<uint64_t> sequence{ 0 };
        fs::path published;
        std::random_device entropy;
        for (unsigned attempt = 0; attempt < 64; ++attempt) {
            checkpoint(CaptureFilePhase::Preparing);
            const auto tick =
                std::chrono::steady_clock::now().time_since_epoch().count();
            const std::string name =
                "captured-texture-T" + std::to_string(m.slot) + "-" +
                std::to_string(tick) + "-" + std::to_string(entropy()) + "-" +
                std::to_string(sequence.fetch_add(1));
            published = parent_directory / name;
            const auto temporary = parent_directory / ("." + name + ".tmp");
            if (!fs::exists(published) && fs::create_directory(temporary)) {
                staging.path = temporary;
                break;
            }
        }
        if (staging.path.empty())
            throw std::runtime_error(
                "Cannot allocate a unique texture export directory.");
        Json manifest = {
            { "schema", "xemu.captured-texture.v1" },
            { "identity",
              { { "shader", Shader(identity.shader) },
                { "scope", Scope(identity.scope) },
                { "backend", BackendName(identity.backend) } } },
            { "draw", Event(event) },
            { "engine_asset", false },
            { "texture",
              { { "slot", m.slot },
                { "guest_format", m.guest_format },
                { "host_format", m.host_format },
                { "width", m.width },
                { "height", m.height },
                { "depth", m.depth },
                { "mip_levels", m.mip_levels },
                { "face_count", m.face_count },
                { "min_filter", m.min_filter },
                { "mag_filter", m.mag_filter },
                { "wrap_s", m.wrap_s },
                { "wrap_t", m.wrap_t },
                { "wrap_r", m.wrap_r },
                { "coordinate_scale", m.coordinate_scale },
                { "guest_storage_captured", false },
                { "guest_palette_captured", false },
                { "pixel_provenance", "owned host-decoded RGBA8" },
                { "images", Json::array() } } }
        };
        uint64_t done = 0;
        for (const auto &image : texture.images) {
            checkpoint(CaptureFilePhase::WritingResources, done,
                       texture.images.size());
            const std::string name = "T" + std::to_string(m.slot) + "-mip" +
                                     std::to_string(image.mip_level) + "-face" +
                                     std::to_string(image.face);
            Json descriptor;
            if (!WriteImage(staging.path, name, image.image, &descriptor,
                            error))
                throw std::runtime_error(
                    error && !error->empty() ?
                        *error :
                        "Cannot write captured texture image.");
            descriptor["mip_level"] = image.mip_level;
            descriptor["face"] = image.face;
            manifest["texture"]["images"].push_back(std::move(descriptor));
            checkpoint(CaptureFilePhase::WritingResources, ++done,
                       texture.images.size());
        }
        manifest["texture"]["images_available"] = texture.images.size();
        manifest["texture"]["images_expected"] =
            uint64_t(m.mip_levels) * m.face_count;
        checkpoint(CaptureFilePhase::WritingMetadata);
        if (!WriteJson(staging.path / "manifest.json", manifest, error))
            throw std::runtime_error("Cannot write captured texture manifest.");
        if (fs::exists(published))
            throw std::runtime_error(
                "Texture export destination already exists.");
        if (control && !control->BeginPublication())
            throw std::runtime_error("Texture export cancelled.");
        fs::rename(staging.path, published);
        staging.path.clear();
        *result_directory = std::move(published);
        if (control)
            control->Finish(true);
        return true;
    } catch (const std::exception &exception) {
        if (control)
            control->Finish(false);
        return Fail(error,
                    std::string("Texture export failed: ") + exception.what());
    }
}

} // namespace xemu::shader_browser

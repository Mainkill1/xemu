// SPDX-License-Identifier: GPL-2.0-or-later
#include "asset-browser-export.hh"
#include "asset-browser-material.hh"
#include "../thirdparty/fpng/fpng.h"
#include <nlohmann/json.hpp>
#include <atomic>
#include <cmath>
#include <cstring>
#include <fstream>
#include <mutex>
#include <stdexcept>

namespace xemu::asset_browser {
namespace {
using Json = nlohmann::json;
constexpr size_t kGlbBudget = 256U * 1024U * 1024U;
void U32(std::vector<uint8_t> &data, uint32_t value)
{
    for (size_t i = 0; i < 4; ++i)
        data.push_back(uint8_t(value >> (8 * i)));
}
void F32(std::vector<uint8_t> &data, float value)
{
    if (!std::isfinite(value))
        throw std::runtime_error(
            "Non-finite decoded geometry cannot be exported as glTF; raw "
            "evidence remains in the capture package");
    uint32_t bits;
    std::memcpy(&bits, &value, 4);
    U32(data, bits);
}
void Check(capture::CaptureFileControl *control, size_t done, size_t total)
{
    if (control &&
        !control->Checkpoint(capture::CaptureFilePhase::WritingResources, done,
                             total))
        throw std::runtime_error("Asset export cancelled");
}
} // namespace
bool ExportAssetGlb(const AssetAssembly &assembly,
                    const std::filesystem::path &path, std::string *error,
                    capture::CaptureFileControl *control)
{
    if (error)
        error->clear();
    std::filesystem::path temporary;
    try {
        Check(control, 0, assembly.parts.size());
        if (path.empty() || std::filesystem::exists(path) ||
            assembly.parts.empty() || assembly.parts.size() > 256)
            throw std::runtime_error(
                "GLB requires bounded parts and a new destination");
        Json doc = {
            { "asset",
              { { "version", "2.0" }, { "generator", "xemu Asset Browser" } } },
            { "scene", 0 },
            { "scenes", Json::array({ { { "nodes", Json::array({ 0 }) } } }) },
            { "nodes", Json::array({ { { "name", assembly.label },
                                       { "children", Json::array() } } }) },
            { "meshes", Json::array() },
            { "materials", Json::array() },
            { "bufferViews", Json::array() },
            { "accessors", Json::array() },
            { "extensionsUsed", Json::array({ "KHR_materials_unlit" }) }
        };
        doc["asset"]["extras"] = {
            { "title_id", assembly.context.scope.title_id },
            { "frame", assembly.frame },
            { "session_epoch", assembly.context.session_epoch },
            { "renderer_epoch", assembly.context.renderer_epoch },
            { "backend", assembly.context.backend },
            { "user_confirmed_membership", assembly.user_confirmed },
            { "fidelity",
              "Captured vertex inputs and base2D textures. Original "
              "vertex/pixel/geometry stages, skinning, generated coordinates, "
              "blending, depth and destination are not replayed. Game "
              "coordinate units and axis meanings are unverified." }
        };
        std::vector<uint8_t> binary;
        auto view = [&](std::vector<uint8_t> data, uint32_t target) {
            while (binary.size() % 4)
                binary.push_back(0);
            if (data.size() > kGlbBudget ||
                binary.size() > kGlbBudget - data.size())
                throw std::runtime_error("GLB byte budget exceeded");
            const size_t id = doc["bufferViews"].size();
            Json record = { { "buffer", 0 },
                            { "byteOffset", binary.size() },
                            { "byteLength", data.size() } };
            if (target)
                record["target"] = target;
            doc["bufferViews"].push_back(record);
            binary.insert(binary.end(), data.begin(), data.end());
            return id;
        };
        auto accessor = [&](size_t buffer, uint32_t type, size_t count,
                            const char *shape) {
            size_t id = doc["accessors"].size();
            doc["accessors"].push_back({ { "bufferView", buffer },
                                         { "componentType", type },
                                         { "count", count },
                                         { "type", shape } });
            return id;
        };
        for (size_t p = 0; p < assembly.parts.size(); ++p) {
            Check(control, p, assembly.parts.size());
            const auto &part = assembly.parts[p];
            if (!part || part->status != AssetStatus::Ready ||
                part->vertices.empty() || part->indices.empty() ||
                part->vertices.size() > 1048576 ||
                part->indices.size() > 3145728 || part->indices.size() % 3)
                throw std::runtime_error(
                    "Every exported part needs supported triangle geometry");
            std::vector<uint8_t> positions, uv, colors, indices;
            std::array<float, 3> minimum = part->vertices[0].position,
                                 maximum = minimum;
            for (const auto &vertex : part->vertices) {
                for (size_t axis = 0; axis < 3; ++axis) {
                    F32(positions, vertex.position[axis]);
                    minimum[axis] =
                        std::min(minimum[axis], vertex.position[axis]);
                    maximum[axis] =
                        std::max(maximum[axis], vertex.position[axis]);
                }
                if (part->has_uv) {
                    F32(uv, vertex.uv[0]);
                    F32(uv, assembly.context.backend == 1 ? 1 - vertex.uv[1] :
                                                            vertex.uv[1]);
                }
                if (part->has_color)
                    for (float channel : vertex.color)
                        F32(colors, std::clamp(channel, 0.f, 1.f));
            }
            for (auto index : part->indices) {
                if (index >= part->vertices.size())
                    throw std::runtime_error("Invalid exported index");
                U32(indices, index);
            }
            Json attributes;
            auto position = accessor(view(std::move(positions), 34962), 5126,
                                     part->vertices.size(), "VEC3");
            doc["accessors"][position]["min"] = minimum;
            doc["accessors"][position]["max"] = maximum;
            attributes["POSITION"] = position;
            if (part->has_uv)
                attributes["TEXCOORD_0"] =
                    accessor(view(std::move(uv), 34962), 5126,
                             part->vertices.size(), "VEC2");
            if (part->has_color)
                attributes["COLOR_0"] =
                    accessor(view(std::move(colors), 34962), 5126,
                             part->vertices.size(), "VEC4");
            auto index = accessor(view(std::move(indices), 34963), 5125,
                                  part->indices.size(), "SCALAR");
            auto texture = DecodeAssetTexture(*part, assembly.context.backend);
            Json material = {
                { "name", "E" + std::to_string(part->id) },
                { "doubleSided", true },
                { "pbrMetallicRoughness",
                  { { "metallicFactor", 0 }, { "roughnessFactor", 1 } } },
                { "extensions", { { "KHR_materials_unlit", Json::object() } } },
                { "extras",
                  { { "event", part->id },
                    { "base_texture_slot", texture.slot },
                    { "material_limitations", texture.reason } } }
            };
            if (!texture.rgba.empty()) {
                static std::once_flag initialized;
                std::call_once(initialized, fpng::fpng_init);
                std::vector<uint8_t> png;
                if (!fpng::fpng_encode_image_to_memory(texture.rgba.data(),
                                                       texture.width,
                                                       texture.height, 4, png))
                    throw std::runtime_error("PNG encoding failed");
                if (!doc.contains("images")) {
                    doc["images"] = Json::array();
                    doc["textures"] = Json::array();
                    doc["samplers"] = Json::array();
                }
                size_t image = doc["images"].size();
                doc["images"].push_back(
                    { { "bufferView", view(std::move(png), 0) },
                      { "mimeType", "image/png" } });
                size_t sampler = doc["samplers"].size();
                doc["samplers"].push_back({ { "minFilter", texture.min_filter },
                                            { "magFilter", texture.mag_filter },
                                            { "wrapS", texture.wrap_s },
                                            { "wrapT", texture.wrap_t } });
                size_t tex = doc["textures"].size();
                doc["textures"].push_back(
                    { { "source", image }, { "sampler", sampler } });
                material["pbrMetallicRoughness"]["baseColorTexture"] = {
                    { "index", tex }, { "texCoord", 0 }
                };
            }
            size_t mesh = doc["meshes"].size(), mat = doc["materials"].size();
            doc["materials"].push_back(material);
            doc["meshes"].push_back(
                { { "primitives", Json::array({ { { "attributes", attributes },
                                                  { "indices", index },
                                                  { "material", mat },
                                                  { "mode", 4 } } }) } });
            size_t node = doc["nodes"].size();
            doc["nodes"].push_back(
                { { "mesh", mesh },
                  { "name", "E" + std::to_string(part->id) },
                  { "extras",
                    { { "event", part->id }, { "frame", part->frame } } } });
            doc["nodes"][0]["children"].push_back(node);
        }
        doc["buffers"] = Json::array({ { { "byteLength", binary.size() } } });
        while (binary.size() % 4)
            binary.push_back(0);
        std::string json = doc.dump();
        while (json.size() % 4)
            json.push_back(' ');
        if (json.size() > 4U * 1024U * 1024U)
            throw std::runtime_error("GLB metadata budget exceeded");
        std::vector<uint8_t> header;
        U32(header, 0x46546c67);
        U32(header, 2);
        U32(header, 28 + json.size() + binary.size());
        U32(header, json.size());
        U32(header, 0x4e4f534a);
        static std::atomic<uint64_t> nonce{ 0 };
        auto parent = path.parent_path();
        if (parent.empty())
            parent = ".";
        for (size_t attempt = 0; attempt < 64; ++attempt) {
            auto candidate =
                parent / (path.filename().string() + ".asset-temp-" +
                          std::to_string(++nonce));
            if (std::filesystem::create_directory(candidate)) {
                temporary = candidate;
                break;
            }
        }
        if (temporary.empty())
            throw std::runtime_error("Cannot reserve export staging directory");
        auto staging = temporary / "asset.glb";
        std::ofstream file(staging, std::ios::binary);
        file.write(reinterpret_cast<const char *>(header.data()),
                   header.size());
        file.write(json.data(), json.size());
        header.clear();
        U32(header, binary.size());
        U32(header, 0x004e4942);
        file.write(reinterpret_cast<const char *>(header.data()),
                   header.size());
        file.write(reinterpret_cast<const char *>(binary.data()),
                   binary.size());
        file.close();
        if (!file)
            throw std::runtime_error("GLB write failed");
        if (control && !control->BeginPublication())
            throw std::runtime_error("Asset export cancelled");
        if (std::filesystem::exists(path))
            throw std::runtime_error(
                "GLB destination was created during export");
        // A hard link publishes without replacing a racing destination. The
        // staging file is on the same filesystem; unsupported filesystems fail.
        std::filesystem::create_hard_link(staging, path);
        // Publication already succeeded. Cleanup failure must not report that
        // the destination was never created.
        std::error_code ignored;
        std::filesystem::remove_all(temporary, ignored);
        temporary.clear();
        if (control)
            control->Finish(true);
        return true;
    } catch (const std::exception &exception) {
        if (!temporary.empty()) {
            std::error_code ignored;
            std::filesystem::remove_all(temporary, ignored);
        }
        if (control)
            control->Finish(false);
        if (error)
            *error = exception.what();
        return false;
    }
}
} // namespace xemu::asset_browser

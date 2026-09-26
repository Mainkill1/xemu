// SPDX-License-Identifier: GPL-2.0-or-later
#include "shader-browser-glsl-export.hh"

#include <xxhash.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <fstream>
#include <vector>

namespace xemu::shader_browser {
namespace {

bool Fail(std::string *error, const char *reason)
{
    if (error) *error = reason;
    return false;
}

std::string Hex(const uint8_t *data, size_t size)
{
    static constexpr char digits[] = "0123456789abcdef";
    std::string result;
    result.reserve(size * 2);
    for (size_t i = 0; i < size; ++i) {
        result.push_back(digits[data[i] >> 4]);
        result.push_back(digits[data[i] & 15]);
    }
    return result;
}

bool WriteFile(const std::filesystem::path &path, const std::string &bytes)
{
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    return file && file.write(bytes.data(), bytes.size()) && file.flush();
}

} // namespace

bool SerializeGeneratedGlsl(const Entry &entry, const HostSource &source,
                            std::string *glsl, std::string *metadata,
                            std::string *error)
{
    if (!glsl || !metadata) return Fail(error, "GLSL export output unavailable");
    if (source.kind != HostSourceKind::Glsl || source.text.empty() ||
        source.text.size() > kMaxDetailSourceBytes ||
        source.text.find('\0') != std::string::npos) {
        return Fail(error, "Selected host source is not bounded GLSL text");
    }
    if (source.backend != DetailBackend::OpenGL &&
        source.backend != DetailBackend::Vulkan) {
        return Fail(error, "Generated GLSL backend is unknown");
    }
    if (entry.key.hash.version == 0 ||
        source.stage == HostSourceStage::Unknown) {
        return Fail(error, "Generated GLSL provenance is incomplete");
    }
    XXH128_hash_t digest = XXH3_128bits(source.text.data(), source.text.size());
    XXH128_canonical_t canonical{};
    XXH128_canonicalFromHash(&canonical, digest);
    std::string result = "{\"schema\":\"xemu.generated-glsl.v1\",";
    result += "\"artifact_kind\":\"generated_glsl\",";
    result += "\"canonical_recipe\":false,";
    result += "\"build_scope_verified\":false,";
    result += "\"selected_shader_hash\":\"" +
              PortableShaderHash(entry.key.hash) + "\",";
    result += "\"selected_shader_stage\":\"" +
              std::string(StageLabel(entry.key.stage)) + "\",";
    result += "\"backend\":\"" +
              std::string(DetailBackendLabel(source.backend)) + "\",";
    result += "\"source_stage\":\"" +
              std::string(HostSourceStageLabel(source.stage)) + "\",";
    result += "\"route\":\"" + std::string(RouteLabel(source.route)) + "\",";
    result += "\"resident\":" +
              std::string(source.exact_runtime_source ? "true" : "false") + ",";
    result += "\"byte_size\":" + std::to_string(source.text.size()) + ",";
    result += "\"digest_algorithm\":\"xxh3-128\",";
    result += "\"source_digest\":\"" +
              Hex(canonical.digest, sizeof(canonical.digest)) + "\",";
    result += "\"associated_scopes\":[";
    std::vector<ShaderScope> scopes = entry.scopes;
    std::sort(scopes.begin(), scopes.end());
    scopes.erase(std::unique(scopes.begin(), scopes.end()), scopes.end());
    for (size_t i = 0; i < scopes.size(); ++i) {
        if (i) result += ',';
        result += "{\"title_id\":\"" + FormatTitleId(scopes[i].title_id) +
                  "\",\"fingerprint_version\":" +
                  std::to_string(scopes[i].executable_fingerprint_version) +
                  ",\"fingerprint\":\"" +
                  Hex(scopes[i].executable_fingerprint.data(),
                      scopes[i].executable_fingerprint.size()) + "\"}";
    }
    result += "]}\n";
    *glsl = source.text;
    *metadata = std::move(result);
    return true;
}

bool ExportGeneratedGlsl(const Entry &entry, const HostSource &source,
                         const std::filesystem::path &config_directory,
                         std::filesystem::path *exported_path,
                         std::string *error)
{
    if (config_directory.empty() || !exported_path) {
        return Fail(error, "Generated GLSL export path unavailable");
    }
    std::string glsl, metadata;
    if (!SerializeGeneratedGlsl(entry, source, &glsl, &metadata, error)) {
        return false;
    }
    std::error_code ec;
    const auto root = config_directory / "shader-exports";
    std::filesystem::create_directories(root, ec);
    if (ec) return Fail(error, "Unable to create shader export directory");
    const auto tick = std::chrono::steady_clock::now().time_since_epoch().count();
    for (unsigned int suffix = 0; suffix < 1000; ++suffix) {
        const std::string stem = "generated-" +
            ShaderHashHex(entry.key.hash) + "-" +
            std::to_string(static_cast<unsigned>(source.stage)) + "-" +
            std::to_string(tick) + "-" + std::to_string(suffix);
        auto glsl_path = root / (stem + ".glsl");
        auto metadata_path = root / (stem + ".meta.json");
        if (std::filesystem::exists(glsl_path, ec) ||
            std::filesystem::exists(metadata_path, ec)) continue;
        if (ec) break;
        auto glsl_tmp = root / (stem + ".glsl.tmp");
        auto metadata_tmp = root / (stem + ".meta.json.tmp");
        if (std::filesystem::exists(glsl_tmp, ec) ||
            std::filesystem::exists(metadata_tmp, ec)) continue;
        if (ec) break;
        if (!WriteFile(glsl_tmp, glsl) || !WriteFile(metadata_tmp, metadata)) {
            std::filesystem::remove(glsl_tmp, ec);
            std::filesystem::remove(metadata_tmp, ec);
            return Fail(error, "Unable to write generated GLSL export");
        }
        std::filesystem::rename(glsl_tmp, glsl_path, ec);
        if (!ec) std::filesystem::rename(metadata_tmp, metadata_path, ec);
        if (!ec) {
            *exported_path = glsl_path;
            return true;
        }
        std::filesystem::remove(glsl_tmp, ec);
        std::filesystem::remove(metadata_tmp, ec);
        std::filesystem::remove(glsl_path, ec);
        return Fail(error, "Unable to publish generated GLSL export");
    }
    return Fail(error, "Unable to choose generated GLSL export path");
}

} // namespace xemu::shader_browser

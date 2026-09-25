// SPDX-License-Identifier: GPL-2.0-or-later
#include "shader-browser-artifact-store.hh"

#include <algorithm>
#include <filesystem>
#include <fstream>

namespace xemu::shader_browser {
namespace {

bool SafeToken(const std::string &value)
{
    return !value.empty() &&
           std::all_of(value.begin(), value.end(), [](unsigned char c) {
               return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                      (c >= '0' && c <= '9') || c == '-' || c == '_';
           });
}

std::string StageDirectory(Stage stage)
{
    switch (stage) {
    case Stage::Vertex: return "vs";
    case Stage::Pixel: return "ps";
    case Stage::Geometry: return "gs";
    case Stage::FixedFunction: return "ff";
    default: return "unknown";
    }
}

} // namespace

bool ShaderArtifactStore::Configure(const ArtifactStoreConfig &new_config)
{
    config = new_config;
    root_path = (std::filesystem::u8path(config.base_path) /
                 "shader-artifacts").u8string();
    return !config.enabled || !config.base_path.empty();
}

bool ShaderArtifactStore::Write(const ArtifactWrite &write,
                                ArtifactMetadata *metadata,
                                std::string *error) const
{
    if (!config.enabled) {
        if (error) *error = "External shader artifact storage is disabled";
        return false;
    }
    if (!metadata || !write.title_id || write.bytes.empty() ||
        !SafeToken(write.backend) || !SafeToken(write.route) ||
        !SafeToken(write.kind) || !SafeToken(write.extension)) {
        if (error) *error = "Invalid shader artifact publication";
        return false;
    }

    std::string hash = ShaderHashBase32(write.key.hash);
    std::filesystem::path relative =
        std::filesystem::u8path(FormatTitleId(write.title_id)) /
        hash / StageDirectory(write.key.stage);
    std::string filename = write.backend + "-" + write.route + "-" +
                           write.kind + "-abi" +
                           std::to_string(write.generator_abi) + "." +
                           write.extension;
    relative /= filename;
    std::filesystem::path absolute = std::filesystem::u8path(root_path) /
                                     relative;
    std::error_code ec;
    std::filesystem::create_directories(absolute.parent_path(), ec);
    if (ec) {
        if (error) *error = "Unable to create shader artifact directory: " +
                            ec.message();
        return false;
    }

    std::filesystem::path temporary = absolute;
    temporary += ".tmp";
    {
        std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
        if (!file) {
            if (error) *error = "Unable to open shader artifact for writing";
            return false;
        }
        file.write(reinterpret_cast<const char *>(write.bytes.data()),
                   static_cast<std::streamsize>(write.bytes.size()));
        file.flush();
        if (!file) {
            if (error) *error = "Unable to write shader artifact";
            file.close();
            std::filesystem::remove(temporary, ec);
            return false;
        }
    }
#ifdef _WIN32
    std::filesystem::remove(absolute, ec);
    ec.clear();
#endif
    std::filesystem::rename(temporary, absolute, ec);
    if (ec) {
        if (error) *error = "Unable to publish shader artifact: " + ec.message();
        std::filesystem::remove(temporary, ec);
        return false;
    }

    metadata->key = write.key;
    metadata->title_id = write.title_id;
    metadata->backend = write.backend;
    metadata->route = write.route;
    metadata->kind = write.kind;
    metadata->generator_abi = write.generator_abi;
    metadata->xemu_revision = write.xemu_revision;
    metadata->relative_path = relative.generic_u8string();
    metadata->byte_size = write.bytes.size();
    metadata->content_hash = write.content_hash;
    return true;
}

std::string ShaderArtifactStore::RootPath() const
{
    return root_path;
}

bool ShaderArtifactStore::Enabled() const
{
    return config.enabled;
}

} // namespace xemu::shader_browser

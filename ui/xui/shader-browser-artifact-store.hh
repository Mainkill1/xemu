// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "shader-browser-database.hh"

#include <cstdint>
#include <string>
#include <vector>

namespace xemu::shader_browser {

struct ArtifactStoreConfig {
    std::string base_path;
    bool enabled = false;
};

struct ArtifactWrite {
    ShaderKey key;
    uint32_t title_id = 0;
    std::string backend;
    std::string route;
    std::string kind;
    std::string extension;
    uint32_t generator_abi = 0;
    std::string xemu_revision;
    std::string content_hash;
    std::vector<uint8_t> bytes;
};

class ShaderArtifactStore
{
public:
    bool Configure(const ArtifactStoreConfig &config);
    bool Write(const ArtifactWrite &write, ArtifactMetadata *metadata,
               std::string *error) const;
    std::string RootPath() const;
    bool Enabled() const;

private:
    ArtifactStoreConfig config;
    std::string root_path;
};

} // namespace xemu::shader_browser

// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "shader-browser-model.hh"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace xemu::shader_browser {

struct CanonicalRecipe {
    ShaderKey key;
    uint32_t recipe_format_version = 0;
    std::vector<uint8_t> bytes;
    std::vector<ShaderScope> scopes;
};

struct GuestField {
    std::string name;
    std::string value;
};

struct RecipeInspection {
    Stage stage = Stage::Unknown;
    uint32_t instruction_count = 0;
    std::vector<GuestField> fields;
};

// Decodes the accepted field-encoded NV2A recipe without touching renderer
// state. Unknown versions, malformed fields, and hash mismatches fail closed.
bool InspectCanonicalRecipe(const CanonicalRecipe &recipe,
                            RecipeInspection *inspection,
                            std::string *error);

std::string PortableRecipeFilename(const CanonicalRecipe &recipe);
bool SerializePortableRecipe(const CanonicalRecipe &recipe,
                             std::string *json, std::string *error);
bool ParsePortableRecipe(const std::string &json, CanonicalRecipe *recipe,
                         std::string *error);
bool ExportPortableRecipe(const CanonicalRecipe &recipe,
                          const std::filesystem::path &config_directory,
                          std::filesystem::path *exported_path,
                          std::string *error);

} // namespace xemu::shader_browser

// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "shader-browser-preview-model.hh"
#include "shader-browser-recipe-inspector.hh"

namespace xemu::shader_browser {

// Call when a new generated pixel recipe becomes the selected source, so the
// workbench enable initially matches the branch already baked into GLSL.
inline bool PreviewCanonicalAlphaTest(const CanonicalRecipe &recipe,
                                      bool *enabled)
{
    if (!enabled) return false;
    RecipeInspection inspection{};
    std::string error;
    if (!InspectCanonicalRecipe(recipe, &inspection, &error) ||
        inspection.stage != Stage::Pixel) return false;
    for (const auto &field : inspection.fields) {
        if (field.name == "alpha_test") {
            *enabled = field.value == "true";
            return true;
        }
    }
    return false;
}

// The generated fragment source owns its alpha discard. The canonical pixel
// recipe records the enable even for NEVER (unconditional discard) and ALWAYS
// (no emitted branch), so source text alone cannot classify it.
inline bool AdmitPreviewBakedAlphaTest(const PreviewPacket &packet,
                                      std::string *error)
{
    const bool generated =
        packet.source_variant == PreviewSourceVariant::Original &&
        packet.selection.mode != PreviewMode::Replacement;
    // Edited and replacement source owns its discard logic. The stored UI
    // enable is advisory for those variants and does not alter the GLSL.
    if (!generated) return true;
    CanonicalRecipe recipe{};
    recipe.key = packet.selection.shader;
    recipe.recipe_format_version = packet.recipe_format_version;
    recipe.bytes = packet.recipe;
    bool enabled_in_source = false;
    if (!PreviewCanonicalAlphaTest(recipe, &enabled_in_source)) {
        if (error) {
            *error = "Unsupported generated alpha state: canonical pixel "
                     "recipe is missing or invalid";
        }
        return false;
    }
    if (packet.render_state.alpha_test == enabled_in_source) return true;
    if (error) {
        *error = "Unsupported alpha-test enable: selected GLSL bakes alpha "
                 "behavior into its source";
    }
    return false;
}

} // namespace xemu::shader_browser

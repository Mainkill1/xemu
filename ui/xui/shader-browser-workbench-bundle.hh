// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "shader-browser-preview-clock.hh"
#include "shader-browser-preview-model.hh"
#include "shader-browser-recipe-inspector.hh"
#include "shader-browser-workbench-draft.hh"

#include <array>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace xemu::shader_browser {

enum class WorkbenchDependencyStatus : uint8_t {
    Supplied,
    Synthetic,
    Substituted,
    Missing,
};

struct WorkbenchDependency {
    std::string name;
    WorkbenchDependencyStatus status = WorkbenchDependencyStatus::Missing;
    std::string reason;
};

// Owned, inactive experiment data. Parsing or reopening never submits preview
// work or touches Stage 3 rules; the caller must perform fresh admission.
struct WorkbenchExperiment {
    CanonicalRecipe recipe;
    PreviewPacket packet;
    GeneratedSourceSnapshot original;
    std::string edited_text;
    uint64_t edit_revision = 0;
    std::optional<FrozenDraftCompile> successful_compile;
    PreviewClockState clock;
    std::array<std::vector<uint8_t>, 4> owned_textures{};
    std::vector<WorkbenchDependency> dependencies;
};

// Bundle packets store only counted bindings. Restore them into the editor's
// fixed, uniquely targeted slots before the editor submits a full packet.
inline std::array<PreviewInputBinding, kPreviewMaxInputBindings>
ExpandWorkbenchPacketBindings(
    const PreviewPacket &packet,
    std::array<PreviewInputBinding, kPreviewMaxInputBindings> defaults)
{
    for (size_t i = 0; i < packet.binding_count &&
                       i < packet.bindings.size(); ++i) {
        const PreviewInputBinding &binding = packet.bindings[i];
        const size_t target = static_cast<size_t>(binding.target);
        if (target < defaults.size()) defaults[target] = binding;
    }
    return defaults;
}

// A saved packet can count fewer bindings than the editor's fixed slots.
// Compare only the displayed sample's counted targets against both sources.
inline bool SameCountedWorkbenchBindings(
    const PreviewResultKey &displayed, const PreviewPacket &packet,
    const std::array<PreviewInputBinding, kPreviewMaxInputBindings> &dock)
{
    if (displayed.binding_count != packet.binding_count ||
        displayed.binding_count > displayed.bindings.size()) {
        return false;
    }
    for (size_t i = 0; i < displayed.binding_count; ++i) {
        const PreviewInputBinding &binding = displayed.bindings[i];
        const size_t target = static_cast<size_t>(binding.target);
        if (target >= dock.size() || !(binding == packet.bindings[i]) ||
            !(binding == dock[target])) {
            return false;
        }
    }
    return true;
}

constexpr size_t kMaxWorkbenchBundleJsonBytes = 24U * 1024U * 1024U;
bool ValidateWorkbenchExperiment(const WorkbenchExperiment &experiment,
                                 std::string *error);
bool SerializeWorkbenchExperiment(const WorkbenchExperiment &experiment,
                                  std::string *json, std::string *error);
bool ParseWorkbenchExperiment(const std::string &json,
                              WorkbenchExperiment *experiment,
                              std::string *error);
bool ExportWorkbenchExperiment(const WorkbenchExperiment &experiment,
                               const std::filesystem::path &config_directory,
                               std::filesystem::path *exported_path,
                               std::string *error);
bool ReadWorkbenchExperiment(const std::filesystem::path &path,
                             WorkbenchExperiment *experiment,
                             std::string *error);

} // namespace xemu::shader_browser

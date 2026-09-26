// SPDX-License-Identifier: GPL-2.0-or-later
#include "shader-browser-stage3-ui.hh"
#include "shader-browser-session-provider.hh"

#include "common.hh"
#include "viewport-manager.hh"
#include "../xemu-settings.h"

#include <xxhash.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <filesystem>
#include <iterator>

namespace xemu::shader_browser {
namespace {

constexpr char kShaderDragPayloadType[] = "XEMU_SHADER_BROWSER_SHADER_KEY_V1";

uint64_t RuleId(uint32_t title_id, const ShaderKey &key,
                OverrideOrigin origin, const ShaderScope *build_scope)
{
    std::array<uint8_t, 4 + 4 + 4 + kShaderHashBytes + 2 + 4 +
                        kExecutableFingerprintBytes> frame{};
    size_t offset = 0;
    auto put_u32 = [&](uint32_t value) {
        for (unsigned shift = 0; shift < 32; shift += 8) {
            frame[offset++] = static_cast<uint8_t>((value >> shift) & 0xffU);
        }
    };
    put_u32(title_id);
    put_u32(key.hash.version);
    put_u32(static_cast<uint32_t>(key.stage));
    std::memcpy(frame.data() + offset, key.hash.bytes.data(),
                key.hash.bytes.size());
    offset += key.hash.bytes.size();
    frame[offset++] = static_cast<uint8_t>(origin);
    frame[offset++] = build_scope ? 1 : 0;
    if (build_scope) {
        put_u32(build_scope->executable_fingerprint_version);
        std::memcpy(frame.data() + offset,
                    build_scope->executable_fingerprint.data(),
                    build_scope->executable_fingerprint.size());
        offset += build_scope->executable_fingerprint.size();
    }
    uint64_t id = XXH3_64bits_withSeed(frame.data(), offset,
                                      UINT64_C(0x58454d5553484452));
    return id ? id : 1;
}

const ShaderScope *FindBuildScope(const Entry *entry, uint32_t title_id)
{
    if (!entry) return nullptr;
    auto it = std::find_if(entry->scopes.begin(), entry->scopes.end(),
                           [title_id](const ShaderScope &scope) {
                               return scope.title_id == title_id &&
                                      scope.executable_fingerprint_version != 0;
                           });
    return it == entry->scopes.end() ? nullptr : &*it;
}

bool OpenDirectory(const std::string &path, std::string *error)
{
    if (path.empty()) {
        if (error) *error = "Replacement directory is not configured";
        return false;
    }
    GError *glib_error = nullptr;
    char *uri = g_filename_to_uri(path.c_str(), nullptr, &glib_error);
    if (!uri) {
        if (error) {
            *error = glib_error ? glib_error->message :
                                  "Unable to create replacement directory URI";
        }
        g_clear_error(&glib_error);
        return false;
    }
    bool ok = SDL_OpenURL(uri);
    g_free(uri);
    if (!ok && error) {
        *error = std::string("Unable to open replacement directory: ") +
                 SDL_GetError();
    }
    return ok;
}

} // namespace

void ShaderOverrideUi::Refresh(uint32_t current_title_id)
{
    const char *base_path = xemu_settings_get_base_path();
    const std::string base = base_path ? base_path : "";
    if (!configured_) {
        GetReplacementLibrary().Configure(base);
        if (!base.empty()) {
            preset_path_ = (std::filesystem::u8path(base) /
                            "shader-presets" /
                            "shader-preset.json").u8string();
        }
        configured_ = true;
    }
    const bool database_enabled = g_config.shader_browser.database.enabled;
    if (!persistence_configured_ ||
        persistence_enabled_ != database_enabled ||
        persistence_base_path_ != base) {
        GetSavedOverrideRules().Configure(
            base, database_enabled, &GetOverrideStore(),
            &persistence_error_);
        persistence_configured_ = true;
        persistence_enabled_ = database_enabled;
        persistence_base_path_ = base;
        if (!GetSavedOverrideRules().Enabled()) save_rule_ = false;
    }
    RefreshSnapshots(current_title_id);
}

void ShaderOverrideUi::RefreshSnapshots(uint32_t current_title_id)
{
    GetOverrideStore().CopySnapshot(&store_snapshot_);
    OverrideContext context = store_snapshot_.context;
    context.title_id = current_title_id;
    context.executable_fingerprint_version = 0;
    context.executable_fingerprint.fill(0);
    XemuShaderBrowserScope live_scope{};
    xemu_shader_browser_copy_current_scope(&live_scope);
    if (current_title_id && live_scope.title_id == current_title_id) {
        context.executable_fingerprint_version =
            live_scope.executable_fingerprint_version;
        std::memcpy(context.executable_fingerprint.data(),
                    live_scope.executable_fingerprint,
                    context.executable_fingerprint.size());
    }
    if (context.title_id != store_snapshot_.context.title_id ||
        context.executable_fingerprint_version !=
            store_snapshot_.context.executable_fingerprint_version ||
        context.executable_fingerprint !=
            store_snapshot_.context.executable_fingerprint) {
        GetOverrideStore().SetContext(context);
        GetOverrideStore().CopySnapshot(&store_snapshot_);
    }
    GetReplacementLibrary().CopySnapshot(&library_snapshot_);
    if (selected_replacement_id_ != 0 && !SelectedPackage()) {
        selected_replacement_id_ = 0;
    }
    if (selected_replacement_id_ == 0 &&
        !library_snapshot_.packages.empty()) {
        selected_replacement_id_ =
            library_snapshot_.packages.front().descriptor.id;
    }
}

const ReplacementPackageInfo *ShaderOverrideUi::SelectedPackage() const
{
    auto it = std::find_if(
        library_snapshot_.packages.begin(), library_snapshot_.packages.end(),
        [this](const ReplacementPackageInfo &package) {
            return package.descriptor.id == selected_replacement_id_;
        });
    return it == library_snapshot_.packages.end() ? nullptr : &*it;
}

OverrideAction ShaderOverrideUi::SelectedAction() const
{
    if (action_index_ < static_cast<int>(OverrideAction::Normal) ||
        action_index_ > static_cast<int>(OverrideAction::Replacement)) {
        return OverrideAction::Normal;
    }
    return static_cast<OverrideAction>(action_index_);
}

ShaderOverrideRowPresentation ShaderOverrideUi::EvaluateRow(
    const Entry &entry, uint32_t title_id) const
{
    ShaderOverrideRowPresentation result{};
    const ReplacementPackageInfo *package = SelectedPackage();
    if (!package || SelectedAction() != OverrideAction::Replacement) {
        return result;
    }
    result.replacement_selected = true;
    if (store_snapshot_.context.backend == OverrideBackend::Vulkan) {
        result.reason = "Vulkan replacement runtime is not available yet";
        return result;
    }
    if (title_id == 0) {
        result.reason = "shader has no active Xbox TitleID";
        return result;
    }
    result.compatible = IsEntryCompatibleWithReplacement(
        entry, title_id, package->descriptor, store_snapshot_.context.backend,
        &result.reason);
    return result;
}

void ShaderOverrideUi::DrawRowDragSource(const Entry &entry,
                                         uint32_t title_id,
                                         const std::string &display_id)
{
    ShaderOverrideRowPresentation row = EvaluateRow(entry, title_id);
    if (!row.compatible) {
        if (row.replacement_selected && ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Not compatible: %s", row.reason.c_str());
        }
        return;
    }
    if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
        ShaderDragPayload payload = MakeShaderDragPayload(title_id, entry.key);
        ImGui::SetDragDropPayload(kShaderDragPayloadType, &payload,
                                  sizeof(payload));
        ImGui::TextUnformatted("Assign replacement to");
        ImGui::TextUnformatted(display_id.c_str());
        ImGui::EndDragDropSource();
    }
}

bool ShaderOverrideUi::ApplyRule(const ShaderKey &key, uint32_t title_id,
                                 const Entry *entry, OverrideAction action,
                                 uint64_t replacement_id,
                                 std::string *message)
{
    if (!title_id) {
        if (message) *message = "A concrete Xbox TitleID is required";
        return false;
    }
    if (store_snapshot_.context.backend == OverrideBackend::Vulkan &&
        action != OverrideAction::Normal &&
        action != OverrideAction::SkipDraw) {
        if (message) *message = "This Vulkan override action is not available yet";
        return false;
    }
    if (!entry || entry->key != key ||
        std::none_of(entry->scopes.begin(), entry->scopes.end(),
                     [title_id](const ShaderScope &scope) {
                         return scope.title_id == title_id;
                     })) {
        if (message) *message = "Shader is not associated with the active title";
        return false;
    }
    OverrideRule rule{};
    rule.enabled = true;
    rule.title_id = title_id;
    rule.shader = key;
    rule.origin = save_rule_ && GetSavedOverrideRules().Enabled() ?
        OverrideOrigin::Saved : OverrideOrigin::Session;
    rule.priority = 1000;
    rule.action = action;
    rule.replacement_id = replacement_id;
    rule.revision = next_rule_revision_++;
    if (restrict_build_) {
        const ShaderScope *scope = FindBuildScope(entry, title_id);
        if (!scope) {
            if (message) {
                *message = "Current shader has no executable fingerprint for "
                           "build-specific targeting";
            }
            return false;
        }
        rule.restrict_build = true;
        rule.executable_fingerprint_version =
            scope->executable_fingerprint_version;
        rule.executable_fingerprint = scope->executable_fingerprint;
    }
    rule.id = RuleId(title_id, key, rule.origin,
                     rule.restrict_build ? FindBuildScope(entry, title_id) :
                                           nullptr);
    std::string error;
    bool applied = rule.origin == OverrideOrigin::Saved ?
        GetSavedOverrideRules().Save(rule, &GetOverrideStore(), &error) :
        GetOverrideStore().UpsertRule(rule, &error);
    if (!applied) {
        if (message) *message = error;
        return false;
    }
    if (message) {
        *message = std::string("Requested ") + OverrideActionLabel(action) +
                   " for the selected shader";
    }
    RefreshSnapshots(title_id);
    return true;
}

bool ShaderOverrideUi::ApplyReplacement(const ShaderKey &key,
                                        uint32_t title_id,
                                        const Entry *entry,
                                        std::string *message)
{
    const ReplacementPackageInfo *package = SelectedPackage();
    if (!package) {
        if (message) *message = "Choose a replacement package first";
        return false;
    }
    if (store_snapshot_.context.backend == OverrideBackend::Vulkan) {
        if (message) *message = "Vulkan replacement runtime is not available yet";
        return false;
    }
    if (!entry) {
        if (message) *message = "Shader is no longer available";
        return false;
    }
    std::string reason;
    if (entry->key != key ||
        !IsEntryCompatibleWithReplacement(
            *entry, title_id, package->descriptor,
            store_snapshot_.context.backend, &reason)) {
        if (reason.empty()) reason = "shader selection changed";
        if (message) *message = "Replacement is incompatible: " + reason;
        return false;
    }
    return ApplyRule(key, title_id, entry, OverrideAction::Replacement,
                     package->descriptor.id, message);
}

void ShaderOverrideUi::DrawRowContextMenu(const Entry &entry,
                                          uint32_t title_id,
                                          std::string *message)
{
    const ReplacementPackageInfo *package = SelectedPackage();
    if (!package || SelectedAction() != OverrideAction::Replacement) return;
    ShaderOverrideRowPresentation row = EvaluateRow(entry, title_id);
    const std::string &reason = row.reason;
    bool compatible = row.compatible;
    ImGui::Separator();
    ImGui::BeginDisabled(!compatible);
    std::string label = "Assign replacement: " + package->descriptor.name;
    if (ImGui::MenuItem(label.c_str())) {
        ApplyReplacement(entry.key, title_id, &entry, message);
    }
    ImGui::EndDisabled();
    if (!compatible && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui::SetTooltip("%s", reason.c_str());
    }
}

void ShaderOverrideUi::DrawPanel(const Entry &entry,
                                 const std::vector<Entry> &entries,
                                 uint32_t title_id,
                                 std::string *message)
{
    Refresh(title_id);
    ImGui::SeparatorText("Game override");

    static const char *actions[] = {
        "Normal", "Force Uber", "Force Specialized",
        "Skip Draw", "Highlight", "Replacement",
    };
    ImGui::SetNextItemWidth(210.0f * g_viewport_mgr.m_scale);
    ImGui::Combo("Action", &action_index_, actions,
                 static_cast<int>(std::size(actions)));

    if (SelectedAction() == OverrideAction::Replacement) {
        const ReplacementPackageInfo *selected = SelectedPackage();
        const char *preview = selected ? selected->descriptor.name.c_str() :
                                         "No replacement packages";
        ImGui::SetNextItemWidth(-1);
        if (ImGui::BeginCombo("Replacement", preview)) {
            for (const ReplacementPackageInfo &package :
                 library_snapshot_.packages) {
                bool is_selected = package.descriptor.id ==
                                   selected_replacement_id_;
                if (ImGui::Selectable(package.descriptor.name.c_str(),
                                      is_selected)) {
                    selected_replacement_id_ = package.descriptor.id;
                }
                if (is_selected) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }

        ShaderOverrideRowPresentation compatibility =
            EvaluateRow(entry, title_id);
        if (selected && compatibility.compatible) {
            ImGui::TextDisabled("Compatible with selected shader");
        } else if (selected) {
            ImGui::TextWrapped("Incompatible: %s",
                               compatibility.reason.c_str());
        }

        ImVec2 target_size(-1, 54.0f * g_viewport_mgr.m_scale);
        ImGui::Button("Drop a highlighted shader here to assign replacement",
                      target_size);
        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload(
                    kShaderDragPayloadType)) {
                if (payload->DataSize == sizeof(ShaderDragPayload)) {
                    const ShaderDragPayload &drag =
                        *static_cast<const ShaderDragPayload *>(payload->Data);
                    uint32_t dropped_title = 0;
                    ShaderKey dropped_key{};
                    if (!DecodeShaderDragPayload(drag, &dropped_title,
                                                 &dropped_key)) {
                        if (message) *message = "Rejected invalid shader drag payload";
                    } else if (dropped_title != title_id) {
                        if (message) {
                            *message = "Dropped shader belongs to a different title";
                        }
                    } else {
                        int index = FindEntryByKey(entries, dropped_key);
                        const Entry *dropped_entry = index >= 0 ?
                            &entries[static_cast<size_t>(index)] : nullptr;
                        ApplyReplacement(dropped_key, dropped_title,
                                         dropped_entry, message);
                    }
                }
            }
            ImGui::EndDragDropTarget();
        }
    }

    const ShaderScope *build_scope = FindBuildScope(&entry, title_id);
    ImGui::BeginDisabled(!build_scope);
    ImGui::Checkbox("Restrict to current executable build", &restrict_build_);
    ImGui::EndDisabled();
    if (!build_scope && restrict_build_) restrict_build_ = false;

    ImGui::BeginDisabled(!GetSavedOverrideRules().Enabled());
    ImGui::Checkbox("Save rule to shader database", &save_rule_);
    ImGui::EndDisabled();
    if (!GetSavedOverrideRules().Enabled()) save_rule_ = false;

    bool action_supported = true;
    std::string unsupported;
    if (store_snapshot_.context.backend == OverrideBackend::Vulkan &&
        SelectedAction() != OverrideAction::Normal &&
        SelectedAction() != OverrideAction::SkipDraw) {
        action_supported = false;
        unsupported = "This Vulkan override action is not available yet";
    }
    if (entry.key.stage != Stage::Pixel &&
        SelectedAction() != OverrideAction::Normal) {
        action_supported = false;
        unsupported = "Stage 3 v1 runtime actions target pixel shaders only";
    }
    if (SelectedAction() == OverrideAction::ForceUber &&
        store_snapshot_.context.backend == OverrideBackend::OpenGL) {
        action_supported = false;
        unsupported = "Force Uber is not available in the OpenGL renderer";
    }
    if (SelectedAction() == OverrideAction::Replacement) {
        const ReplacementPackageInfo *package = SelectedPackage();
        if (action_supported) {
            action_supported = package && IsEntryCompatibleWithReplacement(
                entry, title_id, package->descriptor,
                store_snapshot_.context.backend, &unsupported);
        }
    }

    ImGui::BeginDisabled(!action_supported);
    if (ImGui::Button("Apply to selected shader")) {
        if (SelectedAction() == OverrideAction::Replacement) {
            ApplyReplacement(entry.key, title_id, &entry, message);
        } else {
            ApplyRule(entry.key, title_id, &entry, SelectedAction(), 0,
                      message);
        }
    }
    ImGui::EndDisabled();
    if (!action_supported && !unsupported.empty()) {
        ImGui::TextWrapped("%s", unsupported.c_str());
    }

    OverrideResolution resolution = GetOverrideStore().Resolve(entry.key);
    ImGui::Text("Rule resolution: %s",
                OverrideResolutionLabel(resolution.status));
    if (!resolution.message.empty()) {
        ImGui::SameLine();
        ImGui::TextDisabled("%s", resolution.message.c_str());
    }
    if (GetSavedOverrideRules().Enabled()) {
        for (const OverrideRule &saved : store_snapshot_.rules) {
            if (saved.title_id != title_id || saved.shader != entry.key ||
                saved.origin != OverrideOrigin::Saved) {
                continue;
            }
            ImGui::PushID(static_cast<int>(saved.id));
            const char *label = saved.restrict_build ?
                "Remove saved rule for this build" :
                "Remove saved rule for all builds";
            if (!ImGui::Button(label)) {
                ImGui::PopID();
                continue;
            }
            std::string error;
            if (!GetSavedOverrideRules().Remove(
                    saved.id, &GetOverrideStore(), &error)) {
                if (message) *message = error;
            } else {
                if (message) *message = "Saved rule removed";
            }
            ImGui::PopID();
            RefreshSnapshots(title_id);
            break;
        }
    }
    if (store_snapshot_.context.backend == OverrideBackend::Unknown) {
        ImGui::TextDisabled(
            "Renderer not yet registered; the rule is retained and will be "
            "resolved when OpenGL or Vulkan publishes its context.");
    }
}

void ShaderOverrideUi::DrawSettings(std::string *message)
{
    Refresh(store_snapshot_.context.title_id);
    ImGui::SeparatorText("Replacement library");
    ImGui::TextWrapped("%s", library_snapshot_.root_path.empty() ?
                       "Replacement folder is not configured" :
                       library_snapshot_.root_path.c_str());
    if (ImGui::Button("Create replacement folder")) {
        std::string error;
        if (!GetReplacementLibrary().EnsureRoot(&error)) {
            if (message) *message = error;
        } else if (message) {
            *message = "Replacement folder is ready";
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Reload replacements")) {
        std::string error;
        bool ok = GetReplacementLibrary().Reload(&GetOverrideStore(), &error);
        RefreshSnapshots(store_snapshot_.context.title_id);
        if (message) {
            *message = ok ? "Replacement packages reloaded" : error;
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Open replacement folder")) {
        std::string error;
        if (!OpenDirectory(library_snapshot_.root_path, &error) && message) {
            *message = error;
        }
    }

    ImGui::Text("Packages: %zu | load errors: %zu",
                library_snapshot_.packages.size(),
                library_snapshot_.errors.size());
    for (const ReplacementLibraryError &error : library_snapshot_.errors) {
        ImGui::BulletText("%s: %s", error.path.c_str(), error.message.c_str());
    }

    bool disabled = store_snapshot_.disabled;
    if (ImGui::Checkbox("Disable all user overrides", &disabled)) {
        GetOverrideStore().SetDisabled(disabled);
        RefreshSnapshots(store_snapshot_.context.title_id);
        if (message) {
            *message = disabled ? "User shader overrides disabled" :
                                 "User shader overrides enabled";
        }
    }
    ImGui::TextDisabled(
        "Replacement source is authored data under shader-replacements/. It "
        "is separate from disposable shader-artifacts/ files.");
    if (!persistence_error_.empty()) {
        ImGui::TextWrapped("Saved rules: %s", persistence_error_.c_str());
    }

    ImGui::SeparatorText("Portable override preset");
    ImGui::InputText("Preset JSON file", &preset_path_);
    ImGui::BeginDisabled(!store_snapshot_.context.title_id);
    if (ImGui::Button("Export current title rules")) {
        std::vector<OverrideRule> rules;
        for (const OverrideRule &rule : store_snapshot_.rules) {
            if (rule.title_id == store_snapshot_.context.title_id) {
                rules.push_back(rule);
            }
        }
        std::string error;
        if (!ExportOverridePreset(preset_path_, rules, &error)) {
            if (message) *message = error;
        } else if (message) {
            *message = "Exported " + std::to_string(rules.size()) +
                       " override rules";
        }
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Import preset")) {
        size_t count = 0;
        std::string error;
        if (!ImportOverridePreset(preset_path_, &GetOverrideStore(),
                                  &count, &error)) {
            if (message) *message = error;
        } else {
            RefreshSnapshots(store_snapshot_.context.title_id);
            if (message) {
                *message = "Imported " + std::to_string(count) +
                           " override rules";
            }
        }
    }
    ImGui::TextDisabled(
        "Presets contain title/build rules and package IDs. Copy authored "
        "replacement packages separately when sharing a preset.");
}

ShaderOverrideUi &GetShaderOverrideUi()
{
    static ShaderOverrideUi ui;
    return ui;
}

} // namespace xemu::shader_browser

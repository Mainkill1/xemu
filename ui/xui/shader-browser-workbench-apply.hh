// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "shader-browser-details-model.hh"
#include "shader-browser-overrides.hh"
#include "shader-browser-preview-model.hh"

#include <filesystem>
#include <string>

namespace xemu::shader_browser {

class OverrideStore;
class ReplacementLibrary;
class SavedOverrideRules;

struct DraftGameApplyRequest {
    ShaderKey key;
    ShaderScope scope;
    OverrideBackend backend = OverrideBackend::Unknown;
    HostSourceStage source_stage = HostSourceStage::Unknown;
    uint32_t interface_abi = 0;
    uint64_t draft_id = 0;
    uint64_t draft_revision = 0;
    uint64_t attempted_revision = 0;
    uint64_t successful_revision = 0;
    uint64_t preview_submission_id = 0;
    uint64_t successful_submission_id = 0;
    PreviewDigest successful_source_digest{};
    bool compiled_success = false;
    std::string source;
};

struct WorkbenchAppliedRule {
    uint64_t id = 0;
    ShaderKey key;
    ShaderScope scope;
    uint64_t replacement_id = 0;
    uint64_t saved_rule_id = 0;
    uint64_t revision = 0;
};

struct WorkbenchSavedReplacement {
    DraftGameApplyRequest request;
    std::string logical_id;
    uint64_t replacement_id = 0;
    uint64_t content_revision = 0;
};

bool EvaluateDraftSave(const DraftGameApplyRequest &request, const Entry &entry,
                       std::string *reason);
bool EvaluateDraftGameApply(const DraftGameApplyRequest &request,
                            const Entry &entry, const OverrideContext &context,
                            std::string *reason);
bool SaveWorkbenchDraft(const DraftGameApplyRequest &request,
                        const Entry &entry, OverrideStore *store,
                        ReplacementLibrary *library,
                        const std::filesystem::path &config_directory,
                        WorkbenchSavedReplacement *saved, std::string *error);
bool SaveWorkbenchDraftForSettingsPath(const DraftGameApplyRequest &request,
                                       const Entry &entry, OverrideStore *store,
                                       ReplacementLibrary *library,
                                       const char *settings_path,
                                       WorkbenchSavedReplacement *saved,
                                       std::string *error);
bool EnableWorkbenchReplacement(const WorkbenchSavedReplacement &saved,
                                const Entry &entry, OverrideStore *store,
                                SavedOverrideRules *saved_rules, bool persist,
                                WorkbenchAppliedRule *applied,
                                std::string *error);
bool DisableWorkbenchReplacement(const WorkbenchAppliedRule &applied,
                                 OverrideStore *store,
                                 SavedOverrideRules *saved_rules,
                                 std::string *error);
bool ApplyWorkbenchDraft(const DraftGameApplyRequest &request,
                         const Entry &entry, OverrideStore *store,
                         ReplacementLibrary *library,
                         const std::filesystem::path &config_directory,
                         WorkbenchAppliedRule *applied, std::string *error);
bool RestoreWorkbenchDraft(const WorkbenchAppliedRule &applied,
                           OverrideStore *store,
                           std::string *error);

} // namespace xemu::shader_browser

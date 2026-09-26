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
};

bool EvaluateDraftGameApply(const DraftGameApplyRequest &request,
                            const Entry &entry, const OverrideContext &context,
                            std::string *reason);
bool ApplyWorkbenchDraft(const DraftGameApplyRequest &request,
                         const Entry &entry, OverrideStore *store,
                         ReplacementLibrary *library,
                         const std::filesystem::path &config_directory,
                         WorkbenchAppliedRule *applied, std::string *error);
bool RestoreWorkbenchDraft(const WorkbenchAppliedRule &applied,
                           OverrideStore *store,
                           std::string *error);

} // namespace xemu::shader_browser

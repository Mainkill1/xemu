// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "shader-browser-details-model.hh"
#include "shader-browser-preview-model.hh"

#include <cstdint>
#include <optional>
#include <string>

namespace xemu::shader_browser {

constexpr uint64_t kWorkbenchDraftDebounceNs = UINT64_C(400000000);

struct GeneratedSourceSnapshot {
    ShaderKey key;
    ShaderScope scope;
    PreviewBackend backend = PreviewBackend::Unknown;
    HostSourceStage stage = HostSourceStage::Unknown;
    Route route = Route::Unknown;
    bool resident = false;
    // Detail sources are not tagged with the build that created them.
    bool build_scope_verified = false;
    uint32_t generator_abi = 0;
    uint32_t interface_abi = 0;
    PreviewDigest digest{};
    std::string text;
};

struct FrozenDraftCompile {
    uint64_t draft_id = 0;
    uint64_t revision = 0;
    uint64_t submission_id = 0;
    PreviewDigest digest{};
    std::string source;
};

class WorkbenchDraft
{
public:
    bool Create(const GeneratedSourceSnapshot &base, uint64_t id,
                std::string *error);
    bool MatchesSource(const GeneratedSourceSnapshot &source) const;
    bool Edit(const std::string &text, uint64_t now_ns);
    std::optional<FrozenDraftCompile> FreezeCompile(uint64_t now_ns,
                                                    bool manual);
    bool MarkCompiled(const FrozenDraftCompile &token, bool success,
                      const std::string &diagnostics);
    const GeneratedSourceSnapshot &Base() const { return base_; }
    const std::string &Text() const { return text_; }
    uint64_t Id() const { return id_; }
    uint64_t Revision() const { return revision_; }
    uint64_t AttemptedRevision() const { return attempted_revision_; }
    uint64_t LastSuccessfulRevision() const { return successful_revision_; }
    const std::optional<FrozenDraftCompile> &LastSuccessfulCompile() const
    {
        return successful_compile_;
    }
    const std::string &Diagnostics() const { return diagnostics_; }
    std::string StatusLabel() const;

private:
    GeneratedSourceSnapshot base_;
    std::string text_;
    uint64_t id_ = 0;
    uint64_t revision_ = 0;
    uint64_t submitted_revision_ = 0;
    uint64_t submitted_attempt_id_ = 0;
    uint64_t completed_attempt_id_ = 0;
    uint64_t attempted_revision_ = 0;
    uint64_t successful_revision_ = 0;
    uint64_t edited_ns_ = 0;
    bool failed_ = false;
    std::string diagnostics_;
    std::optional<FrozenDraftCompile> submitted_compile_;
    std::optional<FrozenDraftCompile> successful_compile_;
};

} // namespace xemu::shader_browser

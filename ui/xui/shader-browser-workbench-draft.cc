// SPDX-License-Identifier: GPL-2.0-or-later
#include "shader-browser-workbench-draft.hh"

#include <limits>

namespace xemu::shader_browser {

bool WorkbenchDraft::Create(const GeneratedSourceSnapshot &base, uint64_t id,
                            std::string *error)
{
    if (!id || !base.scope.title_id || !base.key.hash.version ||
        base.backend == PreviewBackend::Unknown ||
        base.stage != HostSourceStage::Fragment ||
        base.key.stage != Stage::Pixel || !base.generator_abi ||
        !base.interface_abi || base.text.empty() ||
        base.text.size() > kPreviewMaxSourceBytes ||
        base.text.find('\0') != std::string::npos ||
        base.digest != ComputePreviewDigest(
            reinterpret_cast<const uint8_t *>(base.text.data()),
            base.text.size())) {
        if (error) *error = "Generated fragment source identity is invalid";
        return false;
    }
    base_ = base;
    text_ = base.text;
    id_ = id;
    revision_ = 1;
    submitted_revision_ = 0;
    submitted_attempt_id_ = 0;
    completed_attempt_id_ = 0;
    attempted_revision_ = 0;
    successful_revision_ = 0;
    edited_ns_ = 0;
    failed_ = false;
    diagnostics_.clear();
    submitted_compile_.reset();
    successful_compile_.reset();
    if (error) error->clear();
    return true;
}

bool WorkbenchDraft::Edit(const std::string &text, uint64_t now_ns)
{
    if (!id_ || text == text_ || text.size() > kPreviewMaxSourceBytes ||
        text.find('\0') != std::string::npos ||
        revision_ == std::numeric_limits<uint64_t>::max()) {
        return false;
    }
    text_ = text;
    ++revision_;
    edited_ns_ = now_ns;
    return true;
}

std::optional<FrozenDraftCompile> WorkbenchDraft::FreezeCompile(
    uint64_t now_ns, bool manual)
{
    const bool new_revision = revision_ > submitted_revision_;
    const bool failed_retry = manual && !new_revision && failed_ &&
        submitted_attempt_id_ == completed_attempt_id_;
    if (!id_ || submitted_attempt_id_ ==
                    std::numeric_limits<uint64_t>::max() ||
        (!new_revision && !failed_retry) ||
        (!manual && (now_ns < edited_ns_ ||
                     now_ns - edited_ns_ < kWorkbenchDraftDebounceNs))) {
        return std::nullopt;
    }
    FrozenDraftCompile frozen{};
    frozen.draft_id = id_;
    frozen.revision = revision_;
    frozen.submission_id = ++submitted_attempt_id_;
    frozen.source = text_;
    frozen.digest = ComputePreviewDigest(
        reinterpret_cast<const uint8_t *>(text_.data()), text_.size());
    submitted_revision_ = revision_;
    submitted_compile_ = frozen;
    return frozen;
}

bool WorkbenchDraft::MarkCompiled(const FrozenDraftCompile &token,
                                 bool success,
                                 const std::string &diagnostics)
{
    if (!submitted_compile_ || !token.draft_id ||
        token.draft_id != submitted_compile_->draft_id ||
        token.revision != submitted_compile_->revision ||
        token.submission_id != submitted_compile_->submission_id ||
        token.submission_id <= completed_attempt_id_ ||
        token.digest != submitted_compile_->digest ||
        token.source != submitted_compile_->source)
        return false;
    attempted_revision_ = token.revision;
    completed_attempt_id_ = token.submission_id;
    failed_ = !success;
    diagnostics_ = diagnostics.substr(0, 8192);
    if (success) {
        successful_revision_ = token.revision;
        successful_compile_ = *submitted_compile_;
    }
    return true;
}

std::string WorkbenchDraft::StatusLabel() const
{
    if (!id_) return "No editable draft";
    auto last_compiled = [this]() {
        return successful_revision_ ?
            "; last compiled r" + std::to_string(successful_revision_) :
            std::string{};
    };
    if (revision_ > submitted_revision_)
        return "Draft r" + std::to_string(revision_) + " uncompiled" +
               last_compiled();
    if (submitted_attempt_id_ > completed_attempt_id_)
        return "Draft r" + std::to_string(submitted_revision_) +
               " requested" + last_compiled();
    if (failed_ && successful_revision_)
        return "Draft r" + std::to_string(attempted_revision_) +
               " failed; last compiled r" +
               std::to_string(successful_revision_);
    if (failed_)
        return "Draft r" + std::to_string(attempted_revision_) + " failed";
    if (successful_revision_)
        return "Compiled draft r" + std::to_string(successful_revision_);
    return "Draft r" + std::to_string(revision_) + " not compiled";
}

} // namespace xemu::shader_browser

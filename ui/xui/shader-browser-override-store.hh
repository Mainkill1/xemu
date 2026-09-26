// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "shader-browser-overrides.hh"

#include <memory>
#include <atomic>
#include <mutex>
#include <unordered_map>

namespace xemu::shader_browser {

constexpr size_t kMaxReplacementSourceBytes = 4U * 1024U * 1024U;

struct ReplacementPayload {
    ReplacementDescriptor descriptor;
    std::string opengl_source;
    std::string vulkan_source;
};

struct OverrideStoreSnapshot {
    uint64_t generation = 0;
    bool disabled = false;
    OverrideContext context;
    size_t resolved_rule_count = 0;
    std::vector<OverrideRule> rules;
    std::vector<ReplacementDescriptor> replacements;
};

class OverrideStore
{
public:
    void SetContext(const OverrideContext &context);
    void SetDisabled(bool disabled);
    bool Disabled() const;

    bool UpsertReplacement(const ReplacementPayload &payload,
                           std::string *error);
    bool RemoveReplacement(uint64_t replacement_id);
    std::shared_ptr<const ReplacementPayload> AcquireReplacement(
        uint64_t replacement_id, uint64_t content_revision,
        OverrideBackend backend) const;

    bool UpsertRule(const OverrideRule &rule, std::string *error);
    bool RemoveRule(uint64_t rule_id);
    void ClearSessionRules();
    void ClearSavedRules();

    OverrideResolution Resolve(const ShaderKey &key) const;
    uint64_t Generation() const;
    bool HasActiveRules() const;
    void CopySnapshot(OverrideStoreSnapshot *snapshot) const;
    void Clear();

private:
    bool ValidateReplacement(const ReplacementPayload &payload,
                             std::string *error) const;
    bool ValidateRule(const OverrideRule &rule, std::string *error) const;
    void RebuildLocked();

    mutable std::mutex mutex_;
    OverrideContext context_;
    bool disabled_ = false;
    uint64_t generation_ = 0;
    std::atomic<bool> has_active_rules_{false};
    std::unordered_map<uint64_t, OverrideRule> rules_;
    std::unordered_map<uint64_t, std::shared_ptr<const ReplacementPayload>>
        replacements_;
    OverrideIndex index_;
};

OverrideStore &GetOverrideStore();

} // namespace xemu::shader_browser

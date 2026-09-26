// SPDX-License-Identifier: GPL-2.0-or-later
#include "shader-browser-overrides.hh"

#include <algorithm>
#include <tuple>

namespace xemu::shader_browser {
namespace {

int OriginRank(OverrideOrigin origin)
{
    switch (origin) {
    case OverrideOrigin::Session: return 3;
    case OverrideOrigin::Saved: return 2;
    case OverrideOrigin::Imported: return 1;
    }
    return 0;
}

bool FingerprintMatches(const OverrideRule &rule,
                        const OverrideContext &context)
{
    if (!rule.restrict_build) {
        return true;
    }
    return rule.executable_fingerprint_version ==
               context.executable_fingerprint_version &&
           rule.executable_fingerprint == context.executable_fingerprint;
}

bool EquivalentPolicy(const OverrideRule &a, const OverrideRule &b)
{
    return a.action == b.action &&
           a.replacement_id == b.replacement_id &&
           a.draw_condition.mask == b.draw_condition.mask &&
           a.draw_condition.element_count_min ==
               b.draw_condition.element_count_min &&
           a.draw_condition.element_count_max ==
               b.draw_condition.element_count_max &&
           a.draw_condition.min_element == b.draw_condition.min_element &&
           a.draw_condition.max_element == b.draw_condition.max_element &&
           a.draw_condition.primitive_mode ==
               b.draw_condition.primitive_mode;
}

std::tuple<int, int, int32_t> RuleRank(const OverrideRule &rule)
{
    return { OriginRank(rule.origin), rule.restrict_build ? 1 : 0,
             rule.priority };
}

bool ActionSupported(OverrideAction action, OverrideBackend backend,
                     std::string *reason)
{
    if (backend == OverrideBackend::Vulkan &&
        action != OverrideAction::Normal &&
        action != OverrideAction::SkipDraw &&
        action != OverrideAction::ForceUber &&
        action != OverrideAction::ForceSpecialized) {
        if (reason) *reason = "This Vulkan override action is not available yet";
        return false;
    }
    if (action == OverrideAction::ForceUber &&
        backend != OverrideBackend::Vulkan) {
        if (reason) {
            *reason = "Force Uber is only supported by the Vulkan renderer";
        }
        return false;
    }
    if (backend == OverrideBackend::Unknown &&
        action != OverrideAction::Normal &&
        action != OverrideAction::SkipDraw &&
        action != OverrideAction::Highlight) {
        if (reason) *reason = "The active renderer is unknown";
        return false;
    }
    return true;
}

} // namespace

ShaderDragPayload MakeShaderDragPayload(uint32_t title_id,
                                        const ShaderKey &key)
{
    ShaderDragPayload payload{};
    payload.title_id = title_id;
    payload.identity_version = key.hash.version;
    payload.shader_hash = key.hash.bytes;
    payload.stage = static_cast<uint32_t>(key.stage);
    return payload;
}

bool DecodeShaderDragPayload(const ShaderDragPayload &payload,
                             uint32_t *title_id, ShaderKey *key)
{
    if (!title_id || !key || payload.title_id == 0 ||
        payload.identity_version == 0 || payload.stage == 0 ||
        payload.stage > static_cast<uint32_t>(Stage::Unknown)) {
        return false;
    }
    key->hash.version = payload.identity_version;
    key->hash.bytes = payload.shader_hash;
    key->stage = static_cast<Stage>(payload.stage);
    if (key->stage == Stage::Any || key->stage == Stage::Unknown) {
        return false;
    }
    *title_id = payload.title_id;
    return true;
}

bool DrawCondition::Matches(const DrawFacts &facts) const
{
    if ((mask & DrawConditionElementCount) &&
        (facts.element_count < element_count_min ||
         facts.element_count > element_count_max)) {
        return false;
    }
    if ((mask & DrawConditionElementRange) &&
        (facts.min_element != min_element ||
         facts.max_element != max_element)) {
        return false;
    }
    if ((mask & DrawConditionPrimitive) &&
        facts.primitive_mode != primitive_mode) {
        return false;
    }
    return true;
}

uint32_t BackendMaskFor(OverrideBackend backend)
{
    switch (backend) {
    case OverrideBackend::OpenGL: return BackendOpenGL;
    case OverrideBackend::Vulkan: return BackendVulkan;
    case OverrideBackend::Unknown: return BackendNone;
    }
    return BackendNone;
}

bool IsReplacementCompatible(const ShaderKey &shader,
                             const ReplacementDescriptor &replacement,
                             OverrideBackend backend,
                             std::string *reason)
{
    auto fail = [reason](const char *message) {
        if (reason) *reason = message;
        return false;
    };
    if (!replacement.enabled) {
        return fail("replacement is disabled");
    }
    if (replacement.id == 0 || replacement.content_revision == 0) {
        return fail("replacement payload is incomplete");
    }
    if (replacement.interface_version != 1) {
        return fail("replacement interface version is unsupported");
    }
    if (replacement.stage != shader.stage) {
        return fail("replacement stage does not match the shader stage");
    }
    if (shader.stage != Stage::Pixel) {
        return fail("Stage 3 v1 supports full pixel/fragment replacements only");
    }
    const uint32_t mask = BackendMaskFor(backend);
    if (backend == OverrideBackend::Unknown) {
        if ((replacement.backend_mask & (BackendOpenGL | BackendVulkan)) == 0) {
            return fail("replacement has no supported renderer payload");
        }
    } else if (!(replacement.backend_mask & mask)) {
        return fail("replacement has no payload for the active renderer");
    }
    if (replacement.entry_point.empty()) {
        return fail("replacement entry point is empty");
    }
    if (reason) reason->clear();
    return true;
}

bool IsEntryCompatibleWithReplacement(const Entry &entry,
                                      uint32_t title_id,
                                      const ReplacementDescriptor &replacement,
                                      OverrideBackend backend,
                                      std::string *reason)
{
    if (title_id != 0) {
        const bool scoped = std::any_of(
            entry.scopes.begin(), entry.scopes.end(),
            [title_id](const ShaderScope &scope) {
                return scope.title_id == title_id;
            });
        if (!scoped) {
            if (reason) *reason = "shader is not associated with the active title";
            return false;
        }
    }
    return IsReplacementCompatible(entry.key, replacement, backend, reason);
}

const char *OverrideActionLabel(OverrideAction action)
{
    switch (action) {
    case OverrideAction::Normal: return "Normal";
    case OverrideAction::ForceUber: return "Force Uber";
    case OverrideAction::ForceSpecialized: return "Force Specialized";
    case OverrideAction::SkipDraw: return "Skip Draw";
    case OverrideAction::Highlight: return "Highlight";
    case OverrideAction::Replacement: return "Replacement";
    }
    return "Unknown";
}

const char *OverrideResolutionLabel(OverrideResolutionStatus status)
{
    switch (status) {
    case OverrideResolutionStatus::None: return "No override";
    case OverrideResolutionStatus::Matched: return "Matched";
    case OverrideResolutionStatus::Conflict: return "Conflict";
    case OverrideResolutionStatus::Incompatible: return "Incompatible";
    }
    return "Unknown";
}

void OverrideIndex::Rebuild(
    const OverrideContext &new_context,
    const std::vector<OverrideRule> &rules,
    const std::vector<ReplacementDescriptor> &replacements)
{
    context = new_context;
    resolved.clear();
    ++generation;

    std::unordered_map<uint64_t, const ReplacementDescriptor *> payloads;
    for (const ReplacementDescriptor &replacement : replacements) {
        if (replacement.id != 0) {
            payloads.emplace(replacement.id, &replacement);
        }
    }

    std::unordered_map<ShaderKey, std::vector<const OverrideRule *>,
                       ShaderKeyHash> candidates;
    for (const OverrideRule &rule : rules) {
        if (!rule.enabled || rule.title_id == 0 ||
            rule.title_id != context.title_id ||
            !FingerprintMatches(rule, context)) {
            continue;
        }
        candidates[rule.shader].push_back(&rule);
    }

    for (const auto &pair : candidates) {
        const ShaderKey &key = pair.first;
        const std::vector<const OverrideRule *> &group = pair.second;
        const OverrideRule *winner = nullptr;
        auto winner_rank = std::tuple<int, int, int32_t>{-1, -1, 0};
        bool conflict = false;

        for (const OverrideRule *rule : group) {
            const auto rank = RuleRank(*rule);
            if (!winner || rank > winner_rank) {
                winner = rule;
                winner_rank = rank;
                conflict = false;
            } else if (rank == winner_rank &&
                       !EquivalentPolicy(*winner, *rule)) {
                conflict = true;
            }
        }

        OverrideResolution resolution{};
        if (!winner) {
            continue;
        }
        if (conflict) {
            resolution.status = OverrideResolutionStatus::Conflict;
            resolution.message =
                "Two rules have equal precedence but request different actions";
            resolved.emplace(key, std::move(resolution));
            continue;
        }

        std::string reason;
        if (winner->action != OverrideAction::Normal &&
            key.stage != Stage::Pixel) {
            resolution.status = OverrideResolutionStatus::Incompatible;
            resolution.message =
                "Stage 3 v1 runtime actions target pixel shaders only";
            resolved.emplace(key, std::move(resolution));
            continue;
        }
        if (!ActionSupported(winner->action, context.backend, &reason)) {
            resolution.status = OverrideResolutionStatus::Incompatible;
            resolution.message = reason;
            resolved.emplace(key, std::move(resolution));
            continue;
        }

        const ReplacementDescriptor *replacement = nullptr;
        if (winner->action == OverrideAction::Replacement) {
            auto it = payloads.find(winner->replacement_id);
            if (it == payloads.end()) {
                resolution.status = OverrideResolutionStatus::Incompatible;
                resolution.message = "replacement payload was not found";
                resolved.emplace(key, std::move(resolution));
                continue;
            }
            replacement = it->second;
            if (!IsReplacementCompatible(key, *replacement, context.backend,
                                         &reason)) {
                resolution.status = OverrideResolutionStatus::Incompatible;
                resolution.message = reason;
                resolved.emplace(key, std::move(resolution));
                continue;
            }
        }

        resolution.status = OverrideResolutionStatus::Matched;
        resolution.policy.action = winner->action;
        resolution.policy.rule_id = winner->id;
        resolution.policy.rule_revision = winner->revision;
        resolution.policy.replacement_id = winner->replacement_id;
        resolution.policy.replacement_revision =
            replacement ? replacement->content_revision : 0;
        resolution.policy.draw_condition = winner->draw_condition;
        resolution.message = OverrideActionLabel(winner->action);
        resolved.emplace(key, std::move(resolution));
    }
}

OverrideResolution OverrideIndex::Resolve(const ShaderKey &key) const
{
    auto it = resolved.find(key);
    if (it == resolved.end()) {
        return {};
    }
    return it->second;
}

} // namespace xemu::shader_browser

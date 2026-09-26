// SPDX-License-Identifier: GPL-2.0-or-later
#include "shader-browser-override-store.hh"
#include "shader-browser-override-runtime.h"

#include <algorithm>
#include <cstring>

namespace xemu::shader_browser {
namespace {

bool ContainsNul(const std::string &value)
{
    return value.find('\0') != std::string::npos;
}

OverrideBackend BackendFromWire(uint32_t backend)
{
    switch (backend) {
    case XEMU_SHADER_OVERRIDE_BACKEND_OPENGL:
        return OverrideBackend::OpenGL;
    case XEMU_SHADER_OVERRIDE_BACKEND_VULKAN:
        return OverrideBackend::Vulkan;
    default:
        return OverrideBackend::Unknown;
    }
}

struct RetainedSource {
    std::shared_ptr<const ReplacementPayload> payload;
    const std::string *source = nullptr;
};

} // namespace

void OverrideStore::SetContext(const OverrideContext &context)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (context_.title_id == context.title_id &&
        context_.executable_fingerprint_version ==
            context.executable_fingerprint_version &&
        context_.executable_fingerprint == context.executable_fingerprint &&
        context_.backend == context.backend) {
        return;
    }
    context_ = context;
    RebuildLocked();
}

void OverrideStore::SetDisabled(bool disabled)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (disabled_ == disabled) {
        return;
    }
    disabled_ = disabled;
    RebuildLocked();
}

bool OverrideStore::Disabled() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return disabled_;
}

bool OverrideStore::ValidateReplacement(const ReplacementPayload &payload,
                                        std::string *error) const
{
    auto fail = [error](const char *message) {
        if (error) *error = message;
        return false;
    };
    const ReplacementDescriptor &descriptor = payload.descriptor;
    if (descriptor.id == 0 || descriptor.content_revision == 0) {
        return fail("Replacement ID and content revision must be non-zero");
    }
    if (descriptor.name.empty() || ContainsNul(descriptor.name)) {
        return fail("Replacement name is empty or contains NUL");
    }
    if (descriptor.stage != Stage::Pixel ||
        descriptor.interface_version != 1) {
        return fail("Stage 3 v1 accepts interface-v1 pixel replacements only");
    }
    if (descriptor.entry_point.empty() || ContainsNul(descriptor.entry_point)) {
        return fail("Replacement entry point is empty or contains NUL");
    }
    if (payload.opengl_source.size() > kMaxReplacementSourceBytes ||
        payload.vulkan_source.size() > kMaxReplacementSourceBytes) {
        return fail("Replacement source exceeds the 4 MiB per-backend limit");
    }
    if (ContainsNul(payload.opengl_source) ||
        ContainsNul(payload.vulkan_source)) {
        return fail("Replacement source contains embedded NUL");
    }
    if ((descriptor.backend_mask & BackendOpenGL) &&
        payload.opengl_source.empty()) {
        return fail("OpenGL payload was declared but no source was supplied");
    }
    if ((descriptor.backend_mask & BackendVulkan) &&
        payload.vulkan_source.empty()) {
        return fail("Vulkan payload was declared but no source was supplied");
    }
    if ((descriptor.backend_mask & (BackendOpenGL | BackendVulkan)) == 0) {
        return fail("Replacement has no supported backend payload");
    }
    return true;
}

bool OverrideStore::UpsertReplacement(const ReplacementPayload &payload,
                                      std::string *error)
{
    if (!ValidateReplacement(payload, error)) {
        return false;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    replacements_[payload.descriptor.id] =
        std::make_shared<const ReplacementPayload>(payload);
    RebuildLocked();
    return true;
}

bool OverrideStore::RemoveReplacement(uint64_t replacement_id)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (!replacements_.erase(replacement_id)) {
        return false;
    }
    RebuildLocked();
    return true;
}

std::shared_ptr<const ReplacementPayload> OverrideStore::AcquireReplacement(
    uint64_t replacement_id, uint64_t content_revision,
    OverrideBackend backend) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = replacements_.find(replacement_id);
    if (it == replacements_.end() ||
        it->second->descriptor.content_revision != content_revision ||
        !(it->second->descriptor.backend_mask & BackendMaskFor(backend))) {
        return {};
    }
    return it->second;
}

bool OverrideStore::ValidateRule(const OverrideRule &rule,
                                 std::string *error) const
{
    auto fail = [error](const char *message) {
        if (error) *error = message;
        return false;
    };
    if (rule.id == 0 || rule.title_id == 0) {
        return fail("Override rule ID and TitleID must be non-zero");
    }
    if (rule.shader.stage == Stage::Any ||
        rule.shader.stage == Stage::Unknown) {
        return fail("Override rule requires a concrete shader stage");
    }
    if (rule.restrict_build &&
        rule.executable_fingerprint_version == 0) {
        return fail("Build-specific rule is missing a fingerprint version");
    }
    if (rule.action == OverrideAction::Replacement &&
        rule.replacement_id == 0) {
        return fail("Replacement rule is missing its replacement ID");
    }
    if ((rule.draw_condition.mask & DrawConditionElementCount) &&
        rule.draw_condition.element_count_min >
            rule.draw_condition.element_count_max) {
        return fail("Draw element-count range is inverted");
    }
    return true;
}

bool OverrideStore::UpsertRule(const OverrideRule &rule, std::string *error)
{
    if (!ValidateRule(rule, error)) {
        return false;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    rules_[rule.id] = rule;
    RebuildLocked();
    return true;
}

bool OverrideStore::RemoveRule(uint64_t rule_id)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (!rules_.erase(rule_id)) {
        return false;
    }
    RebuildLocked();
    return true;
}

void OverrideStore::ClearSessionRules()
{
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto it = rules_.begin(); it != rules_.end();) {
        if (it->second.origin == OverrideOrigin::Session) {
            it = rules_.erase(it);
        } else {
            ++it;
        }
    }
    RebuildLocked();
}

void OverrideStore::RebuildLocked()
{
    ++generation_;
    if (disabled_) {
        index_.Rebuild(context_, {}, {});
        return;
    }
    std::vector<OverrideRule> rules;
    rules.reserve(rules_.size());
    for (const auto &pair : rules_) {
        rules.push_back(pair.second);
    }
    std::vector<ReplacementDescriptor> replacements;
    replacements.reserve(replacements_.size());
    for (const auto &pair : replacements_) {
        replacements.push_back(pair.second->descriptor);
    }
    index_.Rebuild(context_, rules, replacements);
}

OverrideResolution OverrideStore::Resolve(const ShaderKey &key) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (disabled_) {
        return {};
    }
    return index_.Resolve(key);
}

uint64_t OverrideStore::Generation() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return generation_;
}

void OverrideStore::CopySnapshot(OverrideStoreSnapshot *snapshot) const
{
    if (!snapshot) {
        return;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    snapshot->generation = generation_;
    snapshot->disabled = disabled_;
    snapshot->context = context_;
    snapshot->resolved_rule_count = index_.RuleCount();
    snapshot->rules.clear();
    snapshot->rules.reserve(rules_.size());
    for (const auto &pair : rules_) {
        snapshot->rules.push_back(pair.second);
    }
    std::sort(snapshot->rules.begin(), snapshot->rules.end(),
              [](const OverrideRule &a, const OverrideRule &b) {
                  return a.id < b.id;
              });
    snapshot->replacements.clear();
    snapshot->replacements.reserve(replacements_.size());
    for (const auto &pair : replacements_) {
        snapshot->replacements.push_back(pair.second->descriptor);
    }
    std::sort(snapshot->replacements.begin(), snapshot->replacements.end(),
              [](const ReplacementDescriptor &a,
                 const ReplacementDescriptor &b) { return a.id < b.id; });
}

void OverrideStore::Clear()
{
    std::lock_guard<std::mutex> lock(mutex_);
    rules_.clear();
    replacements_.clear();
    context_ = {};
    disabled_ = false;
    RebuildLocked();
}

OverrideStore &GetOverrideStore()
{
    static OverrideStore store;
    return store;
}

} // namespace xemu::shader_browser

using namespace xemu::shader_browser;

extern "C" {

void xemu_shader_override_set_context(
    uint32_t title_id, uint32_t executable_fingerprint_version,
    const uint8_t executable_fingerprint[
        XEMU_SHADER_BROWSER_EXECUTABLE_FINGERPRINT_BYTES],
    uint32_t backend)
{
    OverrideContext context{};
    context.title_id = title_id;
    context.executable_fingerprint_version = executable_fingerprint_version;
    if (executable_fingerprint) {
        std::memcpy(context.executable_fingerprint.data(),
                    executable_fingerprint,
                    context.executable_fingerprint.size());
    }
    context.backend = BackendFromWire(backend);
    GetOverrideStore().SetContext(context);
}

uint64_t xemu_shader_override_generation(void)
{
    return GetOverrideStore().Generation();
}

int xemu_shader_override_resolve(
    uint32_t identity_version,
    const uint8_t identity_hash[XEMU_SHADER_BROWSER_HASH_BYTES],
    uint32_t stage, XemuShaderOverridePolicy *policy)
{
    if (!identity_hash || !policy || stage == 0 || stage > 5) {
        return 0;
    }
    ShaderKey key{};
    key.hash.version = identity_version;
    std::memcpy(key.hash.bytes.data(), identity_hash, key.hash.bytes.size());
    key.stage = static_cast<Stage>(stage);
    OverrideResolution resolved = GetOverrideStore().Resolve(key);
    if (resolved.status != OverrideResolutionStatus::Matched) {
        std::memset(policy, 0, sizeof(*policy));
        policy->generation = GetOverrideStore().Generation();
        return 0;
    }
    const RuntimePolicy &source = resolved.policy;
    std::memset(policy, 0, sizeof(*policy));
    policy->generation = GetOverrideStore().Generation();
    policy->action = static_cast<uint32_t>(source.action);
    policy->rule_id = source.rule_id;
    policy->rule_revision = source.rule_revision;
    policy->replacement_id = source.replacement_id;
    policy->replacement_revision = source.replacement_revision;
    policy->draw_condition_mask = source.draw_condition.mask;
    policy->element_count_min = source.draw_condition.element_count_min;
    policy->element_count_max = source.draw_condition.element_count_max;
    policy->min_element = source.draw_condition.min_element;
    policy->max_element = source.draw_condition.max_element;
    policy->primitive_mode = source.draw_condition.primitive_mode;
    return 1;
}

int xemu_shader_override_policy_matches_draw(
    const XemuShaderOverridePolicy *policy,
    const XemuShaderOverrideDrawFacts *facts)
{
    if (!policy || !facts) {
        return 0;
    }
    if ((policy->draw_condition_mask & facts->available_mask) !=
        policy->draw_condition_mask) {
        return 0;
    }
    DrawCondition condition{};
    condition.mask = policy->draw_condition_mask;
    condition.element_count_min = policy->element_count_min;
    condition.element_count_max = policy->element_count_max;
    condition.min_element = policy->min_element;
    condition.max_element = policy->max_element;
    condition.primitive_mode = policy->primitive_mode;
    return condition.Matches(DrawFacts{
        facts->element_count, facts->min_element, facts->max_element,
        facts->primitive_mode }) ? 1 : 0;
}

int xemu_shader_override_acquire_replacement(
    uint64_t replacement_id, uint64_t content_revision, uint32_t backend,
    XemuShaderReplacementSource *source)
{
    if (!source) {
        return 0;
    }
    std::memset(source, 0, sizeof(*source));
    OverrideBackend selected_backend = BackendFromWire(backend);
    auto payload = GetOverrideStore().AcquireReplacement(
        replacement_id, content_revision, selected_backend);
    if (!payload) {
        return 0;
    }
    auto *retained = new RetainedSource;
    retained->payload = std::move(payload);
    retained->source = selected_backend == OverrideBackend::OpenGL ?
        &retained->payload->opengl_source :
        &retained->payload->vulkan_source;
    if (retained->source->empty()) {
        delete retained;
        return 0;
    }
    source->handle = retained;
    source->data = reinterpret_cast<const uint8_t *>(
        retained->source->data());
    source->size = retained->source->size();
    source->entry_point = retained->payload->descriptor.entry_point.c_str();
    source->replacement_id = replacement_id;
    source->content_revision = content_revision;
    return 1;
}

void xemu_shader_override_release_replacement(
    XemuShaderReplacementSource *source)
{
    if (!source) {
        return;
    }
    delete static_cast<RetainedSource *>(source->handle);
    std::memset(source, 0, sizeof(*source));
}

} // extern "C"

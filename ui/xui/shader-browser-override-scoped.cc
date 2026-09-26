// SPDX-License-Identifier: GPL-2.0-or-later
#include "shader-browser-override-store.hh"
#include "shader-browser-override-runtime.h"

#include <cstring>
#include <mutex>

namespace xemu::shader_browser {
namespace {

struct ScopedIndexCache {
    bool valid = false;
    uint64_t generation = 0;
    OverrideContext context;
    OverrideIndex index;
};

std::mutex scoped_cache_mutex;
ScopedIndexCache scoped_cache[2];

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

bool SameContext(const OverrideContext &a, const OverrideContext &b)
{
    return a.title_id == b.title_id &&
           a.executable_fingerprint_version ==
               b.executable_fingerprint_version &&
           a.executable_fingerprint == b.executable_fingerprint &&
           a.backend == b.backend;
}

int CopyPolicy(const OverrideResolution &resolved, uint64_t generation,
               XemuShaderOverridePolicy *policy)
{
    if (!policy) return 0;
    std::memset(policy, 0, sizeof(*policy));
    policy->generation = generation;
    if (resolved.status != OverrideResolutionStatus::Matched) return 0;

    const RuntimePolicy &source = resolved.policy;
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

} // namespace
} // namespace xemu::shader_browser

using namespace xemu::shader_browser;

extern "C" int xemu_shader_override_resolve_scoped(
    uint32_t title_id, uint32_t executable_fingerprint_version,
    const uint8_t executable_fingerprint[
        XEMU_SHADER_BROWSER_EXECUTABLE_FINGERPRINT_BYTES],
    uint32_t backend, uint32_t identity_version,
    const uint8_t identity_hash[XEMU_SHADER_BROWSER_HASH_BYTES],
    uint32_t stage, XemuShaderOverridePolicy *policy)
{
    if (!policy) return 0;
    std::memset(policy, 0, sizeof(*policy));
    if (!title_id || !identity_version || !identity_hash || stage == 0 ||
        stage > static_cast<uint32_t>(Stage::Unknown)) {
        return 0;
    }

    OverrideContext context{};
    context.title_id = title_id;
    context.executable_fingerprint_version = executable_fingerprint_version;
    if (executable_fingerprint) {
        std::memcpy(context.executable_fingerprint.data(),
                    executable_fingerprint,
                    context.executable_fingerprint.size());
    }
    context.backend = BackendFromWire(backend);
    if (context.backend == OverrideBackend::Unknown) return 0;

    ShaderKey key{};
    key.hash.version = identity_version;
    std::memcpy(key.hash.bytes.data(), identity_hash, key.hash.bytes.size());
    key.stage = static_cast<Stage>(stage);
    if (key.stage == Stage::Any || key.stage == Stage::Unknown) return 0;

    const size_t slot = context.backend == OverrideBackend::OpenGL ? 0 : 1;
    std::lock_guard<std::mutex> cache_lock(scoped_cache_mutex);
    ScopedIndexCache &cache = scoped_cache[slot];
    OverrideStore &store = GetOverrideStore();
    const uint64_t current_generation = store.Generation();

    if (!cache.valid || cache.generation != current_generation ||
        !SameContext(cache.context, context)) {
        OverrideStoreSnapshot snapshot;
        store.CopySnapshot(&snapshot);
        if (snapshot.disabled) {
            cache.valid = false;
            policy->generation = snapshot.generation;
            return 0;
        }
        cache.index.Rebuild(context, snapshot.rules, snapshot.replacements);
        cache.context = context;
        cache.generation = snapshot.generation;
        cache.valid = true;
    }

    return CopyPolicy(cache.index.Resolve(key), cache.generation, policy);
}

// SPDX-License-Identifier: GPL-2.0-or-later
#include "shader-browser-override-runtime.h"

#include <array>
#include <cstring>
#include <map>
#include <mutex>
#include <tuple>

namespace {

struct EffectKey {
    uint32_t title_id;
    uint32_t fingerprint_version;
    std::array<uint8_t, XEMU_SHADER_BROWSER_EXECUTABLE_FINGERPRINT_BYTES>
        fingerprint;
    uint32_t backend;
    uint32_t identity_version;
    std::array<uint8_t, XEMU_SHADER_BROWSER_HASH_BYTES> identity_hash;
    uint32_t stage;

    bool operator<(const EffectKey &other) const
    {
        return std::tie(title_id, fingerprint_version, fingerprint, backend,
                        identity_version, identity_hash, stage) <
               std::tie(other.title_id, other.fingerprint_version,
                        other.fingerprint, other.backend,
                        other.identity_version, other.identity_hash,
                        other.stage);
    }
};

std::mutex effect_mutex;
std::map<EffectKey, XemuShaderOverrideEffect> effects;
uint64_t effects_generation;

EffectKey MakeKey(uint32_t title_id, uint32_t fingerprint_version,
                  const uint8_t *fingerprint, uint32_t backend,
                  uint32_t identity_version, const uint8_t *identity_hash,
                  uint32_t stage)
{
    EffectKey key{};
    key.title_id = title_id;
    key.fingerprint_version = fingerprint_version;
    if (fingerprint) {
        std::memcpy(key.fingerprint.data(), fingerprint,
                    key.fingerprint.size());
    }
    key.backend = backend;
    key.identity_version = identity_version;
    std::memcpy(key.identity_hash.data(), identity_hash,
                key.identity_hash.size());
    key.stage = stage;
    return key;
}

} // namespace

extern "C" void xemu_shader_override_publish_effect(
    uint32_t title_id, uint32_t fingerprint_version,
    const uint8_t *fingerprint, uint32_t backend,
    uint32_t identity_version, const uint8_t *identity_hash,
    uint32_t stage, const XemuShaderOverrideEffect *effect)
{
    if (!title_id || !identity_version || !identity_hash || !effect ||
        !effect->generation || !effect->rule_id) {
        return;
    }
    EffectKey key = MakeKey(title_id, fingerprint_version, fingerprint,
                            backend, identity_version, identity_hash, stage);
    XemuShaderOverrideEffect copy = *effect;
    copy.error[sizeof(copy.error) - 1] = '\0';
    std::lock_guard<std::mutex> lock(effect_mutex);
    if (copy.generation < effects_generation) {
        return;
    }
    if (effects_generation != copy.generation || effects.size() >= 4096) {
        effects.clear();
        effects_generation = copy.generation;
    }
    effects[key] = copy;
}

extern "C" int xemu_shader_override_copy_effect(
    uint32_t title_id, uint32_t fingerprint_version,
    const uint8_t *fingerprint, uint32_t backend,
    uint32_t identity_version, const uint8_t *identity_hash,
    uint32_t stage, uint64_t generation, uint64_t rule_id,
    uint64_t rule_revision, XemuShaderOverrideEffect *effect)
{
    if (!title_id || !identity_version || !identity_hash || !effect ||
        !generation || !rule_id) {
        return 0;
    }
    EffectKey key = MakeKey(title_id, fingerprint_version, fingerprint,
                            backend, identity_version, identity_hash, stage);
    std::lock_guard<std::mutex> lock(effect_mutex);
    auto it = effects.find(key);
    if (it == effects.end() || it->second.generation != generation ||
        it->second.rule_id != rule_id ||
        it->second.rule_revision != rule_revision) {
        return 0;
    }
    *effect = it->second;
    return 1;
}

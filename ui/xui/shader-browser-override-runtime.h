/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef XEMU_SHADER_BROWSER_OVERRIDE_RUNTIME_H
#define XEMU_SHADER_BROWSER_OVERRIDE_RUNTIME_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef XEMU_SHADER_BROWSER_HASH_BYTES
#define XEMU_SHADER_BROWSER_HASH_BYTES 12
#endif
#ifndef XEMU_SHADER_BROWSER_EXECUTABLE_FINGERPRINT_BYTES
#define XEMU_SHADER_BROWSER_EXECUTABLE_FINGERPRINT_BYTES 32
#endif

typedef enum XemuShaderOverrideBackend {
    XEMU_SHADER_OVERRIDE_BACKEND_UNKNOWN = 0,
    XEMU_SHADER_OVERRIDE_BACKEND_OPENGL = 1,
    XEMU_SHADER_OVERRIDE_BACKEND_VULKAN = 2,
} XemuShaderOverrideBackend;

typedef enum XemuShaderOverrideAction {
    XEMU_SHADER_OVERRIDE_ACTION_NORMAL = 0,
    XEMU_SHADER_OVERRIDE_ACTION_FORCE_UBER = 1,
    XEMU_SHADER_OVERRIDE_ACTION_FORCE_SPECIALIZED = 2,
    XEMU_SHADER_OVERRIDE_ACTION_SKIP_DRAW = 3,
    XEMU_SHADER_OVERRIDE_ACTION_HIGHLIGHT = 4,
    XEMU_SHADER_OVERRIDE_ACTION_REPLACEMENT = 5,
} XemuShaderOverrideAction;

typedef enum XemuShaderOverrideDrawConditionMask {
    XEMU_SHADER_OVERRIDE_DRAW_CONDITION_NONE = 0,
    XEMU_SHADER_OVERRIDE_DRAW_CONDITION_ELEMENT_COUNT = 1U << 0,
    XEMU_SHADER_OVERRIDE_DRAW_CONDITION_ELEMENT_RANGE = 1U << 1,
    XEMU_SHADER_OVERRIDE_DRAW_CONDITION_PRIMITIVE = 1U << 2,
} XemuShaderOverrideDrawConditionMask;

typedef struct XemuShaderOverridePolicy {
    uint64_t generation;
    uint32_t action;
    uint64_t rule_id;
    uint64_t rule_revision;
    uint64_t replacement_id;
    uint64_t replacement_revision;
    uint32_t draw_condition_mask;
    uint32_t element_count_min;
    uint32_t element_count_max;
    uint32_t min_element;
    uint32_t max_element;
    uint32_t primitive_mode;
} XemuShaderOverridePolicy;

typedef struct XemuShaderOverrideDrawFacts {
    uint32_t available_mask;
    uint32_t element_count;
    uint32_t min_element;
    uint32_t max_element;
    uint32_t primitive_mode;
} XemuShaderOverrideDrawFacts;

typedef struct XemuShaderReplacementSource {
    void *handle;
    const uint8_t *data;
    size_t size;
    const char *entry_point;
    uint64_t replacement_id;
    uint64_t content_revision;
} XemuShaderReplacementSource;

typedef enum XemuShaderOverrideEffectState {
    XEMU_SHADER_OVERRIDE_EFFECT_UNOBSERVED = 0,
    XEMU_SHADER_OVERRIDE_EFFECT_PREPARING = 1,
    XEMU_SHADER_OVERRIDE_EFFECT_EFFECTIVE = 2,
    XEMU_SHADER_OVERRIDE_EFFECT_FALLBACK = 3,
    XEMU_SHADER_OVERRIDE_EFFECT_FAILED = 4,
    XEMU_SHADER_OVERRIDE_EFFECT_CONDITION_NOT_MATCHED = 5,
} XemuShaderOverrideEffectState;

typedef struct XemuShaderOverrideEffect {
    uint64_t generation;
    uint64_t rule_id;
    uint64_t rule_revision;
    uint32_t requested_action;
    uint32_t effective_action;
    uint32_t state;
    uint64_t requested_replacement_id;
    uint64_t requested_replacement_revision;
    uint64_t effective_replacement_id;
    uint64_t effective_replacement_revision;
    char error[256];
} XemuShaderOverrideEffect;

void xemu_shader_override_publish_effect(
    uint32_t title_id, uint32_t executable_fingerprint_version,
    const uint8_t executable_fingerprint[
        XEMU_SHADER_BROWSER_EXECUTABLE_FINGERPRINT_BYTES],
    uint32_t backend, uint32_t identity_version,
    const uint8_t identity_hash[XEMU_SHADER_BROWSER_HASH_BYTES],
    uint32_t stage, const XemuShaderOverrideEffect *effect);
int xemu_shader_override_copy_effect(
    uint32_t title_id, uint32_t executable_fingerprint_version,
    const uint8_t executable_fingerprint[
        XEMU_SHADER_BROWSER_EXECUTABLE_FINGERPRINT_BYTES],
    uint32_t backend, uint32_t identity_version,
    const uint8_t identity_hash[XEMU_SHADER_BROWSER_HASH_BYTES],
    uint32_t stage, uint64_t generation, uint64_t rule_id,
    uint64_t rule_revision, XemuShaderOverrideEffect *effect);

void xemu_shader_override_set_context(
    uint32_t title_id, uint32_t executable_fingerprint_version,
    const uint8_t executable_fingerprint[
        XEMU_SHADER_BROWSER_EXECUTABLE_FINGERPRINT_BYTES],
    uint32_t backend);
uint64_t xemu_shader_override_generation(void);
int xemu_shader_override_has_active_rules(void);
int xemu_shader_override_has_active_rules_scoped(
    uint32_t title_id, uint32_t executable_fingerprint_version,
    const uint8_t executable_fingerprint[
        XEMU_SHADER_BROWSER_EXECUTABLE_FINGERPRINT_BYTES],
    uint32_t backend);
int xemu_shader_override_resolve(
    uint32_t identity_version,
    const uint8_t identity_hash[XEMU_SHADER_BROWSER_HASH_BYTES],
    uint32_t stage, XemuShaderOverridePolicy *policy);
int xemu_shader_override_resolve_scoped(
    uint32_t title_id, uint32_t executable_fingerprint_version,
    const uint8_t executable_fingerprint[
        XEMU_SHADER_BROWSER_EXECUTABLE_FINGERPRINT_BYTES],
    uint32_t backend, uint32_t identity_version,
    const uint8_t identity_hash[XEMU_SHADER_BROWSER_HASH_BYTES],
    uint32_t stage, XemuShaderOverridePolicy *policy);
int xemu_shader_override_policy_matches_draw(
    const XemuShaderOverridePolicy *policy,
    const XemuShaderOverrideDrawFacts *facts);
int xemu_shader_override_acquire_replacement(
    uint64_t replacement_id, uint64_t content_revision, uint32_t backend,
    XemuShaderReplacementSource *source);
void xemu_shader_override_release_replacement(
    XemuShaderReplacementSource *source);

#ifdef __cplusplus
}
#endif

#endif

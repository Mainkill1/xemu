// SPDX-License-Identifier: GPL-2.0-or-later
#include "shader-browser-override-runtime.h"

#include <cassert>
#include <cstring>

int main()
{
    uint8_t build_a[XEMU_SHADER_BROWSER_EXECUTABLE_FINGERPRINT_BYTES] = { 1 };
    uint8_t build_b[XEMU_SHADER_BROWSER_EXECUTABLE_FINGERPRINT_BYTES] = { 2 };
    uint8_t hash[XEMU_SHADER_BROWSER_HASH_BYTES] = { 3 };
    XemuShaderOverrideEffect in{};
    in.generation = 7;
    in.rule_id = 11;
    in.rule_revision = 13;
    in.requested_action = XEMU_SHADER_OVERRIDE_ACTION_REPLACEMENT;
    in.effective_action = XEMU_SHADER_OVERRIDE_ACTION_NORMAL;
    in.state = XEMU_SHADER_OVERRIDE_EFFECT_FAILED;
    std::memset(in.error, 'x', sizeof(in.error));
    xemu_shader_override_publish_effect(
        0x1234, 1, build_a, XEMU_SHADER_OVERRIDE_BACKEND_OPENGL,
        1, hash, 2, &in);

    XemuShaderOverrideEffect out{};
    assert(xemu_shader_override_copy_effect(
        0x1234, 1, build_a, XEMU_SHADER_OVERRIDE_BACKEND_OPENGL,
        1, hash, 2, 7, 11, 13, &out));
    assert(out.state == XEMU_SHADER_OVERRIDE_EFFECT_FAILED);
    assert(out.error[sizeof(out.error) - 1] == '\0');
    assert(!xemu_shader_override_copy_effect(
        0x1234, 1, build_b, XEMU_SHADER_OVERRIDE_BACKEND_OPENGL,
        1, hash, 2, 7, 11, 13, &out));
    assert(!xemu_shader_override_copy_effect(
        0x1234, 1, build_a, XEMU_SHADER_OVERRIDE_BACKEND_VULKAN,
        1, hash, 2, 7, 11, 13, &out));
    assert(!xemu_shader_override_copy_effect(
        0x1234, 1, build_a, XEMU_SHADER_OVERRIDE_BACKEND_OPENGL,
        1, hash, 2, 7, 11, 14, &out));
    assert(!xemu_shader_override_copy_effect(
        0x1234, 1, build_a, XEMU_SHADER_OVERRIDE_BACKEND_OPENGL,
        1, hash, 2, 8, 11, 13, &out));

    in.generation = 8;
    in.rule_revision = 14;
    in.state = XEMU_SHADER_OVERRIDE_EFFECT_EFFECTIVE;
    in.error[0] = '\0';
    xemu_shader_override_publish_effect(
        0x1234, 1, build_a, XEMU_SHADER_OVERRIDE_BACKEND_OPENGL,
        1, hash, 2, &in);
    assert(xemu_shader_override_copy_effect(
        0x1234, 1, build_a, XEMU_SHADER_OVERRIDE_BACKEND_OPENGL,
        1, hash, 2, 8, 11, 14, &out));
    assert(!xemu_shader_override_copy_effect(
        0x1234, 1, build_a, XEMU_SHADER_OVERRIDE_BACKEND_OPENGL,
        1, hash, 2, 7, 11, 13, &out));
    in.generation = 7;
    in.rule_revision = 13;
    xemu_shader_override_publish_effect(
        0x1234, 1, build_a, XEMU_SHADER_OVERRIDE_BACKEND_OPENGL,
        1, hash, 2, &in);
    assert(xemu_shader_override_copy_effect(
        0x1234, 1, build_a, XEMU_SHADER_OVERRIDE_BACKEND_OPENGL,
        1, hash, 2, 8, 11, 14, &out));
    return 0;
}

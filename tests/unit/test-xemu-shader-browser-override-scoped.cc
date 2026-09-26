#include "../../ui/xui/shader-browser-override-store.hh"
#include "../../ui/xui/shader-browser-override-runtime.h"

#include <cassert>
#include <iostream>

using namespace xemu::shader_browser;

static ShaderKey Key(uint8_t seed)
{
    ShaderKey key{};
    key.stage = Stage::Pixel;
    for (size_t i = 0; i < key.hash.bytes.size(); ++i) {
        key.hash.bytes[i] = seed + i;
    }
    return key;
}

int main()
{
    OverrideStore &store = GetOverrideStore();
    store.Clear();

    ReplacementPayload payload{};
    payload.descriptor.id = 9;
    payload.descriptor.name = "test";
    payload.descriptor.stage = Stage::Pixel;
    payload.descriptor.backend_mask = BackendOpenGL | BackendVulkan;
    payload.descriptor.interface_version = 1;
    payload.descriptor.entry_point = "main";
    payload.descriptor.content_revision = 1;
    payload.opengl_source = "void main(){}";
    payload.vulkan_source = "void main(){}";
    std::string error;
    assert(store.UpsertReplacement(payload, &error));

    ShaderKey key = Key(7);
    OverrideRule rule{};
    rule.id = 3;
    rule.title_id = 0x4d530064;
    rule.shader = key;
    rule.origin = OverrideOrigin::Session;
    rule.action = OverrideAction::Replacement;
    rule.replacement_id = 9;
    rule.revision = 1;
    assert(store.UpsertRule(rule, &error));
    assert(xemu_shader_override_has_active_rules_scoped(
        rule.title_id, 0, nullptr,
        XEMU_SHADER_OVERRIDE_BACKEND_VULKAN));
    assert(!xemu_shader_override_has_active_rules_scoped(
        0x12345678, 0, nullptr,
        XEMU_SHADER_OVERRIDE_BACKEND_VULKAN));

    XemuShaderOverridePolicy policy{};
    assert(xemu_shader_override_resolve_scoped(
        rule.title_id, 0, nullptr, XEMU_SHADER_OVERRIDE_BACKEND_OPENGL,
        key.hash.version, key.hash.bytes.data(),
        static_cast<uint32_t>(key.stage), &policy));
    assert(policy.rule_id == 3 && policy.replacement_revision == 1);

    const uint64_t old_generation = policy.generation;
    payload.descriptor.content_revision = 2;
    payload.opengl_source += "// changed";
    assert(store.UpsertReplacement(payload, &error));
    assert(xemu_shader_override_resolve_scoped(
        rule.title_id, 0, nullptr, XEMU_SHADER_OVERRIDE_BACKEND_OPENGL,
        key.hash.version, key.hash.bytes.data(),
        static_cast<uint32_t>(key.stage), &policy));
    assert(policy.generation > old_generation);
    assert(policy.replacement_revision == 2);
    assert(xemu_shader_override_has_active_rules_scoped(
        rule.title_id, 0, nullptr,
        XEMU_SHADER_OVERRIDE_BACKEND_VULKAN));

    assert(!xemu_shader_override_resolve_scoped(
        0x12345678, 0, nullptr, XEMU_SHADER_OVERRIDE_BACKEND_OPENGL,
        key.hash.version, key.hash.bytes.data(),
        static_cast<uint32_t>(key.stage), &policy));
    assert(policy.action == XEMU_SHADER_OVERRIDE_ACTION_NORMAL);

    store.Clear();
    assert(!xemu_shader_override_has_active_rules_scoped(
        rule.title_id, 0, nullptr,
        XEMU_SHADER_OVERRIDE_BACKEND_VULKAN));
    std::cout << "shader browser scoped resolver tests passed\n";
    return 0;
}

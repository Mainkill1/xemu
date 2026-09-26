#include "../../ui/xui/shader-browser-override-store.hh"
#include "../../ui/xui/shader-browser-override-runtime.h"

#include <cassert>
#include <cstring>
#include <iostream>

using namespace xemu::shader_browser;

static ShaderKey MakeKey(uint8_t seed)
{
    ShaderKey key{};
    key.stage = Stage::Pixel;
    for (size_t i = 0; i < key.hash.bytes.size(); ++i) {
        key.hash.bytes[i] = seed + i;
    }
    return key;
}

static ReplacementPayload MakeReplacement(uint64_t id, uint64_t revision)
{
    ReplacementPayload payload{};
    payload.descriptor.id = id;
    payload.descriptor.name = "replacement";
    payload.descriptor.stage = Stage::Pixel;
    payload.descriptor.backend_mask = BackendOpenGL | BackendVulkan;
    payload.descriptor.interface_version = 1;
    payload.descriptor.entry_point = "main";
    payload.descriptor.content_revision = revision;
    payload.opengl_source = "#version 450\nvoid main(){}\n";
    payload.vulkan_source = "#version 450\nvoid main(){}\n";
    return payload;
}

int main()
{
    OverrideStore store;
    OverrideContext context{};
    context.title_id = 0x4d530064;
    context.backend = OverrideBackend::Vulkan;
    store.SetContext(context);

    std::string error;
    ReplacementPayload payload = MakeReplacement(55, 1);
    assert(store.UpsertReplacement(payload, &error));

    OverrideRule rule{};
    rule.id = 9;
    rule.enabled = true;
    rule.title_id = context.title_id;
    rule.shader = MakeKey(10);
    rule.origin = OverrideOrigin::Session;
    rule.action = OverrideAction::Replacement;
    rule.replacement_id = payload.descriptor.id;
    rule.revision = 1;
    assert(store.UpsertRule(rule, &error));

    OverrideResolution resolved = store.Resolve(rule.shader);
    assert(resolved.status == OverrideResolutionStatus::Matched);
    assert(resolved.policy.replacement_id == 55);
    const uint64_t generation = store.Generation();

    payload.descriptor.content_revision = 2;
    payload.opengl_source += "// changed\n";
    assert(store.UpsertReplacement(payload, &error));
    assert(store.Generation() > generation);
    resolved = store.Resolve(rule.shader);
    assert(resolved.policy.replacement_revision == 2);

    auto acquired = store.AcquireReplacement(
        55, 2, OverrideBackend::Vulkan);
    assert(acquired);
    assert(acquired->vulkan_source.find("void main") != std::string::npos);
    assert(!store.AcquireReplacement(55, 1, OverrideBackend::Vulkan));

    OverrideStoreSnapshot snapshot;
    store.CopySnapshot(&snapshot);
    assert(snapshot.rules.size() == 1);
    assert(snapshot.replacements.size() == 1);
    assert(snapshot.resolved_rule_count == 1);

    assert(store.RemoveRule(9));
    assert(store.Resolve(rule.shader).status ==
           OverrideResolutionStatus::None);

    OverrideStore &global = GetOverrideStore();
    global.Clear();
    global.SetContext(context);
    assert(global.UpsertReplacement(MakeReplacement(77, 4), &error));
    rule.id = 12;
    rule.replacement_id = 77;
    assert(global.UpsertRule(rule, &error));

    XemuShaderOverridePolicy policy{};
    assert(xemu_shader_override_resolve(
        rule.shader.hash.version, rule.shader.hash.bytes.data(),
        static_cast<uint32_t>(rule.shader.stage), &policy));
    assert(policy.action == XEMU_SHADER_OVERRIDE_ACTION_REPLACEMENT);
    assert(policy.replacement_id == 77);

    XemuShaderReplacementSource source{};
    assert(xemu_shader_override_acquire_replacement(
        77, 4, XEMU_SHADER_OVERRIDE_BACKEND_VULKAN, &source));
    assert(source.handle != nullptr);
    assert(source.data != nullptr && source.size > 0);
    assert(std::strstr(reinterpret_cast<const char *>(source.data),
                       "void main") != nullptr);
    xemu_shader_override_release_replacement(&source);
    assert(source.handle == nullptr && source.data == nullptr);

    global.SetDisabled(true);
    assert(!xemu_shader_override_resolve(
        rule.shader.hash.version, rule.shader.hash.bytes.data(),
        static_cast<uint32_t>(rule.shader.stage), &policy));
    global.Clear();

    std::cout << "shader browser override store tests passed\n";
    return 0;
}

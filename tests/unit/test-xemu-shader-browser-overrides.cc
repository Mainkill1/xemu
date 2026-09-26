#include "../../ui/xui/shader-browser-overrides.hh"

#include <cassert>
#include <iostream>

using namespace xemu::shader_browser;

static ShaderKey Key(uint8_t seed, Stage stage = Stage::Pixel)
{
    ShaderKey key{};
    key.stage = stage;
    for (size_t i = 0; i < key.hash.bytes.size(); ++i) {
        key.hash.bytes[i] = seed + i;
    }
    return key;
}

static std::array<uint8_t, kExecutableFingerprintBytes> Build(uint8_t seed)
{
    std::array<uint8_t, kExecutableFingerprintBytes> value{};
    for (size_t i = 0; i < value.size(); ++i) {
        value[i] = seed + i;
    }
    return value;
}

static ReplacementDescriptor Replacement(uint64_t id, Stage stage,
                                         uint32_t backends)
{
    ReplacementDescriptor replacement{};
    replacement.id = id;
    replacement.name = "test";
    replacement.stage = stage;
    replacement.backend_mask = backends;
    replacement.interface_version = 1;
    replacement.entry_point = "main";
    replacement.content_revision = id;
    return replacement;
}

static OverrideRule Rule(uint64_t id, ShaderKey key, OverrideAction action,
                         OverrideOrigin origin = OverrideOrigin::Saved)
{
    OverrideRule rule{};
    rule.id = id;
    rule.enabled = true;
    rule.title_id = 0x4d530064;
    rule.shader = key;
    rule.action = action;
    rule.origin = origin;
    rule.priority = 10;
    return rule;
}

int main()
{
    const ShaderKey pixel = Key(1);
    const ShaderKey vertex = Key(2, Stage::Vertex);
    ReplacementDescriptor glvk = Replacement(
        44, Stage::Pixel, BackendOpenGL | BackendVulkan);
    std::string reason;

    assert(IsReplacementCompatible(pixel, glvk, OverrideBackend::OpenGL,
                                   &reason));
    assert(!IsReplacementCompatible(vertex, glvk, OverrideBackend::OpenGL,
                                    &reason));
    assert(reason.find("stage") != std::string::npos);

    OverrideIndex index;
    OverrideContext context{};
    context.title_id = 0x4d530064;
    context.backend = OverrideBackend::Vulkan;
    context.executable_fingerprint_version = 1;
    context.executable_fingerprint = Build(3);

    OverrideRule saved = Rule(1, pixel, OverrideAction::SkipDraw,
                              OverrideOrigin::Saved);
    OverrideRule session = Rule(2, pixel, OverrideAction::Highlight,
                                OverrideOrigin::Session);
    index.Rebuild(context, { saved, session }, { glvk });
    OverrideResolution result = index.Resolve(pixel);
    assert(result.status == OverrideResolutionStatus::Matched);
    assert(result.policy.action == OverrideAction::Highlight);
    assert(result.policy.rule_id == 2);

    OverrideRule build_specific = Rule(
        3, pixel, OverrideAction::ForceSpecialized,
        OverrideOrigin::Session);
    build_specific.restrict_build = true;
    build_specific.executable_fingerprint_version = 1;
    build_specific.executable_fingerprint = Build(3);
    index.Rebuild(context, { session, build_specific }, { glvk });
    result = index.Resolve(pixel);
    assert(result.policy.rule_id == 3);

    OverrideRule conflict = build_specific;
    conflict.id = 4;
    conflict.action = OverrideAction::SkipDraw;
    index.Rebuild(context, { build_specific, conflict }, { glvk });
    result = index.Resolve(pixel);
    assert(result.status == OverrideResolutionStatus::Conflict);
    assert(result.policy.action == OverrideAction::Normal);

    OverrideRule replacement = Rule(
        5, pixel, OverrideAction::Replacement, OverrideOrigin::Session);
    replacement.replacement_id = 44;
    index.Rebuild(context, { replacement }, { glvk });
    result = index.Resolve(pixel);
    assert(result.status == OverrideResolutionStatus::Matched);
    assert(result.policy.replacement_id == 44);

    OverrideContext gl_context = context;
    gl_context.backend = OverrideBackend::OpenGL;
    OverrideRule uber = Rule(
        6, pixel, OverrideAction::ForceUber, OverrideOrigin::Session);
    index.Rebuild(gl_context, { uber }, { glvk });
    result = index.Resolve(pixel);
    assert(result.status == OverrideResolutionStatus::Incompatible);

    DrawCondition condition{};
    condition.mask = DrawConditionElementCount | DrawConditionPrimitive;
    condition.element_count_min = 100;
    condition.element_count_max = 200;
    condition.primitive_mode = 7;
    assert(condition.Matches(DrawFacts{150, 0, 0, 7}));
    assert(!condition.Matches(DrawFacts{99, 0, 0, 7}));
    assert(!condition.Matches(DrawFacts{150, 0, 0, 4}));

    OverrideRule wrong_title = Rule(
        7, pixel, OverrideAction::SkipDraw, OverrideOrigin::Session);
    wrong_title.title_id = 0x12345678;
    index.Rebuild(context, { wrong_title }, { glvk });
    assert(index.Resolve(pixel).status == OverrideResolutionStatus::None);

    Entry entry{};
    entry.key = pixel;
    ShaderScope scope{};
    scope.title_id = context.title_id;
    entry.scopes.push_back(scope);
    assert(IsEntryCompatibleWithReplacement(
        entry, context.title_id, glvk, OverrideBackend::Vulkan, &reason));
    entry.key = vertex;
    assert(!IsEntryCompatibleWithReplacement(
        entry, context.title_id, glvk, OverrideBackend::Vulkan, &reason));

    std::cout << "shader browser override tests passed\n";
    return 0;
}

#include "../../ui/xui/shader-browser-overrides.hh"
#include <cassert>
#include <iostream>
using namespace xemu::shader_browser;

static ShaderKey Key(uint8_t seed, Stage stage=Stage::Pixel) {
    ShaderKey key{}; key.stage=stage; for(size_t i=0;i<key.hash.bytes.size();++i) key.hash.bytes[i]=seed+i; return key;
}
static std::array<uint8_t,kExecutableFingerprintBytes> Build(uint8_t seed) {
    std::array<uint8_t,kExecutableFingerprintBytes> v{}; for(size_t i=0;i<v.size();++i)v[i]=seed+i; return v;
}
static ReplacementDescriptor Replacement(uint64_t id, Stage stage, uint32_t backends) {
    ReplacementDescriptor r{}; r.id=id; r.name="test"; r.stage=stage; r.backend_mask=backends; r.interface_version=1; r.entry_point="main"; r.content_revision=id; return r;
}
static OverrideRule Rule(uint64_t id, ShaderKey key, OverrideAction action, OverrideOrigin origin=OverrideOrigin::Saved) {
    OverrideRule r{}; r.id=id; r.enabled=true; r.title_id=0x4d530064; r.shader=key; r.action=action; r.origin=origin; r.priority=10; return r;
}

int main() {
    const ShaderKey pixel=Key(1), vertex=Key(2,Stage::Vertex);
    ReplacementDescriptor glvk=Replacement(44,Stage::Pixel,BackendOpenGL|BackendVulkan);
    std::string reason;
    assert(IsReplacementCompatible(pixel, glvk, OverrideBackend::OpenGL, &reason));
    assert(!IsReplacementCompatible(vertex, glvk, OverrideBackend::OpenGL, &reason));
    assert(reason.find("stage") != std::string::npos);

    OverrideIndex index;
    OverrideContext ctx{}; ctx.title_id=0x4d530064; ctx.backend=OverrideBackend::Vulkan; ctx.executable_fingerprint_version=1; ctx.executable_fingerprint=Build(3);
    OverrideRule saved=Rule(1,pixel,OverrideAction::SkipDraw,OverrideOrigin::Saved);
    OverrideRule session=Rule(2,pixel,OverrideAction::Normal,OverrideOrigin::Session);
    index.Rebuild(ctx,{saved,session},{glvk});
    OverrideResolution result=index.Resolve(pixel);
    assert(result.status==OverrideResolutionStatus::Matched);
    assert(result.policy.action==OverrideAction::Normal);
    assert(result.policy.rule_id==2);

    OverrideRule build_specific=Rule(3,pixel,OverrideAction::SkipDraw,OverrideOrigin::Session);
    build_specific.restrict_build=true; build_specific.executable_fingerprint_version=1; build_specific.executable_fingerprint=Build(3);
    build_specific.priority=10;
    index.Rebuild(ctx,{session,build_specific},{glvk});
    result=index.Resolve(pixel);
    assert(result.policy.rule_id==3);

    OverrideRule conflict=build_specific; conflict.id=4; conflict.action=OverrideAction::Highlight;
    index.Rebuild(ctx,{build_specific,conflict},{glvk});
    result=index.Resolve(pixel);
    assert(result.status==OverrideResolutionStatus::Conflict);
    assert(result.policy.action==OverrideAction::Normal);

    OverrideRule replacement=Rule(5,pixel,OverrideAction::Replacement,OverrideOrigin::Session);
    replacement.replacement_id=44;
    index.Rebuild(ctx,{replacement},{glvk});
    result=index.Resolve(pixel);
    assert(result.status==OverrideResolutionStatus::Incompatible);
    OverrideContext glctx=ctx; glctx.backend=OverrideBackend::OpenGL;
    index.Rebuild(glctx,{replacement},{glvk});
    result=index.Resolve(pixel);
    assert(result.status==OverrideResolutionStatus::Matched);
    assert(result.policy.replacement_id==44);

    OverrideRule uber=Rule(6,pixel,OverrideAction::ForceUber,OverrideOrigin::Session);
    index.Rebuild(glctx,{uber},{glvk});
    result=index.Resolve(pixel);
    assert(result.status==OverrideResolutionStatus::Incompatible);
    index.Rebuild(ctx,{uber},{glvk});
    result=index.Resolve(pixel);
    assert(result.status==OverrideResolutionStatus::Matched);
    assert(result.policy.action==OverrideAction::ForceUber);
    OverrideRule specialized=uber;
    specialized.id=10;
    specialized.action=OverrideAction::ForceSpecialized;
    index.Rebuild(ctx,{specialized},{glvk});
    result=index.Resolve(pixel);
    assert(result.status==OverrideResolutionStatus::Matched);
    assert(result.policy.action==OverrideAction::ForceSpecialized);

    DrawCondition cond{}; cond.mask=DrawConditionElementCount|DrawConditionPrimitive; cond.element_count_min=100; cond.element_count_max=200; cond.primitive_mode=7;
    assert(cond.Matches(DrawFacts{150,0,0,7}));
    assert(!cond.Matches(DrawFacts{99,0,0,7}));
    assert(!cond.Matches(DrawFacts{150,0,0,4}));

    OverrideRule wrong_title=Rule(7,pixel,OverrideAction::SkipDraw,OverrideOrigin::Session); wrong_title.title_id=0x12345678;
    index.Rebuild(ctx,{wrong_title},{glvk});
    assert(index.Resolve(pixel).status==OverrideResolutionStatus::None);

    OverrideRule vertex_skip=Rule(8,vertex,OverrideAction::SkipDraw,OverrideOrigin::Session);
    index.Rebuild(ctx,{vertex_skip},{glvk});
    assert(index.Resolve(vertex).status==OverrideResolutionStatus::Incompatible);

    OverrideRule wrong_build=build_specific;
    wrong_build.id=9;
    wrong_build.executable_fingerprint[0] ^= 0xff;
    index.Rebuild(ctx,{wrong_build},{glvk});
    assert(index.Resolve(pixel).status==OverrideResolutionStatus::None);

    ShaderDragPayload drag = MakeShaderDragPayload(ctx.title_id, pixel);
    uint32_t drag_title = 0;
    ShaderKey drag_key{};
    assert(DecodeShaderDragPayload(drag, &drag_title, &drag_key));
    assert(drag_title == ctx.title_id);
    assert(drag_key == pixel);
    drag.title_id = 0;
    assert(!DecodeShaderDragPayload(drag, &drag_title, &drag_key));

    Entry e{}; e.key=pixel; ShaderScope s{}; s.title_id=ctx.title_id; e.scopes.push_back(s);
    assert(IsEntryCompatibleWithReplacement(e,ctx.title_id,glvk,OverrideBackend::Vulkan,&reason));
    e.key=vertex;
    assert(!IsEntryCompatibleWithReplacement(e,ctx.title_id,glvk,OverrideBackend::Vulkan,&reason));

    std::cout << "shader browser override tests passed\n";
}

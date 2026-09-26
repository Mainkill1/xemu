#include "../../ui/xui/shader-browser-glsl-export.hh"

#include <cassert>

using namespace xemu::shader_browser;

int main()
{
    Entry entry{};
    entry.key.stage = Stage::Pixel;
    entry.key.hash.bytes[0] = 0x7f;
    ShaderScope scope{};
    scope.title_id = 0x12345678;
    entry.scopes.push_back(scope);
    HostSource source{};
    source.backend = DetailBackend::Vulkan;
    source.stage = HostSourceStage::Fragment;
    source.kind = HostSourceKind::Glsl;
    source.route = Route::Specialized;
    source.exact_runtime_source = true;
    source.text = "#version 450\nvoid main() {}\n";
    std::string glsl, metadata, error;
    assert(SerializeGeneratedGlsl(entry, source, &glsl, &metadata, &error));
    assert(glsl == source.text);
    assert(metadata.find("\"artifact_kind\":\"generated_glsl\"") != std::string::npos);
    assert(metadata.find("\"canonical_recipe\":false") != std::string::npos);
    assert(metadata.find("\"build_scope_verified\":false") != std::string::npos);
    source.kind = HostSourceKind::Metadata;
    assert(!SerializeGeneratedGlsl(entry, source, &glsl, &metadata, &error));
    return 0;
}

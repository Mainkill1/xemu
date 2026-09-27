#include "../../ui/xui/shader-browser-details-bridge.h"
#include "../../ui/xui/shader-browser-details-store.hh"

#include <cassert>
#include <cstring>
#include <iostream>

using namespace xemu::shader_browser;

int main()
{
    DetailStore &store = GetDetailStore();
    store.Clear();
    ShaderKey key{};
    key.hash.version = 1;
    key.hash.bytes[0] = 0x42;
    key.stage = Stage::Pixel;
    store.Request(key, DetailBackend::Vulkan);
    assert(xemu_shader_browser_details_pending());

    XemuShaderBrowserDetailRequest request{};
    assert(!xemu_shader_browser_details_try_claim(
        XEMU_SHADER_BROWSER_DETAIL_OPENGL, &request));
    assert(xemu_shader_browser_details_try_claim(
        XEMU_SHADER_BROWSER_DETAIL_VULKAN, &request));
    assert(request.stage == XEMU_SHADER_BROWSER_STAGE_PIXEL);
    assert(request.identity_hash[0] == 0x42);
    assert(request.flags == DetailRequestAll);

    XemuShaderBrowserDetailSource source{};
    source.stage = XEMU_SHADER_BROWSER_DETAIL_SOURCE_FRAGMENT;
    source.kind = XEMU_SHADER_BROWSER_DETAIL_SOURCE_GLSL;
    source.route = XEMU_SHADER_BROWSER_ROUTE_SPECIALIZED;
    source.artifact_id = 7;
    source.exact_runtime_source = 1;
    source.label = "resident fragment GLSL";
    source.text = "void main() {}\n";
    source.text_size = std::strlen(source.text);
    XemuShaderBrowserDetailVariant variant{};
    variant.variant_id = 9;
    variant.route = XEMU_SHADER_BROWSER_ROUTE_SPECIALIZED;
    variant.readiness = XEMU_SHADER_BROWSER_READINESS_READY;
    variant.label = "resident shader binding";
    variant.valid_fields = XEMU_SHADER_BROWSER_DETAIL_VALID_DRAW_COUNT;
    variant.draw_count = 0;
    XemuShaderBrowserDetailResult result{};
    result.request = request;
    result.backend = XEMU_SHADER_BROWSER_DETAIL_VULKAN;
    result.state = XEMU_SHADER_BROWSER_DETAIL_COMPLETE;
    result.sources = &source;
    result.source_count = 1;
    result.variants = &variant;
    result.variant_count = 1;
    assert(xemu_shader_browser_details_complete(&result));
    assert(!xemu_shader_browser_details_pending());

    DetailSnapshot snapshot{};
    assert(store.CopySnapshot(&snapshot));
    assert(snapshot.state == DetailState::Complete);
    assert(snapshot.sources.size() == 1);
    assert(snapshot.sources[0].text == source.text);
    assert(snapshot.variants.size() == 1);
    assert(snapshot.variants[0].valid_fields == HostVariantDrawCount);
    store.Clear();
    std::cout << "shader browser detail bridge tests passed\n";
    return 0;
}

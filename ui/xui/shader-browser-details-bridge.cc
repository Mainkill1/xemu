// SPDX-License-Identifier: GPL-2.0-or-later
#include "shader-browser-details-bridge.h"

#include "shader-browser-details-store.hh"

#include <cstring>
#include <string>
#include <utility>

namespace xemu::shader_browser {
namespace {

DetailBackend ConvertBackend(uint32_t backend)
{
    switch (backend) {
    case XEMU_SHADER_BROWSER_DETAIL_OPENGL: return DetailBackend::OpenGL;
    case XEMU_SHADER_BROWSER_DETAIL_VULKAN: return DetailBackend::Vulkan;
    default: return DetailBackend::Unknown;
    }
}

Stage ConvertStage(uint32_t stage)
{
    switch (stage) {
    case XEMU_SHADER_BROWSER_STAGE_VERTEX: return Stage::Vertex;
    case XEMU_SHADER_BROWSER_STAGE_PIXEL: return Stage::Pixel;
    case XEMU_SHADER_BROWSER_STAGE_GEOMETRY: return Stage::Geometry;
    case XEMU_SHADER_BROWSER_STAGE_FIXED_FUNCTION:
        return Stage::FixedFunction;
    default: return Stage::Unknown;
    }
}

uint32_t ConvertStage(Stage stage)
{
    switch (stage) {
    case Stage::Vertex: return XEMU_SHADER_BROWSER_STAGE_VERTEX;
    case Stage::Pixel: return XEMU_SHADER_BROWSER_STAGE_PIXEL;
    case Stage::Geometry: return XEMU_SHADER_BROWSER_STAGE_GEOMETRY;
    case Stage::FixedFunction: return XEMU_SHADER_BROWSER_STAGE_FIXED_FUNCTION;
    default: return XEMU_SHADER_BROWSER_STAGE_UNKNOWN;
    }
}

HostSourceStage ConvertSourceStage(uint32_t stage)
{
    switch (stage) {
    case XEMU_SHADER_BROWSER_DETAIL_SOURCE_VERTEX:
        return HostSourceStage::Vertex;
    case XEMU_SHADER_BROWSER_DETAIL_SOURCE_GEOMETRY:
        return HostSourceStage::Geometry;
    case XEMU_SHADER_BROWSER_DETAIL_SOURCE_FRAGMENT:
        return HostSourceStage::Fragment;
    case XEMU_SHADER_BROWSER_DETAIL_SOURCE_LINKED_PROGRAM:
        return HostSourceStage::LinkedProgram;
    case XEMU_SHADER_BROWSER_DETAIL_SOURCE_PIPELINE:
        return HostSourceStage::Pipeline;
    default: return HostSourceStage::Unknown;
    }
}

HostSourceKind ConvertSourceKind(uint32_t kind)
{
    switch (kind) {
    case XEMU_SHADER_BROWSER_DETAIL_SOURCE_GLSL:
        return HostSourceKind::Glsl;
    case XEMU_SHADER_BROWSER_DETAIL_SOURCE_SPIRV_TEXT:
        return HostSourceKind::SpirvText;
    case XEMU_SHADER_BROWSER_DETAIL_SOURCE_METADATA:
        return HostSourceKind::Metadata;
    default: return HostSourceKind::Unknown;
    }
}

DetailState ConvertState(uint32_t state)
{
    switch (state) {
    case XEMU_SHADER_BROWSER_DETAIL_COMPLETE: return DetailState::Complete;
    case XEMU_SHADER_BROWSER_DETAIL_PARTIAL: return DetailState::Partial;
    case XEMU_SHADER_BROWSER_DETAIL_UNAVAILABLE:
        return DetailState::Unavailable;
    case XEMU_SHADER_BROWSER_DETAIL_FAILED: return DetailState::Failed;
    default: return DetailState::Idle;
    }
}

Route ConvertRoute(uint32_t route)
{
    switch (route) {
    case XEMU_SHADER_BROWSER_ROUTE_SPECIALIZED: return Route::Specialized;
    case XEMU_SHADER_BROWSER_ROUTE_UBER: return Route::Uber;
    case XEMU_SHADER_BROWSER_ROUTE_FIXED_FUNCTION: return Route::FixedFunction;
    case XEMU_SHADER_BROWSER_ROUTE_REPLACEMENT: return Route::Replacement;
    case XEMU_SHADER_BROWSER_ROUTE_DISABLED: return Route::Disabled;
    default: return Route::Unknown;
    }
}

Readiness ConvertReadiness(uint32_t readiness)
{
    switch (readiness) {
    case XEMU_SHADER_BROWSER_READINESS_READY: return Readiness::Ready;
    case XEMU_SHADER_BROWSER_READINESS_PENDING: return Readiness::Pending;
    case XEMU_SHADER_BROWSER_READINESS_FAILED: return Readiness::Failed;
    default: return Readiness::Unknown;
    }
}

ShaderKey ConvertKey(const XemuShaderBrowserDetailRequest &request)
{
    ShaderKey key{};
    key.hash.version = request.identity_version;
    std::memcpy(key.hash.bytes.data(), request.identity_hash,
                key.hash.bytes.size());
    key.stage = ConvertStage(request.stage);
    return key;
}

bool CopyText(const char *text, std::string *out)
{
    if (!text) {
        out->clear();
        return true;
    }
    size_t length = 0;
    while (length <= kMaxDetailTextBytes && text[length]) ++length;
    if (length > kMaxDetailTextBytes) return false;
    out->assign(text, length);
    return true;
}

} // namespace
} // namespace xemu::shader_browser

extern "C" {

int xemu_shader_browser_details_pending(void)
{
    return xemu::shader_browser::GetDetailStore().HasPending() ? 1 : 0;
}

int xemu_shader_browser_details_try_claim(
    uint32_t backend, XemuShaderBrowserDetailRequest *wire)
{
    using namespace xemu::shader_browser;
    if (!wire) return 0;
    DetailRequest request{};
    if (!GetDetailStore().TryClaim(ConvertBackend(backend), &request)) return 0;
    wire->request_id = request.request_id;
    wire->identity_version = request.key.hash.version;
    std::memcpy(wire->identity_hash, request.key.hash.bytes.data(),
                request.key.hash.bytes.size());
    wire->stage = ConvertStage(request.key.stage);
    wire->flags = request.flags;
    return 1;
}

int xemu_shader_browser_details_complete(
    const XemuShaderBrowserDetailResult *wire)
{
    using namespace xemu::shader_browser;
    if (!wire || wire->source_count > kMaxDetailSources ||
        wire->variant_count > kMaxDetailVariants ||
        wire->lifecycle_count > kMaxLifecycleEvents * 8 ||
        (wire->source_count && !wire->sources) ||
        (wire->variant_count && !wire->variants) ||
        (wire->lifecycle_count && !wire->lifecycle)) return 0;

    DetailResult result{};
    result.request_id = wire->request.request_id;
    result.key = ConvertKey(wire->request);
    result.backend = ConvertBackend(wire->backend);
    if (!GetDetailStore().CanComplete(result.request_id, result.key,
                                       result.backend)) return 0;
    result.state = ConvertState(wire->state);
    result.dropped_lifecycle_events = wire->dropped_lifecycle_events;
    if (!CopyText(wire->status, &result.status)) return 0;

    for (size_t i = 0; i < wire->source_count; ++i) {
        const auto &input = wire->sources[i];
        if (input.text_size > kMaxDetailSourceBytes ||
            (input.text_size && !input.text)) return 0;
        HostSource source{};
        source.backend = result.backend;
        source.stage = ConvertSourceStage(input.stage);
        source.kind = ConvertSourceKind(input.kind);
        source.route = ConvertRoute(input.route);
        source.artifact_id = input.artifact_id;
        source.exact_runtime_source = input.exact_runtime_source != 0;
        if (!CopyText(input.label, &source.label)) return 0;
        if (input.text_size) source.text.assign(input.text, input.text_size);
        result.sources.push_back(std::move(source));
    }
    for (size_t i = 0; i < wire->variant_count; ++i) {
        const auto &input = wire->variants[i];
        HostVariant variant{};
        variant.variant_id = input.variant_id;
        variant.backend = result.backend;
        variant.route = ConvertRoute(input.route);
        variant.readiness = ConvertReadiness(input.readiness);
        variant.valid_fields = input.valid_fields;
        variant.first_frame = input.first_frame;
        variant.last_frame = input.last_frame;
        variant.draw_count = input.draw_count;
        variant.compile_time_ns = input.compile_time_ns;
        variant.create_time_ns = input.create_time_ns;
        variant.primitive_mode = input.primitive_mode;
        variant.color_format = input.color_format;
        variant.depth_format = input.depth_format;
        variant.vertex_binding_count = input.vertex_binding_count;
        variant.vertex_attribute_count = input.vertex_attribute_count;
        if (!CopyText(input.label, &variant.label)) return 0;
        result.variants.push_back(std::move(variant));
    }
    for (size_t i = 0; i < wire->lifecycle_count; ++i) {
        const auto &input = wire->lifecycle[i];
        LifecycleEvent event{};
        event.sequence = input.sequence;
        event.frame = input.frame;
        event.kind = input.kind >= XEMU_SHADER_BROWSER_DETAIL_EVENT_DISCOVERED &&
                             input.kind <=
                                 XEMU_SHADER_BROWSER_DETAIL_EVENT_REQUEST_COMPLETED ?
                         static_cast<LifecycleKind>(input.kind - 1) :
                         LifecycleKind::Unknown;
        event.backend = result.backend;
        event.route = ConvertRoute(input.route);
        event.artifact_id = input.artifact_id;
        if (!CopyText(input.message, &event.message)) return 0;
        result.lifecycle.push_back(std::move(event));
    }
    std::string error;
    return GetDetailStore().Complete(std::move(result), &error) ? 1 : 0;
}

int xemu_shader_browser_details_abandon(uint64_t request_id,
                                        uint32_t backend,
                                        const char *reason)
{
    using namespace xemu::shader_browser;
    std::string message;
    if (!CopyText(reason, &message)) return 0;
    return GetDetailStore().Abandon(request_id, ConvertBackend(backend),
                                    message) ? 1 : 0;
}

void xemu_shader_browser_details_reset(void)
{
    xemu::shader_browser::GetDetailStore().Clear();
}

} // extern "C"

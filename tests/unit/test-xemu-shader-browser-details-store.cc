#include "../../ui/xui/shader-browser-details-store.hh"

#include <cassert>
#include <iostream>
#include <string>

using namespace xemu::shader_browser;

static ShaderKey MakeKey(uint8_t seed)
{
    ShaderKey key{};
    key.hash.version = 1;
    for (size_t i = 0; i < key.hash.bytes.size(); ++i) {
        key.hash.bytes[i] = static_cast<uint8_t>(seed + i);
    }
    key.stage = Stage::Pixel;
    return key;
}

int main()
{
    DetailStore store;
    const ShaderKey first_key = MakeKey(1);
    const ShaderKey second_key = MakeKey(20);

    const uint64_t first_id = store.Request(first_key, DetailBackend::Vulkan);
    const uint64_t second_id = store.Request(second_key, DetailBackend::OpenGL);
    assert(second_id > first_id);

    DetailRequest request{};
    assert(!store.TryClaim(DetailBackend::Vulkan, &request));
    assert(store.TryClaim(DetailBackend::OpenGL, &request));
    assert(request.request_id == second_id);
    assert(request.key == second_key);
    assert(!store.TryClaim(DetailBackend::OpenGL, &request));

    DetailResult stale{};
    stale.request_id = first_id;
    stale.key = first_key;
    stale.state = DetailState::Complete;
    stale.backend = DetailBackend::Vulkan;
    std::string error;
    assert(!store.Complete(stale, &error));
    assert(error == "stale shader detail completion");

    DetailResult completed{};
    completed.request_id = second_id;
    completed.key = second_key;
    completed.state = DetailState::Partial;
    completed.backend = DetailBackend::OpenGL;
    completed.status = "resident program metadata available";

    HostSource source{};
    source.backend = DetailBackend::OpenGL;
    source.stage = HostSourceStage::Fragment;
    source.kind = HostSourceKind::Glsl;
    source.route = Route::Specialized;
    source.artifact_id = 7;
    source.exact_runtime_source = false;
    source.label = "reconstructed fragment GLSL";
    source.text = "void main() {}\n";
    completed.sources.push_back(source);

    HostVariant variant{};
    variant.variant_id = 11;
    variant.backend = DetailBackend::OpenGL;
    variant.route = Route::Specialized;
    variant.readiness = Readiness::Ready;
    variant.label = "resident linked program";
    completed.variants.push_back(variant);

    for (size_t i = 0; i < kMaxLifecycleEvents + 3; ++i) {
        LifecycleEvent event{};
        event.sequence = i + 1;
        event.frame = 100 + i;
        event.kind = LifecycleKind::Selected;
        event.backend = DetailBackend::OpenGL;
        event.message = "selected";
        completed.lifecycle.push_back(event);
    }

    error.clear();
    assert(store.Complete(completed, &error));

    DetailSnapshot snapshot{};
    assert(store.CopySnapshot(&snapshot));
    assert(snapshot.request.request_id == second_id);
    assert(snapshot.state == DetailState::Partial);
    assert(snapshot.sources.size() == 1);
    assert(snapshot.variants.size() == 1);
    assert(snapshot.lifecycle.size() == kMaxLifecycleEvents);
    assert(snapshot.lifecycle.front().sequence == 4);
    assert(snapshot.dropped_lifecycle_events == 3);

    const uint64_t generation_after_complete = snapshot.generation;
    store.Clear();
    assert(store.CopySnapshot(&snapshot));
    assert(snapshot.state == DetailState::Idle);
    assert(snapshot.generation > generation_after_complete);

    const uint64_t active_id = store.Request(first_key, DetailBackend::Active,
                                             DetailRequestSources);
    assert(store.TryClaim(DetailBackend::Vulkan, &request));
    assert(request.request_id == active_id);
    assert(request.flags == DetailRequestSources);

    DetailResult oversized{};
    oversized.request_id = active_id;
    oversized.key = first_key;
    oversized.state = DetailState::Complete;
    oversized.backend = DetailBackend::Vulkan;
    HostSource huge{};
    huge.text.resize(kMaxDetailSourceBytes + 1, 'x');
    oversized.sources.push_back(std::move(huge));
    error.clear();
    assert(!store.Complete(oversized, &error));
    assert(error == "host source exceeds 4 MiB");

    DetailResult invalid_text{};
    invalid_text.request_id = active_id;
    invalid_text.key = first_key;
    invalid_text.state = DetailState::Failed;
    invalid_text.backend = DetailBackend::Vulkan;
    invalid_text.status.assign("bad\0status", 10);
    error.clear();
    assert(!store.Complete(invalid_text, &error));
    assert(error == "detail status contains an embedded NUL");

    DetailResult unavailable{};
    unavailable.request_id = active_id;
    unavailable.key = first_key;
    unavailable.state = DetailState::Unavailable;
    unavailable.backend = DetailBackend::Vulkan;
    unavailable.status = "shader is not resident";
    error.clear();
    assert(store.Complete(unavailable, &error));
    assert(store.CopySnapshot(&snapshot));
    assert(snapshot.state == DetailState::Unavailable);

    std::cout << "shader browser detail store tests passed\n";
    return 0;
}

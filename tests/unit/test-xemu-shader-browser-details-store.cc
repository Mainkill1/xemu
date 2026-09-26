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
    assert(store.HasPending());
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
    assert(error == "stale or unclaimed shader detail completion");

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
    assert(!store.HasPending());

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
    assert(!store.HasPending());
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

    // A renderer must claim the request before publishing details, and the
    // completion must come from that same renderer.
    DetailStore ownership;
    const uint64_t ownership_id = ownership.Request(
        first_key, DetailBackend::Vulkan, DetailRequestSources);
    DetailResult owned{};
    owned.request_id = ownership_id;
    owned.key = first_key;
    owned.state = DetailState::Complete;
    owned.backend = DetailBackend::Vulkan;
    assert(!ownership.Complete(owned, &error));
    assert(ownership.TryClaim(DetailBackend::Vulkan, &request));
    owned.backend = DetailBackend::OpenGL;
    assert(!ownership.Complete(owned, &error));
    owned.backend = DetailBackend::Vulkan;
    assert(ownership.Complete(owned, &error));
    assert(!ownership.Complete(owned, &error));

    // A request for variants cannot carry source text or lifecycle events.
    const uint64_t variants_id = ownership.Request(
        second_key, DetailBackend::OpenGL, DetailRequestVariants);
    assert(ownership.TryClaim(DetailBackend::OpenGL, &request));
    DetailResult wrong_fields{};
    wrong_fields.request_id = variants_id;
    wrong_fields.key = second_key;
    wrong_fields.state = DetailState::Complete;
    wrong_fields.backend = DetailBackend::OpenGL;
    wrong_fields.sources.push_back(source);
    assert(!ownership.Complete(wrong_fields, &error));
    wrong_fields.sources.clear();
    wrong_fields.lifecycle.push_back(completed.lifecycle.front());
    assert(!ownership.Complete(wrong_fields, &error));

    // The UI can poll an unchanged generation without copying source text.
    DetailSnapshot unchanged{};
    unchanged.status = "sentinel";
    assert(ownership.CopySnapshot(&snapshot));
    assert(!ownership.CopySnapshotIfChanged(snapshot.generation, &unchanged));
    assert(unchanged.status == "sentinel");
    assert(ownership.Abandon(variants_id, DetailBackend::OpenGL,
                             "renderer switched"));
    assert(ownership.CopySnapshotIfChanged(snapshot.generation, &unchanged));
    assert(unchanged.state == DetailState::Unavailable);
    assert(unchanged.status == "renderer switched");

    // Validation limits and deduplication apply at publication, never draws.
    DetailResult invalid{};
    invalid.request_id = 1;
    invalid.key = first_key;
    invalid.state = DetailState::Partial;
    invalid.backend = DetailBackend::Vulkan;
    assert(!ValidateDetailResult(invalid, &error));
    invalid.status = "missing resident program";
    invalid.lifecycle.push_back({});
    assert(!ValidateDetailResult(invalid, &error));
    invalid.lifecycle.clear();
    invalid.sources.push_back(source);
    invalid.sources.push_back(source);
    assert(!ValidateDetailResult(invalid, &error));
    invalid.sources.clear();
    invalid.variants.push_back(variant);
    invalid.variants.push_back(variant);
    assert(!ValidateDetailResult(invalid, &error));
    invalid.variants.clear();
    invalid.sources.resize(33, source);
    assert(!ValidateDetailResult(invalid, &error));
    invalid.sources.clear();
    HostVariant known_zero = variant;
    known_zero.backend = DetailBackend::Vulkan;
    known_zero.valid_fields = HostVariantFrames | HostVariantDrawCount;
    invalid.variants.push_back(known_zero);
    assert(ValidateDetailResult(invalid, &error));
    invalid.variants.front().valid_fields = 1U << 31;
    assert(!ValidateDetailResult(invalid, &error));
    invalid.variants.clear();
    invalid.variants.resize(kMaxDetailVariants + 1, known_zero);
    assert(!ValidateDetailResult(invalid, &error));
    invalid.variants.clear();
    invalid.sources.push_back(source);
    assert(!ValidateDetailResult(invalid, &error));
    invalid.sources.front().backend = DetailBackend::Vulkan;
    assert(ValidateDetailResult(invalid, &error));

    std::cout << "shader browser detail store tests passed\n";
    return 0;
}

#include "../../ui/xui/shader-browser-workbench-apply.hh"
#include "../../ui/xui/shader-browser-override-store.hh"
#include "../../ui/xui/shader-browser-replacement-library.hh"

#include <cassert>
#include <cstdlib>
#include <filesystem>

using namespace xemu::shader_browser;

int main()
{
    Entry entry{};
    entry.key.stage = Stage::Pixel;
    entry.key.hash.bytes[0] = 1;
    ShaderScope scope{};
    scope.title_id = 0x12345678;
    scope.executable_fingerprint_version = 1;
    scope.executable_fingerprint[0] = 0x42;
    entry.scopes.push_back(scope);
    OverrideContext context{};
    context.title_id = scope.title_id;
    context.executable_fingerprint_version = scope.executable_fingerprint_version;
    context.executable_fingerprint = scope.executable_fingerprint;
    context.backend = OverrideBackend::OpenGL;
    DraftGameApplyRequest request{};
    request.key = entry.key;
    request.scope = scope;
    request.backend = OverrideBackend::OpenGL;
    request.source_stage = HostSourceStage::Fragment;
    request.interface_abi = 1;
    request.draft_id = 5;
    request.draft_revision = 2;
    request.attempted_revision = 2;
    request.successful_revision = 2;
    request.preview_submission_id = 9;
    request.successful_submission_id = 9;
    request.compiled_success = true;
    request.source = "#version 330\nvoid main() {}\n";
    request.successful_source_digest = ComputePreviewDigest(
        reinterpret_cast<const uint8_t *>(request.source.data()),
        request.source.size());
    std::string reason;
    assert(EvaluateDraftGameApply(request, entry, context, &reason));
    request.successful_revision = 1;
    assert(!EvaluateDraftGameApply(request, entry, context, &reason));
    request.successful_revision = 2;
    request.preview_submission_id = 10;
    assert(!EvaluateDraftGameApply(request, entry, context, &reason));
    request.preview_submission_id = 9;
    request.compiled_success = false;
    assert(!EvaluateDraftGameApply(request, entry, context, &reason));
    request.compiled_success = true;
    request.source += "// edited";
    assert(!EvaluateDraftGameApply(request, entry, context, &reason));
    request.source.resize(request.source.size() - 9);
    request.interface_abi = 2;
    assert(!EvaluateDraftGameApply(request, entry, context, &reason));
    request.interface_abi = 1;
    request.source_stage = HostSourceStage::Vertex;
    assert(!EvaluateDraftGameApply(request, entry, context, &reason));
    request.source_stage = HostSourceStage::Fragment;
    request.backend = OverrideBackend::Vulkan;
    assert(!EvaluateDraftGameApply(request, entry, context, &reason));
    request.backend = OverrideBackend::OpenGL;
    context.executable_fingerprint[0] = 0x43;
    assert(!EvaluateDraftGameApply(request, entry, context, &reason));
    context.executable_fingerprint[0] = 0x42;
    entry.key.stage = Stage::Vertex;
    request.key = entry.key;
    assert(!EvaluateDraftGameApply(request, entry, context, &reason));
    entry.key.stage = Stage::Pixel;
    request.key = entry.key;

    // The environment supplies an isolated test root on native rigs. There
    // are no filesystem side effects when it is absent.
    const char *test_root = std::getenv("XEMU_SHADER_WORKBENCH_TEST_ROOT");
    if (test_root && *test_root) {
        const auto base = std::filesystem::u8path(test_root) /
                          "workbench-apply-test";
        std::filesystem::remove_all(base);
        std::filesystem::create_directories(base);
        OverrideStore store;
        ReplacementLibrary library;
        store.SetContext(context);
        library.Configure(base.u8string());
        std::filesystem::create_directories(
            base / "shader-replacements" / "unrelated-invalid-package");
        WorkbenchAppliedRule first{}, second{};
        assert(ApplyWorkbenchDraft(request, entry, &store, &library, base,
                                   &first, &reason));
        assert(first.id && first.scope == scope);
        assert(ApplyWorkbenchDraft(request, entry, &store, &library, base,
                                   &second, &reason));
        assert(second.id && second.id != first.id);
        OverrideStoreSnapshot snapshot{};
        store.CopySnapshot(&snapshot);
        assert(snapshot.rules.size() == 2);
        assert(RestoreWorkbenchDraft(first, &store, &reason));
        store.CopySnapshot(&snapshot);
        assert(snapshot.rules.size() == 1);
        assert(snapshot.rules[0].id == second.id);
        assert(!RestoreWorkbenchDraft(first, &store, &reason));
        assert(RestoreWorkbenchDraft(second, &store, &reason));
        std::filesystem::remove_all(base);
    }
    return 0;
}

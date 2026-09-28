#include "../../ui/xui/shader-browser-workbench-apply.hh"
#include "../../ui/xui/shader-browser-override-store.hh"
#include "../../ui/xui/shader-browser-replacement-library.hh"
#include "../../ui/xui/shader-browser-saved-rules.hh"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <chrono>
#include <filesystem>
#include <fstream>

using namespace xemu::shader_browser;

#define CHECK(condition)                                                \
    do {                                                                \
        if (!(condition)) {                                             \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, \
                         __LINE__, #condition);                         \
            std::abort();                                               \
        }                                                               \
    } while (false)

static void TestSettingsFileSaveProfile(const DraftGameApplyRequest &request,
                                        const Entry &entry,
                                        const std::filesystem::path &root)
{
    const auto global_directory = root / "global-data";
    const auto profile_directory =
        root / std::filesystem::u8path(u8"custom-profile-\u00e9");
    const auto settings_path = profile_directory / "custom-xemu.toml";
    const std::string settings_text = settings_path.u8string();
    CHECK(std::filesystem::create_directory(profile_directory));
    OverrideStore store;
    ReplacementLibrary library;
    library.Configure(profile_directory.u8string());
    const std::string expected_library_root = library.RootPath();
    WorkbenchSavedReplacement saved{};
    std::string reason;
    CHECK(!SaveWorkbenchDraftForSettingsPath(request, entry, &store, &library,
                                             nullptr, &saved, &reason));
    CHECK(!SaveWorkbenchDraftForSettingsPath(request, entry, &store, &library,
                                             "", &saved, &reason));
    // The old UI caller used the global data directory despite -config_path.
    CHECK(!SaveWorkbenchDraft(request, entry, &store, &library,
                              global_directory, &saved, &reason));
    CHECK(reason == "Replacement library is not configured for this profile");
    CHECK(SaveWorkbenchDraftForSettingsPath(request, entry, &store, &library,
                                            settings_text.c_str(), &saved,
                                            &reason));
    CHECK(library.RootPath() == expected_library_root);
    CHECK(!std::filesystem::exists(global_directory));
    CHECK(std::filesystem::is_regular_file(profile_directory /
                                           "shader-replacements" /
                                           saved.logical_id / "source.glsl"));
    OverrideStoreSnapshot snapshot{};
    store.CopySnapshot(&snapshot);
    CHECK(snapshot.rules.empty());

    OverrideStore reopened_store;
    ReplacementLibrary reopened_library;
    reopened_library.Configure(profile_directory.u8string());
    CHECK(reopened_library.Reload(&reopened_store, &reason));
    const auto reopened = reopened_store.AcquireReplacement(
        saved.replacement_id, saved.content_revision, request.backend);
    CHECK(reopened && reopened->opengl_source == request.source);
    reopened_store.CopySnapshot(&snapshot);
    CHECK(snapshot.rules.empty());

    // A settings path must never redirect a differently configured library.
    ReplacementLibrary wrong_library;
    wrong_library.Configure(global_directory.u8string());
    const std::string wrong_root = wrong_library.RootPath();
    CHECK(!SaveWorkbenchDraftForSettingsPath(
        request, entry, &reopened_store, &wrong_library, settings_text.c_str(),
        &saved, &reason));
    CHECK(reason == "Replacement library is not configured for this profile");
    CHECK(wrong_library.RootPath() == wrong_root);
    CHECK(!std::filesystem::exists(global_directory));
    reopened_store.CopySnapshot(&snapshot);
    CHECK(snapshot.rules.empty());

    // A bare relative settings filename has the same dirname as GLib: '.'.
    const auto relative_directory = root / "relative-profile";
    CHECK(std::filesystem::create_directory(relative_directory));
    const auto previous_directory = std::filesystem::current_path();
    std::filesystem::current_path(relative_directory);
    OverrideStore relative_store;
    ReplacementLibrary relative_library;
    relative_library.Configure(".");
    CHECK(SaveWorkbenchDraftForSettingsPath(
        request, entry, &relative_store, &relative_library,
        "relative-xemu.toml", &saved, &reason));
    CHECK(std::filesystem::is_regular_file(
        std::filesystem::path("shader-replacements") / saved.logical_id /
        "source.glsl"));
    relative_store.CopySnapshot(&snapshot);
    CHECK(snapshot.rules.empty());
    std::filesystem::current_path(previous_directory);
}

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
    CHECK(EvaluateDraftSave(request, entry, &reason));
    CHECK(EvaluateDraftGameApply(request, entry, context, &reason));
    request.successful_revision = 1;
    CHECK(!EvaluateDraftGameApply(request, entry, context, &reason));
    request.successful_revision = 2;
    request.preview_submission_id = 10;
    CHECK(!EvaluateDraftGameApply(request, entry, context, &reason));
    request.preview_submission_id = 9;
    request.compiled_success = false;
    CHECK(!EvaluateDraftGameApply(request, entry, context, &reason));
    request.compiled_success = true;
    request.source += "// edited";
    CHECK(!EvaluateDraftGameApply(request, entry, context, &reason));
    request.source.resize(request.source.size() - 9);
    request.interface_abi = 2;
    CHECK(!EvaluateDraftGameApply(request, entry, context, &reason));
    request.interface_abi = 1;
    request.source_stage = HostSourceStage::Vertex;
    CHECK(!EvaluateDraftGameApply(request, entry, context, &reason));
    request.source_stage = HostSourceStage::Fragment;
    request.backend = OverrideBackend::Vulkan;
    CHECK(EvaluateDraftSave(request, entry, &reason));
    CHECK(!EvaluateDraftGameApply(request, entry, context, &reason));
    request.backend = OverrideBackend::OpenGL;
    context.executable_fingerprint[0] = 0x43;
    CHECK(EvaluateDraftSave(request, entry, &reason));
    CHECK(!EvaluateDraftGameApply(request, entry, context, &reason));
    context.executable_fingerprint[0] = 0x42;
    request.scope.executable_fingerprint[0] = 0x43;
    CHECK(!EvaluateDraftSave(request, entry, &reason));
    request.scope.executable_fingerprint[0] = 0x42;
    request.scope.executable_fingerprint_version = 0;
    CHECK(!EvaluateDraftSave(request, entry, &reason));
    request.scope.executable_fingerprint_version = 1;
    request.backend = OverrideBackend::Unknown;
    CHECK(!EvaluateDraftSave(request, entry, &reason));
    request.backend = OverrideBackend::OpenGL;
    entry.key.stage = Stage::Vertex;
    request.key = entry.key;
    CHECK(!EvaluateDraftGameApply(request, entry, context, &reason));
    entry.key.stage = Stage::Pixel;
    request.key = entry.key;

    const char *test_root = std::getenv("XEMU_SHADER_WORKBENCH_TEST_ROOT");
    {
        const auto parent = test_root && *test_root ?
                                std::filesystem::u8path(test_root) :
                                std::filesystem::temp_directory_path();
        const auto stamp =
            std::chrono::steady_clock::now().time_since_epoch().count();
        const auto base =
            parent / ("workbench-apply-test-" + std::to_string(stamp));
        std::filesystem::create_directories(parent);
        CHECK(std::filesystem::create_directory(base));
        TestSettingsFileSaveProfile(request, entry, base);
        OverrideStore store;
        ReplacementLibrary library;
        // A reopened archive may be edited and saved with no active game.
        OverrideContext offline_context{};
        offline_context.backend = request.backend;
        store.SetContext(offline_context);
        library.Configure(base.u8string());
        std::filesystem::create_directories(
            base / "shader-replacements" / "unrelated-invalid-package");
        WorkbenchSavedReplacement saved{};
        CHECK(SaveWorkbenchDraft(request, entry, &store, &library, base, &saved,
                                 &reason));
        CHECK(saved.replacement_id && saved.content_revision);
        CHECK(saved.request.scope == scope);
        CHECK(saved.request.successful_source_digest ==
              request.successful_source_digest);
        ReplacementLibrarySnapshot packages{};
        library.CopySnapshot(&packages);
        CHECK(packages.packages.size() == 1);
        CHECK(packages.packages[0].logical_id == saved.logical_id);
        OverrideStoreSnapshot snapshot{};
        store.CopySnapshot(&snapshot);
        CHECK(snapshot.rules.empty());
        WorkbenchSavedReplacement saved_again{};
        CHECK(SaveWorkbenchDraft(request, entry, &store, &library, base,
                                 &saved_again, &reason));
        CHECK(saved_again.logical_id == saved.logical_id);
        CHECK(saved_again.replacement_id == saved.replacement_id);
        store.CopySnapshot(&snapshot);
        CHECK(snapshot.rules.empty());

        WorkbenchAppliedRule offline_enable{};
        CHECK(!EnableWorkbenchReplacement(saved, entry, &store, nullptr, false,
                                          &offline_enable, &reason));
        store.CopySnapshot(&snapshot);
        CHECK(snapshot.rules.empty());
        context.backend = OverrideBackend::Vulkan;
        store.SetContext(context);
        CHECK(SaveWorkbenchDraft(request, entry, &store, &library, base,
                                 &saved_again, &reason));
        CHECK(!EnableWorkbenchReplacement(saved, entry, &store, nullptr, false,
                                          &offline_enable, &reason));
        store.CopySnapshot(&snapshot);
        CHECK(snapshot.rules.empty());
        OverrideStore offline_reopened;
        ReplacementLibrary reopened_library;
        reopened_library.Configure(base.u8string());
        // An unrelated invalid package may make Reload return false; the
        // previously saved immutable package must still be admitted.
        reopened_library.Reload(&offline_reopened, &reason);
        reopened_library.CopySnapshot(&packages);
        CHECK(packages.packages.size() == 1);
        CHECK(packages.packages[0].logical_id == saved.logical_id);
        const auto reopened_payload = offline_reopened.AcquireReplacement(
            saved.replacement_id, saved.content_revision, request.backend);
        CHECK(reopened_payload);
        CHECK(reopened_payload->opengl_source == saved.request.source);
        offline_reopened.CopySnapshot(&snapshot);
        CHECK(snapshot.rules.empty());
        context.backend = OverrideBackend::OpenGL;
        store.SetContext(context);

        // Saving owns the validated source independently of further edits.
        const std::string validated_source = request.source;
        request.source += "// later draft edit\n";
        CHECK(saved.request.source == validated_source);
        WorkbenchAppliedRule enabled{};
        CHECK(EnableWorkbenchReplacement(saved, entry, &store, nullptr, false,
                                         &enabled, &reason));
        store.CopySnapshot(&snapshot);
        CHECK(snapshot.rules.size() == 1);
        CHECK(snapshot.rules[0].origin == OverrideOrigin::Session);
        CHECK(snapshot.rules[0].replacement_id == saved.replacement_id);
        CHECK(snapshot.rules[0].title_id == scope.title_id);
        CHECK(snapshot.rules[0].shader == entry.key);
        CHECK(snapshot.rules[0].restrict_build);
        CHECK(snapshot.rules[0].executable_fingerprint ==
              scope.executable_fingerprint);
        CHECK(store.Resolve(entry.key).policy.replacement_id ==
              saved.replacement_id);
        CHECK(DisableWorkbenchReplacement(enabled, &store, nullptr, &reason));
        store.CopySnapshot(&snapshot);
        CHECK(snapshot.rules.empty());
        request.source = validated_source;

        // A saved snapshot cannot be enabled for a different running build.
        context.executable_fingerprint[0] = 0x43;
        store.SetContext(context);
        CHECK(!EnableWorkbenchReplacement(saved, entry, &store, nullptr, false,
                                          &enabled, &reason));
        store.CopySnapshot(&snapshot);
        CHECK(snapshot.rules.empty());
        context.executable_fingerprint[0] = 0x42;
        store.SetContext(context);
        auto tampered = saved;
        tampered.request.source += "// changed snapshot\n";
        CHECK(!EnableWorkbenchReplacement(tampered, entry, &store, nullptr,
                                          false, &enabled, &reason));
        tampered = saved;
        ++tampered.content_revision;
        CHECK(!EnableWorkbenchReplacement(tampered, entry, &store, nullptr,
                                          false, &enabled, &reason));
        CHECK(!EnableWorkbenchReplacement(saved, entry, &store, nullptr, true,
                                          &enabled, &reason));
        store.CopySnapshot(&snapshot);
        CHECK(snapshot.rules.empty());

        const auto source_path =
            base / "shader-replacements" / saved.logical_id / "source.glsl";
        {
            std::ofstream changed_source(source_path, std::ios::binary);
            changed_source << validated_source << "// changed package\n";
        }
        library.Reload(&store, &reason);
        CHECK(!EnableWorkbenchReplacement(saved, entry, &store, nullptr, false,
                                          &enabled, &reason));
        {
            std::ofstream original_source(source_path, std::ios::binary);
            original_source << validated_source;
        }
        library.Reload(&store, &reason);

        SavedOverrideRules saved_rules;
        CHECK(saved_rules.Configure(base.u8string(), true, &store, &reason));
        CHECK(EnableWorkbenchReplacement(saved, entry, &store, &saved_rules,
                                         true, &enabled, &reason));
        CHECK(enabled.id && enabled.saved_rule_id);
        store.CopySnapshot(&snapshot);
        CHECK(snapshot.rules.size() == 2);
        CHECK(std::count_if(snapshot.rules.begin(), snapshot.rules.end(),
                            [](const OverrideRule &rule) {
                                return rule.origin == OverrideOrigin::Saved;
                            }) == 1);
        auto changed_rule = *std::find_if(
            snapshot.rules.begin(), snapshot.rules.end(),
            [&](const OverrideRule &rule) { return rule.id == enabled.id; });
        ++changed_rule.revision;
        CHECK(store.UpsertRule(changed_rule, &reason));
        CHECK(!DisableWorkbenchReplacement(enabled, &store, &saved_rules,
                                           &reason));
        store.CopySnapshot(&snapshot);
        CHECK(snapshot.rules.size() == 2);
        --changed_rule.revision;
        CHECK(store.UpsertRule(changed_rule, &reason));

        OverrideRule unrelated = snapshot.rules[0];
        unrelated.id = 0x123;
        unrelated.priority = 0;
        unrelated.origin = OverrideOrigin::Saved;
        unrelated.action = OverrideAction::Normal;
        unrelated.replacement_id = 0;
        CHECK(saved_rules.Save(unrelated, &store, &reason));
        saved_rules.Close();
        OverrideStore reloaded;
        reloaded.SetContext(context);
        CHECK(saved_rules.Configure(base.u8string(), true, &reloaded, &reason));
        library.Reload(&reloaded, &reason);
        CHECK(reloaded.Resolve(entry.key).policy.replacement_id ==
              saved.replacement_id);
        reloaded.CopySnapshot(&snapshot);
        CHECK(snapshot.rules.size() == 2);
        CHECK(std::any_of(snapshot.rules.begin(), snapshot.rules.end(),
                          [&](const OverrideRule &rule) {
                              return rule.id == enabled.saved_rule_id &&
                                     rule.origin == OverrideOrigin::Saved &&
                                     rule.shader == entry.key &&
                                     rule.replacement_id ==
                                         saved.replacement_id &&
                                     rule.executable_fingerprint ==
                                         scope.executable_fingerprint;
                          }));
        // Remove persisted data and its session counterpart in the original
        // store, leaving unrelated rules intact in memory and after reload.
        CHECK(DisableWorkbenchReplacement(enabled, &store, &saved_rules,
                                          &reason));
        store.CopySnapshot(&snapshot);
        CHECK(snapshot.rules.size() == 1);
        CHECK(snapshot.rules[0].id == unrelated.id);
        CHECK(store.Resolve(entry.key).policy.rule_id == unrelated.id);
        CHECK(EnableWorkbenchReplacement(saved, entry, &store, &saved_rules,
                                         true, &enabled, &reason));
        store.ClearSessionRules();
        saved_rules.Close();
        CHECK(saved_rules.Configure(base.u8string(), true, &reloaded, &reason));
        CHECK(DisableWorkbenchReplacement(enabled, &reloaded, &saved_rules,
                                          &reason));
        reloaded.CopySnapshot(&snapshot);
        CHECK(snapshot.rules.size() == 1);
        CHECK(snapshot.rules[0].id == unrelated.id);
        CHECK(reloaded.Resolve(entry.key).policy.rule_id == unrelated.id);
        saved_rules.Close();
        CHECK(saved_rules.Configure(base.u8string(), true, &reloaded, &reason));
        reloaded.CopySnapshot(&snapshot);
        CHECK(snapshot.rules.size() == 1);
        CHECK(snapshot.rules[0].id == unrelated.id);
        saved_rules.Close();
        store.ClearSavedRules();

        WorkbenchAppliedRule first{}, second{};
        CHECK(ApplyWorkbenchDraft(request, entry, &store, &library, base,
                                  &first, &reason));
        CHECK(first.id && first.scope == scope);
        CHECK(ApplyWorkbenchDraft(request, entry, &store, &library, base,
                                  &second, &reason));
        CHECK(second.id && second.id != first.id);
        store.CopySnapshot(&snapshot);
        CHECK(snapshot.rules.size() == 2);
        CHECK(RestoreWorkbenchDraft(first, &store, &reason));
        store.CopySnapshot(&snapshot);
        CHECK(snapshot.rules.size() == 1);
        CHECK(snapshot.rules[0].id == second.id);
        CHECK(!RestoreWorkbenchDraft(first, &store, &reason));
        CHECK(RestoreWorkbenchDraft(second, &store, &reason));
        std::filesystem::remove_all(base);
    }
    return 0;
}

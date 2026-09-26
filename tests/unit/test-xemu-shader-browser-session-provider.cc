#include "../../ui/xui/shader-browser-provider.hh"
#include "../../ui/xui/shader-browser-session-provider.hh"

#include <cassert>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <thread>

using namespace xemu::shader_browser;

int main()
{
    std::filesystem::path root = std::filesystem::temp_directory_path() /
                                 "xemu-shader-provider-test";
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(root);

    assert(xemu_shader_browser_session_install(root.string().c_str()));
    char error[256] = {};
    assert(xemu_shader_browser_database_configure(0, 0, 0,
                                                   error, sizeof(error)));
    assert(!xemu_shader_browser_external_artifacts_enabled());

    XemuShaderBrowserScope current_scope{};
    uint64_t initial_scope_generation =
        xemu_shader_browser_copy_current_scope(&current_scope);
    assert(xemu_shader_browser_scope_generation() ==
           initial_scope_generation);
    assert(current_scope.title_id == 0);
    XemuShaderBrowserScope identified_scope{};
    identified_scope.title_id = 0x4d530064;
    identified_scope.executable_fingerprint_version = 1;
    identified_scope.executable_fingerprint[0] = 0x55;
    xemu_shader_browser_set_current_scope(&identified_scope);
    uint64_t identified_scope_generation =
        xemu_shader_browser_copy_current_scope(&current_scope);
    assert(identified_scope_generation > initial_scope_generation);
    assert(xemu_shader_browser_scope_generation() ==
           identified_scope_generation);
    assert(current_scope.title_id == identified_scope.title_id);
    assert(current_scope.executable_fingerprint[0] == 0x55);
    Snapshot scope_snapshot;
    assert(GetProvider().CopySnapshot(&scope_snapshot));
    assert(scope_snapshot.current_title_id == identified_scope.title_id);
    xemu_shader_browser_set_current_scope(&identified_scope);
    assert(xemu_shader_browser_copy_current_scope(&current_scope) ==
           identified_scope_generation);
    xemu_shader_browser_set_current_scope(nullptr);
    assert(xemu_shader_browser_copy_current_scope(&current_scope) >
           identified_scope_generation);
    assert(current_scope.title_id == 0);
    assert(GetProvider().CopySnapshot(&scope_snapshot));
    assert(scope_snapshot.current_title_id == 0);

    uint8_t recipe[] = {1, 2, 3, 4};
    XemuShaderBrowserShaderRecord shader{};
    shader.identity_version = 1;
    shader.stage = XEMU_SHADER_BROWSER_STAGE_PIXEL;
    shader.recipe_format_version = 1;
    shader.recipe_data = recipe;
    shader.recipe_size = sizeof(recipe);
    assert(xemu_shader_browser_compute_shader_hash(
        shader.identity_version, shader.stage, shader.recipe_format_version,
        shader.recipe_data, shader.recipe_size, shader.identity_hash));
    shader.scope.title_id = 0x4d530064;
    shader.scope.executable_fingerprint_version = 1;
    shader.scope.executable_fingerprint[0] = 0x55;
    XemuShaderBrowserShaderRecord bad_hash = shader;
    bad_hash.identity_hash[0] ^= 0xff;
    assert(!xemu_shader_browser_publish_shader(&bad_hash));
    assert(xemu_shader_browser_publish_shader(&shader));
    assert(!std::filesystem::exists(root / "shader-browser.db"));

    Snapshot snapshot;
    assert(GetProvider().CopySnapshot(&snapshot));
    assert(snapshot.entries.size() == 1);
    assert(!snapshot.database_enabled);
    assert(!snapshot.entries[0].stored_in_database);

    assert(xemu_shader_browser_database_configure(1, 1, 1,
                                                   error, sizeof(error)));
    assert(xemu_shader_browser_external_artifacts_enabled());
    assert(xemu_shader_browser_flush_database(error, sizeof(error)));
    assert(std::filesystem::exists(root / "shader-browser.db"));

    // Large generated/compiled artifacts are external files. Flush is a
    // durability boundary for both the file queue and its DB metadata.
    std::vector<uint8_t> artifact_bytes(8 * 1024 * 1024, 0x5a);
    XemuShaderBrowserExternalArtifact artifact{};
    artifact.identity_version = shader.identity_version;
    std::copy(std::begin(shader.identity_hash), std::end(shader.identity_hash),
              std::begin(artifact.identity_hash));
    artifact.stage = shader.stage;
    artifact.title_id = shader.scope.title_id;
    artifact.backend = "vulkan";
    artifact.route = "specialized";
    artifact.kind = "spirv";
    artifact.extension = "spv";
    artifact.generator_abi = 1;
    artifact.xemu_revision = "test";
    artifact.content_hash = "test-hash";
    artifact.data = artifact_bytes.data();
    artifact.size = artifact_bytes.size();
    assert(xemu_shader_browser_publish_external_artifact(&artifact));
    assert(xemu_shader_browser_flush_database(error, sizeof(error)));

    size_t artifact_files = 0;
    for (const auto &item : std::filesystem::recursive_directory_iterator(
             root / "shader-artifacts")) {
        if (item.is_regular_file()) artifact_files++;
    }
    assert(artifact_files == 1);

    assert(GetProvider().CopySnapshot(&snapshot));
    assert(snapshot.artifact_count == 1);
    assert(snapshot.external_artifact_bytes >= artifact_bytes.size());
    assert(snapshot.database_enabled && snapshot.database_open);

    // Turning persistence off closes SQLite but never deletes the DB or
    // user-visible artifact folder. Live/current-process browsing remains.
    assert(xemu_shader_browser_database_configure(0, 0, 0,
                                                   error, sizeof(error)));
    assert(!xemu_shader_browser_external_artifacts_enabled());
    assert(std::filesystem::exists(root / "shader-browser.db"));
    assert(artifact_files == 1);
    assert(GetProvider().CopySnapshot(&snapshot));
    assert(!snapshot.database_enabled);
    assert(snapshot.entries.size() == 1);
    assert(!snapshot.entries[0].stored_in_database);

    assert(xemu_shader_browser_database_configure(1, 1, 1,
                                                   error, sizeof(error)));
    assert(xemu_shader_browser_flush_database(error, sizeof(error)));
    assert(GetProvider().CopySnapshot(&snapshot));
    assert(snapshot.entries.size() == 1);
    assert(snapshot.entries[0].stored_in_database);
    assert(snapshot.stored_shader_count == 1);
    assert(snapshot.entries[0].stored_in_database);

    XemuShaderBrowserPerformanceSession session{};
    session.session_id = "provider-session";
    session.title_id = 0x4d530064;
    session.executable_fingerprint_version = 1;
    session.executable_fingerprint[0] = 0x55;
    session.started_unix_ms = 1000;
    session.renderer = "Vulkan";
    session.xemu_revision = "test";
    session.telemetry_version = 1;
    assert(xemu_shader_browser_performance_session_begin(
        &session, error, sizeof(error)));
    assert(xemu_shader_browser_session_collection_enabled());

    XemuShaderBrowserObservation observation{};
    observation.identity_version = 1;
    std::copy(std::begin(shader.identity_hash), std::end(shader.identity_hash),
              std::begin(observation.identity_hash));
    observation.stage = XEMU_SHADER_BROWSER_STAGE_PIXEL;
    observation.status = XEMU_SHADER_BROWSER_STATUS_NORMAL;
    observation.route = XEMU_SHADER_BROWSER_ROUTE_SPECIALIZED;
    observation.readiness = XEMU_SHADER_BROWSER_READINESS_READY;
    observation.flags = XEMU_SHADER_BROWSER_OBSERVED;
    observation.first_frame = 10;
    observation.last_frame = 12;
    observation.draw_count_delta = 3;
    observation.specialized_draw_delta = 3;
    observation.compile_cpu.sample_count = 2;
    observation.compile_cpu.total_ns = 4000000;
    observation.compile_cpu.min_ns = 1000000;
    observation.compile_cpu.max_ns = 3000000;
    xemu_shader_browser_publish_observations(&observation, 1);
    assert(xemu_shader_browser_performance_session_end(
        "provider-session", 2000, 1, error, sizeof(error)));
    assert(xemu_shader_browser_flush_database(error, sizeof(error)));

    // Live clear is independent from durable performance-session storage.
    GetProvider().SetLiveCollectionEnabled(true);
    xemu_shader_browser_publish_observations(&observation, 1);
    assert(GetProvider().CopySnapshot(&snapshot));
    assert(snapshot.entries[0].draw_count == 3);
    xemu_shader_browser_session_clear_live();
    assert(GetProvider().CopySnapshot(&snapshot));
    assert(snapshot.entries[0].draw_count == 0);
    assert(snapshot.session_count >= 1);

    std::string clear_error;
    assert(GetProvider().ClearPerformanceHistory(&clear_error));
    assert(GetProvider().CopySnapshot(&snapshot));
    assert(snapshot.session_count == 0);
    assert(snapshot.session_stat_count == 0);

    xemu_shader_browser_session_uninstall();
    std::filesystem::remove_all(root, ec);
    std::cout << "shader browser session provider tests passed\n";
    return 0;
}

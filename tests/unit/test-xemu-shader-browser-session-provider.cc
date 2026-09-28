#include "../../ui/xui/shader-browser-provider.hh"
#include "../../ui/xui/shader-browser-database.hh"
#include "../../ui/xui/shader-browser-session-provider.hh"

#include <cassert>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <thread>
#include <sqlite3.h>

using namespace xemu::shader_browser;

static void test_shared_pipeline_members_keep_separate_costs(
    const std::filesystem::path &root)
{
    const auto path = root / "shared-pipeline";
    assert(xemu_shader_browser_session_install(path.string().c_str()));
    char error[256] = {};
    assert(
        xemu_shader_browser_database_configure(1, 1, 0, error, sizeof(error)));
    XemuShaderBrowserScope scope{};
    scope.title_id = 0x4d530064;
    xemu_shader_browser_set_current_scope(&scope);
    XemuShaderBrowserProfilingConfig profiling{};
    profiling.monitoring_level = XEMU_SHADER_BROWSER_MONITOR_DIAGNOSTIC;
    profiling.cpu_timing = 1;
    profiling.gpu_timing = 1;
    xemu_shader_browser_configure_profiling(&profiling);
    GetProvider().SetLiveCollectionEnabled(true);
    XemuShaderBrowserPerformanceSession session{};
    session.session_id = "shared-pipeline-session";
    session.title_id = scope.title_id;
    session.started_unix_ms = 1000;
    session.renderer = "Vulkan";
    assert(xemu_shader_browser_performance_session_begin(&session, error,
                                                         sizeof(error)));

    XemuShaderBrowserPerformanceSample first{};
    first.owner = XEMU_SHADER_BROWSER_PERF_BINDING;
    first.metric = XEMU_SHADER_BROWSER_PERF_DRAW_GPU;
    first.backend = XEMU_SHADER_BROWSER_BACKEND_VK;
    first.route = XEMU_SHADER_BROWSER_ROUTE_UBER;
    first.variant_kind = XEMU_SHADER_BROWSER_VARIANT_PIPELINE;
    first.variant_id = 17;
    first.flags = XEMU_SHADER_BROWSER_SAMPLE_SAMPLED;
    first.frame = 1;
    first.represented_draws = 1;
    first.duration_ns = 2000;
    first.identity_count = 2;
    first.identities[0].version = 1;
    first.identities[0].stage = XEMU_SHADER_BROWSER_STAGE_VERTEX;
    first.identities[1].version = 1;
    first.identities[1].stage = XEMU_SHADER_BROWSER_STAGE_PIXEL;
    first.identities[1].hash[0] = 1;
    assert(xemu_shader_browser_capture_performance_context(&first.context));
    XemuShaderBrowserPerformanceSample second = first;
    second.identities[1].hash[0] = 2;
    second.duration_ns = 7000;
    xemu_shader_browser_publish_performance_samples(&first, 1);
    xemu_shader_browser_publish_performance_samples(&second, 1);

    auto check_costs = [](const Snapshot &snapshot) {
        assert(snapshot.binding_variants.size() == 2);
        for (const BindingVariant &variant : snapshot.binding_variants) {
            assert(variant.variant_id == 17);
            assert(variant.members.size() == 2);
            const auto pixel = std::find_if(
                variant.members.begin(), variant.members.end(),
                [](const ShaderKey &key) { return key.stage == Stage::Pixel; });
            assert(pixel != variant.members.end());
            const auto &cost =
                variant.timing[XEMU_SHADER_BROWSER_PERF_DRAW_GPU];
            assert(cost.samples == 1);
            assert(variant.sampled_gpu_draws == 1);
            if (pixel->hash.bytes[0] == 1) {
                assert(cost.total_ns == 2000);
            } else {
                assert(pixel->hash.bytes[0] == 2);
                assert(cost.total_ns == 7000);
            }
        }
    };
    Snapshot snapshot{};
    assert(GetProvider().CopySnapshot(&snapshot));
    check_costs(snapshot);
    assert(snapshot.dropped_performance_samples == 0);
    assert(xemu_shader_browser_performance_session_end(
        session.session_id, 2000, 1, error, sizeof(error)));
    assert(xemu_shader_browser_flush_database(error, sizeof(error)));
    GetProvider().SetTimingSession(session.session_id);
    assert(GetProvider().CopySnapshot(&snapshot));
    assert(snapshot.timing_session_id == session.session_id);
    check_costs(snapshot);
    assert(snapshot.dropped_performance_samples == 0);
    xemu_shader_browser_session_uninstall();
}

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

    // Off is a real zero-monitoring mode, including when the browser asks
    // for live collection. CPU/GPU switches are effective only in Diagnostic.
    assert(!xemu_shader_browser_monitoring_enabled());
    assert(!xemu_shader_browser_cpu_profiling_enabled());
    assert(!xemu_shader_browser_gpu_profiling_enabled());
    GetProvider().SetLiveCollectionEnabled(true);
    assert(!xemu_shader_browser_session_collection_enabled());
    xemu_shader_browser_publish_frame(1);
    Snapshot off_snapshot{};
    assert(GetProvider().CopySnapshot(&off_snapshot));
    assert(off_snapshot.current_frame == 0);
    uint64_t off_generation = xemu_shader_browser_scope_generation();
    XemuShaderBrowserProfilingConfig profiling{};
    profiling.monitoring_level = XEMU_SHADER_BROWSER_MONITOR_BASIC;
    profiling.cpu_timing = 1;
    profiling.gpu_timing = 1;
    profiling.draw_sample_interval = 0;
    profiling.max_gpu_samples_per_frame = 1000;
    xemu_shader_browser_configure_profiling(&profiling);
    assert(xemu_shader_browser_scope_generation() > off_generation);
    assert(xemu_shader_browser_monitoring_enabled());
    assert(xemu_shader_browser_session_collection_enabled());
    assert(!xemu_shader_browser_cpu_profiling_enabled());
    assert(!xemu_shader_browser_gpu_profiling_enabled());
    profiling.monitoring_level = XEMU_SHADER_BROWSER_MONITOR_DIAGNOSTIC;
    xemu_shader_browser_configure_profiling(&profiling);
    assert(xemu_shader_browser_cpu_profiling_enabled());
    assert(xemu_shader_browser_gpu_profiling_enabled());
    XemuShaderBrowserProfilingConfig effective{};
    xemu_shader_browser_copy_profiling_config(&effective);
    assert(effective.draw_sample_interval == 1);
    assert(effective.max_gpu_samples_per_frame == 64);
    profiling.monitoring_level = XEMU_SHADER_BROWSER_MONITOR_OFF;
    xemu_shader_browser_configure_profiling(&profiling);
    assert(!xemu_shader_browser_session_collection_enabled());
    profiling.monitoring_level = XEMU_SHADER_BROWSER_MONITOR_BASIC;
    xemu_shader_browser_configure_profiling(&profiling);
    GetProvider().SetLiveCollectionEnabled(false);

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
    CanonicalRecipe copied_recipe{};
    assert(GetProvider().CopyCanonicalRecipe(snapshot.entries[0].key,
                                              &copied_recipe));
    assert(copied_recipe.bytes == std::vector<uint8_t>(recipe,
                                                       recipe + sizeof(recipe)));
    assert(copied_recipe.scopes.size() == 1);
    assert(copied_recipe.scopes.front().title_id == shader.scope.title_id);
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

    XemuShaderBrowserExternalArtifact oversized = artifact;
    oversized.size = 32U * 1024U * 1024U + 1;
    assert(!xemu_shader_browser_publish_external_artifact(&oversized));
    assert(GetProvider().CopySnapshot(&snapshot));
    assert(snapshot.rejected_artifact_count == 1);

    // A renderer may publish while XUI toggles artifact persistence. The
    // configuration boundary must drain accepted jobs before closing SQLite.
    std::vector<uint8_t> small_artifact(1024, 0x31);
    XemuShaderBrowserExternalArtifact concurrent = artifact;
    concurrent.data = small_artifact.data();
    concurrent.size = small_artifact.size();
    concurrent.kind = "glsl";
    concurrent.extension = "glsl";
    std::thread publisher([&] {
        for (int i = 0; i < 100; ++i) {
            xemu_shader_browser_publish_external_artifact(&concurrent);
        }
    });
    for (int i = 0; i < 10; ++i) {
        assert(xemu_shader_browser_database_configure(
            1, 1, 0, error, sizeof(error)));
        assert(xemu_shader_browser_database_configure(
            1, 1, 1, error, sizeof(error)));
    }
    publisher.join();
    assert(xemu_shader_browser_flush_database(error, sizeof(error)));

    size_t artifact_files = 0;
    for (const auto &item : std::filesystem::recursive_directory_iterator(
             root / "shader-artifacts")) {
        if (item.is_regular_file()) artifact_files++;
    }
    assert(artifact_files >= 1);

    assert(GetProvider().CopySnapshot(&snapshot));
    assert(snapshot.artifact_count >= 1);
    assert(snapshot.external_artifact_bytes >= artifact_bytes.size());
    assert(snapshot.database_enabled && snapshot.database_open);

    // Turning persistence off closes SQLite but never deletes the DB or
    // user-visible artifact folder. Live/current-process browsing remains.
    assert(xemu_shader_browser_database_configure(0, 0, 0,
                                                   error, sizeof(error)));
    assert(!xemu_shader_browser_external_artifacts_enabled());
    assert(std::filesystem::exists(root / "shader-browser.db"));
    assert(artifact_files >= 1);
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
    profiling.monitoring_level = XEMU_SHADER_BROWSER_MONITOR_DIAGNOSTIC;
    xemu_shader_browser_configure_profiling(&profiling);
    GetProvider().SetLiveCollectionEnabled(false);
    XemuShaderBrowserPerformanceSample recorded_sample{};
    recorded_sample.owner = XEMU_SHADER_BROWSER_PERF_STAGE;
    recorded_sample.metric = XEMU_SHADER_BROWSER_PERF_COMPILE_CPU;
    recorded_sample.backend = XEMU_SHADER_BROWSER_BACKEND_VK;
    recorded_sample.route = XEMU_SHADER_BROWSER_ROUTE_SPECIALIZED;
    recorded_sample.flags = XEMU_SHADER_BROWSER_SAMPLE_BACKGROUND;
    assert(xemu_shader_browser_capture_performance_context(
        &recorded_sample.context));
    recorded_sample.duration_ns = 7654;
    recorded_sample.identity_count = 1;
    recorded_sample.identities[0].version = shader.identity_version;
    recorded_sample.identities[0].stage = shader.stage;
    std::memcpy(recorded_sample.identities[0].hash, shader.identity_hash,
                sizeof(shader.identity_hash));
    xemu_shader_browser_publish_performance_samples(&recorded_sample, 1);
    recorded_sample.owner = XEMU_SHADER_BROWSER_PERF_BINDING;
    recorded_sample.variant_kind = XEMU_SHADER_BROWSER_VARIANT_PIPELINE;
    recorded_sample.metric = XEMU_SHADER_BROWSER_PERF_DRAW_SUBMIT_CPU;
    recorded_sample.variant_id = 987;
    recorded_sample.duration_ns = 2345;
    recorded_sample.identity_count = 2;
    recorded_sample.identities[1] = recorded_sample.identities[0];
    recorded_sample.identities[1].stage = XEMU_SHADER_BROWSER_STAGE_VERTEX;
    recorded_sample.identities[1].hash[0] ^= 0x10;
    xemu_shader_browser_publish_performance_samples(&recorded_sample, 1);

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
    std::thread concurrent_snapshot([&] {
        char flush_error[256] = {};
        assert(xemu_shader_browser_flush_database(
            flush_error, sizeof(flush_error)));
    });
    assert(xemu_shader_browser_performance_session_end(
        "provider-session", 2000, 1, error, sizeof(error)));
    concurrent_snapshot.join();
    assert(xemu_shader_browser_flush_database(error, sizeof(error)));
    ShaderDatabase inspect;
    DatabaseConfig inspect_config{};
    inspect_config.base_path = root.string();
    inspect_config.enabled = true;
    inspect_config.record_performance_sessions = true;
    std::string inspect_error;
    assert(inspect.Configure(inspect_config, &inspect_error));
    auto persisted = inspect.CopySessionStats("provider-session");
    assert(persisted.size() == 1);
    assert(persisted[0].draw_count == 3);
    assert(persisted[0].compile_cpu.total_ns == 4000000);
    auto recorded_sessions = inspect.CopySessions(0x4d530064, 4);
    assert(!recorded_sessions.empty());
    assert(recorded_sessions[0].cpu_model == "Unknown");
    auto persisted_samples =
        inspect.CopyPerformanceAggregates("provider-session");
    assert(persisted_samples.size() == 2);
    assert(persisted_samples[0].duration.total_ns == 7654 ||
           persisted_samples[1].duration.total_ns == 7654);
    assert(std::any_of(persisted_samples.begin(), persisted_samples.end(),
                       [](const PerformanceAggregate &row) {
                           return row.variant_kind ==
                                  XEMU_SHADER_BROWSER_VARIANT_PIPELINE;
                       }));
    inspect.Close();

    // An asynchronous result issued in session A cannot enter session B.
    XemuShaderBrowserPerformanceSession next_session = session;
    next_session.session_id = "fresh-session";
    next_session.started_unix_ms = 3000;
    assert(xemu_shader_browser_performance_session_begin(&next_session, error,
                                                         sizeof(error)));
    xemu_shader_browser_publish_performance_samples(&recorded_sample, 1);
    assert(xemu_shader_browser_performance_session_end("fresh-session", 4000, 1,
                                                       error, sizeof(error)));
    assert(xemu_shader_browser_flush_database(error, sizeof(error)));
    assert(inspect.Configure(inspect_config, &inspect_error));
    assert(inspect.CopyPerformanceAggregates("fresh-session").empty());
    inspect.Close();

    // Snapshot draining runs concurrently with session creation. Every
    // sample captured after begin must reach its own recorded session.
    std::atomic<bool> keep_snapshots{ true };
    std::thread snapshot_reader([&] {
        Snapshot concurrent{};
        while (keep_snapshots.load(std::memory_order_acquire)) {
            assert(GetProvider().CopySnapshot(&concurrent));
        }
    });
    for (int trial = 0; trial < 16; ++trial) {
        std::string id = "boundary-session-" + std::to_string(trial);
        XemuShaderBrowserPerformanceSession boundary_session = session;
        boundary_session.session_id = id.c_str();
        boundary_session.started_unix_ms = 5000 + trial;
        assert(xemu_shader_browser_performance_session_begin(
            &boundary_session, error, sizeof(error)));
        XemuShaderBrowserPerformanceSample boundary_sample = recorded_sample;
        assert(xemu_shader_browser_capture_performance_context(
            &boundary_sample.context));
        xemu_shader_browser_publish_performance_samples(&boundary_sample, 1);
        assert(xemu_shader_browser_performance_session_end(
            id.c_str(), 6000 + trial, 1, error, sizeof(error)));
    }
    keep_snapshots.store(false, std::memory_order_release);
    snapshot_reader.join();
    assert(xemu_shader_browser_flush_database(error, sizeof(error)));
    assert(inspect.Configure(inspect_config, &inspect_error));
    for (int trial = 0; trial < 16; ++trial) {
        assert(inspect
                   .CopyPerformanceAggregates("boundary-session-" +
                                              std::to_string(trial))
                   .size() == 1);
    }
    inspect.Close();
    xemu_shader_browser_set_current_scope(&identified_scope);
    GetProvider().SetTimingSession("provider-session");
    assert(GetProvider().CopySnapshot(&snapshot));
    assert(snapshot.timing_session_id == "provider-session");
    assert(snapshot.entries.size() == 1);
    assert(snapshot.entries[0].compile_cpu.total_ns == 7654);
    assert(snapshot.stage_profiles.size() == 1);
    assert(snapshot.stage_profiles[0].backend ==
           XEMU_SHADER_BROWSER_BACKEND_VK);
    assert(snapshot.stage_profiles[0].flags ==
           XEMU_SHADER_BROWSER_SAMPLE_BACKGROUND);
    assert(snapshot.stage_profiles[0]
               .timing[XEMU_SHADER_BROWSER_PERF_COMPILE_CPU]
               .total_ns == 7654);
    assert(snapshot.binding_variants.size() == 1);
    assert(snapshot.binding_variants[0].variant_kind ==
           XEMU_SHADER_BROWSER_VARIANT_PIPELINE);
    GetProvider().SetTimingSession("");
    assert(GetProvider().CopySnapshot(&snapshot));
    assert(snapshot.timing_session_id.empty());
    assert(snapshot.binding_variants.empty());
    xemu_shader_browser_set_current_scope(nullptr);
    profiling.monitoring_level = XEMU_SHADER_BROWSER_MONITOR_BASIC;
    xemu_shader_browser_configure_profiling(&profiling);

    XemuShaderBrowserPerformanceSession idle_session = session;
    idle_session.session_id = "idle-snapshot";
    assert(xemu_shader_browser_performance_session_begin(
        &idle_session, error, sizeof(error)));
    xemu_shader_browser_publish_observations(&observation, 1);
    assert(xemu_shader_browser_flush_database(error, sizeof(error)));
    sqlite3 *audit = nullptr;
    assert(sqlite3_open((root / "shader-browser.db").string().c_str(),
                        &audit) == SQLITE_OK);
    assert(sqlite3_exec(audit,
        "CREATE TABLE snapshot_audit(n INTEGER);"
        "CREATE TRIGGER count_snapshot_update AFTER UPDATE ON "
        "shader_session_stats BEGIN INSERT INTO snapshot_audit VALUES(1); END;",
        nullptr, nullptr, nullptr) == SQLITE_OK);
    auto audit_count = [&] {
        sqlite3_stmt *statement = nullptr;
        assert(sqlite3_prepare_v2(audit, "SELECT COUNT(*) FROM snapshot_audit",
                                  -1, &statement, nullptr) == SQLITE_OK);
        assert(sqlite3_step(statement) == SQLITE_ROW);
        int count = sqlite3_column_int(statement, 0);
        sqlite3_finalize(statement);
        return count;
    };
    assert(xemu_shader_browser_flush_database(error, sizeof(error)));
    assert(audit_count() == 0);
    xemu_shader_browser_publish_observations(&observation, 1);
    assert(xemu_shader_browser_flush_database(error, sizeof(error)));
    assert(audit_count() == 1);
    assert(xemu_shader_browser_performance_session_end(
        "idle-snapshot", 3000, 1, error, sizeof(error)));
    assert(xemu_shader_browser_flush_database(error, sizeof(error)));
    assert(audit_count() == 1);
    sqlite3_close(audit);

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

    // Explicit disable/re-enable reloads committed rows and backfills the
    // process-lifetime discovery catalog after a failed writer transaction.
    sqlite3 *failure_inject = nullptr;
    assert(sqlite3_open((root / "shader-browser.db").string().c_str(),
                        &failure_inject) == SQLITE_OK);
    assert(sqlite3_exec(failure_inject,
        "CREATE TRIGGER fail_new_title BEFORE INSERT ON titles "
        "BEGIN SELECT RAISE(FAIL, 'injected provider write failure'); END;",
        nullptr, nullptr, nullptr) == SQLITE_OK);
    uint8_t recovery_recipe[] = {9, 8, 7, 6};
    XemuShaderBrowserShaderRecord recovery_shader = shader;
    recovery_shader.recipe_data = recovery_recipe;
    recovery_shader.scope.title_id = 0x12345678;
    assert(xemu_shader_browser_compute_shader_hash(
        recovery_shader.identity_version, recovery_shader.stage,
        recovery_shader.recipe_format_version, recovery_recipe,
        sizeof(recovery_recipe), recovery_shader.identity_hash));
    assert(xemu_shader_browser_publish_shader(&recovery_shader));
    assert(!xemu_shader_browser_flush_database(error, sizeof(error)));
    assert(GetProvider().CopySnapshot(&snapshot));
    assert(snapshot.database_write_failed);
    assert(!xemu_shader_browser_external_artifacts_enabled());
    assert(xemu_shader_browser_database_configure(0, 0, 0,
                                                   error, sizeof(error)));
    assert(sqlite3_exec(failure_inject, "DROP TRIGGER fail_new_title",
                        nullptr, nullptr, nullptr) == SQLITE_OK);
    sqlite3_close(failure_inject);
    assert(xemu_shader_browser_database_configure(1, 1, 1,
                                                   error, sizeof(error)));
    assert(xemu_shader_browser_flush_database(error, sizeof(error)));
    assert(GetProvider().CopySnapshot(&snapshot));
    assert(!snapshot.database_write_failed);
    assert(snapshot.stored_shader_count >= 2);

    // The file worker may accept a job before discovering an I/O failure.
    // A regular file at the intended stage directory forces that failure.
    std::filesystem::path blocked_stage = root / "shader-artifacts" /
        FormatTitleId(shader.scope.title_id) /
        ShaderHashBase32(ComputeShaderHash(
            1, Stage::Pixel, shader.recipe_format_version, recipe,
            sizeof(recipe))) / "vs";
    std::filesystem::create_directories(blocked_stage.parent_path());
    std::ofstream(blocked_stage) << "not a directory";
    assert(GetProvider().CopySnapshot(&snapshot));
    size_t artifacts_before_failure = snapshot.artifact_count;
    std::string late_hash(64, 'a');
    XemuShaderBrowserExternalArtifact successful_late = concurrent;
    successful_late.content_hash = late_hash.c_str();
    assert(xemu_shader_browser_publish_external_artifact(&successful_late));
    XemuShaderBrowserExternalArtifact failed_artifact = concurrent;
    failed_artifact.stage = XEMU_SHADER_BROWSER_STAGE_VERTEX;
    assert(xemu_shader_browser_publish_external_artifact(&failed_artifact));
    assert(!xemu_shader_browser_flush_database(error, sizeof(error)));
    assert(error[0]);
    assert(GetProvider().CopySnapshot(&snapshot));
    assert(snapshot.failed_artifact_count == 1);
    assert(!snapshot.last_artifact_error.empty());
    assert(snapshot.artifact_count >= artifacts_before_failure + 1);
    assert(xemu_shader_browser_database_configure(0, 0, 0,
                                                   error, sizeof(error)));
    assert(GetProvider().CopySnapshot(&snapshot));
    assert(snapshot.failed_artifact_count == 1);

    // Typed performance samples keep stage creation on its shader and
    // binding costs in one shared variant, even with multiple members.
    profiling.monitoring_level = XEMU_SHADER_BROWSER_MONITOR_DIAGNOSTIC;
    xemu_shader_browser_configure_profiling(&profiling);
    GetProvider().SetLiveCollectionEnabled(true);
    XemuShaderBrowserPerformanceSample sample{};
    sample.owner = XEMU_SHADER_BROWSER_PERF_STAGE;
    sample.metric = XEMU_SHADER_BROWSER_PERF_COMPILE_CPU;
    sample.backend = XEMU_SHADER_BROWSER_BACKEND_GL;
    sample.route = XEMU_SHADER_BROWSER_ROUTE_SPECIALIZED;
    assert(xemu_shader_browser_capture_performance_context(&sample.context));
    sample.duration_ns = 1000;
    sample.identity_count = 1;
    sample.identities[0].version = shader.identity_version;
    sample.identities[0].stage = shader.stage;
    std::memcpy(sample.identities[0].hash, shader.identity_hash,
                sizeof(shader.identity_hash));
    xemu_shader_browser_publish_performance_samples(&sample, 1);
    sample.owner = XEMU_SHADER_BROWSER_PERF_BINDING;
    sample.variant_kind = XEMU_SHADER_BROWSER_VARIANT_BINDING;
    sample.metric = XEMU_SHADER_BROWSER_PERF_LINK_OR_PIPELINE_CPU;
    sample.variant_id = 23;
    sample.duration_ns = 4000;
    sample.identity_count = 2;
    sample.identities[1] = sample.identities[0];
    sample.identities[1].stage = XEMU_SHADER_BROWSER_STAGE_VERTEX;
    sample.identities[1].hash[0] ^= 0x80;
    xemu_shader_browser_publish_performance_samples(&sample, 1);
    assert(GetProvider().CopySnapshot(&snapshot));
    auto pixel = std::find_if(snapshot.entries.begin(), snapshot.entries.end(),
                              [&](const Entry &entry) {
                                  return entry.key.stage == Stage::Pixel &&
                                         entry.key.hash.bytes[0] ==
                                             shader.identity_hash[0];
                              });
    assert(pixel != snapshot.entries.end());
    assert(pixel->compile_cpu.samples == 1);
    assert(pixel->compile_cpu.total_ns == 1000);
    assert(pixel->pipeline_cpu.samples == 0);
    assert(snapshot.stage_profiles.size() == 1);
    assert(snapshot.stage_profiles[0].key == pixel->key);
    assert(snapshot.stage_profiles[0]
               .timing[XEMU_SHADER_BROWSER_PERF_COMPILE_CPU]
               .total_ns == 1000);
    assert(snapshot.binding_variants.size() == 1);
    assert(snapshot.binding_variants[0].members.size() == 2);
    assert(snapshot.binding_variants[0]
               .timing[XEMU_SHADER_BROWSER_PERF_LINK_OR_PIPELINE_CPU]
               .total_ns == 4000);
    sample.route = XEMU_SHADER_BROWSER_ROUTE_UBER;
    sample.metric = XEMU_SHADER_BROWSER_PERF_DRAW_SUBMIT_CPU;
    sample.duration_ns = 2000;
    sample.represented_draws = 1;
    for (int i = 0; i < 1100; ++i) {
        xemu_shader_browser_publish_performance_samples(&sample, 1);
    }
    assert(GetProvider().CopySnapshot(&snapshot));
    assert(snapshot.binding_variants.size() == 2);
    assert(snapshot.binding_variants[1].route == Route::Uber);
    assert(snapshot.binding_variants[1]
               .timing[XEMU_SHADER_BROWSER_PERF_DRAW_SUBMIT_CPU]
               .samples == 1024);
    assert(snapshot.dropped_performance_samples == 76);
    assert(snapshot.pending_performance_samples == 0);

    // Concurrent producer threads share the fixed ring and drop on overflow.
    std::vector<std::thread> producers;
    for (int thread = 0; thread < 4; ++thread) {
        producers.emplace_back([sample] {
            for (int i = 0; i < 300; ++i) {
                xemu_shader_browser_publish_performance_samples(&sample, 1);
            }
        });
    }
    for (std::thread &producer : producers)
        producer.join();
    assert(GetProvider().CopySnapshot(&snapshot));
    assert(snapshot.binding_variants[1]
               .timing[XEMU_SHADER_BROWSER_PERF_DRAW_SUBMIT_CPU]
               .samples == 2048);
    assert(snapshot.dropped_performance_samples == 252);
    assert(snapshot.pending_performance_samples == 0);

    // Clear and toggle transitions invalidate pending issue-time contexts.
    assert(xemu_shader_browser_capture_performance_context(&sample.context));
    xemu_shader_browser_session_clear_live();
    xemu_shader_browser_publish_performance_samples(&sample, 1);
    assert(GetProvider().CopySnapshot(&snapshot));
    assert(snapshot.binding_variants.empty());

    assert(xemu_shader_browser_capture_performance_context(&sample.context));
    xemu_shader_browser_publish_performance_samples(&sample, 1);
    assert(GetProvider().CopySnapshot(&snapshot));
    assert(snapshot.binding_variants.size() == 1);
    const uint64_t accepted =
        snapshot.binding_variants[0]
            .timing[XEMU_SHADER_BROWSER_PERF_DRAW_SUBMIT_CPU]
            .samples;
    profiling.monitoring_level = XEMU_SHADER_BROWSER_MONITOR_OFF;
    xemu_shader_browser_configure_profiling(&profiling);
    profiling.monitoring_level = XEMU_SHADER_BROWSER_MONITOR_DIAGNOSTIC;
    xemu_shader_browser_configure_profiling(&profiling);
    xemu_shader_browser_publish_performance_samples(&sample, 1);
    assert(GetProvider().CopySnapshot(&snapshot));
    assert(snapshot.binding_variants[0]
               .timing[XEMU_SHADER_BROWSER_PERF_DRAW_SUBMIT_CPU]
               .samples == accepted);

    sample.metric = XEMU_SHADER_BROWSER_PERF_DRAW_GPU;
    sample.variant_kind = XEMU_SHADER_BROWSER_VARIANT_PIPELINE;
    assert(xemu_shader_browser_capture_performance_context(&sample.context));
    profiling.gpu_timing = 0;
    xemu_shader_browser_configure_profiling(&profiling);
    profiling.gpu_timing = 1;
    xemu_shader_browser_configure_profiling(&profiling);
    xemu_shader_browser_publish_performance_samples(&sample, 1);
    assert(GetProvider().CopySnapshot(&snapshot));
    assert(snapshot.binding_variants.size() == 1);
    assert(xemu_shader_browser_capture_performance_context(&sample.context));
    xemu_shader_browser_publish_performance_samples(&sample, 1);
    assert(GetProvider().CopySnapshot(&snapshot));
    assert(snapshot.binding_variants.size() == 2);
    assert(snapshot.binding_variants[1].variant_kind ==
           XEMU_SHADER_BROWSER_VARIANT_PIPELINE);
    sample.variant_id += 1;
    assert(xemu_shader_browser_capture_performance_context(&sample.context));
    xemu_shader_browser_publish_performance_samples(&sample, 1);
    assert(GetProvider().CopySnapshot(&snapshot));
    assert(snapshot.binding_variants.size() == 3);
    assert(snapshot.binding_variants[2].variant_kind ==
           XEMU_SHADER_BROWSER_VARIANT_PIPELINE);
    const uint64_t pipeline_samples =
        snapshot.binding_variants[2]
            .timing[XEMU_SHADER_BROWSER_PERF_DRAW_GPU]
            .samples;
    GetProvider().SetLiveCollectionEnabled(false);
    GetProvider().SetLiveCollectionEnabled(true);
    xemu_shader_browser_publish_performance_samples(&sample, 1);
    assert(GetProvider().CopySnapshot(&snapshot));
    assert(snapshot.binding_variants.size() == 3);
    assert(snapshot.binding_variants[2]
               .timing[XEMU_SHADER_BROWSER_PERF_DRAW_GPU]
               .samples == pipeline_samples);
    assert(xemu_shader_browser_capture_performance_context(&sample.context));
    xemu_shader_browser_publish_performance_samples(&sample, 1);
    assert(GetProvider().CopySnapshot(&snapshot));
    assert(snapshot.binding_variants[2]
               .timing[XEMU_SHADER_BROWSER_PERF_DRAW_GPU]
               .samples == pipeline_samples + 1);

    xemu_shader_browser_session_uninstall();
    test_shared_pipeline_members_keep_separate_costs(root);
    std::filesystem::remove_all(root, ec);
    std::cout << "shader browser session provider tests passed\n";
    return 0;
}

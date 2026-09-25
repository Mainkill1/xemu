#include "../../ui/xui/shader-browser-database.hh"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

using namespace xemu::shader_browser;

static ShaderRecord MakeRecord(uint32_t title, uint8_t seed)
{
    ShaderRecord record{};
    record.key.stage = Stage::Pixel;
    record.recipe_format_version = 1;
    record.recipe = {seed, static_cast<uint8_t>(seed + 1), 0x7f};
    record.key.hash = ComputeShaderHash(
        1, record.key.stage, record.recipe_format_version,
        record.recipe.data(), record.recipe.size());
    record.scope.title_id = title;
    record.scope.executable_fingerprint_version = 1;
    record.scope.executable_fingerprint[0] = seed;
    return record;
}

int main()
{
    std::filesystem::path root = std::filesystem::temp_directory_path() /
                                 "xemu-shader-db-test";
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(root);

    ShaderDatabase db;
    DatabaseConfig config{};
    config.base_path = root.string();
    config.enabled = false;
    config.record_performance_sessions = false;
    config.save_external_artifacts = false;

    std::string error;
    assert(db.Configure(config, &error));
    assert(!db.GetStats().enabled);
    assert(!std::filesystem::exists(root / "shader-browser.db"));

    config.enabled = true;
    assert(db.Configure(config, &error));
    DatabaseStats stats = db.GetStats();
    assert(stats.enabled && stats.open);
    assert(stats.schema_version == 1);
    assert(stats.database_path == (root / "shader-browser.db").string());
    assert(stats.artifact_root == (root / "shader-artifacts").string());

    ShaderRecord a = MakeRecord(0x4d530064, 1);
    ShaderRecord b = MakeRecord(0x4d530065, 2);
    ShaderRecord bad_hash = a;
    bad_hash.key.hash.bytes[0] ^= 0xff;
    assert(!db.UpsertShader(bad_hash, &error));
    assert(db.UpsertShader(a, &error));
    assert(db.UpsertShader(a, &error)); // dedupe
    assert(db.UpsertShader(b, &error));
    assert(db.Flush(&error));

    auto metadata = db.CopyMetadata();
    assert(metadata.size() == 2);
    assert(db.GetStats().shader_count == 2);
    assert(db.GetStats().title_count == 2);

    SessionDescriptor session{};
    session.session_id = "session-a";
    session.title_id = 0x4d530064;
    session.executable_fingerprint_version = 1;
    session.executable_fingerprint[0] = 1;
    session.renderer = "Vulkan";
    session.xemu_revision = "test-revision";
    session.host_os = "test-os";
    session.cpu_model = "test-cpu";
    session.gpu_name = "test-gpu";
    session.started_unix_ms = 1000;

    config.record_performance_sessions = true;
    assert(db.Configure(config, &error));
    assert(db.BeginPerformanceSession(session, &error));

    ShaderSessionStats perf{};
    perf.key = a.key;
    perf.draw_count = 40;
    perf.first_frame = 10;
    perf.last_frame = 20;
    perf.specialized_draws = 30;
    perf.uber_draws = 10;
    perf.compile_cpu.AddSample(1000000);
    perf.compile_cpu.AddSample(3000000);
    perf.pipeline_cpu.AddSample(500000);
    perf.gpu_execution.AddSample(80000);
    perf.gpu_execution.AddSample(120000);
    assert(db.UpsertPerformanceStats("session-a", perf, &error));
    assert(db.UpsertPerformanceStats("session-a", perf, &error)); // same row
    assert(db.EndPerformanceSession("session-a", 2000, true, &error));
    assert(db.Flush(&error));

    auto sessions = db.CopySessions(0x4d530064, 16);
    assert(sessions.size() == 1);
    auto rows = db.CopySessionStats("session-a");
    assert(rows.size() == 1);
    assert(rows[0].compile_cpu.samples == 2);
    assert(rows[0].compile_cpu.min_ns == 1000000);
    assert(rows[0].compile_cpu.max_ns == 3000000);
    assert(rows[0].compile_cpu.total_ns == 4000000);
    assert(rows[0].gpu_execution.samples == 2);
    assert(rows[0].draw_count == 40);
    assert(db.GetStats().session_count == 1);
    assert(db.GetStats().session_stat_count == 1);

    // A later run of the same game/shader is a separate sample set. Never
    // blend this into a lifetime average.
    SessionDescriptor session_b = session;
    session_b.session_id = "session-b";
    session_b.started_unix_ms = 3000;
    assert(db.BeginPerformanceSession(session_b, &error));
    ShaderSessionStats perf_b{};
    perf_b.key = a.key;
    perf_b.draw_count = 5;
    perf_b.compile_cpu.AddSample(10000000);
    assert(db.UpsertPerformanceStats("session-b", perf_b, &error));
    assert(db.EndPerformanceSession("session-b", 4000, true, &error));
    assert(db.Flush(&error));
    auto rows_b = db.CopySessionStats("session-b");
    assert(rows_b.size() == 1);
    assert(rows_b[0].compile_cpu.samples == 1);
    assert(rows_b[0].compile_cpu.total_ns == 10000000);
    rows = db.CopySessionStats("session-a");
    assert(rows[0].compile_cpu.total_ns == 4000000);
    assert(db.GetStats().session_count == 2);
    assert(db.GetStats().session_stat_count == 2);

    config.save_external_artifacts = true;
    assert(db.Configure(config, &error));

    ArtifactMetadata artifact{};
    artifact.key = a.key;
    artifact.title_id = 0x4d530064;
    artifact.backend = "vulkan";
    artifact.route = "specialized";
    artifact.kind = "spirv";
    artifact.generator_abi = 1;
    artifact.relative_path = "4D530064/test/ps/vulkan-specialized.spv";
    artifact.byte_size = 999999;
    assert(db.RegisterArtifact(artifact, &error));
    assert(db.RegisterArtifact(artifact, &error)); // same external path
    assert(db.Flush(&error));
    assert(db.GetStats().artifact_count == 1);
    // Artifact storage accounting is metadata-driven. GetStats() must not
    // recursively scan a potentially multi-gigabyte artifact directory.
    assert(db.GetStats().external_artifact_bytes == artifact.byte_size);

    // DB stores metadata only; the compiled artifact is not embedded.
    uint64_t db_bytes_before = db.GetStats().database_bytes;
    std::filesystem::create_directories(root / "shader-artifacts" /
                                        "4D530064" / "test" / "ps");
    std::ofstream(root / "shader-artifacts" / artifact.relative_path,
                  std::ios::binary) << std::string(1024 * 1024, 'x');
    assert(db.Flush(&error));
    uint64_t db_bytes_after = db.GetStats().database_bytes;
    assert(db_bytes_after < db_bytes_before + 256 * 1024);

    std::string check;
    assert(db.QuickCheck(&check, &error));
    assert(check == "ok");

    db.Close();

    ShaderDatabase reopened;
    assert(reopened.Configure(config, &error));
    assert(reopened.CopyMetadata().size() == 2);
    assert(reopened.CopySessions(0x4d530064, 16).size() == 2);
    assert(reopened.CopySessionStats("session-a").size() == 1);
    assert(reopened.GetStats().artifact_count == 1);
    assert(reopened.ClearPerformanceHistory(&error));
    assert(reopened.CopySessions(0x4d530064, 16).empty());
    assert(reopened.GetStats().session_count == 0);
    assert(reopened.GetStats().session_stat_count == 0);
    reopened.Close();

    std::filesystem::remove_all(root, ec);
    std::cout << "shader browser database tests passed\n";
    return 0;
}

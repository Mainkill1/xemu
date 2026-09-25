// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "shader-browser-model.hh"

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

struct sqlite3;

namespace xemu::shader_browser {

constexpr uint32_t kShaderDatabaseSchemaVersion = 1;

struct DatabaseConfig {
    std::string base_path;
    bool enabled = false;
    bool record_performance_sessions = false;
    bool save_external_artifacts = false;
};

struct ShaderRecord {
    ShaderKey key;
    uint32_t recipe_format_version = 0;
    std::vector<uint8_t> recipe;
    ShaderScope scope;
};

struct DatabaseShaderMetadata {
    ShaderKey key;
    uint32_t recipe_format_version = 0;
    size_t recipe_size = 0;
    std::vector<ShaderScope> scopes;
};

struct SessionDescriptor {
    std::string session_id;
    uint32_t title_id = 0;
    uint32_t executable_fingerprint_version = 0;
    std::array<uint8_t, kExecutableFingerprintBytes>
        executable_fingerprint{};
    uint64_t started_unix_ms = 0;
    std::string xemu_revision;
    std::string renderer;
    std::string host_os;
    std::string cpu_model;
    std::string gpu_name;
    uint32_t gpu_vendor_id = 0;
    uint32_t gpu_device_id = 0;
    std::string gpu_driver;
    uint32_t internal_resolution_scale = 1;
    std::string ubershader_mode;
    bool shader_cache_enabled = false;
    uint32_t telemetry_version = 1;
};

struct ShaderSessionStats {
    ShaderKey key;
    uint64_t draw_count = 0;
    uint64_t first_frame = 0;
    uint64_t last_frame = 0;
    uint64_t specialized_draws = 0;
    uint64_t uber_draws = 0;
    uint64_t replacement_draws = 0;
    uint64_t compile_failures = 0;
    DurationStats compile_cpu;
    DurationStats pipeline_cpu;
    DurationStats prepare_cpu;
    DurationStats foreground_stall_cpu;
    DurationStats gpu_execution;
};

struct SessionSummary {
    std::string session_id;
    uint32_t title_id = 0;
    uint64_t started_unix_ms = 0;
    uint64_t ended_unix_ms = 0;
    std::string xemu_revision;
    std::string renderer;
    std::string gpu_name;
    bool clean_shutdown = false;
};

struct ArtifactMetadata {
    ShaderKey key;
    uint32_t title_id = 0;
    std::string backend;
    std::string route;
    std::string kind;
    uint32_t generator_abi = 0;
    std::string xemu_revision;
    std::string relative_path;
    uint64_t byte_size = 0;
    std::string content_hash;
};

struct DatabaseStats {
    bool enabled = false;
    bool open = false;
    bool record_performance_sessions = false;
    bool save_external_artifacts = false;
    bool write_failed = false;
    uint32_t schema_version = 0;
    size_t shader_count = 0;
    size_t title_count = 0;
    size_t session_count = 0;
    size_t session_stat_count = 0;
    size_t artifact_count = 0;
    size_t pending_writes = 0;
    uint64_t database_bytes = 0;
    uint64_t wal_bytes = 0;
    uint64_t external_artifact_bytes = 0;
    std::string database_path;
    std::string artifact_root;
    std::string journal_mode;
    std::string status;
};

class ShaderDatabase
{
public:
    ShaderDatabase();
    ~ShaderDatabase();

    ShaderDatabase(const ShaderDatabase &) = delete;
    ShaderDatabase &operator=(const ShaderDatabase &) = delete;

    bool Configure(const DatabaseConfig &config, std::string *error);
    void Close();

    bool UpsertShader(const ShaderRecord &record, std::string *error);
    bool CopyRecord(const ShaderKey &key, ShaderRecord *record) const;
    std::vector<DatabaseShaderMetadata> CopyMetadata() const;

    bool BeginPerformanceSession(const SessionDescriptor &session,
                                 std::string *error);
    bool UpsertPerformanceStats(const std::string &session_id,
                                const ShaderSessionStats &stats,
                                std::string *error);
    bool EndPerformanceSession(const std::string &session_id,
                               uint64_t ended_unix_ms, bool clean_shutdown,
                               std::string *error);
    std::vector<SessionSummary> CopySessions(uint32_t title_id, size_t limit);
    std::vector<ShaderSessionStats> CopySessionStats(
        const std::string &session_id);

    bool RegisterArtifact(const ArtifactMetadata &artifact,
                          std::string *error);
    bool ReconcileExternalArtifacts(size_t *removed, std::string *error);

    bool Flush(std::string *error);
    bool QuickCheck(std::string *result, std::string *error);
    bool Optimize(std::string *error);
    bool ClearPerformanceHistory(std::string *error);

    DatabaseStats GetStats() const;
    bool Enabled() const;
    bool RecordPerformanceSessions() const;
    bool SaveExternalArtifacts() const;

private:
    struct StoredRecord {
        ShaderRecord record;
        std::vector<ShaderScope> scopes;
    };

    using WriteJob = std::function<bool(sqlite3 *, std::string *)>;

    bool OpenLocked(const DatabaseConfig &config, std::string *error);
    bool InitializeSchema(sqlite3 *db, std::string *error);
    bool LoadCache(sqlite3 *db, std::string *error);
    bool ValidateShaderRecord(const ShaderRecord &record,
                              std::string *error) const;
    void Enqueue(WriteJob job);
    void WriterMain();
    void SetWriteError(const std::string &message);
    bool OpenAuxConnection(sqlite3 **db, bool read_only,
                           std::string *error) const;

    mutable std::shared_mutex lifecycle_mutex;
    DatabaseConfig config;
    std::string database_path;
    std::string artifact_root;
    sqlite3 *writer_db = nullptr;
    bool open = false;

    mutable std::mutex cache_mutex;
    std::unordered_map<ShaderKey, StoredRecord, ShaderKeyHash> records;
    std::unordered_set<uint32_t> known_titles;
    std::unordered_set<std::string> known_sessions;
    std::unordered_set<std::string> known_session_stats;
    std::unordered_map<std::string, uint64_t> known_artifact_paths;

    mutable std::mutex queue_mutex;
    std::condition_variable queue_cv;
    std::condition_variable flush_cv;
    std::deque<WriteJob> write_queue;
    std::thread writer_thread;
    bool writer_stop = false;
    bool write_in_flight = false;
    bool write_failed = false;
    std::string write_error;

    std::atomic<size_t> session_count{0};
    std::atomic<size_t> session_stat_count{0};
    std::atomic<size_t> artifact_count{0};
    std::atomic<uint64_t> artifact_bytes{0};
};

} // namespace xemu::shader_browser

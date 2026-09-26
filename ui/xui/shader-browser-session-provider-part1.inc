// SPDX-License-Identifier: GPL-2.0-or-later
#include "shader-browser-session-provider.hh"

#include "shader-browser-artifact-store.hh"
#include "shader-browser-database.hh"
#include "shader-browser-provider.hh"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <deque>
#include <mutex>
#include <shared_mutex>
#include <unordered_map>
#include <utility>
#include <vector>

namespace xemu::shader_browser {
namespace {

constexpr size_t kMaxExternalArtifactBytes = 256U * 1024U * 1024U;

Stage ConvertStage(uint32_t stage)
{
    switch (stage) {
    case XEMU_SHADER_BROWSER_STAGE_VERTEX: return Stage::Vertex;
    case XEMU_SHADER_BROWSER_STAGE_PIXEL: return Stage::Pixel;
    case XEMU_SHADER_BROWSER_STAGE_GEOMETRY: return Stage::Geometry;
    case XEMU_SHADER_BROWSER_STAGE_FIXED_FUNCTION: return Stage::FixedFunction;
    default: return Stage::Unknown;
    }
}

Status ConvertStatus(uint32_t status)
{
    switch (status) {
    case XEMU_SHADER_BROWSER_STATUS_NORMAL: return Status::Normal;
    case XEMU_SHADER_BROWSER_STATUS_HOT: return Status::Hot;
    case XEMU_SHADER_BROWSER_STATUS_COMPILE_SPIKE: return Status::CompileSpike;
    case XEMU_SHADER_BROWSER_STATUS_DISABLED: return Status::Disabled;
    case XEMU_SHADER_BROWSER_STATUS_CUSTOM: return Status::Custom;
    default: return Status::NotObserved;
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

ShaderHash ConvertHash(uint32_t version, const uint8_t *bytes)
{
    ShaderHash hash{};
    hash.version = version;
    std::memcpy(hash.bytes.data(), bytes, hash.bytes.size());
    return hash;
}

ShaderScope ConvertScope(const XemuShaderBrowserScope &wire)
{
    ShaderScope scope{};
    scope.title_id = wire.title_id;
    scope.executable_fingerprint_version =
        wire.executable_fingerprint_version;
    std::memcpy(scope.executable_fingerprint.data(),
                wire.executable_fingerprint,
                scope.executable_fingerprint.size());
    return scope;
}

DurationStats ConvertDuration(const XemuShaderBrowserDurationStats &wire)
{
    DurationStats stats{};
    if (!wire.sample_count) return stats;
    stats.samples = wire.sample_count;
    stats.total_ns = wire.total_ns;
    stats.min_ns = wire.min_ns;
    stats.max_ns = wire.max_ns;
    return stats;
}

bool IsZeroScope(const ShaderScope &scope)
{
    return scope.title_id == 0 &&
           scope.executable_fingerprint_version == 0 &&
           std::all_of(scope.executable_fingerprint.begin(),
                       scope.executable_fingerprint.end(),
                       [](uint8_t byte) { return byte == 0; });
}

void AddScope(std::vector<ShaderScope> *scopes, const ShaderScope &scope)
{
    if (IsZeroScope(scope)) return;
    if (std::find(scopes->begin(), scopes->end(), scope) == scopes->end()) {
        scopes->push_back(scope);
        std::sort(scopes->begin(), scopes->end());
    }
}

struct LiveSessionEntry {
    Entry entry;
    uint64_t last_event_sequence = 0;
};

struct TransientRecord {
    ShaderRecord record;
    std::vector<ShaderScope> scopes;
};

enum class EventKind : uint8_t { Observation, Frame };

struct QueuedEvent {
    EventKind kind = EventKind::Observation;
    uint64_t epoch = 0;
    uint64_t sequence = 0;
    XemuShaderBrowserObservation observation{};
    uint64_t frame = 0;
};

struct ArtifactJob {
    ArtifactWrite write;
};

class SessionProvider final : public Provider {
public:
    bool Install(const char *new_base_path)
    {
        Uninstall();
        if (!new_base_path || !new_base_path[0]) return false;
        std::unique_lock<std::shared_mutex> lock(lifecycle_mutex);
        base_path = new_base_path;
        installed = true;
        scope_generation.fetch_add(1, std::memory_order_release);
        live_collection_enabled.store(false, std::memory_order_release);
        live_epoch.store(1, std::memory_order_release);
        live_consumer_epoch = 1;
        snapshot_sequence = 0;
        maintenance_stop = false;
        artifact_stop = false;
        artifact_in_flight = false;
        maintenance_thread = std::thread(&SessionProvider::MaintenanceMain, this);
        artifact_thread = std::thread(&SessionProvider::ArtifactMain, this);
        return true;
    }

    void Uninstall()
    {
        {
            std::unique_lock<std::shared_mutex> lock(lifecycle_mutex);
            if (!installed && !maintenance_thread.joinable() &&
                !artifact_thread.joinable()) {
                return;
            }
            live_collection_enabled.store(false, std::memory_order_release);
            installed = false;
            std::lock_guard<std::mutex> scope_lock(scope_mutex);
            if (current_scope.title_id ||
                current_scope.executable_fingerprint_version ||
                std::any_of(std::begin(current_scope.executable_fingerprint),
                            std::end(current_scope.executable_fingerprint),
                            [](uint8_t byte) { return byte != 0; })) {
                current_scope = {};
                scope_generation.fetch_add(1, std::memory_order_release);
            }
        }

        PersistPerformanceSnapshot();
        {
            std::lock_guard<std::mutex> lock(maintenance_mutex);
            maintenance_stop = true;
        }
        maintenance_cv.notify_all();
        if (maintenance_thread.joinable()) maintenance_thread.join();

        {
            std::lock_guard<std::mutex> lock(artifact_mutex);
            artifact_stop = true;
        }
        artifact_cv.notify_all();
        if (artifact_thread.joinable()) artifact_thread.join();

        database.Close();
        {
            std::lock_guard<std::mutex> lock(live_queue_mutex);
            live_events.clear();
            next_event_sequence = 0;
        }
        live_entries.clear();
        {
            std::lock_guard<std::mutex> lock(performance_mutex);
            performance_entries.clear();
            performance_session_id.clear();
        }
        current_frame = 0;
    }

    void SetCurrentScope(const XemuShaderBrowserScope *scope)
    {
        std::shared_lock<std::shared_mutex> lifecycle(lifecycle_mutex);
        if (!installed) return;
        XemuShaderBrowserScope next = scope ? *scope :
                                           XemuShaderBrowserScope{};
        std::lock_guard<std::mutex> lock(scope_mutex);
        if (current_scope.title_id != next.title_id ||
            current_scope.executable_fingerprint_version !=
                next.executable_fingerprint_version ||
            std::memcmp(current_scope.executable_fingerprint,
                        next.executable_fingerprint,
                        sizeof(next.executable_fingerprint)) != 0) {
            current_scope = next;
            scope_generation.fetch_add(1, std::memory_order_release);
        }
    }

    uint64_t CopyCurrentScope(XemuShaderBrowserScope *scope)
    {
        if (!scope) return 0;
        std::lock_guard<std::mutex> lock(scope_mutex);
        *scope = current_scope;
        return scope_generation.load(std::memory_order_acquire);
    }

    uint64_t ScopeGeneration() const
    {
        return scope_generation.load(std::memory_order_acquire);
    }

    bool ConfigurePersistence(bool database_enabled,
                              bool record_performance_sessions,
                              bool save_external_artifacts,
                              std::string *error) override
    {
        std::shared_lock<std::shared_mutex> lifecycle(lifecycle_mutex);
        if (!installed) {
            if (error) *error = "Shader Browser provider is not installed";
            return false;
        }
        // Treat a persistence toggle as a durability boundary. This prevents
        // a user from disabling persistence while an external artifact has
        // been written but its SQLite metadata is still queued.
        if (database.Enabled()) {
            FlushArtifactQueue();
            std::string ignored;
            database.Flush(&ignored);
        }

        DatabaseConfig db_config{};
        db_config.base_path = base_path;
        db_config.enabled = database_enabled;
        db_config.record_performance_sessions =
            database_enabled && record_performance_sessions;
        db_config.save_external_artifacts =
            database_enabled && save_external_artifacts;
        if (!database.Configure(db_config, error)) return false;

        ArtifactStoreConfig artifact_config{};
        artifact_config.base_path = base_path;
        artifact_config.enabled = db_config.save_external_artifacts;
        artifact_store.Configure(artifact_config);

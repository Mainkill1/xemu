// SPDX-License-Identifier: GPL-2.0-or-later
#include "shader-browser-session-provider.hh"

#include "shader-browser-artifact-store.hh"
#include "shader-browser-database.hh"
#include "shader-browser-provider.hh"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstring>
#include <deque>
#include <map>
#include <mutex>
#include <shared_mutex>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

namespace xemu::shader_browser {
namespace {

constexpr size_t kMaxExternalArtifactBytes = 32U * 1024U * 1024U;
constexpr size_t kMaxPendingArtifactBytes = 64U * 1024U * 1024U;
constexpr size_t kMaxPendingArtifactJobs = 128;

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

struct PerformanceEntry {
    ShaderSessionStats stats;
    bool needs_enqueue = false;
};

struct QueuedPerformanceSample {
    XemuShaderBrowserPerformanceSample sample{};
    uint64_t live_epoch = 0;
    uint64_t performance_epoch = 0;
    bool live_requested = false;
};

// Fixed MPSC ring: renderer and compiler threads only copy into a reserved
// slot. A full ring drops the new sample without waiting for the UI consumer.
class PerformanceSampleQueue {
    static constexpr size_t Capacity = 1024;
    struct Slot {
        std::atomic<size_t> sequence;
        QueuedPerformanceSample value;
    };
    std::array<Slot, Capacity> slots;
    std::atomic<size_t> head{0};
    std::atomic<size_t> tail{0};
public:
    PerformanceSampleQueue()
    {
        for (size_t i = 0; i < Capacity; ++i) {
            slots[i].sequence.store(i, std::memory_order_relaxed);
        }
    }

    bool Push(const QueuedPerformanceSample &value)
    {
        size_t position = head.load(std::memory_order_relaxed);
        for (;;) {
            Slot &slot = slots[position & (Capacity - 1)];
            size_t sequence = slot.sequence.load(std::memory_order_acquire);
            intptr_t difference = static_cast<intptr_t>(sequence) -
                                  static_cast<intptr_t>(position);
            if (difference == 0) {
                if (head.compare_exchange_weak(position, position + 1,
                                               std::memory_order_relaxed)) {
                    slot.value = value;
                    slot.sequence.store(position + 1,
                                        std::memory_order_release);
                    return true;
                }
            } else if (difference < 0) {
                return false;
            } else {
                position = head.load(std::memory_order_relaxed);
            }
        }
    }

    bool Pop(QueuedPerformanceSample *value)
    {
        size_t position = tail.load(std::memory_order_relaxed);
        Slot &slot = slots[position & (Capacity - 1)];
        size_t sequence = slot.sequence.load(std::memory_order_acquire);
        if (sequence != position + 1) return false;
        *value = slot.value;
        slot.sequence.store(position + Capacity, std::memory_order_release);
        tail.store(position + 1, std::memory_order_relaxed);
        return true;
    }

    size_t Pending() const
    {
        return head.load(std::memory_order_acquire) -
               tail.load(std::memory_order_acquire);
    }
};

using VariantKey = std::tuple<uint32_t, uint32_t, uint64_t, uint32_t>;
using StageProfileKey = std::tuple<ShaderKey, uint32_t, uint32_t, uint32_t>;
using PerformanceKey = std::tuple<uint32_t, uint32_t, uint32_t, uint64_t,
                                  uint32_t, uint32_t,
                                  std::vector<ShaderKey>>;

struct PerformanceAggregateEntry {
    PerformanceAggregate aggregate;
    bool needs_enqueue = false;
};

class SessionProvider final : public Provider {
public:
    void RecordDroppedSamples(uint64_t count)
    {
        dropped_performance_samples.fetch_add(count,
                                               std::memory_order_relaxed);
    }

    void ReportGpuState(uint32_t backend, bool supported, uint32_t pending)
    {
        if (backend == XEMU_SHADER_BROWSER_BACKEND_GL) {
            gl_gpu_supported.store(supported, std::memory_order_relaxed);
            gl_gpu_pending.store(pending, std::memory_order_relaxed);
        } else if (backend == XEMU_SHADER_BROWSER_BACKEND_VK) {
            vk_gpu_supported.store(supported, std::memory_order_relaxed);
            vk_gpu_pending.store(pending, std::memory_order_relaxed);
        }
    }

    void PublishPerformanceSamples(
        const XemuShaderBrowserPerformanceSample *samples, size_t count)
    {
        if (!samples || !count) return;
        const bool live = live_collection_enabled.load(
            std::memory_order_relaxed);
        if (!live && !performance_active.load(
                         std::memory_order_relaxed)) return;
        const uint64_t word = profiling_word.load(std::memory_order_relaxed);
        if ((word & 3U) != XEMU_SHADER_BROWSER_MONITOR_DIAGNOSTIC) return;
        const uint64_t epoch = live_epoch.load(std::memory_order_acquire);
        const uint64_t record_epoch =
            performance_epoch.load(std::memory_order_acquire);
        for (size_t i = 0; i < count; ++i) {
            const auto &sample = samples[i];
            const bool gpu = sample.metric == XEMU_SHADER_BROWSER_PERF_DRAW_GPU;
            if (!(word & (gpu ? 8U : 4U)) || !sample.duration_ns ||
                !sample.scope_generation || sample.identity_count == 0 ||
                sample.identity_count > 3 ||
                (sample.owner != XEMU_SHADER_BROWSER_PERF_STAGE &&
                 sample.owner != XEMU_SHADER_BROWSER_PERF_BINDING) ||
                (sample.owner == XEMU_SHADER_BROWSER_PERF_STAGE &&
                 sample.identity_count != 1) ||
                sample.metric < XEMU_SHADER_BROWSER_PERF_SOURCE_CPU ||
                sample.metric > XEMU_SHADER_BROWSER_PERF_QUEUE_DELAY_CPU ||
                (sample.owner == XEMU_SHADER_BROWSER_PERF_STAGE &&
                 sample.metric > XEMU_SHADER_BROWSER_PERF_MODULE_CPU) ||
                (sample.owner == XEMU_SHADER_BROWSER_PERF_BINDING &&
                 sample.metric < XEMU_SHADER_BROWSER_PERF_LINK_OR_PIPELINE_CPU)) {
                continue;
            }
            if (!sample_queue.Push({sample, epoch, record_epoch, live})) {
                dropped_performance_samples.fetch_add(
                    1, std::memory_order_relaxed);
            }
        }
    }

    void ConfigureProfiling(const XemuShaderBrowserProfilingConfig *config)
    {
        if (!config) return;
        const uint32_t level = config->monitoring_level <=
                                       XEMU_SHADER_BROWSER_MONITOR_DIAGNOSTIC ?
                                   config->monitoring_level :
                                   static_cast<uint32_t>(
                                       XEMU_SHADER_BROWSER_MONITOR_OFF);
        const uint32_t interval = std::clamp(
            config->draw_sample_interval, 1U, 4096U);
        const uint32_t maximum = std::clamp(
            config->max_gpu_samples_per_frame, 1U, 64U);
        const uint64_t next = level |
                              (uint64_t)(config->cpu_timing != 0) << 2 |
                              (uint64_t)(config->gpu_timing != 0) << 3 |
                              (uint64_t)interval << 4 |
                              (uint64_t)maximum << 17;
        const uint64_t previous = profiling_word.exchange(
            next, std::memory_order_acq_rel);
        if ((previous & 3U) != level) {
            scope_generation.fetch_add(1, std::memory_order_release);
        }
    }

    void CopyProfilingConfig(XemuShaderBrowserProfilingConfig *config) const
    {
        if (!config) return;
        const uint64_t word = profiling_word.load(std::memory_order_acquire);
        config->monitoring_level = word & 3U;
        config->cpu_timing = (word >> 2) & 1U;
        config->gpu_timing = (word >> 3) & 1U;
        config->draw_sample_interval = (word >> 4) & 0x1fffU;
        config->max_gpu_samples_per_frame = (word >> 17) & 0x7fU;
    }

    bool MonitoringEnabled() const
    {
        return (profiling_word.load(std::memory_order_relaxed) & 3U) != 0;
    }

    bool CpuProfilingEnabled() const
    {
        const uint64_t word = profiling_word.load(std::memory_order_relaxed);
        return (word & 3U) == XEMU_SHADER_BROWSER_MONITOR_DIAGNOSTIC &&
               (word & (1U << 2));
    }

    bool GpuProfilingEnabled() const
    {
        const uint64_t word = profiling_word.load(std::memory_order_relaxed);
        return (word & 3U) == XEMU_SHADER_BROWSER_MONITOR_DIAGNOSTIC &&
               (word & (1U << 3));
    }

    bool Install(const char *new_base_path)
    {
        Uninstall();
        if (!new_base_path || !new_base_path[0]) return false;
        std::unique_lock<std::shared_mutex> lock(lifecycle_mutex);
        base_path = new_base_path;
        installed = true;
        scope_generation.fetch_add(1, std::memory_order_release);
        live_collection_enabled.store(false, std::memory_order_release);
        performance_active.store(false, std::memory_order_release);
        live_epoch.store(1, std::memory_order_release);
        performance_epoch.fetch_add(1, std::memory_order_release);
        live_consumer_epoch = 1;
        snapshot_sequence = 0;
        maintenance_stop = false;
        artifact_stop = false;
        artifact_in_flight = false;
        artifact_pending_bytes = 0;
        artifact_pending_jobs = 0;
        rejected_artifacts.store(0, std::memory_order_release);
        dropped_performance_samples.store(0, std::memory_order_release);
        gl_gpu_pending.store(0, std::memory_order_release);
        vk_gpu_pending.store(0, std::memory_order_release);
        gl_gpu_supported.store(false, std::memory_order_release);
        vk_gpu_supported.store(false, std::memory_order_release);
        failed_artifact_jobs = 0;
        last_artifact_error.clear();
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
        }

        PersistPerformanceSnapshot();
        performance_active.store(false, std::memory_order_release);
        {
            std::unique_lock<std::shared_mutex> lifecycle(lifecycle_mutex);
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
        live_binding_variants.clear();
        live_stage_profiles.clear();
        snapshot_events.clear();
        QueuedPerformanceSample discarded;
        while (sample_queue.Pop(&discarded)) {}
        {
            std::lock_guard<std::mutex> lock(performance_mutex);
            performance_entries.clear();
            performance_aggregates.clear();
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
        std::unique_lock<std::shared_mutex> lifecycle(lifecycle_mutex);
        if (!installed) {
            if (error) *error = "Shader Browser provider is not installed";
            return false;
        }
        // Treat a persistence toggle as a durability boundary. This prevents
        // a user from disabling persistence while an external artifact has
        // been written but its SQLite metadata is still queued.
        if (database.Enabled()) {
            std::string ignored_artifact_error;
            FlushArtifactQueue(&ignored_artifact_error);
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

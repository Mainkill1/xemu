// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define XEMU_SHADER_BROWSER_HASH_BYTES 12
#define XEMU_SHADER_BROWSER_EXECUTABLE_FINGERPRINT_BYTES 32

typedef enum XemuShaderBrowserMonitoringLevel {
    XEMU_SHADER_BROWSER_MONITOR_OFF = 0,
    XEMU_SHADER_BROWSER_MONITOR_BASIC = 1,
    XEMU_SHADER_BROWSER_MONITOR_DIAGNOSTIC = 2,
} XemuShaderBrowserMonitoringLevel;

typedef struct XemuShaderBrowserProfilingConfig {
    uint32_t monitoring_level;
    int cpu_timing;
    int gpu_timing;
    uint32_t draw_sample_interval;
    uint32_t max_gpu_samples_per_frame;
} XemuShaderBrowserProfilingConfig;

typedef enum XemuShaderBrowserStage {
    XEMU_SHADER_BROWSER_STAGE_VERTEX = 1,
    XEMU_SHADER_BROWSER_STAGE_PIXEL = 2,
    XEMU_SHADER_BROWSER_STAGE_GEOMETRY = 3,
    XEMU_SHADER_BROWSER_STAGE_FIXED_FUNCTION = 4,
    XEMU_SHADER_BROWSER_STAGE_UNKNOWN = 5,
} XemuShaderBrowserStage;

typedef enum XemuShaderBrowserStatus {
    XEMU_SHADER_BROWSER_STATUS_NOT_OBSERVED = 0,
    XEMU_SHADER_BROWSER_STATUS_NORMAL = 1,
    XEMU_SHADER_BROWSER_STATUS_HOT = 2,
    XEMU_SHADER_BROWSER_STATUS_COMPILE_SPIKE = 3,
    XEMU_SHADER_BROWSER_STATUS_DISABLED = 4,
    XEMU_SHADER_BROWSER_STATUS_CUSTOM = 5,
} XemuShaderBrowserStatus;

typedef enum XemuShaderBrowserRoute {
    XEMU_SHADER_BROWSER_ROUTE_UNKNOWN = 0,
    XEMU_SHADER_BROWSER_ROUTE_SPECIALIZED = 1,
    XEMU_SHADER_BROWSER_ROUTE_UBER = 2,
    XEMU_SHADER_BROWSER_ROUTE_FIXED_FUNCTION = 3,
    XEMU_SHADER_BROWSER_ROUTE_REPLACEMENT = 4,
    XEMU_SHADER_BROWSER_ROUTE_DISABLED = 5,
} XemuShaderBrowserRoute;

typedef enum XemuShaderBrowserReadiness {
    XEMU_SHADER_BROWSER_READINESS_UNKNOWN = 0,
    XEMU_SHADER_BROWSER_READINESS_READY = 1,
    XEMU_SHADER_BROWSER_READINESS_PENDING = 2,
    XEMU_SHADER_BROWSER_READINESS_FAILED = 3,
} XemuShaderBrowserReadiness;

typedef enum XemuShaderBrowserObservationFlags {
    XEMU_SHADER_BROWSER_OBSERVED = 1U << 0,
} XemuShaderBrowserObservationFlags;

typedef struct XemuShaderBrowserScope {
    uint32_t title_id;
    uint32_t executable_fingerprint_version;
    uint8_t executable_fingerprint[
        XEMU_SHADER_BROWSER_EXECUTABLE_FINGERPRINT_BYTES];
} XemuShaderBrowserScope;

typedef struct XemuShaderBrowserDurationStats {
    uint64_t sample_count;
    uint64_t total_ns;
    uint64_t min_ns;
    uint64_t max_ns;
} XemuShaderBrowserDurationStats;

// identity_hash is the 96-bit portable ShaderHash. Use
// xemu_shader_browser_compute_shader_hash() so every renderer frames and hashes
// canonical recipes identically. title_id is intentionally NOT hashed into
// ShaderHash. Normal game-scoped identity is TitleID + ShaderHash; explicit
// cross-title research may query ShaderHash without the title scope.
typedef struct XemuShaderBrowserShaderRecord {
    uint32_t identity_version;
    uint8_t identity_hash[XEMU_SHADER_BROWSER_HASH_BYTES];
    uint32_t stage;
    uint32_t recipe_format_version;
    const uint8_t *recipe_data;
    size_t recipe_size;
    XemuShaderBrowserScope scope;
} XemuShaderBrowserShaderRecord;

// Renderers publish bounded-cadence aggregate observations. When performance
// recording is enabled this remains active even with the XUI window closed.
typedef struct XemuShaderBrowserObservation {
    uint32_t identity_version;
    uint8_t identity_hash[XEMU_SHADER_BROWSER_HASH_BYTES];
    uint32_t stage;
    uint32_t status;
    uint32_t route;
    uint32_t readiness;
    uint32_t flags;
    uint32_t pipeline_variant_count;
    uint64_t first_frame;
    uint64_t last_frame;
    uint64_t draw_count_delta;
    uint64_t specialized_draw_delta;
    uint64_t uber_draw_delta;
    uint64_t replacement_draw_delta;
    uint64_t compile_failure_delta;
    XemuShaderBrowserDurationStats compile_cpu;
    XemuShaderBrowserDurationStats pipeline_cpu;
    XemuShaderBrowserDurationStats prepare_cpu;
    XemuShaderBrowserDurationStats foreground_stall_cpu;
    XemuShaderBrowserDurationStats gpu_execution;
} XemuShaderBrowserObservation;

typedef enum XemuShaderBrowserPerfOwner {
    XEMU_SHADER_BROWSER_PERF_STAGE = 1,
    XEMU_SHADER_BROWSER_PERF_BINDING = 2,
} XemuShaderBrowserPerfOwner;

typedef enum XemuShaderBrowserPerfMetric {
    XEMU_SHADER_BROWSER_PERF_SOURCE_CPU = 1,
    XEMU_SHADER_BROWSER_PERF_COMPILE_CPU = 2,
    XEMU_SHADER_BROWSER_PERF_MODULE_CPU = 3,
    XEMU_SHADER_BROWSER_PERF_LINK_OR_PIPELINE_CPU = 4,
    XEMU_SHADER_BROWSER_PERF_FOREGROUND_STALL_CPU = 5,
    XEMU_SHADER_BROWSER_PERF_DRAW_SUBMIT_CPU = 6,
    XEMU_SHADER_BROWSER_PERF_DRAW_GPU = 7,
    XEMU_SHADER_BROWSER_PERF_BINDING_PREPARE_CPU = 8,
} XemuShaderBrowserPerfMetric;

typedef enum XemuShaderBrowserPerfBackend {
    XEMU_SHADER_BROWSER_BACKEND_GL = 1,
    XEMU_SHADER_BROWSER_BACKEND_VK = 2,
} XemuShaderBrowserPerfBackend;

typedef enum XemuShaderBrowserPerfFlags {
    XEMU_SHADER_BROWSER_SAMPLE_FOREGROUND = 1U << 0,
    XEMU_SHADER_BROWSER_SAMPLE_BACKGROUND = 1U << 1,
    XEMU_SHADER_BROWSER_SAMPLE_CACHED = 1U << 2,
    XEMU_SHADER_BROWSER_SAMPLE_SAMPLED = 1U << 3,
} XemuShaderBrowserPerfFlags;

typedef struct XemuShaderBrowserPerfIdentity {
    uint32_t version;
    uint8_t hash[XEMU_SHADER_BROWSER_HASH_BYTES];
    uint32_t stage;
} XemuShaderBrowserPerfIdentity;

typedef struct XemuShaderBrowserPerformanceSample {
    uint32_t owner;
    uint32_t metric;
    uint32_t backend;
    uint32_t route;
    uint64_t variant_id;
    uint64_t scope_generation;
    uint64_t frame;
    uint64_t duration_ns;
    uint64_t represented_draws;
    uint32_t identity_count;
    XemuShaderBrowserPerfIdentity identities[3];
    uint32_t flags;
} XemuShaderBrowserPerformanceSample;

typedef struct XemuShaderBrowserPerformanceSession {
    const char *session_id;
    uint32_t title_id;
    uint32_t executable_fingerprint_version;
    uint8_t executable_fingerprint[
        XEMU_SHADER_BROWSER_EXECUTABLE_FINGERPRINT_BYTES];
    uint64_t started_unix_ms;
    const char *xemu_revision;
    const char *renderer;
    const char *host_os;
    const char *cpu_model;
    const char *gpu_name;
    uint32_t gpu_vendor_id;
    uint32_t gpu_device_id;
    const char *gpu_driver;
    uint32_t internal_resolution_scale;
    const char *ubershader_mode;
    int shader_cache_enabled;
    uint32_t telemetry_version;
} XemuShaderBrowserPerformanceSession;

typedef struct XemuShaderBrowserExternalArtifact {
    uint32_t identity_version;
    uint8_t identity_hash[XEMU_SHADER_BROWSER_HASH_BYTES];
    uint32_t stage;
    uint32_t title_id;
    const char *backend;
    const char *route;
    const char *kind;
    const char *extension;
    uint32_t generator_abi;
    const char *xemu_revision;
    const char *content_hash;
    const uint8_t *data;
    size_t size;
} XemuShaderBrowserExternalArtifact;

// Installs the process-lifetime in-memory provider. No database is created
// here. Persistence is enabled separately from the user configuration.
int xemu_shader_browser_session_install(const char *base_path);
void xemu_shader_browser_session_uninstall(void);
void xemu_shader_browser_configure_profiling(
    const XemuShaderBrowserProfilingConfig *config);
void xemu_shader_browser_copy_profiling_config(
    XemuShaderBrowserProfilingConfig *config);
int xemu_shader_browser_monitoring_enabled(void);
int xemu_shader_browser_cpu_profiling_enabled(void);
int xemu_shader_browser_gpu_profiling_enabled(void);

// The UI publishes the active XBE scope when it changes. Renderer discovery
// copies it at binding creation, outside the ordinary draw path. A changed
// generation means a cached binding may need a new title association.
void xemu_shader_browser_set_current_scope(
    const XemuShaderBrowserScope *scope);
uint64_t xemu_shader_browser_copy_current_scope(
    XemuShaderBrowserScope *scope);
uint64_t xemu_shader_browser_scope_generation(void);

int xemu_shader_browser_compute_shader_hash(
    uint32_t identity_version, uint32_t stage,
    uint32_t recipe_format_version, const uint8_t *recipe_data,
    size_t recipe_size, uint8_t out_hash[XEMU_SHADER_BROWSER_HASH_BYTES]);

int xemu_shader_browser_database_configure(int enabled,
                                           int record_performance_sessions,
                                           int save_external_artifacts,
                                           char *error,
                                           size_t error_size);

// Trusted in-process renderer discovery enters the in-memory catalog. When
// the DB setting is enabled, the same record is asynchronously persisted.
int xemu_shader_browser_publish_shader(
    const XemuShaderBrowserShaderRecord *record);

// True when either live XUI collection OR opt-in performance recording needs
// aggregate observations. Callers should not publish observations otherwise.
int xemu_shader_browser_session_collection_enabled(void);
void xemu_shader_browser_publish_observations(
    const XemuShaderBrowserObservation *observations, size_t count);
void xemu_shader_browser_publish_performance_samples(
    const XemuShaderBrowserPerformanceSample *samples, size_t count);
void xemu_shader_browser_publish_frame(uint64_t frame);

void xemu_shader_browser_session_clear_live(void);

int xemu_shader_browser_performance_session_begin(
    const XemuShaderBrowserPerformanceSession *session,
    char *error, size_t error_size);
int xemu_shader_browser_performance_session_end(
    const char *session_id, uint64_t ended_unix_ms, int clean_shutdown,
    char *error, size_t error_size);

// Generated/compiled host artifacts are never stored as SQLite BLOBs. When
// enabled they are copied to <config>/shader-artifacts/ on a background worker
// and only relative-path metadata is registered in shader-browser.db.
int xemu_shader_browser_publish_external_artifact(
    const XemuShaderBrowserExternalArtifact *artifact);
int xemu_shader_browser_external_artifacts_enabled(void);

int xemu_shader_browser_flush_database(char *error, size_t error_size);

#ifdef __cplusplus
}
#endif

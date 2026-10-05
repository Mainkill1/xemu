/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "qemu/osdep.h"
#include <glib/gstdio.h>
#include "qobject/qdict.h"
#include "qobject/qjson.h"
#include "qobject/qlist.h"
#include "qobject/qnum.h"
#include "ui/xemu-shortcut-evidence.h"
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif

typedef struct OwnedSource {
    char *id;
    XemuShortcutCounterDescriptor descriptors[XEMU_SHORTCUT_MAX_COUNTERS];
    size_t count;
    void (*copy)(void *, XemuShortcutCounterSnapshot *);
    void *opaque;
    XemuShortcutCounterSnapshot final;
    bool retired;
} OwnedSource;

struct XemuShortcutEvidenceSnapshot {
    QDict *document;
};

/* Low-frequency registry operations protect callbacks until they return.
 * Owners release their published-copy lock before entering this registry. */
static GMutex mutex;
static GCond shutdown_done;
static int enabled;
static bool initialized, closing, finalized;
static char *output_error;
static QDict *final_document;
static XemuShortcutEvidenceOptions session_options;
static QDict *identity;
static XemuTweakResolution initial_profile;
static OwnedSource sources[XEMU_SHORTCUT_MAX_SOURCES];
static size_t source_count;
static bool source_reset, collection_error, gpu_changed;
static char *actual_backend;
static PGRAPHVkDeviceRecord actual_gpu;
static bool gpu_available;
static QDict *execution;
static uint64_t execution_revision;

#ifdef XEMU_SHORTCUT_EVIDENCE_TEST
static void (*publication_hook)(void *);
static void *publication_hook_opaque;

void xemu_shortcut_evidence_test_publication_hook(void (*hook)(void *),
                                                  void *opaque)
{
    publication_hook = hook;
    publication_hook_opaque = opaque;
}
#endif

void xemu_shortcut_evidence_publish_dsp(uint8_t *owner_state, bool gp_realtime,
                                        bool ep_realtime, bool gp_jit,
                                        bool ep_jit)
{
    if (!xemu_shortcut_evidence_enabled()) {
        return;
    }
    uint8_t state =
        0x10 | gp_realtime | (ep_realtime << 1) | (gp_jit << 2) | (ep_jit << 3);
    if (*owner_state == state) {
        return;
    }
    g_autoptr(QDict) fields = qdict_new();
    qdict_put_bool(fields, "gp_realtime", gp_realtime);
    qdict_put_bool(fields, "ep_realtime", ep_realtime);
    qdict_put_str(fields, "gp_engine", gp_jit ? "jit" : "interpreter");
    qdict_put_str(fields, "ep_engine", ep_jit ? "jit" : "interpreter");
    xemu_shortcut_evidence_publish_execution("dsp", fields, &error_abort);
    *owner_state = state;
}

bool xemu_shortcut_evidence_publish_execution(const char *component,
                                              const QDict *fields, Error **errp)
{
    if (!xemu_shortcut_evidence_enabled()) {
        return true;
    }
    if (!component ||
        (strcmp(component, "cpu") && strcmp(component, "voice") &&
         strcmp(component, "dsp") && strcmp(component, "presentation") &&
         strcmp(component, "perturbations") && strcmp(component, "renderer") &&
         strcmp(component, "vulkan_texture_stage")) ||
        !fields) {
        error_setg(errp, "Unknown execution profile component");
        return false;
    }
    for (const QDictEntry *entry = qdict_first(fields); entry;
         entry = qdict_next(fields, entry)) {
        QType type = qobject_type(qdict_entry_value(entry));
        if (type == QTYPE_QDICT || type == QTYPE_QLIST) {
            error_setg(errp, "Execution profile fields must be flat scalars");
            return false;
        }
    }
    /* The fixture can stop a publisher after preflight but before shutdown's
     * registry transaction. Production contains no hook or extra branch. */
#ifdef XEMU_SHORTCUT_EVIDENCE_TEST
    if (publication_hook) {
        publication_hook(publication_hook_opaque);
    }
#endif
    g_mutex_lock(&mutex);
    if (!initialized || closing || finalized) {
        /* Shutdown can win after the unlocked enabled preflight. Late owner
         * publication must not abort an otherwise successful early exit. */
        g_mutex_unlock(&mutex);
        return true;
    }
    if (execution_revision == UINT64_MAX) {
        collection_error = true;
        error_setg(errp,
                   "Execution profile publication is no longer available");
        g_mutex_unlock(&mutex);
        return false;
    }
    if (!execution) {
        execution = qdict_new();
    }
    QDict *previous = qobject_to(QDict, qdict_get(execution, component));
    GString *old = previous ? qobject_to_json(QOBJECT(previous)) : NULL;
    GString *next = qobject_to_json(QOBJECT(fields));
    if (!old || strcmp(old->str, next->str)) {
        qdict_put(execution, component, qdict_clone_shallow(fields));
        qatomic_set(&execution_revision, execution_revision + 1);
    }
    if (old) {
        g_string_free(old, true);
    }
    g_string_free(next, true);
    g_mutex_unlock(&mutex);
    return true;
}

uint64_t xemu_shortcut_evidence_execution_revision(void)
{
    return qatomic_read(&execution_revision);
}

void xemu_shortcut_evidence_publish_presentation(const char *transport,
                                                 const char *vendor,
                                                 const char *renderer)
{
    if (!xemu_shortcut_evidence_enabled()) {
        return;
    }
    g_autoptr(QDict) fields = qdict_new();
    qdict_put_str(fields, "transport", transport);
    if (vendor) {
        qdict_put_str(fields, "gl_vendor", vendor);
    }
    if (renderer) {
        qdict_put_str(fields, "gl_renderer", renderer);
    }
    xemu_shortcut_evidence_publish_execution("presentation", fields,
                                             &error_abort);
}

void xemu_shortcut_evidence_publish_vsync(int interval)
{
    if (xemu_shortcut_evidence_enabled()) {
        g_autoptr(QDict) fields = qdict_new();
        qdict_put_int(fields, "vsync_interval", interval);
        xemu_shortcut_evidence_publish_execution("perturbations", fields,
                                                 &error_abort);
    }
}

static void put_uint(QDict *dict, const char *name, uint64_t value)
{
    qdict_put(dict, name, qnum_from_uint(value));
}

static const char *backend_name(XemuTweakRenderer backend)
{
    switch (backend) {
    case XEMU_TWEAK_RENDERER_OPENGL:
        return "opengl";
    case XEMU_TWEAK_RENDERER_VULKAN:
        return "vulkan";
    default:
        return "none";
    }
}

static const char *policy_name(XemuTweakPolicy policy)
{
    static const char *const names[] = {"auto", "disabled", "enabled"};
    return (unsigned int)policy < ARRAY_SIZE(names) ? names[policy]
                                                     : "invalid";
}

static const char *mode_name(XemuVulkanUbershaderMode mode)
{
    static const char *const names[] = {"off", "fallback", "prewarm", "always"};
    return (unsigned int)mode < ARRAY_SIZE(names) ? names[mode] : "invalid";
}

static QDict *setting_json(const XemuTweakRuntimeState *state)
{
    QDict *dict = qdict_new();
    qdict_put_str(dict, "requested", policy_name(state->policy_requested));
    qdict_put_str(dict, "effective", state->effective ? "enabled" : "disabled");
    qdict_put_bool(dict, "requested_enabled", state->requested);
    qdict_put_bool(dict, "selected", state->selected);
    qdict_put_bool(dict, "available", state->available);
    qdict_put_int(dict, "availability", state->availability);
    qdict_put_bool(dict, "restart_pending", state->restart_pending);
    qdict_put_bool(dict, "overridden", state->overridden);
    qdict_put_str(dict, "origin",
                  state->overridden ? "process_override" : "saved_settings");
    qdict_put_str(dict, "reason", state->reason ? state->reason : "Unknown");
    return dict;
}

static QDict *settings_json(const XemuTweakResolution *profile)
{
    QDict *dict = qdict_new();
    for (unsigned int i = 0; i < XEMU_TWEAK_COUNT; i++) {
        qdict_put(dict, xemu_tweak_name(i), setting_json(&profile->state[i]));
    }
    QDict *cache = setting_json(&profile->cache);
    qdict_put_bool(cache, "installed", profile->cache_installed);
    qdict_put_bool(cache, "session_eligible", profile->cache_session_eligible);
    qdict_put_str(cache, "meaning",
                  "Permission and initialization; not cache warmth or a hit");
    qdict_put(dict, "cache_shaders", cache);
    const XemuVulkanUbershaderRuntimeState *mode = &profile->ubershader;
    QDict *uber = qdict_new();
    qdict_put_str(uber, "requested", mode_name(mode->requested));
    qdict_put_str(uber, "selected", mode_name(mode->policy));
    qdict_put_str(uber, "effective", mode_name(mode->active));
    qdict_put_bool(uber, "available", mode->available);
    qdict_put_bool(uber, "restart_pending", mode->restart_pending);
    qdict_put_bool(uber, "overridden", mode->overridden);
    qdict_put_str(uber, "origin",
                  mode->overridden ? "process_override" : "saved_settings");
    qdict_put_str(uber, "reason", mode->reason ? mode->reason : "Unknown");
    qdict_put(dict, "vk_ubershader_mode", uber);
    return dict;
}

static QDict *profile_json(const XemuTweakResolution *profile)
{
    QDict *dict = qdict_new();
    put_uint(dict, "sequence", profile->sequence);
    qdict_put_str(dict, "backend", backend_name(profile->renderer));
    put_uint(dict, "selected_bits", profile->selected_bits);
    put_uint(dict, "effective_bits", profile->effective_bits);
    qdict_put(dict, "settings", settings_json(profile));
    return dict;
}

static bool same_profile(const XemuTweakResolution *a,
                         const XemuTweakResolution *b)
{
    g_autoptr(QDict) first = profile_json(a);
    g_autoptr(QDict) second = profile_json(b);
    GString *x = qobject_to_json(QOBJECT(first));
    GString *y = qobject_to_json(QOBJECT(second));
    bool same = !strcmp(x->str, y->str);
    g_string_free(x, true);
    g_string_free(y, true);
    return same;
}

static void put_hex(QDict *dict, const char *key, const uint8_t *bytes,
                    size_t size)
{
    g_autofree char *text = g_malloc(size * 2 + 1);
    for (size_t i = 0; i < size; i++) {
        snprintf(text + i * 2, 3, "%02x", bytes[i]);
    }
    qdict_put_str(dict, key, text);
}

static QDict *gpu_json(void)
{
    QDict *gpu = qdict_new();
    qdict_put_bool(gpu, "available", gpu_available);
    if (gpu_available) {
        qdict_put_str(gpu, "name", actual_gpu.name);
        put_hex(gpu, "device_uuid", actual_gpu.device_uuid,
                sizeof(actual_gpu.device_uuid));
        put_hex(gpu, "driver_uuid", actual_gpu.driver_uuid,
                sizeof(actual_gpu.driver_uuid));
        put_uint(gpu, "vendor_id", actual_gpu.vendor_id);
        put_uint(gpu, "device_id", actual_gpu.device_id);
        put_uint(gpu, "driver_version", actual_gpu.driver_version);
        put_uint(gpu, "api_version", actual_gpu.api_version);
        qdict_put_int(gpu, "type", actual_gpu.type);
    } else {
        qdict_put_str(gpu, "reason",
                      "No owned GPU/driver identity was published");
    }
    return gpu;
}

static bool nonempty(const char *value)
{
    return value && *value;
}

static char *hash_executable(const char *path, Error **errp)
{
    FILE *file = g_fopen(path, "rb");
    if (!file) {
        error_setg_errno(errp, errno, "Cannot read running executable '%s'",
                         path);
        return NULL;
    }
    GChecksum *hash = g_checksum_new(G_CHECKSUM_SHA256);
    uint8_t bytes[65536];
    size_t count;
    while ((count = fread(bytes, 1, sizeof(bytes), file)) != 0) {
        g_checksum_update(hash, bytes, count);
    }
    char *digest = NULL;
    if (ferror(file)) {
        error_setg_errno(errp, errno ? errno : EIO,
                         "Cannot hash running executable '%s'", path);
    } else {
        digest = g_strdup(g_checksum_get_string(hash));
    }
    fclose(file);
    g_checksum_free(hash);
    return digest;
}

bool xemu_shortcut_evidence_init(const XemuShortcutEvidenceOptions *options,
                                 const XemuShortcutSessionIdentity *session,
                                 Error **errp)
{
    if (!options || !session || !nonempty(options->output_path) ||
        !nonempty(options->session_id) || !nonempty(options->workload) ||
        !nonempty(options->input_sha256) || !nonempty(options->order_group) ||
        !nonempty(options->order) || !options->frame_count ||
        options->start_frame > UINT64_MAX - options->frame_count ||
        options->position < 1 || options->position > 4 ||
        (strcmp(options->order, "ABBA") && strcmp(options->order, "BAAB")) ||
        !nonempty(session->commit) || !nonempty(session->version) ||
        !nonempty(session->build_type) || !nonempty(session->platform) ||
        !nonempty(session->requested_backend) ||
        !nonempty(session->requested_gpu) ||
        !nonempty(session->base_config_sha256) ||
        !nonempty(session->comparison_config_sha256) || !session->input_paths ||
        !session->screenshot_directory) {
        error_setg(errp,
                   "Incomplete shortcut evidence options or build identity");
        return false;
    }
    g_mutex_lock(&mutex);
    if (initialized) {
        error_setg(errp, "Shortcut evidence session is already initialized");
        g_mutex_unlock(&mutex);
        return false;
    }
    /* Resolve the running file here; a caller-supplied path/hash is not
     * identity. */
    g_autofree char *path = xemu_shortcut_evidence_executable_path(errp);
    g_autofree char *digest = path ? hash_executable(path, errp) : NULL;
    if (!digest) {
        g_mutex_unlock(&mutex);
        return false;
    }
    identity = qdict_new();
    qdict_put_str(identity, "schema", "xemu-shortcut-evidence/v1");
    qdict_put_str(identity, "commit", session->commit);
    qdict_put_str(identity, "version", session->version);
    qdict_put_str(identity, "build_type", session->build_type);
    qdict_put_str(identity, "platform", session->platform);
    qdict_put_str(identity, "executable_path", path);
    qdict_put_str(identity, "executable_sha256", digest);
    qdict_put_str(identity, "requested_backend", session->requested_backend);
    qdict_put_str(identity, "requested_gpu", session->requested_gpu);
    qdict_put_str(identity, "base_config_sha256", session->base_config_sha256);
    qdict_put_str(identity, "comparison_config_sha256",
                  session->comparison_config_sha256);
    qdict_put(identity, "input_paths", qdict_clone_shallow(session->input_paths));
    qdict_put_str(identity, "screenshot_directory", session->screenshot_directory);
    qdict_put_str(identity, "session_id", options->session_id);
    qdict_put_str(identity, "workload", options->workload);
    qdict_put_str(identity, "input_sha256", options->input_sha256);
    qdict_put_str(identity, "order_group", options->order_group);
    qdict_put_str(identity, "order", options->order);
    put_uint(identity, "position", options->position);
    qdict_put_str(identity, "input_provenance",
                  "Runner declaration; not verified guest input");
    qdict_put_str(identity, "timing_provenance",
                  "Live instrumented owner window");
    qdict_put_str(identity, "cache_provenance",
                  "Permission observed; cache contents/warmth not established");
    session_options = *options;
    session_options.output_path = g_strdup(options->output_path);
    session_options.session_id = g_strdup(options->session_id);
    session_options.workload = g_strdup(options->workload);
    session_options.input_sha256 = g_strdup(options->input_sha256);
    session_options.order_group = g_strdup(options->order_group);
    session_options.order = g_strdup(options->order);
    initial_profile = session->initial_profile;
    initialized = true;
    qatomic_set(&enabled, 1);
    g_mutex_unlock(&mutex);
    return true;
}

bool xemu_shortcut_evidence_enabled(void)
{
    return qatomic_read(&enabled);
}

bool xemu_shortcut_evidence_window(uint64_t *start, uint64_t *count)
{
    g_mutex_lock(&mutex);
    bool active = initialized && !closing && !finalized;
    if (active && start && count) {
        *start = session_options.start_frame;
        *count = session_options.frame_count;
    }
    g_mutex_unlock(&mutex);
    return active && start && count;
}

char *xemu_shortcut_evidence_executable_path(Error **errp)
{
#ifdef __linux__
    /* This inode remains the running file even when its pathname is replaced.
     */
    return g_strdup("/proc/self/exe");
#elif defined(_WIN32)
    for (DWORD size = 512; size <= 32768; size *= 2) {
        g_autofree wchar_t *path = g_new(wchar_t, size);
        DWORD count = GetModuleFileNameW(NULL, path, size);
        if (count && count < size) {
            GError *conversion_error = NULL;
            char *utf8 = g_utf16_to_utf8((gunichar2 *)path, count, NULL, NULL,
                                         &conversion_error);
            if (!utf8) {
                error_setg(errp, "Cannot encode running executable path: %s",
                           conversion_error->message);
            }
            g_clear_error(&conversion_error);
            return utf8;
        }
        if (!count) {
            break;
        }
    }
    error_setg(errp, "Cannot resolve running executable (Windows error %lu)",
               GetLastError());
    return NULL;
#elif defined(__APPLE__)
    uint32_t size = 0;
    _NSGetExecutablePath(NULL, &size);
    g_autofree char *path = g_malloc(size);
    if (_NSGetExecutablePath(path, &size) == 0) {
        char *resolved = realpath(path, NULL);
        if (resolved) {
            return resolved;
        }
    }
    error_setg_errno(errp, errno, "Cannot resolve running executable");
    return NULL;
#else
    error_setg(errp,
               "Running executable identity is unsupported on this platform");
    return NULL;
#endif
}

void xemu_shortcut_evidence_publish_gpu(const char *backend,
                                        const PGRAPHVkDeviceRecord *device)
{
    if (!xemu_shortcut_evidence_enabled() || !backend) {
        return;
    }
    g_mutex_lock(&mutex);
    if (!closing && !finalized) {
        if (actual_backend && strcmp(actual_backend, backend)) {
            gpu_changed = true;
        }
        if (device && gpu_available &&
            (memcmp(actual_gpu.device_uuid, device->device_uuid,
                    sizeof(device->device_uuid)) ||
             memcmp(actual_gpu.driver_uuid, device->driver_uuid,
                    sizeof(device->driver_uuid)) ||
             actual_gpu.vendor_id != device->vendor_id ||
             actual_gpu.device_id != device->device_id ||
             actual_gpu.driver_version != device->driver_version ||
             actual_gpu.api_version != device->api_version)) {
            gpu_changed = true;
        }
        g_free(actual_backend);
        actual_backend = g_strdup(backend);
        gpu_available = device != NULL;
        if (device) {
            actual_gpu = *device;
            actual_gpu.name[sizeof(actual_gpu.name) - 1] = 0;
            actual_gpu.rejection_reason = NULL;
        }
    }
    g_mutex_unlock(&mutex);
}

static bool valid_counter_name(const char *name)
{
    bool dot = false, segment = false;
    if (!nonempty(name) || strlen(name) > 128) {
        return false;
    }
    for (const char *p = name; *p; p++) {
        if (*p == '.') {
            if (!segment) {
                return false;
            }
            dot = true;
            segment = false;
        } else if (g_ascii_islower(*p) || g_ascii_isdigit(*p) || *p == '_') {
            segment = true;
        } else {
            return false;
        }
    }
    return dot && segment;
}

uint64_t
xemu_shortcut_evidence_register_source(const XemuShortcutCounterSource *source,
                                       Error **errp)
{
    g_mutex_lock(&mutex);
    if (!initialized || closing || finalized) {
        error_setg(errp, "Shortcut evidence session is not recording");
        g_mutex_unlock(&mutex);
        return 0;
    }
    if (!source || !nonempty(source->id) || strlen(source->id) > 128 ||
        !source->snapshot || source->count > XEMU_SHORTCUT_MAX_COUNTERS ||
        (source->count && !source->descriptors)) {
        error_setg(errp,
                   "Invalid shortcut counter source or descriptor capacity");
        goto invalid;
    }
    for (size_t i = 0; i < source_count; i++) {
        if (!strcmp(sources[i].id, source->id) && !sources[i].retired) {
            error_setg(errp, "Counter source '%s' is already registered",
                       source->id);
            goto invalid;
        }
    }
    if (source_count == XEMU_SHORTCUT_MAX_SOURCES) {
        error_setg(errp,
                   "Shortcut counter source incarnation capacity exceeded");
        goto invalid;
    }
    for (size_t i = 0; i < source->count; i++) {
        const XemuShortcutCounterDescriptor *d = &source->descriptors[i];
        if (!valid_counter_name(d->name) || !nonempty(d->unit) ||
            strlen(d->unit) > 64) {
            error_setg(errp, "Invalid shortcut counter name or unit");
            goto invalid;
        }
        for (size_t j = 0; j < i; j++) {
            if (!strcmp(d->name, source->descriptors[j].name)) {
                error_setg(errp, "Duplicate counter '%s' in source '%s'",
                           d->name, source->id);
                goto invalid;
            }
        }
    }
    OwnedSource *owned = &sources[source_count];
    owned->id = g_strdup(source->id);
    owned->count = source->count;
    owned->copy = source->snapshot;
    owned->opaque = source->opaque;
    for (size_t i = 0; i < source->count; i++) {
        owned->descriptors[i].name = g_strdup(source->descriptors[i].name);
        owned->descriptors[i].unit = g_strdup(source->descriptors[i].unit);
    }
    for (size_t i = 0; i < source_count; i++) {
        source_reset |= !strcmp(sources[i].id, source->id);
    }
    uint64_t handle = ++source_count;
    g_mutex_unlock(&mutex);
    return handle;
invalid:
    collection_error = true;
    g_mutex_unlock(&mutex);
    return 0;
}

bool xemu_shortcut_evidence_unregister_source(uint64_t handle, Error **errp)
{
    g_mutex_lock(&mutex);
    if (!handle || handle > source_count) {
        error_setg(errp, "Unknown shortcut counter source handle");
        g_mutex_unlock(&mutex);
        return false;
    }
    OwnedSource *source = &sources[handle - 1];
    if (!source->retired) {
        if (!closing && !finalized) {
            source->copy(source->opaque, &source->final);
        }
        source->retired = true;
        source->copy = NULL;
        source->opaque = NULL;
    }
    g_mutex_unlock(&mutex);
    return true;
}

static QDict *window_json(const XemuShortcutCounterSnapshot *copy)
{
    QDict *window = qdict_new();
    put_uint(window, "progress_incarnation", copy->progress_incarnation);
    put_uint(window, "start_frame", copy->start_frame);
    put_uint(window, "end_frame", copy->end_frame);
    put_uint(window, "start_monotonic_ns", copy->start_monotonic_ns);
    put_uint(window, "end_monotonic_ns", copy->end_monotonic_ns);
    put_uint(window, "start_execution_revision",
             copy->start_execution_revision);
    put_uint(window, "end_execution_revision", copy->end_execution_revision);
    return window;
}

/* Caller holds the registry lock. JSONObject storage owns all copied strings.
 */
static QDict *snapshot_locked(void)
{
    QDict *dict = qdict_clone_shallow(identity);
    QDict *counters = qdict_new(), *units = qdict_new(), *invalid = qdict_new();
    QList *list = qlist_new();
    bool complete = source_count && gpu_available && !source_reset &&
                    !collection_error && !gpu_changed;
    bool overflowed = false, profile_changed = false, windows_match = true;
    bool profiles_published = source_count != 0;
    XemuShortcutCounterSnapshot first = {0};
    for (size_t i = 0; i < source_count; i++) {
        OwnedSource *source = &sources[i];
        XemuShortcutCounterSnapshot copy = source->final;
        if (!source->retired) {
            source->copy(source->opaque, &copy);
        }
        if (!i) {
            first = copy;
        }
        bool window_ok =
            copy.progress_incarnation &&
            copy.start_frame == session_options.start_frame &&
            copy.end_frame >= copy.start_frame &&
            copy.end_frame - copy.start_frame == session_options.frame_count &&
            copy.start_monotonic_ns &&
            copy.end_monotonic_ns > copy.start_monotonic_ns;
        bool changed =
            !same_profile(&copy.start_profile, &copy.end_profile) ||
            copy.start_execution_revision != copy.end_execution_revision ||
            copy.end_execution_revision != execution_revision;
        profiles_published &=
            copy.start_profile.sequence && copy.end_profile.sequence;
        profile_changed |= changed;
        windows_match &=
            copy.start_frame == first.start_frame &&
            copy.end_frame == first.end_frame &&
            copy.progress_incarnation == first.progress_incarnation &&
            same_profile(&first.end_profile, &copy.end_profile);
        complete &= copy.complete && window_ok && !changed && !copy.overflowed;
        overflowed |= copy.overflowed;
        QDict *record = qdict_new(), *values = qdict_new(),
              *source_units = qdict_new();
        qdict_put_str(record, "id", source->id);
        put_uint(record, "incarnation", i + 1);
        qdict_put_bool(record, "retired", source->retired);
        qdict_put_bool(record, "complete", copy.complete);
        qdict_put_bool(record, "window_complete", window_ok);
        qdict_put_bool(record, "overflowed", copy.overflowed);
        qdict_put_bool(record, "profile_changed", changed);
        qdict_put(record, "window", window_json(&copy));
        qdict_put(record, "start_profile", profile_json(&copy.start_profile));
        qdict_put(record, "end_profile", profile_json(&copy.end_profile));
        for (size_t j = 0; j < source->count; j++) {
            const char *name = source->descriptors[j].name;
            const char *unit = source->descriptors[j].unit;
            uint64_t value = copy.values[j];
            put_uint(values, name, value);
            qdict_put_str(source_units, name, unit);
            if (qdict_haskey(invalid, name)) {
                continue;
            }
            if (qdict_haskey(counters, name)) {
                uint64_t previous = qdict_get_uint(counters, name);
                bool wraps = previous > UINT64_MAX - value;
                if (wraps || strcmp(qdict_get_str(units, name), unit)) {
                    qdict_del(counters, name);
                    qdict_put_bool(invalid, name, true);
                    complete = false;
                    overflowed |= wraps;
                    collection_error = true;
                    continue;
                }
                value += previous;
            }
            put_uint(counters, name, value);
            qdict_put_str(units, name, unit);
        }
        qdict_put(record, "counters", values);
        qdict_put(record, "units", source_units);
        qlist_append(list, record);
    }
    bool backend_match =
        source_count && actual_backend &&
        first.end_profile.renderer != XEMU_TWEAK_RENDERER_NONE &&
        !strcmp(actual_backend, backend_name(first.end_profile.renderer));
    complete &= windows_match && backend_match && profiles_published;
    qdict_put_str(dict, "actual_backend",
                  actual_backend ? actual_backend : "unknown");
    qdict_put(dict, "gpu", gpu_json());
    qdict_put(dict, "execution",
              execution ? qdict_clone_shallow(execution) : qdict_new());
    put_uint(dict, "execution_revision", execution_revision);
    qdict_put_bool(dict, "gpu_changed", gpu_changed);
    qdict_put_bool(dict, "source_reset", source_reset);
    qdict_put_bool(dict, "collection_error", collection_error);
    qdict_put_bool(dict, "overflowed", overflowed);
    qdict_put_bool(dict, "profile_changed", profile_changed);
    qdict_put_bool(dict, "windows_match", windows_match);
    qdict_put_bool(dict, "backend_match", backend_match);
    qdict_put_bool(dict, "profiles_published", profiles_published);
    qdict_put_bool(dict, "complete", complete);
    QDict *configured = qdict_new();
    put_uint(configured, "start_frame", session_options.start_frame);
    put_uint(configured, "frame_count", session_options.frame_count);
    qdict_put(dict, "configured_window", configured);
    if (source_count) {
        qdict_put(dict, "actual_window", window_json(&first));
        if (first.end_monotonic_ns >= first.start_monotonic_ns &&
            first.start_monotonic_ns) {
            put_uint(dict, "wall_interval_ns",
                     first.end_monotonic_ns - first.start_monotonic_ns);
            qdict_put_str(dict, "wall_interval_source", sources[0].id);
        }
    } else {
        qdict_put_null(dict, "actual_window");
    }
    qdict_put(
        dict, "settings",
        settings_json(source_count ? &first.end_profile : &initial_profile));
    qdict_put(dict, "initial_profile", profile_json(&initial_profile));
    qdict_put(dict, "counters", counters);
    qdict_put(dict, "counter_units", units);
    qdict_put(dict, "invalid_counters", invalid);
    qdict_put(dict, "sources", list);
    return dict;
}

XemuShortcutEvidenceSnapshot *xemu_shortcut_evidence_snapshot(Error **errp)
{
    g_mutex_lock(&mutex);
    while (closing) {
        g_cond_wait(&shutdown_done, &mutex);
    }
    if (!initialized) {
        error_setg(errp, "Shortcut evidence session is not initialized");
        g_mutex_unlock(&mutex);
        return NULL;
    }
    XemuShortcutEvidenceSnapshot *snapshot =
        g_new0(XemuShortcutEvidenceSnapshot, 1);
    snapshot->document =
        finalized ? qobject_ref(final_document) : snapshot_locked();
    g_mutex_unlock(&mutex);
    return snapshot;
}

char *xemu_shortcut_evidence_snapshot_json(
    const XemuShortcutEvidenceSnapshot *snapshot)
{
    if (!snapshot) {
        return NULL;
    }
    return g_string_free(qobject_to_json(QOBJECT(snapshot->document)), false);
}

void xemu_shortcut_evidence_snapshot_free(
    XemuShortcutEvidenceSnapshot *snapshot)
{
    if (snapshot) {
        qobject_unref(snapshot->document);
        g_free(snapshot);
    }
}

bool xemu_shortcut_evidence_shutdown(Error **errp)
{
    g_mutex_lock(&mutex);
    while (closing) {
        g_cond_wait(&shutdown_done, &mutex);
    }
    if (!initialized) {
        g_mutex_unlock(&mutex);
        return true;
    }
    if (!finalized) {
        closing = true;
        qatomic_set(&enabled, 0);
        final_document = snapshot_locked();
        GString *json = qobject_to_json(QOBJECT(final_document));
        g_mutex_unlock(&mutex);
        GError *error = NULL;
        bool written = g_file_set_contents_full(
            session_options.output_path, json->str, json->len,
            G_FILE_SET_CONTENTS_CONSISTENT | G_FILE_SET_CONTENTS_DURABLE, 0600,
            &error);
        g_string_free(json, true);
        g_mutex_lock(&mutex);
        if (!written) {
            output_error =
                g_strdup_printf("Shortcut evidence output '%s' failed: %s",
                                session_options.output_path, error->message);
        }
        g_clear_error(&error);
        closing = false;
        finalized = true;
        g_cond_broadcast(&shutdown_done);
    }
    bool success = output_error == NULL;
    if (!success) {
        error_setg(errp, "%s", output_error);
    }
    g_mutex_unlock(&mutex);
    return success;
}

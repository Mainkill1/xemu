/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "qemu/osdep.h"
#include "qobject/qdict.h"
#include "qobject/qjson.h"
#include "qobject/qlist.h"
#include "ui/xemu-shortcut-evidence.h"
#include "ui/xemu-shortcut-window.h"

typedef struct Fixture {
    XemuShortcutCounterSnapshot published;
    unsigned int copies;
    GMutex mutex;
    GCond condition;
    bool block;
    bool entered;
    bool release;
    int unregister_started;
    int unregister_done;
    uint64_t handle;
} Fixture;

static char *directory;
static char *invoked_program;

static void copy_source(void *opaque, XemuShortcutCounterSnapshot *out)
{
    Fixture *fixture = opaque;
    g_mutex_lock(&fixture->mutex);
    fixture->entered = true;
    g_cond_broadcast(&fixture->condition);
    while (fixture->block && !fixture->release) {
        g_cond_wait(&fixture->condition, &fixture->mutex);
    }
    *out = fixture->published;
    fixture->copies++;
    g_mutex_unlock(&fixture->mutex);
}

static Fixture fixture(uint64_t value)
{
    XemuTweakRequestedState request = {0};
    XemuTweakEnvironment env = {
        .renderer = XEMU_TWEAK_RENDERER_VULKAN,
        .ubershader_installed = true,
        .ubershader_operational = true,
        .cache_renderer = XEMU_TWEAK_RENDERER_VULKAN,
        .cache_installed = true,
        .cache_session_eligible = true,
    };
    XemuTweakResolution profile =
        xemu_tweaks_resolve(&request, &env, NULL, true);
    profile.sequence = 1;
    Fixture fixture = {.published = {
                         .values = {value},
                         .complete = true,
                         .progress_incarnation = 1,
                         .start_frame = 100,
                         .end_frame = 120,
                         .start_monotonic_ns = 1000,
                         .end_monotonic_ns = 3000,
                         .start_profile = profile,
                         .end_profile = profile,
                     } };
    return fixture;
}

static void init_service(bool writable)
{
    Error *error = NULL;
    GError *io_error = NULL;
    directory = g_dir_make_tmp("xemu-shortcut-evidence-XXXXXX", &io_error);
    g_assert_no_error(io_error);
    XemuShortcutEvidenceOptions options = {
        .output_path = g_build_filename(
            directory, writable ? "run.json" : "missing/run.json", NULL),
        .session_id = g_strdup("s-A1"),
        .workload = g_strdup("owned-fixture"),
        .input_sha256 = g_strdup(
            "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"),
        .order_group = g_strdup("group"),
        .order = g_strdup("ABBA"),
        .position = 1,
        .start_frame = 100,
        .frame_count = 20,
    };
    char *exe = xemu_shortcut_evidence_executable_path(&error);
    g_assert_null(error);
    char *commit = g_strdup("0123456789abcdef0123456789abcdef01234567");
    g_autoptr(QDict) input_paths = qdict_new();
    const char *roles[] = { "bootrom", "flashrom", "eeprom", "hdd", "dvd" };
    for (size_t i = 0; i < ARRAY_SIZE(roles); i++) {
        qdict_put_str(input_paths, roles[i], "");
    }
    XemuShortcutSessionIdentity identity = {
        .commit = commit,
        .version = "fixture",
        .build_type = "unit-fixture",
        .platform = "test-host",
        .requested_backend = "vulkan",
        .requested_gpu = "auto",
        .base_config_sha256 =
            "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
        .comparison_config_sha256 =
            "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb",
        .input_paths = input_paths,
        .initial_profile = fixture(0).published.start_profile,
    };
    g_assert_true(xemu_shortcut_evidence_init(&options, &identity, &error));
    g_assert_null(error);
    qdict_put_str(input_paths, "eeprom", "changed-after-initialization");
    memset(commit, 'x', strlen(commit));
    g_free(commit);
    g_free(exe);
    xemu_shortcut_evidence_options_clear(&options);
    PGRAPHVkDeviceRecord device = {.vendor_id = 0x1002,
                                   .device_id = 0x164e,
                                   .driver_version = 23,
                                   .api_version = 42};
    strcpy(device.name, "fixture \"GPU\"");
    memset(device.device_uuid, 0xaa, sizeof(device.device_uuid));
    memset(device.driver_uuid, 0xbb, sizeof(device.driver_uuid));
    xemu_shortcut_evidence_publish_gpu("vulkan", &device);
    memset(&device, 0, sizeof(device));
}

static bool child(void)
{
    if (g_test_subprocess()) {
        return true;
    }
    g_test_trap_subprocess(NULL, 0, (GTestSubprocessFlags)0);
    g_test_trap_assert_passed();
    return false;
}

static uint64_t register_source(Fixture *f, const char *id, const char *name)
{
    char *owned_name = g_strdup(name);
    char *owned_unit = g_strdup("events");
    XemuShortcutCounterDescriptor descriptor = {owned_name, owned_unit};
    XemuShortcutCounterSource source = {id, &descriptor, 1, copy_source, f};
    Error *error = NULL;
    uint64_t handle = xemu_shortcut_evidence_register_source(&source, &error);
    g_assert_null(error);
    g_assert_cmpuint(handle, >, 0);
    memset(owned_name, 'x', strlen(owned_name));
    g_free(owned_name);
    g_free(owned_unit);
    return handle;
}

static QDict *snapshot(void)
{
    Error *error = NULL;
    XemuShortcutEvidenceSnapshot *owned =
        xemu_shortcut_evidence_snapshot(&error);
    g_assert_null(error);
    g_assert_nonnull(owned);
    char *text = xemu_shortcut_evidence_snapshot_json(owned);
    xemu_shortcut_evidence_snapshot_free(owned);
    QDict *document = qobject_to(QDict, qobject_from_json(text, &error));
    g_assert_null(error);
    g_assert_nonnull(document);
    g_free(text);
    return document;
}

static void cleanup(void)
{
    char *path = g_build_filename(directory, "run.json", NULL);
    g_unlink(path);
    g_assert_cmpint(g_rmdir(directory), ==, 0);
    g_free(path);
    g_free(directory);
}

static void test_owned_full_width(void)
{
    if (!child()) {
        return;
    }
    init_service(true);
    Fixture f = fixture(UINT64_MAX);
    uint64_t handle = register_source(&f, "vk", "vk.draw.attempts");
    QDict *document = snapshot();
    g_assert_cmpstr(qdict_get_str(document, "schema"), ==,
                    "xemu-shortcut-evidence/v1");
    g_assert_cmpstr(qdict_get_str(document, "commit"), ==,
                    "0123456789abcdef0123456789abcdef01234567");
    g_assert_cmpstr(qdict_get_str(document, "session_id"), ==, "s-A1");
    g_assert_cmpstr(qdict_get_str(qdict_get_qdict(document, "input_paths"),
                                  "eeprom"), ==, "");
    QDict *gpu = qdict_get_qdict(document, "gpu");
    g_assert_cmpstr(qdict_get_str(gpu, "name"), ==, "fixture \"GPU\"");
    g_assert_cmpstr(qdict_get_str(gpu, "driver_uuid"), ==,
                    "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb");
    g_assert_cmpuint(qdict_get_uint(gpu, "driver_version"), ==, 23);
    g_assert_cmpuint(qdict_get_uint(qdict_get_qdict(document, "counters"),
                                    "vk.draw.attempts"),
                     ==, UINT64_MAX);
    g_assert_true(qdict_get_bool(document, "complete"));
    g_assert_cmpuint(f.published.values[0], ==, UINT64_MAX);
    QDict *settings = qdict_get_qdict(document, "settings");
    g_assert_nonnull(qdict_get_qdict(settings, "cache_shaders"));
    for (unsigned int i = 0; i < XEMU_TWEAK_COUNT; i++) {
        QDict *state = qdict_get_qdict(settings, xemu_tweak_name(i));
        g_assert_nonnull(state);
        g_assert_nonnull(qdict_get_str(state, "requested"));
        g_assert_nonnull(qdict_get_str(state, "effective"));
        g_assert_true(qdict_haskey(state, "restart_pending"));
    }
    char *bytes;
    gsize size;
    /* Use an independent kernel path on Linux, not the service's resolver. On
     * other test hosts the invoked executable remains at its launch pathname.
     */
#ifdef __linux__
    const char *expected_path = "/proc/self/exe";
#else
    const char *expected_path = invoked_program;
#endif
    g_assert_true(g_file_get_contents(expected_path, &bytes, &size, NULL));
    char *expected =
        g_compute_checksum_for_data(G_CHECKSUM_SHA256, (guchar *)bytes, size);
    g_assert_cmpstr(qdict_get_str(document, "executable_sha256"), ==, expected);
    g_free(expected);
    g_free(bytes);
    g_assert_true(
        xemu_shortcut_evidence_unregister_source(handle, &error_abort));
    f.published.values[0] = 7;
    QDict *final = snapshot();
    g_assert_cmpuint(
        qdict_get_uint(qdict_get_qdict(final, "counters"), "vk.draw.attempts"),
        ==, UINT64_MAX);
    unsigned int copies = f.copies;
    g_assert_true(xemu_shortcut_evidence_shutdown(&error_abort));
    g_assert_true(xemu_shortcut_evidence_shutdown(&error_abort));
    g_assert_cmpuint(f.copies, ==, copies);
    char *path = g_build_filename(directory, "run.json", NULL);
    char *json;
    g_assert_true(g_file_get_contents(path, &json, NULL, NULL));
    QDict *reopened = qobject_to(QDict, qobject_from_json(json, &error_abort));
    g_assert_cmpuint(qdict_get_uint(qdict_get_qdict(reopened, "counters"),
                                    "vk.draw.attempts"),
                     ==, UINT64_MAX);
    qobject_unref(reopened);
    qobject_unref(document);
    qobject_unref(final);
    g_free(json);
    g_free(path);
    cleanup();
}

static void test_merge_and_overflow(void)
{
    if (!child()) {
        return;
    }
    init_service(true);
    Fixture a = fixture(10), b = fixture(21);
    register_source(&a, "owner-a", "vk.draw.attempts");
    register_source(&b, "owner-b", "vk.draw.attempts");
    QDict *document = snapshot();
    g_assert_cmpuint(qdict_get_uint(qdict_get_qdict(document, "counters"),
                                    "vk.draw.attempts"),
                     ==, 31);
    qobject_unref(document);
    a.published.values[0] = UINT64_MAX;
    document = snapshot();
    g_assert_true(qdict_get_bool(document, "overflowed"));
    g_assert_false(qdict_get_bool(document, "complete"));
    g_assert_false(qdict_haskey(qdict_get_qdict(document, "counters"),
                                "vk.draw.attempts"));
    qobject_unref(document);
    g_assert_true(xemu_shortcut_evidence_shutdown(&error_abort));
    cleanup();
}

static void test_incomplete_and_reset(void)
{
    if (!child()) {
        return;
    }
    init_service(true);
    Fixture a = fixture(12);
    a.published.complete = false;
    uint64_t handle = register_source(&a, "vk", "vk.draw.attempts");
    QDict *document = snapshot();
    g_assert_false(qdict_get_bool(document, "complete"));
    qobject_unref(document);
    g_assert_true(
        xemu_shortcut_evidence_unregister_source(handle, &error_abort));
    Fixture b = fixture(13);
    uint64_t next = register_source(&b, "vk", "vk.draw.attempts");
    g_assert_cmpuint(next, !=, handle);
    document = snapshot();
    g_assert_true(qdict_get_bool(document, "source_reset"));
    g_assert_false(qdict_get_bool(document, "complete"));
    qobject_unref(document);
    g_assert_true(xemu_shortcut_evidence_shutdown(&error_abort));
    cleanup();
}

static gpointer snapshot_thread(gpointer unused)
{
    QDict *document = snapshot();
    qobject_unref(document);
    return NULL;
}

static gpointer unregister_thread(gpointer opaque)
{
    Fixture *f = opaque;
    qatomic_set(&f->unregister_started, 1);
    g_assert_true(
        xemu_shortcut_evidence_unregister_source(f->handle, &error_abort));
    qatomic_set(&f->unregister_done, 1);
    return NULL;
}

static void test_unregister_waits_for_callback(void)
{
    if (!child()) {
        return;
    }
    init_service(true);
    Fixture *f = g_new0(Fixture, 1);
    *f = fixture(42);
    f->block = true;
    f->handle = register_source(f, "vk", "vk.draw.attempts");
    GThread *reader = g_thread_new("evidence-copy", snapshot_thread, NULL);
    g_mutex_lock(&f->mutex);
    while (!f->entered) {
        g_assert_true(g_cond_wait_until(&f->condition, &f->mutex,
                                        g_get_monotonic_time() + 5000000));
    }
    g_mutex_unlock(&f->mutex);
    GThread *closer = g_thread_new("evidence-close", unregister_thread, f);
    while (!qatomic_read(&f->unregister_started)) {
        g_thread_yield();
    }
    g_assert_false(qatomic_read(&f->unregister_done));
    g_mutex_lock(&f->mutex);
    f->release = true;
    g_cond_broadcast(&f->condition);
    g_mutex_unlock(&f->mutex);
    g_thread_join(reader);
    g_thread_join(closer);
    g_cond_clear(&f->condition);
    g_mutex_clear(&f->mutex);
    g_free(f);
    QDict *document = snapshot();
    g_assert_cmpuint(qdict_get_uint(qdict_get_qdict(document, "counters"),
                                    "vk.draw.attempts"),
                     ==, 42);
    qobject_unref(document);
    g_assert_true(xemu_shortcut_evidence_shutdown(&error_abort));
    cleanup();
}

static void test_explicit_output_failure(void)
{
    if (!child()) {
        return;
    }
    init_service(false);
    Fixture f = fixture(1);
    register_source(&f, "vk", "vk.draw.attempts");
    Error *error = NULL;
    g_assert_false(xemu_shortcut_evidence_shutdown(&error));
    g_assert_nonnull(error);
    g_assert_nonnull(strstr(error_get_pretty(error), "missing/run.json"));
    g_assert_nonnull(strstr(error_get_pretty(error), g_strerror(ENOENT)));
    error_free(error);
    unsigned int copies = f.copies;
    error = NULL;
    g_assert_false(xemu_shortcut_evidence_shutdown(&error));
    g_assert_nonnull(error);
    g_assert_cmpuint(f.copies, ==, copies);
    error_free(error);
    cleanup();
}

static void test_missing_sources_is_not_zero_evidence(void)
{
    if (!child()) {
        return;
    }
    init_service(true);
    QDict *document = snapshot();
    g_assert_false(qdict_get_bool(document, "complete"));
    g_assert_cmpuint(qdict_size(qdict_get_qdict(document, "counters")), ==, 0);
    g_assert_cmpuint(qlist_size(qdict_get_qlist(document, "sources")), ==, 0);
    qobject_unref(document);
    g_assert_true(xemu_shortcut_evidence_shutdown(&error_abort));
    cleanup();
}

static void test_profile_backend_mismatch(void)
{
    if (!child()) {
        return;
    }
    init_service(true);
    Fixture f = fixture(12);
    f.published.start_profile.renderer = XEMU_TWEAK_RENDERER_OPENGL;
    f.published.end_profile.renderer = XEMU_TWEAK_RENDERER_OPENGL;
    register_source(&f, "vk", "vk.draw.attempts");
    QDict *document = snapshot();
    g_assert_false(qdict_get_bool(document, "complete"));
    g_assert_false(qdict_get_bool(document, "backend_match"));
    qobject_unref(document);
    g_assert_true(xemu_shortcut_evidence_shutdown(&error_abort));
    cleanup();
}

static void test_capacity_is_explicit(void)
{
    if (!child()) {
        return;
    }
    init_service(true);
    Fixture f = fixture(12);
    for (unsigned int i = 0; i < 8; i++) {
        char *id = g_strdup_printf("owner-%u", i);
        register_source(&f, id, "vk.draw.attempts");
        g_free(id);
    }
    g_assert_cmpuint(f.copies, ==, 0);
    XemuShortcutCounterDescriptor descriptor = {"vk.draw.attempts", "events"};
    XemuShortcutCounterSource extra = {"ninth", &descriptor, 1, copy_source,
                                       &f};
    Error *error = NULL;
    g_assert_cmpuint(xemu_shortcut_evidence_register_source(&extra, &error), ==,
                     0);
    g_assert_nonnull(error);
    g_assert_nonnull(strstr(error_get_pretty(error), "capacity"));
    error_free(error);
    QDict *document = snapshot();
    g_assert_false(qdict_get_bool(document, "complete"));
    g_assert_true(qdict_get_bool(document, "collection_error"));
    g_assert_cmpuint(qlist_size(qdict_get_qlist(document, "sources")), ==, 8);
    g_assert_cmpuint(qdict_get_uint(qdict_get_qdict(document, "counters"),
                                    "vk.draw.attempts"),
                     ==, 96);
    qobject_unref(document);
    g_assert_true(xemu_shortcut_evidence_shutdown(&error_abort));
    cleanup();
}

static void test_conflicting_units_are_not_added(void)
{
    if (!child()) {
        return;
    }
    init_service(true);
    Fixture a = fixture(12), b = fixture(13);
    register_source(&a, "owner-a", "vk.draw.attempts");
    XemuShortcutCounterDescriptor descriptor = {"vk.draw.attempts", "bytes"};
    XemuShortcutCounterSource other = {"owner-b", &descriptor, 1, copy_source,
                                       &b};
    g_assert_cmpuint(
        xemu_shortcut_evidence_register_source(&other, &error_abort), >, 0);
    QDict *document = snapshot();
    g_assert_false(qdict_get_bool(document, "complete"));
    g_assert_false(qdict_haskey(qdict_get_qdict(document, "counters"),
                                "vk.draw.attempts"));
    g_assert_true(qdict_haskey(qdict_get_qdict(document, "invalid_counters"),
                               "vk.draw.attempts"));
    g_assert_cmpuint(qlist_size(qdict_get_qlist(document, "sources")), ==, 2);
    qobject_unref(document);
    g_assert_true(xemu_shortcut_evidence_shutdown(&error_abort));
    cleanup();
}

static void test_window_and_profile_must_be_stable(void)
{
    if (!child()) {
        return;
    }
    init_service(true);
    Fixture f = fixture(12);
    f.published.end_frame = 119;
    f.published.end_profile.sequence++;
    register_source(&f, "vk", "vk.draw.attempts");
    QDict *document = snapshot();
    g_assert_false(qdict_get_bool(document, "complete"));
    g_assert_true(qdict_get_bool(document, "profile_changed"));
    g_assert_cmpuint(
        qdict_get_uint(qdict_get_qdict(document, "actual_window"), "end_frame"),
        ==, 119);
    g_assert_cmpuint(qdict_get_uint(qdict_get_qdict(document, "counters"),
                                    "vk.draw.attempts"),
                     ==, 12);
    qobject_unref(document);
    g_assert_true(xemu_shortcut_evidence_shutdown(&error_abort));
    cleanup();
}

static void test_snapshot_survives_owner_changes(void)
{
    if (!child()) {
        return;
    }
    init_service(true);
    Fixture f = fixture(42);
    uint64_t handle = register_source(&f, "vk", "vk.draw.attempts");
    XemuShortcutEvidenceSnapshot *owned =
        xemu_shortcut_evidence_snapshot(&error_abort);
    f.published.values[0] = 22;
    g_assert_true(
        xemu_shortcut_evidence_unregister_source(handle, &error_abort));
    PGRAPHVkDeviceRecord replacement_gpu = {.driver_version = 24};
    xemu_shortcut_evidence_publish_gpu("vulkan", &replacement_gpu);
    char *json = xemu_shortcut_evidence_snapshot_json(owned);
    QDict *document = qobject_to(QDict, qobject_from_json(json, &error_abort));
    g_assert_cmpuint(qdict_get_uint(qdict_get_qdict(document, "counters"),
                                    "vk.draw.attempts"),
                     ==, 42);
    g_assert_cmpuint(
        qdict_get_uint(qdict_get_qdict(document, "gpu"), "driver_version"), ==,
        23);
    qobject_unref(document);
    g_free(json);
    xemu_shortcut_evidence_snapshot_free(owned);
    document = snapshot();
    g_assert_true(qdict_get_bool(document, "gpu_changed"));
    g_assert_false(qdict_get_bool(document, "complete"));
    qobject_unref(document);
    g_assert_true(xemu_shortcut_evidence_shutdown(&error_abort));
    cleanup();
}

static void test_disarmed_shutdown(void)
{
    if (!child()) {
        return;
    }
    uint64_t start = 17, count = 19;
    g_assert_false(xemu_shortcut_evidence_enabled());
    g_assert_false(xemu_shortcut_evidence_window(&start, &count));
    g_assert_cmpuint(start, ==, 17);
    g_assert_cmpuint(count, ==, 19);
    g_assert_true(xemu_shortcut_evidence_shutdown(&error_abort));
    g_assert_false(xemu_shortcut_evidence_enabled());
}

static void test_unpublished_profile_is_incomplete(void)
{
    if (!child()) {
        return;
    }
    init_service(true);
    Fixture f = fixture(12);
    f.published.start_profile.sequence = 0;
    f.published.end_profile.sequence = 0;
    register_source(&f, "vk", "vk.draw.attempts");
    QDict *document = snapshot();
    g_assert_false(qdict_get_bool(document, "complete"));
    g_assert_false(qdict_get_bool(document, "profiles_published"));
    qobject_unref(document);
    g_assert_true(xemu_shortcut_evidence_shutdown(&error_abort));
    cleanup();
}

static void test_owner_window(void)
{
    if (!child()) {
        return;
    }
    init_service(true);
    XemuShortcutWindow window = {0};
    XemuShortcutCounterDescriptor descriptors[] = {
        {"vk.draw.attempts", "draws"},
    };
    XemuTweakResolution profile = fixture(0).published.start_profile;
    g_assert_true(xemu_shortcut_window_init(&window, "vk", descriptors, 1, 1,
                                            &error_abort));
    xemu_shortcut_window_boundary(&window, 99, 900, &profile);
    xemu_shortcut_window_add(&window, 0, 999);
    g_assert_false(window.active);
    xemu_shortcut_window_boundary(&window, 100, 1000, &profile);
    g_assert_true(window.active);
    xemu_shortcut_window_add(&window, 0, 5);
    xemu_shortcut_window_boundary(&window, 101, 1100, &profile);
    QDict *document = snapshot();
    g_assert_false(qdict_get_bool(document, "complete"));
    g_assert_cmpuint(qdict_get_uint(qdict_get_qdict(document, "counters"),
                                    "vk.draw.attempts"),
                     ==, 5);
    qobject_unref(document);
    xemu_shortcut_window_add(&window, 0, 7);
    xemu_shortcut_window_boundary(&window, 120, 3000, &profile);
    g_assert_false(window.active);
    xemu_shortcut_window_add(&window, 0, 999);
    profile.sequence++;
    xemu_shortcut_window_boundary(&window, 121, 3100, &profile);
    g_assert_true(xemu_shortcut_window_destroy(&window, &error_abort));
    document = snapshot();
    g_assert_true(qdict_get_bool(document, "complete"));
    g_assert_cmpuint(qdict_get_uint(qdict_get_qdict(document, "counters"),
                                    "vk.draw.attempts"),
                     ==, 12);
    qobject_unref(document);
    /* A later global runtime publication cannot describe the frozen window. */
    xemu_shortcut_evidence_publish_vsync(1);
    document = snapshot();
    g_assert_false(qdict_get_bool(document, "complete"));
    g_assert_true(qdict_get_bool(document, "profile_changed"));
    qobject_unref(document);
    g_assert_true(xemu_shortcut_evidence_shutdown(&error_abort));
    cleanup();
}

static void test_owner_window_reset(void)
{
    if (!child()) {
        return;
    }
    init_service(true);
    XemuShortcutWindow window = {0};
    XemuShortcutCounterDescriptor descriptor = {"vk.draw.attempts", "draws"};
    XemuTweakResolution profile = fixture(0).published.start_profile;
    g_assert_true(xemu_shortcut_window_init(&window, "vk", &descriptor, 1, 1,
                                            &error_abort));
    xemu_shortcut_window_boundary(&window, 100, 1000, &profile);
    xemu_shortcut_window_add(&window, 0, 5);
    xemu_shortcut_window_boundary(&window, 99, 1100, &profile);
    g_assert_false(window.active);
    xemu_shortcut_window_boundary(&window, 120, 3000, &profile);
    g_assert_true(xemu_shortcut_window_destroy(&window, &error_abort));
    QDict *document = snapshot();
    g_assert_false(qdict_get_bool(document, "complete"));
    qobject_unref(document);
    g_assert_true(xemu_shortcut_evidence_shutdown(&error_abort));
    cleanup();
}

static void test_owner_window_invalidate(gconstpointer finalized)
{
    if (!child()) {
        return;
    }
    init_service(true);
    XemuShortcutCounterDescriptor descriptor = {"vk.draw.attempts", "draws"};
    XemuTweakResolution profile = fixture(0).published.start_profile;
    XemuShortcutWindow window = {0};
    g_assert_true(xemu_shortcut_window_init(&window, "vk", &descriptor, 1, 1,
                                           &error_abort));
    /* Setup changes before the captured context must not prevent capture. */
    xemu_shortcut_window_invalidate(&window);
    xemu_shortcut_window_boundary(&window, 100, 1000, &profile);
    xemu_shortcut_window_add(&window, 0, 5);
    if (finalized) {
        xemu_shortcut_window_boundary(&window, 120, 3000, &profile);
    }
    QDict *document = snapshot();
    g_assert_cmpint(qdict_get_bool(document, "complete"), ==,
                    finalized != NULL);
    qobject_unref(document);
    /* Reset/save/load must revoke even an already finalized capture. */
    xemu_shortcut_window_invalidate(&window);
    xemu_shortcut_window_boundary(&window, 120, 3000, &profile);
    document = snapshot();
    g_assert_false(qdict_get_bool(document, "complete"));
    g_assert_cmpuint(qdict_get_uint(qdict_get_qdict(document, "counters"),
                                   "vk.draw.attempts"), ==, 5);
    qobject_unref(document);
    g_assert_true(xemu_shortcut_window_destroy(&window, &error_abort));
    g_assert_true(xemu_shortcut_evidence_shutdown(&error_abort));
    cleanup();
}

static void test_owner_window_overflow(void)
{
    if (!child()) {
        return;
    }
    init_service(true);
    XemuShortcutWindow window = {0};
    XemuShortcutCounterDescriptor descriptor = {"vk.draw.attempts", "draws"};
    XemuTweakResolution profile = fixture(0).published.start_profile;
    g_assert_true(xemu_shortcut_window_init(&window, "vk", &descriptor, 1, 1,
                                            &error_abort));
    xemu_shortcut_window_boundary(&window, 100, 1000, &profile);
    xemu_shortcut_window_add(&window, 0, UINT64_MAX);
    xemu_shortcut_window_add(&window, 0, 1);
    xemu_shortcut_window_boundary(&window, 120, 3000, &profile);
    g_assert_true(xemu_shortcut_window_destroy(&window, &error_abort));
    QDict *document = snapshot();
    g_assert_false(qdict_get_bool(document, "complete"));
    g_assert_true(qdict_get_bool(document, "overflowed"));
    qobject_unref(document);
    g_assert_true(xemu_shortcut_evidence_shutdown(&error_abort));
    cleanup();
}

static void test_execution_publication(void)
{
    if (!child()) {
        return;
    }
    init_service(true);
    QDict *fields = qdict_new();
    qdict_put_str(fields, "route", "tcg");
    qdict_put_bool(fields, "hard_fpu", true);
    g_assert_true(
        xemu_shortcut_evidence_publish_execution("cpu", fields, &error_abort));
    uint64_t revision = xemu_shortcut_evidence_execution_revision();
    g_assert_cmpuint(revision, >, 0);
    g_assert_true(
        xemu_shortcut_evidence_publish_execution("cpu", fields, &error_abort));
    g_assert_cmpuint(xemu_shortcut_evidence_execution_revision(), ==, revision);
    qdict_put_str(fields, "route", "caller-mutated");
    QDict *document = snapshot();
    QDict *execution = qdict_get_qdict(document, "execution");
    g_assert_cmpstr(qdict_get_str(qdict_get_qdict(execution, "cpu"), "route"),
                    ==, "tcg");
    g_assert_false(qdict_haskey(execution, "audio"));
    qobject_unref(document);
    g_assert_true(
        xemu_shortcut_evidence_publish_execution("cpu", fields, &error_abort));
    g_assert_cmpuint(xemu_shortcut_evidence_execution_revision(), >, revision);
    qobject_unref(fields);
    g_assert_true(xemu_shortcut_evidence_shutdown(&error_abort));
    cleanup();
}

static void test_actual_dsp_transition(void)
{
    if (!child()) {
        return;
    }
    init_service(true);
    uint8_t state = 0;
    xemu_shortcut_evidence_publish_dsp(&state, true, true, true, true);
    uint64_t revision = xemu_shortcut_evidence_execution_revision();
    g_assert_cmpuint(revision, >, 0);
    xemu_shortcut_evidence_publish_dsp(&state, true, true, true, true);
    g_assert_cmpuint(xemu_shortcut_evidence_execution_revision(), ==, revision);
    XemuShortcutWindow window = {0};
    XemuShortcutCounterDescriptor counter = {"vk.draw.attempts", "draws"};
    XemuTweakResolution profile = fixture(0).published.start_profile;
    g_assert_true(
        xemu_shortcut_window_init(&window, "vk", &counter, 1, 1, &error_abort));
    xemu_shortcut_window_boundary(&window, 100, 1000, &profile);
    xemu_shortcut_evidence_publish_dsp(&state, false, true, true, true);
    g_assert_cmpuint(xemu_shortcut_evidence_execution_revision(), >, revision);
    xemu_shortcut_window_boundary(&window, 120, 2000, &profile);
    QDict *document = snapshot();
    g_assert_false(qdict_get_bool(document, "complete"));
    QDict *execution = qdict_get_qdict(document, "execution");
    g_assert_false(
        qdict_get_bool(qdict_get_qdict(execution, "dsp"), "gp_realtime"));
    qobject_unref(document);
    g_assert_true(xemu_shortcut_window_destroy(&window, &error_abort));
    g_assert_true(xemu_shortcut_evidence_shutdown(&error_abort));
    cleanup();
}

typedef struct PublicationRace {
    GMutex lock;
    GCond condition;
    bool entered;
    bool released;
} PublicationRace;

static void wait_for_shutdown(void *opaque)
{
    PublicationRace *race = opaque;
    g_mutex_lock(&race->lock);
    race->entered = true;
    g_cond_broadcast(&race->condition);
    while (!race->released) {
        g_cond_wait(&race->condition, &race->lock);
    }
    g_mutex_unlock(&race->lock);
}

static gpointer publish_during_shutdown(gpointer opaque)
{
    QDict *fields = qdict_new();
    qdict_put_str(fields, "route", "tcg");
    g_assert_true(
        xemu_shortcut_evidence_publish_execution("cpu", fields, &error_abort));
    qobject_unref(fields);
    return NULL;
}

static void test_publication_shutdown_race(void)
{
    if (!child()) {
        return;
    }
    init_service(true);
    PublicationRace race = {0};
    g_mutex_init(&race.lock);
    g_cond_init(&race.condition);
    xemu_shortcut_evidence_test_publication_hook(wait_for_shutdown, &race);
    GThread *thread = g_thread_new("publisher", publish_during_shutdown, NULL);
    g_mutex_lock(&race.lock);
    while (!race.entered) {
        g_cond_wait(&race.condition, &race.lock);
    }
    g_mutex_unlock(&race.lock);
    g_assert_true(xemu_shortcut_evidence_shutdown(&error_abort));
    g_mutex_lock(&race.lock);
    race.released = true;
    g_cond_broadcast(&race.condition);
    g_mutex_unlock(&race.lock);
    g_thread_join(thread);
    xemu_shortcut_evidence_test_publication_hook(NULL, NULL);
    g_cond_clear(&race.condition);
    g_mutex_clear(&race.lock);
    cleanup();
}

int main(int argc, char **argv)
{
    invoked_program = g_canonicalize_filename(argv[0], NULL);
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/xemu/shortcut-evidence/owned-full-width",
                    test_owned_full_width);
    g_test_add_func("/xemu/shortcut-evidence/merge-overflow",
                    test_merge_and_overflow);
    g_test_add_func("/xemu/shortcut-evidence/incomplete-reset",
                    test_incomplete_and_reset);
    g_test_add_func("/xemu/shortcut-evidence/unregister-lifetime",
                    test_unregister_waits_for_callback);
    g_test_add_func("/xemu/shortcut-evidence/output-failure",
                    test_explicit_output_failure);
    g_test_add_func("/xemu/shortcut-evidence/no-sources",
                    test_missing_sources_is_not_zero_evidence);
    g_test_add_func("/xemu/shortcut-evidence/backend-mismatch",
                    test_profile_backend_mismatch);
    g_test_add_func("/xemu/shortcut-evidence/capacity",
                    test_capacity_is_explicit);
    g_test_add_func("/xemu/shortcut-evidence/unit-conflict",
                    test_conflicting_units_are_not_added);
    g_test_add_func("/xemu/shortcut-evidence/window-profile",
                    test_window_and_profile_must_be_stable);
    g_test_add_func("/xemu/shortcut-evidence/snapshot-ownership",
                    test_snapshot_survives_owner_changes);
    g_test_add_func("/xemu/shortcut-evidence/disarmed", test_disarmed_shutdown);
    g_test_add_func("/xemu/shortcut-evidence/unpublished-profile",
                    test_unpublished_profile_is_incomplete);
    g_test_add_func("/xemu/shortcut-evidence/owner-window", test_owner_window);
    g_test_add_data_func("/xemu/shortcut-evidence/owner-window-invalidate-active",
                         NULL, test_owner_window_invalidate);
    g_test_add_data_func("/xemu/shortcut-evidence/owner-window-invalidate-final",
                         GINT_TO_POINTER(1), test_owner_window_invalidate);
    g_test_add_func("/xemu/shortcut-evidence/owner-reset",
                    test_owner_window_reset);
    g_test_add_func("/xemu/shortcut-evidence/owner-overflow",
                    test_owner_window_overflow);
    g_test_add_func("/xemu/shortcut-evidence/execution-publication",
                    test_execution_publication);
    g_test_add_func("/xemu/shortcut-evidence/actual-dsp-transition",
                    test_actual_dsp_transition);
    g_test_add_func("/xemu/shortcut-evidence/publication-shutdown-race",
                    test_publication_shutdown_race);
    int result = g_test_run();
    g_free(invoked_program);
    return result;
}

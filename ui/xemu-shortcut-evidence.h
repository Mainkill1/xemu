/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef UI_XEMU_SHORTCUT_EVIDENCE_H
#define UI_XEMU_SHORTCUT_EVIDENCE_H

#include <glib.h>
#include "qobject/qdict.h"
#include "ui/xemu-tweak-overrides.h"
#include "hw/xbox/nv2a/pgraph/vk/device-selection.h"

#ifdef __cplusplus
extern "C" {
#endif

#define XEMU_SHORTCUT_MAX_SOURCES 8
#define XEMU_SHORTCUT_MAX_COUNTERS 64

typedef struct XemuShortcutEvidenceOptions {
    char *output_path;
    char *session_id;
    char *workload;
    char *input_sha256;
    char *order_group;
    char *order;
    unsigned int position;
    uint64_t start_frame;
    uint64_t frame_count;
} XemuShortcutEvidenceOptions;

typedef struct XemuShortcutSessionIdentity {
    const char *commit;
    const char *version;
    const char *build_type;
    const char *platform;
    const char *requested_backend;
    const char *requested_gpu;
    const char *base_config_sha256;
    const char *comparison_config_sha256;
    const QDict *input_paths;
    const char *screenshot_directory;
    XemuTweakResolution initial_profile;
} XemuShortcutSessionIdentity;

typedef struct XemuShortcutCounterSnapshot {
    uint64_t values[XEMU_SHORTCUT_MAX_COUNTERS];
    bool complete;
    bool overflowed;
    uint64_t progress_incarnation;
    uint64_t start_frame;
    uint64_t end_frame;
    uint64_t start_monotonic_ns;
    uint64_t end_monotonic_ns;
    uint64_t start_execution_revision;
    uint64_t end_execution_revision;
    XemuTweakResolution start_profile;
    XemuTweakResolution end_profile;
} XemuShortcutCounterSnapshot;

typedef struct XemuShortcutCounterDescriptor {
    const char *name;
    const char *unit;
} XemuShortcutCounterDescriptor;

typedef struct XemuShortcutCounterSource {
    const char *id;
    const XemuShortcutCounterDescriptor *descriptors;
    size_t count;
    /* Copy only the owner's protected published numeric block. Never reenter
     * this registry or acquire a PGRAPH lock. Profile reasons are static. */
    void (*snapshot)(void *opaque, XemuShortcutCounterSnapshot *out);
    void *opaque;
} XemuShortcutCounterSource;

typedef struct XemuShortcutEvidenceSnapshot XemuShortcutEvidenceSnapshot;

/* Parse atomically: failure preserves argv/options/overrides. Strings are owned
 * by options, and the caller owns the returned typed override array. */
bool xemu_shortcut_evidence_parse_early(int argc, char **argv,
                                        XemuShortcutEvidenceOptions *options,
                                        GArray **overrides, Error **errp);
void xemu_shortcut_evidence_options_clear(XemuShortcutEvidenceOptions *options);
bool xemu_shortcut_evidence_init(const XemuShortcutEvidenceOptions *options,
                                 const XemuShortcutSessionIdentity *identity,
                                 Error **errp);
bool xemu_shortcut_evidence_enabled(void);
bool xemu_shortcut_evidence_window(uint64_t *start, uint64_t *count);
char *xemu_shortcut_evidence_executable_path(Error **errp);
void xemu_shortcut_evidence_publish_gpu(const char *backend,
                                        const PGRAPHVkDeviceRecord *device);
/* Low-frequency owner publication. Flat scalar fields are copied; missing
 * components remain unknown. A changed owner record advances the revision. */
bool xemu_shortcut_evidence_publish_execution(const char *component,
                                              const QDict *fields,
                                              Error **errp);
uint64_t xemu_shortcut_evidence_execution_revision(void);
void xemu_shortcut_evidence_publish_presentation(const char *transport,
                                                 const char *vendor,
                                                 const char *renderer);
void xemu_shortcut_evidence_publish_vsync(int interval);
/* The APU owner keeps a zero-initialized state byte. Unchanged actual state
 * takes no registry lock and allocates nothing. */
void xemu_shortcut_evidence_publish_dsp(uint8_t *owner_state, bool gp_realtime,
                                        bool ep_realtime, bool gp_jit,
                                        bool ep_jit);
uint64_t
xemu_shortcut_evidence_register_source(const XemuShortcutCounterSource *source,
                                       Error **errp);
bool xemu_shortcut_evidence_unregister_source(uint64_t handle, Error **errp);
XemuShortcutEvidenceSnapshot *xemu_shortcut_evidence_snapshot(Error **errp);
char *xemu_shortcut_evidence_snapshot_json(
    const XemuShortcutEvidenceSnapshot *snapshot);
void xemu_shortcut_evidence_snapshot_free(
    XemuShortcutEvidenceSnapshot *snapshot);
/* Emits once. Repeated calls return the original result without new callbacks
 * or writes. An explicit output error must become a failing application exit.
 */
bool xemu_shortcut_evidence_shutdown(Error **errp);

#ifdef XEMU_SHORTCUT_EVIDENCE_TEST
/* Test-only scheduling point; absent from production objects. */
void xemu_shortcut_evidence_test_publication_hook(void (*hook)(void *),
                                                  void *opaque);
#endif

#ifdef __cplusplus
}
#endif
#endif

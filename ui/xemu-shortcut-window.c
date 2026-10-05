/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "qemu/osdep.h"
#include "ui/xemu-shortcut-window.h"

static void copy_published(void *opaque, XemuShortcutCounterSnapshot *out)
{
    XemuShortcutWindow *window = opaque;
    g_mutex_lock(&window->mutex);
    *out = window->published;
    g_mutex_unlock(&window->mutex);
}

static void publish(XemuShortcutWindow *window)
{
    g_mutex_lock(&window->mutex);
    window->published = window->live;
    g_mutex_unlock(&window->mutex);
}

bool xemu_shortcut_window_init(XemuShortcutWindow *window, const char *id,
                               const XemuShortcutCounterDescriptor *descriptors,
                               size_t count, uint64_t incarnation, Error **errp)
{
    if (window->handle || !incarnation || !count ||
        count > XEMU_SHORTCUT_MAX_COUNTERS) {
        error_setg(errp, "Invalid or already owned shortcut counter window");
        return false;
    }
    uint64_t frames;
    if (!xemu_shortcut_evidence_window(&window->first, &frames)) {
        return true;
    }
    window->last = window->first + frames;
    window->count = count;
    window->live.progress_incarnation = incarnation;
    window->published = window->live;
    g_mutex_init(&window->mutex);
    XemuShortcutCounterSource source = {
        .id = id,
        .descriptors = descriptors,
        .count = count,
        .snapshot = copy_published,
        .opaque = window,
    };
    window->handle = xemu_shortcut_evidence_register_source(&source, errp);
    if (!window->handle) {
        g_mutex_clear(&window->mutex);
        return false;
    }
    return true;
}

void xemu_shortcut_window_boundary(XemuShortcutWindow *window, uint64_t frame,
                                   uint64_t ns,
                                   const XemuTweakResolution *profile)
{
    if (!window->handle || window->finished) {
        return;
    }
    if (!ns || !profile->sequence ||
        (window->have_previous &&
         (frame <= window->previous_frame || ns <= window->previous_ns))) {
        window->invalid = true;
        window->finished = true;
        window->active = false;
    }
    window->previous_frame = frame;
    window->previous_ns = ns;
    window->have_previous = true;
    if (!window->finished && !window->started && frame >= window->first) {
        window->started = true;
        window->live.start_frame = frame;
        window->live.start_monotonic_ns = ns;
        window->live.start_profile = *profile;
        window->live.start_execution_revision =
            xemu_shortcut_evidence_execution_revision();
        window->invalid |= frame != window->first;
        window->active = !window->invalid;
    }
    if (window->started) {
        window->live.end_frame = frame;
        window->live.end_monotonic_ns = ns;
        window->live.end_profile = *profile;
        window->live.end_execution_revision =
            xemu_shortcut_evidence_execution_revision();
        window->invalid |=
            profile->sequence != window->live.start_profile.sequence;
        window->invalid |= window->live.start_execution_revision !=
                           window->live.end_execution_revision;
        if (frame >= window->last || window->invalid) {
            window->finished = true;
            window->active = false;
            window->live.complete = frame == window->last && !window->invalid;
        }
    }
    publish(window);
}

void xemu_shortcut_window_add(XemuShortcutWindow *window, size_t counter,
                              uint64_t amount)
{
    if (!window->active) {
        return;
    }
    if (counter >= window->count) {
        window->invalid = true;
        return;
    }
    uint64_t *value = &window->live.values[counter];
    if (amount > UINT64_MAX - *value) {
        *value = UINT64_MAX;
        window->live.overflowed = true;
        window->invalid = true;
    } else {
        *value += amount;
    }
}

bool xemu_shortcut_window_destroy(XemuShortcutWindow *window, Error **errp)
{
    if (!window->handle) {
        return true;
    }
    window->active = false;
    window->finished = true;
    publish(window);
    /* No owner publication lock may be held while unregistering. */
    bool result =
        xemu_shortcut_evidence_unregister_source(window->handle, errp);
    if (result) {
        window->handle = 0;
        g_mutex_clear(&window->mutex);
    }
    return result;
}

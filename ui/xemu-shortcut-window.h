/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef UI_XEMU_SHORTCUT_WINDOW_H
#define UI_XEMU_SHORTCUT_WINDOW_H

#include "ui/xemu-shortcut-evidence.h"

/* A fresh zero-initialized window has one renderer-thread owner. Observers
 * copy only the published block under its lock. Registration and retirement
 * must not hold that publication lock; registry callbacks never take renderer
 * locks. A destroyed window must be zeroed before reuse. */
typedef struct XemuShortcutWindow {
    GMutex mutex;
    XemuShortcutCounterSnapshot live;
    XemuShortcutCounterSnapshot published;
    uint64_t handle;
    uint64_t first;
    uint64_t last;
    uint64_t previous_frame;
    uint64_t previous_ns;
    size_t count;
    bool active;
    bool started;
    bool finished;
    bool invalid;
    bool have_previous;
} XemuShortcutWindow;

bool xemu_shortcut_window_init(XemuShortcutWindow *window, const char *id,
                               const XemuShortcutCounterDescriptor *descriptors,
                               size_t count, uint64_t incarnation,
                               Error **errp);
void xemu_shortcut_window_boundary(XemuShortcutWindow *window, uint64_t frame,
                                   uint64_t monotonic_ns,
                                   const XemuTweakResolution *profile);
void xemu_shortcut_window_add(XemuShortcutWindow *window, size_t counter,
                              uint64_t amount);
bool xemu_shortcut_window_destroy(XemuShortcutWindow *window, Error **errp);

#endif

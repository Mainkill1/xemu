/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef XEMU_SETTINGS_MENU_H
#define XEMU_SETTINGS_MENU_H

/* Persisted layout version 1 inserts DLSS after Display. */
enum {
    XEMU_SETTINGS_TAB_GENERAL,
    XEMU_SETTINGS_TAB_INPUT,
    XEMU_SETTINGS_TAB_DISPLAY,
    XEMU_SETTINGS_TAB_DLSS,
    XEMU_SETTINGS_TAB_AUDIO,
    XEMU_SETTINGS_TAB_NETWORK,
    XEMU_SETTINGS_TAB_SNAPSHOTS,
    XEMU_SETTINGS_TAB_SYSTEM,
    XEMU_SETTINGS_TAB_ADVANCED,
    XEMU_SETTINGS_TAB_ABOUT,
    XEMU_SETTINGS_TAB_COUNT,
    XEMU_SETTINGS_MENU_VERSION = 1,
};

static inline int xemu_settings_menu_migrate_index(int index, int version)
{
    if (index < 0 || index >= (version < 1 ? 9 : XEMU_SETTINGS_TAB_COUNT)) {
        return XEMU_SETTINGS_TAB_GENERAL;
    }
    return version < 1 && index >= XEMU_SETTINGS_TAB_DLSS ? index + 1 : index;
}
#endif

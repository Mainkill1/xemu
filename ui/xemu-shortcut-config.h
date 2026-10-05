/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef UI_XEMU_SHORTCUT_CONFIG_H
#define UI_XEMU_SHORTCUT_CONFIG_H

#ifdef __cplusplus
extern "C" {
#endif

/* Startup/UI thread only, before workers. Owns its returned string and does not
 * update ConfigTree, save settings, or include the process override overlay. */
char *xemu_shortcut_base_config_sha256(void);

#ifdef __cplusplus
}
#endif
#endif

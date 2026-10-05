/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef UI_XEMU_SHORTCUT_CONFIG_H
#define UI_XEMU_SHORTCUT_CONFIG_H

#ifdef __cplusplus
extern "C" {
#endif

#include "qobject/qdict.h"

/* Startup/UI thread only, before workers. Owns its returned string and does not
 * update ConfigTree, save settings, or include the process override overlay. */
char *xemu_shortcut_base_config_sha256(void);
/* Keeps bound/unbound roles but excludes per-run resource pathnames. Admission
 * must separately bind each path to verified runner input contents. */
char *xemu_shortcut_comparison_config_sha256(const QDict *input_paths);
/* Rejects unresolved or unsupported resource routes in evidence mode. */
QDict *xemu_shortcut_input_paths(int argc, char **argv, Error **errp);

#ifdef __cplusplus
}
#endif
#endif

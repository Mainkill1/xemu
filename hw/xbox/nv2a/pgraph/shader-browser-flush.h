/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef HW_XBOX_NV2A_PGRAPH_SHADER_BROWSER_FLUSH_H
#define HW_XBOX_NV2A_PGRAPH_SHADER_BROWSER_FLUSH_H

#ifdef __cplusplus
extern "C" {
#endif

/* Drain renderer-owned observations, change the UI session/scope while PGRAPH
 * is serialized, then refresh collection state before drawing resumes. */
void pgraph_shader_browser_transition(void (*change)(void *), void *opaque);

#ifdef __cplusplus
}
#endif

#endif

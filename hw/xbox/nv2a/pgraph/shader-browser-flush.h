/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef HW_XBOX_NV2A_PGRAPH_SHADER_BROWSER_FLUSH_H
#define HW_XBOX_NV2A_PGRAPH_SHADER_BROWSER_FLUSH_H

#ifdef __cplusplus
extern "C" {
#endif

/* Drain renderer-owned observations, change the UI session/scope while PGRAPH
 * is serialized, then refresh collection state before drawing resumes. */
void pgraph_shader_browser_transition(void (*change)(void *), void *opaque);

/* Wake the PFIFO owner so a selected-detail request is serviced even while
 * the guest is paused and no new commands arrive. */
void pgraph_shader_browser_request_details(void);

#ifdef __cplusplus
}
#endif

#endif

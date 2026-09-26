/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef HW_XBOX_NV2A_PGRAPH_SHADER_BROWSER_FLUSH_H
#define HW_XBOX_NV2A_PGRAPH_SHADER_BROWSER_FLUSH_H

#ifdef __cplusplus
extern "C" {
#endif

/* Drain renderer-owned observations before a UI performance session or title
 * scope ends. Safe to call from the UI thread while the renderer is active. */
void pgraph_shader_browser_flush_pending(void);

#ifdef __cplusplus
}
#endif

#endif

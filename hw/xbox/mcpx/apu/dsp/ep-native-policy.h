/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef XEMU_EP_NATIVE_POLICY_H
#define XEMU_EP_NATIVE_POLICY_H

#include <stdbool.h>

/* The experimental path is limited to fully prepared, identified EP frames.
 * Recording keeps the DSP authoritative so ordered transactions are captured.
 */
static inline bool ep_native_can_execute(bool requested, bool realtime,
                                         bool identified, bool entry_ok,
                                         bool projection_ok, bool fifo_ok,
                                         bool recording)
{
    return requested && realtime && identified && entry_ok && projection_ok &&
           fifo_ok && !recording;
}
#endif

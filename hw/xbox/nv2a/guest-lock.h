/* Guest-visible NV2A lock waits. SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef HW_XBOX_NV2A_GUEST_LOCK_H
#define HW_XBOX_NV2A_GUEST_LOCK_H

#include "qemu/thread.h"

void nv2a_guest_mmio_lock(QemuMutex *lock, const char *name);

#endif

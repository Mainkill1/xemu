/*
 * Keep unrelated NV2A device registration out of report integration units.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef XBOX_NV2A_REPORT_TEST_SHIM_H
#define XBOX_NV2A_REPORT_TEST_SHIM_H

/* Import the normal module definitions before overriding only this root. */
#include "qemu/module.h"

#undef type_init
#define type_init(function)

#endif

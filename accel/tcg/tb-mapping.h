/*
 * Translation-block virtual-to-host mapping validation.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef ACCEL_TCG_TB_MAPPING_H
#define ACCEL_TCG_TB_MAPPING_H

#include "exec/target_page.h"
#include "exec/tlb-flags.h"

static inline bool tb_code_mapping_matches(vaddr pc, uintptr_t addr_code,
                                           uintptr_t addend,
                                           uintptr_t expected_addend)
{
    vaddr page = pc & TARGET_PAGE_MASK;
    uintptr_t compare_mask = TARGET_PAGE_MASK | TLB_INVALID_MASK;

    return (addr_code & compare_mask) == page && addend == expected_addend;
}

#endif /* ACCEL_TCG_TB_MAPPING_H */

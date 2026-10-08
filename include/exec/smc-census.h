/* Diagnostic-only PR #317 reachability probe. */
#ifndef EXEC_SMC_CENSUS_H
#define EXEC_SMC_CENSUS_H

enum {
    SMC_CALLS,
    SMC_RESOLVED,
    SMC_FIRST_PAGE,
    SMC_SECOND_PAGE,
    SMC_CURRENT_ENTRIES,
    SMC_OVERLAP_ENTRIES,
    SMC_DISJOINT_ENTRIES,
    SMC_DISJOINT_RESTARTS,
    SMC_RESTARTS,
    SMC_COUNTER_COUNT,
};

void xemu_smc_census_snapshot(uint64_t values[SMC_COUNTER_COUNT]);
#endif

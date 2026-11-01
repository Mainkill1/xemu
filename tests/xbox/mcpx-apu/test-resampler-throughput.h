/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef TEST_MCPX_APU_RESAMPLER_THROUGHPUT_H
#define TEST_MCPX_APU_RESAMPLER_THROUGHPUT_H

#include <pthread.h>

int __real_pthread_cond_broadcast(pthread_cond_t *cond);
int __wrap_pthread_cond_broadcast(pthread_cond_t *cond);

#endif

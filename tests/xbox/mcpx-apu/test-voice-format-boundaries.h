/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef TEST_MCPX_APU_VOICE_FORMAT_BOUNDARIES_H
#define TEST_MCPX_APU_VOICE_FORMAT_BOUNDARIES_H

#include <pthread.h>

int __real_pthread_mutex_lock(pthread_mutex_t *mutex);
int __wrap_pthread_mutex_lock(pthread_mutex_t *mutex);

#endif

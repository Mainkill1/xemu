/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef TEST_MCPX_APU_VOICE_FORMAT_BOUNDARIES_H
#define TEST_MCPX_APU_VOICE_FORMAT_BOUNDARIES_H

#include <pthread.h>
#include <samplerate.h>

int __real_pthread_mutex_lock(pthread_mutex_t *mutex);
int __wrap_pthread_mutex_lock(pthread_mutex_t *mutex);
SRC_STATE *__real_src_callback_new(src_callback_t callback, int type,
                                   int channels, int *error, void *opaque);
SRC_STATE *__wrap_src_callback_new(src_callback_t callback, int type,
                                   int channels, int *error, void *opaque);

#endif

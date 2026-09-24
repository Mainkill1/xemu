/*
 * NV2A Vulkan hybrid compiler queue
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/atomic.h"
#include "qemu/thread.h"

#include "hw/xbox/nv2a/pgraph/vk/hybrid-compiler.h"

typedef struct HybridCompilerJob {
    struct HybridCompilerJob *next;
    PGRAPHVkHybridCompileRequest request;
    uint8_t *glsl;
    uint8_t *recipe;
    uint8_t *config;
    size_t bytes;
    bool blocking;
    bool done;
    bool cancelled;
    bool success;
    uint8_t *spirv;
    size_t spirv_size;
    size_t generated_glsl_size;
    uint64_t submitted_us;
    uint64_t started_us;
    uint64_t finished_us;
    bool speculative_active_at_start;
} HybridCompilerJob;

typedef struct HybridCompilerState HybridCompilerState;

typedef struct HybridCompilerLane {
    HybridCompilerState *state;
    QemuThread thread;
    HybridCompilerJob *active;
    PGRAPHVkHybridWorkerClass worker_class;
    PGRAPHVkHybridWorkerStatus status;
} HybridCompilerLane;

struct HybridCompilerState {
    QemuMutex lock;
    QemuCond work_ready;
    QemuCond blocking_ready;
    QemuCond blocking_done;
    QemuThread blocking_worker;
    HybridCompilerLane async_lanes[2];
    size_t async_lane_count;
    PGRAPHVkHybridCompilerConfig config;
    HybridCompilerJob *async_head;
    HybridCompilerJob *async_tail;
    HybridCompilerJob *result_head;
    HybridCompilerJob *result_tail;
    HybridCompilerJob *active_blocking;
    HybridCompilerJob *blocking;
    size_t async_jobs;
    size_t async_bytes;
    int result_available;
    bool stopping;
    bool joined;
};

static bool request_valid(const PGRAPHVkHybridCompileRequest *request)
{
    size_t input_size;

    if (!request || !request->ticket ||
        request->kind < PGRAPH_VK_HYBRID_JOB_COMPILE_SOURCE ||
        request->kind > PGRAPH_VK_HYBRID_JOB_GENERATE_SOURCE ||
        request->urgency < PGRAPH_VK_COMPILE_SPECULATIVE ||
        request->urgency > PGRAPH_VK_COMPILE_DEMAND ||
        (request->config_size && !request->config)) {
        return false;
    }
    if (request->kind == PGRAPH_VK_HYBRID_JOB_COMPILE_SOURCE) {
        if (!request->glsl || !request->glsl_size || request->recipe ||
            request->recipe_size) {
            return false;
        }
        input_size = request->glsl_size;
    } else {
        if (!request->recipe || !request->recipe_size || request->glsl ||
            request->glsl_size) {
            return false;
        }
        input_size = request->recipe_size;
    }
    return input_size <= SIZE_MAX - request->config_size;
}

static HybridCompilerJob *job_new(const PGRAPHVkHybridCompileRequest *request,
                                  bool blocking)
{
    HybridCompilerJob *job;

    if (!request_valid(request)) {
        return NULL;
    }
    job = g_new0(HybridCompilerJob, 1);
    if (request->glsl_size) {
        job->glsl = g_memdup2(request->glsl, request->glsl_size);
    }
    if (request->recipe_size) {
        job->recipe = g_memdup2(request->recipe, request->recipe_size);
    }
    if (request->config_size) {
        job->config = g_memdup2(request->config, request->config_size);
    }
    if ((request->glsl_size && !job->glsl) ||
        (request->recipe_size && !job->recipe) ||
        (request->config_size && !job->config)) {
        g_free(job->glsl);
        g_free(job->recipe);
        g_free(job->config);
        g_free(job);
        return NULL;
    }
    job->request = *request;
    job->request.glsl = job->glsl;
    job->request.recipe = job->recipe;
    job->request.config = job->config;
    job->bytes = request->glsl_size + request->recipe_size +
                 request->config_size;
    job->blocking = blocking;
    job->submitted_us = g_get_monotonic_time();
    return job;
}

static void job_destroy(HybridCompilerJob *job)
{
    if (!job) {
        return;
    }
    g_free(job->spirv);
    g_free(job->config);
    g_free(job->recipe);
    g_free(job->glsl);
    g_free(job);
}

static bool job_matches_request(const HybridCompilerJob *job,
                                const PGRAPHVkHybridCompileRequest *request)
{
    const void *job_input = job->request.kind ==
                                PGRAPH_VK_HYBRID_JOB_COMPILE_SOURCE ?
                            (const void *)job->glsl : job->recipe;
    const void *request_input = request->kind ==
                                    PGRAPH_VK_HYBRID_JOB_COMPILE_SOURCE ?
                                request->glsl : request->recipe;
    size_t input_size = request->kind ==
                            PGRAPH_VK_HYBRID_JOB_COMPILE_SOURCE ?
                        request->glsl_size : request->recipe_size;

    return job->request.kind == request->kind &&
           job->request.stage == request->stage &&
           job->request.glsl_size == request->glsl_size &&
           job->request.recipe_size == request->recipe_size &&
           job->request.config_size == request->config_size &&
           !memcmp(job_input, request_input, input_size) &&
           (!request->config_size ||
            !memcmp(job->config, request->config, request->config_size));
}

static HybridCompilerJob *find_in_list(
    HybridCompilerJob *head, const PGRAPHVkHybridCompileRequest *request)
{
    for (HybridCompilerJob *job = head; job; job = job->next) {
        if (job_matches_request(job, request)) {
            return job;
        }
    }
    return NULL;
}

static HybridCompilerJob *find_matching_async_job(
    HybridCompilerState *state, const PGRAPHVkHybridCompileRequest *request,
    bool *queued)
{
    *queued = false;
    for (size_t i = 0; i < state->async_lane_count; i++) {
        HybridCompilerJob *active = state->async_lanes[i].active;
        if (active && job_matches_request(active, request)) {
            return active;
        }
    }
    HybridCompilerJob *job = find_in_list(state->async_head, request);
    if (job) {
        *queued = true;
        return job;
    }
    return find_in_list(state->result_head, request);
}

static bool async_has_capacity(const HybridCompilerState *state,
                               size_t bytes)
{
    return !state->stopping &&
           state->async_jobs < state->config.max_async_jobs &&
           bytes <= state->config.max_async_bytes - state->async_bytes;
}

static void async_account_release(HybridCompilerState *state,
                                  HybridCompilerJob *job)
{
    assert(!job->blocking);
    assert(state->async_jobs);
    assert(state->async_bytes >= job->bytes);
    state->async_jobs--;
    state->async_bytes -= job->bytes;
}

static void async_list_destroy(HybridCompilerState *state,
                               HybridCompilerJob **head,
                               HybridCompilerJob **tail)
{
    HybridCompilerJob *job = *head;

    while (job) {
        HybridCompilerJob *next = job->next;
        async_account_release(state, job);
        job_destroy(job);
        job = next;
    }
    *head = NULL;
    *tail = NULL;
}

static bool lane_accepts_job(const HybridCompilerLane *lane,
                             const HybridCompilerJob *job)
{
    if (lane->state->config.worker_policy !=
        PGRAPH_VK_HYBRID_WORKERS_SPLIT_DEMAND) {
        return true;
    }
    return lane->worker_class == PGRAPH_VK_HYBRID_WORKER_NORMAL ?
               job->request.urgency == PGRAPH_VK_COMPILE_DEMAND :
               job->request.urgency != PGRAPH_VK_COMPILE_DEMAND;
}

static bool async_has_work_for_lane(const HybridCompilerLane *lane)
{
    for (HybridCompilerJob *job = lane->state->async_head; job;
         job = job->next) {
        if (lane_accepts_job(lane, job)) {
            return true;
        }
    }
    return false;
}

static bool async_has_active_job(const HybridCompilerState *state)
{
    for (size_t i = 0; i < state->async_lane_count; i++) {
        if (state->async_lanes[i].active) {
            return true;
        }
    }
    return false;
}

static HybridCompilerJob *async_pop(HybridCompilerLane *lane)
{
    HybridCompilerState *state = lane->state;
    HybridCompilerJob *job = state->async_head;
    HybridCompilerJob *previous = NULL;
    HybridCompilerJob *best = NULL;
    HybridCompilerJob *best_previous = NULL;

    for (; job; previous = job, job = job->next) {
        if (!lane_accepts_job(lane, job)) {
            continue;
        }
        if (!best || job->request.urgency > best->request.urgency) {
            best = job;
            best_previous = previous;
        }
    }
    if (!best) {
        return NULL;
    }
    if (best_previous) {
        best_previous->next = best->next;
    } else {
        state->async_head = best->next;
    }
    if (state->async_tail == best) {
        state->async_tail = best_previous;
    }
    if (!state->async_head) {
        state->async_tail = NULL;
    }
    best->next = NULL;
    return best;
}

static void result_append(HybridCompilerState *state, HybridCompilerJob *job)
{
    assert(!job->next);
    if (state->result_tail) {
        state->result_tail->next = job;
    } else {
        state->result_head = job;
    }
    state->result_tail = job;
    qatomic_set(&state->result_available, true);
}

static void *hybrid_compiler_worker(void *opaque)
{
    HybridCompilerLane *lane = opaque;
    HybridCompilerState *state = lane->state;

    PGRAPHVkWorkerPriorityResult priority_result =
        PGRAPH_VK_WORKER_PRIORITY_UNSUPPORTED;
    bool lower_priority =
        lane->worker_class == PGRAPH_VK_HYBRID_WORKER_BACKGROUND;
    if (lower_priority && state->config.set_lower_priority) {
        priority_result = state->config.set_lower_priority(
            state->config.priority_opaque);
    }

    qemu_mutex_lock(&state->lock);
    lane->status = (PGRAPHVkHybridWorkerStatus) {
        .started = true,
        .lower_priority_requested = lower_priority,
        .priority_result = priority_result,
    };
    qemu_cond_broadcast(&state->work_ready);
    qemu_mutex_unlock(&state->lock);

    for (;;) {
        HybridCompilerJob *job;
        uint8_t *artifact = NULL;
        size_t artifact_size = 0;
        bool success;

        qemu_mutex_lock(&state->lock);
        while (!state->stopping && !async_has_work_for_lane(lane)) {
            qemu_cond_wait(&state->work_ready, &state->lock);
        }
        if (state->stopping) {
            qemu_mutex_unlock(&state->lock);
            break;
        }
        job = async_pop(lane);
        assert(job);
        lane->active = job;
        job->started_us = g_get_monotonic_time();
        qemu_mutex_unlock(&state->lock);

        if (job->request.kind == PGRAPH_VK_HYBRID_JOB_GENERATE_SOURCE) {
            success = state->config.generate(
                state->config.opaque, &job->request, &artifact,
                &artifact_size);
        } else {
            success = state->config.compile(
                state->config.opaque, &job->request, &artifact,
                &artifact_size);
        }
        if (!success || !artifact || !artifact_size ||
            (job->request.kind == PGRAPH_VK_HYBRID_JOB_GENERATE_SOURCE &&
             artifact_size > state->config.max_async_bytes)) {
            g_free(artifact);
            artifact = NULL;
            artifact_size = 0;
            success = false;
        }

        qemu_mutex_lock(&state->lock);
        job->finished_us = g_get_monotonic_time();
        lane->active = NULL;
        if (success &&
            job->request.kind == PGRAPH_VK_HYBRID_JOB_GENERATE_SOURCE) {
            if (artifact_size > state->config.max_async_bytes -
                                    state->async_bytes) {
                g_free(artifact);
                artifact = NULL;
                artifact_size = 0;
                success = false;
            } else {
                state->async_bytes += artifact_size;
                job->bytes += artifact_size;
            }
        }
        job->success = success;
        if (job->request.kind == PGRAPH_VK_HYBRID_JOB_GENERATE_SOURCE) {
            job->glsl = artifact;
            job->generated_glsl_size = artifact_size;
        } else {
            job->spirv = artifact;
            job->spirv_size = artifact_size;
        }
        if (state->stopping) {
            async_account_release(state, job);
            job_destroy(job);
        } else {
            result_append(state, job);
        }
        bool stopping = state->stopping;
        qemu_mutex_unlock(&state->lock);
        if (stopping) {
            break;
        }
    }

    return NULL;
}

/* Required draw dependencies must not wait for an active speculative
 * compilation. This lane has no asynchronous work and is independent of
 * the bounded background queue. */
static void *hybrid_compiler_blocking_worker(void *opaque)
{
    HybridCompilerState *state = opaque;

    for (;;) {
        HybridCompilerJob *job;
        uint8_t *spirv = NULL;
        size_t spirv_size = 0;
        bool success;

        qemu_mutex_lock(&state->lock);
        while (!state->stopping && !state->blocking) {
            qemu_cond_wait(&state->blocking_ready, &state->lock);
        }
        if (state->stopping) {
            qemu_mutex_unlock(&state->lock);
            break;
        }
        job = state->blocking;
        state->active_blocking = job;
        job->started_us = g_get_monotonic_time();
        job->speculative_active_at_start = async_has_active_job(state);
        qemu_mutex_unlock(&state->lock);

        success = state->config.compile(state->config.opaque, &job->request,
                                        &spirv, &spirv_size);
        if (!success || !spirv || !spirv_size) {
            g_free(spirv);
            spirv = NULL;
            spirv_size = 0;
            success = false;
        }

        qemu_mutex_lock(&state->lock);
        job->finished_us = g_get_monotonic_time();
        state->active_blocking = NULL;
        state->blocking = NULL;
        job->success = success;
        job->spirv = spirv;
        job->spirv_size = spirv_size;
        job->done = true;
        qemu_cond_broadcast(&state->blocking_done);
        bool stopping = state->stopping;
        qemu_mutex_unlock(&state->lock);
        if (stopping) {
            break;
        }
    }

    return NULL;
}

bool pgraph_vk_hybrid_compiler_init(
    PGRAPHVkHybridCompiler *compiler,
    const PGRAPHVkHybridCompilerConfig *config)
{
    HybridCompilerState *state;

    if (!compiler || compiler->state || !config || !config->max_async_jobs ||
        !config->max_async_bytes || !config->compile) {
        return false;
    }

    state = g_new0(HybridCompilerState, 1);
    state->config = *config;
    qemu_mutex_init(&state->lock);
    qemu_cond_init(&state->work_ready);
    qemu_cond_init(&state->blocking_ready);
    qemu_cond_init(&state->blocking_done);
    if (config->worker_policy > PGRAPH_VK_HYBRID_WORKERS_SPLIT_DEMAND) {
        qemu_cond_destroy(&state->blocking_done);
        qemu_cond_destroy(&state->blocking_ready);
        qemu_cond_destroy(&state->work_ready);
        qemu_mutex_destroy(&state->lock);
        g_free(state);
        return false;
    }
    state->async_lane_count =
        config->worker_policy == PGRAPH_VK_HYBRID_WORKERS_SPLIT_DEMAND ? 2 : 1;
    for (size_t i = 0; i < state->async_lane_count; i++) {
        HybridCompilerLane *lane = &state->async_lanes[i];
        const char *name;

        lane->state = state;
        if ((config->worker_policy == PGRAPH_VK_HYBRID_WORKERS_SPLIT_DEMAND &&
             i == 0) ||
            config->worker_policy == PGRAPH_VK_HYBRID_WORKERS_ALL_LOW) {
            lane->worker_class = PGRAPH_VK_HYBRID_WORKER_BACKGROUND;
            name = "vk-hybrid-background";
        } else {
            lane->worker_class = PGRAPH_VK_HYBRID_WORKER_NORMAL;
            name = config->worker_policy ==
                           PGRAPH_VK_HYBRID_WORKERS_SPLIT_DEMAND ?
                       "vk-hybrid-demand" : "vk-hybrid-compiler";
        }
        qemu_thread_create(&lane->thread, name, hybrid_compiler_worker, lane,
                           QEMU_THREAD_JOINABLE);
    }
    qemu_mutex_lock(&state->lock);
    for (;;) {
        bool all_started = true;
        for (size_t i = 0; i < state->async_lane_count; i++) {
            all_started &= state->async_lanes[i].status.started;
        }
        if (all_started) {
            break;
        }
        qemu_cond_wait(&state->work_ready, &state->lock);
    }
    qemu_mutex_unlock(&state->lock);
    qemu_thread_create(&state->blocking_worker, "vk-hybrid-required",
                       hybrid_compiler_blocking_worker, state,
                       QEMU_THREAD_JOINABLE);
    compiler->state = state;
    return true;
}

bool pgraph_vk_hybrid_compiler_can_submit_async(
    PGRAPHVkHybridCompiler *compiler, size_t glsl_size, size_t config_size)
{
    HybridCompilerState *state = compiler ? compiler->state : NULL;
    bool result;

    if (!state || !glsl_size || glsl_size > SIZE_MAX - config_size) {
        return false;
    }
    qemu_mutex_lock(&state->lock);
    result = async_has_capacity(state, glsl_size + config_size);
    qemu_mutex_unlock(&state->lock);
    return result;
}

PGRAPHVkHybridCompilerSubmitResult pgraph_vk_hybrid_compiler_submit_async(
    PGRAPHVkHybridCompiler *compiler,
    const PGRAPHVkHybridCompileRequest *request,
    PGRAPHVkHybridCompileIdentity *owner)
{
    HybridCompilerState *state = compiler ? compiler->state : NULL;
    HybridCompilerJob *job;
    HybridCompilerJob *existing;
    PGRAPHVkHybridCompilerSubmitResult result;
    bool queued;

    if (owner) {
        *owner = (PGRAPHVkHybridCompileIdentity) { 0 };
    }
    if (!state || !request_valid(request) ||
        (request->kind == PGRAPH_VK_HYBRID_JOB_GENERATE_SOURCE &&
         !state->config.generate)) {
        return PGRAPH_VK_HYBRID_COMPILER_INVALID;
    }
    job = job_new(request, false);
    if (!job) {
        return PGRAPH_VK_HYBRID_COMPILER_INVALID;
    }

    qemu_mutex_lock(&state->lock);
    if (state->stopping) {
        result = PGRAPH_VK_HYBRID_COMPILER_STOPPED;
    } else if ((existing = find_matching_async_job(state, request,
                                                    &queued))) {
        if (queued && request->urgency > existing->request.urgency) {
            existing->request.urgency = request->urgency;
            result = PGRAPH_VK_HYBRID_COMPILER_DUPLICATE_PROMOTED;
            qemu_cond_broadcast(&state->work_ready);
        } else {
            result = PGRAPH_VK_HYBRID_COMPILER_DUPLICATE;
        }
        if (owner) {
            *owner = (PGRAPHVkHybridCompileIdentity) {
                .generation = existing->request.generation,
                .ticket = existing->request.ticket,
            };
        }
    } else if (state->async_jobs == state->config.max_async_jobs) {
        result = PGRAPH_VK_HYBRID_COMPILER_QUEUE_FULL;
    } else if (job->bytes > state->config.max_async_bytes -
               state->async_bytes) {
        result = PGRAPH_VK_HYBRID_COMPILER_BYTE_LIMIT;
    } else {
        if (state->async_tail) {
            state->async_tail->next = job;
        } else {
            state->async_head = job;
        }
        state->async_tail = job;
        state->async_jobs++;
        state->async_bytes += job->bytes;
        if (owner) {
            *owner = (PGRAPHVkHybridCompileIdentity) {
                .generation = request->generation,
                .ticket = request->ticket,
            };
        }
        qemu_cond_broadcast(&state->work_ready);
        qemu_mutex_unlock(&state->lock);
        return PGRAPH_VK_HYBRID_COMPILER_ACCEPTED;
    }
    qemu_mutex_unlock(&state->lock);
    job_destroy(job);
    return result;
}

bool pgraph_vk_hybrid_compiler_submit_blocking(
    PGRAPHVkHybridCompiler *compiler,
    const PGRAPHVkHybridCompileRequest *request,
    PGRAPHVkHybridCompileResult *result)
{
    HybridCompilerState *state = compiler ? compiler->state : NULL;
    HybridCompilerJob *job;
    bool success;

    if (result) {
        *result = (PGRAPHVkHybridCompileResult) { 0 };
    }
    if (!state || !result || !request_valid(request) ||
        request->kind != PGRAPH_VK_HYBRID_JOB_COMPILE_SOURCE) {
        return false;
    }
    job = job_new(request, true);
    if (!job) {
        return false;
    }

    qemu_mutex_lock(&state->lock);
    while (!state->stopping && state->blocking) {
        qemu_cond_wait(&state->blocking_done, &state->lock);
    }
    if (state->stopping) {
        qemu_mutex_unlock(&state->lock);
        job_destroy(job);
        return false;
    }
    state->blocking = job;
    qemu_cond_signal(&state->blocking_ready);
    while (!job->done) {
        qemu_cond_wait(&state->blocking_done, &state->lock);
    }
    success = !job->cancelled && !state->stopping;
    if (success) {
        *result = (PGRAPHVkHybridCompileResult) {
            .generation = job->request.generation,
            .ticket = job->request.ticket,
            .kind = job->request.kind,
            .stage = job->request.stage,
            .urgency = job->request.urgency,
            .success = job->success,
            .spirv = job->spirv,
            .spirv_size = job->spirv_size,
            .submitted_us = job->submitted_us,
            .started_us = job->started_us,
            .finished_us = job->finished_us,
            .caller_return_us = g_get_monotonic_time(),
            .speculative_active_at_start =
                job->speculative_active_at_start,
        };
        job->spirv = NULL;
        job->spirv_size = 0;
    }
    qemu_mutex_unlock(&state->lock);
    job_destroy(job);
    return success;
}

bool pgraph_vk_hybrid_compiler_take_result(
    PGRAPHVkHybridCompiler *compiler,
    PGRAPHVkHybridCompileResult *result)
{
    HybridCompilerState *state = compiler ? compiler->state : NULL;
    HybridCompilerJob *job;

    if (result) {
        *result = (PGRAPHVkHybridCompileResult) { 0 };
    }
    if (!state || !result) {
        return false;
    }

    qemu_mutex_lock(&state->lock);
    job = state->result_head;
    if (!job) {
        qemu_mutex_unlock(&state->lock);
        return false;
    }
    state->result_head = job->next;
    if (!state->result_head) {
        state->result_tail = NULL;
        qatomic_set(&state->result_available, false);
    }
    async_account_release(state, job);
    *result = (PGRAPHVkHybridCompileResult) {
        .generation = job->request.generation,
        .ticket = job->request.ticket,
        .kind = job->request.kind,
        .stage = job->request.stage,
        .urgency = job->request.urgency,
        .success = job->success,
        .glsl = job->request.kind ==
                    PGRAPH_VK_HYBRID_JOB_GENERATE_SOURCE ? job->glsl : NULL,
        .glsl_size = job->request.kind ==
                         PGRAPH_VK_HYBRID_JOB_GENERATE_SOURCE ?
                     job->generated_glsl_size : 0,
        .spirv = job->spirv,
        .spirv_size = job->spirv_size,
        .submitted_us = job->submitted_us,
        .started_us = job->started_us,
        .finished_us = job->finished_us,
    };
    if (job->request.kind == PGRAPH_VK_HYBRID_JOB_GENERATE_SOURCE) {
        job->glsl = NULL;
        job->generated_glsl_size = 0;
    }
    job->spirv = NULL;
    job->spirv_size = 0;
    qemu_mutex_unlock(&state->lock);
    job_destroy(job);
    return true;
}

bool pgraph_vk_hybrid_compiler_has_result(
    const PGRAPHVkHybridCompiler *compiler)
{
    const HybridCompilerState *state = compiler ? compiler->state : NULL;

    return state && qatomic_read(&state->result_available);
}

bool pgraph_vk_hybrid_compiler_get_worker_status(
    PGRAPHVkHybridCompiler *compiler, PGRAPHVkHybridWorkerClass worker_class,
    PGRAPHVkHybridWorkerStatus *status)
{
    HybridCompilerState *state = compiler ? compiler->state : NULL;
    bool found = false;

    if (status) {
        *status = (PGRAPHVkHybridWorkerStatus) { 0 };
    }
    if (!state || !status) {
        return false;
    }
    qemu_mutex_lock(&state->lock);
    for (size_t i = 0; i < state->async_lane_count; i++) {
        if (state->async_lanes[i].worker_class == worker_class) {
            *status = state->async_lanes[i].status;
            found = true;
            break;
        }
    }
    qemu_mutex_unlock(&state->lock);
    return found;
}

void pgraph_vk_hybrid_compile_result_destroy(
    PGRAPHVkHybridCompileResult *result)
{
    if (!result) {
        return;
    }
    g_free(result->glsl);
    g_free(result->spirv);
    *result = (PGRAPHVkHybridCompileResult) { 0 };
}

void pgraph_vk_hybrid_compiler_stop(PGRAPHVkHybridCompiler *compiler)
{
    HybridCompilerState *state = compiler ? compiler->state : NULL;

    if (!state) {
        return;
    }
    qemu_mutex_lock(&state->lock);
    if (!state->stopping) {
        state->stopping = true;
        async_list_destroy(state, &state->async_head, &state->async_tail);
        async_list_destroy(state, &state->result_head, &state->result_tail);
        qatomic_set(&state->result_available, false);
        if (state->blocking && state->blocking != state->active_blocking) {
            HybridCompilerJob *job = state->blocking;
            state->blocking = NULL;
            job->cancelled = true;
            job->done = true;
        }
        qemu_cond_broadcast(&state->work_ready);
        qemu_cond_broadcast(&state->blocking_ready);
        qemu_cond_broadcast(&state->blocking_done);
    }
    qemu_mutex_unlock(&state->lock);
}

void pgraph_vk_hybrid_compiler_join(PGRAPHVkHybridCompiler *compiler)
{
    HybridCompilerState *state = compiler ? compiler->state : NULL;

    if (!state || state->joined) {
        return;
    }
    pgraph_vk_hybrid_compiler_stop(compiler);
    for (size_t i = 0; i < state->async_lane_count; i++) {
        qemu_thread_join(&state->async_lanes[i].thread);
    }
    qemu_thread_join(&state->blocking_worker);
    state->joined = true;
}

void pgraph_vk_hybrid_compiler_destroy(PGRAPHVkHybridCompiler *compiler)
{
    HybridCompilerState *state = compiler ? compiler->state : NULL;

    if (!state) {
        return;
    }
    pgraph_vk_hybrid_compiler_join(compiler);
    qemu_mutex_lock(&state->lock);
    async_list_destroy(state, &state->async_head, &state->async_tail);
    async_list_destroy(state, &state->result_head, &state->result_tail);
    for (size_t i = 0; i < state->async_lane_count; i++) {
        assert(!state->async_lanes[i].active);
    }
    assert(!state->active_blocking);
    assert(!state->blocking);
    qemu_mutex_unlock(&state->lock);
    qemu_cond_destroy(&state->blocking_done);
    qemu_cond_destroy(&state->blocking_ready);
    qemu_cond_destroy(&state->work_ready);
    qemu_mutex_destroy(&state->lock);
    g_free(state);
    compiler->state = NULL;
}

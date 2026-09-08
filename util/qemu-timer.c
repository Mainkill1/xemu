/*
 * QEMU System Emulator
 *
 * Copyright (c) 2003-2008 Fabrice Bellard
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "qemu/osdep.h"
#include "qemu/main-loop.h"
#include "qemu/timer.h"
#include "qemu/lockable.h"
#include "system/cpu-timers.h"
#include "exec/icount.h"
#include "system/replay.h"
#include "system/cpus.h"

#ifdef CONFIG_POSIX
#include <pthread.h>
#endif

#ifdef CONFIG_PPOLL
#include <poll.h>
#endif

#ifdef CONFIG_PRCTL_PR_SET_TIMERSLACK
#include <sys/prctl.h>
#endif

#ifdef XBOX
#define XBOX_POLL_PROFILE_OWNER_MAX 32

typedef struct XboxPollOwnerProfile {
    QEMUTimerCB *callback;
    QEMUClockType clock_type;
    uint64_t selected_calls;
    uint64_t selected_zero_calls;
    uint64_t selected_spin_calls;
    uint64_t selected_requested_ns;
    uint64_t selected_spin_ns;
    uint64_t tied_deadlines;
    uint64_t callback_calls;
    uint64_t callback_late_ns;
    uint64_t callback_max_late_ns;
} XboxPollOwnerProfile;

typedef struct XboxPollSpinProfile {
    bool initialized;
    bool enabled;
    uint64_t all_calls;
    uint64_t negative_calls;
    uint64_t zero_calls;
    uint64_t long_calls;
    uint64_t entries;
    uint64_t timeout_buckets[6];
    uint64_t requested_ns;
    uint64_t spin_ns;
    uint64_t iterations;
    uint64_t ready_exits;
    uint64_t errors;
    uint64_t nonspin_ready_exits;
    uint64_t nonspin_errors;
    uint64_t late_ns;
    uint64_t max_late_ns;
    uint64_t max_fds;
    uint64_t source_calls[XBOX_POLL_DEADLINE_SOURCE_COUNT];
    uint64_t source_zero_calls[XBOX_POLL_DEADLINE_SOURCE_COUNT];
    uint64_t source_spin_calls[XBOX_POLL_DEADLINE_SOURCE_COUNT];
    uint64_t owner_overflow;
    XboxPollDeadlineSource context_source;
    XboxTimerDeadlineInfo context_timer;
    XboxPollOwnerProfile owners[XBOX_POLL_PROFILE_OWNER_MAX];
    int64_t last_report_ns;
#ifdef _WIN32
    HANDLE reset_event;
    HANDLE reset_ack_event;
    HANDLE flush_event;
    HANDLE flush_ack_event;
#endif
    uint64_t phase;
} XboxPollSpinProfile;

static __thread XboxPollSpinProfile xbox_poll_spin_profile;

#ifdef _WIN32
static HANDLE xbox_poll_profile_named_event(const wchar_t *operation,
                                            const wchar_t *suffix)
{
    wchar_t name[128];

    swprintf(name, ARRAY_SIZE(name),
             L"Local\\XemuQemuPollProfile%ls-%lu%ls", operation,
             GetCurrentProcessId(), suffix);
    return CreateEventW(NULL, FALSE, FALSE, name);
}

static void xbox_poll_profile_init_controls(XboxPollSpinProfile *profile)
{
    profile->reset_event = xbox_poll_profile_named_event(L"Reset", L"");
    profile->reset_ack_event = xbox_poll_profile_named_event(L"Reset", L"-Ack");
    profile->flush_event = xbox_poll_profile_named_event(L"Flush", L"");
    profile->flush_ack_event = xbox_poll_profile_named_event(L"Flush", L"-Ack");
}
#endif

static bool xbox_poll_spin_profile_enabled(XboxPollSpinProfile *profile)
{
    if (!profile->initialized) {
        const char *value = getenv("XEMU_QEMU_POLL_PROFILE");

        profile->enabled = value && strcmp(value, "0");
        profile->initialized = true;
#ifdef _WIN32
        if (profile->enabled) {
            xbox_poll_profile_init_controls(profile);
        }
#endif
    }
    return profile->enabled;
}

static void xbox_poll_profile_emit(XboxPollSpinProfile *profile);

static void xbox_poll_profile_reset(XboxPollSpinProfile *profile)
{
#ifdef _WIN32
    HANDLE reset_event = profile->reset_event;
    HANDLE reset_ack_event = profile->reset_ack_event;
    HANDLE flush_event = profile->flush_event;
    HANDLE flush_ack_event = profile->flush_ack_event;
#endif
    uint64_t phase = profile->phase + 1;

    memset(profile, 0, sizeof(*profile));
    profile->initialized = true;
    profile->enabled = true;
    profile->phase = phase;
#ifdef _WIN32
    profile->reset_event = reset_event;
    profile->reset_ack_event = reset_ack_event;
    profile->flush_event = flush_event;
    profile->flush_ack_event = flush_ack_event;
#endif
}

static void xbox_poll_profile_apply_controls(XboxPollSpinProfile *profile)
{
#ifdef _WIN32
    if (profile->reset_event &&
        WaitForSingleObject(profile->reset_event, 0) == WAIT_OBJECT_0) {
        xbox_poll_profile_reset(profile);
        if (profile->reset_ack_event) {
            SetEvent(profile->reset_ack_event);
        }
    }
    if (profile->flush_event &&
        WaitForSingleObject(profile->flush_event, 0) == WAIT_OBJECT_0) {
        xbox_poll_profile_emit(profile);
        if (profile->flush_ack_event) {
            SetEvent(profile->flush_ack_event);
        }
    }
#endif
}

static uint64_t xbox_poll_profile_callback_id(QEMUTimerCB *callback)
{
#ifdef _WIN32
    return (uintptr_t)callback - (uintptr_t)GetModuleHandleW(NULL);
#else
    return (uintptr_t)callback;
#endif
}

static XboxPollOwnerProfile *xbox_poll_profile_owner(
    XboxPollSpinProfile *profile, QEMUTimerCB *callback,
    QEMUClockType clock_type)
{
    XboxPollOwnerProfile *empty = NULL;
    unsigned int i;

    if (!callback) {
        return NULL;
    }

    for (i = 0; i < ARRAY_SIZE(profile->owners); i++) {
        if (profile->owners[i].callback == callback &&
            profile->owners[i].clock_type == clock_type) {
            return &profile->owners[i];
        }
        if (!empty && !profile->owners[i].callback) {
            empty = &profile->owners[i];
        }
    }

    if (!empty) {
        profile->owner_overflow++;
        return NULL;
    }

    empty->callback = callback;
    empty->clock_type = clock_type;
    return empty;
}

static void xbox_poll_profile_record_context(XboxPollSpinProfile *profile,
                                             int64_t timeout,
                                             int64_t spin_ns)
{
    XboxPollOwnerProfile *owner;
    XboxPollDeadlineSource source = profile->context_source;

    if (source >= XBOX_POLL_DEADLINE_SOURCE_COUNT) {
        source = XBOX_POLL_DEADLINE_UNKNOWN;
    }

    profile->source_calls[source]++;
    profile->source_zero_calls[source] += timeout == 0;
    profile->source_spin_calls[source] += spin_ns > 0;

    if (source == XBOX_POLL_DEADLINE_TIMER) {
        owner = xbox_poll_profile_owner(profile,
                                        profile->context_timer.callback,
                                        profile->context_timer.clock_type);
        if (owner) {
            owner->selected_calls++;
            owner->selected_zero_calls += timeout == 0;
            owner->selected_spin_calls += spin_ns > 0;
            owner->selected_requested_ns += timeout > 0 ? timeout : 0;
            owner->selected_spin_ns += spin_ns > 0 ? spin_ns : 0;
            owner->tied_deadlines +=
                profile->context_timer.tied_deadlines;
        }
    }

    profile->context_source = XBOX_POLL_DEADLINE_UNKNOWN;
    memset(&profile->context_timer, 0, sizeof(profile->context_timer));
}

void xbox_poll_profile_set_context(XboxPollDeadlineSource source,
                                   const XboxTimerDeadlineInfo *timer_info)
{
    XboxPollSpinProfile *profile = &xbox_poll_spin_profile;

    if (!xbox_poll_spin_profile_enabled(profile)) {
        return;
    }
    xbox_poll_profile_apply_controls(profile);

    profile->context_source = source;
    if (timer_info) {
        profile->context_timer = *timer_info;
    } else {
        memset(&profile->context_timer, 0, sizeof(profile->context_timer));
    }
}

void xbox_poll_profile_override_context(XboxPollDeadlineSource source)
{
    xbox_poll_profile_set_context(source, NULL);
}

static void xbox_poll_profile_record_callback(QEMUTimerCB *callback,
                                              QEMUClockType clock_type,
                                              int64_t expire_time_ns,
                                              int64_t callback_start_ns)
{
    XboxPollSpinProfile *profile = &xbox_poll_spin_profile;
    XboxPollOwnerProfile *owner;
    uint64_t late_ns;

    if (!xbox_poll_spin_profile_enabled(profile)) {
        return;
    }

    owner = xbox_poll_profile_owner(profile, callback, clock_type);
    if (!owner) {
        return;
    }

    late_ns = callback_start_ns > expire_time_ns ?
              callback_start_ns - expire_time_ns : 0;
    owner->callback_calls++;
    owner->callback_late_ns += late_ns;
    owner->callback_max_late_ns = MAX(owner->callback_max_late_ns, late_ns);
}

static void xbox_poll_profile_emit(XboxPollSpinProfile *profile)
{
    unsigned int i;

    fprintf(stderr,
            "XEMU_QEMU_POLL_PROFILE v=5 tid=%d phase=%" PRIu64
            " all_calls=%" PRIu64
            " negative_calls=%" PRIu64 " zero_calls=%" PRIu64
            " long_calls=%" PRIu64 " entries=%" PRIu64
            " buckets=%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64
            ",%" PRIu64 ",%" PRIu64 " requested_ns=%" PRIu64
            " spin_ns=%" PRIu64 " iterations=%" PRIu64
            " ready_exits=%" PRIu64 " errors=%" PRIu64
            " nonspin_ready_exits=%" PRIu64
            " nonspin_errors=%" PRIu64
            " late_ns=%" PRIu64
            " max_late_ns=%" PRIu64 " max_fds=%" PRIu64
            " sources=%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64
            ",%" PRIu64 " source_zeros=%" PRIu64 ",%" PRIu64 ",%" PRIu64
            ",%" PRIu64 ",%" PRIu64 " source_spins=%" PRIu64 ",%" PRIu64
            ",%" PRIu64 ",%" PRIu64 ",%" PRIu64
            " owner_overflow=%" PRIu64 "\n",
            qemu_get_thread_id(), profile->phase, profile->all_calls,
            profile->negative_calls, profile->zero_calls,
            profile->long_calls, profile->entries,
            profile->timeout_buckets[0], profile->timeout_buckets[1],
            profile->timeout_buckets[2], profile->timeout_buckets[3],
            profile->timeout_buckets[4], profile->timeout_buckets[5],
            profile->requested_ns, profile->spin_ns, profile->iterations,
            profile->ready_exits, profile->errors,
            profile->nonspin_ready_exits, profile->nonspin_errors,
            profile->late_ns, profile->max_late_ns, profile->max_fds,
            profile->source_calls[0], profile->source_calls[1],
            profile->source_calls[2], profile->source_calls[3],
            profile->source_calls[4], profile->source_zero_calls[0],
            profile->source_zero_calls[1], profile->source_zero_calls[2],
            profile->source_zero_calls[3], profile->source_zero_calls[4],
            profile->source_spin_calls[0], profile->source_spin_calls[1],
            profile->source_spin_calls[2], profile->source_spin_calls[3],
            profile->source_spin_calls[4], profile->owner_overflow);

    for (i = 0; i < ARRAY_SIZE(profile->owners); i++) {
        XboxPollOwnerProfile *owner = &profile->owners[i];

        if (!owner->callback) {
            continue;
        }
        fprintf(stderr,
                "XEMU_QEMU_TIMER_OWNER_PROFILE v=2 tid=%d phase=%" PRIu64
                " callback_id=0x%" PRIx64
                " clock=%d selected=%" PRIu64 " selected_zero=%" PRIu64
                " selected_spin=%" PRIu64 " selected_requested_ns=%" PRIu64
                " selected_spin_ns=%" PRIu64 " tied=%" PRIu64
                " callbacks=%" PRIu64 " callback_late_ns=%" PRIu64
                " callback_max_late_ns=%" PRIu64 "\n",
                qemu_get_thread_id(), profile->phase,
                xbox_poll_profile_callback_id(owner->callback),
                owner->clock_type, owner->selected_calls,
                owner->selected_zero_calls, owner->selected_spin_calls,
                owner->selected_requested_ns, owner->selected_spin_ns,
                owner->tied_deadlines, owner->callback_calls,
                owner->callback_late_ns, owner->callback_max_late_ns);
    }
    fflush(stderr);
}

static void xbox_poll_spin_profile_record(int64_t requested_ns,
                                          int64_t spin_ns,
                                          uint64_t iterations,
                                          int64_t late_ns,
                                          guint nfds,
                                          int poll_ret,
                                          int64_t now_ns)
{
    XboxPollSpinProfile *profile = &xbox_poll_spin_profile;
    unsigned int bucket;

    if (!xbox_poll_spin_profile_enabled(profile)) {
        return;
    }

    if (requested_ns < 100000) {
        bucket = 0;
    } else if (requested_ns < 250000) {
        bucket = 1;
    } else if (requested_ns < 500000) {
        bucket = 2;
    } else if (requested_ns < 750000) {
        bucket = 3;
    } else if (requested_ns < 1000000) {
        bucket = 4;
    } else {
        bucket = 5;
    }

    profile->entries++;
    profile->all_calls++;
    profile->timeout_buckets[bucket]++;
    profile->requested_ns += (uint64_t)requested_ns;
    profile->spin_ns += (uint64_t)spin_ns;
    profile->iterations += iterations;
    profile->ready_exits += poll_ret > 0;
    profile->errors += poll_ret < 0;
    profile->late_ns += (uint64_t)late_ns;
    profile->max_late_ns = MAX(profile->max_late_ns, (uint64_t)late_ns);
    profile->max_fds = MAX(profile->max_fds, (uint64_t)nfds);
    xbox_poll_profile_record_context(profile, requested_ns, spin_ns);

    if (!profile->last_report_ns) {
        profile->last_report_ns = now_ns;
    } else if (now_ns - profile->last_report_ns >= NANOSECONDS_PER_SECOND) {
        xbox_poll_profile_emit(profile);
        profile->last_report_ns = now_ns;
    }
}

static void xbox_poll_profile_record_nonspin(int64_t timeout, int poll_ret)
{
    XboxPollSpinProfile *profile = &xbox_poll_spin_profile;

    if (!xbox_poll_spin_profile_enabled(profile)) {
        return;
    }

    profile->all_calls++;
    if (timeout < 0) {
        profile->negative_calls++;
    } else if (timeout == 0) {
        profile->zero_calls++;
    } else {
        profile->long_calls++;
    }
    profile->nonspin_ready_exits += poll_ret > 0;
    profile->nonspin_errors += poll_ret < 0;
    xbox_poll_profile_record_context(profile, timeout, 0);

    /* Capture a zero-timeout-only tail without another clock read. */
    if (!(profile->all_calls & 4095)) {
        xbox_poll_profile_emit(profile);
    }
}
#endif

/***********************************************************/
/* timers */

typedef struct QEMUClock {
    /* We rely on BQL to protect the timerlists */
    QLIST_HEAD(, QEMUTimerList) timerlists;

    QEMUClockType type;
    bool enabled;
} QEMUClock;

QEMUTimerListGroup main_loop_tlg;
static QEMUClock qemu_clocks[QEMU_CLOCK_MAX];

/* A QEMUTimerList is a list of timers attached to a clock. More
 * than one QEMUTimerList can be attached to each clock, for instance
 * used by different AioContexts / threads. Each clock also has
 * a list of the QEMUTimerLists associated with it, in order that
 * reenabling the clock can call all the notifiers.
 */

struct QEMUTimerList {
    QEMUClock *clock;
    QemuMutex active_timers_lock;
    QEMUTimer *active_timers;
    QLIST_ENTRY(QEMUTimerList) list;
    QEMUTimerListNotifyCB *notify_cb;
    void *notify_opaque;

    /* lightweight method to mark the end of timerlist's running */
    QemuEvent timers_done_ev;
};

/**
 * qemu_clock_ptr:
 * @type: type of clock
 *
 * Translate a clock type into a pointer to QEMUClock object.
 *
 * Returns: a pointer to the QEMUClock object
 */
static inline QEMUClock *qemu_clock_ptr(QEMUClockType type)
{
    return &qemu_clocks[type];
}

static bool timer_expired_ns(const QEMUTimer *timer_head, int64_t current_time)
{
    return timer_head && (timer_head->expire_time <= current_time);
}

QEMUTimerList *timerlist_new(QEMUClockType type,
                             QEMUTimerListNotifyCB *cb,
                             void *opaque)
{
    QEMUTimerList *timer_list;
    QEMUClock *clock = qemu_clock_ptr(type);

    timer_list = g_new0(QEMUTimerList, 1);
    qemu_event_init(&timer_list->timers_done_ev, true);
    timer_list->clock = clock;
    timer_list->notify_cb = cb;
    timer_list->notify_opaque = opaque;
    qemu_mutex_init(&timer_list->active_timers_lock);
    QLIST_INSERT_HEAD(&clock->timerlists, timer_list, list);
    return timer_list;
}

void timerlist_free(QEMUTimerList *timer_list)
{
    assert(!timerlist_has_timers(timer_list));
    if (timer_list->clock) {
        QLIST_REMOVE(timer_list, list);
    }
    qemu_mutex_destroy(&timer_list->active_timers_lock);
    g_free(timer_list);
}

static void qemu_clock_init(QEMUClockType type, QEMUTimerListNotifyCB *notify_cb)
{
    QEMUClock *clock = qemu_clock_ptr(type);

    /* Assert that the clock of type TYPE has not been initialized yet. */
    assert(main_loop_tlg.tl[type] == NULL);

    clock->type = type;
    clock->enabled = (type == QEMU_CLOCK_VIRTUAL ? false : true);
    QLIST_INIT(&clock->timerlists);
    main_loop_tlg.tl[type] = timerlist_new(type, notify_cb, NULL);
}

bool qemu_clock_use_for_deadline(QEMUClockType type)
{
    return !(icount_enabled() && (type == QEMU_CLOCK_VIRTUAL));
}

void qemu_clock_notify(QEMUClockType type)
{
    QEMUTimerList *timer_list;
    QEMUClock *clock = qemu_clock_ptr(type);
    QLIST_FOREACH(timer_list, &clock->timerlists, list) {
        timerlist_notify(timer_list);
    }
}

/* Disabling the clock will wait for related timerlists to stop
 * executing qemu_run_timers.  Thus, this functions should not
 * be used from the callback of a timer that is based on @clock.
 * Doing so would cause a deadlock.
 *
 * Caller should hold BQL.
 */
void qemu_clock_enable(QEMUClockType type, bool enabled)
{
    QEMUClock *clock = qemu_clock_ptr(type);
    QEMUTimerList *tl;
    bool old = clock->enabled;
    clock->enabled = enabled;
    if (enabled && !old) {
        qemu_clock_notify(type);
    } else if (!enabled && old) {
        QLIST_FOREACH(tl, &clock->timerlists, list) {
            qemu_event_wait(&tl->timers_done_ev);
        }
    }
}

bool timerlist_has_timers(QEMUTimerList *timer_list)
{
    return !!qatomic_read(&timer_list->active_timers);
}

bool qemu_clock_has_timers(QEMUClockType type)
{
    return timerlist_has_timers(
        main_loop_tlg.tl[type]);
}

bool timerlist_expired(QEMUTimerList *timer_list)
{
    int64_t expire_time = 0;

    if (!qatomic_read(&timer_list->active_timers)) {
        return false;
    }

    WITH_QEMU_LOCK_GUARD(&timer_list->active_timers_lock) {
        if (!timer_list->active_timers) {
            return false;
        }
        expire_time = timer_list->active_timers->expire_time;
    }

    return expire_time <= qemu_clock_get_ns(timer_list->clock->type);
}

bool qemu_clock_expired(QEMUClockType type)
{
    return timerlist_expired(
        main_loop_tlg.tl[type]);
}

/*
 * As above, but return -1 for no deadline, and do not cap to 2^32
 * as we know the result is always positive.
 */

int64_t timerlist_deadline_ns(QEMUTimerList *timer_list)
{
    int64_t delta;
    int64_t expire_time = 0;

    if (!qatomic_read(&timer_list->active_timers)) {
        return -1;
    }

    if (!timer_list->clock->enabled) {
        return -1;
    }

    /* The active timers list may be modified before the caller uses our return
     * value but ->notify_cb() is called when the deadline changes.  Therefore
     * the caller should notice the change and there is no race condition.
     */
    WITH_QEMU_LOCK_GUARD(&timer_list->active_timers_lock) {
        if (!timer_list->active_timers) {
            return -1;
        }
        expire_time = timer_list->active_timers->expire_time;
    }

    delta = expire_time - qemu_clock_get_ns(timer_list->clock->type);

    if (delta <= 0) {
        return 0;
    }

    return delta;
}

/* Calculate the soonest deadline across all timerlists attached
 * to the clock. This is used for the icount timeout so we
 * ignore whether or not the clock should be used in deadline
 * calculations.
 */
int64_t qemu_clock_deadline_ns_all(QEMUClockType type, int attr_mask)
{
    int64_t deadline = -1;
    int64_t delta;
    int64_t expire_time;
    QEMUTimer *ts;
    QEMUTimerList *timer_list;
    QEMUClock *clock = qemu_clock_ptr(type);

    if (!clock->enabled) {
        return -1;
    }

    QLIST_FOREACH(timer_list, &clock->timerlists, list) {
        if (!qatomic_read(&timer_list->active_timers)) {
            continue;
        }
        qemu_mutex_lock(&timer_list->active_timers_lock);
        ts = timer_list->active_timers;
        /* Skip all external timers */
        while (ts && (ts->attributes & ~attr_mask)) {
            ts = ts->next;
        }
        if (!ts) {
            qemu_mutex_unlock(&timer_list->active_timers_lock);
            continue;
        }
        expire_time = ts->expire_time;
        qemu_mutex_unlock(&timer_list->active_timers_lock);

        delta = expire_time - qemu_clock_get_ns(type);
        if (delta <= 0) {
            delta = 0;
        }
        deadline = qemu_soonest_timeout(deadline, delta);
    }
    return deadline;
}

void timerlist_notify(QEMUTimerList *timer_list)
{
    if (timer_list->notify_cb) {
        timer_list->notify_cb(timer_list->notify_opaque, timer_list->clock->type);
    } else {
        qemu_notify_event();
    }
}

/* Transition function to convert a nanosecond timeout to ms
 * This is used where a system does not support ppoll
 */
int qemu_timeout_ns_to_ms(int64_t ns)
{
    int64_t ms;
    if (ns < 0) {
        return -1;
    }

    if (!ns) {
        return 0;
    }

    /* Always round up, because it's better to wait too long than to wait too
     * little and effectively busy-wait
     */
    ms = DIV_ROUND_UP(ns, SCALE_MS);

    /* To avoid overflow problems, limit this to 2^31, i.e. approx 25 days */
    return MIN(ms, INT32_MAX);
}


/* qemu implementation of g_poll which uses a nanosecond timeout but is
 * otherwise identical to g_poll
 */
int qemu_poll_ns(GPollFD *fds, guint nfds, int64_t timeout)
{
#ifdef CONFIG_PPOLL
    if (timeout < 0) {
        return ppoll((struct pollfd *)fds, nfds, NULL, NULL);
    } else {
        struct timespec ts;
        int64_t tvsec = timeout / 1000000000LL;
        /* Avoid possibly overflowing and specifying a negative number of
         * seconds, which would turn a very long timeout into a busy-wait.
         */
        if (tvsec > (int64_t)INT32_MAX) {
            tvsec = INT32_MAX;
        }
        ts.tv_sec = tvsec;
        ts.tv_nsec = timeout % 1000000000LL;
        return ppoll((struct pollfd *)fds, nfds, &ts, NULL);
    }
#else

#ifdef XBOX
    /* Timers are facilitated by this function. Busy-wait if the deadline is
     * near, to avoid missing deadlines due to costly sleeps.
     */
    #define XBOX_BUSYWAIT_THRESHOLD_NS 1250000
    if ((0 < timeout) && (timeout < XBOX_BUSYWAIT_THRESHOLD_NS)) {
        int64_t now = qemu_clock_get_ns(QEMU_CLOCK_REALTIME);
        int64_t start = now;
        int64_t end = now + timeout;
        uint64_t iterations = 0;
        int ret;

        while (now < end) {
            now = qemu_clock_get_ns(QEMU_CLOCK_REALTIME);
            iterations++;
        }
        ret = g_poll(fds, nfds, 0);
        xbox_poll_spin_profile_record(timeout, now - start, iterations,
                                      MAX(now - end, 0), nfds, ret, now);
        return ret;
    }
#endif

#ifdef XBOX
    {
        int ret = g_poll(fds, nfds, qemu_timeout_ns_to_ms(timeout));

        xbox_poll_profile_record_nonspin(timeout, ret);
        return ret;
    }
#else
    return g_poll(fds, nfds, qemu_timeout_ns_to_ms(timeout));
#endif
#endif
}


void timer_init_full(QEMUTimer *ts,
                     QEMUTimerListGroup *timer_list_group, QEMUClockType type,
                     int scale, int attributes,
                     QEMUTimerCB *cb, void *opaque)
{
    if (!timer_list_group) {
        timer_list_group = &main_loop_tlg;
    }
    ts->timer_list = timer_list_group->tl[type];
    ts->cb = cb;
    ts->opaque = opaque;
    ts->scale = scale;
    ts->attributes = attributes;
    ts->expire_time = -1;
}

void timer_deinit(QEMUTimer *ts)
{
    assert(ts->expire_time == -1);
    ts->timer_list = NULL;
}

static void timer_del_locked(QEMUTimerList *timer_list, QEMUTimer *ts)
{
    QEMUTimer **pt, *t;

    ts->expire_time = -1;
    pt = &timer_list->active_timers;
    for(;;) {
        t = *pt;
        if (!t)
            break;
        if (t == ts) {
            qatomic_set(pt, t->next);
            break;
        }
        pt = &t->next;
    }
}

static bool timer_mod_ns_locked(QEMUTimerList *timer_list,
                                QEMUTimer *ts, int64_t expire_time)
{
    QEMUTimer **pt, *t;

    /* add the timer in the sorted list */
    pt = &timer_list->active_timers;
    for (;;) {
        t = *pt;
        if (!timer_expired_ns(t, expire_time)) {
            break;
        }
        pt = &t->next;
    }
    ts->expire_time = MAX(expire_time, 0);
    ts->next = *pt;
    qatomic_set(pt, ts);

    return pt == &timer_list->active_timers;
}

static void timerlist_rearm(QEMUTimerList *timer_list)
{
    timerlist_notify(timer_list);
}

/* stop a timer, but do not dealloc it */
void timer_del(QEMUTimer *ts)
{
    QEMUTimerList *timer_list = ts->timer_list;

    if (timer_list) {
        qemu_mutex_lock(&timer_list->active_timers_lock);
        timer_del_locked(timer_list, ts);
        qemu_mutex_unlock(&timer_list->active_timers_lock);
    }
}

/* modify the current timer so that it will be fired when current_time
   >= expire_time. The corresponding callback will be called. */
void timer_mod_ns(QEMUTimer *ts, int64_t expire_time)
{
    QEMUTimerList *timer_list = ts->timer_list;
    bool rearm;

    qemu_mutex_lock(&timer_list->active_timers_lock);
    timer_del_locked(timer_list, ts);
    rearm = timer_mod_ns_locked(timer_list, ts, expire_time);
    qemu_mutex_unlock(&timer_list->active_timers_lock);

    if (rearm) {
        timerlist_rearm(timer_list);
    }
}

/* modify the current timer so that it will be fired when current_time
   >= expire_time or the current deadline, whichever comes earlier.
   The corresponding callback will be called. */
void timer_mod_anticipate_ns(QEMUTimer *ts, int64_t expire_time)
{
    QEMUTimerList *timer_list = ts->timer_list;
    bool rearm = false;

    WITH_QEMU_LOCK_GUARD(&timer_list->active_timers_lock) {
        if (ts->expire_time == -1 || ts->expire_time > expire_time) {
            if (ts->expire_time != -1) {
                timer_del_locked(timer_list, ts);
            }
            rearm = timer_mod_ns_locked(timer_list, ts, expire_time);
        } else {
            rearm = false;
        }
    }
    if (rearm) {
        timerlist_rearm(timer_list);
    }
}

void timer_mod(QEMUTimer *ts, int64_t expire_time)
{
    timer_mod_ns(ts, expire_time * ts->scale);
}

void timer_mod_anticipate(QEMUTimer *ts, int64_t expire_time)
{
    timer_mod_anticipate_ns(ts, expire_time * ts->scale);
}

bool timer_pending(const QEMUTimer *ts)
{
    return ts->expire_time >= 0;
}

bool timer_expired(const QEMUTimer *timer_head, int64_t current_time)
{
    return timer_expired_ns(timer_head, current_time * timer_head->scale);
}

bool timerlist_run_timers(QEMUTimerList *timer_list)
{
    QEMUTimer *ts;
    int64_t current_time;
    int64_t expire_time;
    bool progress = false;
    QEMUTimerCB *cb;
    void *opaque;

    if (!qatomic_read(&timer_list->active_timers)) {
        return false;
    }

    qemu_event_reset(&timer_list->timers_done_ev);
    if (!timer_list->clock->enabled) {
        goto out;
    }

    switch (timer_list->clock->type) {
    case QEMU_CLOCK_REALTIME:
        break;
    default:
    case QEMU_CLOCK_VIRTUAL:
        break;
    case QEMU_CLOCK_HOST:
        if (!replay_checkpoint(CHECKPOINT_CLOCK_HOST)) {
            goto out;
        }
        break;
    case QEMU_CLOCK_VIRTUAL_RT:
        if (!replay_checkpoint(CHECKPOINT_CLOCK_VIRTUAL_RT)) {
            goto out;
        }
        break;
    }

    /*
     * Extract expired timers from active timers list and process them.
     *
     * In rr mode we need "filtered" checkpointing for virtual clock.  The
     * checkpoint must be recorded/replayed before processing any non-EXTERNAL timer,
     * and that must only be done once since the clock value stays the same. Because
     * non-EXTERNAL timers may appear in the timers list while it being processed,
     * the checkpoint can be issued at a time until no timers are left and we are
     * done".
     */
    current_time = qemu_clock_get_ns(timer_list->clock->type);
    qemu_mutex_lock(&timer_list->active_timers_lock);
    while ((ts = timer_list->active_timers)) {
        if (!timer_expired_ns(ts, current_time)) {
            /* No expired timers left.  The checkpoint can be skipped
             * if no timers fired or they were all external.
             */
            break;
        }
        /* Checkpoint for virtual clock is redundant in cases where
         * it's being triggered with only non-EXTERNAL timers, because
         * these timers don't change guest state directly.
         */
        if (replay_mode != REPLAY_MODE_NONE
            && timer_list->clock->type == QEMU_CLOCK_VIRTUAL
            && !(ts->attributes & QEMU_TIMER_ATTR_EXTERNAL)
            && !replay_checkpoint(CHECKPOINT_CLOCK_VIRTUAL)) {
            qemu_mutex_unlock(&timer_list->active_timers_lock);
            goto out;
        }

        /* remove timer from the list before calling the callback */
        timer_list->active_timers = ts->next;
        ts->next = NULL;
        expire_time = ts->expire_time;
        ts->expire_time = -1;
        cb = ts->cb;
        opaque = ts->opaque;

        /* run the callback (the timer list can be modified) */
        qemu_mutex_unlock(&timer_list->active_timers_lock);
#ifdef XBOX
        if (xbox_poll_spin_profile_enabled(&xbox_poll_spin_profile)) {
            int64_t callback_start =
                qemu_clock_get_ns(timer_list->clock->type);

            xbox_poll_profile_record_callback(cb, timer_list->clock->type,
                                              expire_time, callback_start);
        }
#endif
        cb(opaque);
        qemu_mutex_lock(&timer_list->active_timers_lock);

        progress = true;
    }
    qemu_mutex_unlock(&timer_list->active_timers_lock);

out:
    qemu_event_set(&timer_list->timers_done_ev);
    return progress;
}

bool qemu_clock_run_timers(QEMUClockType type)
{
    return timerlist_run_timers(main_loop_tlg.tl[type]);
}

void timerlistgroup_init(QEMUTimerListGroup *tlg,
                         QEMUTimerListNotifyCB *cb, void *opaque)
{
    QEMUClockType type;
    for (type = 0; type < QEMU_CLOCK_MAX; type++) {
        tlg->tl[type] = timerlist_new(type, cb, opaque);
    }
}

void timerlistgroup_deinit(QEMUTimerListGroup *tlg)
{
    QEMUClockType type;
    for (type = 0; type < QEMU_CLOCK_MAX; type++) {
        timerlist_free(tlg->tl[type]);
    }
}

bool timerlistgroup_run_timers(QEMUTimerListGroup *tlg)
{
    QEMUClockType type;
    bool progress = false;
    for (type = 0; type < QEMU_CLOCK_MAX; type++) {
        progress |= timerlist_run_timers(tlg->tl[type]);
    }
    return progress;
}

int64_t timerlistgroup_deadline_ns(QEMUTimerListGroup *tlg)
{
    int64_t deadline = -1;
    QEMUClockType type;
    for (type = 0; type < QEMU_CLOCK_MAX; type++) {
        if (qemu_clock_use_for_deadline(type)) {
            deadline = qemu_soonest_timeout(deadline,
                                            timerlist_deadline_ns(tlg->tl[type]));
        }
    }
    return deadline;
}

#ifdef XBOX
static int64_t xbox_timerlist_deadline_ns(QEMUTimerList *timer_list,
                                          XboxTimerDeadlineInfo *info)
{
    QEMUTimer *timer;
    int64_t delta;

    if (!qatomic_read(&timer_list->active_timers) ||
        !timer_list->clock->enabled) {
        return -1;
    }

    WITH_QEMU_LOCK_GUARD(&timer_list->active_timers_lock) {
        timer = timer_list->active_timers;
        if (!timer) {
            return -1;
        }
        info->expire_time_ns = timer->expire_time;
        info->callback = timer->cb;
        info->clock_type = timer_list->clock->type;
    }

    delta = info->expire_time_ns -
            qemu_clock_get_ns(timer_list->clock->type);
    info->deadline_ns = MAX(delta, 0);
    return info->deadline_ns;
}

int64_t xbox_timerlistgroup_deadline_ns(QEMUTimerListGroup *tlg,
                                        XboxTimerDeadlineInfo *info)
{
    XboxTimerDeadlineInfo candidate;
    int64_t candidate_deadline;
    int64_t deadline = -1;
    QEMUClockType type;

    memset(info, 0, sizeof(*info));
    info->deadline_ns = -1;
    info->expire_time_ns = -1;

    for (type = 0; type < QEMU_CLOCK_MAX; type++) {
        if (!qemu_clock_use_for_deadline(type)) {
            continue;
        }

        memset(&candidate, 0, sizeof(candidate));
        candidate_deadline =
            xbox_timerlist_deadline_ns(tlg->tl[type], &candidate);
        if (candidate_deadline < 0) {
            continue;
        }
        if (deadline < 0 || candidate_deadline < deadline) {
            *info = candidate;
            deadline = candidate_deadline;
        } else if (candidate_deadline == deadline) {
            info->tied_deadlines++;
        }
    }

    return deadline;
}
#endif

int64_t qemu_clock_get_ns(QEMUClockType type)
{
    switch (type) {
    case QEMU_CLOCK_REALTIME:
        return get_clock();
    default:
    case QEMU_CLOCK_VIRTUAL:
        return cpus_get_virtual_clock();
    case QEMU_CLOCK_HOST:
        return REPLAY_CLOCK(REPLAY_CLOCK_HOST, get_clock_realtime());
    case QEMU_CLOCK_VIRTUAL_RT:
        return REPLAY_CLOCK(REPLAY_CLOCK_VIRTUAL_RT, cpu_get_clock());
    }
}

static void qemu_virtual_clock_set_ns(int64_t time)
{
    return cpus_set_virtual_clock(time);
}

void qemu_init_clocks(QEMUTimerListNotifyCB *notify_cb)
{
    QEMUClockType type;
    for (type = 0; type < QEMU_CLOCK_MAX; type++) {
        qemu_clock_init(type, notify_cb);
    }

#ifdef CONFIG_PRCTL_PR_SET_TIMERSLACK
    prctl(PR_SET_TIMERSLACK, 1, 0, 0, 0);
#endif
}

uint64_t timer_expire_time_ns(const QEMUTimer *ts)
{
    return timer_pending(ts) ? ts->expire_time : -1;
}

bool qemu_clock_run_all_timers(void)
{
    bool progress = false;
    QEMUClockType type;

    for (type = 0; type < QEMU_CLOCK_MAX; type++) {
        if (qemu_clock_use_for_deadline(type)) {
            progress |= qemu_clock_run_timers(type);
        }
    }

    return progress;
}

int64_t qemu_clock_advance_virtual_time(int64_t dest)
{
    int64_t clock = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
    AioContext *aio_context;
    aio_context = qemu_get_aio_context();
    while (clock < dest) {
        int64_t deadline = qemu_clock_deadline_ns_all(QEMU_CLOCK_VIRTUAL,
                                                      QEMU_TIMER_ATTR_ALL);
        int64_t warp = qemu_soonest_timeout(dest - clock, deadline);

        qemu_virtual_clock_set_ns(qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + warp);

        qemu_clock_run_timers(QEMU_CLOCK_VIRTUAL);
        timerlist_run_timers(aio_context->tlg.tl[QEMU_CLOCK_VIRTUAL]);
        clock = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
    }
    qemu_clock_notify(QEMU_CLOCK_VIRTUAL);

    return clock;
}

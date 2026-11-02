/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "qemu/osdep.h"
#include "qemu/bswap.h"
#include "qemu/thread.h"
#include "qemu/timer.h"
#include "gain_shadow.h"
#include "dsp_capture.h"

struct DspGainShadow {
    QemuThread worker;
    QemuMutex lock;
    QemuCond cond;
    bool job, ready, busy, stop;
    unsigned mask;
    int fault;
    uint32_t input[64], output[192];
    uint64_t elapsed[3];
    GChecksum *checksum;
    DspJitCapture capture;
    DspGainShadowStats stats;
};

static void *gain_worker(void *opaque)
{
    DspGainShadow *s = opaque;
    qemu_mutex_lock(&s->lock);
    for (;;) {
        while (!s->job && !s->stop) {
            qemu_cond_wait(&s->cond, &s->lock);
        }
        if (s->stop) {
            break;
        }
        s->job = false;
        qemu_mutex_unlock(&s->lock);
        for (unsigned route = 0; route < 3; route++) {
            if (!(s->mask & (1u << route))) {
                continue;
            }
            int64_t start = qemu_clock_get_ns(QEMU_CLOCK_REALTIME);
            for (unsigned i = 0; i < 64; i++) {
                uint32_t word = s->input[i];
                int64_t value =
                    word & 0x800000 ? (int64_t)word - 0x1000000 : word;
                if (route == 1) {
                    value = -value;
                }
                int64_t divisor = route == 2 ? 4 : 2;
                int64_t result = value / divisor;
                if (value < 0 && value % divisor) {
                    result--;
                }
                s->output[route * 64 + i] = (uint32_t)result & 0xffffff;
            }
            s->elapsed[route] = qemu_clock_get_ns(QEMU_CLOCK_REALTIME) - start;
        }
        if (s->fault >= 0 && (s->mask & (1u << (s->fault / 64)))) {
            s->output[s->fault] ^= 1;
        }
        qemu_mutex_lock(&s->lock);
        s->ready = true;
        qemu_cond_broadcast(&s->cond);
    }
    qemu_mutex_unlock(&s->lock);
    return NULL;
}

DspGainShadow *dsp_gain_shadow_new(unsigned mask, int fault)
{
    if (mask > 7 || fault < -1 || fault >= 192) {
        return NULL;
    }
    DspGainShadow *s = g_try_new0(DspGainShadow, 1);
    if (!s) {
        return NULL;
    }
    s->mask = mask;
    s->fault = fault;
    s->checksum = g_checksum_new(G_CHECKSUM_SHA256);
    if (!s->checksum) {
        g_free(s);
        return NULL;
    }
    qemu_mutex_init(&s->lock);
    qemu_cond_init(&s->cond);
    qemu_thread_create(&s->worker, "dsp-gain-shadow", gain_worker, s,
                       QEMU_THREAD_JOINABLE);
    return s;
}

void dsp_gain_shadow_free(DspGainShadow *s)
{
    if (!s) {
        return;
    }
    /* The owner must finish every admitted frame before teardown. */
    g_assert(!s->busy);
    qemu_mutex_lock(&s->lock);
    s->stop = true;
    qemu_cond_signal(&s->cond);
    qemu_mutex_unlock(&s->lock);
    qemu_thread_join(&s->worker);
    qemu_cond_destroy(&s->cond);
    qemu_mutex_destroy(&s->lock);
    g_checksum_free(s->checksum);
    g_free(s);
}

bool dsp_gain_shadow_submit(DspGainShadow *s, const uint32_t input[64])
{
    if (!s || !input || s->busy) {
        return false;
    }
    for (unsigned i = 0; i < 64; i++) {
        if (input[i] > 0xffffff) {
            return false;
        }
    }
    qemu_mutex_lock(&s->lock);
    memcpy(s->input, input, sizeof(s->input));
    memset(s->elapsed, 0, sizeof(s->elapsed));
    s->busy = true;
    s->ready = false;
    s->job = true;
    s->stats.admitted_jobs++;
    qemu_cond_signal(&s->cond);
    qemu_mutex_unlock(&s->lock);
    return true;
}

bool dsp_gain_shadow_begin(DspGainShadow *s, DSPState *gp, bool enabled)
{
    if (!s) {
        return false;
    }
    if (!enabled || !dsp_jit_capture(gp, &s->capture) ||
        s->capture.state.pc != 0 || s->capture.xram[0x3f0]) {
        s->stats.lle_only_frames++;
        return false;
    }
    static const uint8_t identity[32] = {
        0x41, 0xa1, 0x4a, 0xd0, 0xef, 0xf5, 0xce, 0x00, 0xe4, 0x5f, 0xb7,
        0x3c, 0x81, 0xf4, 0x97, 0xd1, 0xd1, 0x05, 0x6b, 0xf1, 0xff, 0xc6,
        0x30, 0x1b, 0x91, 0xb1, 0xd7, 0x8d, 0x56, 0x63, 0x77, 0x8a,
    };
    g_checksum_reset(s->checksum);
    for (unsigned i = 0; i < DSP_PRAM_SIZE; i++) {
        uint8_t word[4];
        stl_le_p(word, s->capture.pram[i]);
        g_checksum_update(s->checksum, word, sizeof(word));
    }
    uint8_t digest[32];
    gsize length = sizeof(digest);
    g_checksum_get_digest(s->checksum, digest, &length);
    if (memcmp(digest, identity, sizeof(digest)) ||
        !dsp_gain_shadow_submit(s, s->capture.xram + 0x100)) {
        s->stats.lle_only_frames++;
        return false;
    }
    return true;
}

void dsp_gain_shadow_finish(DspGainShadow *s, const uint32_t reference[192],
                            uint64_t reference_frame_wall_ns)
{
    g_assert(s && s->busy && reference);
    qemu_mutex_lock(&s->lock);
    while (!s->ready) {
        qemu_cond_wait(&s->cond, &s->lock);
    }
    s->busy = false;
    qemu_mutex_unlock(&s->lock);
    s->stats.reference_frame_wall_ns += reference_frame_wall_ns;
    for (unsigned route = 0; route < 3; route++) {
        DspGainRouteStats *r = &s->stats.route[route];
        if (!(s->mask & (1u << route))) {
            r->fallback_calls++;
            continue;
        }
        r->native_calls++;
        r->native_wall_ns += s->elapsed[route];
        unsigned differences = 0;
        for (unsigned i = 0; i < 64; i++) {
            uint32_t expected = reference[route * 64 + i];
            uint32_t actual = s->output[route * 64 + i];
            r->compared_words++;
            if (expected == actual) {
                r->matching_words++;
            } else {
                differences++;
                r->differing_words++;
                if (!r->first_valid) {
                    r->first_valid = true;
                    r->first_sample = i;
                    r->first_expected = expected;
                    r->first_actual = actual;
                }
            }
        }
        r->matched_calls += !differences;
        r->mismatched_calls += differences != 0;
    }
}

const DspGainShadowStats *dsp_gain_shadow_stats(const DspGainShadow *s)
{
    return s ? &s->stats : NULL;
}

void dsp_gain_shadow_report(const DspGainShadow *s, FILE *file)
{
    static const char *names[3] = { "gain_half", "invert_half",
                                    "gain_quarter" };
    uint64_t words = 0, differences = 0;
    for (unsigned i = 0; i < 3; i++) {
        words += s->stats.route[i].compared_words;
        differences += s->stats.route[i].differing_words;
    }
    fprintf(file,
            "{\"status\":\"%s\",\"authoritative\":\"dsp\","
            "\"live_native_commits\":0,\"route_mask\":%u,\"fault_index\":%d,"
            "\"admitted_jobs\":%" PRIu64 ",\"lle_only_frames\":%" PRIu64 ","
            "\"reference_frame_wall_ns\":%" PRIu64
            ",\"timing_comparable\":false,\"routes\":[",
            differences ? "MISMATCH" :
            words       ? "BASELINE_MATCH" :
                          "NO_COMPARISONS",
            s->mask, s->fault, s->stats.admitted_jobs, s->stats.lle_only_frames,
            s->stats.reference_frame_wall_ns);
    for (unsigned i = 0; i < 3; i++) {
        const DspGainRouteStats *r = &s->stats.route[i];
        fprintf(file,
                "%s{\"name\":\"%s\",\"native_calls\":%" PRIu64
                ",\"fallback_calls\":%" PRIu64 ",\"compared_words\":%" PRIu64
                ",\"matching_words\":%" PRIu64 ",\"differing_words\":%" PRIu64
                ",\"matched_calls\":%" PRIu64 ",\"mismatched_calls\":%" PRIu64
                ",\"native_wall_ns\":%" PRIu64 ",\"first_difference\":",
                i ? "," : "", names[i], r->native_calls, r->fallback_calls,
                r->compared_words, r->matching_words, r->differing_words,
                r->matched_calls, r->mismatched_calls, r->native_wall_ns);
        if (r->first_valid) {
            fprintf(file, "{\"sample\":%u,\"expected\":%u,\"actual\":%u}",
                    r->first_sample, r->first_expected, r->first_actual);
        } else {
            fprintf(file, "null");
        }
        fprintf(file, "}");
    }
    fprintf(file, "]}\n");
}

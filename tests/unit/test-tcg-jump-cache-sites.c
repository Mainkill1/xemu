/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "qemu/osdep.h"
#include "accel/tcg/jump-cache-probe-lookup.h"
#include "accel/tcg/jump-cache-sites.h"

static TCGSiteKey key(uint64_t pc)
{
    return (TCGSiteKey){ .pc = pc, .cs_base = 0x1000, .flags = 3, .cflags = 4 };
}

static TCGSiteEntry *observe(TCGJumpCacheProbe *p, TCGSiteKey target,
                             TCGJumpCacheProbeLookup result)
{
    TCGSiteObservation o = tcg_site_observe_begin(p, key(0x123), TCG_SITE_CALL);
    g_assert_nonnull(o.entry);
    tcg_site_observe_end(p, &o, target, result);
    return o.entry;
}

static void test_unattributed(void)
{
    TCGJumpCacheProbe *p = tcg_jump_cache_probe_new("sites");
    TCGJumpCacheProbe *counts = tcg_jump_cache_probe_new("counters");
    g_assert_false(tcg_site_enabled(NULL));
    g_assert_false(tcg_site_enabled(counts));
    g_assert_true(tcg_site_enabled(p));
    for (unsigned i = 0; i < TCG_SITE_PROFILE_CAPACITY + 1; i++) {
        TCGSiteObservation o =
            tcg_site_observe_begin(p, key(i), TCG_SITE_OTHER);
        g_assert_null(o.entry);
        tcg_site_observe_end(p, &o, key(i + 1), TCG_JUMP_CACHE_GLOBAL_HIT);
    }
    g_assert_cmpuint(p->owner.sites->used, ==, 0);
    g_assert_cmpuint(p->owner.sites->attempts[TCG_SITE_OTHER], ==,
                     TCG_SITE_PROFILE_CAPACITY + 1);
    g_assert_cmpuint(p->owner.sites->untracked[TCG_SITE_OTHER], ==,
                     TCG_SITE_PROFILE_CAPACITY + 1);
    g_assert_nonnull(observe(p, key(10), TCG_JUMP_CACHE_GLOBAL_HIT));
    tcg_jump_cache_probe_free(counts);
    tcg_jump_cache_probe_free(p);
}

static void test_locality(void)
{
    TCGJumpCacheProbe *p = tcg_jump_cache_probe_new("sites");
    g_assert_nonnull(p);
    g_assert_false(p->owner.timing);
    g_assert_null(p->owner.conflicts);
    TCGSiteEntry *e = observe(p, key(10), TCG_JUMP_CACHE_HIT);
    observe(p, key(10), TCG_JUMP_CACHE_HIT);
    observe(p, key(20), TCG_JUMP_CACHE_GLOBAL_HIT);
    observe(p, key(10), TCG_JUMP_CACHE_GLOBAL_HIT);
    observe(p, key(20), TCG_JUMP_CACHE_GLOBAL_HIT);
    g_assert_cmpuint(e->attempts, ==, 5);
    g_assert_cmpuint(e->recent_hits[0], ==, 1);
    g_assert_cmpuint(e->recent_hits[1], ==, 3);
    g_assert_cmpuint(e->global_recent_hits[0], ==, 0);
    g_assert_cmpuint(e->global_recent_hits[1], ==, 2);
    TCGSiteObservation ret =
        tcg_site_observe_begin(p, key(0x123), TCG_SITE_RETURN);
    g_assert_true(ret.entry != e);
    g_assert_cmpuint(ret.entry->attempts, ==, 1);
    /* No completion models a non-local exception/breakpoint exit. */
    g_assert_cmpuint(ret.entry->completed[0] + ret.entry->completed[1] +
                         ret.entry->completed[2],
                     ==, 0);
    tcg_jump_cache_probe_free(p);
}

static void test_full_key_and_epilogue(void)
{
    TCGJumpCacheProbe *p = tcg_jump_cache_probe_new("sites");
    TCGSiteKey k = key(10);
    TCGSiteEntry *e = observe(p, k, TCG_JUMP_CACHE_GLOBAL_MISS);
    g_assert_cmpuint(e->history_count, ==, 0);
    observe(p, k, TCG_JUMP_CACHE_GLOBAL_HIT);
    k.pc++;
    observe(p, k, TCG_JUMP_CACHE_HIT);
    k.cs_base++;
    observe(p, k, TCG_JUMP_CACHE_HIT);
    k.flags++;
    observe(p, k, TCG_JUMP_CACHE_HIT);
    k.cflags++;
    observe(p, k, TCG_JUMP_CACHE_HIT);
    g_assert_cmpuint(e->recent_hits[1], ==, 0);
    g_assert_cmpuint(e->completed[TCG_JUMP_CACHE_GLOBAL_MISS], ==, 1);
    tcg_jump_cache_probe_free(p);
}

static void test_invalidation(void)
{
    TCGJumpCacheProbe *p = tcg_jump_cache_probe_new("sites");
    TCGSiteEntry *e = observe(p, key(10), TCG_JUMP_CACHE_HIT);
    tcg_jump_cache_probe_invalidate_begin(p);
    tcg_jump_cache_probe_invalidate_end(p);
    observe(p, key(10), TCG_JUMP_CACHE_HIT);
    g_assert_cmpuint(e->resets, ==, 1);
    g_assert_cmpuint(e->recent_hits[0], ==, 0);
    TCGSiteObservation o = tcg_site_observe_begin(p, key(0x123), TCG_SITE_CALL);
    tcg_jump_cache_probe_invalidate_begin(p);
    tcg_jump_cache_probe_invalidate_end(p);
    tcg_site_observe_end(p, &o, key(10), TCG_JUMP_CACHE_GLOBAL_HIT);
    g_assert_cmpuint(e->excluded, ==, 1);
    g_assert_cmpuint(e->history_count, ==, 0);
    tcg_jump_cache_probe_invalidate_begin(p);
    o = tcg_site_observe_begin(p, key(0x123), TCG_SITE_CALL);
    tcg_jump_cache_probe_invalidate_end(p);
    tcg_site_observe_end(p, &o, key(10), TCG_JUMP_CACHE_HIT);
    g_assert_cmpuint(e->excluded, ==, 2);
    tcg_jump_cache_probe_free(p);
}

static void test_bound_and_format(void)
{
    TCGJumpCacheProbe *p = tcg_jump_cache_probe_new("sites");
    unsigned tracked = 0;
    for (unsigned i = 0; i < TCG_SITE_PROFILE_CAPACITY * 3; i++) {
        TCGSiteObservation o =
            tcg_site_observe_begin(p, key(i * 2), TCG_SITE_JUMP);
        tracked += o.entry != NULL;
    }
    g_assert_cmpuint(tracked, <=, TCG_SITE_PROFILE_CAPACITY);
    g_assert_cmpuint(p->owner.sites->used, ==, tracked);
    g_assert_cmpuint(p->owner.sites->untracked[TCG_SITE_JUMP] + tracked, ==,
                     TCG_SITE_PROFILE_CAPACITY * 3);
    g_autoptr(GString) text = g_string_new(NULL);
    tcg_site_format_owner(p, text, 0);
    g_assert_nonnull(strstr(text->str, "untracked="));
    g_assert_nonnull(strstr(text->str, "not executable-cache hits"));
    tcg_jump_cache_probe_free(p);
}

static void test_returns_nested(void)
{
    TCGJumpCacheProbe *p = tcg_jump_cache_probe_new("returns");
    g_assert_nonnull(p);
    g_assert_true(tcg_site_enabled(p));
    tcg_return_observe(p, key(10), 0xfffc, TCG_RETURN_DIRECT_CALL);
    tcg_return_observe(p, key(20), 0xfff8, TCG_RETURN_INDIRECT_CALL);
    tcg_return_observe(p, key(20), 0xfff8, TCG_RETURN_POP);
    tcg_return_observe(p, key(10), 0xfffc, TCG_RETURN_POP);
    TCGReturnProfile *r = &p->owner.sites->returns;
    g_assert_cmpuint(r->calls[0], ==, 1);
    g_assert_cmpuint(r->calls[1], ==, 1);
    g_assert_cmpuint(r->pops, ==, 2);
    g_assert_cmpuint(r->matched, ==, 2);
    g_assert_cmpuint(r->context_matched, ==, 2);
    g_assert_cmpuint(r->epoch_stable, ==, 2);
    g_assert_cmpuint(r->count, ==, 0);
    g_assert_cmpuint(r->peak, ==, 2);
    g_autoptr(GString) text = g_string_new(NULL);
    tcg_site_format_owner(p, text, 0);
    g_assert_nonnull(strstr(text->str, "returns depth=64"));
    g_assert_nonnull(strstr(text->str, "not executable-target availability"));
    tcg_jump_cache_probe_free(p);
}

static void test_returns_mismatch(void)
{
    TCGJumpCacheProbe *p = tcg_jump_cache_probe_new("returns");
    tcg_return_observe(p, key(10), 4, TCG_RETURN_POP);
    tcg_return_observe(p, key(10), 4, TCG_RETURN_DIRECT_CALL);
    tcg_return_observe(p, key(20), 0, TCG_RETURN_DIRECT_CALL);
    tcg_return_observe(p, key(20), 4, TCG_RETURN_POP);
    TCGReturnProfile *r = &p->owner.sites->returns;
    g_assert_cmpuint(r->underflow, ==, 1);
    g_assert_cmpuint(r->mismatch, ==, 1);
    g_assert_cmpuint(r->count, ==, 0);
    tcg_return_observe(p, key(10), 4, TCG_RETURN_DIRECT_CALL);
    tcg_return_observe(p, key(11), 4, TCG_RETURN_POP);
    g_assert_cmpuint(r->mismatch, ==, 2);
    g_assert_cmpuint(r->matched, ==, 0);
    tcg_jump_cache_probe_free(p);
}

static void test_returns_bounds(void)
{
    TCGJumpCacheProbe *p = tcg_jump_cache_probe_new("returns");
    for (unsigned i = 0; i < TCG_RETURN_PROFILE_DEPTH + 3; i++) {
        tcg_return_observe(p, key(i), UINT32_MAX - i * 4,
                           TCG_RETURN_DIRECT_CALL);
    }
    TCGReturnProfile *r = &p->owner.sites->returns;
    g_assert_cmpuint(r->overflow, ==, 3);
    g_assert_cmpuint(r->peak, ==, TCG_RETURN_PROFILE_DEPTH);
    for (unsigned i = TCG_RETURN_PROFILE_DEPTH + 3; i > 3; i--) {
        tcg_return_observe(p, key(i - 1), UINT32_MAX - (i - 1) * 4,
                           TCG_RETURN_POP);
    }
    g_assert_cmpuint(r->matched, ==, TCG_RETURN_PROFILE_DEPTH);
    g_assert_cmpuint(r->count, ==, 0);
    tcg_return_observe(p, key(2), UINT32_MAX - 8, TCG_RETURN_POP);
    g_assert_cmpuint(r->underflow, ==, 1);
    tcg_jump_cache_probe_free(p);
}

static void test_returns_context_epoch(void)
{
    TCGJumpCacheProbe *p = tcg_jump_cache_probe_new("returns");
    TCGJumpCacheProbe *sites = tcg_jump_cache_probe_new("sites");
    tcg_return_observe(NULL, key(10), 4, TCG_RETURN_DIRECT_CALL);
    tcg_return_observe(sites, key(10), 4, TCG_RETURN_DIRECT_CALL);
    g_assert_cmpuint(sites->owner.sites->returns.count, ==, 0);
    TCGReturnProfile *r = &p->owner.sites->returns;
    for (unsigned i = 0; i < 3; i++) {
        TCGSiteKey k = key(10);
        tcg_return_observe(p, k, 4, TCG_RETURN_DIRECT_CALL);
        if (i == 0) {
            k.cs_base++;
        }
        if (i == 1) {
            k.flags++;
        }
        if (i == 2) {
            k.cflags++;
        }
        tcg_return_observe(p, k, 4, TCG_RETURN_POP);
    }
    g_assert_cmpuint(r->matched, ==, 3);
    g_assert_cmpuint(r->context_matched, ==, 0);
    tcg_return_observe(p, key(10), 4, TCG_RETURN_DIRECT_CALL);
    tcg_jump_cache_probe_invalidate_begin(p);
    tcg_jump_cache_probe_invalidate_end(p);
    tcg_return_observe(p, key(10), 4, TCG_RETURN_POP);
    tcg_jump_cache_probe_invalidate_begin(p);
    tcg_return_observe(p, key(10), 4, TCG_RETURN_DIRECT_CALL);
    tcg_jump_cache_probe_invalidate_end(p);
    tcg_return_observe(p, key(10), 4, TCG_RETURN_POP);
    tcg_return_observe(p, key(10), 4, TCG_RETURN_DIRECT_CALL);
    tcg_jump_cache_probe_invalidate_begin(p);
    tcg_return_observe(p, key(10), 4, TCG_RETURN_POP);
    tcg_jump_cache_probe_invalidate_end(p);
    g_assert_cmpuint(r->context_matched, ==, 3);
    g_assert_cmpuint(r->epoch_stable, ==, 0);
    tcg_return_observe(p, key(10), 4, TCG_RETURN_DIRECT_CALL);
    tcg_return_observe(p, key(10), 4, TCG_RETURN_POP);
    g_assert_cmpuint(r->epoch_stable, ==, 1);
    tcg_jump_cache_probe_free(sites);
    tcg_jump_cache_probe_free(p);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/sites/unattributed", test_unattributed);
    g_test_add_func("/sites/locality", test_locality);
    g_test_add_func("/sites/full-key-epilogue", test_full_key_and_epilogue);
    g_test_add_func("/sites/invalidation", test_invalidation);
    g_test_add_func("/sites/bound-format", test_bound_and_format);
    g_test_add_func("/returns/nested", test_returns_nested);
    g_test_add_func("/returns/mismatch", test_returns_mismatch);
    g_test_add_func("/returns/bounds", test_returns_bounds);
    g_test_add_func("/returns/context-epoch", test_returns_context_epoch);
    return g_test_run();
}

#include "qemu/osdep.h"
#include "hw/xbox/nv2a/debug.h"
#include "hw/xbox/nv2a/pgraph/pgraph.h"
#include "ui/xui/shader-browser-draw-request.h"
#include "hw/xbox/nv2a/pgraph/glsl/shader-browser-observation.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static bool collection_enabled;
static XemuShaderBrowserObservation published[16];
static size_t published_count;
static uint64_t published_epochs[16];
static uint64_t live_epoch = 1;
static uint64_t last_frame;

int xemu_shader_browser_session_collection_enabled(void)
{
    return collection_enabled;
}

void xemu_shader_browser_publish_observations(
    const XemuShaderBrowserObservation *observations, size_t count)
{
    assert(published_count + count <= 16);
    memcpy(published + published_count, observations,
           count * sizeof(*observations));
    for (size_t i = 0; i < count; ++i) {
        published_epochs[published_count + i] = live_epoch;
    }
    published_count += count;
}

void xemu_shader_browser_publish_frame(uint64_t frame)
{
    last_frame = frame;
}

static bool capture_armed;
static unsigned claim_calls, finish_calls;
static uint64_t claimed_submission, finished_token;
static int finished_emitted;

int xemu_shader_draw_request_is_armed(void)
{
    return capture_armed;
}

uint64_t nv2a_profile_preview_renderer_epoch(void)
{
    return 7;
}

uint64_t xemu_shader_draw_request_claim(
    uint64_t scope_generation, uint64_t renderer_epoch,
    const XemuShaderDrawIdentity *identities, size_t count, uint64_t frame,
    uint32_t draw, uint64_t submission)
{
    assert(scope_generation == 6 && renderer_epoch == 7);
    assert(count == 2 &&
           identities[1].stage == XEMU_SHADER_BROWSER_STAGE_PIXEL);
    assert(identities[1].identity_hash[0] == 2);
    assert(frame == 40 && draw == 50);
    claimed_submission = submission;
    ++claim_calls;
    return 123;
}

int xemu_shader_draw_request_finish(uint64_t token, int emitted,
                                    uint32_t primitive_mode,
                                    uint32_t vertex_count, uint32_t index_count)
{
    assert(primitive_mode == 5 && vertex_count == 4 && index_count == 3);
    finished_token = token;
    finished_emitted = emitted;
    ++finish_calls;
    return 1;
}

static void test_capture_emission_bridge(PGRAPHShaderBrowserBinding *binding)
{
    static PGRAPHState pg;
    pg.frame_time = 40;
    pg.draw_time = 50;
    pg.primitive_mode = 5;
    pg.shader_browser_submission = 60;
    binding->count = 2;
    assert(!pgraph_shader_browser_capture_claim(&pg, binding));
    assert(!claim_calls);
    capture_armed = true;
    assert(!pgraph_shader_browser_capture_claim(&pg, NULL));
    binding->count = ARRAY_SIZE(binding->identities) + 1;
    assert(!pgraph_shader_browser_capture_claim(&pg, binding));
    assert(!claim_calls);
    binding->count = 2;
    uint64_t token = pgraph_shader_browser_capture_claim(&pg, binding);
    assert(token == 123 && claim_calls == 1 && claimed_submission == 61);
    pgraph_shader_browser_capture_finish(&pg, token, false, 4, 3);
    assert(pg.shader_browser_submission == 60 && finished_token == token);
    assert(finish_calls == 1 && !finished_emitted);
    pgraph_shader_browser_capture_finish(&pg, token, true, 4, 3);
    assert(pg.shader_browser_submission == 61 && finish_calls == 2);
    assert(finished_token == token && finished_emitted);
    pgraph_shader_browser_capture_finish(&pg, 0, true, 4, 3);
    assert(pg.shader_browser_submission == 62 && finish_calls == 2);
}

int main(void)
{
    PGRAPHShaderBrowserObservations batch = { 0 };
    PGRAPHShaderBrowserBinding binding = { 0 };
    binding.scope_generation = 4;
    binding.count = 2;
    binding.identities[0].stage = XEMU_SHADER_BROWSER_STAGE_FIXED_FUNCTION;
    binding.identities[0].hash[0] = 1;
    binding.identities[1].stage = XEMU_SHADER_BROWSER_STAGE_PIXEL;
    binding.identities[1].hash[0] = 2;
    binding.compile_cpu_ns = 100000;
    binding.prepare_cpu_ns = 20000;
    binding.timings_pending = true;

    collection_enabled = true;
    pgraph_shader_browser_record_draw(&batch, &binding, 10,
                                      XEMU_SHADER_BROWSER_ROUTE_UBER);
    pgraph_shader_browser_record_draw(&batch, &binding, 11,
                                      XEMU_SHADER_BROWSER_ROUTE_UBER);
    assert(batch.used == 2);
    pgraph_shader_browser_flush_observations(&batch, 12);
    assert(last_frame == 12 && published_count == 2);
    assert(published[0].stage == XEMU_SHADER_BROWSER_STAGE_FIXED_FUNCTION);
    assert(published[0].route == XEMU_SHADER_BROWSER_ROUTE_FIXED_FUNCTION);
    assert(published[0].draw_count_delta == 2);
    assert(published[0].first_frame == 10 && published[0].last_frame == 11);
    assert(published[1].stage == XEMU_SHADER_BROWSER_STAGE_PIXEL);
    assert(published[1].route == XEMU_SHADER_BROWSER_ROUTE_UBER);
    assert(published[1].uber_draw_delta == 2);
    assert(published[1].compile_cpu.sample_count == 1);
    assert(published[1].compile_cpu.total_ns == 100000);
    assert(published[1].prepare_cpu.total_ns == 20000);
    assert(!binding.timings_pending);

    pgraph_shader_browser_record_draw(&batch, &binding, 13,
                                      XEMU_SHADER_BROWSER_ROUTE_SPECIALIZED);
    assert(batch.used == 2);
    binding.scope_generation = 5;
    pgraph_shader_browser_record_draw(&batch, &binding, 14,
                                      XEMU_SHADER_BROWSER_ROUTE_SPECIALIZED);
    assert(published_count == 4 && batch.used == 2);
    collection_enabled = false;
    pgraph_shader_browser_flush_observations(&batch, 15);
    assert(published_count == 6 && !batch.collecting);
    pgraph_shader_browser_record_draw(&batch, &binding, 16,
                                      XEMU_SHADER_BROWSER_ROUTE_UBER);
    assert(batch.used == 0);

    // Clear before the next flip: draining at the transition keeps buffered
    // old draws in the old epoch, while the next draw enters the new epoch.
    memset(&batch, 0, sizeof(batch));
    published_count = 0;
    collection_enabled = true;
    binding.count = 1;
    binding.scope_generation = 6;
    for (int i = 0; i < 10; ++i) {
        pgraph_shader_browser_record_draw(&batch, &binding, 20 + i,
                                          XEMU_SHADER_BROWSER_ROUTE_SPECIALIZED);
    }
    assert(batch.used == 1 && published_count == 0);
    pgraph_shader_browser_flush_observations(&batch, 30);
    ++live_epoch;
    pgraph_shader_browser_flush_observations(&batch, 30);
    assert(published_count == 1 && published_epochs[0] == 1);
    assert(published[0].draw_count_delta == 10);
    pgraph_shader_browser_record_draw(&batch, &binding, 31,
                                      XEMU_SHADER_BROWSER_ROUTE_SPECIALIZED);
    pgraph_shader_browser_flush_observations(&batch, 32);
    assert(published_count == 2 && published_epochs[1] == 2);
    assert(published[1].draw_count_delta == 1);
    assert(published[0].draw_count_delta + published[1].draw_count_delta == 11);
    test_capture_emission_bridge(&binding);
    puts("1..3\nok 1 - bounded renderer observations track routes and scope\n"
         "ok 2 - live clear drains unflushed draws before epoch change\n"
         "ok 3 - capture bridge carries exact tokens and emission serials");
    return 0;
}

#include "hw/xbox/nv2a/pgraph/glsl/shader-browser-observation.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static bool collection_enabled;
static bool monitoring_enabled = true;
static XemuShaderBrowserObservation published[16];
static size_t published_count;
static uint64_t published_epochs[16];
static uint64_t live_epoch = 1;
static uint64_t last_frame;
static XemuShaderBrowserProfilingConfig profiling = {
    .monitoring_level = XEMU_SHADER_BROWSER_MONITOR_OFF,
    .draw_sample_interval = 2,
    .max_gpu_samples_per_frame = 1,
};

void xemu_shader_browser_copy_profiling_config(
    XemuShaderBrowserProfilingConfig *config)
{
    *config = profiling;
}

int xemu_shader_browser_session_collection_enabled(void)
{
    return collection_enabled;
}

int xemu_shader_browser_monitoring_enabled(void)
{
    return monitoring_enabled;
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

int main(void)
{
    PGRAPHShaderBrowserSampler sampler = { 0 };
    PGRAPHShaderBrowserSampleDecision choice =
        pgraph_shader_browser_choose_sample(&sampler, 1, true);
    assert(!choice.cpu && !choice.gpu && sampler.eligible_draws == 0);
    profiling.monitoring_level = XEMU_SHADER_BROWSER_MONITOR_DIAGNOSTIC;
    profiling.cpu_timing = 1;
    profiling.gpu_timing = 1;
    choice = pgraph_shader_browser_choose_sample(&sampler, 1, true);
    assert(!choice.cpu && !choice.gpu);
    choice = pgraph_shader_browser_choose_sample(&sampler, 1, true);
    assert(choice.cpu && choice.gpu);
    pgraph_shader_browser_choose_sample(&sampler, 1, true);
    choice = pgraph_shader_browser_choose_sample(&sampler, 1, true);
    assert(choice.cpu && !choice.gpu);
    pgraph_shader_browser_choose_sample(&sampler, 2, true);
    choice = pgraph_shader_browser_choose_sample(&sampler, 2, true);
    assert(choice.cpu && choice.gpu);

    PGRAPHShaderBrowserObservations batch = { 0 };
    PGRAPHShaderBrowserBinding binding = { 0 };
    binding.scope_generation = 4;
    binding.count = 2;
    binding.identities[0].stage = XEMU_SHADER_BROWSER_STAGE_FIXED_FUNCTION;
    binding.identities[0].hash[0] = 1;
    binding.identities[1].stage = XEMU_SHADER_BROWSER_STAGE_PIXEL;
    binding.identities[1].hash[0] = 2;

    collection_enabled = true;
    monitoring_enabled = false;
    pgraph_shader_browser_record_draw(&batch, &binding, 9,
                                      XEMU_SHADER_BROWSER_ROUTE_UBER);
    assert(batch.used == 0 && batch.draw_poll_count == 0);
    monitoring_enabled = true;
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
    assert(published[1].compile_cpu.sample_count == 0);
    assert(published[1].prepare_cpu.sample_count == 0);

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
    puts("1..2\nok 1 - bounded renderer observations track routes and scope\n"
         "ok 2 - live clear drains unflushed draws before epoch change");
    return 0;
}

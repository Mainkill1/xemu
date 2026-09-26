#include "hw/xbox/nv2a/pgraph/glsl/shader-browser-observation.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static bool collection_enabled;
static XemuShaderBrowserObservation published[16];
static size_t published_count;
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
    published_count += count;
}

void xemu_shader_browser_publish_frame(uint64_t frame)
{
    last_frame = frame;
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
    puts("1..1\nok 1 - bounded renderer observations track routes and scope");
    return 0;
}

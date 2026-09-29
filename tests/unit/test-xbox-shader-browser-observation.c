#include "qemu/osdep.h"
#include "hw/xbox/nv2a/debug.h"
#include "hw/xbox/nv2a/nv2a.h"
#include "hw/xbox/nv2a/pgraph/pgraph.h"
#include "ui/xui/shader-browser-draw-request.h"
#include "hw/xbox/nv2a/pgraph/glsl/shader-browser-observation.h"
#include "hw/xbox/nv2a/pgraph/vk/shader-timing-policy.h"

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

static bool capture_armed;
static bool capture_session_active;
static unsigned claim_calls, finish_calls;
static uint64_t claimed_submission, finished_token;
static int finished_emitted;
static unsigned session_claim_calls, captured_bank_count, captured_abi_count;
static uint32_t captured_bank_words[6];
static size_t captured_bank_sizes[6];
static const uint64_t session_token = UINT64_C(0x800000000000007b);
static uint8_t display_palette[256 * 3], captured_palette[256 * 3];
static unsigned palette_copy_calls, palette_capture_calls;

/* The isolated bridge test has no realized NV2A display device. */
void nv2a_copy_dac_palette(uint8_t destination[256 * 3])
{
    memcpy(destination, display_palette, sizeof(display_palette));
    ++palette_copy_calls;
}

int xemu_shader_capture_session_active(void)
{
    return capture_session_active;
}

uint64_t xemu_shader_browser_scope_generation(void)
{
    return 6;
}

uint64_t xemu_shader_capture_session_begin_event(
    uint32_t kind, uint64_t frame, uint32_t draw, uint64_t submission,
    uint64_t scope_generation, uint64_t renderer_epoch)
{
    assert(capture_session_active && kind == XEMU_SHADER_CAPTURE_DRAW);
    assert(frame == 40 && draw == 50 && submission == 0);
    assert(scope_generation == 6 && renderer_epoch == 7);
    ++session_claim_calls;
    return session_token;
}

int xemu_shader_draw_request_wants_inputs(uint64_t token)
{
    return token == session_token;
}

int xemu_shader_draw_request_stage_blob(uint64_t token,
                                        const XemuShaderDrawBlob *blob)
{
    if (!strcmp(blob->name, "display.dac_palette")) {
        assert(token == session_token && blob->data);
        assert(blob->byte_count == sizeof(captured_palette));
        memcpy(captured_palette, blob->data, sizeof(captured_palette));
        ++palette_capture_calls;
        return 1;
    }
    static const char *const names[] = {
        "pgraph.registers", "pgraph.vertex_program", "pgraph.vertex_constants",
        "pgraph.lighting_a", "pgraph.lighting_b", "pgraph.lighting_c",
    };
    assert(token == session_token && captured_bank_count < ARRAY_SIZE(names));
    assert(!strcmp(blob->name, names[captured_bank_count]));
    assert(blob->data && blob->byte_count >= sizeof(uint32_t));
    memcpy(&captured_bank_words[captured_bank_count], blob->data,
           sizeof(uint32_t));
    captured_bank_sizes[captured_bank_count++] = blob->byte_count;
    return 1;
}

int xemu_shader_draw_request_stage_register(uint64_t token, const char *name,
                                           uint32_t value)
{
    assert(token == session_token && value == 1);
    assert(!strcmp(name, "capture.generator_abi") ||
           !strcmp(name, "capture.interface_abi"));
    ++captured_abi_count;
    return 1;
}

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

static void test_capture_session_without_shader_binding(void)
{
    static PGRAPHState pg;
    pg.frame_time = 40;
    pg.draw_time = 50;
    pg.primitive_mode = 5;
    pg.shader_browser_submission = 60;
    pg.regs_[0] = 0x12345678;
    pg.program_data[0][0] = 0x23456789;
    pg.vsh_constants[0][0] = 0x3456789a;
    pg.ltctxa[0][0] = 0x456789ab;
    pg.ltctxb[0][0] = 0x56789abc;
    pg.ltc1[0][0] = 0x6789abcd;
    capture_session_active = true;

    /* A rejected guest draw has raw state even when no shader could be bound.
     * Its event has no backend submission, and finishing it must not invent one.
     */
    uint64_t token = pgraph_shader_browser_capture_claim(&pg, NULL);
    assert(token == session_token && session_claim_calls == 1);
    assert(claim_calls == 1 && captured_bank_count == 6);
    assert(captured_abi_count == 2);
    assert(captured_bank_sizes[0] == sizeof(pg.regs_));
    assert(captured_bank_sizes[1] == sizeof(pg.program_data));
    assert(captured_bank_sizes[2] == sizeof(pg.vsh_constants));
    assert(captured_bank_sizes[3] == sizeof(pg.ltctxa));
    assert(captured_bank_sizes[4] == sizeof(pg.ltctxb));
    assert(captured_bank_sizes[5] == sizeof(pg.ltc1));
    for (unsigned i = 0; i < ARRAY_SIZE(captured_bank_words); ++i) {
        assert(captured_bank_words[i] == UINT32_C(0x12345678) +
                                          i * UINT32_C(0x11111111));
    }
    pg.regs_[0] = 0;
    assert(captured_bank_words[0] == 0x12345678);
    pgraph_shader_browser_capture_finish(&pg, token, false, 4, 3);
    assert(pg.shader_browser_submission == 60 && finish_calls == 3);
    assert(finished_token == session_token && !finished_emitted);
    capture_session_active = false;
}

static void test_capture_display_palette(void)
{
    ShaderState state = { 0 };
    PGRAPHShaderBrowserBinding binding = { 0 };
    for (size_t i = 0; i < sizeof(display_palette); ++i)
        display_palette[i] = (uint8_t)(i * 17);
    pgraph_shader_browser_capture_recipes(0, &state, &binding);
    assert(!palette_copy_calls && !palette_capture_calls);
    pgraph_shader_browser_capture_recipes(session_token, &state, &binding);
    assert(palette_copy_calls == 1 && palette_capture_calls == 1);
    assert(!memcmp(captured_palette, display_palette, sizeof(display_palette)));
    memset(display_palette, 0xff, sizeof(display_palette));
    assert(captured_palette[0] == 0 && captured_palette[1] == 17);
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

    /* A first draw may be eligible before its command buffer exists. The
     * query pool becomes ready during begin, before timestamp recording. */
    profiling.draw_sample_interval = 1;
    PGRAPHShaderBrowserSampler first_draw = { 0 };
    choice = pgraph_vk_shader_timing_choose_sample(&first_draw, 3, true, false,
                                                   false);
    assert(choice.gpu);
    PGRAPHVkShaderTimingInput timing = {
        .requested = choice.gpu,
        .query_pool_ready = true,
        .pipeline_ready = true,
        .shader_ready = true,
        .capacity = 1,
    };
    assert(pgraph_vk_shader_timing_decide(&timing) ==
           PGRAPH_VK_SHADER_TIMING_RECORD);
    PGRAPHShaderBrowserSampler unreset_buffer = { 0 };
    choice = pgraph_vk_shader_timing_choose_sample(&unreset_buffer, 3, true,
                                                   true, false);
    assert(!choice.gpu);

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
    test_capture_emission_bridge(&binding);
    test_capture_session_without_shader_binding();
    test_capture_display_palette();
    puts("1..5\nok 1 - bounded renderer observations track routes and scope\n"
         "ok 2 - live clear drains unflushed draws before epoch change\n"
         "ok 3 - capture bridge carries exact tokens and emission serials\n"
         "ok 4 - rejected session draws retain raw banks without submissions\n"
         "ok 5 - capture recipes retain an owned display palette");
    return 0;
}

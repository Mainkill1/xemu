// SPDX-License-Identifier: GPL-2.0-or-later
#include "shader-browser-capture-session.hh"
#include <glib.h>
#include <cstring>
extern "C" void xemu_test_texture_reuse_start();
extern "C" void xemu_test_texture_reuse_stage(uint64_t, uint64_t, uint64_t,
                                              uint64_t, uint16_t, uint32_t,
                                              uint32_t);
extern "C" void xemu_test_texture_reuse_retire(uint64_t[5], bool);
extern "C" void xemu_test_texture_reuse_modify(unsigned);
extern "C" uint64_t xemu_test_texture_reuse_pending();
using namespace xemu::shader_browser;
static void Run(unsigned mode, uint32_t mips = 1, uint32_t faces = 1)
{
    auto &session = GetCaptureSession();
    CaptureSessionContext context;
    context.scope.title_id = 17;
    context.scope_generation = 1;
    context.session_epoch = 2;
    context.renderer_epoch = 3;
    context.generation = 4;
    g_assert_true(session.Start(context));
    session.GuestFrameBoundary(7);
    xemu_test_texture_reuse_start();
    uint64_t tokens[3]{};
    for (unsigned i = 0; i < 3; ++i) {
        DrawCaptureSummary summary;
        summary.scope = context.scope;
        summary.key = { 2, 3, 7, i + 1, i + 1 };
        summary.shader_count = 1;
        summary.shaders[0].stage = Stage::Pixel;
        tokens[i] = session.BeginOccurrence(summary);
        g_assert_cmpuint(tokens[i], !=, 0);
        const bool changed = i == 2;
        xemu_test_texture_reuse_modify(mode == 10 && i == 1  ? 1 :
                                       mode == 11 && changed ? 2 :
                                       mode == 12 && changed ? 4 :
                                                               0);
        xemu_test_texture_reuse_stage(tokens[i],
                                      mode == 6            ? 0 :
                                      changed && mode == 2 ? 102 :
                                                             101,
                                      mode == 5            ? 0 :
                                      mode == 9            ? UINT64_MAX :
                                      changed && mode == 0 ? 2 :
                                                             1,
                                      mode == 7            ? 0 :
                                      changed && mode == 3 ? 12 :
                                                             11,
                                      changed && mode == 0 ? 0xa000 : 0x8000,
                                      mips, faces);
        g_assert_true(session.Finish(tokens[i], true, 5, 3, 0));
    }
    if (mode == 4)
        session.Invalidate(2, 3, 4);
    uint64_t stats[5]{};
    xemu_test_texture_reuse_retire(stats, mode == 8);
    const unsigned versions =
        (mode >= 5 && mode <= 7) || mode == 9             ? 3 :
        mode == 0 || mode == 2 || mode == 3 || mode == 11 ? 2 :
                                                            1;
    g_assert_cmpuint(stats[0], ==, versions * mips * faces);
    g_assert_cmpuint(stats[1], ==, stats[0]);
    g_assert_cmpuint(stats[2], ==, mode == 4 ? 0 : stats[0]);
    g_assert_cmpuint(stats[3], ==, stats[0]);
    g_assert_cmpuint(stats[4], ==, 0);
    if (mode != 4) {
        const auto snapshot = session.Snapshot();
        if (mode == 12)
            g_assert_true(snapshot.state ==
                          CaptureSessionState::BudgetExceeded);
        std::vector<std::shared_ptr<const CaptureOccurrence>> events;
        for (const auto &event : snapshot.events)
            if (event->type == CaptureEventType::Draw)
                events.push_back(event);
        g_assert_cmpuint(events.size(), ==, 3);
        for (unsigned i = 0; i < 3; ++i) {
            const auto &event = events[i];
            g_assert_true(event->inputs.complete);
            const unsigned slot = mode == 10 && i == 1 ? 1 : 0;
            const auto &texture = event->inputs.textures[slot];
            g_assert_cmpuint(texture.metadata.slot, ==, slot);
            g_assert_cmpuint(texture.metadata.min_filter, ==, slot ? 1 : 0);
            if (mode == 12 && i == 2) {
                g_assert_true(event->limitations & CaptureReadbackFailed);
                g_assert_false(event->failure.empty());
                g_assert_true(texture.images.empty());
                continue;
            }
            g_assert_cmpuint(texture.images.size(), ==, mips * faces);
            for (const auto &image : texture.images) {
                const uint16_t sample =
                    (i == 2 && mode == 0 ? 0xa000 : 0x8000) +
                    image.face * 0x500 + image.mip_level * 0x300;
                g_assert_nonnull(image.image.rgba.get());
                g_assert_cmpuint(image.image.rgba->bytes[0], ==,
                                 mode == 11 && i == 2 ?
                                     0 :
                                     (uint32_t(sample) * 255 + 32767) / 65535);
            }
            size_t raw_blocks = 0;
            for (const auto &blob : event->inputs.blobs) {
                if (blob.name.rfind("texture.storage.", 0) != 0)
                    continue;
                ++raw_blocks;
                uint16_t value = 0;
                g_assert_nonnull(blob.data.get());
                std::memcpy(&value, blob.data->bytes.data(), sizeof(value));
                const uint16_t expected =
                    (i == 2 && mode == 0 ? 0xa000 : 0x8000);
                g_assert_cmpuint(value, ==, expected);
            }
            g_assert_cmpuint(raw_blocks, ==, faces == 1 ? 1 : 0);
        }
    }
    session.Stop();
}
int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, nullptr);
    g_test_add_func("/vk/texture-reuse/overwritten-generation", [] { Run(0); });
    g_test_add_func("/vk/texture-reuse/unchanged-generation", [] { Run(1); });
    g_test_add_func("/vk/texture-reuse/allocation-incarnation", [] { Run(2); });
    g_test_add_func("/vk/texture-reuse/capture-batch", [] { Run(3); });
    g_test_add_func("/vk/texture-reuse/cancelled", [] { Run(4); });
    g_test_add_func("/vk/texture-reuse/mip-and-face", [] { Run(1, 2, 6); });
    g_test_add_func("/vk/texture-reuse/unknown-version", [] { Run(5); });
    g_test_add_func("/vk/texture-reuse/unknown-owner", [] { Run(6); });
    g_test_add_func("/vk/texture-reuse/unknown-batch", [] { Run(7); });
    g_test_add_func("/vk/texture-reuse/owner-retires-first", [] { Run(8); });
    g_test_add_func("/vk/texture-reuse/saturated-version", [] { Run(9); });
    g_test_add_func("/vk/texture-reuse/consumer-slot-and-sampler",
                    [] { Run(10); });
    g_test_add_func("/vk/texture-reuse/component-mapping", [] { Run(11); });
    g_test_add_func("/vk/texture-reuse/consumer-budget", [] { Run(12); });
    g_test_add_func("/vk/texture-reuse/pending-consumer-bytes", [] {
        auto &session = GetCaptureSession();
        CaptureSessionContext context;
        context.scope.title_id = 17;
        context.scope_generation = 1;
        context.session_epoch = 2;
        context.renderer_epoch = 3;
        context.generation = 4;
        g_assert_true(session.Start(context));
        session.GuestFrameBoundary(7);
        xemu_test_texture_reuse_start();
        xemu_test_texture_reuse_modify(8);
        for (unsigned i = 0; i < 6; ++i) {
            DrawCaptureSummary summary;
            summary.scope = context.scope;
            summary.key = { 2, 3, 7, i + 1, i + 1 };
            auto token = session.BeginOccurrence(summary);
            g_assert_cmpuint(token, !=, 0);
            xemu_test_texture_reuse_stage(token, 101, 1, 11, 0x8000, 1, 1);
            g_assert_true(session.Finish(token, true, 5, 3, 0));
            g_assert_cmpuint(xemu_test_texture_reuse_pending(), ==,
                             (i + 1) * 24U * 1024U * 1024U);
        }
        uint64_t stats[5]{};
        xemu_test_texture_reuse_retire(stats, true);
        g_assert_cmpuint(stats[0], ==, 1);
        g_assert_cmpuint(stats[1], ==, stats[3]);
        g_assert_cmpuint(stats[4], ==, 0);
        const auto snapshot = session.Snapshot();
        g_assert_cmpuint(snapshot.pending_events, ==, 0);
        g_assert_true(snapshot.state != CaptureSessionState::BudgetExceeded);
        session.Stop();
    });
    return g_test_run();
}

// SPDX-License-Identifier: GPL-2.0-or-later
#include "shader-browser-capture-session.hh"
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <cstdlib>
extern "C" bool xemu_test_depth_rejected(uint32_t, uint32_t);
extern "C" bool xemu_test_stage_y16(uint64_t, uint8_t *, size_t);
using namespace xemu::shader_browser;
int main()
{
    if (xemu_test_depth_rejected(0x30, 70)) {
        std::fputs("Sampled guest Y16 / host R16_UNORM rejected\n", stderr);
        return 1;
    }
    if (!xemu_test_depth_rejected(0x30, 124))
        std::abort(); // true D16
    auto &session = GetCaptureSession();
    CaptureSessionContext context;
    context.scope.title_id = 17;
    context.scope_generation = 1;
    context.session_epoch = 2;
    context.renderer_epoch = 3;
    context.generation = 4;
    if (!session.Start(context))
        std::abort();
    session.GuestFrameBoundary(7);
    DrawCaptureSummary summary;
    summary.scope = context.scope;
    summary.key = { 2, 3, 7, 1, 1 };
    summary.shader_count = 1;
    summary.shaders[0].stage = Stage::Pixel;
    const auto token = session.BeginOccurrence(summary);
    uint8_t raw[] = { 0, 0x80, 1, 0x80, 0xff, 0xff, 0, 0 };
    if (!token || !xemu_test_stage_y16(token, raw, sizeof(raw)))
        std::abort();
    std::memset(raw, 0, sizeof(raw));
    session.Finish(token, true, 5, 3, 0);
    session.InputsComplete(token);
    const auto snap = session.Snapshot();
    if (snap.events.empty())
        std::abort();
    const auto inputs = snap.events.back()->CopyInputs();
    if (inputs.blobs.size() != 1 || inputs.blobs[0].bytes[3] != 0x80 ||
        inputs.blobs[0].bytes[2] != 1 || inputs.blobs[0].format != 70)
        std::abort();
    session.Stop();
    std::puts("Native sampled-depth admission and owned R16 snapshot PASS");
}

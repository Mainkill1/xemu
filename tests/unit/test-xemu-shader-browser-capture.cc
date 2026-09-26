// SPDX-License-Identifier: GPL-2.0-or-later
#include "ui/xui/shader-browser-capture-bridge.h"
#include "ui/xui/shader-browser-capture.hh"

#include <cassert>
#include <cstring>
#include <chrono>
#include <thread>

using namespace xemu::shader_browser;

int main()
{
    Entry entry{};
    ShaderScope older{};
    older.title_id = 7;
    older.executable_fingerprint_version = 1;
    older.executable_fingerprint[0] = 1;
    ShaderScope current = older;
    current.executable_fingerprint[0] = 2;
    entry.scopes = {older, current};
    XemuShaderBrowserScope live{};
    live.title_id = 7;
    live.executable_fingerprint_version = 1;
    live.executable_fingerprint[0] = 2;
    ShaderScope found{};
    std::string scope_reason;
    assert(FindCurrentCaptureScope(entry, live, &found, &scope_reason));
    assert(found.executable_fingerprint[0] == 2);
    entry.scopes.pop_back();
    assert(!FindCurrentCaptureScope(entry, live, &found, &scope_reason));
    assert(scope_reason.find("current build") != std::string::npos);
    live.title_id = 8;
    assert(!FindCurrentCaptureScope(entry, live, &found, &scope_reason));

    CaptureStore store;
    XemuShaderCaptureRequest request{};
    request.title_id = 7;
    request.fingerprint_version = 1;
    request.fingerprint[0] = 5;
    request.backend = 1;
    request.session_epoch = 2;
    request.renderer_epoch = 3;
    request.scope_generation = 4;
    request.deadline_ns = 1000;
    request.selected.stage = 2;
    request.selected.hash[0] = 9;
    XemuShaderCaptureDraw draw{};
    draw.context = request;
    draw.stage_count = 1;
    draw.stages[0] = request.selected;
    draw.vertex_count = 3;
    draw.width = 64;
    draw.height = 64;
    float vertices[12] = { 1, 2, 3, 1, 4, 5, 6, 1, 7, 8, 9, 1 };
    draw.attribute_mask = 1;
    draw.attributes[0] = vertices;
    XemuShaderCaptureAbiSizes abi = xemu_shader_capture_abi_sizes();
    std::vector<uint8_t> shader_state(abi.shader_state);
    std::vector<uint8_t> vertex_uniforms(abi.vertex_uniforms);
    std::vector<uint8_t> pixel_uniforms(abi.pixel_uniforms);
    draw.shader_state = shader_state.data();
    draw.shader_state_size = shader_state.size();
    draw.vertex_uniforms = vertex_uniforms.data();
    draw.vertex_uniforms_size = vertex_uniforms.size();
    draw.pixel_uniforms = pixel_uniforms.data();
    draw.pixel_uniforms_size = pixel_uniforms.size();

    assert(!store.Submitted(draw, 1));
    store.Request(request);
    assert(store.CopyRequest(&draw.context));
    draw.sampled_nonce = store.SampleNonce();
    draw.context.renderer_epoch++;
    assert(!store.Submitted(draw, 1));
    draw.context.renderer_epoch--;
    draw.context.session_epoch++;
    assert(!store.Submitted(draw, 1));
    draw.context.session_epoch--;
    draw.context.scope_generation++;
    assert(!store.Submitted(draw, 1));
    draw.context.scope_generation--;
    assert(store.Submitted(draw, 1));
    assert(store.Status().duration_ns <= 2000000);
    assert(!store.Submitted(draw, 1));
    vertices[0] = 999;
    CapturedDraw captured;
    assert(store.Take(&captured));
    assert(captured.attributes[0][0] == 1);
    assert(!store.Take(&captured));

    store.Request(request);
    std::atomic<bool> lock_entered{false};
    std::thread holder([&] { store.HoldLockForTest(&lock_entered, 250); });
    while (!lock_entered.load(std::memory_order_acquire))
        std::this_thread::yield();
    auto lock_start = std::chrono::steady_clock::now();
    XemuShaderCaptureRequest contended{};
    assert(!store.CopyRequest(&contended));
    assert(!store.Submitted(draw, 1));
    auto lock_duration = std::chrono::steady_clock::now() - lock_start;
    assert(lock_duration < std::chrono::milliseconds(50));
    holder.join();

    store.Request(request);
    uint64_t old_nonce = draw.sampled_nonce;
    assert(store.CopyRequest(&draw.context));
    draw.sampled_nonce = store.SampleNonce();
    assert(draw.sampled_nonce != old_nonce);
    draw.sampled_nonce = old_nonce;
    assert(!store.Submitted(draw, 1));
    draw.sampled_nonce = store.SampleNonce();
    XemuShaderCaptureRequest newer = request;
    newer.selected.hash[0] = 10;
    store.Request(newer);
    assert(!store.Submitted(draw, 1));
    store.Cancel();
    assert(!store.Submitted(draw, 1));
    store.Request(request);
    assert(store.CopyRequest(&draw.context));
    draw.sampled_nonce = store.SampleNonce();
    assert(!store.Submitted(draw, 1001));
    assert(store.Status().kind == CaptureStatusKind::Expired);

    store.Request(request);
    assert(store.CopyRequest(&draw.context));
    draw.sampled_nonce = store.SampleNonce();
    draw.vertex_count = XEMU_SHADER_CAPTURE_MAX_VERTICES + 1;
    assert(!store.Submitted(draw, 1));
    assert(store.Status().kind == CaptureStatusKind::Unsupported);

    store.Request(request);
    assert(store.CopyRequest(&draw.context));
    draw.sampled_nonce = store.SampleNonce();
    draw.vertex_count = 3;
    std::vector<uint8_t> oversized(2U * 1024U * 1024U);
    draw.shader_state = oversized.data();
    draw.shader_state_size = oversized.size();
    assert(!store.Submitted(draw, 1));
    assert(store.Status().kind == CaptureStatusKind::Unsupported);
    draw.shader_state = nullptr;
    draw.shader_state_size = 0;

    request.deadline_ns = 10000000;
    store.Request(request);
    assert(store.CopyRequest(&draw.context));
    draw.sampled_nonce = store.SampleNonce();
    draw.capture_started_ns = 10;
    assert(!store.Submitted(draw, 2000011));
    assert(store.Status().kind == CaptureStatusKind::Unsupported);
    draw.capture_started_ns = 0;

    CapturedDraw packet{};
    packet.header = draw;
    packet.header.vertex_count = 3;
    packet.header.stage_count = 2;
    packet.header.stages[0].stage = 1;
    packet.header.stages[1] = request.selected;
    packet.header.attribute_mask = 1;
    packet.header.control_0 = 0x3c000000;
    packet.header.width = 64;
    packet.header.height = 64;
    packet.header.viewport_width = 64;
    packet.header.viewport_height = 64;
    packet.header.scissor_width = 64;
    packet.header.scissor_height = 64;
    packet.header.route = 1;
    packet.header.primitive = 4;
    packet.header.shader_state = nullptr;
    packet.header.shader_state_size = abi.shader_state;
    packet.header.vertex_uniforms = nullptr;
    packet.header.vertex_uniforms_size = abi.vertex_uniforms;
    packet.header.pixel_uniforms = nullptr;
    packet.header.pixel_uniforms_size = abi.pixel_uniforms;
    packet.header.vertex_source = nullptr;
    packet.header.pixel_source = nullptr;
    packet.header.attributes[0] = nullptr;
    packet.attributes[0] = std::vector<float>(vertices, vertices + 12);
    packet.shader_state.resize(abi.shader_state);
    packet.vertex_uniforms.resize(abi.vertex_uniforms, 2);
    packet.pixel_uniforms.resize(abi.pixel_uniforms, 3);
    packet.vertex_source = "void main() {}";
    packet.pixel_source = "void main() {}";
    std::string reason;
    packet.shader_state.resize(1);
    packet.header.shader_state_size = 1;
    assert(!SealCapturedDraw(&packet, &reason));
    packet.shader_state.resize(abi.shader_state);
    packet.header.shader_state_size = abi.shader_state;
    packet.pixel_uniforms.resize(1);
    packet.header.pixel_uniforms_size = 1;
    assert(!SealCapturedDraw(&packet, &reason));
    packet.pixel_uniforms.resize(abi.pixel_uniforms, 3);
    packet.header.pixel_uniforms_size = abi.pixel_uniforms;
    assert(SealCapturedDraw(&packet, &reason));
    assert(packet.replay_class == CaptureReplayClass::Approximate);
    assert(ValidateCapturedDraw(packet, &reason));
    packet.vertex_source.pop_back();
    packet.pixel_source.insert(packet.pixel_source.begin(), '}');
    assert(!ValidateCapturedDraw(packet, &reason));
    packet.pixel_source.erase(packet.pixel_source.begin());
    packet.vertex_source.push_back('}');
    packet.header.route = 4;
    assert(!SealCapturedDraw(&packet, &reason));
    packet.header.route = 1;
    assert(SealCapturedDraw(&packet, &reason));
    packet.header.viewport_width = 0;
    assert(!ValidateCapturedDraw(packet, &reason));
    packet.header.viewport_width = 64;
    assert(ValidateCapturedDraw(packet, &reason));
    packet.attributes[0][0]++;
    assert(!ValidateCapturedDraw(packet, &reason));
    packet.header.stage_count = 3;
    packet.header.stages[2].stage = 3;
    packet.header.primitive = 10;
    packet.geometry_source = "void main() {}";
    assert(SealCapturedDraw(&packet, &reason));
    assert(ValidateCapturedDraw(packet, &reason));
    packet.header.texture_mask = 1;
    xemu_shader_capture_test_make_state(packet.shader_state.data(),
                                        packet.shader_state.size(), 0);
    xemu_shader_capture_test_make_pixel_uniforms(
        packet.pixel_uniforms.data(), packet.pixel_uniforms.size());
    assert(SealCapturedDraw(&packet, &reason));
    assert(packet.replay_class == CaptureReplayClass::Approximate);
    assert(packet.substitution.find("texture stage 0") != std::string::npos);
    assert(packet.substitute_texels[0][3] == 255);
    assert(packet.substitute_sampler_kind[0] == 1);
    assert(packet.substitute_tex_scale[0] == 1.0f);
    assert(ValidateCapturedDraw(packet, &reason));
    xemu_shader_capture_test_make_state(packet.shader_state.data(),
                                        packet.shader_state.size(), 1);
    assert(!SealCapturedDraw(&packet, &reason));
    xemu_shader_capture_test_make_state(packet.shader_state.data(),
                                        packet.shader_state.size(), 0);
    packet.header.blend = 8;
    assert(SealCapturedDraw(&packet, &reason));
    assert(packet.substitution.find("blend") != std::string::npos);
    assert(ValidateCapturedDraw(packet, &reason));
    packet.header.control_0 = 0x14000000;
    assert(SealCapturedDraw(&packet, &reason));
    assert(packet.substitution.find("Untouched color channels") !=
           std::string::npos);
    assert(ValidateCapturedDraw(packet, &reason));
    packet.header.setup_raster = (1U << 28) | (2U << 21);
    assert(SealCapturedDraw(&packet, &reason));
    assert(ValidateCapturedDraw(packet, &reason));
    return 0;
}

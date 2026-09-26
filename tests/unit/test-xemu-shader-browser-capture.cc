// SPDX-License-Identifier: GPL-2.0-or-later
#include "ui/xui/shader-browser-capture-bridge.h"
#include "ui/xui/shader-browser-capture.hh"

#include <cassert>
#include <cstring>

using namespace xemu::shader_browser;

int main()
{
    CaptureStore store;
    XemuShaderCaptureRequest request{};
    request.title_id = 7;
    request.backend = 1;
    request.session_epoch = 2;
    request.renderer_epoch = 3;
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

    assert(!store.Submitted(draw, 1));
    store.Request(request);
    draw.context.renderer_epoch++;
    assert(!store.Submitted(draw, 1));
    draw.context.renderer_epoch--;
    draw.context.session_epoch++;
    assert(!store.Submitted(draw, 1));
    draw.context.session_epoch--;
    assert(store.Submitted(draw, 1));
    assert(!store.Submitted(draw, 1));
    vertices[0] = 999;
    CapturedDraw captured;
    assert(store.Take(&captured));
    assert(captured.attributes[0][0] == 1);
    assert(!store.Take(&captured));

    store.Request(request);
    XemuShaderCaptureRequest newer = request;
    newer.selected.hash[0] = 10;
    store.Request(newer);
    assert(!store.Submitted(draw, 1));
    store.Cancel();
    assert(!store.Submitted(draw, 1));
    store.Request(request);
    assert(!store.Submitted(draw, 1001));
    assert(store.Status().kind == CaptureStatusKind::Expired);

    store.Request(request);
    draw.vertex_count = XEMU_SHADER_CAPTURE_MAX_VERTICES + 1;
    assert(!store.Submitted(draw, 1));
    assert(store.Status().kind == CaptureStatusKind::Unsupported);

    store.Request(request);
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
    packet.header.shader_state_size = 1;
    packet.header.vertex_uniforms = nullptr;
    packet.header.vertex_uniforms_size = 1;
    packet.header.pixel_uniforms = nullptr;
    packet.header.pixel_uniforms_size = 1;
    packet.header.vertex_source = nullptr;
    packet.header.pixel_source = nullptr;
    packet.header.attributes[0] = nullptr;
    packet.attributes[0] = std::vector<float>(vertices, vertices + 12);
    packet.shader_state = { 1 };
    packet.vertex_uniforms = { 2 };
    packet.pixel_uniforms = { 3 };
    packet.vertex_source = "void main() {}";
    packet.pixel_source = "void main() {}";
    std::string reason;
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
    assert(SealCapturedDraw(&packet, &reason));
    assert(packet.replay_class == CaptureReplayClass::Approximate);
    assert(packet.substitution.find("texture stage 0") != std::string::npos);
    assert(packet.substitute_texels[0][3] == 255);
    assert(ValidateCapturedDraw(packet, &reason));
    packet.header.blend = 8;
    assert(SealCapturedDraw(&packet, &reason));
    assert(packet.substitution.find("blend") != std::string::npos);
    assert(ValidateCapturedDraw(packet, &reason));
    packet.header.control_0 = 0x14000000;
    assert(SealCapturedDraw(&packet, &reason));
    assert(packet.substitution.find("Untouched color channels") !=
           std::string::npos);
    assert(ValidateCapturedDraw(packet, &reason));
    return 0;
}

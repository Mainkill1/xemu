// SPDX-License-Identifier: GPL-2.0-or-later
#include "shader-browser-preview-vk.hh"
#include "shader-browser-preview-adapter.hh"
#include <cstdio>
#include <cstdlib>

using namespace xemu::shader_browser;
#define CHECK(x)                                                             \
    do {                                                                     \
        if (!(x)) {                                                          \
            std::fprintf(stderr, "%s:%d: %s (%s)\n", __FILE__, __LINE__, #x, \
                         error.c_str());                                     \
            std::abort();                                                    \
        }                                                                    \
    } while (0)

int main(int argc, char **)
{
    std::string error;
    PreviewVkExecutor executor;
    PreviewWorkItem work{};
    auto packet = std::make_shared<PreviewPacket>();
    packet->selection.backend = PreviewBackend::Vulkan;
    packet->width = packet->height = 32;
    packet->source = "#version 450\nlayout(location=0) in vec4 vtxD0;\n"
                     "layout(location=0) out vec4 color;\n"
                     "void main(){color=vtxD0;}\n";
    packet->partner_source = BuildPreviewSyntheticVertexSource(
        packet->source, PreviewBackend::Vulkan);
    PreviewSyntheticFixture fixture{};
    for (auto &color : fixture.corner_colors)
        color = { 255, 0, 0, 73 };
    packet->fixture_bytes = EncodePreviewSyntheticFixture(fixture);
    work.packet = packet;
    bool unsupported = false;
    if (argc > 1) {
        CHECK(!executor.Prepare(work, &error, &unsupported));
        CHECK(!unsupported);
        CHECK(!error.empty());
        CHECK(!executor.Prepare(work, &error, &unsupported));
        std::puts("private Vulkan unavailable-device retry: PASS");
        return 0;
    }
    CHECK(executor.Prepare(work, &error, &unsupported));
    std::vector<uint8_t> pixels;
    std::atomic<bool> stop{ false };
    CHECK(executor.Render(work, stop, &pixels, &error));
    CHECK(pixels.size() == 32 * 32 * 4);
    CHECK(pixels[4 * (16 * 32 + 16)] == 255);
    CHECK(pixels[4 * (16 * 32 + 16) + 1] == 0);
    CHECK(pixels[4 * (16 * 32 + 16) + 3] == 73);
    const auto final_pixels = pixels;
    for (auto channel : { PreviewChannel::Red, PreviewChannel::Green,
                          PreviewChannel::Blue, PreviewChannel::Alpha }) {
        work.result_key.channel = channel;
        CHECK(executor.Render(work, stop, &pixels, &error));
        const auto value =
            final_pixels[4 * (16 * 32 + 16) + PreviewChannelComponent(channel)];
        for (int c = 0; c < 3; ++c)
            CHECK(pixels[4 * (16 * 32 + 16) + c] == value);
        CHECK(pixels[4 * (16 * 32 + 16) + 3] == 255);
    }
    fixture.fog = 0.25f;
    fixture.alpha_reference = 63;
    packet->fixture_bytes = EncodePreviewSyntheticFixture(fixture);
    for (auto channel :
         { PreviewChannel::UV, PreviewChannel::D0, PreviewChannel::T0,
           PreviewChannel::Fog, PreviewChannel::DepthRamp,
           PreviewChannel::FixtureAlphaMask }) {
        work.result_key.channel = channel;
        CHECK(executor.Render(work, stop, &pixels, &error));
        const auto center = 4 * (16 * 32 + 16);
        const uint8_t expected = channel == PreviewChannel::UV ||
                                         channel == PreviewChannel::DepthRamp ?
                                     131 :
                                 channel == PreviewChannel::Fog ? 64 :
                                 channel == PreviewChannel::T0  ? 0 :
                                                                  255;
        CHECK(pixels[center] == expected);
        CHECK(pixels[center + 3] == 255);
    }
    work.result_key.channel = PreviewChannel::ShaderDiscard;
    CHECK(!executor.Render(work, stop, &pixels, &error));
    CHECK(error.find("Unsupported") != std::string::npos);
    work.result_key.channel = PreviewChannel::FinalRGBA;
    std::puts("Vulkan final scalar channels and fixture charts / unsupported "
              "discard PASS");
    // Mesh and camera edits reuse this prepared pipeline.
    std::vector<std::vector<uint8_t>> mesh_pixels;
    for (auto kind :
         { PreviewMesh::Quad, PreviewMesh::Sphere, PreviewMesh::Cube }) {
        work.result_key.scene = {};
        work.result_key.scene.mesh = kind;
        work.result_key.scene.yaw = 30;
        work.result_key.scene.pitch = 20;
        work.result_key.scene.distance = 4;
        CHECK(executor.Render(work, stop, &pixels, &error));
        mesh_pixels.push_back(pixels);
        packet->update_policy = PreviewUpdatePolicy::Continuous;
        work.result_key.time_seconds = 4;
        CHECK(executor.Render(work, stop, &pixels, &error));
        CHECK(pixels != mesh_pixels.back());
        packet->update_policy = PreviewUpdatePolicy::OnDirty;
        work.result_key.time_seconds = 0;
        pixels = mesh_pixels.back();
        size_t coverage = 0;
        for (size_t i = 0; i < pixels.size(); i += 4)
            coverage += pixels[i] == 255;
        CHECK(coverage > 20 && coverage < 900);
        std::fprintf(stderr, "Vulkan mesh %d: %zu red pixels\n", int(kind),
                     coverage);
        work.result_key.scene.yaw = -40;
        work.result_key.scene.pan[0] = 0.5f;
        CHECK(executor.Render(work, stop, &pixels, &error));
        CHECK(pixels != mesh_pixels.back());
    }
    CHECK(mesh_pixels[0] != mesh_pixels[1]);
    CHECK(mesh_pixels[1] != mesh_pixels[2]);
    CHECK(mesh_pixels[0] != mesh_pixels[2]);
    work.result_key.scene = {};
    auto source = [&](const std::string &text) {
        packet->source = text;
        packet->partner_source =
            BuildPreviewSyntheticVertexSource(text, PreviewBackend::Vulkan);
        ++work.compile_key.interface_abi;
    };
    auto render = [&] {
        CHECK(executor.Prepare(work, &error, &unsupported));
        CHECK(!unsupported);
        CHECK(executor.Render(work, stop, &pixels, &error));
        CHECK(pixels.size() == size_t(packet->width) * packet->height * 4);
    };
    source("#version 450\nlayout(binding=3) uniform sampler2D texSamp0;\n"
           "layout(location=5) in vec4 vtxT0;\n"
           "layout(location=0) out vec4 color;\n"
           "void main(){color=texture(texSamp0,vtxT0.xy);}\n");
    for (auto &texel : fixture.texture_texels)
        texel = { 0, 255, 0, 255 };
    packet->fixture_bytes = EncodePreviewSyntheticFixture(fixture);
    render();
    CHECK(pixels[4 * (16 * 32 + 16)] == 0);
    CHECK(pixels[4 * (16 * 32 + 16) + 1] == 255);
    // A failed sampler edit must leave the old descriptor and cached settings
    // intact. Inspect before retrying so a broken implementation cannot submit
    // a descriptor referring to a destroyed sampler to the native device.
    fixture.texture_texels = { { { 255, 0, 0, 255 },
                                 { 0, 255, 0, 255 },
                                 { 255, 0, 0, 255 },
                                 { 0, 255, 0, 255 } } };
    fixture.uv_scale = { 0, 0 };
    fixture.uv_offset = { 1.25f, 0.25f };
    packet->fixture_bytes = EncodePreviewSyntheticFixture(fixture);
    render();
    CHECK(pixels[4 * (16 * 32 + 16) + 1] == 255); // clamped right texel
    const bool old_linear = fixture.linear_filter;
    const bool old_repeat = fixture.repeat_wrap;
    fixture.linear_filter = !old_linear;
    fixture.repeat_wrap = !old_repeat;
    packet->fixture_bytes = EncodePreviewSyntheticFixture(fixture);
    for (unsigned attempt = 0; attempt < 2; ++attempt) {
        executor.FailNextSamplerCreationForTest();
        CHECK(!executor.Render(work, stop, &pixels, &error));
        CHECK(executor.HasSamplerSettingsForTest(old_linear, old_repeat));
    }
    CHECK(executor.Render(work, stop, &pixels, &error));
    CHECK(executor.HasSamplerSettingsForTest(!old_linear, !old_repeat));
    CHECK(pixels[4 * (16 * 32 + 16)] == 255); // repeated left texel
    CHECK(pixels[4 * (16 * 32 + 16) + 1] == 0);
    std::fprintf(stderr, "private Vulkan sampler failure/retry: PASS\n");
    fixture.uv_scale = { 1, 1 };
    fixture.uv_offset = { 0, 0 };
    // Same compile identity, updated fixture, sampler and extent.
    for (auto &texel : fixture.texture_texels)
        texel = { 0, 0, 255, 255 };
    fixture.linear_filter = fixture.repeat_wrap = 1;
    packet->fixture_bytes = EncodePreviewSyntheticFixture(fixture);
    packet->width = packet->height = 160;
    render();
    CHECK(pixels[4 * (80 * 160 + 80) + 2] == 255);
    packet->width = packet->height = 320;
    render();
    CHECK(pixels[4 * (160 * 320 + 160) + 2] == 255);
    source("#version 450\nlayout(binding=1,std140) uniform PshUniforms {\n"
           "int alphaRef; mat2 bumpMat[4]; float bumpOffset[4]; float "
           "bumpScale[4];\n"
           "vec4 clipRange; ivec4 clipRegion[8]; uint colorKey[4]; uint "
           "colorKeyMask[4];\n"
           "vec4 consts[18]; float depthFactor; float depthOffset; vec4 "
           "fogColor;\n"
           "ivec2 surfaceScale; float texScale[4]; };\n"
           "layout(location=0) out vec4 color;\n"
           "void main(){ if (gl_FragCoord.x >= clipRegion[0].z) discard;\n"
           "color=vec4(consts[0].r,fogColor.g,consts[0].b,float(alphaRef)/"
           "255.0); }\n");
    fixture.constant_color = { 0, 1, 0, 1 };
    fixture.fog_color = { 0, 1, 0, 1 };
    fixture.alpha_reference = 128;
    packet->fixture_bytes = EncodePreviewSyntheticFixture(fixture);
    render();
    CHECK(pixels[4 * (160 * 320 + 160) + 1] == 255);
    CHECK(pixels[4 * (160 * 320 + 160) + 3] == 128);
    fixture.constant_color = { 1, 0, 0, 1 };
    fixture.alpha_reference = 64;
    fixture.fog_color[1] = 0.4f;
    packet->fixture_bytes = EncodePreviewSyntheticFixture(fixture);
    CHECK(executor.Render(work, stop, &pixels, &error));
    CHECK(pixels[4 * (160 * 320 + 160)] == 255 &&
          pixels[4 * (160 * 320 + 160) + 3] == 64);
    CHECK(pixels[4 * (160 * 320 + 160) + 1] == 102);
    std::puts(
        "Vulkan constant, fog color and alpha edits without prepare PASS");
    source("#version 450\nlayout(location=4,component=0) in float vtxFog;\n"
           "layout(location=0) out vec4 color;\n"
           "void main(){color=vec4(vtxFog,0,0,1);}\n");
    render(); // Explicit component zero matches the implicit partner component.
    CHECK(pixels[4 * (160 * 320 + 160) + 3] == 255);
    source("#version 450\nlayout(location=0) in vec4 vtxD0;\n"
           "layout(location=0) out vec4 color;\n"
           "void main(){color=vtxD0;}\n");
    for (auto &corner : fixture.corner_colors) corner = {255, 128, 64, 255};
    packet->fixture_bytes = EncodePreviewSyntheticFixture(fixture);
    packet->update_policy = PreviewUpdatePolicy::Continuous;
    render();
    const auto time_zero = pixels;
    work.result_key.time_seconds = 4.0;
    // A clock sample consumes the prepared program without another Prepare.
    CHECK(executor.Render(work, stop, &pixels, &error));
    CHECK(pixels != time_zero);
    CHECK(pixels[4 * (160 * 320 + 160)] < time_zero[4 * (160 * 320 + 160)]);
    packet->update_policy = PreviewUpdatePolicy::OnDirty;
    packet->width = packet->height = 32;
    fixture = MakePreviewFixture(PreviewFixtureProfile::Cubemap);
    packet->fixture_bytes = EncodePreviewSyntheticFixture(fixture);
    source(
        "#version 450\nlayout(binding=3) uniform samplerCube texSamp0;\n"
        "layout(location=5) in vec4 vtxT0; layout(location=0) out vec4 color;\n"
        "void main(){color=texture(texSamp0,vtxT0.xyz);}");
    render();
    CHECK(pixels[4 * (16 * 32 + 16) + 2] == 255);
    const auto cube_z = pixels;
    fixture.cube_direction = { 1, 0, 0 };
    packet->fixture_bytes = EncodePreviewSyntheticFixture(fixture);
    CHECK(executor.Render(work, stop, &pixels, &error));
    CHECK(pixels[4 * (16 * 32 + 16)] == 255);
    CHECK(pixels != cube_z);
    fixture.cube_direction = { 0, 0, 1 };
    packet->fixture_bytes = EncodePreviewSyntheticFixture(fixture);
    source(
        "#version 450\nlayout(binding=3) uniform samplerCube\n\ttexSamp0;\n"
        "layout(location=5) in vec4 vtxT0; layout(location=0) out vec4 color;"
        "void main(){color=texture(texSamp0,vtxT0.xyz);}");
    render();
    CHECK(pixels[4 * (16 * 32 + 16)] == 40 &&
          pixels[4 * (16 * 32 + 16) + 2] == 255);
    fixture.cube_direction = { 1, 0, 0 };
    fixture.uv_scale = { 0, 0 };
    fixture.uv_offset = { 0.25f, 0.25f };
    packet->fixture_bytes = EncodePreviewSyntheticFixture(fixture);
    source(
        "#version 450\n// samplerCube texSamp0\n/* samplerCube texSamp0; */\n"
        "layout(binding=3) uniform sampler2D texSamp0;\n"
        "layout(location=5) in vec4 vtxT0; layout(location=0) out vec4 color;"
        "void main(){color=vec4(vtxT0.xy,0,1)*texture(texSamp0,vec2(0.5));}");
    render();
    CHECK(pixels[4 * (16 * 32 + 16)] == 64 &&
          pixels[4 * (16 * 32 + 16) + 1] == 10);
    std::puts("Vulkan reflected cube routing: whitespace and misleading "
              "comments PASS");
    std::puts("Vulkan cube +Z blue / +X red PASS");
    fixture = MakePreviewFixture(PreviewFixtureProfile::MultiTexture);
    fixture.textures.fill(PreviewFixtureProfile::MultiTexture);
    packet->fixture_bytes = EncodePreviewSyntheticFixture(fixture);
    source("#version 450\nlayout(binding=7) uniform sampler2D texSamp0;\n"
           "layout(binding=2) uniform sampler2D texSamp1;\n"
           "layout(location=0) out vec4 color;\n"
           "void "
           "main(){color=vec4(texture(texSamp0,vec2(0.5)).r,texture(texSamp1,"
           "vec2(0.5)).g,0,1);}");
    render();
    CHECK(pixels[4 * (16 * 32 + 16)] == 255 &&
          pixels[4 * (16 * 32 + 16) + 1] == 255);
    fixture.textures[1] = PreviewFixtureProfile::Flat;
    packet->fixture_bytes = EncodePreviewSyntheticFixture(fixture);
    CHECK(executor.Render(work, stop, &pixels, &error));
    CHECK(pixels[4 * (16 * 32 + 16)] == 255 &&
          pixels[4 * (16 * 32 + 16) + 1] == 90);
    std::puts("Vulkan distinct T0/T1 and independent input edit PASS");
    source("#version 450\nlayout(location=0) in vec4 vtxD0; layout(location=1) "
           "in vec4 vtxD1;\n"
           "layout(location=2) in vec4 vtxB0; layout(location=3) in vec4 "
           "vtxB1; layout(location=4) in float vtxFog;\n"
           "layout(location=0) out vec4 color; void "
           "main(){color=vec4(vtxD0.r,vtxD1.g,vtxB0.b,vtxB1.a)*vtxFog;}");
    fixture.fog = 0.5f;
    fixture.colors[0][0] = 0.2f;
    fixture.colors[1][1] = 0.4f;
    fixture.colors[2][2] = 0.6f;
    fixture.colors[3][3] = 0.8f;
    packet->fixture_bytes = EncodePreviewSyntheticFixture(fixture);
    render();
    for (int i = 0; i < 4; ++i)
        CHECK(std::abs(int(pixels[4 * (16 * 32 + 16) + i]) -
                       int((i + 1) * 25.5f)) <= 1);
    fixture.fog = 1;
    packet->fixture_bytes = EncodePreviewSyntheticFixture(fixture);
    CHECK(executor.Render(work, stop, &pixels, &error));
    CHECK(pixels[4 * (16 * 32 + 16)] == 51);
    std::puts("Vulkan independent D0/D1/B0/B1 and fog edit PASS");
    source("#version 450\nlayout(binding=4) uniform sampler2D texSamp0; "
           "layout(location=0) out vec4 color; void "
           "main(){color=texture(texSamp0,vec2(0.3125));}");
    for (int profile = 0; profile < 9; ++profile) {
        fixture =
            MakePreviewFixture(static_cast<PreviewFixtureProfile>(profile));
        fixture.linear_filter = 0;
        packet->fixture_bytes = EncodePreviewSyntheticFixture(fixture);
        render();
        const auto expected = GeneratePreviewTexture(fixture, 0);
        for (int c = 0; c < 4; ++c)
            CHECK(pixels[4 * (16 * 32 + 16) + c] ==
                  expected[4 * (2 * 8 + 2) + c]);
    }
    std::puts("Vulkan all nine profile sample readbacks PASS");
    auto reject = [&](const std::string &declaration, const std::string &body) {
        source("#version 450\n" + declaration +
               "\nlayout(location=0) out vec4 color;\nvoid main(){" + body +
               "}\n");
        error.clear();
        CHECK(!executor.Prepare(work, &error, &unsupported));
        CHECK(unsupported);
    };
    reject("layout(binding=3) uniform samplerCube cube;",
           "color=texture(cube,vec3(1));");
    reject("layout(binding=3) uniform usampler2D texSamp0;",
           "color=vec4(texture(texSamp0,vec2(1)));");
    reject("layout(binding=3) uniform sampler2DArray texSamp0;",
           "color=texture(texSamp0,vec3(1));");
    reject("layout(binding=1,std140) uniform U {vec2 consts[18];};",
           "color=vec4(consts[0],0,1);");
    reject("layout(binding=1,std140) uniform U {vec4 unknown;};",
           "color=unknown;");
    reject("layout(push_constant) uniform U {vec4 consts;};", "color=consts;");
    reject("layout(binding=1,std430) buffer U {vec4 consts;};",
           "color=consts;");
    reject("layout(location=5) in vec3 unexpected;",
           "color=vec4(unexpected,1);");
    reject("layout(location=4,component=1) in float vtxFog;",
           "color=vec4(vtxFog);");
    reject("", "color=vec4(gl_PointCoord,0,1);");
    source("#version 450\nthis is invalid GLSL");
    CHECK(!executor.Prepare(work, &error, &unsupported));
    CHECK(!unsupported);
    source("#version 450\nlayout(location=0) out vec4 color;\nvoid "
           "main(){color=vec4(1);}");
    render(); // recover from rejected/failed preparations
    stop = true;
    CHECK(!executor.Render(work, stop, &pixels, &error));
    std::puts("private Vulkan: quad, sampler, fixture edits, 160/320 resize, "
              "uniform block, rejection, recovery and stop PASS");
}

// SPDX-License-Identifier: GPL-2.0-or-later
#include "asset-browser-placement.hh"
#include <cassert>
#include <cmath>
#include <cstring>
#include <limits>
#include <cstdio>
using namespace xemu::asset_browser;
static capture::SharedCaptureBlock Bytes(const void *p, size_t n)
{
    auto b = std::make_shared<capture::CaptureImmutableBlock>();
    b->bytes.assign(static_cast<const uint8_t *>(p),
                    static_cast<const uint8_t *>(p) + n);
    return b;
}
static capture::CaptureOccurrence Draw()
{
    capture::CaptureOccurrence d;
    const std::string source =
        "#define R12 oPos\nvoid main(){\n"
        "DP4(oPos,x, v0, c[114]);\nDP4(oPos,y, v0, c[115]);\n"
        "DP4(oPos,z, v0, c[116]);\nDP4(oPos,w, v0, c[117]);\n"
        "RCC(R1,x, R12.w);\nMUL(oPos,xyz, R12.xyz, c[58].xyz);\n"
        "LIT(R1,z, R9.xyyw);\nMAD(oPos,xyz, R12.xyz, R1.x, c[59].xyz);\n"
        "oPos.xy = roundScreenCoords(oPos.xy);\noPos.w = "
        "clampAwayZeroInf(oPos.w);\n"
        "vec4 vtxPos = oPos;\noPos.xy = (2.0f * oPos.xy - surfaceSize) / "
        "surfaceSize;\n"
        "oPos.z = oPos.z / clipRange.y;\noPos.xyz *= oPos.w;\ngl_Position = "
        "oPos;\n}";
    d.inputs.sources[1] = Bytes(source.data(), source.size());
    std::array<float, 192 * 4> c{};
    for (size_t row = 0; row < 4; ++row)
        c[(114 + row) * 4 + row] = 1;
    c[58 * 4] = 320;
    c[58 * 4 + 1] = 240;
    c[58 * 4 + 2] = 65535;
    c[59 * 4] = 320;
    c[59 * 4 + 1] = 240;
    const std::array<float, 2> extent{ 640, 480 };
    const std::array<float, 4> clip{ 0, 65535, 0, 0 };
    d.inputs.uniforms.push_back(
        { 1, 1, 4, 192, "c", Bytes(c.data(), sizeof(c)) });
    d.inputs.uniforms.push_back(
        { 1, 1, 2, 1, "surfaceSize", Bytes(extent.data(), sizeof(extent)) });
    d.inputs.uniforms.push_back(
        { 1, 1, 4, 1, "clipRange", Bytes(clip.data(), sizeof(clip)) });
    return d;
}
int main()
{
    auto d = Draw();
    auto gl = d;
    for (auto &u : gl.inputs.uniforms)
        u.stage = 0;
    assert(DecodeAssetPlacement(gl).valid);
    auto prefix = std::make_shared<capture::CaptureImmutableBlock>(
        *gl.inputs.uniforms[0].data);
    prefix->bytes.resize(118 * 16);
    gl.inputs.uniforms[0].count = 118;
    gl.inputs.uniforms[0].data = prefix;
    assert(
        DecodeAssetPlacement(gl).valid); // GL retains only active array prefix.
    prefix->bytes.resize(117 * 16);
    gl.inputs.uniforms[0].count = 117;
    assert(!DecodeAssetPlacement(gl).valid); // Consumed row is unavailable.

    auto placement = DecodeAssetPlacement(d);
    assert(placement.valid);
    AssetMatrix inverse;
    assert(InvertAssetMatrix(placement.clip_from_local, &inverse));
    auto identity = MultiplyAssetMatrices(inverse, placement.clip_from_local);
    for (size_t i = 0; i < 16; ++i)
        assert(std::abs(identity[i] - (i % 5 == 0 ? 1.f : 0.f)) < 1e-5f);
    auto wheel = placement.clip_from_local;
    wheel[0] = 0;
    wheel[1] = -1;
    wheel[4] = 1;
    wheel[5] = 0;
    wheel[3] = .8f;
    wheel[7] = -.4f;
    auto relative = MultiplyAssetMatrices(inverse, wheel);
    std::array<float, 3> point;
    assert(TransformAssetPoint(relative, { .2f, 0, 0 }, &point));
    assert(std::abs(point[0] - .8f) < 1e-5 && std::abs(point[1] + .2f) < 1e-5);
    AssetMatrix singular{};
    assert(!InvertAssetMatrix(singular, &inverse));
    wheel[0] = std::numeric_limits<float>::quiet_NaN();
    assert(!InvertAssetMatrix(wheel, &inverse));
    auto constants = std::make_shared<capture::CaptureImmutableBlock>(
        *d.inputs.uniforms[0].data);
    const uint32_t nan_bits = 0x7fc01234;
    std::memcpy(constants->bytes.data(), &nan_bits,
                4); // Unused c0 is evidence, not placement.
    d.inputs.uniforms[0].data = constants;
    assert(DecodeAssetPlacement(d).valid);
    std::memcpy(constants->bytes.data() + 114 * 16, &nan_bits, 4);
    assert(!DecodeAssetPlacement(d).valid);
    const float one = 1;
    std::memcpy(constants->bytes.data() + 114 * 16, &one, 4);
    assert(DecodeAssetPlacement(d).valid);
    const auto good = d.inputs.sources[1];
    std::string bad(good->bytes.begin(), good->bytes.end());
    bad.insert(bad.find("gl_Position"), "ADD(oPos,x, R12.x, c[5].x);\n");
    d.inputs.sources[1] = Bytes(bad.data(), bad.size());
    assert(!DecodeAssetPlacement(d).valid);
    d.inputs.sources[1] = good;
    d.inputs.uniforms[0].data.reset();
    assert(!DecodeAssetPlacement(d).valid);
    std::puts(
        "Captured placement, wheel rotation and unsupported transform PASS");
}

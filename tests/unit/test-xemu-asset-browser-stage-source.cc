// SPDX-License-Identifier: GPL-2.0-or-later
#include "asset-browser-stage-source.hh"
#include <cassert>
#include <cstdio>
using namespace xemu::asset_browser;
int main()
{
    const std::string vertex =
        "#version 450\nlayout(push_constant) uniform P {\nvec4 "
        "inlineValue[2];\n};\n"
        "layout(binding = 0, std140) uniform U {\nvec4 c[192];\nvec4 "
        "clipRange;\n#define C c[114]\n};\n"
        "layout(location=0) in vec4 v0;\nlayout(location=9) flat out vec4 "
        "vtxPos0;\n"
        "layout(location=10) flat out vec4 vtxPos1;\nlayout(location=11) flat "
        "out vec4 vtxPos2;\n"
        "void "
        "main(){gl_Position=v0*C+inlineValue[0];vtxPos0=v0;vtxPos1=v0;vtxPos2="
        "v0;}\n";
    const auto rounded = BuildAssetStageSource(
        "#version 450\nvoid main(){vec4 oPos=vec4(1);oPos.xy = "
        "roundScreenCoords(oPos.xy);gl_Position=oPos;}\n",
        1);
    assert(rounded.text.find("oPos.xy = roundScreenCoords(oPos.xy)") ==
           std::string::npos);
    const auto v = BuildAssetStageSource(vertex, 1);
    assert(!v.text.empty() && v.error.empty());
    assert(v.text.find("push_constant") == std::string::npos);
    assert(v.text.find("uniform vec4 asset1_c[192]") != std::string::npos);
    assert(v.text.find("#define C asset1_c[114]") != std::string::npos);
    assert(v.text.find("uniform vec4 asset1_inlineValue[2]") !=
           std::string::npos);
    assert(v.text.find("asset_inspection_from_clip * gl_Position") !=
           std::string::npos);
    assert(v.text.find("vtxPos2 = asset_window_position") != std::string::npos);
    assert(AssetStageUniformName(2, "clipRegion[0]") == "asset2_clipRegion[0]");
    const auto p = BuildAssetStageSource(
        "#version 450\nlayout(binding=3)uniform sampler2D "
        "texSamp0;\nlayout(location=0)out vec4 fragColor;\nvoid "
        "main(){fragColor=texture(texSamp0,vec2(.5));gl_FragDepth=.1;}\n",
        2);
    assert(p.text.find("asset2_texSamp0") != std::string::npos);
    assert(p.text.find("gl_FragDepth = gl_FragCoord.z") != std::string::npos);
    assert(
        BuildAssetStageSource("#version 450\nvoid wrong(){}", 1).text.empty());
    capture::Bounds3 bounds;
    bounds.valid = true;
    bounds.minimum = { -1, -1, -1 };
    bounds.maximum = { 1, 1, 1 };
    const auto camera = BuildAssetCameraMatrix(bounds, 0, 0, 1, 0, 0, 1);
    assert(camera[0] > 0 && camera[5] > 0 && camera[15] == 1);
    std::puts(
        "Captured stage interface adaptation and camera bookkeeping PASS");
}

// SPDX-License-Identifier: GPL-2.0-or-later
#include "asset-browser-stage-source.hh"
#include <algorithm>
#include <cmath>
#include <regex>
#include <sstream>
#include <set>
namespace xemu::asset_browser {
std::string AssetStageUniformName(uint32_t stage, const std::string &name)
{
    return "asset" + std::to_string(stage) + "_" + name;
}
AssetStageSource BuildAssetStageSource(const std::string &original,
                                       uint32_t stage)
{
    AssetStageSource out;
    auto fail = [&](const char *message) {
        out.text.clear();
        out.error = message;
        return out;
    };
    if (original.empty() || original.size() > 256 * 1024 ||
        original.find('\0') != std::string::npos || stage < 1 || stage > 3)
        return fail("Missing or oversized generated stage source");
    std::string text;
    for (size_t i = 0; i < original.size();) {
        if (original.compare(i, 2, "/*") == 0) {
            const size_t end = original.find("*/", i + 2);
            if (end == std::string::npos)
                return fail("Unterminated source comment");
            text += ' ';
            i = end + 2;
        } else if (original.compare(i, 2, "//") == 0) {
            const size_t end = original.find('\n', i + 2);
            i = end == std::string::npos ? original.size() : end;
        } else
            text += original[i++];
    }
    static const std::regex block(
        R"(layout\s*\([^)]*(?:std140|push_constant)[^)]*\)\s*uniform\s+[A-Za-z_][A-Za-z0-9_]*\s*\{([^{}]*)\}\s*;)");
    std::smatch match;
    size_t blocks = 0;
    while (std::regex_search(text, match, block)) {
        if (++blocks > 8)
            return fail("Generated stage has too many uniform blocks");
        std::istringstream lines(match[1]);
        std::string line, pending, flat;
        while (std::getline(lines, line)) {
            const auto start = line.find_first_not_of(" \t\r");
            if (start != std::string::npos && line[start] == '#') {
                if (pending.find_first_not_of(" \t\r") == std::string::npos)
                    pending.clear();
                else
                    return fail("Unsupported preprocessor inside a uniform "
                                "declaration");
                flat += line + '\n';
                continue;
            }
            pending += line + ' ';
            size_t semi;
            while ((semi = pending.find(';')) != std::string::npos) {
                const auto declaration_start =
                    pending.find_first_not_of(" \t\r");
                if (declaration_start == std::string::npos ||
                    declaration_start >= semi)
                    return fail("Empty generated uniform declaration");
                flat += "uniform " +
                        pending.substr(declaration_start,
                                       semi - declaration_start) +
                        ";\n";
                pending.erase(0, semi + 1);
            }
        }
        if (pending.find_first_not_of(" \t\r") != std::string::npos)
            return fail("Unsupported generated uniform block declaration");
        text.replace(size_t(match.position()), size_t(match.length()), flat);
    }
    static const std::regex uniform(
        R"(uniform\s+[A-Za-z_][A-Za-z0-9_]*\s+([A-Za-z_][A-Za-z0-9_]*)\s*(?:\[[0-9]+\])?\s*;)");
    std::set<std::string> names;
    for (std::sregex_iterator it(text.begin(), text.end(), uniform), end;
         it != end; ++it)
        names.insert((*it)[1]);
    if (names.size() > 256)
        return fail("Generated stage uniform count exceeds its bound");
    for (const auto &name : names) {
        text = std::regex_replace(text, std::regex("\\b" + name + "\\b"),
                                  AssetStageUniformName(stage, name));
        out.uniforms.push_back(name);
    }
    static const std::regex main(R"(void\s+main\s*\(\s*(?:void)?\s*\))");
    if (!std::regex_search(text, main))
        return fail("Generated stage entry point is missing");
    if (stage != 3) {
        text = std::regex_replace(text, main, "void asset_original_main()");
        if (stage == 1) {
            // Guest pixel rounding belongs to the captured game camera. It
            // would quantize the free inspection camera after inverse
            // placement.
            static const std::regex screen_rounding(
                R"(oPos\s*\.\s*xy\s*=\s*roundScreenCoords\s*\(\s*oPos\s*\.\s*xy\s*\)\s*;)");
            text = std::regex_replace(text, screen_rounding, "");
            text += "\nuniform mat4 asset_inspection_from_clip;\nuniform vec2 "
                    "asset_viewport_extent;\n"
                    "void main(){asset_original_main();\n"
                    "gl_Position = asset_inspection_from_clip * gl_Position;\n"
                    "vec4 asset_window_position = "
                    "vec4((gl_Position.xy/"
                    "gl_Position.w*.5+.5)*asset_viewport_extent,gl_Position.z/"
                    "gl_Position.w,gl_Position.w);\n";
            for (const auto *name : { "vtxPos0", "vtxPos1", "vtxPos2" })
                if (text.find(name) != std::string::npos)
                    text += std::string(name) + " = asset_window_position;\n";
            text += "}\n";
        } else
            text += "\nvoid main(){asset_original_main(); gl_FragDepth = "
                    "gl_FragCoord.z;}\n";
    }
    out.text = std::move(text);
    return out;
}
AssetMatrix BuildAssetCameraMatrix(const capture::Bounds3 &bounds, float yaw,
                                   float pitch, float zoom, float pan_x,
                                   float pan_y, float aspect)
{
    AssetMatrix result{};
    if (!bounds.valid || !std::isfinite(yaw) || !std::isfinite(pitch) ||
        !std::isfinite(zoom) || !std::isfinite(pan_x) ||
        !std::isfinite(pan_y) || !std::isfinite(aspect) || aspect <= 0)
        return result;
    std::array<float, 3> center;
    double radius = 0;
    for (size_t i = 0; i < 3; ++i) {
        if (!std::isfinite(bounds.minimum[i]) ||
            !std::isfinite(bounds.maximum[i]))
            return result;
        center[i] = (bounds.minimum[i] + bounds.maximum[i]) * .5f;
        radius +=
            std::pow(double(bounds.maximum[i]) - bounds.minimum[i], 2) * .25;
    }
    const float inverse_radius = 1 / std::max(float(std::sqrt(radius)), 1e-6f);
    const float scale = .8f * std::clamp(zoom, .1f, 20.f) * inverse_radius;
    const float cy = std::cos(yaw), sy = std::sin(yaw), cp = std::cos(pitch),
                sp = std::sin(pitch);
    const float sx = scale / std::max(aspect, 1.f),
                scaley = scale * std::min(aspect, 1.f);
    result = { sx * cy,
               0,
               sx * sy,
               0,
               -scaley * sp * sy,
               scaley * cp,
               scaley * sp * cy,
               0,
               -.5f * inverse_radius * cp * sy,
               -.5f * inverse_radius * sp,
               .5f * inverse_radius * cp * cy,
               0,
               0,
               0,
               0,
               1 };
    for (size_t row = 0; row < 3; ++row)
        for (size_t col = 0; col < 3; ++col)
            result[row * 4 + 3] -= result[row * 4 + col] * center[col];
    result[3] += pan_x;
    result[7] += pan_y;
    return result;
}
} // namespace xemu::asset_browser

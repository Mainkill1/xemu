// SPDX-License-Identifier: GPL-2.0-or-later
#include "shader-browser-preview-adapter.hh"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <utility>

namespace xemu::shader_browser {

void ApplyPreviewSyntheticFixture(const PreviewSyntheticFixture &fixture,
                                  std::vector<PreviewSceneVertex> &vertices,
                                  const std::array<bool, 4> &cube_stages)
{
    for (auto &v : vertices) {
        const float u = v.uv[0], t = v.uv[1];
        for (size_t c = 0; c < 4; ++c)
            v.color[c] = ((1 - u) * t * fixture.corner_colors[0][c] +
                          u * t * fixture.corner_colors[1][c] +
                          (1 - u) * (1 - t) * fixture.corner_colors[2][c] +
                          u * (1 - t) * fixture.corner_colors[3][c]) /
                         255.0f;
        for (size_t i = 0; i < 4; ++i)
            std::copy(fixture.colors[i].begin(), fixture.colors[i].end(),
                      v.colors[i]);
        for (size_t i = 0; i < 4; ++i)
            v.cube_stages[i] = cube_stages[i] ? 1.0f : 0.0f;
        v.fog = fixture.profile == PreviewFixtureProfile::Fog ?
                    u * fixture.fog :
                    fixture.fog;
        std::copy(fixture.cube_direction.begin(), fixture.cube_direction.end(),
                  v.direction);
        for (size_t c = 0; c < 2; ++c)
            v.uv[c] = v.uv[c] * fixture.uv_scale[c] + fixture.uv_offset[c];
    }
}

void AnimatePreviewSyntheticFixture(PreviewSyntheticFixture *fixture,
                                    double time_seconds)
{
    // Preview-owned diagnostic motion; no guest time uniform is inferred.
    const float phase = static_cast<float>(std::fmod(time_seconds, 8.0) / 8.0);
    fixture->uv_offset[0] += phase;
    fixture->uv_offset[1] += phase * 0.5f;
    const float gain = 0.65f + 0.35f * std::cos(phase * 6.28318530718f);
    for (auto &color : fixture->corner_colors) {
        for (size_t c = 0; c < 3; ++c) {
            color[c] = static_cast<uint8_t>(color[c] * gain);
        }
    }
}

std::vector<uint8_t> EncodePreviewSyntheticFixture(
    const PreviewSyntheticFixture &fixture)
{
    static_assert(sizeof(float) == 4, "Preview fixture expects 32-bit float");
    std::vector<uint8_t> bytes;
    bytes.reserve(kPreviewSyntheticFixtureBytes);
    for (const auto &color : fixture.corner_colors) {
        bytes.insert(bytes.end(), color.begin(), color.end());
    }
    for (const auto &texel : fixture.texture_texels) {
        bytes.insert(bytes.end(), texel.begin(), texel.end());
    }
    auto append_float = [&bytes](float value) {
        uint8_t data[sizeof(value)];
        std::memcpy(data, &value, sizeof(value));
        bytes.insert(bytes.end(), data, data + sizeof(data));
    };
    for (float value : fixture.uv_scale) append_float(value);
    for (float value : fixture.uv_offset) append_float(value);
    for (float value : fixture.constant_color) append_float(value);
    for (float value : fixture.fog_color) append_float(value);
    bytes.push_back(fixture.alpha_reference);
    bytes.push_back(fixture.linear_filter);
    bytes.push_back(fixture.repeat_wrap);
    bytes.insert(bytes.end(), { 'P', 'F', 'X', 2 });
    bytes.push_back(static_cast<uint8_t>(fixture.profile));
    for (auto profile : fixture.textures)
        bytes.push_back(static_cast<uint8_t>(profile));
    for (auto color : fixture.colors)
        for (float value : color)
            append_float(value);
    append_float(fixture.fog);
    for (float value : fixture.cube_direction)
        append_float(value);
    return bytes;
}

bool DecodePreviewSyntheticFixture(const std::vector<uint8_t> &bytes,
                                   PreviewSyntheticFixture *fixture,
                                   std::string *error)
{
    if (!fixture || bytes.size() != kPreviewSyntheticFixtureBytes) {
        if (error) *error = "Synthetic fixture has an unsupported format";
        return false;
    }
    PreviewSyntheticFixture decoded{};
    size_t offset = 0;
    for (auto &color : decoded.corner_colors) {
        std::copy_n(bytes.data() + offset, color.size(), color.begin());
        offset += color.size();
    }
    for (auto &texel : decoded.texture_texels) {
        std::copy_n(bytes.data() + offset, texel.size(), texel.begin());
        offset += texel.size();
    }
    auto read_float = [&bytes, &offset](float *value) {
        std::memcpy(value, bytes.data() + offset, sizeof(*value));
        offset += sizeof(*value);
    };
    for (float &value : decoded.uv_scale) read_float(&value);
    for (float &value : decoded.uv_offset) read_float(&value);
    for (float &value : decoded.constant_color) read_float(&value);
    for (float &value : decoded.fog_color) read_float(&value);
    decoded.alpha_reference = bytes[offset++];
    decoded.linear_filter = bytes[offset++];
    decoded.repeat_wrap = bytes[offset++];
    if (bytes[offset++] != 'P' || bytes[offset++] != 'F' ||
        bytes[offset++] != 'X' || bytes[offset++] != 2) {
        if (error)
            *error = "Unsupported fixture encoding version";
        return false;
    }
    decoded.profile = static_cast<PreviewFixtureProfile>(bytes[offset++]);
    for (auto &p : decoded.textures)
        p = static_cast<PreviewFixtureProfile>(bytes[offset++]);
    for (auto &color : decoded.colors)
        for (float &v : color)
            read_float(&v);
    read_float(&decoded.fog);
    for (float &v : decoded.cube_direction)
        read_float(&v);
    auto valid_profile = [](PreviewFixtureProfile p) {
        return static_cast<unsigned>(p) <= 8;
    };
    bool valid = valid_profile(decoded.profile) && std::isfinite(decoded.fog) &&
                 decoded.fog >= 0 && decoded.fog <= 1;
    for (auto p : decoded.textures)
        valid &= valid_profile(p);
    for (auto color : decoded.colors)
        for (float v : color)
            valid &= std::isfinite(v) && v >= 0 && v <= 1;
    float length = 0;
    for (float v : decoded.cube_direction) {
        valid &= std::isfinite(v) && v >= -1 && v <= 1;
        length += v * v;
    }
    if (!valid || length < 0.0001f) {
        if (error)
            *error = "Unsupported fixture profile or input range";
        return false;
    }
    auto valid_range = [](float value, float low, float high) {
        return std::isfinite(value) && value >= low && value <= high;
    };
    for (float value : decoded.uv_scale) {
        if (!valid_range(value, -4.0f, 4.0f)) {
            if (error) *error = "Synthetic UV scale is outside [-4, 4]";
            return false;
        }
    }
    for (float value : decoded.uv_offset) {
        if (!valid_range(value, -4.0f, 4.0f)) {
            if (error) *error = "Synthetic UV offset is outside [-4, 4]";
            return false;
        }
    }
    for (float value : decoded.constant_color) {
        if (!valid_range(value, 0.0f, 1.0f)) {
            if (error) *error = "Synthetic constant color is outside [0, 1]";
            return false;
        }
    }
    for (float value : decoded.fog_color) {
        if (!valid_range(value, 0.0f, 1.0f)) {
            if (error) *error = "Synthetic fog color is outside [0, 1]";
            return false;
        }
    }
    if (decoded.linear_filter > 1 || decoded.repeat_wrap > 1) {
        if (error) *error = "Synthetic texture sampler setting is unsupported";
        return false;
    }
    *fixture = decoded;
    if (error) error->clear();
    return true;
}

bool CopyPreviewFragmentSource(const PreviewSelection &selection,
                               const DetailSnapshot &detail,
                               std::string *source, std::string *error)
{
    if (!source) {
        if (error) *error = "Preview source destination is null";
        return false;
    }
    source->clear();
    if (selection.mode == PreviewMode::Replacement ||
        detail.request.key != selection.shader ||
        (detail.state != DetailState::Complete &&
         detail.state != DetailState::Partial)) {
        if (error) *error = "Selected resident shader details are unavailable";
        return false;
    }
    const DetailBackend backend = selection.backend == PreviewBackend::OpenGL ?
        DetailBackend::OpenGL : DetailBackend::Vulkan;
    const Route route = selection.mode == PreviewMode::Uber ? Route::Uber :
                                                              Route::Specialized;
    for (const HostSource &candidate : detail.sources) {
        if (candidate.backend == backend &&
            candidate.stage == HostSourceStage::Fragment &&
            candidate.kind == HostSourceKind::Glsl &&
            candidate.route == route && !candidate.text.empty() &&
            candidate.text.size() <= kPreviewMaxSourceBytes) {
            *source = candidate.text;
            if (error) error->clear();
            return true;
        }
    }
    if (error) *error = "No resident fragment source matches the selected preview mode";
    return false;
}

std::string BuildPreviewSyntheticVertexSource(
    const std::string &fragment_source, PreviewBackend backend)
{
    if (backend != PreviewBackend::OpenGL &&
        backend != PreviewBackend::Vulkan) {
        return {};
    }
    const bool vulkan = backend == PreviewBackend::Vulkan;
    std::string result = vulkan ? "#version 450\n" : "#version 400\n";
    result += "layout(location = 0) in vec4 previewPosition;\n"
              "layout(location = 1) in vec4 previewColor;\n"
              "layout(location = 2) in vec2 previewUV;\n"
              "layout(location = 3) in vec4 previewD0;\n"
              "layout(location = 4) in vec4 previewD1;\n"
              "layout(location = 5) in vec4 previewB0;\n"
              "layout(location = 6) in vec4 previewB1;\n"
              "layout(location = 7) in float previewFog;\n"
              "layout(location = 8) in vec3 previewDirection;\n"
              "layout(location = 9) in vec4 previewCubeStages;\n";
    const char *names[] = { "vtxD0", "vtxD1", "vtxB0", "vtxB1",
                            "vtxFog", "vtxT0", "vtxT1", "vtxT2",
                            "vtxT3", "vtxPos0", "vtxPos1", "vtxPos2",
                            "triMZ" };
    for (unsigned i = 0; i < 13; ++i) {
        const bool flat = i >= 9 || i == 12 ||
            fragment_source.find(std::string("flat in ") +
                                 (i == 4 || i == 12 ? "float " : "vec4 ") +
                                 names[i]) != std::string::npos;
        if (vulkan) {
            result += "layout(location = " + std::to_string(i) + ") ";
        }
        if (flat) result += "flat ";
        result += "out ";
        result += (i == 4 || i == 12) ? "float " : "vec4 ";
        result += names[i];
        result += ";\n";
    }
    result += "void main() {\n"
              "  gl_Position = previewPosition;\n"
              "  vtxD0 = previewColor * previewD0; vtxD1 = previewD1;\n"
              "  vtxB0 = previewB0; vtxB1 = previewB1;\n"
              "  vtxFog = previewFog;\n"
              "  vtxT0 = vec4(previewUV, 0.0, 1.0);\n"
              "  vtxT1 = vtxT0; vtxT2 = vtxT0; vtxT3 = vtxT0;\n"
              "  vtxPos0 = gl_Position; vtxPos1 = gl_Position;\n"
              "  vtxPos2 = gl_Position; triMZ = 0.0;\n"
              "}\n";
    // Sampler types are supplied by the backend's linked/reflected interface.
    // GLSL comments, whitespace and preprocessor aliases cannot affect routing.
    std::string routing;
    const char *components[] = { "x", "y", "z", "w" };
    for (int i = 0; i < 4; ++i)
        routing += "vtxT" + std::to_string(i) + " = previewCubeStages." +
                   components[i] +
                   " > 0.5 ? vec4(previewDirection, 1.0) : vec4(previewUV, "
                   "0.0, 1.0);\n";
    result.insert(result.rfind('}'), routing);
    return result;
}

bool BuildPreviewPacket(const PreviewPacketInputs &inputs,
                        PreviewPacket *packet, std::string *error)
{
    auto fail = [error](const char *message) {
        if (error) {
            *error = message;
        }
        return false;
    };
    if (!packet) {
        return fail("Preview packet destination is null");
    }
    if (inputs.recipe.key != inputs.selection.shader) {
        return fail("Canonical recipe is not the selected shader");
    }
    if (!inputs.recipe.scopes.empty() &&
        std::find(inputs.recipe.scopes.begin(), inputs.recipe.scopes.end(),
                  inputs.selection.scope) == inputs.recipe.scopes.end()) {
        return fail("Canonical recipe has no selected title/build scope");
    }
    if (inputs.fixture_bytes.empty()) {
        return fail("Synthetic preview requires owned fixture bytes");
    }
    PreviewSyntheticFixture fixture{};
    if (!DecodePreviewSyntheticFixture(inputs.fixture_bytes, &fixture,
                                       error)) {
        return false;
    }
    size_t remaining = kPreviewMaxOwnedPacketBytes;
    auto charge = [&remaining](size_t capacity) {
        if (capacity > remaining) {
            return false;
        }
        remaining -= capacity;
        return true;
    };
    if (!charge(inputs.recipe.bytes.capacity()) ||
        !charge(inputs.source.capacity()) ||
        !charge(inputs.partner_source.capacity()) ||
        !charge(inputs.fixture_bytes.capacity())) {
        return fail("Preview packet exceeds the 32 MiB retained-data limit");
    }
    if (inputs.source.size() > kPreviewMaxSourceBytes ||
        inputs.partner_source.size() > kPreviewMaxSourceBytes ||
        inputs.recipe.bytes.size() > kPreviewMaxRecipeBytes) {
        return fail("Preview source or recipe exceeds its size limit");
    }

    PreviewPacket candidate{};
    candidate.selection = inputs.selection;
    candidate.recipe_format_version = inputs.recipe.recipe_format_version;
    candidate.recipe = inputs.recipe.bytes;
    candidate.source = inputs.source;
    candidate.partner_source = inputs.partner_source;
    candidate.fixture_bytes = inputs.fixture_bytes;
    candidate.generator_abi = inputs.generator_abi;
    candidate.interface_abi = inputs.interface_abi;
    candidate.replacement_id = inputs.replacement_id;
    candidate.replacement_revision = inputs.replacement_revision;
    candidate.input_revision = inputs.input_revision;
    candidate.view_revision = inputs.view_revision;
    candidate.scene = ClampPreviewScene(inputs.scene);
    candidate.width = inputs.width;
    candidate.height = inputs.height;
    candidate.update_policy = inputs.update_policy;
    candidate.packet_kind = PreviewPacketKind::Synthetic;
    candidate.replay_class = PreviewReplayClass::Synthetic;
    if (!candidate.source.empty()) {
        candidate.source_digest = ComputePreviewDigest(
            reinterpret_cast<const uint8_t *>(candidate.source.data()),
            candidate.source.size());
    }
    if (!candidate.partner_source.empty()) {
        candidate.partner_digest = ComputePreviewDigest(
            reinterpret_cast<const uint8_t *>(candidate.partner_source.data()),
            candidate.partner_source.size());
    }
    candidate.fixture_digest = ComputePreviewDigest(
        candidate.fixture_bytes.data(), candidate.fixture_bytes.size());
    if (!ValidatePreviewPacket(candidate, error)) {
        return false;
    }
    *packet = std::move(candidate);
    return true;
}

} // namespace xemu::shader_browser

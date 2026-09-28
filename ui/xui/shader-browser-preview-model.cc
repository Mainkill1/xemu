// SPDX-License-Identifier: GPL-2.0-or-later
#include "shader-browser-preview-model.hh"

#include <algorithm>
#include <cmath>
#include <limits>
#include <cstring>

#include <xxhash.h>
#include <tuple>

namespace xemu::shader_browser {
namespace {

bool ContainsNul(const std::string &value)
{
    return value.find('\0') != std::string::npos;
}

bool DigestIsZero(const PreviewDigest &digest)
{
    for (uint8_t byte : digest) {
        if (byte != 0) {
            return false;
        }
    }
    return true;
}

bool CheckedAdd(size_t lhs, size_t rhs, size_t *result)
{
    if (std::numeric_limits<size_t>::max() - lhs < rhs) {
        return false;
    }
    *result = lhs + rhs;
    return true;
}

bool CheckedMultiply(size_t lhs, size_t rhs, size_t *result)
{
    if (rhs && lhs > std::numeric_limits<size_t>::max() / rhs)
        return false;
    *result = lhs * rhs;
    return true;
}

} // namespace

bool PreviewCapturedUniformAllowed(const OwnedDrawUniform &uniform)
{
    if (uniform.stage != 0 && uniform.stage != 2)
        return false;
    std::string name = uniform.name.substr(0, uniform.name.find('['));
    return name == "alphaRef" || name == "fogColor" || name == "consts" ||
           name == "texScale" || name == "bumpMat" || name == "bumpOffset" ||
           name == "bumpScale" || name == "colorKey" || name == "colorKeyMask";
}

size_t PreviewCapturedAttributeElementBytes(PreviewBackend backend,
                                            const OwnedDrawBlob &stream)
{
    const uint32_t components =
        stream.components == 0x80e1 ? 4 : stream.components;
    if (!components || components > 4)
        return 0;
    if (backend == PreviewBackend::OpenGL) {
        switch (stream.format) {
        case 0x1400:
        case 0x1401:
            return components;
        case 0x1402:
        case 0x1403:
        case 0x140b:
            return components * 2;
        case 0x1404:
        case 0x1405:
        case 0x1406:
            return components * 4;
        case 0x8368:
        case 0x8d9f:
            return components == 4 ? 4 : 0;
        default:
            return 0;
        }
    }
    if (backend == PreviewBackend::Vulkan) {
        if (stream.format >= 98 && stream.format <= 109)
            return ((stream.format - 98) / 3 + 1) * 4;
        for (uint32_t n = 0; n < 4; ++n) {
            if ((n < 3 && stream.format == 9 + 7 * n) ||
                (n == 3 && stream.format == 37))
                return n + 1;
            if (stream.format == 71 + 7 * n || stream.format == 76 + 7 * n)
                return (n + 1) * 2;
        }
    }
    return 0;
}

bool DecodePreviewCapturedRaster(const OwnedDrawInputs &inputs,
                                 PreviewBackend backend,
                                 PreviewCapturedRaster *out, std::string *error)
{
    PreviewCapturedRaster state;
    bool invalid = false;
    auto reg = [&](const std::string &name, uint32_t *value) {
        bool found = false;
        for (const auto &item : inputs.registers)
            if (item.name == name) {
                if (found)
                    invalid = true;
                *value = item.value;
                found = true;
            }
        return found;
    };
    auto blob = [&](const char *name, void *value, size_t bytes) {
        bool found = false;
        for (const auto &item : inputs.blobs)
            if (item.name == name) {
                if (found || item.bytes.size() != bytes) {
                    invalid = true;
                    continue;
                }
                std::memcpy(value, item.bytes.data(), bytes);
                found = true;
            }
        return found;
    };
    auto boolean = [&](const std::string &name, bool *value) {
        uint32_t raw = 0;
        if (!reg(name, &raw))
            return false;
        if (raw > 1)
            invalid = true;
        *value = raw != 0;
        return true;
    };
    auto gl_enum = [&](const char *name, uint32_t *value,
                       const std::vector<uint32_t> &choices) {
        uint32_t raw;
        if (!reg(name, &raw))
            return false;
        auto found = std::find(choices.begin(), choices.end(), raw);
        if (found == choices.end())
            invalid = true;
        else
            *value = uint32_t(found - choices.begin());
        return true;
    };
    const std::vector<uint32_t> factors{
        0,      1,      0x0300, 0x0301, 0x0306, 0x0307, 0x0302, 0x0303,
        0x0304, 0x0305, 0x8001, 0x8002, 0x8003, 0x8004, 0x0308
    };
    const std::vector<uint32_t> operations{ 0x8006, 0x800a, 0x800b, 0x8007,
                                            0x8008 };
    const std::vector<uint32_t> compares{ 0x0200, 0x0201, 0x0202, 0x0203,
                                          0x0204, 0x0205, 0x0206, 0x0207 };
    const std::vector<uint32_t> stencil_ops{ 0x1e00, 0,      0x1e01, 0x1e02,
                                             0x1e03, 0x150a, 0x8507, 0x8508 };
    if (backend == PreviewBackend::OpenGL) {
        int32_t viewport[4]{};
        if (blob("host.viewport", viewport, sizeof(viewport))) {
            if (viewport[0] || viewport[1] || viewport[2] <= 0 ||
                viewport[3] <= 0)
                invalid = true;
            else {
                state.width = viewport[2];
                state.height = viewport[3];
                state.available |= PreviewRasterViewport;
            }
        }
        int32_t scissor[4]{};
        if (boolean("host.scissor_enabled", &state.scissor_enabled) &&
            blob("host.scissor", scissor, sizeof(scissor))) {
            if (scissor[2] < 0 || scissor[3] < 0 || !state.height ||
                int64_t(state.height) - scissor[1] - scissor[3] < INT32_MIN ||
                int64_t(state.height) - scissor[1] - scissor[3] > INT32_MAX)
                invalid = true;
            else {
                state.scissor_x = scissor[0];
                state.scissor_y =
                    int64_t(state.height) - scissor[1] - scissor[3];
                state.scissor_width = scissor[2];
                state.scissor_height = scissor[3];
                state.available |= PreviewRasterScissor;
            }
        }
        uint8_t write[4]{};
        if (blob("host.color_write", write, sizeof(write))) {
            state.color_write = 0;
            for (size_t i = 0; i < 4; ++i) {
                if (write[i] > 1)
                    invalid = true;
                state.color_write |= uint32_t(write[i] != 0) << i;
            }
            state.available |= PreviewRasterColorWrite;
        }
        if (boolean("host.blend_enabled", &state.blend_enabled) &&
            gl_enum("host.blend_src_rgb", &state.src_rgb, factors) &&
            gl_enum("host.blend_dst_rgb", &state.dst_rgb, factors) &&
            gl_enum("host.blend_src_alpha", &state.src_alpha, factors) &&
            gl_enum("host.blend_dst_alpha", &state.dst_alpha, factors) &&
            gl_enum("host.blend_equation_rgb", &state.blend_rgb, operations) &&
            gl_enum("host.blend_equation_alpha", &state.blend_alpha,
                    operations) &&
            blob("host.blend_color", state.blend_color.data(),
                 sizeof(state.blend_color)))
            state.available |= PreviewRasterBlend;
        double range[2]{};
        if (boolean("host.depth_enabled", &state.depth_test) &&
            boolean("host.depth_write", &state.depth_write) &&
            gl_enum("host.depth_func", &state.depth_compare, compares) &&
            blob("host.depth_range", range, sizeof(range))) {
            state.depth_min = range[0];
            state.depth_max = range[1];
            state.available |= PreviewRasterDepth;
        }
        if (boolean("host.depth_clamp_enabled", &state.depth_clamp))
            state.available |= PreviewRasterDepthClamp;
        uint32_t sample_buffers = 0, samples = 0;
        bool alpha_coverage = false, alpha_one = false;
        bool sample_coverage = false, sample_mask = false;
        if (reg("host.sample_buffers", &sample_buffers) &&
            reg("host.samples", &samples) &&
            boolean("host.sample_alpha_to_coverage", &alpha_coverage) &&
            boolean("host.sample_alpha_to_one", &alpha_one) &&
            boolean("host.sample_coverage_enabled", &sample_coverage) &&
            boolean("host.sample_mask_enabled", &sample_mask)) {
            // The owned preview target is single-sample. Retain unsupported
            // states as raw evidence and refuse to replay them with defaults.
            if (sample_buffers || samples || alpha_coverage || alpha_one ||
                sample_coverage || sample_mask)
                invalid = true;
            state.available |= PreviewRasterCoverage;
        }
        auto stencil = [&](const char *prefix, PreviewCapturedStencil *face) {
            const std::string p = prefix;
            return gl_enum((p + "func").c_str(), &face->compare, compares) &&
                   reg(p + "ref", &face->reference) &&
                   reg(p + "read_mask", &face->read_mask) &&
                   reg(p + "write_mask", &face->write_mask) &&
                   gl_enum((p + "fail").c_str(), &face->fail, stencil_ops) &&
                   gl_enum((p + "depth_fail").c_str(), &face->depth_fail,
                           stencil_ops) &&
                   gl_enum((p + "pass").c_str(), &face->pass, stencil_ops);
        };
        if (boolean("host.stencil_enabled", &state.stencil_test) &&
            stencil("host.stencil_", &state.front_stencil) &&
            stencil("host.stencil_back_", &state.back_stencil))
            state.available |= PreviewRasterStencil;
        bool cull = false;
        uint32_t face = 0, front = 0;
        if (boolean("host.cull_enabled", &cull) &&
            reg("host.cull_face", &face) && reg("host.front_face", &front)) {
            if ((face != 0x0404 && face != 0x0405 && face != 0x0408) ||
                (front != 0x0900 && front != 0x0901))
                invalid = true;
            state.cull_mode = !cull          ? 0 :
                              face == 0x0404 ? 1 :
                              face == 0x0405 ? 2 :
                                               3;
            state.front_ccw = front == 0x0901;
            state.available |= PreviewRasterCull;
        }
        if (cull && !(state.available & PreviewRasterCull))
            invalid = true;
        if (boolean("host.polygon_offset", &state.depth_bias)) {
            if (state.depth_bias)
                invalid = true; // Coefficients were not captured by this ABI.
            state.available |= PreviewRasterDepthBias;
        }
    } else if (backend == PreviewBackend::Vulkan) {
        float viewport[6]{};
        int32_t scissor[4]{};
        if (blob("vk.viewport", viewport, sizeof(viewport))) {
            if (viewport[0] || viewport[1] || !std::isfinite(viewport[2]) ||
                !std::isfinite(viewport[3]) || viewport[2] <= 0 ||
                viewport[3] <= 0 || viewport[2] > kPreviewMaxCapturedWidth ||
                viewport[3] > kPreviewMaxCapturedHeight ||
                std::trunc(viewport[2]) != viewport[2] ||
                std::trunc(viewport[3]) != viewport[3])
                invalid = true;
            else {
                state.width = viewport[2];
                state.height = viewport[3];
                state.depth_min = viewport[4];
                state.depth_max = viewport[5];
                state.available |= PreviewRasterViewport;
            }
        }
        if (blob("vk.scissor", scissor, sizeof(scissor))) {
            if (scissor[2] < 0 || scissor[3] < 0)
                invalid = true;
            else {
                state.scissor_x = scissor[0];
                state.scissor_y = scissor[1];
                state.scissor_width = scissor[2];
                state.scissor_height = scissor[3];
                state.scissor_enabled = true;
                state.available |= PreviewRasterScissor;
            }
        }
        // ABI1 stores native 64-bit Vk structures with all pointer members
        // zero. Decode scalar offsets only; no native structure or pointer is
        // retained.
        auto wire = [&](const char *name, size_t bytes,
                        uint32_t kind) -> std::vector<uint32_t> {
            std::vector<uint32_t> data(bytes / 4);
            if (!blob(name, data.data(), bytes))
                return {};
            uint32_t abi = 0;
            if (!reg("capture.vk.pipeline_abi", &abi) || abi != 1 ||
                data[0] != kind || data[2] || data[3] || data[4])
                invalid = true;
            return data;
        };
        auto assembly = wire("vk.pipeline.assembly", 32, 20);
        if (!assembly.empty() &&
            (assembly[5] < 3 || assembly[5] > 7 || assembly[6]))
            invalid = true;
        auto raster = wire("vk.pipeline.raster", 64, 23);
        if (!raster.empty()) {
            if (raster[5] > 1 || raster[6] || raster[7] || raster[8] > 3 ||
                raster[9] > 1 || raster[10] > 1)
                invalid = true;
            float line, clamp;
            std::memcpy(&state.bias_constant, &raster[11], 4);
            std::memcpy(&clamp, &raster[12], 4);
            std::memcpy(&state.bias_slope, &raster[13], 4);
            std::memcpy(&line, &raster[14], 4);
            uint32_t dynamic_line = 0;
            if (!reg("capture.vk.dynamic_line_width", &dynamic_line) ||
                dynamic_line > 1)
                invalid = true;
            if (dynamic_line && !blob("vk.line_width", &line, sizeof(line)))
                invalid = true;
            if (clamp != 0 || line != 1)
                invalid = true;
            state.cull_mode = raster[8];
            state.front_ccw = raster[9] == 0;
            state.depth_bias = raster[10] != 0;
            state.depth_clamp = raster[5] != 0;
            state.available |= PreviewRasterCull | PreviewRasterDepthBias |
                               PreviewRasterDepthClamp;
        }
        auto ms = wire("vk.pipeline.multisample", 48, 24);
        if (!ms.empty()) {
            if (ms[5] != 1 || ms[6] || ms[8] || ms[9] || ms[10] || ms[11])
                invalid = true;
            state.available |= PreviewRasterCoverage;
        }
        auto depth = wire("vk.pipeline.depth_stencil", 104, 25);
        if (!depth.empty()) {
            if (depth[5] > 1 || depth[6] > 1 || depth[7] > 7 || depth[8] ||
                depth[9] > 1)
                invalid = true;
            state.depth_test = depth[5];
            state.depth_write = depth[6];
            state.depth_compare = depth[7];
            state.stencil_test = depth[9];
            auto face = [&](size_t n, PreviewCapturedStencil *value) {
                value->fail = depth[n];
                value->pass = depth[n + 1];
                value->depth_fail = depth[n + 2];
                value->compare = depth[n + 3];
                value->read_mask = depth[n + 4];
                value->write_mask = depth[n + 5];
                value->reference = depth[n + 6];
            };
            face(10, &state.front_stencil);
            face(17, &state.back_stencil);
            state.available |= PreviewRasterDepth | PreviewRasterStencil;
        }
        auto blend = wire("vk.pipeline.blend", 56, 26);
        uint32_t attachment[8]{};
        if (!blend.empty()) {
            if (blend[5] || blend[7] != 1 || blend[8] || blend[9])
                invalid = true;
            std::memcpy(state.blend_color.data(), blend.data() + 10, 16);
            uint32_t dynamic_mask = 0;
            if (!reg("capture.vk.dynamic_blend_constant_mask", &dynamic_mask))
                invalid = true;
            if (dynamic_mask &&
                !blob("vk.blend_constants", state.blend_color.data(), 16))
                invalid = true;
            if (blob("vk.pipeline.blend_attachment", attachment,
                     sizeof(attachment))) {
                state.blend_enabled = attachment[0];
                state.src_rgb = attachment[1];
                state.dst_rgb = attachment[2];
                state.blend_rgb = attachment[3];
                state.src_alpha = attachment[4];
                state.dst_alpha = attachment[5];
                state.blend_alpha = attachment[6];
                state.color_write = attachment[7];
                if (attachment[0] > 1)
                    invalid = true;
                state.available |= PreviewRasterBlend | PreviewRasterColorWrite;
            }
        }
    } else
        invalid = true;
    auto finite = [](double value) { return std::isfinite(value); };
    const auto stencil_valid = [](const PreviewCapturedStencil &s) {
        return s.fail <= 7 && s.pass <= 7 && s.depth_fail <= 7 &&
               s.compare <= 7;
    };
    if ((state.scissor_enabled && !(state.available & PreviewRasterScissor)) ||
        (state.blend_enabled && !(state.available & PreviewRasterBlend)) ||
        (state.depth_test && !(state.available & PreviewRasterDepth)) ||
        (state.stencil_test && !(state.available & PreviewRasterStencil)))
        invalid = true;
    if (((state.available & PreviewRasterViewport) &&
         !PreviewExtentWithinLimits(state.width, state.height, true)) ||
        state.width > kPreviewMaxCapturedWidth ||
        state.height > kPreviewMaxCapturedHeight || state.src_rgb > 14 ||
        state.dst_rgb > 14 || state.src_alpha > 14 || state.dst_alpha > 14 ||
        state.blend_rgb > 4 || state.blend_alpha > 4 ||
        state.color_write > 15 || !finite(state.depth_min) ||
        !finite(state.depth_max) || state.depth_min < 0 ||
        state.depth_min > 1 || state.depth_max < 0 || state.depth_max > 1 ||
        !finite(state.bias_constant) || !finite(state.bias_slope) ||
        !stencil_valid(state.front_stencil) ||
        !stencil_valid(state.back_stencil) ||
        !std::all_of(state.blend_color.begin(), state.blend_color.end(),
                     finite))
        invalid = true;
    if (invalid) {
        if (error)
            *error =
                "Captured native raster state is malformed or unsupported "
                "(logic, MSAA, wireframe, bias clamp or missing coefficients)";
        return false;
    }
    *out = state;
    if (error)
        error->clear();
    return true;
}

std::string
DescribePreviewCapturedRaster(const PreviewCapturedPipeline &pipeline)
{
    std::string result =
        "Original VS/GS camera; captured raster state; color destination " +
        (pipeline.color_before.rgba.empty() ?
             std::string("unavailable (preview zero clear)") :
             std::string("seeded from owned before image")) +
        "; preview RGBA8 destination format; depth/stencil "
        "destination unavailable (preview depth 1/stencil 0)";
    const std::pair<uint32_t, const char *> components[] = {
        { PreviewRasterViewport, "viewport" },
        { PreviewRasterScissor, "scissor" },
        { PreviewRasterBlend, "blending" },
        { PreviewRasterColorWrite, "color writes" },
        { PreviewRasterDepth, "depth" },
        { PreviewRasterStencil, "stencil" },
        { PreviewRasterCull, "culling" },
        { PreviewRasterDepthBias, "depth bias" },
        { PreviewRasterCoverage, "multisample coverage" },
        { PreviewRasterDepthClamp, "depth clamp" }
    };
    if (pipeline.raster.available != 1023) {
        result += "; raster evidence unavailable: ";
        bool first = true;
        for (const auto &component : components)
            if (!(pipeline.raster.available & component.first)) {
                if (!first)
                    result += ", ";
                result += component.second;
                first = false;
            }
        result += " (preview defaults)";
    }
    if (!pipeline.host_topology_captured)
        result += "; legacy triangle-list host topology inferred";
    return result;
}

bool ValidatePreviewCapturedPipeline(const PreviewCapturedPipeline &pipeline,
                                     std::string *error)
{
    auto fail = [&](const char *message) {
        if (error)
            *error = message;
        return false;
    };
    static constexpr uint32_t vk_topology[] = { 3, 4, 5, 6, 7, 5 };
    static constexpr uint32_t gl_topology[] = { 4, 5, 6, 10, 11, 6 };
    if (pipeline.guest_primitive_mode < 5 ||
        pipeline.guest_primitive_mode > 10 ||
        pipeline.host_topology !=
            (pipeline.backend == PreviewBackend::Vulkan ?
                 vk_topology :
                 gl_topology)[pipeline.guest_primitive_mode - 5] ||
        (!pipeline.host_topology_captured &&
         pipeline.guest_primitive_mode != 5))
        return fail("Original camera guest and captured host topology disagree "
                    "or the filled topology is unsupported");
    if (pipeline.primitive_restart)
        return fail("Original camera primitive restart is unsupported");
    if ((pipeline.guest_primitive_mode == 8 ||
         pipeline.guest_primitive_mode == 9) &&
        pipeline.geometry_source.empty())
        return fail("Original camera quad adjacency requires the captured "
                    "geometry input stage");
    const auto &r = pipeline.raster;
    const auto stencil_valid = [](const PreviewCapturedStencil &s) {
        return s.fail <= 7 && s.pass <= 7 && s.depth_fail <= 7 &&
               s.compare <= 7;
    };
    if (r.available > 1023 ||
        (r.depth_clamp && !(r.available & PreviewRasterDepthClamp)) ||
        ((r.available & PreviewRasterViewport) &&
         !PreviewExtentWithinLimits(r.width, r.height, true)) ||
        r.width > kPreviewMaxCapturedWidth ||
        r.height > kPreviewMaxCapturedHeight || r.cull_mode > 3 ||
        r.color_write > 15 || r.src_rgb > 14 || r.dst_rgb > 14 ||
        r.src_alpha > 14 || r.dst_alpha > 14 || r.blend_rgb > 4 ||
        r.blend_alpha > 4 || r.depth_compare > 7 ||
        !std::isfinite(r.depth_min) || !std::isfinite(r.depth_max) ||
        r.depth_min < 0 || r.depth_min > 1 || r.depth_max < 0 ||
        r.depth_max > 1 || !std::isfinite(r.bias_constant) ||
        !std::isfinite(r.bias_slope) || !stencil_valid(r.front_stencil) ||
        !stencil_valid(r.back_stencil) ||
        !std::all_of(r.blend_color.begin(), r.blend_color.end(),
                     [](float v) { return std::isfinite(v); }))
        return fail(
            "Captured native raster values exceed the supported interface");
    const auto &before = pipeline.color_before;
    if ((!before.rgba.empty() &&
         (!PreviewExtentWithinLimits(before.width, before.height, true) ||
          before.rgba.size() != size_t(before.width) * before.height * 4)) ||
        (before.rgba.empty() && (before.width || before.height)))
        return fail("Captured native color destination is malformed or exceeds "
                    "1920 x 1080");
    if ((r.blend_enabled || r.color_write != 15) && before.rgba.empty())
        return fail("Captured blending or color masking requires the owned "
                    "before destination");
    size_t retained = sizeof(pipeline);
    auto charge = [&](size_t bytes) {
        return CheckedAdd(retained, bytes, &retained) &&
               retained <= kPreviewMaxCapturedPipelineBytes;
    };
    if (!charge(pipeline.geometry_source.capacity()) ||
        !charge(before.rgba.capacity()) ||
        !charge(pipeline.indices.capacity() * sizeof(uint32_t)) ||
        !charge(pipeline.ranges.capacity() * sizeof(pipeline.ranges[0])) ||
        !charge(pipeline.uniforms.capacity() * sizeof(OwnedDrawUniform)))
        return fail("Owned native pipeline exceeds the 16 MiB limit");
    for (const auto &attribute : pipeline.attributes)
        if (!charge(attribute.stream.bytes.capacity()) ||
            !charge(attribute.stream.name.capacity()))
            return fail("Owned native pipeline exceeds the 16 MiB limit");
    for (const auto &uniform : pipeline.uniforms)
        if (!charge(uniform.data.capacity()) ||
            !charge(uniform.name.capacity()))
            return fail("Owned native pipeline exceeds the 16 MiB limit");
    if ((pipeline.backend != PreviewBackend::OpenGL &&
         pipeline.backend != PreviewBackend::Vulkan) ||
        pipeline.uniform_attribute_mask > 0xffff || !pipeline.vertex_count ||
        pipeline.first_vertex > 4096 ||
        pipeline.vertex_count > 4096 - pipeline.first_vertex ||
        pipeline.geometry_source.size() > kPreviewMaxSourceBytes ||
        ContainsNul(pipeline.geometry_source) ||
        pipeline.uniforms.size() > 256 || pipeline.ranges.size() > 64 ||
        pipeline.indices.size() > 12288)
        return fail("Original camera pipeline exceeds a supported input bound");
    if (pipeline.indices.empty() == pipeline.ranges.empty())
        return fail(
            "Original camera pipeline requires one indexed or array command");
    for (uint32_t index : pipeline.indices)
        if (index < pipeline.first_vertex ||
            index - pipeline.first_vertex >= pipeline.vertex_count)
            return fail(
                "Original camera index exceeds the captured vertex span");
    for (const auto &range : pipeline.ranges)
        if (!range[1] || range[0] < pipeline.first_vertex ||
            range[0] - pipeline.first_vertex >= pipeline.vertex_count ||
            range[1] >
                pipeline.vertex_count - (range[0] - pipeline.first_vertex))
            return fail(
                "Original camera array range exceeds the captured vertex span");
    size_t padded_bytes = 0;
    for (size_t slot = 0; slot < 16; ++slot) {
        const auto &attribute = pipeline.attributes[slot];
        const auto &stream = attribute.stream;
        if (stream.name.empty())
            continue; // Preparation rejects a missing active attribute.
        const size_t element =
            PreviewCapturedAttributeElementBytes(pipeline.backend, stream);
        if (stream.slot != slot || !element || !stream.stride ||
            stream.stride > 4096 || stream.normalized > 1 ||
            stream.integer > 1 ||
            (attribute.enabled &&
             (stream.count != pipeline.vertex_count ||
              stream.bytes.size() !=
                  size_t(stream.count - 1) * stream.stride + element)) ||
            (!attribute.enabled &&
             (stream.components != 4 || stream.bytes.size() != 16)))
            return fail(
                "Original camera attribute stream is missing or unsupported");
        if (attribute.enabled)
            padded_bytes += size_t(pipeline.first_vertex) * stream.stride +
                            stream.bytes.size();
    }
    if (padded_bytes > kPreviewMaxCapturedPipelineBytes)
        return fail("Original camera padded host streams exceed the 16 MiB "
                    "worker bound");
    for (const auto &uniform : pipeline.uniforms) {
        if (uniform.stage > 3 || uniform.name.empty() ||
            uniform.name.size() > 255 || ContainsNul(uniform.name) ||
            !uniform.count || uniform.count > 4096 || !uniform.components ||
            uniform.components > 16 || uniform.data.size() > 65536 ||
            uniform.type < XEMU_SHADER_DRAW_UNIFORM_FLOAT ||
            uniform.type > XEMU_SHADER_DRAW_UNIFORM_MAT4 ||
            (uniform.type <= XEMU_SHADER_DRAW_UNIFORM_UINT &&
             uniform.components > 4) ||
            (uniform.type == XEMU_SHADER_DRAW_UNIFORM_MAT2 &&
             uniform.components != 4) ||
            (uniform.type == XEMU_SHADER_DRAW_UNIFORM_MAT4 &&
             uniform.components != 16) ||
            uniform.data.size() !=
                size_t(uniform.count) * uniform.components * 4)
            return fail("Original camera uniform is missing or unsupported");
    }
    if (error)
        error->clear();
    return true;
}

std::shared_ptr<const PreviewCapturedPipeline>
BuildPreviewCapturedPipeline(const OwnedDrawInputs &inputs,
                             PreviewBackend backend, uint32_t primitive_mode,
                             std::string *error, size_t max_bytes)
{
    auto fail = [&](const char *message)
        -> std::shared_ptr<const PreviewCapturedPipeline> {
        if (error)
            *error = message;
        return {};
    };
    if (!inputs.complete || inputs.sources[1].empty() || primitive_mode < 5 ||
        primitive_mode > 10)
        return fail("Original camera requires complete inputs, an owned VS and "
                    "a supported filled topology command");
    auto pipeline = std::make_shared<PreviewCapturedPipeline>();
    pipeline->backend = backend;
    pipeline->guest_primitive_mode = primitive_mode;
    // Legacy triangle-list captures predate explicit host assembly evidence.
    // Other topologies always require the actual captured backend command.
    if (primitive_mode == 5)
        pipeline->host_topology = backend == PreviewBackend::Vulkan ? 3 : 4;
    if (backend == PreviewBackend::Vulkan) {
        const OwnedDrawBlob *assembly = nullptr;
        for (const auto &blob : inputs.blobs)
            if (blob.name == "vk.pipeline.assembly") {
                if (assembly || blob.bytes.size() != 32)
                    return fail("Captured host topology assembly is malformed");
                assembly = &blob;
            }
        if (assembly) {
            uint32_t words[8];
            std::memcpy(words, assembly->bytes.data(), sizeof(words));
            if (words[0] != 20 || words[2] || words[3] || words[4] ||
                words[6] > 1)
                return fail("Captured host topology assembly is malformed");
            pipeline->host_topology = words[5];
            pipeline->primitive_restart = words[6] != 0;
            pipeline->host_topology_captured = true;
        }
    } else {
        bool found_topology = false, found_restart = false;
        for (const auto &value : inputs.registers) {
            if (value.name == "host.primitive_mode") {
                if (found_topology || value.value > UINT32_MAX)
                    return fail("Captured host topology is malformed");
                pipeline->host_topology = value.value;
                found_topology = true;
            } else if (value.name == "host.primitive_restart") {
                if (found_restart || value.value > 1)
                    return fail("Captured primitive restart is malformed");
                pipeline->primitive_restart = value.value != 0;
                found_restart = true;
            }
        }
        if (found_topology && !found_restart)
            return fail("Captured host topology requires primitive restart "
                        "evidence");
        pipeline->host_topology_captured = found_topology;
    }
    if (!DecodePreviewCapturedRaster(inputs, backend, &pipeline->raster, error))
        return {};
    auto reg = [&](const std::string &name, uint32_t fallback) {
        for (const auto &value : inputs.registers)
            if (value.name == name)
                return value.value;
        return fallback;
    };
    pipeline->first_vertex =
        reg(backend == PreviewBackend::OpenGL ? "capture.first_vertex" :
                                                "capture.vertices.first",
            UINT32_MAX);
    pipeline->uniform_attribute_mask = reg("capture.vertices.uniform_mask", 0);
    const uint32_t last = reg("capture.last_vertex", UINT32_MAX);
    pipeline->vertex_count = backend == PreviewBackend::OpenGL &&
                                     last != UINT32_MAX &&
                                     last >= pipeline->first_vertex ?
                                 last - pipeline->first_vertex + 1 :
                                 reg("capture.vertices.count", 0);
    size_t bytes = sizeof(*pipeline);
    max_bytes = std::min(max_bytes, kPreviewMaxCapturedPipelineBytes);
    auto charge = [&](size_t count) {
        if (count > max_bytes - std::min(bytes, max_bytes))
            return false;
        bytes += count;
        return true;
    };
    if (!charge(inputs.sources[3].size()))
        return fail(
            "Original camera geometry source exceeds the packet budget");
    pipeline->geometry_source = inputs.sources[3];
    if (!inputs.before.rgba.empty()) {
        if (!charge(inputs.before.rgba.size()))
            return fail(
                "Owned before destination exceeds the native packet budget");
        pipeline->color_before = inputs.before;
    }
    const OwnedDrawBlob *starts = nullptr, *counts = nullptr;
    for (const auto &blob : inputs.blobs) {
        if (blob.name == "geometry.host_indices" ||
            blob.name == "vertex.indices") {
            if (blob.count > 12288 ||
                blob.bytes.size() != size_t(blob.count) * 4 ||
                !charge(blob.bytes.size()))
                return fail("Original camera index evidence exceeds its bound");
            pipeline->indices.resize(blob.count);
            std::memcpy(pipeline->indices.data(), blob.bytes.data(),
                        blob.bytes.size());
        } else if (blob.name == "geometry.draw_starts")
            starts = &blob;
        else if (blob.name == "geometry.draw_counts")
            counts = &blob;
        else if (blob.slot < 16 &&
                 (blob.name == "vertex.attribute" + std::to_string(blob.slot) ||
                  blob.name == "vertex.current" + std::to_string(blob.slot))) {
            auto &attribute = pipeline->attributes[blob.slot];
            const bool enabled =
                reg(backend == PreviewBackend::OpenGL ?
                        "vertex.enabled" + std::to_string(blob.slot) :
                        "capture.vertex." + std::to_string(blob.slot) +
                            ".enabled",
                    0) != 0;
            if (backend == PreviewBackend::Vulkan &&
                ((enabled && blob.name.find("current") != std::string::npos) ||
                 (!enabled &&
                  blob.name.find("attribute") != std::string::npos)))
                continue;
            if (!charge(blob.bytes.size() + blob.name.size()))
                return fail(
                    "Original camera attributes exceed the packet budget");
            attribute.enabled = enabled;
            attribute.stream = blob;
        }
    }
    if (pipeline->indices.empty()) {
        if (backend == PreviewBackend::OpenGL) {
            if (!starts || !counts || starts->count != counts->count ||
                starts->count > 64 ||
                starts->bytes.size() != size_t(starts->count) * 4 ||
                counts->bytes.size() != starts->bytes.size())
                return fail("Original camera array command ranges are missing");
            for (size_t i = 0; i < starts->count; ++i) {
                std::array<uint32_t, 2> range;
                std::memcpy(&range[0], starts->bytes.data() + i * 4, 4);
                std::memcpy(&range[1], counts->bytes.data() + i * 4, 4);
                pipeline->ranges.push_back(range);
            }
        } else
            pipeline->ranges.push_back(
                { pipeline->first_vertex, pipeline->vertex_count });
    }
    for (const auto &uniform : inputs.uniforms) {
        if (!charge(sizeof(uniform) + uniform.name.size() +
                    uniform.data.size()))
            return fail("Original camera uniforms exceed the packet budget");
        pipeline->uniforms.push_back(uniform);
    }
    if (!ValidatePreviewCapturedPipeline(*pipeline, error))
        return {};
    return pipeline;
}

PreviewDigest
ComputePreviewCapturedPipelineDigest(const PreviewCapturedPipeline &pipeline,
                                     bool layout_only)
{
    std::vector<uint8_t> bytes;
    auto append = [&](const auto &value) {
        const auto *data = reinterpret_cast<const uint8_t *>(&value);
        bytes.insert(bytes.end(), data, data + sizeof(value));
    };
    append(pipeline.backend);
    append(pipeline.guest_primitive_mode);
    append(pipeline.host_topology);
    append(pipeline.primitive_restart);
    append(pipeline.host_topology_captured);
    append(pipeline.first_vertex);
    append(pipeline.vertex_count);
    append(pipeline.indices.size());
    append(pipeline.uniform_attribute_mask);
    const auto &r = pipeline.raster;
    append(r.available);
    append(r.width);
    append(r.height);
    append(r.scissor_enabled);
    append(r.scissor_x);
    append(r.scissor_y);
    append(r.scissor_width);
    append(r.scissor_height);
    append(r.blend_enabled);
    append(r.src_rgb);
    append(r.dst_rgb);
    append(r.src_alpha);
    append(r.dst_alpha);
    append(r.blend_rgb);
    append(r.blend_alpha);
    append(r.color_write);
    for (float value : r.blend_color)
        append(value);
    append(r.depth_test);
    append(r.depth_write);
    append(r.depth_clamp);
    append(r.depth_compare);
    append(r.depth_min);
    append(r.depth_max);
    append(r.stencil_test);
    auto stencil = [&](const PreviewCapturedStencil &s) {
        append(s.fail);
        append(s.pass);
        append(s.depth_fail);
        append(s.compare);
        append(s.read_mask);
        append(s.write_mask);
        append(s.reference);
    };
    stencil(r.front_stencil);
    stencil(r.back_stencil);
    append(r.cull_mode);
    append(r.front_ccw);
    append(r.depth_bias);
    append(r.bias_constant);
    append(r.bias_slope);
    append(pipeline.geometry_source.size());
    bytes.insert(bytes.end(), pipeline.geometry_source.begin(),
                 pipeline.geometry_source.end());
    for (const auto &attribute : pipeline.attributes) {
        const auto &stream = attribute.stream;
        append(attribute.enabled);
        append(stream.format);
        append(stream.components);
        append(stream.stride);
        append(stream.normalized);
        append(stream.integer);
        if (!layout_only)
            bytes.insert(bytes.end(), stream.bytes.begin(), stream.bytes.end());
    }
    if (!layout_only) {
        append(pipeline.color_before.width);
        append(pipeline.color_before.height);
        bytes.insert(bytes.end(), pipeline.color_before.rgba.begin(),
                     pipeline.color_before.rgba.end());
        append(pipeline.first_vertex);
        append(pipeline.vertex_count);
        append(pipeline.indices.size());
        append(pipeline.ranges.size());
        for (uint32_t index : pipeline.indices)
            append(index);
        for (const auto &range : pipeline.ranges)
            append(range);
        for (const auto &uniform : pipeline.uniforms) {
            append(uniform.stage);
            append(uniform.type);
            append(uniform.components);
            append(uniform.count);
            append(uniform.name.size());
            bytes.insert(bytes.end(), uniform.name.begin(), uniform.name.end());
            append(uniform.data.size());
            bytes.insert(bytes.end(), uniform.data.begin(), uniform.data.end());
        }
    }
    return ComputePreviewDigest(bytes.data(), bytes.size());
}

std::shared_ptr<const PreviewCapturedMaterial>
BuildPreviewCapturedMaterial(const OwnedDrawInputs &inputs, size_t max_bytes)
{
    return BuildPreviewCapturedMaterial(inputs, PreviewBackend::OpenGL,
                                        max_bytes);
}

std::shared_ptr<const PreviewCapturedMaterial>
BuildPreviewCapturedMaterial(const OwnedDrawInputs &inputs,
                             PreviewBackend backend, size_t max_bytes)
{
    auto material = std::make_shared<PreviewCapturedMaterial>();
    max_bytes = std::min(max_bytes, kPreviewMaxCapturedMaterialBytes);
    size_t remaining =
        max_bytes > sizeof(*material) ? max_bytes - sizeof(*material) : 0;
    if (!inputs.complete)
        material->limitations |= PreviewMaterialUnavailable;
    for (size_t slot = 0; slot < material->textures.size(); ++slot) {
        const auto &source = inputs.textures[slot];
        auto &texture = material->textures[slot];
        texture.described = source.described;
        texture.metadata = source.metadata;
        texture.metadata.slot = slot;
        texture.metadata.image = {};
        texture.metadata.mip_level = texture.metadata.face = 0;
        if (backend == PreviewBackend::Vulkan) {
            texture.metadata.min_filter =
                source.metadata.min_filter == 1 ? 0x2601 : 0x2600;
            texture.metadata.mag_filter =
                source.metadata.mag_filter == 1 ? 0x2601 : 0x2600;
            auto wrap = [](uint32_t value) {
                switch (value) {
                case 0:
                    return 0x2901U;
                case 1:
                    return 0x8370U;
                case 2:
                    return 0x812fU;
                case 3:
                    return 0x812dU;
                case 4:
                    return 0x8743U;
                default:
                    return 0U;
                }
            };
            texture.metadata.wrap_s = wrap(source.metadata.wrap_s);
            texture.metadata.wrap_t = wrap(source.metadata.wrap_t);
            texture.metadata.wrap_r = wrap(source.metadata.wrap_r);
        }
        if (!source.described) {
            material->limitations |= PreviewMaterialMissingTexture;
            continue;
        }
        if (!source.metadata.bound)
            continue;
        const auto &meta = texture.metadata;
        if (meta.depth != 1 || (meta.face_count != 1 && meta.face_count != 6) ||
            !meta.width || !meta.height || meta.width > 2048 ||
            meta.height > 2048 ||
            (meta.face_count == 6 && meta.width != meta.height)) {
            material->limitations |= PreviewMaterialUnsupportedTexture;
            continue;
        }
        std::array<const OwnedDrawTextureImage *, 6> faces{};
        size_t image_bytes = size_t(meta.width) * meta.height * 4;
        for (const auto &image : source.images) {
            if (image.mip_level == 0 && image.face < meta.face_count &&
                image.image.width == meta.width &&
                image.image.height == meta.height &&
                image.image.rgba.size() == image_bytes)
                faces[image.face] = &image;
        }
        bool complete = true;
        for (size_t face = 0; face < meta.face_count; ++face)
            complete &= faces[face] != nullptr;
        if (!complete) {
            material->limitations |= PreviewMaterialMissingTexture;
            continue;
        }
        const size_t bytes =
            meta.face_count * (image_bytes + sizeof(OwnedDrawTextureImage));
        if (bytes > remaining) {
            material->limitations |=
                PreviewMaterialBudgetLimited | PreviewMaterialMissingTexture;
            continue;
        }
        texture.images.reserve(meta.face_count);
        for (size_t face = 0; face < meta.face_count; ++face)
            texture.images.push_back(*faces[face]);
        // glGetTexImage returns storage channels before texture swizzling.
        // Bake the captured channel mapping into this reduced RGBA copy.
        std::array<uint32_t, 4> swizzle{ 0x1903, 0x1904, 0x1905, 0x1906 };
        const std::string prefix =
            "capture.texture" + std::to_string(slot) + ".";
        for (const auto &reg : inputs.registers) {
            for (size_t channel = 0; channel < 4; ++channel)
                if (backend == PreviewBackend::OpenGL &&
                    reg.name == prefix + "swizzle" + std::to_string(channel))
                    swizzle[channel] = reg.value;
            if ((reg.name == prefix + "lod_bias_bits" && reg.value) ||
                (reg.name == prefix + "compare_mode" && reg.value) ||
                (reg.name == prefix + "anisotropy_bits" &&
                 reg.value != 0x3f800000))
                material->limitations |= PreviewMaterialApproximateSampler;
        }
        for (auto &image : texture.images) {
            if (swizzle ==
                std::array<uint32_t, 4>{ 0x1903, 0x1904, 0x1905, 0x1906 })
                continue;
            for (size_t pixel = 0; pixel < image.image.rgba.size();
                 pixel += 4) {
                std::array<uint8_t, 4> original;
                std::copy_n(image.image.rgba.data() + pixel, 4,
                            original.begin());
                for (size_t channel = 0; channel < 4; ++channel) {
                    uint8_t value = original[channel];
                    switch (swizzle[channel]) {
                    case 0:
                        value = 0;
                        break;
                    case 1:
                        value = 255;
                        break;
                    case 0x1903:
                        value = original[0];
                        break;
                    case 0x1904:
                        value = original[1];
                        break;
                    case 0x1905:
                        value = original[2];
                        break;
                    case 0x1906:
                        value = original[3];
                        break;
                    default:
                        material->limitations |=
                            PreviewMaterialApproximateSampler;
                        break;
                    }
                    image.image.rgba[pixel + channel] = value;
                }
            }
        }
        texture.metadata.mip_levels = 1;
        remaining -= bytes;
        auto wrap_supported = [](uint32_t value) {
            return value == 0x2901 || value == 0x812f || value == 0x8370;
        };
        if (!wrap_supported(meta.wrap_s) || !wrap_supported(meta.wrap_t) ||
            !wrap_supported(meta.wrap_r) ||
            (meta.mag_filter != 0x2600 && meta.mag_filter != 0x2601) ||
            (meta.min_filter != 0x2600 && meta.min_filter != 0x2601 &&
             (meta.min_filter < 0x2700 || meta.min_filter > 0x2703)))
            material->limitations |= PreviewMaterialApproximateSampler;
    }
    for (const auto &source : inputs.uniforms) {
        if (!PreviewCapturedUniformAllowed(source)) {
            // Vertex constants and viewport controls intentionally use private
            // preview defaults. Other unbound fragment inputs are partial.
            const std::string name =
                source.name.substr(0, source.name.find('['));
            const bool sampler = name == "texSamp0" || name == "texSamp1" ||
                                 name == "texSamp2" || name == "texSamp3";
            if ((source.stage == 0 || source.stage == 2) && !sampler &&
                name != "clipRegion" && name != "clipRange" &&
                name != "surfaceScale" && name != "depthFactor" &&
                name != "depthOffset")
                material->limitations |= PreviewMaterialUnappliedUniform;
            continue;
        }
        const size_t bytes =
            sizeof(OwnedDrawUniform) + source.name.size() + source.data.size();
        if (material->uniforms.size() >= 256 || bytes > remaining) {
            material->limitations |= PreviewMaterialBudgetLimited;
            continue;
        }
        if (source.name.empty() || source.name.size() > 255 ||
            source.name.find('\0') != std::string::npos || !source.count ||
            source.count > 4096 || !source.components ||
            source.components > 16 ||
            source.type < XEMU_SHADER_DRAW_UNIFORM_FLOAT ||
            source.type > XEMU_SHADER_DRAW_UNIFORM_MAT4 ||
            (source.type <= XEMU_SHADER_DRAW_UNIFORM_UINT &&
             source.components > 4) ||
            (source.type == XEMU_SHADER_DRAW_UNIFORM_MAT2 &&
             source.components != 4) ||
            (source.type == XEMU_SHADER_DRAW_UNIFORM_MAT4 &&
             source.components != 16) ||
            source.data.size() !=
                size_t(source.count) * source.components * 4 ||
            source.data.size() > 65536) {
            material->limitations |= PreviewMaterialUnappliedUniform;
            continue;
        }
        material->uniforms.push_back(source);
        remaining -= bytes;
    }
    material->uniforms.shrink_to_fit();
    for (const auto &reg : inputs.registers) {
        if (reg.name == "capture.uniforms.status" && reg.value)
            material->limitations |= PreviewMaterialUnappliedUniform;
    }
    if (!remaining && max_bytes < sizeof(*material))
        material->limitations |= PreviewMaterialBudgetLimited;
    return material;
}

PreviewDigest
ComputePreviewCapturedMaterialDigest(const PreviewCapturedMaterial &material)
{
    std::vector<uint8_t> bytes;
    auto append = [&bytes](const auto &value) {
        const auto *data = reinterpret_cast<const uint8_t *>(&value);
        bytes.insert(bytes.end(), data, data + sizeof(value));
    };
    append(material.limitations);
    for (const auto &texture : material.textures) {
        append(texture.described);
        const auto &m = texture.metadata;
        append(m.slot);
        append(m.bound);
        append(m.guest_format);
        append(m.host_format);
        append(m.width);
        append(m.height);
        append(m.depth);
        append(m.face_count);
        append(m.mip_levels);
        append(m.min_filter);
        append(m.mag_filter);
        append(m.wrap_s);
        append(m.wrap_t);
        append(m.wrap_r);
        append(m.coordinate_scale);
        append(texture.images.size());
        for (const auto &image : texture.images) {
            append(image.face);
            append(image.mip_level);
            append(image.image.width);
            append(image.image.height);
            append(image.image.rgba.size());
            bytes.insert(bytes.end(), image.image.rgba.begin(),
                         image.image.rgba.end());
        }
    }
    append(material.uniforms.size());
    for (const auto &uniform : material.uniforms) {
        append(uniform.stage);
        append(uniform.type);
        append(uniform.components);
        append(uniform.count);
        append(uniform.name.size());
        bytes.insert(bytes.end(), uniform.name.begin(), uniform.name.end());
        append(uniform.data.size());
        bytes.insert(bytes.end(), uniform.data.begin(), uniform.data.end());
    }
    return ComputePreviewDigest(bytes.data(), bytes.size());
}

std::string
DescribePreviewCapturedMaterial(const PreviewCapturedMaterial &material)
{
    std::string description =
        "Partial captured material: base-level textures and fragment constants";
    if (material.limitations & PreviewMaterialUnavailable)
        description += "; inputs unavailable";
    if (material.limitations & PreviewMaterialMissingTexture)
        description += "; missing texture images use zero";
    if (material.limitations & PreviewMaterialUnsupportedTexture)
        description += "; unsupported texture images use zero";
    if (material.limitations & PreviewMaterialUnappliedUniform)
        description += "; some uniforms unavailable or unapplied";
    if (material.limitations & PreviewMaterialBudgetLimited)
        description += "; inputs omitted by packet budget";
    if (material.limitations & PreviewMaterialApproximateSampler)
        description += "; some sampler state approximated";
    return description;
}

static size_t
CapturedMaterialOwnedBytes(const PreviewCapturedMaterial &material)
{
    size_t total = sizeof(material);
    for (const auto &texture : material.textures) {
        if (!CheckedAdd(total,
                        texture.images.capacity() *
                            sizeof(OwnedDrawTextureImage),
                        &total))
            return std::numeric_limits<size_t>::max();
        for (const auto &image : texture.images)
            if (!CheckedAdd(total, image.image.rgba.capacity(), &total))
                return std::numeric_limits<size_t>::max();
    }
    if (!CheckedAdd(total,
                    material.uniforms.capacity() * sizeof(OwnedDrawUniform),
                    &total))
        return std::numeric_limits<size_t>::max();
    for (const auto &uniform : material.uniforms)
        if (!CheckedAdd(total, uniform.name.capacity(), &total) ||
            !CheckedAdd(total, uniform.data.capacity(), &total))
            return std::numeric_limits<size_t>::max();
    return total;
}

std::string PreviewSourceIdentity(const PreviewCompileKey &key)
{
    static const char hex[] = "0123456789abcdef";
    std::string digest;
    for (uint8_t byte : key.source_digest) {
        digest += hex[byte >> 4];
        digest += hex[byte & 15];
    }
    if (std::all_of(key.source_digest.begin(), key.source_digest.end(),
                    [](uint8_t byte) { return byte == 0; })) {
        digest = "unavailable";
    }
    if (key.source_variant == PreviewSourceVariant::Edited) {
        return "draft " + std::to_string(key.draft_id) +
               " revision " + std::to_string(key.draft_revision) +
               " attempt " + std::to_string(key.draft_submission_id) +
               " / source " + digest;
    }
    if (key.selection.mode == PreviewMode::Replacement) {
        return "replacement " + std::to_string(key.replacement_id) +
               " revision " + std::to_string(key.replacement_revision) +
               " / source " + digest;
    }
    return digest;
}

bool SamePreviewDisplayScope(const PreviewSelection &lhs,
                             const PreviewSelection &rhs)
{
    PreviewSelection comparable = lhs;
    comparable.mode = rhs.mode;
    return comparable == rhs;
}

bool PreviewSelection::operator==(const PreviewSelection &other) const
{
    return scope == other.scope && shader == other.shader &&
           session_epoch == other.session_epoch &&
           renderer_epoch == other.renderer_epoch &&
           backend == other.backend && mode == other.mode;
}

bool PreviewSelection::operator!=(const PreviewSelection &other) const
{
    return !(*this == other);
}

bool PreviewCompileKey::operator==(const PreviewCompileKey &other) const
{
    return selection == other.selection &&
           source_variant == other.source_variant &&
           draft_id == other.draft_id &&
           draft_revision == other.draft_revision &&
           draft_submission_id == other.draft_submission_id &&
           recipe_format_version == other.recipe_format_version &&
           generator_abi == other.generator_abi &&
           interface_abi == other.interface_abi &&
           replacement_id == other.replacement_id &&
           replacement_revision == other.replacement_revision &&
           source_digest == other.source_digest &&
           partner_digest == other.partner_digest &&
           pipeline_layout_digest == other.pipeline_layout_digest;
}

bool PreviewCompileKey::operator!=(const PreviewCompileKey &other) const
{
    return !(*this == other);
}

bool PreviewInputBinding::operator==(const PreviewInputBinding &other) const
{
    return target == other.target && enabled == other.enabled &&
           base == other.base && amplitude == other.amplitude &&
           period_seconds == other.period_seconds;
}

bool PreviewResultKey::operator==(const PreviewResultKey &other) const
{
    return channel == other.channel && clock_revision == other.clock_revision &&
           clock_edit_revision == other.clock_edit_revision &&
           time_seconds == other.time_seconds && compile == other.compile &&
           input_revision == other.input_revision &&
           binding_count == other.binding_count && bindings == other.bindings &&
           scene == other.scene && render_state == other.render_state &&
           view_revision == other.view_revision && width == other.width &&
           height == other.height && packet_kind == other.packet_kind &&
           replay_class == other.replay_class &&
           fixture_digest == other.fixture_digest &&
           mesh_digest == other.mesh_digest &&
           material_digest == other.material_digest &&
           pipeline_digest == other.pipeline_digest &&
           profile_draw == other.profile_draw;
}

const char *PreviewDrawTimingStatusLabel(PreviewDrawTimingStatus status)
{
    switch (status) {
    case PreviewDrawTimingStatus::Disarmed:
        return "Disarmed";
    case PreviewDrawTimingStatus::Pending:
        return "Pending GPU timestamps";
    case PreviewDrawTimingStatus::Measured:
        return "Measured draw interval";
    case PreviewDrawTimingStatus::Unsupported:
        return "Unsupported";
    case PreviewDrawTimingStatus::Failed:
        return "Timestamp query failed";
    }
    return "Unknown";
}
const char *PreviewDrawTimingProvenanceLabel(PreviewDrawTimingProvenance p)
{
    switch (p) {
    case PreviewDrawTimingProvenance::None:
        return "Disarmed";
    case PreviewDrawTimingProvenance::SelectedPreviewInstrumented:
        return "Selected preview instrumented draw; all shader stages";
    case PreviewDrawTimingProvenance::ReplayInstrumented:
        return "Replay instrumented draw; all shader stages";
    }
    return "Unknown";
}
bool ComputePreviewDrawInterval(uint64_t start, uint64_t finish, uint32_t bits,
                                double period, uint64_t *nanoseconds,
                                std::string *error)
{
    if (nanoseconds)
        *nanoseconds = 0;
    auto fail = [&](const char *message) {
        if (error)
            *error = message;
        return false;
    };
    if (!nanoseconds || !bits || bits > 64 || !std::isfinite(period) ||
        period <= 0)
        return fail("Invalid timestamp counter width or period");
    const uint64_t mask = bits == 64 ? UINT64_MAX : (UINT64_C(1) << bits) - 1;
    const uint64_t ticks = (finish - start) & mask;
    if (!ticks || ticks >= (UINT64_C(1) << (bits - 1)))
        return fail(
            "Zero, reversed or ambiguous wrapped GPU timestamp interval");
    const long double ns = static_cast<long double>(ticks) * period;
    if (!std::isfinite(ns) || ns < .5L || ns > kPreviewMaxDrawTimingNs)
        return fail(
            "GPU timestamp interval is outside the supported 10 s bound");
    *nanoseconds = uint64_t(std::round(ns));
    if (error)
        error->clear();
    return true;
}
bool ValidatePreviewDrawTiming(const PreviewDrawTiming &timing,
                               const PreviewResultKey &expected)
{
    if (timing.result != expected || timing.message.size() > 1024 ||
        timing.backend != expected.compile.selection.backend ||
        uint32_t(timing.status) > uint32_t(PreviewDrawTimingStatus::Failed))
        return false;
    if (!expected.profile_draw)
        return timing.status == PreviewDrawTimingStatus::Disarmed &&
               !timing.nanoseconds &&
               timing.provenance == PreviewDrawTimingProvenance::None;
    const auto provenance =
        expected.packet_kind == PreviewPacketKind::Replay ?
            PreviewDrawTimingProvenance::ReplayInstrumented :
            PreviewDrawTimingProvenance::SelectedPreviewInstrumented;
    if (timing.status == PreviewDrawTimingStatus::Disarmed ||
        timing.provenance != provenance)
        return false;
    if (timing.status != PreviewDrawTimingStatus::Measured)
        return !timing.nanoseconds;
    return timing.nanoseconds &&
           timing.nanoseconds <= kPreviewMaxDrawTimingNs &&
           timing.actual_draw_commands && timing.actual_draw_commands <= 64 &&
           timing.timestamp_valid_bits && timing.timestamp_valid_bits <= 64 &&
           std::isfinite(timing.timestamp_period_ns) &&
           timing.timestamp_period_ns > 0;
}
void AccumulatePreviewDrawTiming(PreviewDrawTimingDistribution *d,
                                 const PreviewDrawTiming &timing)
{
    if (!d || timing.status == PreviewDrawTimingStatus::Disarmed)
        return;
    ++d->requested;
    switch (timing.status) {
    case PreviewDrawTimingStatus::Disarmed:
        break;
    case PreviewDrawTimingStatus::Pending:
        ++d->pending;
        break;
    case PreviewDrawTimingStatus::Unsupported:
        ++d->unsupported;
        break;
    case PreviewDrawTimingStatus::Failed:
        ++d->failed;
        break;
    case PreviewDrawTimingStatus::Measured:
        if (!timing.nanoseconds ||
            timing.nanoseconds > kPreviewMaxDrawTimingNs) {
            ++d->failed;
            break;
        }
        ++d->measured;
        if (d->sample_count == kPreviewDrawTimingWindow)
            std::move(d->samples.begin() + 1, d->samples.end(),
                      d->samples.begin());
        else
            ++d->sample_count;
        d->samples[d->sample_count - 1] = timing.nanoseconds;
        break;
    }
}
void FinalizePreviewDrawTimingDistribution(PreviewDrawTimingDistribution *d)
{
    if (!d || !d->sample_count)
        return;
    auto sorted = d->samples;
    std::sort(sorted.begin(), sorted.begin() + d->sample_count);
    const size_t n = d->sample_count;
    d->median_ns = n % 2 ?
                       double(sorted[n / 2]) :
                       (double(sorted[n / 2 - 1]) + double(sorted[n / 2])) / 2;
    d->p95_ns = sorted[(95 * n + 99) / 100 - 1];
}

bool PreviewResultKey::operator!=(const PreviewResultKey &other) const
{
    return !(*this == other);
}

const char *PreviewChannelLabel(PreviewChannel channel)
{
    static const char *labels[] = { "Final RGBA",
                                    "Final R",
                                    "Final G",
                                    "Final B",
                                    "Final Alpha",
                                    "Fixture UV",
                                    "Fixture D0",
                                    "Fixture D1",
                                    "Fixture B0",
                                    "Fixture B1",
                                    "Fixture T0 atlas",
                                    "Fixture T1 atlas",
                                    "Fixture T2 atlas",
                                    "Fixture T3 atlas",
                                    "Fixture Fog",
                                    "Depth-like UV ramp",
                                    "Fixture alpha threshold mask",
                                    "Exact shader discard mask" };
    const auto index = static_cast<size_t>(channel);
    return index < static_cast<size_t>(PreviewChannel::Count) ?
               labels[index] :
               "Unknown channel";
}

bool PreviewChannelAvailable(PreviewChannel channel)
{
    return channel < PreviewChannel::ShaderDiscard;
}

bool PreviewChannelIsDiagnostic(PreviewChannel channel)
{
    return channel >= PreviewChannel::UV && PreviewChannelAvailable(channel);
}

int PreviewChannelComponent(PreviewChannel channel)
{
    return channel >= PreviewChannel::Red && channel <= PreviewChannel::Alpha ?
               static_cast<int>(channel) -
                   static_cast<int>(PreviewChannel::Red) :
               -1;
}

const char *PreviewChannelProvenance(PreviewChannel channel)
{
    if (!PreviewChannelAvailable(channel))
        return "Unsupported: exact shader discard/alpha-test coverage cannot "
               "be recovered from RGBA.";
    if (channel <= PreviewChannel::Alpha)
        return "Selected shader output with synthetic inputs. Scalar views are "
               "opaque grayscale; Final RGBA preserves shader alpha.";
    if (channel >= PreviewChannel::T0 && channel <= PreviewChannel::T3)
        return "Preview-owned RGB texel atlas, opaque: bottom +X/-X/+Y, top "
               "-Y/+Z/-Z (2D repeats). Before filtering, coordinate routing or "
               "shader execution.";
    if (channel == PreviewChannel::DepthRamp)
        return "Preview-owned horizontal UV ramp, not scene depth or guest "
               "depth.";
    if (channel == PreviewChannel::FixtureAlphaMask)
        return "Approximate preview-owned mask: D0 alpha > alpha reference. "
               "Ignores shader execution, guest alpha function and discard.";
    return "Preview-owned 2D fixture chart before shader execution, with "
           "bilinear corner colors; independent of mesh/camera. RGB is opaque; "
           "UV is clamped RG, Fog is grayscale.";
}

const char *PreviewModeLabel(PreviewMode mode)
{
    switch (mode) {
    case PreviewMode::Normal: return "Normal";
    case PreviewMode::Uber: return "Uber Shader";
    case PreviewMode::Replacement: return "Replacement Shader";
    case PreviewMode::Visualize: return "Visualize";
    }
    return "Unknown";
}

const char *PreviewBackendLabel(PreviewBackend backend)
{
    switch (backend) {
    case PreviewBackend::Unknown: return "Unknown";
    case PreviewBackend::OpenGL: return "OpenGL";
    case PreviewBackend::Vulkan: return "Vulkan";
    }
    return "Unknown";
}

const char *PreviewStateLabel(PreviewState state)
{
    switch (state) {
    case PreviewState::Disabled: return "Disabled";
    case PreviewState::Hidden: return "Hidden";
    case PreviewState::NoSelection: return "No selection";
    case PreviewState::WaitingForInputs: return "Waiting for inputs";
    case PreviewState::Debouncing: return "Debouncing";
    case PreviewState::NeedsPreparation: return "Needs preparation";
    case PreviewState::Preparing: return "Preparing";
    case PreviewState::Ready: return "Ready";
    case PreviewState::Rendering: return "Rendering";
    case PreviewState::Throttled: return "Throttled";
    case PreviewState::Frozen: return "Frozen";
    case PreviewState::Unsupported: return "Unsupported";
    case PreviewState::Failed: return "Failed";
    case PreviewState::Retiring: return "Retiring";
    }
    return "Unknown";
}

const char *PreviewPressureLabel(PreviewPressure pressure)
{
    switch (pressure) {
    case PreviewPressure::Normal: return "Normal";
    case PreviewPressure::Elevated: return "Elevated";
    case PreviewPressure::High: return "High";
    case PreviewPressure::Critical: return "Critical";
    }
    return "Unknown";
}

const char *PreviewReplayClassLabel(PreviewReplayClass replay_class)
{
    switch (replay_class) {
    case PreviewReplayClass::Synthetic: return "Synthetic";
    case PreviewReplayClass::Complete: return "Complete replay";
    case PreviewReplayClass::Approximate: return "Approximate replay";
    case PreviewReplayClass::Unsupported: return "Unsupported";
    }
    return "Unknown";
}

PreviewDigest ComputePreviewDigest(const uint8_t *data, size_t size)
{
    PreviewDigest digest{};
    if ((!data && size) || size == 0) {
        return digest;
    }
    XXH128_hash_t first = XXH3_128bits_withSeed(
        data, size, UINT64_C(0x58454d5550525631));
    XXH128_hash_t second = XXH3_128bits_withSeed(
        data, size, UINT64_C(0x58454d5550525632));
    XXH128_canonical_t canonical{};
    XXH128_canonicalFromHash(&canonical, first);
    std::copy_n(canonical.digest, 16, digest.begin());
    XXH128_canonicalFromHash(&canonical, second);
    std::copy_n(canonical.digest, 16, digest.begin() + 16);
    return digest;
}

PreviewDigest ComputeCapturedMeshDigest(const PreviewCapturedMesh &mesh)
{
    std::vector<uint8_t> bytes;
    bytes.reserve(mesh.positions.size() * sizeof(mesh.positions[0]) +
                  mesh.indices.size() * sizeof(mesh.indices[0]));
    for (const auto &position : mesh.positions) {
        const auto *begin = reinterpret_cast<const uint8_t *>(position.data());
        bytes.insert(bytes.end(), begin, begin + sizeof(float) * 4);
    }
    for (uint32_t index : mesh.indices) {
        const auto *begin = reinterpret_cast<const uint8_t *>(&index);
        bytes.insert(bytes.end(), begin, begin + sizeof(index));
    }
    return ComputePreviewDigest(bytes.data(), bytes.size());
}

size_t PreviewPacketOwnedBytes(const PreviewPacket &packet, bool *overflow)
{
    size_t total = 0;
    size_t position_bytes = 0, index_bytes = 0;
    bool valid = CheckedMultiply(packet.captured_mesh.positions.capacity(),
                                 sizeof(packet.captured_mesh.positions[0]),
                                 &position_bytes) &&
                 CheckedMultiply(packet.captured_mesh.indices.capacity(),
                                 sizeof(packet.captured_mesh.indices[0]),
                                 &index_bytes) &&
                 CheckedAdd(total, packet.recipe.capacity(), &total) &&
                 CheckedAdd(total, packet.source.capacity(), &total) &&
                 CheckedAdd(total, packet.partner_source.capacity(), &total) &&
                 CheckedAdd(total, packet.fixture_bytes.capacity(), &total) &&
                 CheckedAdd(total, position_bytes, &total) &&
                 CheckedAdd(total, index_bytes, &total);
    if (valid && packet.captured_material)
        valid = CheckedAdd(
            total, CapturedMaterialOwnedBytes(*packet.captured_material),
            &total);
    if (valid && packet.captured_pipeline) {
        const auto &pipeline = *packet.captured_pipeline;
        valid = CheckedAdd(
            total,
            sizeof(pipeline) + pipeline.geometry_source.capacity() +
                pipeline.color_before.rgba.capacity() +
                pipeline.indices.capacity() * 4 +
                pipeline.ranges.capacity() * sizeof(pipeline.ranges[0]) +
                pipeline.uniforms.capacity() * sizeof(OwnedDrawUniform),
            &total);
        for (const auto &attribute : pipeline.attributes)
            valid = valid && CheckedAdd(total,
                                        attribute.stream.bytes.capacity() +
                                            attribute.stream.name.capacity(),
                                        &total);
        for (const auto &uniform : pipeline.uniforms)
            valid = valid && CheckedAdd(total,
                                        uniform.name.capacity() +
                                            uniform.data.capacity(),
                                        &total);
    }
    if (overflow) {
        *overflow = !valid;
    }
    return valid ? total : std::numeric_limits<size_t>::max();
}

bool ValidatePreviewPacket(const PreviewPacket &packet, std::string *error)
{
    auto fail = [error](const char *message) {
        if (error) {
            *error = message;
        }
        return false;
    };

    // Reject retained allocations before digesting any caller supplied bytes.
    bool overflow = false;
    size_t bytes = PreviewPacketOwnedBytes(packet, &overflow);
    if (overflow || bytes > kPreviewMaxOwnedPacketBytes) {
        return fail("Preview packet exceeds the 32 MiB owned-data limit");
    }

    if (packet.selection.scope.title_id == 0) {
        return fail("Preview packet requires an explicit Xbox TitleID");
    }
    if (packet.selection.shader.stage != Stage::Pixel) {
        return fail("Stage 4 v1 supports pixel/fragment shaders only");
    }
    if (packet.selection.shader.hash.version == 0) {
        return fail("Preview shader identity version must be non-zero");
    }
    if (packet.selection.backend == PreviewBackend::Unknown) {
        return fail("Preview packet requires a resolved renderer backend");
    }
    if (packet.recipe_format_version == 0 || packet.recipe.empty() ||
        packet.recipe.size() > kPreviewMaxRecipeBytes) {
        return fail("Canonical recipe is missing or exceeds the 8192-byte limit");
    }
    ShaderHash recomputed = ComputeShaderHash(
        packet.selection.shader.hash.version, packet.selection.shader.stage,
        packet.recipe_format_version, packet.recipe.data(),
        packet.recipe.size());
    if (recomputed != packet.selection.shader.hash) {
        return fail("Canonical recipe does not match the selected shader hash");
    }
    if (packet.generator_abi == 0 || packet.interface_abi == 0) {
        return fail("Preview generator and interface ABI must be non-zero");
    }
    if (packet.source.size() > kPreviewMaxSourceBytes ||
        ContainsNul(packet.source)) {
        return fail("Preview source exceeds 4 MiB or contains embedded NUL");
    }
    if (!packet.source.empty()) {
        if (packet.source_digest != ComputePreviewDigest(
                reinterpret_cast<const uint8_t *>(packet.source.data()),
                packet.source.size())) {
            return fail("Preview source digest does not match its bytes");
        }
    } else if (!DigestIsZero(packet.source_digest)) {
        return fail("Preview source digest was supplied without source bytes");
    }
    if (packet.partner_source.size() > kPreviewMaxSourceBytes ||
        ContainsNul(packet.partner_source)) {
        return fail("Preview partner source exceeds 4 MiB or contains "
                    "embedded NUL");
    }
    if (!packet.partner_source.empty()) {
        if (packet.partner_digest != ComputePreviewDigest(
                reinterpret_cast<const uint8_t *>(packet.partner_source.data()),
                packet.partner_source.size())) {
            return fail("Preview partner digest does not match its bytes");
        }
    } else if (!DigestIsZero(packet.partner_digest)) {
        return fail("Preview partner digest was supplied without partner bytes");
    }
    if (!packet.fixture_bytes.empty()) {
        if (packet.fixture_digest != ComputePreviewDigest(
                packet.fixture_bytes.data(), packet.fixture_bytes.size())) {
            return fail("Preview fixture digest does not match its bytes");
        }
    } else if (!DigestIsZero(packet.fixture_digest)) {
        return fail("Preview fixture digest was supplied without fixture bytes");
    }
    if (!PreviewExtentWithinLimits(packet.width, packet.height,
                                   bool(packet.captured_pipeline))) {
        return fail(packet.captured_pipeline ?
                        "Captured preview extent exceeds 1920 x 1080 pixels" :
                        "Preview extent exceeds 640 x 480 pixels");
    }
    if (!(ClampPreviewRenderState(packet.render_state) ==
          packet.render_state)) {
        return fail("Preview render state is invalid");
    }
    if (packet.packet_kind == PreviewPacketKind::Replay &&
        packet.replay_class == PreviewReplayClass::Unsupported) {
        return fail("Unsupported draw replay packets cannot be executed");
    }
    if (packet.packet_kind == PreviewPacketKind::Replay &&
        (packet.replay_class == PreviewReplayClass::Synthetic ||
         packet.fixture_bytes.empty())) {
        return fail("Replay packets require owned draw inputs and an exact or "
                    "approximate classification");
    }
    if (packet.packet_kind == PreviewPacketKind::Synthetic &&
        packet.replay_class != PreviewReplayClass::Synthetic) {
        return fail("Synthetic packets must use the synthetic classification");
    }
    if (packet.packet_kind == PreviewPacketKind::Replay) {
        const auto &mesh = packet.captured_mesh;
        const bool raw_only = packet.captured_pipeline &&
                              mesh.positions.empty() && mesh.indices.empty() &&
                              DigestIsZero(packet.mesh_digest);
        if (packet.replay_class != PreviewReplayClass::Approximate ||
            (!raw_only &&
             (mesh.positions.empty() || mesh.positions.size() > 4096 ||
              mesh.indices.empty() || mesh.indices.size() > 12288 ||
              mesh.indices.size() % 3 != 0 ||
              packet.mesh_digest != ComputeCapturedMeshDigest(mesh)))) {
            return fail("Approximate game draw requires bounded owned triangle geometry");
        }
        for (const auto &position : mesh.positions) {
            for (float value : position) {
                if (!std::isfinite(value) || std::abs(value) > 1.0e9f)
                    return fail("Game draw position is not finite or exceeds the scene range");
            }
        }
        for (uint32_t index : mesh.indices) {
            if (index >= mesh.positions.size())
                return fail("Game draw index exceeds position count");
        }
    } else if (!packet.captured_mesh.positions.empty() ||
               !packet.captured_mesh.indices.empty() ||
               !DigestIsZero(packet.mesh_digest)) {
        return fail("Synthetic packet cannot carry game draw geometry");
    }
    if (packet.captured_material) {
        if (packet.packet_kind != PreviewPacketKind::Replay)
            return fail("Synthetic packet cannot carry captured material");
        const auto &material = *packet.captured_material;
        if (material.uniforms.size() > 256)
            return fail("Captured material uniform count exceeds its bound");
        for (const auto &texture : material.textures) {
            if (texture.metadata.image.rgba ||
                texture.metadata.image.byte_count || texture.images.size() > 6)
                return fail("Captured material contains borrowed images or too "
                            "many faces");
            uint32_t faces = 0;
            for (const auto &image : texture.images) {
                if (!texture.described || !texture.metadata.bound ||
                    texture.metadata.depth != 1 ||
                    (texture.metadata.face_count != 1 &&
                     texture.metadata.face_count != 6) ||
                    image.mip_level ||
                    image.face >= texture.metadata.face_count ||
                    (faces & (1U << image.face)) || !image.image.width ||
                    !image.image.height || image.image.width > 2048 ||
                    image.image.height > 2048 ||
                    image.image.width != texture.metadata.width ||
                    image.image.height != texture.metadata.height ||
                    image.image.rgba.size() !=
                        size_t(image.image.width) * image.image.height * 4)
                    return fail(
                        "Captured material texture base image is invalid");
                faces |= 1U << image.face;
            }
            if (!texture.images.empty() &&
                (texture.images.size() != texture.metadata.face_count ||
                 (texture.metadata.face_count == 6 &&
                  texture.metadata.width != texture.metadata.height)))
                return fail(
                    "Captured material requires complete cube base faces");
        }
        for (const auto &uniform : material.uniforms) {
            if (!PreviewCapturedUniformAllowed(uniform) ||
                uniform.name.empty() || uniform.name.size() > 255 ||
                ContainsNul(uniform.name) || !uniform.count ||
                uniform.count > 4096 || !uniform.components ||
                uniform.components > 16 ||
                uniform.type < XEMU_SHADER_DRAW_UNIFORM_FLOAT ||
                uniform.type > XEMU_SHADER_DRAW_UNIFORM_MAT4 ||
                (uniform.type <= XEMU_SHADER_DRAW_UNIFORM_UINT &&
                 uniform.components > 4) ||
                (uniform.type == XEMU_SHADER_DRAW_UNIFORM_MAT2 &&
                 uniform.components != 4) ||
                (uniform.type == XEMU_SHADER_DRAW_UNIFORM_MAT4 &&
                 uniform.components != 16) ||
                uniform.data.size() > 65536 ||
                uniform.data.size() !=
                    size_t(uniform.count) * uniform.components * 4)
                return fail("Captured material uniform is invalid");
        }
        if (packet.material_digest !=
            ComputePreviewCapturedMaterialDigest(*packet.captured_material))
            return fail("Captured material digest does not match its bytes");
    } else if (!DigestIsZero(packet.material_digest)) {
        return fail("Captured material digest requires owned material");
    }
    if (packet.captured_pipeline) {
        if (packet.packet_kind != PreviewPacketKind::Replay ||
            packet.captured_pipeline->backend != packet.selection.backend ||
            packet.partner_source.empty() ||
            !ValidatePreviewCapturedPipeline(*packet.captured_pipeline, error))
            return fail(
                "Original camera requires a supported owned replay pipeline");
        if (packet.pipeline_digest != ComputePreviewCapturedPipelineDigest(
                                          *packet.captured_pipeline) ||
            packet.pipeline_layout_digest !=
                ComputePreviewCapturedPipelineDigest(*packet.captured_pipeline,
                                                     true))
            return fail("Original camera pipeline digest does not match its "
                        "owned inputs");
        const auto &r = packet.captured_pipeline->raster;
        const auto &before = packet.captured_pipeline->color_before;
        if (((r.available & PreviewRasterViewport) &&
             (r.width != packet.width || r.height != packet.height)) ||
            (!before.rgba.empty() &&
             (before.width != packet.width || before.height != packet.height)))
            return fail("Native viewport and owned before destination must "
                        "match packet extent without rescaling");
    } else if (!DigestIsZero(packet.pipeline_digest) ||
               !DigestIsZero(packet.pipeline_layout_digest))
        return fail("Original camera pipeline digest requires owned inputs");
    if (packet.selection.mode == PreviewMode::Replacement) {
        if (packet.replacement_id == 0 ||
            packet.replacement_revision == 0 || packet.source.empty() ||
            DigestIsZero(packet.source_digest)) {
            return fail("Replacement preview requires immutable source identity");
        }
    }
    if (packet.binding_count > kPreviewMaxInputBindings) {
        return fail("Preview input binding count exceeds the fixed limit");
    }
    uint32_t seen_targets = 0;
    for (size_t i = 0; i < packet.binding_count; ++i) {
        const PreviewInputBinding &binding = packet.bindings[i];
        const unsigned target = static_cast<unsigned>(binding.target);
        if (target >= static_cast<unsigned>(PreviewInputTarget::Count) ||
            !std::isfinite(binding.base) ||
            !std::isfinite(binding.amplitude) ||
            !std::isfinite(binding.period_seconds) ||
            std::abs(binding.base) > 16.0f ||
            std::abs(binding.amplitude) > 16.0f ||
            binding.period_seconds <= 0.0f ||
            binding.period_seconds > 3600.0f ||
            (seen_targets & (1U << target))) {
            return fail("Preview input binding is invalid or duplicated");
        }
        seen_targets |= 1U << target;
    }
    if (packet.source_variant == PreviewSourceVariant::Edited) {
        if (packet.selection.mode == PreviewMode::Replacement ||
            !packet.draft_id || !packet.draft_revision ||
            !packet.draft_submission_id ||
            packet.source.empty() || DigestIsZero(packet.source_digest)) {
            return fail("Edited preview requires an independent immutable draft identity");
        }
    } else if (packet.source_variant == PreviewSourceVariant::Original) {
        if (packet.draft_id || packet.draft_revision ||
            packet.draft_submission_id) {
            return fail("Original preview cannot carry a draft identity");
        }
    } else {
        return fail("Preview source variant is invalid");
    }

    if (error) {
        error->clear();
    }
    return true;
}

PreviewCompileKey BuildPreviewCompileKey(const PreviewPacket &packet)
{
    PreviewCompileKey key{};
    key.selection = packet.selection;
    key.source_variant = packet.source_variant;
    key.draft_id = packet.draft_id;
    key.draft_revision = packet.draft_revision;
    key.draft_submission_id = packet.draft_submission_id;
    key.recipe_format_version = packet.recipe_format_version;
    key.generator_abi = packet.generator_abi;
    key.interface_abi = packet.interface_abi;
    key.replacement_id = packet.replacement_id;
    key.replacement_revision = packet.replacement_revision;
    key.source_digest = packet.source_digest;
    key.partner_digest = packet.partner_digest;
    key.pipeline_layout_digest = packet.pipeline_layout_digest;
    return key;
}

PreviewResultKey BuildPreviewResultKey(const PreviewPacket &packet)
{
    PreviewResultKey key{};
    key.compile = BuildPreviewCompileKey(packet);
    key.input_revision = packet.input_revision;
    key.binding_count = packet.binding_count;
    key.bindings = packet.bindings;
    key.view_revision = packet.view_revision;
    key.scene = ClampPreviewScene(packet.scene);
    key.render_state = packet.render_state;
    key.width = packet.width;
    key.height = packet.height;
    key.packet_kind = packet.packet_kind;
    key.replay_class = packet.replay_class;
    key.fixture_digest = packet.fixture_digest;
    key.mesh_digest = packet.mesh_digest;
    key.material_digest = packet.material_digest;
    key.pipeline_digest = packet.pipeline_digest;
    key.profile_draw = packet.profile_draw;
    return key;
}

} // namespace xemu::shader_browser

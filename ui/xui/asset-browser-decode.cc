// SPDX-License-Identifier: GPL-2.0-or-later
#include "asset-browser-decode.hh"
#include "hw/xbox/nv2a/nv2a_regs.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <unordered_map>
#include <xxhash.h>

namespace xemu::asset_browser {
namespace {
enum class Number {
    Unsigned,
    Signed,
    Float,
    Half,
    Double,
    PackedUnsigned,
    PackedSigned
};
struct Format {
    size_t components = 0, bytes = 0;
    Number number = Number::Float;
    bool normalized = false, bgra = false;
};
// Serialized core VkFormat and GLenum values. These must also decode archives
// on builds without a Vulkan loader; no live API objects participate.
Format HostFormat(const capture::CaptureOwnedBlob &blob, uint32_t backend)
{
    Format out;
    out.normalized = blob.normalized != 0;
    out.components = blob.components == 0x80e1 ? 4 : blob.components;
    out.bgra = blob.components == 0x80e1;
    if (out.components < 1 || out.components > 4)
        return {};
    if (backend == 1) {
        switch (blob.format) {
        case 0x1400:
            out.bytes = 1;
            out.number = Number::Signed;
            break;
        case 0x1401:
            out.bytes = 1;
            out.number = Number::Unsigned;
            break;
        case 0x1402:
            out.bytes = 2;
            out.number = Number::Signed;
            break;
        case 0x1403:
            out.bytes = 2;
            out.number = Number::Unsigned;
            break;
        case 0x1404:
            out.bytes = 4;
            out.number = Number::Signed;
            break;
        case 0x1405:
            out.bytes = 4;
            out.number = Number::Unsigned;
            break;
        case 0x1406:
            out.bytes = 4;
            out.number = Number::Float;
            break;
        case 0x140a:
            out.bytes = 8;
            out.number = Number::Double;
            break;
        case 0x140b:
            out.bytes = 2;
            out.number = Number::Half;
            break;
        case 0x8d9f:
            out.bytes = 4;
            out.number = Number::PackedSigned;
            break;
        case 0x8368:
            out.bytes = 4;
            out.number = Number::PackedUnsigned;
            break;
        default:
            return {};
        }
    } else if (backend == 2) {
        const uint32_t f = blob.format;
        if (f >= 98 && f <= 109) {
            out.components = (f - 98) / 3 + 1;
            out.bytes = 4;
            out.number = (f - 98) % 3 == 2 ? Number::Float :
                         (f - 98) % 3 == 1 ? Number::Signed :
                                             Number::Unsigned;
            out.normalized = false;
        } else if (f >= 70 && f <= 97) {
            out.components = (f - 70) / 7 + 1;
            out.bytes = 2;
            const auto kind = (f - 70) % 7;
            out.number = kind == 6                           ? Number::Half :
                         kind == 1 || kind == 3 || kind == 5 ? Number::Signed :
                                                               Number::Unsigned;
            out.normalized = kind <= 1;
        } else if ((f >= 9 && f <= 29) || (f >= 37 && f <= 42) || f == 30 ||
                   f == 44) {
            out.components = f == 30 ? 3 :
                             f == 44 ? 4 :
                             f >= 37 ? 4 :
                                       (f - 9) / 7 + 1;
            const auto kind = f == 30 || f == 44 ? 0 :
                              f >= 37            ? f - 37 :
                                                   (f - 9) % 7;
            if (kind == 6)
                return {};
            out.bytes = 1;
            out.number = kind == 1 || kind == 3 || kind == 5 ? Number::Signed :
                                                               Number::Unsigned;
            out.normalized = kind <= 1;
            out.bgra = f == 30 || f == 44;
        } else if (f == 58 || f == 64 || f == 59 || f == 65) {
            out.components = 4;
            out.bytes = 4;
            out.normalized = true;
            out.number = f == 59 || f == 65 ? Number::PackedSigned :
                                              Number::PackedUnsigned;
            out.bgra = f == 58 || f == 59;
        } else {
            return {};
        }
        if (blob.components != out.components)
            return {};
    } else {
        return {};
    }
    return out;
}
uint64_t Little(const uint8_t *data, size_t bytes)
{
    uint64_t value = 0;
    for (size_t i = 0; i < bytes; ++i)
        value |= uint64_t(data[i]) << (i * 8);
    return value;
}
int64_t Signed(uint64_t value, unsigned bits)
{
    if (bits == 64) {
        int64_t result;
        std::memcpy(&result, &value, sizeof(result));
        return result;
    }
    const uint64_t sign = UINT64_C(1) << (bits - 1);
    return int64_t(value & (sign - 1)) - int64_t(value & sign);
}
float Half(uint16_t bits)
{
    const int exponent = (bits >> 10) & 31;
    float value = exponent == 31 ?
                      ((bits & 1023) ? std::numeric_limits<float>::quiet_NaN() :
                                       std::numeric_limits<float>::infinity()) :
                  exponent ?
                      std::ldexp(float(1024 + (bits & 1023)), exponent - 25) :
                      std::ldexp(float(bits & 1023), -24);
    return bits & 32768 ? -value : value;
}
const capture::CaptureOwnedBlob *Blob(const capture::CaptureOccurrence &event,
                                      const std::string &name)
{
    const capture::CaptureOwnedBlob *found = nullptr;
    for (const auto &blob : event.inputs.blobs)
        if (blob.name == name) {
            if (found)
                return nullptr;
            found = &blob;
        }
    return found;
}
uint32_t Reg(const capture::CaptureOccurrence &event, const std::string &name,
             uint32_t fallback = 0)
{
    for (const auto &reg : event.inputs.registers)
        if (reg.name == name)
            return reg.value;
    return fallback;
}
bool Values(const capture::CaptureOwnedBlob *blob, uint32_t backend,
            size_t index, std::array<float, 4> *value, bool compressed)
{
    if (!blob)
        return false;
    // Disabled OpenGL attributes already contain decoded float current values.
    // A packed mask alone does not make those values packed integer streams.
    compressed &= backend == 1 ? blob->format == 0x1404 && blob->integer :
                                 blob->format == 99;
    if (!compressed)
        return DecodeAssetAttribute(*blob, backend,
                                    blob->count == 1 ? 0 : index, value);
    if (blob->count == 1 || blob->stride == 0)
        index = 0;
    if (!blob->data || (blob->stride && blob->stride < 4) ||
        index >= blob->count ||
        index >
            (blob->data->bytes.size() >= 4 ?
                 (blob->stride ? (blob->data->bytes.size() - 4) / blob->stride :
                                 0) :
                 0) ||
        blob->data->bytes.size() < 4)
        return false;
    const uint32_t bits =
        Little(blob->data->bytes.data() + index * blob->stride, 4);
    // Same signed bitfield normalization as the generated NV2A vertex stage.
    *value = { float(Signed(bits & 2047, 11)) / 1023,
               float(Signed((bits >> 11) & 2047, 11)) / 1023,
               float(Signed(bits >> 22, 10)) / 511, 1 };
    return true;
}
bool Finite(const std::array<float, 4> &value)
{
    return std::all_of(value.begin(), value.end(),
                       [](float v) { return std::isfinite(v); });
}
} // namespace

bool DecodeAssetAttribute(const capture::CaptureOwnedBlob &blob,
                          uint32_t backend, size_t element,
                          std::array<float, 4> *output)
{
    const auto format = HostFormat(blob, backend);
    if (!output || !blob.data || !format.bytes || element >= blob.count)
        return false;
    const bool packed = format.number == Number::PackedSigned ||
                        format.number == Number::PackedUnsigned;
    const size_t bytes = packed ? 4 : format.bytes * format.components;
    const size_t step = blob.stride ? blob.stride : backend == 2 ? 0 : bytes;
    if ((step && step < bytes) || blob.data->bytes.size() < bytes ||
        (step && element > (blob.data->bytes.size() - bytes) / step))
        return false;
    const uint8_t *data = blob.data->bytes.data() + element * step;
    std::array<float, 4> value{ 0, 0, 0, 1 };
    for (size_t c = 0; c < format.components; ++c) {
        if (packed) {
            const unsigned bits = c == 3 ? 2 : 10;
            const uint32_t word = Little(data, 4);
            const uint32_t scalar = (word >> (c * 10)) & ((1U << bits) - 1);
            if (format.number == Number::PackedSigned)
                value[c] =
                    format.normalized ?
                        std::max(-1.0f, float(Signed(scalar, bits)) /
                                            float((1U << (bits - 1)) - 1)) :
                        float(Signed(scalar, bits));
            else
                value[c] = format.normalized ?
                               float(scalar) / float((1U << bits) - 1) :
                               float(scalar);
            continue;
        }
        uint64_t bits = Little(data + c * format.bytes, format.bytes);
        switch (format.number) {
        case Number::Float: {
            const uint32_t word = bits;
            std::memcpy(&value[c], &word, 4);
            break;
        }
        case Number::Half:
            value[c] = Half(bits);
            break;
        case Number::Double: {
            double number;
            std::memcpy(&number, &bits, 8);
            value[c] = float(number);
            break;
        }
        case Number::Signed:
            value[c] = float(Signed(bits, format.bytes * 8));
            if (format.normalized)
                value[c] = std::max(
                    -1.0f,
                    value[c] /
                        float((UINT64_C(1) << (format.bytes * 8 - 1)) - 1));
            break;
        case Number::Unsigned:
            value[c] = float(bits);
            if (format.normalized)
                value[c] /= float((UINT64_C(1) << (format.bytes * 8)) - 1);
            break;
        default:
            return false;
        }
    }
    if (format.bgra)
        std::swap(value[0], value[2]);
    *output = value;
    return true;
}

AssetDecodeResult
DecodeAssetPart(std::shared_ptr<const capture::CaptureOccurrence> event,
                uint32_t backend, const AssetLimits &limits)
{
    AssetPart part;
    part.occurrence = std::move(event);
    if (!part.occurrence)
        return part;
    const auto &source = *part.occurrence;
    part.id = source.event_id;
    part.frame = source.summary.key.frame;
    const auto fail = [&](AssetStatus status, const char *reason) {
        part.status = status;
        part.reason = reason;
        part.vertices.clear();
        part.indices.clear();
        part.bounds = {};
        return part;
    };
    if (source.pending)
        return fail(AssetStatus::Pending, "Waiting for owned draw inputs");
    if (source.limitations &
        (capture::CaptureReadbackFailed | capture::CaptureInvalidated)) {
        part = fail(AssetStatus::Missing,
                    "Captured inputs failed or were invalidated");
        if (!source.failure.empty())
            part.reason += ": " + source.failure;
        return part;
    }
    if (source.type != capture::CaptureEventType::Draw || !source.emitted)
        return fail(AssetStatus::Missing, "No emitted geometry command");
    const auto *position = Blob(source, "vertex.attribute0");
    if (!position || !position->data)
        return fail(AssetStatus::Missing,
                    "Captured position stream is unavailable");
    const uint32_t first = Reg(source, backend == 2 ? "capture.vertices.first" :
                                                      "capture.first_vertex");
    uint32_t primitive = source.summary.primitive_mode;
    bool restart = false;
    if (backend == 2) {
        const auto *assembly = Blob(source, "vk.pipeline.assembly");
        if (assembly) {
            if (!assembly->data || assembly->data->bytes.size() != 32)
                return fail(AssetStatus::Malformed,
                            "Invalid captured topology assembly");
            const uint32_t host = Little(assembly->data->bytes.data() + 20, 4);
            restart = Little(assembly->data->bytes.data() + 24, 4) != 0;
            primitive = host == 3 ? 5 : host == 4 ? 6 : host == 5 ? 7 : 0;
        } else if (primitive != 5) {
            return fail(AssetStatus::Missing,
                        "Actual host topology is unavailable");
        }
    } else if (backend == 1) {
        const uint32_t host = Reg(source, "host.primitive_mode", UINT32_MAX);
        if (host != UINT32_MAX)
            primitive = host >= 4 && host <= 9 ? host + 1 : 0;
        if (Reg(source, "host.primitive_restart"))
            return fail(AssetStatus::Unsupported,
                        "OpenGL restart-index identity is unavailable");
    } else {
        return fail(AssetStatus::Unsupported, "Unknown captured backend");
    }
    if (primitive < 5 || primitive > 10)
        return fail(AssetStatus::Unsupported,
                    "This draw has no supported filled topology");
    std::vector<uint32_t> raw;
    const auto *indices =
        Blob(source, backend == 2 ? "vertex.indices" : "geometry.host_indices");
    if (!indices && source.summary.index_count)
        return fail(AssetStatus::Missing,
                    "Owned index stream is unavailable for an indexed draw");
    if (indices) {
        if (!indices->data || indices->count > limits.maximum_indices ||
            indices->data->bytes.size() != uint64_t(indices->count) * 4)
            return fail(AssetStatus::Malformed,
                        "Invalid captured index stream");
        raw.resize(indices->count);
        for (size_t i = 0; i < raw.size(); ++i)
            raw[i] = Little(indices->data->bytes.data() + i * 4, 4);
    }
    bool budget = false, invalid = false;
    const auto append = [&](const std::vector<uint32_t> &range) {
        const size_t count = range.size();
        const size_t triangles =
            primitive == 5 ? count / 3 :
            primitive == 8 ? count / 4 * 2 :
            primitive == 9 ? (count >= 4 ? (count / 2 - 1) * 2 : 0) :
                             (count >= 3 ? count - 2 : 0);
        if (triangles >
                (limits.maximum_indices -
                 std::min(part.indices.size(), limits.maximum_indices)) /
                    3 ||
            triangles > (limits.decoded_byte_budget / 4 -
                         std::min<uint64_t>(part.indices.size(),
                                            limits.decoded_byte_budget / 4)) /
                            3) {
            budget = true;
            return;
        }
        for (size_t t = 0; t < triangles; ++t) {
            size_t a, b, c;
            if (primitive == 5) {
                a = t * 3;
                b = a + 1;
                c = a + 2;
            } else if (primitive == 6) {
                a = t + (t & 1);
                b = t + 1 - (t & 1);
                c = t + 2;
            } else if (primitive == 8) {
                c = t / 2 * 4;
                a = c + ((t & 1) ? 2 : 1);
                b = c + ((t & 1) ? 3 : 2);
            } else if (primitive == 9) {
                const auto base = t / 2 * 2;
                a = base + ((t & 1) ? 2 : 0);
                b = base + 1;
                c = base + ((t & 1) ? 3 : 2);
            } else {
                a = 0;
                b = t + 1;
                c = t + 2;
            }
            part.indices.insert(part.indices.end(),
                                { range[a], range[b], range[c] });
        }
    };
    if (indices) {
        if (restart) {
            std::vector<uint32_t> range;
            for (uint32_t index : raw) {
                if (index == UINT32_MAX) {
                    append(range);
                    range.clear();
                } else
                    range.push_back(index);
            }
            append(range);
        } else {
            append(raw);
        }
    } else {
        const auto *starts = Blob(source, "geometry.draw_starts");
        const auto *counts = Blob(source, "geometry.draw_counts");
        if (backend == 1 && (!starts || !counts))
            return fail(AssetStatus::Missing,
                        "Captured OpenGL subdraw ranges are unavailable");
        if (starts || counts) {
            if (!starts || !counts || !starts->data || !counts->data ||
                starts->count != counts->count || starts->count > 4096 ||
                starts->data->bytes.size() != uint64_t(starts->count) * 4 ||
                counts->data->bytes.size() != uint64_t(counts->count) * 4)
                return fail(AssetStatus::Malformed,
                            "Invalid captured subdraw ranges");
        }
        const size_t ranges = starts ? starts->count : 1;
        for (size_t r = 0; r < ranges; ++r) {
            const uint32_t start =
                starts ? Little(starts->data->bytes.data() + r * 4, 4) : first;
            const uint32_t count =
                counts ?
                    Little(counts->data->bytes.data() + r * 4, 4) :
                backend == 2 ?
                    Reg(source, "capture.vertices.count", position->count) :
                    Reg(source, "capture.last_vertex",
                        first + position->count - 1) -
                        first + 1;
            if (count > limits.maximum_indices ||
                uint64_t(start) + count > UINT32_MAX) {
                budget = true;
                break;
            }
            std::vector<uint32_t> range(count);
            for (size_t i = 0; i < count; ++i)
                range[i] = start + i;
            append(range);
        }
    }
    if (budget)
        return fail(AssetStatus::BudgetExceeded,
                    "Asset triangle budget exceeded");
    if (part.indices.empty())
        return fail(AssetStatus::Missing,
                    "No complete triangles in the captured subdraws");
    const auto *uv = Blob(source, "vertex.attribute9");
    if (!uv)
        uv = Blob(source, "vertex.current9");
    const auto *normal = Blob(source, "vertex.attribute2");
    if (!normal)
        normal = Blob(source, "vertex.current2");
    const auto *color = Blob(source, "vertex.attribute3");
    if (!color)
        color = Blob(source, "vertex.current3");
    const uint32_t compressed = Reg(source, "capture.vertices.compressed_mask");
    part.has_uv = uv != nullptr;
    part.has_normals = normal != nullptr;
    part.has_color = color != nullptr;
    std::unordered_map<uint32_t, uint32_t> remap;
    for (auto &index : part.indices) {
        if (index < first || index - first >= position->count) {
            invalid = true;
            break;
        }
        const uint32_t local = index - first;
        const auto found = remap.find(local);
        if (found != remap.end()) {
            index = found->second;
            continue;
        }
        if (part.vertices.size() >= limits.maximum_vertices ||
            part.vertices.size() >=
                (limits.decoded_byte_budget -
                 std::min<uint64_t>(uint64_t(part.indices.size()) * 4,
                                    limits.decoded_byte_budget)) /
                    sizeof(AssetVertex)) {
            budget = true;
            break;
        }
        AssetVertex vertex;
        std::array<float, 4> value;
        if (!Values(position, backend, local, &value, compressed & 1) ||
            !Finite(value)) {
            invalid = true;
            break;
        }
        vertex.position = { value[0], value[1], value[2] };
        if (Values(uv, backend, local, &value, compressed & (1U << 9)) &&
            Finite(value))
            vertex.uv = { value[0], value[1] };
        else
            part.has_uv = false;
        if (Values(normal, backend, local, &value, compressed & (1U << 2)) &&
            Finite(value))
            vertex.normal = { value[0], value[1], value[2] };
        else
            part.has_normals = false;
        if (Values(color, backend, local, &value, compressed & (1U << 3)) &&
            Finite(value))
            vertex.color = value;
        else
            part.has_color = false;
        if (!part.bounds.valid) {
            part.bounds.minimum = part.bounds.maximum = vertex.position;
            part.bounds.valid = true;
        } else
            for (size_t axis = 0; axis < 3; ++axis) {
                part.bounds.minimum[axis] =
                    std::min(part.bounds.minimum[axis], vertex.position[axis]);
                part.bounds.maximum[axis] =
                    std::max(part.bounds.maximum[axis], vertex.position[axis]);
            }
        index = uint32_t(part.vertices.size());
        remap.emplace(local, index);
        part.vertices.push_back(vertex);
    }
    if (invalid)
        return fail(
            AssetStatus::Malformed,
            "Captured referenced vertex data is invalid or unavailable");
    if (budget)
        return fail(AssetStatus::BudgetExceeded,
                    "Asset decoded geometry budget exceeded");
    XXH3_state_t *hash = XXH3_createState();
    if (!hash)
        return fail(AssetStatus::BudgetExceeded,
                    "Asset signature allocation failed");
    XXH3_128bits_reset(hash);
    for (const auto &vertex : part.vertices)
        XXH3_128bits_update(hash, vertex.position.data(),
                            sizeof(vertex.position));
    XXH3_128bits_update(hash, part.indices.data(),
                        part.indices.size() * sizeof(uint32_t));
    XXH128_canonical_t canonical;
    XXH128_canonicalFromHash(&canonical, XXH3_128bits_digest(hash));
    XXH3_freeState(hash);
    std::copy(std::begin(canonical.digest), std::end(canonical.digest),
              part.geometry_signature.begin());
    part.decoded_bytes = part.vertices.size() * sizeof(AssetVertex) +
                         part.indices.size() * sizeof(uint32_t);
    part.status = AssetStatus::Ready;
    part.reason = "Captured vertex inputs; pose and material are diagnostic "
                  "interpretations";
    return part;
}
} // namespace xemu::asset_browser

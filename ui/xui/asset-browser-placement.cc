// SPDX-License-Identifier: GPL-2.0-or-later
#include "asset-browser-placement.hh"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <regex>
#include <limits>
#include <sstream>
namespace xemu::asset_browser {
namespace {
bool Uniform(const capture::CaptureOccurrence &event, const char *name,
             float *values, size_t count, bool prefix = false)
{
    for (const auto &u : event.inputs.uniforms)
        if ((u.stage == 1 || u.stage == 0) &&
            (u.name == name || u.name == std::string(name) + "[0]") &&
            u.type == XEMU_SHADER_DRAW_UNIFORM_FLOAT && u.data &&
            (prefix ? (u.components == 4 && u.count > 0 &&
                       size_t(u.components) * u.count <= count) :
                      size_t(u.components) * u.count == count) &&
            u.data->bytes.size() == size_t(u.components) * u.count * 4) {
            std::memcpy(values, u.data->bytes.data(), u.data->bytes.size());
            return true;
        }
    return false;
}
std::string CompactSource(const capture::SharedCaptureBlock &block)
{
    if (!block || block->bytes.size() > 256 * 1024)
        return {};
    const std::string source(block->bytes.begin(), block->bytes.end());
    std::string result;
    for (size_t i = 0; i < source.size();) {
        if (source.compare(i, 2, "/*") == 0) {
            const size_t end = source.find("*/", i + 2);
            if (end == std::string::npos)
                return {};
            i = end + 2;
        } else if (source.compare(i, 2, "//") == 0) {
            const size_t end = source.find('\n', i + 2);
            i = end == std::string::npos ? source.size() : end;
        } else {
            if (!std::isspace(static_cast<unsigned char>(source[i])))
                result += source[i];
            ++i;
        }
    }
    return result;
}
bool CompatibleView(const AssetPart &a, const AssetPart &b)
{
    if (!a.occurrence || !b.occurrence)
        return false;
    const auto viewport =
        [](const AssetPart &p) -> capture::SharedCaptureBlock {
        for (const auto &blob : p.occurrence->inputs.blobs)
            if (blob.name == "vk.viewport")
                return blob.data;
        return {};
    };
    const auto va = viewport(a), vb = viewport(b);
    if (va && vb && va->bytes != vb->bytes)
        return false;
    const auto ra = a.occurrence->resource_evidence,
               rb = b.occurrence->resource_evidence;
    if (ra && rb && ra->domain_id && rb->domain_id) {
        if (ra->domain_id != rb->domain_id)
            return false;
        for (const auto &x : ra->accesses) {
            if (x.kind != XEMU_SHADER_CAPTURE_RESOURCE_COLOR &&
                x.kind != XEMU_SHADER_CAPTURE_RESOURCE_DEPTH_STENCIL)
                continue;
            for (const auto &y : rb->accesses) {
                if (x.kind != y.kind || x.slot != y.slot)
                    continue;
                if ((x.allocation_id && y.allocation_id &&
                     x.allocation_id != y.allocation_id) ||
                    (x.view_id && y.view_id && x.view_id != y.view_id))
                    return false;
            }
        }
    }
    for (const auto &x : a.occurrence->summary.resources) {
        if (x.resource.kind != capture::ResourceKind::ColorTarget &&
            x.resource.kind != capture::ResourceKind::DepthStencilTarget)
            continue;
        for (const auto &y : b.occurrence->summary.resources) {
            if (y.resource.kind != x.resource.kind || x.slot != y.slot)
                continue;
            if (x.resource.storage_id && y.resource.storage_id &&
                x.resource.storage_id != y.resource.storage_id)
                return false;
            if (x.resource.guest.length && y.resource.guest.length &&
                (x.resource.guest.address != y.resource.guest.address ||
                 x.resource.guest.length != y.resource.guest.length))
                return false;
        }
    }
    return true;
}
} // namespace
AssetPlacement DecodeAssetPlacement(const capture::CaptureOccurrence &event)
{
    AssetPlacement result;
    result.reason =
        "Position transform is not a supported generated direct matrix path";
    std::string source = CompactSource(event.inputs.sources[1]);
    const size_t main = source.find("voidmain(){");
    if (main == std::string::npos ||
        source.find("#defineR12oPos") == std::string::npos)
        return result;
    source = source.substr(main);
    const std::string epilogue =
        "oPos.xy=roundScreenCoords(oPos.xy);oPos.w=clampAwayZeroInf(oPos.w);"
        "vec4vtxPos=oPos;oPos.xy=(2.0f*oPos.xy-surfaceSize)/"
        "surfaceSize;oPos.z=oPos.z/clipRange.y;oPos.xyz*=oPos.w;";
    const std::string vk_position = "gl_Position=oPos;";
    const std::string gl_position =
        "gl_Position=vec4(oPos.x,oPos.y,2.0*oPos.z-oPos.w,oPos.w);";
    const bool gl_depth = source.find(gl_position) != std::string::npos;
    const auto &position = gl_depth ? gl_position : vk_position;
    if (source.find(epilogue) == std::string::npos ||
        source.find(position) == std::string::npos)
        return result;
    static const std::regex instruction(R"(([A-Z][A-Z0-9]*)\(([^;{}]+)\);)");
    static const std::regex dp4(R"(oPos,([xyzw]),v0,c\[([0-9]{1,3})\])");
    static const std::regex scale(
        R"(oPos,xyz,R12\.xyz,c\[([0-9]{1,3})\]\.xyz)");
    static const std::regex bias(
        R"(oPos,xyz,R12\.xyz,(R[0-9]{1,3})\.x,c\[([0-9]{1,3})\]\.xyz)");
    std::array<int, 4> rows{ -1, -1, -1, -1 };
    int scale_index = -1, bias_index = -1;
    std::string reciprocal;
    size_t dp_end = 0, scale_at = 0, bias_at = 0;
    std::vector<std::pair<std::string, std::string>> calls;
    for (std::sregex_iterator it(source.begin(), source.end(), instruction),
         end;
         it != end; ++it) {
        const std::string op = (*it)[1], args = (*it)[2];
        calls.emplace_back(op, args);
        if (args.rfind("oPos,", 0) != 0 && args.rfind("R12,", 0) != 0)
            continue;
        std::smatch match;
        if (op == "DP4" && std::regex_match(args, match, dp4)) {
            const size_t axis = std::string("xyzw").find(match[1]);
            if (rows[axis] != -1)
                return result;
            rows[axis] = std::stoi(match[2]);
            dp_end = size_t(it->position());
        } else if (op == "MUL" && scale_index == -1 &&
                   std::regex_match(args, match, scale)) {
            scale_index = std::stoi(match[1]);
            scale_at = size_t(it->position());
        } else if (op == "MAD" && bias_index == -1 &&
                   std::regex_match(args, match, bias)) {
            reciprocal = match[1];
            bias_index = std::stoi(match[2]);
            bias_at = size_t(it->position());
        } else
            return result;
    }
    if (scale_index < 0 || bias_index < 0 || scale_at <= dp_end ||
        bias_at <= scale_at || scale_index >= 192 || bias_index >= 192 ||
        reciprocal.empty())
        return result;
    for (int row : rows)
        if (row < 0 || row >= 192)
            return result;
    size_t reciprocal_writes = 0;
    for (const auto &[op, args] : calls) {
        if (args.rfind(reciprocal + ",", 0) != 0)
            continue;
        const size_t comma = args.find(',', reciprocal.size() + 1);
        if (comma == std::string::npos)
            return result;
        const auto mask =
            args.substr(reciprocal.size() + 1, comma - reciprocal.size() - 1);
        if (mask.find('x') == std::string::npos)
            continue;
        if (op != "RCC" || args != reciprocal + ",x,R12.w")
            return result;
        ++reciprocal_writes;
    }
    if (reciprocal_writes != 1)
        return result;
    auto other = std::regex_replace(source, instruction, "");
    auto remove = [&](const std::string &text) {
        const auto at = other.find(text);
        if (at != std::string::npos)
            other.erase(at, text.size());
    };
    remove(epilogue);
    remove(position);
    if (other.find("oPos") != std::string::npos ||
        other.find("R12") != std::string::npos)
        return result;
    std::array<float, 192 * 4> c;
    c.fill(std::numeric_limits<float>::quiet_NaN());
    std::array<float, 2> extent;
    std::array<float, 4> clip;
    if (!Uniform(event, "c", c.data(), c.size(), true) ||
        !Uniform(event, "surfaceSize", extent.data(), extent.size()) ||
        !Uniform(event, "clipRange", clip.data(), clip.size()) ||
        !std::isfinite(extent[0]) || !std::isfinite(extent[1]) ||
        !std::isfinite(clip[1]) || extent[0] <= 0 || extent[1] <= 0 ||
        clip[1] <= 0) {
        result.reason =
            "Missing or nonfinite captured position constants/viewport";
        return result;
    }
    for (size_t column = 0; column < 4; ++column) {
        const float w = c[rows[3] * 4 + column];
        result.clip_from_local[column] =
            2 * c[scale_index * 4] / extent[0] * c[rows[0] * 4 + column] +
            (2 * c[bias_index * 4] / extent[0] - 1) * w;
        result.clip_from_local[4 + column] =
            2 * c[scale_index * 4 + 1] / extent[1] * c[rows[1] * 4 + column] +
            (2 * c[bias_index * 4 + 1] / extent[1] - 1) * w;
        result.clip_from_local[8 + column] =
            c[scale_index * 4 + 2] / clip[1] * c[rows[2] * 4 + column] +
            c[bias_index * 4 + 2] / clip[1] * w;
        if (gl_depth)
            result.clip_from_local[8 + column] =
                2 * result.clip_from_local[8 + column] - w;
        result.clip_from_local[12 + column] = w;
    }
    AssetMatrix inverse;
    result.valid = InvertAssetMatrix(result.clip_from_local, &inverse);
    result.reason = result.valid ?
                        "Captured direct position matrix; screen rounding "
                        "omitted for placement" :
                        "Singular/nonfinite captured position transform";
    return result;
}
AssetMatrix MultiplyAssetMatrices(const AssetMatrix &a, const AssetMatrix &b)
{
    AssetMatrix out{};
    for (size_t row = 0; row < 4; ++row)
        for (size_t col = 0; col < 4; ++col)
            for (size_t k = 0; k < 4; ++k)
                out[row * 4 + col] += a[row * 4 + k] * b[k * 4 + col];
    return out;
}
bool InvertAssetMatrix(const AssetMatrix &matrix, AssetMatrix *out)
{
    if (!out)
        return false;
    double a[4][8]{};
    for (size_t row = 0; row < 4; ++row)
        for (size_t col = 0; col < 4; ++col) {
            if (!std::isfinite(matrix[row * 4 + col]))
                return false;
            a[row][col] = matrix[row * 4 + col];
            a[row][4 + col] = row == col;
        }
    for (size_t col = 0; col < 4; ++col) {
        size_t pivot = col;
        for (size_t row = col + 1; row < 4; ++row)
            if (std::abs(a[row][col]) > std::abs(a[pivot][col]))
                pivot = row;
        if (std::abs(a[pivot][col]) < 1e-12)
            return false;
        for (size_t i = 0; i < 8; ++i)
            std::swap(a[pivot][i], a[col][i]);
        const double divisor = a[col][col];
        for (double &value : a[col])
            value /= divisor;
        for (size_t row = 0; row < 4; ++row)
            if (row != col) {
                const double factor = a[row][col];
                for (size_t i = 0; i < 8; ++i)
                    a[row][i] -= factor * a[col][i];
            }
    }
    AssetMatrix result;
    for (size_t row = 0; row < 4; ++row)
        for (size_t col = 0; col < 4; ++col) {
            result[row * 4 + col] = float(a[row][4 + col]);
            if (!std::isfinite(result[row * 4 + col]))
                return false;
        }
    *out = result;
    return true;
}
bool TransformAssetPoint(const AssetMatrix &matrix,
                         const std::array<float, 3> &point,
                         std::array<float, 3> *out)
{
    if (!out)
        return false;
    std::array<double, 4> result{};
    for (size_t row = 0; row < 4; ++row) {
        result[row] = matrix[row * 4 + 3];
        for (size_t col = 0; col < 3; ++col)
            result[row] += double(matrix[row * 4 + col]) * point[col];
        if (!std::isfinite(result[row]))
            return false;
    }
    if (std::abs(result[3]) < 1e-9)
        return false;
    for (size_t axis = 0; axis < 3; ++axis) {
        (*out)[axis] = float(result[axis] / result[3]);
        if (!std::isfinite((*out)[axis]))
            return false;
    }
    return true;
}
std::vector<uint64_t> SuggestRelatedAssetParts(const AssetCatalog &catalog,
                                               uint64_t id)
{
    const auto it = std::find_if(catalog.parts.begin(), catalog.parts.end(),
                                 [id](const auto &p) { return p->id == id; });
    if (it == catalog.parts.end() || !(*it)->placement.valid ||
        !(*it)->bounds.valid)
        return {};
    const auto &anchor = **it;
    AssetMatrix inverse;
    if (!InvertAssetMatrix(anchor.placement.clip_from_local, &inverse))
        return {};
    float diameter = 0;
    for (size_t axis = 0; axis < 3; ++axis)
        diameter = std::max(diameter, anchor.bounds.maximum[axis] -
                                          anchor.bounds.minimum[axis]);
    if (diameter <= 0)
        return {};
    std::vector<uint64_t> ids{ id };
    for (const auto &p : catalog.parts) {
        if (p->id == id || p->status != AssetStatus::Ready ||
            !p->placement.valid || p->frame != anchor.frame ||
            ids.size() == 128)
            continue;
        // Nearby geometry plus a neighboring emission suggests related passes;
        // it does not prove engine ownership. The user confirms membership.
        const auto a = anchor.occurrence->summary.key.submission;
        const auto b = p->occurrence->summary.key.submission;
        if ((a > b ? a - b : b - a) > 64)
            continue;
        if (!CompatibleView(anchor, *p))
            continue;
        const auto relative =
            MultiplyAssetMatrices(inverse, p->placement.clip_from_local);
        bool inside = true;
        for (const auto &v : p->vertices) {
            std::array<float, 3> point;
            if (!TransformAssetPoint(relative, v.position, &point)) {
                inside = false;
                break;
            }
            for (size_t axis = 0; axis < 3; ++axis)
                if (point[axis] <
                        anchor.bounds.minimum[axis] - diameter * .25f ||
                    point[axis] > anchor.bounds.maximum[axis] + diameter * .25f)
                    inside = false;
        }
        if (inside && !p->vertices.empty())
            ids.push_back(p->id);
    }
    return ids;
}
} // namespace xemu::asset_browser

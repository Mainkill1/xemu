// SPDX-License-Identifier: GPL-2.0-or-later
#include "shader-browser-draw-capture.hh"

#include <algorithm>
#include <map>
#include <numeric>
#include <unordered_map>

namespace xemu::shader_browser {

namespace {

class SegmentationDisjointSet {
public:
    explicit SegmentationDisjointSet(size_t size)
        : parent_(size), rank_(size, 0)
    {
        std::iota(parent_.begin(), parent_.end(), size_t{0});
    }

    size_t Find(size_t value)
    {
        if (parent_[value] != value) {
            parent_[value] = Find(parent_[value]);
        }
        return parent_[value];
    }

    void Unite(size_t lhs, size_t rhs)
    {
        lhs = Find(lhs);
        rhs = Find(rhs);
        if (lhs == rhs) {
            return;
        }
        if (rank_[lhs] < rank_[rhs]) {
            std::swap(lhs, rhs);
        }
        parent_[rhs] = lhs;
        if (rank_[lhs] == rank_[rhs]) {
            ++rank_[lhs];
        }
    }

private:
    std::vector<size_t> parent_;
    std::vector<uint8_t> rank_;
};

} // namespace

PrimitiveSegmentation SegmentTriangleList(
    const std::vector<uint32_t> &indices)
{
    PrimitiveSegmentation result{};
    const size_t primitive_count = indices.size() / 3;
    result.trailing_index_count = static_cast<uint32_t>(indices.size() % 3);

    struct Triangle {
        uint32_t primitive = 0;
        std::array<uint32_t, 3> vertices{};
    };
    std::vector<Triangle> triangles;
    triangles.reserve(primitive_count);
    for (size_t primitive = 0; primitive < primitive_count; ++primitive) {
        const uint32_t a = indices[primitive * 3];
        const uint32_t b = indices[primitive * 3 + 1];
        const uint32_t c = indices[primitive * 3 + 2];
        if (a == b || b == c || a == c) {
            result.degenerate_primitives.push_back(
                static_cast<uint32_t>(primitive));
            continue;
        }
        triangles.push_back({ static_cast<uint32_t>(primitive), { a, b, c } });
    }

    SegmentationDisjointSet components(triangles.size());
    std::unordered_map<uint32_t, size_t> first_triangle_for_vertex;
    for (size_t index = 0; index < triangles.size(); ++index) {
        for (uint32_t vertex : triangles[index].vertices) {
            const auto inserted = first_triangle_for_vertex.emplace(vertex,
                                                                     index);
            if (!inserted.second) {
                components.Unite(index, inserted.first->second);
            }
        }
    }

    std::map<size_t, size_t> root_to_island;
    for (size_t index = 0; index < triangles.size(); ++index) {
        const size_t root = components.Find(index);
        auto found = root_to_island.find(root);
        if (found == root_to_island.end()) {
            result.islands.push_back({});
            found = root_to_island.emplace(root,
                                            result.islands.size() - 1).first;
        }
        PrimitiveIsland &island = result.islands[found->second];
        island.primitive_indices.push_back(triangles[index].primitive);
        island.unique_vertices.insert(island.unique_vertices.end(),
                                      triangles[index].vertices.begin(),
                                      triangles[index].vertices.end());
    }

    for (auto &island : result.islands) {
        std::sort(island.primitive_indices.begin(),
                  island.primitive_indices.end());
        std::sort(island.unique_vertices.begin(), island.unique_vertices.end());
        island.unique_vertices.erase(
            std::unique(island.unique_vertices.begin(),
                        island.unique_vertices.end()),
            island.unique_vertices.end());
    }
    std::sort(result.islands.begin(), result.islands.end(),
              [](const PrimitiveIsland &lhs, const PrimitiveIsland &rhs) {
                  return lhs.primitive_indices.front() <
                         rhs.primitive_indices.front();
              });
    for (size_t index = 0; index < result.islands.size(); ++index) {
        result.islands[index].island_id = static_cast<uint32_t>(index);
    }
    return result;
}

} // namespace xemu::shader_browser

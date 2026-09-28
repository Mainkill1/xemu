// SPDX-License-Identifier: GPL-2.0-or-later
#include "ui/xui/shader-browser-list-navigation.hh"
#include <array>
#include <cstdlib>
#include <cstdint>
#include <iostream>
#include <limits>
#include <vector>

using namespace xemu::shader_browser;

#define CHECK(condition)                                             \
    do {                                                             \
        if (!(condition)) {                                          \
            std::cerr << "Check failed at line " << __LINE__ << ": " \
                      << #condition << '\n';                         \
            std::exit(EXIT_FAILURE);                                 \
        }                                                            \
    } while (false)

int main()
{
    std::vector<uint64_t> ids;
    for (uint64_t i = 0; i < 40; ++i)
        ids.push_back(100 + 7 * i);
    auto select = [&](uint64_t current, ShaderListMove move, size_t page = 12) {
        const auto found = std::find(ids.begin(), ids.end(), current);
        const auto index = ShaderListNavigationTarget(
            ids.size(), size_t(found - ids.begin()), move, page);
        return index == ids.size() ? uint64_t(0) : ids[index];
    };
    CHECK(select(100, ShaderListMove::Next) == 107);
    CHECK(select(100, ShaderListMove::Previous) == 100);
    CHECK(select(107, ShaderListMove::PageNext) == 191);
    CHECK(select(191, ShaderListMove::PagePrevious) == 107);
    CHECK(select(107, ShaderListMove::First) == 100);
    CHECK(select(107, ShaderListMove::Last) == 373);
    CHECK(select(107, ShaderListMove::PageNext,
                 std::numeric_limits<size_t>::max()) == 373);
    // Reordering retains the selected event; the neighbor changes with order.
    std::reverse(ids.begin(), ids.end());
    CHECK(select(191, ShaderListMove::Next) == 184);
    ids.erase(std::remove(ids.begin(), ids.end(), 191), ids.end());
    CHECK(select(191, ShaderListMove::Next) == 373);
    CHECK(select(191, ShaderListMove::Previous) == 100);
    ids.clear();
    CHECK(select(191, ShaderListMove::Last) == 0);
    std::cout
        << "shader list navigation: stable IDs, paging and bounds passed\n";
}

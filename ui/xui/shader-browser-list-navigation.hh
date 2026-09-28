// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <algorithm>
#include <cstddef>

namespace xemu::shader_browser {

enum class ShaderListMove {
    Previous,
    Next,
    PagePrevious,
    PageNext,
    First,
    Last
};

// The caller resolves its stable selected ID in the current visible ordering,
// then resolves this index back to an ID. An absent selection starts at the
// edge requested; filtering and sorting never reuse the old row ordinal.
inline size_t ShaderListNavigationTarget(size_t count, size_t current,
                                         ShaderListMove move, size_t page_rows)
{
    if (!count)
        return count;
    const size_t last = count - 1;
    if (move == ShaderListMove::First)
        return 0;
    if (move == ShaderListMove::Last)
        return last;
    if (current >= count)
        return move == ShaderListMove::Previous ||
                       move == ShaderListMove::PagePrevious ?
                   last :
                   0;
    const size_t step = move == ShaderListMove::PageNext ||
                                move == ShaderListMove::PagePrevious ?
                            std::max(size_t(1), page_rows) :
                            1;
    return move == ShaderListMove::Previous ||
                   move == ShaderListMove::PagePrevious ?
               current - std::min(current, step) :
               current + std::min(last - current, step);
}

} // namespace xemu::shader_browser

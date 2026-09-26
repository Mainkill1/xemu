// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "shader-browser-preview-backend.hh"

namespace xemu::shader_browser {

// Consumes a coarse, atomically published flip counter and interval. This
// deliberately has no game-renderer pointer or draw-path dependency.
class PreviewHealthMonitor
{
public:
    PreviewHealth Sample(uint64_t now_ns, uint64_t completed_flips,
                         uint64_t flip_interval_ns, bool guest_running);

private:
    uint64_t last_flips_ = 0;
    uint64_t last_progress_ns_ = 0;
};

} // namespace xemu::shader_browser

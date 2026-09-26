// SPDX-License-Identifier: GPL-2.0-or-later
#include "shader-browser-preview-health.hh"

namespace xemu::shader_browser {

PreviewHealth PreviewHealthMonitor::Sample(uint64_t now_ns,
                                           uint64_t completed_flips,
                                           uint64_t flip_interval_ns,
                                           bool guest_running)
{
    PreviewHealth health{};
    health.sampled_ns = now_ns;
    if (!guest_running || completed_flips < last_flips_) {
        last_flips_ = completed_flips;
        last_progress_ns_ = 0;
        return health;
    }
    if (completed_flips > last_flips_) {
        last_flips_ = completed_flips;
        last_progress_ns_ = now_ns;
    }
    if (!last_progress_ns_ || now_ns < last_progress_ns_ ||
        now_ns - last_progress_ns_ > kPreviewHealthStaleNs ||
        flip_interval_ns == 0) {
        return health;
    }
    health.game_progressing = true;
    if (flip_interval_ns <= UINT64_C(20000000)) {
        health.pressure = PreviewPressure::Normal;
    } else if (flip_interval_ns <= UINT64_C(30000000)) {
        health.pressure = PreviewPressure::Elevated;
    } else if (flip_interval_ns <= UINT64_C(45000000)) {
        health.pressure = PreviewPressure::High;
    }
    return health;
}

} // namespace xemu::shader_browser

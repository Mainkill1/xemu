// SPDX-License-Identifier: GPL-2.0-or-later
#include "shader-browser-preview-clock.hh"

#include <cmath>

namespace xemu::shader_browser {

void PreviewClock::WrapTime()
{
    if (state_.loop && state_.time_seconds >= state_.loop_seconds) {
        state_.time_seconds = std::fmod(state_.time_seconds,
                                        state_.loop_seconds);
    }
}

void PreviewClock::AdvanceTime(uint64_t now_ns)
{
    if (!anchored_) {
        anchored_ = true;
        last_sample_ns_ = now_ns;
        last_emit_ns_ = now_ns;
        return;
    }
    if (now_ns < last_sample_ns_) {
        return;
    }
    if (state_.playing) {
        const double elapsed = static_cast<double>(now_ns - last_sample_ns_) /
                               1000000000.0;
        state_.time_seconds += elapsed * state_.speed;
        WrapTime();
    }
    last_sample_ns_ = now_ns;
}

bool PreviewClock::SetPlaying(bool playing, uint64_t now_ns)
{
    if (state_.playing == playing) {
        return false;
    }
    AdvanceTime(now_ns);
    state_.playing = playing;
    last_sample_ns_ = now_ns;
    last_emit_ns_ = now_ns;
    ++state_.revision;
    return true;
}

bool PreviewClock::SetLoop(bool loop, uint64_t now_ns)
{
    if (state_.loop == loop) {
        return false;
    }
    AdvanceTime(now_ns);
    state_.loop = loop;
    WrapTime();
    last_emit_ns_ = now_ns;
    ++state_.revision;
    return true;
}

bool PreviewClock::SetSpeed(double speed, uint64_t now_ns)
{
    if (!std::isfinite(speed) || speed < 0.05 || speed > 8.0 ||
        state_.speed == speed) {
        return false;
    }
    AdvanceTime(now_ns);
    state_.speed = speed;
    last_emit_ns_ = now_ns;
    ++state_.revision;
    return true;
}

bool PreviewClock::SetLoopLength(double seconds, uint64_t now_ns)
{
    if (!std::isfinite(seconds) || seconds < 0.1 || seconds > 3600.0 ||
        state_.loop_seconds == seconds) {
        return false;
    }
    AdvanceTime(now_ns);
    state_.loop_seconds = seconds;
    WrapTime();
    last_emit_ns_ = now_ns;
    ++state_.revision;
    return true;
}

bool PreviewClock::Scrub(double seconds, uint64_t now_ns)
{
    if (!std::isfinite(seconds) || seconds < 0.0) {
        return false;
    }
    state_.time_seconds = seconds;
    WrapTime();
    anchored_ = true;
    last_sample_ns_ = now_ns;
    last_emit_ns_ = now_ns;
    ++state_.revision;
    return true;
}

bool PreviewClock::Restart(uint64_t now_ns)
{
    state_.time_seconds = 0.0;
    state_.frame = 0;
    anchored_ = true;
    last_sample_ns_ = now_ns;
    last_emit_ns_ = now_ns;
    ++state_.revision;
    return true;
}

bool PreviewClock::Tick(uint64_t now_ns, uint64_t update_interval_ns)
{
    if (update_interval_ns == 0) {
        update_interval_ns = 1;
    }
    if (anchored_ && now_ns < last_sample_ns_) {
        return false;
    }
    if (!anchored_) {
        AdvanceTime(now_ns);
        return false;
    }
    if (!state_.playing || now_ns < last_emit_ns_ ||
        now_ns - last_emit_ns_ < update_interval_ns) {
        return false;
    }
    // Result identity includes time. Keep ordinary samples unchanged until
    // emission, including the first interval after a pressure freeze.
    // Explicit controls still advance/scrub time immediately.
    AdvanceTime(now_ns);
    last_emit_ns_ = now_ns;
    ++state_.frame;
    ++state_.revision;
    return true;
}

void PreviewClock::Suspend(uint64_t now_ns)
{
    anchored_ = true;
    last_sample_ns_ = now_ns;
    last_emit_ns_ = now_ns;
}

} // namespace xemu::shader_browser

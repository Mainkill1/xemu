// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <cstdint>

namespace xemu::shader_browser {

struct PreviewClockState {
    bool playing = false;
    bool loop = true;
    double time_seconds = 0.0;
    double speed = 1.0;
    double loop_seconds = 8.0;
    uint64_t frame = 0;
    uint64_t revision = 1;
};

// Service-owned preview time. The clock never advances the game or submits GPU
// work; callers choose a cadence and publish only the newest frame state.
class PreviewClock
{
public:
    const PreviewClockState &State() const { return state_; }

    bool SetPlaying(bool playing, uint64_t now_ns);
    bool SetLoop(bool loop, uint64_t now_ns);
    bool SetSpeed(double speed, uint64_t now_ns);
    bool SetLoopLength(double seconds, uint64_t now_ns);
    bool Scrub(double seconds, uint64_t now_ns);
    bool Restart(uint64_t now_ns);
    bool Tick(uint64_t now_ns, uint64_t update_interval_ns);
    void Suspend(uint64_t now_ns);

private:
    void AdvanceTime(uint64_t now_ns);
    void WrapTime();

    PreviewClockState state_;
    bool anchored_ = false;
    uint64_t last_sample_ns_ = 0;
    uint64_t last_emit_ns_ = 0;
};

} // namespace xemu::shader_browser

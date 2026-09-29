#include "../../ui/xui/shader-browser-preview-clock.hh"

#include <cassert>
#include <cmath>
#include <cstdint>
#include <iostream>

using namespace xemu::shader_browser;

static bool Near(double lhs, double rhs)
{
    return std::abs(lhs - rhs) < 0.000001;
}

int main()
{
    constexpr uint64_t second = UINT64_C(1000000000);
    PreviewClock clock;
    assert(!clock.State().playing);
    assert(Near(clock.State().time_seconds, 0.0));
    assert(!clock.Tick(10 * second, second / 30));

    assert(clock.SetPlaying(true, 10 * second));
    const uint64_t first_revision = clock.State().revision;
    assert(!clock.Tick(10 * second + second / 60, second / 30));
    assert(clock.State().revision == first_revision);
    assert(Near(clock.State().time_seconds, 0.0));
    assert(clock.Tick(10 * second + second / 30, second / 30));
    assert(clock.State().frame == 1);
    assert(Near(clock.State().time_seconds, 1.0 / 30.0));

    assert(clock.SetPlaying(false, 11 * second));
    const double paused_time = clock.State().time_seconds;
    assert(!clock.Tick(111 * second, second / 30));
    assert(Near(clock.State().time_seconds, paused_time));
    assert(clock.SetPlaying(true, 111 * second));
    assert(clock.Tick(111 * second + second / 30, second / 30));
    assert(Near(clock.State().time_seconds, paused_time + 1.0 / 30.0));

    assert(clock.SetLoop(false, 112 * second));
    assert(clock.Scrub(7.0, 112 * second));
    assert(clock.Tick(612 * second, second / 30));
    assert(Near(clock.State().time_seconds, 507.0));
    assert(clock.State().playing);

    assert(clock.SetPlaying(false, 612 * second));
    assert(clock.Restart(612 * second));
    assert(clock.State().frame == 0);
    assert(Near(clock.State().time_seconds, 0.0));
    assert(clock.SetSpeed(2.0, 612 * second));
    assert(clock.SetPlaying(true, 612 * second));
    assert(clock.Tick(613 * second, second / 30));
    assert(Near(clock.State().time_seconds, 2.0));
    assert(clock.SetSpeed(0.5, 613 * second));
    assert(clock.Tick(614 * second, second / 30));
    assert(Near(clock.State().time_seconds, 2.5));

    assert(clock.SetPlaying(false, 614 * second));
    assert(clock.SetLoopLength(2.0, 614 * second));
    assert(clock.SetLoop(true, 614 * second));
    assert(Near(clock.State().time_seconds, 0.5));
    assert(clock.Scrub(1.8, 614 * second));
    assert(clock.SetPlaying(true, 614 * second));
    assert(clock.Tick(615 * second, second / 30));
    assert(Near(clock.State().time_seconds, 0.3));

    assert(!clock.SetSpeed(0.0, 615 * second));
    assert(!clock.SetLoopLength(INFINITY, 615 * second));
    const double before_backwards = clock.State().time_seconds;
    assert(!clock.Tick(614 * second, second / 30));
    assert(Near(clock.State().time_seconds, before_backwards));
    clock.Suspend(1000 * second);
    assert(clock.Tick(1000 * second + second / 30, second / 30));
    assert(Near(clock.State().time_seconds,
                before_backwards + 0.5 / 30.0));
    assert(!clock.Tick(1000 * second + second / 30, 0));

    std::cout << "shader browser preview clock tests passed\n";
}

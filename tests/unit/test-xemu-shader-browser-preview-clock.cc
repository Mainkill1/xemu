#include "../../ui/xui/shader-browser-preview-clock.hh"

#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <cstdint>
#include <iostream>

using namespace xemu::shader_browser;

#define CHECK(condition)                                                \
    do {                                                                \
        if (!(condition)) {                                             \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, \
                         __LINE__, #condition);                         \
            std::abort();                                               \
        }                                                               \
    } while (false)

static bool Near(double lhs, double rhs)
{
    return std::abs(lhs - rhs) < 0.000001;
}

static void TestPollingPhase()
{
    constexpr uint64_t second = UINT64_C(1000000000);
    constexpr uint64_t polls[] = { 700000, 1300000, 900000, 1100000 };
    for (uint64_t interval : { UINT64_C(16666667), UINT64_C(33333333) }) {
        for (bool jittered : { false, true }) {
            PreviewClock clock;
            CHECK(clock.SetLoop(false, 0));
            CHECK(clock.SetPlaying(true, 0));
            uint64_t now = 0;
            uint64_t last_poll = 0;
            uint64_t emitted = 0;
            size_t poll = 0;
            while (now < 10 * second) {
                const auto revision = clock.State().revision;
                const auto time = clock.State().time_seconds;
                if (clock.Tick(now, interval)) {
                    ++emitted;
                    CHECK(clock.State().revision == revision + 1);
                    CHECK(Near(clock.State().time_seconds,
                               double(now) / double(second)));
                } else {
                    CHECK(clock.State().revision == revision);
                    CHECK(clock.State().time_seconds == time);
                }
                last_poll = now;
                now += jittered ? polls[poll++ % 4] : UINT64_C(1000000);
            }
            CHECK(emitted == last_poll / interval);
            CHECK(clock.State().frame == emitted);
            // A long gap skips missed deadlines and emits one current sample.
            const uint64_t resumed = 20 * second;
            CHECK(clock.Tick(resumed, interval));
            CHECK(clock.State().frame == emitted + 1);
            CHECK(Near(clock.State().time_seconds, 20.0));
            CHECK(!clock.Tick(resumed, interval));
        }
    }
}

int main()
{
    TestPollingPhase();
    constexpr uint64_t second = UINT64_C(1000000000);
    PreviewClock clock;
    CHECK(!clock.State().playing);
    CHECK(Near(clock.State().time_seconds, 0.0));
    CHECK(!clock.Tick(10 * second, second / 30));

    CHECK(clock.SetPlaying(true, 10 * second));
    const uint64_t first_revision = clock.State().revision;
    CHECK(!clock.Tick(10 * second + second / 60, second / 30));
    CHECK(clock.State().revision == first_revision);
    CHECK(Near(clock.State().time_seconds, 0.0));
    CHECK(clock.Tick(10 * second + second / 30, second / 30));
    CHECK(clock.State().frame == 1);
    CHECK(Near(clock.State().time_seconds, 1.0 / 30.0));

    CHECK(clock.SetPlaying(false, 11 * second));
    const double paused_time = clock.State().time_seconds;
    CHECK(!clock.Tick(111 * second, second / 30));
    CHECK(Near(clock.State().time_seconds, paused_time));
    CHECK(clock.SetPlaying(true, 111 * second));
    CHECK(clock.Tick(111 * second + second / 30, second / 30));
    CHECK(Near(clock.State().time_seconds, paused_time + 1.0 / 30.0));

    CHECK(clock.SetLoop(false, 112 * second));
    CHECK(clock.Scrub(7.0, 112 * second));
    CHECK(clock.Tick(612 * second, second / 30));
    CHECK(Near(clock.State().time_seconds, 507.0));
    CHECK(clock.State().playing);

    CHECK(clock.SetPlaying(false, 612 * second));
    CHECK(clock.Restart(612 * second));
    CHECK(clock.State().frame == 0);
    CHECK(Near(clock.State().time_seconds, 0.0));
    CHECK(clock.SetSpeed(2.0, 612 * second));
    CHECK(clock.SetPlaying(true, 612 * second));
    CHECK(clock.Tick(613 * second, second / 30));
    CHECK(Near(clock.State().time_seconds, 2.0));
    CHECK(clock.SetSpeed(0.5, 613 * second));
    CHECK(clock.Tick(614 * second, second / 30));
    CHECK(Near(clock.State().time_seconds, 2.5));

    CHECK(clock.SetPlaying(false, 614 * second));
    CHECK(clock.SetLoopLength(2.0, 614 * second));
    CHECK(clock.SetLoop(true, 614 * second));
    CHECK(Near(clock.State().time_seconds, 0.5));
    CHECK(clock.Scrub(1.8, 614 * second));
    CHECK(clock.SetPlaying(true, 614 * second));
    CHECK(clock.Tick(615 * second, second / 30));
    CHECK(Near(clock.State().time_seconds, 0.3));

    CHECK(!clock.SetSpeed(0.0, 615 * second));
    CHECK(!clock.SetLoopLength(INFINITY, 615 * second));
    const double before_backwards = clock.State().time_seconds;
    CHECK(!clock.Tick(614 * second, second / 30));
    CHECK(Near(clock.State().time_seconds, before_backwards));
    clock.Suspend(1000 * second);
    CHECK(clock.Tick(1000 * second + second / 30, second / 30));
    CHECK(Near(clock.State().time_seconds, before_backwards + 0.5 / 30.0));
    CHECK(!clock.Tick(1000 * second + second / 30, 0));

    std::cout << "shader browser preview clock tests passed\n";
}

#include "../../ui/xui/shader-browser-preview-health.hh"

#include <cassert>
#include <iostream>

using namespace xemu::shader_browser;

int main()
{
    PreviewHealthMonitor monitor;
    constexpr uint64_t start = UINT64_C(1000000000);
    PreviewHealth health = monitor.Sample(start, 0, 0, false);
    assert(!health.game_progressing);
    assert(health.pressure == PreviewPressure::Critical);

    health = monitor.Sample(start + 1, 1, 0, true);
    assert(!health.game_progressing);
    health = monitor.Sample(start + 2, 2, UINT64_C(16666667), true);
    assert(health.game_progressing);
    assert(health.pressure == PreviewPressure::Normal);
    assert(health.sampled_ns == start + 2);

    health = monitor.Sample(start + 3, 3, UINT64_C(28000000), true);
    assert(health.pressure == PreviewPressure::Elevated);
    health = monitor.Sample(start + 4, 4, UINT64_C(35000000), true);
    assert(health.pressure == PreviewPressure::High);
    health = monitor.Sample(start + 5, 5, UINT64_C(50000000), true);
    assert(health.pressure == PreviewPressure::Critical);

    health = monitor.Sample(start + kPreviewHealthStaleNs + 6, 5,
                            UINT64_C(16666667), true);
    assert(!health.game_progressing);
    assert(health.pressure == PreviewPressure::Critical);
    health = monitor.Sample(start + kPreviewHealthStaleNs + 7, 6,
                            UINT64_C(16666667), true);
    assert(health.game_progressing);

    health = monitor.Sample(start + kPreviewHealthStaleNs + 8, 7,
                            UINT64_C(16666667), false);
    assert(!health.game_progressing);
    assert(health.pressure == PreviewPressure::Critical);

    std::cout << "shader browser preview health tests passed\n";
    return 0;
}

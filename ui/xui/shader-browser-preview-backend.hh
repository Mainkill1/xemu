// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "shader-browser-preview-model.hh"

#include <cstdint>
#include <memory>

namespace xemu::shader_browser {

constexpr size_t kPreviewSlotCount = 3U;
constexpr uint64_t kPreviewSelectionDebounceNs = UINT64_C(150000000);
constexpr uint64_t kPreviewHealthStaleNs = UINT64_C(500000000);
constexpr uint64_t kPreviewVisibilityStaleNs = UINT64_C(250000000);
constexpr uint64_t kPreviewPressureRecoveryNs = UINT64_C(2000000000);
constexpr uint64_t kPreviewNormalIntervalNs = UINT64_C(66666667);
constexpr uint64_t kPreviewPausedIntervalNs = UINT64_C(33333333);
constexpr uint64_t kPreviewElevatedIntervalNs = UINT64_C(125000000);
constexpr uint64_t kPreviewHighIntervalNs = UINT64_C(250000000);

struct PreviewHealth {
    uint64_t sampled_ns = 0;
    PreviewPressure pressure = PreviewPressure::Critical;
    bool game_progressing = false;
};

struct PreviewWorkItem {
    uint64_t token = 0;
    uint64_t request_id = 0;
    PreviewWorkKind kind = PreviewWorkKind::None;
    std::shared_ptr<const PreviewPacket> packet;
    PreviewCompileKey compile_key;
    PreviewResultKey result_key;
    uint32_t slot = 0;
    uint64_t slot_generation = 0;
};

struct PreviewFrameRef {
    PreviewResultKey result_key;
    uint32_t slot = 0;
    uint64_t slot_generation = 0;
    uint32_t width = 0;
    uint32_t height = 0;
};

} // namespace xemu::shader_browser

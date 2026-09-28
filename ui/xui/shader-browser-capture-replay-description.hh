// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "shader-browser-capture-replay-description.h"
#include "shader-browser-draw-inputs.h"
#include <cstring>
#include <memory>
#include <string>
#include <vector>

namespace xemu::shader_browser {
struct CaptureReplayDescription {
    uint32_t kind = 0;
    std::vector<XemuShaderCaptureReplayBinding> bindings;
    uint32_t source_x = 0, source_y = 0, destination_x = 0, destination_y = 0;
    uint32_t width = 0, height = 0;
    uint64_t source_offset = 0, destination_offset = 0, bytes = 0;
    uint32_t clear_color_bits[4]{};
    uint32_t clear_color_mask = 0;
};
using SharedCaptureReplayDescription =
    std::shared_ptr<const CaptureReplayDescription>;
inline bool
ValidateCaptureReplayDescription(const CaptureReplayDescription &description,
                                 std::string *error)
{
    const auto Fail = [](std::string *target, const char *message) {
        if (target)
            *target = message;
        return false;
    };
    if (description.kind > XEMU_SHADER_CAPTURE_COMMAND_CHECKPOINT ||
        description.bindings.size() > XEMU_SHADER_CAPTURE_REPLAY_MAX_BINDINGS ||
        description.source_offset > UINT64_MAX - description.bytes ||
        description.destination_offset > UINT64_MAX - description.bytes)
        return Fail(error, "Replay description exceeds structural bounds");
    for (const auto &binding : description.bindings) {
        const auto &image = binding.image;
        if (binding.resource.kind > XEMU_SHADER_CAPTURE_RESOURCE_BUFFER ||
            binding.resource.slot > 32 || binding.resource.write > 1 ||
            binding.resource.ordinal > 255 || binding.checkpoint > 1 ||
            binding.role > XEMU_SHADER_CAPTURE_REPLAY_COPY_DESTINATION ||
            binding.slot > 32 ||
            !std::memchr(binding.blob_name, 0, sizeof(binding.blob_name)) ||
            image.format > XEMU_SHADER_CAPTURE_REPLAY_BGRA8_UNORM ||
            image.coordinate_origin > XEMU_SHADER_CAPTURE_REPLAY_BOTTOM_UP)
            return Fail(error, "Replay binding exceeds structural bounds");
        for (size_t i = 0; i < 4; ++i)
            if (image.storage_to_rgba[i] > 3 || image.sample_swizzle[i] > 5)
                return Fail(error, "Replay component mapping is invalid");
    }
    return true;
}
} // namespace xemu::shader_browser

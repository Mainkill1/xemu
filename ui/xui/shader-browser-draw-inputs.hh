// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "shader-browser-draw-inputs.h"
#include <array>
#include <string>
#include <vector>

namespace xemu::shader_browser {
constexpr size_t kDrawInputBudget = 64U * 1024U * 1024U;
struct OwnedDrawImage {
    uint32_t width = 0, height = 0;
    std::vector<uint8_t> rgba;
};
struct OwnedDrawTextureImage {
    uint32_t mip_level = 0, face = 0;
    OwnedDrawImage image;
};
struct OwnedDrawTexture {
    bool described = false;
    XemuShaderDrawTexture metadata{}; // pointers are always cleared in the copy
    std::vector<OwnedDrawTextureImage> images;
};
struct OwnedDrawUniform {
    uint32_t stage = 0;
    std::string name;
    uint32_t type = 0, components = 0, count = 0;
    std::vector<uint8_t> data;
};
struct OwnedDrawRegister {
    std::string name;
    uint32_t value = 0;
};
struct OwnedDrawBlob {
    std::string name;
    std::vector<uint8_t> bytes;
    uint32_t slot = 0, format = 0, components = 0, stride = 0, count = 0;
    uint64_t offset = 0;
    uint32_t normalized = 0, integer = 0;
};
struct OwnedDrawInputs {
    std::array<OwnedDrawTexture, 4> textures;
    std::vector<OwnedDrawUniform> uniforms;
    std::array<std::string, 5> sources;
    std::vector<OwnedDrawRegister> registers;
    std::vector<OwnedDrawBlob> blobs;
    OwnedDrawImage before, after;
    bool complete = false;
};
} // namespace xemu::shader_browser

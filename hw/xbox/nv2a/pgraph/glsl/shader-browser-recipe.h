/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef HW_XBOX_NV2A_PGRAPH_GLSL_SHADER_BROWSER_RECIPE_H
#define HW_XBOX_NV2A_PGRAPH_GLSL_SHADER_BROWSER_RECIPE_H

#include "shaders.h"
#include "ui/xui/shader-browser-session-provider.hh"

#define PGRAPH_SHADER_BROWSER_RECIPE_VERSION 1U
#define PGRAPH_SHADER_BROWSER_RECIPE_MAX 8192U

/* Serialize guest-derived shader choices in a stable little-endian format.
 * Stage and recipe version are also framed by the portable hash function. */
bool pgraph_shader_browser_encode_recipe(const ShaderState *state,
                                         uint32_t stage, uint8_t *data,
                                         size_t capacity, size_t *size);

#endif

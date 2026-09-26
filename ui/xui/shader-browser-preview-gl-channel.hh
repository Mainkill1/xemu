// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "shader-browser-preview-model.hh"
#include <epoxy/gl.h>

namespace xemu::shader_browser {
// Set only on a worker-owned output slot before its producer fence. Sampling
// yields grayscale without another draw, readback, source copy or compilation.
inline void SetPreviewGlOutputChannel(PreviewChannel channel)
{
    const GLint components[] = { GL_RED, GL_GREEN, GL_BLUE, GL_ALPHA };
    const int component = PreviewChannelComponent(channel);
    GLint swizzle[] = { GL_RED, GL_GREEN, GL_BLUE, GL_ALPHA };
    if (component >= 0) {
        swizzle[0] = swizzle[1] = swizzle[2] = components[component];
        swizzle[3] = GL_ONE;
    }
    glTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_RGBA, swizzle);
}
} // namespace xemu::shader_browser

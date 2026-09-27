/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "qemu/osdep.h"
#include "hw/xbox/nv2a/pgraph/gl/override-interface.h"

#include <EGL/egl.h>
#include <EGL/eglext.h>

static GLuint compile_shader(GLenum stage, const char *source)
{
    GLuint shader = glCreateShader(stage);
    glShaderSource(shader, 1, &source, NULL);
    glCompileShader(shader);
    GLint compiled = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
    assert(compiled == GL_TRUE);
    return shader;
}

static GLuint link_program(const char *fragment_source)
{
    const char *vertex_source =
        "#version 330 core\n"
        "void main() { gl_Position = vec4(0, 0, 0, 1); }\n";
    GLuint vertex = compile_shader(GL_VERTEX_SHADER, vertex_source);
    GLuint fragment = compile_shader(GL_FRAGMENT_SHADER, fragment_source);
    GLuint program = glCreateProgram();
    glAttachShader(program, vertex);
    glAttachShader(program, fragment);
    glLinkProgram(program);
    GLint linked = 0;
    glGetProgramiv(program, GL_LINK_STATUS, &linked);
    assert(linked == GL_TRUE);
    glDeleteShader(vertex);
    glDeleteShader(fragment);
    return program;
}

int main(void)
{
    PFNEGLGETPLATFORMDISPLAYEXTPROC platform_display =
        (PFNEGLGETPLATFORMDISPLAYEXTPROC)eglGetProcAddress(
            "eglGetPlatformDisplayEXT");
    EGLDisplay display = platform_display ?
                             platform_display(EGL_PLATFORM_SURFACELESS_MESA,
                                              EGL_DEFAULT_DISPLAY, NULL) :
                             eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (display == EGL_NO_DISPLAY || !eglInitialize(display, NULL, NULL)) {
        fprintf(stderr, "Skipping GL ABI test: EGL display unavailable\n");
        return 77;
    }
    assert(eglBindAPI(EGL_OPENGL_API));
    const EGLint config_attrs[] = {
        EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_RENDERABLE_TYPE,
        EGL_OPENGL_BIT,   EGL_NONE,
    };
    EGLConfig config;
    EGLint count = 0;
    assert(eglChooseConfig(display, config_attrs, &config, 1, &count));
    assert(count == 1);
    const EGLint pbuffer_attrs[] = {
        EGL_WIDTH, 1, EGL_HEIGHT, 1, EGL_NONE,
    };
    EGLSurface surface =
        eglCreatePbufferSurface(display, config, pbuffer_attrs);
    EGLContext context =
        eglCreateContext(display, config, EGL_NO_CONTEXT, NULL);
    if (surface == EGL_NO_SURFACE || context == EGL_NO_CONTEXT ||
        !eglMakeCurrent(display, surface, surface, context)) {
        fprintf(stderr, "Skipping GL ABI test: OpenGL context unavailable\n");
        return 77;
    }

    const UniformInfo alpha_ref = {
        .name = "alphaRef",
        .type = UniformElementType_int,
        .size = sizeof(int),
        .count = 1,
    };
    GLuint valid =
        link_program("#version 330 core\n"
                     "uniform int alphaRef;\n"
                     "layout(location=0) out vec4 color;\n"
                     "void main() { color=vec4(float(alphaRef)); }\n");
    char *error = NULL;
    assert(pgraph_gl_override_validate_uniforms(valid, &alpha_ref, 1, &error));
    glUseProgram(valid);
    glUniform1i(glGetUniformLocation(valid, "alphaRef"), 1);
    assert(glGetError() == GL_NO_ERROR);

    GLuint bad_alpha = link_program("#version 330 core\n"
                                    "uniform float alphaRef;\n"
                                    "layout(location=0) out vec4 color;\n"
                                    "void main() { color=vec4(alphaRef); }\n");
    assert(!pgraph_gl_override_validate_uniforms(bad_alpha, &alpha_ref, 1,
                                                 &error));
    assert(strstr(error, "alphaRef"));
    g_clear_pointer(&error, g_free);
    assert(glGetError() == GL_NO_ERROR);

    GLuint bad_sampler =
        link_program("#version 330 core\n"
                     "uniform float texSamp0;\n"
                     "layout(location=0) out vec4 color;\n"
                     "void main() { color=vec4(texSamp0); }\n");
    assert(!pgraph_gl_override_validate_sampler(bad_sampler, "texSamp0",
                                                GL_SAMPLER_2D, &error));
    assert(strstr(error, "texSamp0"));
    g_clear_pointer(&error, g_free);
    assert(glGetError() == GL_NO_ERROR);

    const UniformInfo constants = {
        .name = "c",
        .type = UniformElementType_vec4,
        .size = sizeof(vec4),
        .count = 192,
    };
    GLuint prefix = link_program("#version 330 core\n"
                                 "uniform vec4 c[192];\n"
                                 "layout(location=0) out vec4 color;\n"
                                 "void main() { color=c[109]; }\n");
    assert(pgraph_gl_override_validate_uniforms(prefix, &constants, 1, &error));

    glDeleteProgram(prefix);
    glDeleteProgram(bad_sampler);
    glDeleteProgram(bad_alpha);
    glDeleteProgram(valid);
    eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglDestroyContext(display, context);
    eglDestroySurface(display, surface);
    eglTerminate(display);
    return 0;
}

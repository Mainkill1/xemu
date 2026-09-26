// SPDX-License-Identifier: GPL-2.0-or-later
#include "shader-browser-preview-adapter.hh"
#include <SDL3/SDL.h>
#include <epoxy/gl.h>
#include <cassert>
#include <cstdio>
#include <vector>
using namespace xemu::shader_browser;
int main()
{
    assert(SDL_Init(SDL_INIT_VIDEO));
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,
                        SDL_GL_CONTEXT_PROFILE_CORE);
    auto *window = SDL_CreateWindow("Private scene test", 64, 64,
                                    SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
    assert(window);
    auto context = SDL_GL_CreateContext(window);
    assert(context);
    const std::string fragment = "#version 400\nin vec4 vtxD0;\nout vec4 "
                                 "color;\nvoid main(){color=vec4(1,0,0,1);}";
    auto compile = [](GLenum type, const std::string &source) {
        GLuint shader = glCreateShader(type);
        const char *text = source.c_str();
        glShaderSource(shader, 1, &text, nullptr);
        glCompileShader(shader);
        GLint ok = 0;
        glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
        assert(ok);
        return shader;
    };
    GLuint vs = compile(
        GL_VERTEX_SHADER,
        BuildPreviewSyntheticVertexSource(fragment, PreviewBackend::OpenGL));
    GLuint fs = compile(GL_FRAGMENT_SHADER, fragment);
    GLuint program = glCreateProgram();
    glAttachShader(program, vs);
    glAttachShader(program, fs);
    glLinkProgram(program);
    GLint linked = 0;
    glGetProgramiv(program, GL_LINK_STATUS, &linked);
    assert(linked);
    glUseProgram(program);
    GLuint vao, vbo, fbo, texture;
    glGenVertexArrays(1, &vao);
    glBindVertexArray(vao);
    glGenBuffers(1, &vbo);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, sizeof(PreviewSceneVertex),
                          nullptr);
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 64, 64, 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, nullptr);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                           texture, 0);
    assert(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE);
    glViewport(0, 0, 64, 64);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_BLEND);
    auto render = [&](const PreviewScene &scene) {
        auto vertices = BuildPreviewSceneGeometry(scene);
        glBufferData(GL_ARRAY_BUFFER,
                     vertices.size() * sizeof(PreviewSceneVertex),
                     vertices.data(), GL_STREAM_DRAW);
        glClearColor(0, 0, 0, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        glDrawArrays(GL_TRIANGLES, 0, vertices.size());
        std::vector<uint8_t> pixels(64 * 64 * 4);
        glReadPixels(0, 0, 64, 64, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
        assert(glGetError() == GL_NO_ERROR);
        return pixels;
    };
    std::vector<std::vector<uint8_t>> images;
    for (auto mesh :
         { PreviewMesh::Quad, PreviewMesh::Sphere, PreviewMesh::Cube }) {
        PreviewScene scene;
        scene.mesh = mesh;
        scene.yaw = 30;
        scene.pitch = 20;
        scene.distance = 4;
        images.push_back(render(scene));
        size_t covered = 0;
        for (size_t i = 0; i < images.back().size(); i += 4)
            covered += images.back()[i] == 255;
        assert(covered > 80 && covered < 3600);
        std::printf("OpenGL mesh %d: %zu red pixels\n", int(mesh), covered);
        scene.yaw = -40;
        scene.pan[0] = 0.5f;
        assert(render(scene) != images.back());
    }
    assert(images[0] != images[1] && images[0] != images[2] &&
           images[1] != images[2]);
    glDeleteTextures(1, &texture);
    glDeleteFramebuffers(1, &fbo);
    glDeleteBuffers(1, &vbo);
    glDeleteVertexArrays(1, &vao);
    glDeleteProgram(program);
    glDeleteShader(vs);
    glDeleteShader(fs);
    SDL_GL_DestroyContext(context);
    SDL_DestroyWindow(window);
    SDL_Quit();
    std::puts("OpenGL private scene silhouettes and camera output PASS");
}

// SPDX-License-Identifier: GPL-2.0-or-later
#include "shader-browser-preview-adapter.hh"
#include "shader-browser-preview-gl-channel.hh"
#include <SDL3/SDL.h>
#include <epoxy/gl.h>
#include <cassert>
#include <cstdio>
#include <cmath>
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
    const std::string fragment =
        "#version 400\nin vec4 vtxD0;\nin vec4 vtxT0;\n"
        "uniform bool diagnostic;\nout vec4 color;\n"
        "void main(){color=diagnostic ? vec4(vtxD0.r,vtxT0.xy,1) : "
        "vec4(1,0,0,1);}";
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
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(
        1, 4, GL_FLOAT, GL_FALSE, sizeof(PreviewSceneVertex),
        reinterpret_cast<const void *>(offsetof(PreviewSceneVertex, color)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(
        2, 2, GL_FLOAT, GL_FALSE, sizeof(PreviewSceneVertex),
        reinterpret_cast<const void *>(offsetof(PreviewSceneVertex, uv)));
    for (int i = 0; i < 4; ++i) {
        glEnableVertexAttribArray(3 + i);
        glVertexAttribPointer(
            3 + i, 4, GL_FLOAT, GL_FALSE, sizeof(PreviewSceneVertex),
            reinterpret_cast<const void *>(
                offsetof(PreviewSceneVertex, colors) + i * 16));
    }
    glEnableVertexAttribArray(7);
    glVertexAttribPointer(
        7, 1, GL_FLOAT, GL_FALSE, sizeof(PreviewSceneVertex),
        reinterpret_cast<const void *>(offsetof(PreviewSceneVertex, fog)));
    glEnableVertexAttribArray(8);
    glVertexAttribPointer(8, 3, GL_FLOAT, GL_FALSE, sizeof(PreviewSceneVertex),
                          reinterpret_cast<const void *>(
                              offsetof(PreviewSceneVertex, direction)));
    glEnableVertexAttribArray(9);
    glVertexAttribPointer(9, 4, GL_FLOAT, GL_FALSE, sizeof(PreviewSceneVertex),
                          reinterpret_cast<const void *>(
                              offsetof(PreviewSceneVertex, cube_stages)));
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
        PreviewSyntheticFixture fixture;
        fixture.corner_colors = { { { 0, 255, 0, 255 },
                                    { 255, 255, 0, 255 },
                                    { 0, 0, 0, 255 },
                                    { 255, 0, 0, 255 } } };
        fixture.uv_scale = { 0.5f, 0.25f };
        fixture.uv_offset = { 0.2f, 0.3f };
        ApplyPreviewSyntheticFixture(fixture, vertices);
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
    GLuint depth_buffer;
    glGenRenderbuffers(1, &depth_buffer);
    glBindRenderbuffer(GL_RENDERBUFFER, depth_buffer);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, 64, 64);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                              GL_RENDERBUFFER, depth_buffer);
    assert(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE);
    const char *reference_vertex =
        "#version 400\nlayout(location=0) in vec4 position;"
        "layout(location=1) in vec4 vertexColor;out vec4 referenceColor;"
        "void main(){gl_Position=position;referenceColor=vertexColor;}";
    const char *reference_fragment =
        "#version 400\nin vec4 referenceColor;out vec4 color;"
        "void main(){color=referenceColor;}";
    GLuint rv = compile(GL_VERTEX_SHADER, reference_vertex);
    GLuint rf = compile(GL_FRAGMENT_SHADER, reference_fragment);
    GLuint reference_program = glCreateProgram();
    glAttachShader(reference_program, rv);
    glAttachShader(reference_program, rf);
    glLinkProgram(reference_program);
    glGetProgramiv(reference_program, GL_LINK_STATUS, &linked);
    assert(linked);
    const std::string discard_fragment =
        "#version 400\nout vec4 color;void main(){discard;}";
    GLuint dv = compile(GL_VERTEX_SHADER, BuildPreviewSyntheticVertexSource(
        discard_fragment, PreviewBackend::OpenGL));
    GLuint df = compile(GL_FRAGMENT_SHADER, discard_fragment);
    GLuint discard_program = glCreateProgram();
    glAttachShader(discard_program, dv);
    glAttachShader(discard_program, df);
    glLinkProgram(discard_program);
    glGetProgramiv(discard_program, GL_LINK_STATUS, &linked);
    assert(linked);
    auto render_shared = [&](const PreviewScene &scene, bool discard) {
        const auto frame = BuildPreviewSceneFrame(scene);
        assert(frame.draw_count == 5);
        glBufferData(GL_ARRAY_BUFFER,
                     frame.vertices.size() * sizeof(PreviewSceneVertex),
                     frame.vertices.data(), GL_STREAM_DRAW);
        glViewport(0, 0, 64, 64);
        glEnable(GL_DEPTH_TEST);
        glDepthMask(GL_TRUE);
        glDepthFunc(GL_LESS);
        glDisable(GL_CULL_FACE);
        glDisable(GL_BLEND);
        glClearColor(0, 0, 0, 1);
        glClearDepth(1);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glUseProgram(reference_program);
        for (size_t i = 0; i < frame.draw_count - 1; ++i) {
            const auto &draw = frame.draws[i];
            glDrawArrays(GL_TRIANGLES, draw.first_vertex, draw.vertex_count);
        }
        const auto &target = frame.draws[4];
        glUseProgram(discard ? discard_program : program);
        glDrawArrays(GL_TRIANGLES, target.first_vertex, target.vertex_count);
        std::vector<uint8_t> output(64 * 64 * 4);
        glReadPixels(0, 0, 64, 64, GL_RGBA, GL_UNSIGNED_BYTE,
                     output.data());
        assert(glGetError() == GL_NO_ERROR);
        return output;
    };
    auto scene_pixels = render_shared({}, false);
    auto find_color = [&](int red, int green, int blue) {
        for (size_t i = 0; i < scene_pixels.size(); i += 4)
            if (std::abs(int(scene_pixels[i]) - red) <= 4 &&
                std::abs(int(scene_pixels[i + 1]) - green) <= 4 &&
                std::abs(int(scene_pixels[i + 2]) - blue) <= 4)
                return i;
        return scene_pixels.size();
    };
    assert(find_color(41, 209, 214) < scene_pixels.size());
    assert(find_color(242, 184, 48) < scene_pixels.size());
    assert(find_color(69, 74, 79) < scene_pixels.size());
    assert(find_color(41, 69, 94) < scene_pixels.size());
    PreviewScene without_blocker;
    without_blocker.references[3].visible = false;
    auto uncovered = render_shared(without_blocker, false);
    size_t blocker_pixel = scene_pixels.size();
    for (size_t i = 0; i < scene_pixels.size(); i += 4)
        if (std::abs(int(scene_pixels[i]) - 41) <= 4 &&
            std::abs(int(scene_pixels[i + 1]) - 209) <= 4 &&
            uncovered[i] == 255 && uncovered[i + 1] == 0) {
            blocker_pixel = i;
            break;
        }
    assert(blocker_pixel < scene_pixels.size());
    auto discarded = render_shared({}, true);
    assert(discarded[4 * (32 * 64 + 32)] != 255);
    assert(std::abs(int(discarded[blocker_pixel]) - 41) <= 4);
    glDisable(GL_DEPTH_TEST);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                              GL_RENDERBUFFER, 0);
    glDeleteRenderbuffers(1, &depth_buffer);
    glDeleteProgram(reference_program);
    glDeleteProgram(discard_program);
    glDeleteShader(rv);
    glDeleteShader(rf);
    glDeleteShader(dv);
    glDeleteShader(df);
    glUseProgram(program);
    std::puts("OpenGL shared-depth references, blocker and discard PASS");
    glUniform1i(glGetUniformLocation(program, "diagnostic"), 1);
    const auto diagnostic = render(PreviewScene{});
    // Analytic quad interpolation at off-center pixel centers. Red is corner
    // color, green/blue are independently scaled and offset fixture UVs.
    for (int y : { 24, 40 })
        for (int x : { 24, 40 }) {
            const float u = (((x + 0.5f) / 32 - 1) * 4 / 2.41421356f + 1) / 2;
            const float v = (((y + 0.5f) / 32 - 1) * 4 / 2.41421356f + 1) / 2;
            const float expected[] = { u, u * 0.5f + 0.2f, v * 0.25f + 0.3f };
            for (size_t c = 0; c < 3; ++c)
                assert(std::abs(diagnostic[4 * (y * 64 + x) + c] -
                                expected[c] * 255) < 2);
        }
    std::puts("OpenGL fixture color and transformed UV interpolation PASS");
    auto fixture_render = [&](const std::string &source,
                              const PreviewSyntheticFixture &fixture) {
        GLuint vertex = compile(
            GL_VERTEX_SHADER,
            BuildPreviewSyntheticVertexSource(source, PreviewBackend::OpenGL));
        GLuint fragment_shader = compile(GL_FRAGMENT_SHADER, source);
        GLuint p = glCreateProgram();
        glAttachShader(p, vertex);
        glAttachShader(p, fragment_shader);
        glLinkProgram(p);
        GLint ok;
        glGetProgramiv(p, GL_LINK_STATUS, &ok);
        assert(ok);
        glUseProgram(p);
        std::array<bool, 4> cubes{};
        for (size_t i = 0; i < 4; ++i) {
            const std::string name = "texSamp" + std::to_string(i);
            const char *ptr = name.c_str();
            GLuint index = GL_INVALID_INDEX;
            glGetUniformIndices(p, 1, &ptr, &index);
            GLint type = GL_SAMPLER_2D;
            if (index != GL_INVALID_INDEX)
                glGetActiveUniformsiv(p, 1, &index, GL_UNIFORM_TYPE, &type);
            cubes[i] = type == GL_SAMPLER_CUBE;
        }
        auto vertices = BuildPreviewSceneGeometry({});
        ApplyPreviewSyntheticFixture(fixture, vertices, cubes);
        glBufferData(GL_ARRAY_BUFFER,
                     vertices.size() * sizeof(PreviewSceneVertex),
                     vertices.data(), GL_STREAM_DRAW);
        GLuint textures[4];
        glGenTextures(4, textures);
        for (int i = 0; i < 4; ++i) {
            const auto pixels = GeneratePreviewTexture(fixture, i);
            const std::string name = "texSamp" + std::to_string(i);
            const bool cube = cubes[i];
            const GLenum target = cube ? GL_TEXTURE_CUBE_MAP : GL_TEXTURE_2D;
            glActiveTexture(GL_TEXTURE0 + i);
            glBindTexture(target, textures[i]);
            for (int face = 0; face < (cube ? 6 : 1); ++face)
                glTexImage2D(cube ? GL_TEXTURE_CUBE_MAP_POSITIVE_X + face :
                                    target,
                             0, GL_RGBA8, 8, 8, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                             pixels.data() + face * kPreviewTextureFaceBytes);
            glTexParameteri(target, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            glTexParameteri(target, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            glUniform1i(glGetUniformLocation(p, name.c_str()), i);
        }
        glUniform4fv(glGetUniformLocation(p, "fogColor"), 1,
                     fixture.fog_color.data());
        glUniform4fv(glGetUniformLocation(p, "consts"), 1,
                     fixture.constant_color.data());
        glUniform1i(glGetUniformLocation(p, "alphaRef"),
                    fixture.alpha_reference);
        glClear(GL_COLOR_BUFFER_BIT);
        glDrawArrays(GL_TRIANGLES, 0, vertices.size());
        std::array<uint8_t, 4> pixel;
        glReadPixels(32, 32, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel.data());
        assert(glGetError() == GL_NO_ERROR);
        glDeleteTextures(4, textures);
        glDeleteProgram(p);
        glDeleteShader(vertex);
        glDeleteShader(fragment_shader);
        return pixel;
    };
    for (int profile = 0; profile < 9; ++profile) {
        auto f =
            MakePreviewFixture(static_cast<PreviewFixtureProfile>(profile));
        auto result = fixture_render(
            "#version 400\nuniform sampler2D texSamp0; out vec4 color; void "
            "main(){color=texture(texSamp0,vec2(0.3125));}",
            f);
        auto expected = GeneratePreviewTexture(f, 0);
        for (int c = 0; c < 4; ++c)
            assert(result[c] == expected[4 * (2 * 8 + 2) + c]);
    }
    std::puts("OpenGL all nine profile sample readbacks PASS");
    auto fixture = MakePreviewFixture(PreviewFixtureProfile::Cubemap);
    const std::string cube_source =
        "#version 400\nuniform samplerCube texSamp0; in vec4 vtxT0; out vec4 "
        "color; void main(){color=texture(texSamp0,vtxT0.xyz);}";
    assert((fixture_render(cube_source, fixture) ==
            std::array<uint8_t, 4>{ 40, 40, 255, 255 }));
    fixture.cube_direction = { 1, 0, 0 };
    assert((fixture_render(cube_source, fixture) ==
            std::array<uint8_t, 4>{ 255, 40, 40, 255 }));
    fixture.cube_direction = { 0, 0, 1 };
    const std::string spaced_cube =
        "#version 400\nuniform samplerCube\n\ttexSamp0; in vec4 vtxT0; out "
        "vec4 color;"
        "void main(){color=texture(texSamp0,vtxT0.xyz);}";
    assert((fixture_render(spaced_cube, fixture) ==
            std::array<uint8_t, 4>{ 40, 40, 255, 255 }));
    fixture.cube_direction = { 1, 0, 0 };
    fixture.uv_scale = { 0, 0 };
    fixture.uv_offset = { 0.25f, 0.25f };
    const std::string commented_2d =
        "#version 400\n// samplerCube texSamp0\n/* samplerCube texSamp0; */\n"
        "uniform sampler2D texSamp0; in vec4 vtxT0; out vec4 color;"
        "void main(){color=vec4(vtxT0.xy,0,1)*texture(texSamp0,vec2(0.5));}";
    assert((fixture_render(commented_2d, fixture) ==
            std::array<uint8_t, 4>{ 64, 10, 0, 255 }));
    std::puts("OpenGL reflected cube routing: whitespace and misleading "
              "comments PASS");
    std::puts("OpenGL cube +Z blue / +X red PASS");
    fixture = MakePreviewFixture(PreviewFixtureProfile::MultiTexture);
    fixture.textures.fill(PreviewFixtureProfile::MultiTexture);
    const std::string multi =
        "#version 400\nuniform sampler2D texSamp0; uniform sampler2D texSamp1; "
        "out vec4 color; void "
        "main(){color=vec4(texture(texSamp0,vec2(0.5)).r,texture(texSamp1,vec2("
        "0.5)).g,0,1);}";
    assert((fixture_render(multi, fixture) ==
            std::array<uint8_t, 4>{ 255, 255, 0, 255 }));
    fixture.textures[1] = PreviewFixtureProfile::Flat;
    assert((fixture_render(multi, fixture) ==
            std::array<uint8_t, 4>{ 255, 90, 0, 255 }));
    std::puts("OpenGL distinct T0/T1 and independent input edit PASS");
    fixture.fog = 0.5f;
    fixture.colors[0][0] = 0.2f;
    fixture.colors[1][1] = 0.4f;
    fixture.colors[2][2] = 0.6f;
    fixture.colors[3][3] = 0.8f;
    const std::string colors =
        "#version 400\nin vec4 vtxD0; in vec4 vtxD1; in vec4 vtxB0; in vec4 "
        "vtxB1; in float vtxFog; out vec4 color; void "
        "main(){color=vec4(vtxD0.r,vtxD1.g,vtxB0.b,vtxB1.a)*vtxFog;}";
    auto pixel = fixture_render(colors, fixture);
    for (int i = 0; i < 4; ++i)
        assert(std::abs(int(pixel[i]) - int((i + 1) * 25.5f)) <= 1);
    fixture.fog = 1;
    assert(fixture_render(colors, fixture)[0] == 51);
    const std::string constants =
        "#version 400\nuniform vec4 consts; uniform vec4 fogColor; uniform int "
        "alphaRef; out vec4 color; void "
        "main(){color=vec4(consts.r,fogColor.g,0,float(alphaRef)/255);}";
    fixture.constant_color[0] = 0.2f;
    fixture.fog_color[1] = 0.4f;
    fixture.alpha_reference = 128;
    assert((fixture_render(constants, fixture) ==
            std::array<uint8_t, 4>{ 51, 102, 0, 128 }));
    fixture.constant_color[0] = 0.6f;
    fixture.fog_color[1] = 0.8f;
    fixture.alpha_reference = 64;
    assert((fixture_render(constants, fixture) ==
            std::array<uint8_t, 4>{ 153, 204, 0, 64 }));
    std::puts("OpenGL independent colors, fog, constants, alpha edits PASS");
    // Sample the real RGBA output through the same production swizzle used by
    // the HUD. glReadPixels on the source FBO alone would ignore texture
    // swizzle.
    GLuint sample_vs =
        compile(GL_VERTEX_SHADER, "#version 400\nvoid main(){vec2 "
                                  "p=vec2((gl_VertexID<<1)&2,gl_VertexID&2);gl_"
                                  "Position=vec4(p*2-1,0,1);}");
    GLuint sample_fs =
        compile(GL_FRAGMENT_SHADER,
                "#version 400\nuniform sampler2D image;out vec4 color;void "
                "main(){color=texture(image,vec2(0.5));}");
    GLuint sample_program = glCreateProgram();
    glAttachShader(sample_program, sample_vs);
    glAttachShader(sample_program, sample_fs);
    glLinkProgram(sample_program);
    glGetProgramiv(sample_program, GL_LINK_STATUS, &linked);
    assert(linked);
    glUseProgram(sample_program);
    glUniform1i(glGetUniformLocation(sample_program, "image"), 0);
    GLuint sample_texture, sample_fbo;
    glGenTextures(1, &sample_texture);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, sample_texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                 nullptr);
    glGenFramebuffers(1, &sample_fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, sample_fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                           sample_texture, 0);
    assert(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glViewport(0, 0, 1, 1);
    auto sample_output = [&] {
        glDrawArrays(GL_TRIANGLES, 0, 3);
        std::array<uint8_t, 4> result;
        glReadPixels(0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, result.data());
        assert(glGetError() == GL_NO_ERROR);
        return result;
    };
    const std::array<uint8_t, 4> final_pixel{ 153, 204, 0, 64 };
    for (auto channel : { PreviewChannel::Red, PreviewChannel::Green,
                          PreviewChannel::Blue, PreviewChannel::Alpha }) {
        SetPreviewGlOutputChannel(channel);
        const auto value = final_pixel[PreviewChannelComponent(channel)];
        assert((sample_output() ==
                std::array<uint8_t, 4>{ value, value, value, 255 }));
    }
    SetPreviewGlOutputChannel(PreviewChannel::FinalRGBA);
    assert(sample_output() == final_pixel);
    auto chart_fixture = MakePreviewFixture(PreviewFixtureProfile::Flat);
    for (auto &c : chart_fixture.corner_colors)
        c = { 255, 255, 255, 255 };
    chart_fixture.colors[0] = { 0.2f, 0.4f, 0.6f, 0.5f };
    chart_fixture.fog = 0.25f;
    chart_fixture.alpha_reference = 127;
    chart_fixture.textures[0] = PreviewFixtureProfile::MultiTexture;
    std::vector<uint8_t> chart;
    std::string chart_error;
    for (auto channel :
         { PreviewChannel::UV, PreviewChannel::D0, PreviewChannel::T0,
           PreviewChannel::Fog, PreviewChannel::DepthRamp,
           PreviewChannel::FixtureAlphaMask }) {
        assert(RenderPreviewDiagnostic(channel, chart_fixture, 64, 64, &chart,
                                       &chart_error));
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 64, 64, 0, GL_RGBA,
                     GL_UNSIGNED_BYTE, chart.data());
        const auto result = sample_output();
        const uint8_t expected = channel == PreviewChannel::UV ||
                                         channel == PreviewChannel::DepthRamp ?
                                     129 :
                                 channel == PreviewChannel::Fog ? 64 :
                                 channel == PreviewChannel::D0  ? 51 :
                                                                  255;
        assert(result[0] == expected && result[3] == 255);
    }
    assert(!RenderPreviewDiagnostic(PreviewChannel::ShaderDiscard,
                                    chart_fixture, 64, 64, &chart,
                                    &chart_error));
    assert(chart_error.find("Unsupported") != std::string::npos);
    std::puts("OpenGL sampled final RGBA/scalars and fixture charts / "
              "unsupported discard PASS");
    glDeleteTextures(1, &sample_texture);
    glDeleteFramebuffers(1, &sample_fbo);
    glDeleteProgram(sample_program);
    glDeleteShader(sample_vs);
    glDeleteShader(sample_fs);
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

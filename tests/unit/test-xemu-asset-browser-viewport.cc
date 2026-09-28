// SPDX-License-Identifier: GPL-2.0-or-later
#include "../../ui/xui/asset-browser-viewport.hh"
#include "../../ui/xui/asset-browser-material.hh"
#include <SDL3/SDL.h>
#include <epoxy/gl.h>
#include <glib.h>
#include <chrono>
using namespace xemu::asset_browser;
static std::shared_ptr<AssetAssembly> Fixture(uint64_t id)
{
    auto assembly = std::make_shared<AssetAssembly>();
    assembly->id = id;
    assembly->bounds.valid = true;
    assembly->bounds.minimum = { -1, -0.5f, 0 };
    assembly->bounds.maximum = { 1, 0.5f, 0 };
    for (int side = 0; side < 2; ++side) {
        auto part = std::make_shared<AssetPart>();
        part->id = id * 2 + side;
        part->status = AssetStatus::Ready;
        part->has_uv = true;
        float left = side ? 0.1f : -1.0f, right = side ? 1.0f : -0.1f;
        part->vertices.resize(4);
        part->vertices[0].position = { left, -0.5f, 0 };
        part->vertices[1].position = { right, -0.5f, 0 };
        part->vertices[2].position = { right, 0.5f, 0 };
        part->vertices[3].position = { left, 0.5f, 0 };
        part->vertices[0].uv = { 0, 0 };
        part->vertices[1].uv = { 1, 0 };
        part->vertices[2].uv = { 1, 1 };
        part->vertices[3].uv = { 0, 1 };
        part->indices = { 0, 1, 2, 0, 2, 3 };
        auto event = std::make_shared<capture::CaptureOccurrence>();
        auto &tex = event->inputs.textures[0];
        tex.described = true;
        tex.metadata.bound = 1;
        tex.metadata.depth = 1;
        tex.metadata.face_count = 1;
        tex.metadata.width = tex.metadata.height = 2;
        tex.metadata.mag_filter = tex.metadata.min_filter = 0x2600;
        auto block = std::make_shared<capture::CaptureImmutableBlock>();
        // Left part uses a red/blue UV split; right is green.
        block->bytes =
            side ? std::vector<uint8_t>{ 0, 255, 0, 255, 0, 255, 0, 255,
                                         0, 255, 0, 255, 0, 255, 0, 255 } :
                   std::vector<uint8_t>{ 255, 0, 0, 255, 0, 0, 255, 255,
                                         255, 0, 0, 255, 0, 0, 255, 255 };
        tex.images.push_back({ 0, 0, { 2, 2, block } });
        part->occurrence = event;
        assembly->parts.push_back(part);
    }
    return assembly;
}
int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, nullptr);
    g_assert_true(SDL_Init(SDL_INIT_VIDEO));
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,
                        SDL_GL_CONTEXT_PROFILE_CORE);
    auto *window = SDL_CreateWindow("Asset viewport fixture", 128, 128,
                                    SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
    g_assert_nonnull(window);
    auto context = SDL_GL_CreateContext(window);
    g_assert_nonnull(context);
    AssetViewport viewport;
    GLuint sentinel = 0;
    glGenTextures(1, &sentinel);
    glActiveTexture(GL_TEXTURE3);
    glBindTexture(GL_TEXTURE_2D, sentinel);
    glEnable(GL_SCISSOR_TEST);
    glScissor(1, 2, 3, 4);
    glViewport(5, 6, 7, 8);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 17);
    glPixelStorei(GL_UNPACK_SKIP_ROWS, 3);
    glEnable(GL_COLOR_LOGIC_OP);
    glEnable(GL_CLIP_DISTANCE0);
    auto frame = viewport.Render(Fixture(1), {}, 128, 128);
    g_assert_cmpuint(frame.texture, !=, 0);
    g_assert_cmpuint(frame.drawn_parts, ==, 2);
    g_assert_cmpuint(frame.textured_parts, ==, 2);
    GLint actual = 0, vp[4];
    glGetIntegerv(GL_ACTIVE_TEXTURE, &actual);
    g_assert_cmpint(actual, ==, GL_TEXTURE3);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &actual);
    g_assert_cmpuint(actual, ==, sentinel);
    glGetIntegerv(GL_VIEWPORT, vp);
    g_assert_cmpint(vp[0], ==, 5);
    g_assert_cmpint(vp[3], ==, 8);
    g_assert_true(glIsEnabled(GL_SCISSOR_TEST));
    g_assert_true(glIsEnabled(GL_COLOR_LOGIC_OP));
    g_assert_true(glIsEnabled(GL_CLIP_DISTANCE0));
    glGetIntegerv(GL_UNPACK_ROW_LENGTH, &actual);
    g_assert_cmpint(actual, ==, 17);
    glGetIntegerv(GL_UNPACK_SKIP_ROWS, &actual);
    g_assert_cmpint(actual, ==, 3);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, frame.texture);
    std::vector<uint8_t> pixels(128 * 128 * 4);
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    auto pixel = [&](int x, int y, int channel) {
        return pixels[(y * 128 + x) * 4 + channel];
    };
    g_assert_cmpuint(pixel(25, 64, 0), >, 200);
    g_assert_cmpuint(pixel(45, 64, 2), >, 200);
    g_assert_cmpuint(pixel(95, 64, 1), >, 200);
    auto gl_rows = Fixture(90);
    gl_rows->context.backend = 1;
    auto row_part = std::make_shared<AssetPart>(*gl_rows->parts[0]);
    auto row_event =
        std::make_shared<capture::CaptureOccurrence>(*row_part->occurrence);
    auto row_pixels = std::make_shared<capture::CaptureImmutableBlock>();
    row_pixels->bytes = { 255, 0, 0,   255, 255, 0, 0,   255,
                          0,   0, 255, 255, 0,   0, 255, 255 };
    row_event->inputs.textures[0].images[0].image.rgba = row_pixels;
    row_part->occurrence = row_event;
    gl_rows->parts[0] = row_part;
    auto row_frame = viewport.Render(gl_rows, {}, 128, 128);
    glBindTexture(GL_TEXTURE_2D, row_frame.texture);
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    g_assert_cmpuint(pixel(30, 75, 0), >, 200);
    g_assert_cmpuint(pixel(30, 55, 2), >, 200);
    row_part->has_uv = false;
    auto image = viewport.TextureImage(row_part, 1, 0);
    g_assert_cmpuint(image.texture, !=, 0);
    g_assert_cmpuint(image.height, ==, 2);
    auto invalid = Fixture(70);
    auto texture = DecodeAssetTexture(*invalid->parts[0], 1, 0, 4);
    g_assert_true(texture.rgba.empty());
    g_assert_true(texture.reason.find("budget") != std::string::npos);
    g_assert_cmpuint(viewport.Render(invalid, {}, 4096, 128).texture, ==, 0);
    AssetCamera bad;
    bad.zoom = std::numeric_limits<float>::quiet_NaN();
    g_assert_cmpuint(viewport.Render(invalid, bad, 128, 128).texture, ==, 0);
    for (uint64_t id = 2; id < 34; ++id)
        g_assert_cmpuint(viewport.Thumbnail(Fixture(id)).texture, !=, 0);
    g_assert_cmpuint(viewport.ThumbnailCount(), <=, 24);
    auto assembly = Fixture(50);
    viewport.Render(assembly, {}, 512, 512);
    auto start = std::chrono::steady_clock::now();
    for (int i = 0; i < 120; ++i) {
        AssetCamera camera;
        camera.yaw = float(i) * 0.01f;
        viewport.Render(assembly, camera, 512, 512);
    }
    glFinish();
    double seconds =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - start)
            .count();
    g_print("120 cached 512x512 frames: %.3f s, %.1f FPS (fixture, not PGR2)\n",
            seconds, 120 / seconds);
    g_assert_cmpuint(glGetError(), ==, GL_NO_ERROR);
    viewport.Shutdown();
    g_assert_cmpuint(viewport.ThumbnailCount(), ==, 0);
    glDeleteTextures(1, &sentinel);
    SDL_GL_DestroyContext(context);
    SDL_DestroyWindow(window);
    SDL_Quit();
    g_print("Asset viewport fixture passed\n");
    return 0;
}

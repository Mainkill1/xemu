// SPDX-License-Identifier: GPL-2.0-or-later
#include "../../ui/xui/asset-browser-viewport.hh"
#include "../../ui/xui/asset-browser-material.hh"
#include <SDL3/SDL.h>
#include "../../ui/xemu-gl-context.h"
#include <epoxy/gl.h>
#include <glib.h>
#include <chrono>
#include <cstring>
#include <cstdlib>
#include <vulkan/vulkan.h>
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
template <typename T>
static capture::SharedCaptureBlock StageBytes(const T *data, size_t count)
{
    auto block = std::make_shared<capture::CaptureImmutableBlock>();
    const auto *bytes = reinterpret_cast<const uint8_t *>(data);
    block->bytes.assign(bytes, bytes + sizeof(T) * count);
    return block;
}
static std::shared_ptr<AssetAssembly> StageFixture()
{
    auto assembly = Fixture(500);
    assembly->context.backend = 2;
    assembly->captured_placement = true;
    assembly->local_from_captured_clip = { 1, 0, 0, 0, 0, 1, 0, 0,
                                           0, 0, 1, 0, 0, 0, 0, 1 };
    for (size_t side = 0; side < 2; ++side) {
        auto p = std::make_shared<AssetPart>(*assembly->parts[side]);
        p->source_vertices = { 0, 1, 2, 3 };
        p->placement.valid = true;
        auto e = std::make_shared<capture::CaptureOccurrence>(*p->occurrence);
        e->inputs.complete = true;
        const std::string vs =
            "#version 450\nlayout(location=0) in vec4 v0;\n"
            "layout(binding=0,std140)uniform U {\nvec4 paint;\nvec4 "
            "shift;\n};\n"
            "layout(location=0)out vec4 vtxD0;\n"
            "void main(){gl_Position=v0+shift;vtxD0=paint;}\n";
        const std::string ps =
            "#version 450\nlayout(location=0)in vec4 vtxD0;\n"
            "layout(binding=3)uniform sampler2D texSamp0;\n"
            "layout(location=0)out vec4 fragColor;\n"
            "void main(){vec4 d=texture(texSamp0,vec2(.5));"
            "fragColor=(d.r>0.50001526 && all(equal(d.gba,vec3(0)))) ? "
            "vtxD0:vec4(1,0,0,1);gl_FragDepth=.8;}\n";
        e->inputs.sources[1] = StageBytes(vs.data(), vs.size());
        e->inputs.sources[2] = StageBytes(ps.data(), ps.size());
        std::vector<float> positions;
        for (const auto &v : p->vertices)
            positions.insert(positions.end(), v.position.begin(),
                             v.position.end());
        capture::CaptureOwnedBlob vertices;
        vertices.name = "vertex.attribute0";
        vertices.format = 106;
        vertices.components = 3;
        vertices.stride = 12;
        vertices.count = 4;
        vertices.data = StageBytes(positions.data(), positions.size());
        e->inputs.blobs.push_back(vertices);
        const std::array<float, 4> paint =
            side ? std::array<float, 4>{ 1, 0, 1, 1 } :
                   std::array<float, 4>{ 0, 1, 1, 1 };
        const std::array<float, 4> shift =
            side ? std::array<float, 4>{ -.7f, 0, -.25f, 0 } :
                   std::array<float, 4>{ 0, 0, 0, 0 };
        e->inputs.uniforms.push_back(
            { 1, 1, 4, 1, "paint", StageBytes(paint.data(), 4) });
        e->inputs.uniforms.push_back(
            { 1, 1, 4, 1, "shift", StageBytes(shift.data(), 4) });
        auto blob = [&](const char *name, const auto &value) {
            capture::CaptureOwnedBlob b;
            b.name = name;
            b.data = StageBytes(&value, 1);
            e->inputs.blobs.push_back(b);
        };
        VkPipelineInputAssemblyStateCreateInfo a{};
        a.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        a.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        blob("vk.pipeline.assembly", a);
        VkPipelineRasterizationStateCreateInfo r{};
        r.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        r.lineWidth = 1;
        blob("vk.pipeline.raster", r);
        VkPipelineMultisampleStateCreateInfo m{};
        m.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        m.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
        blob("vk.pipeline.multisample", m);
        VkPipelineDepthStencilStateCreateInfo d{};
        d.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        d.depthTestEnable = d.depthWriteEnable = VK_TRUE;
        d.depthCompareOp = VK_COMPARE_OP_LESS;
        blob("vk.pipeline.depth_stencil", d);
        VkPipelineColorBlendStateCreateInfo b{};
        b.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        b.attachmentCount = 1;
        blob("vk.pipeline.blend", b);
        VkPipelineColorBlendAttachmentState att{};
        att.colorWriteMask = 15;
        att.srcColorBlendFactor = att.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        blob("vk.pipeline.blend_attachment", att);
        VkViewport viewport{ 0, 0, 640, 480, 0, 1 };
        blob("vk.viewport", viewport);
        VkRect2D scissor{ { 0, 0 }, { 640, 480 } };
        blob("vk.scissor", scissor);
        e->inputs.registers.push_back({ "capture.vk.pipeline_abi", 1 });
        e->inputs.registers.push_back({ "capture.vk.dynamic_line_width", 0 });
        e->inputs.registers.push_back(
            { "capture.vk.dynamic_blend_constant_mask", 0 });
        const uint16_t depth[] = { 0x8001, 0x8001, 0x8001, 0x8001 };
        capture::CaptureOwnedBlob storage;
        storage.name = "texture.storage.0";
        storage.slot = 0;
        storage.format = 70;
        storage.components = 1;
        storage.stride = 2;
        storage.count = 4;
        storage.normalized = 1;
        storage.data = StageBytes(depth, 4);
        e->inputs.blobs.push_back(storage);
        auto &meta = e->inputs.textures[0].metadata;
        meta.host_format = 70;
        meta.mip_levels = 1;
        meta.min_filter = meta.mag_filter = 0;
        meta.wrap_s = meta.wrap_t = meta.wrap_r = 2;
        for (int c = 1; c < 4; ++c)
            e->inputs.registers.push_back(
                { "capture.texture0.swizzle" + std::to_string(c), 0 });
        p->occurrence = e;
        assembly->parts[side] = p;
    }
    return assembly;
}
static std::shared_ptr<AssetAssembly> AnimatedCarFixture(float angle)
{
    auto car = StageFixture();
    const std::array<std::array<float, 4>, 4> paints{
        { { 0, 1, 1, 1 }, { 1, 0, 1, 1 }, { 1, 1, 0, 1 }, { 1, 0, 0, .5f } }
    };
    const std::array<std::array<float, 4>, 4> shifts{ { { 0, 0, 0, 0 },
                                                        { -.7f, -.35f, -.2f,
                                                          0 },
                                                        { .7f, -.35f, -.2f, 0 },
                                                        { 0, .2f, -.3f, 0 } } };
    car->parts.resize(4);
    const auto seed = car->parts[0];
    for (size_t i = 0; i < 4; ++i) {
        auto part = std::make_shared<AssetPart>(*seed);
        part->id = 700 + i;
        auto e =
            std::make_shared<capture::CaptureOccurrence>(*seed->occurrence);
        e->summary.key.submission = i + 1;
        const std::string vs =
            "#version 450\nlayout(location=0) in vec4 v0;\n"
            "layout(binding=0,std140)uniform U {\nvec4 paint;\nvec4 "
            "shift;\nfloat angle;\n};\n"
            "layout(location=0)out vec4 vtxD0;\nvoid main(){"
            "float "
            "c=cos(angle),s=sin(angle);gl_Position=vec4(c*v0.x-s*v0.y,s*v0.x+c*"
            "v0.y,v0.z,v0.w)+shift;vtxD0=paint;}\n";
        e->inputs.sources[1] = StageBytes(vs.data(), vs.size());
        auto &vertices = e->inputs.blobs[0];
        const float w = i == 0 ? 1.f :
                        i == 3 ? .4f :
                                 .3f,
                    h = i == 0 ? .5f :
                        i == 3 ? .2f :
                                 .06f;
        const float points[] = { -w, -h, 0, w, -h, 0, w, h, 0, -w, h, 0 };
        vertices.data = StageBytes(points, 12);
        e->inputs.uniforms[0].data = StageBytes(paints[i].data(), 4);
        e->inputs.uniforms[1].data = StageBytes(shifts[i].data(), 4);
        const float rotation = (i == 1 || i == 2) ? angle : 0;
        e->inputs.uniforms.push_back(
            { 1, 1, 1, 1, "angle", StageBytes(&rotation, 1) });
        if (i == 3) {
            for (auto &blob : e->inputs.blobs) {
                if (blob.name == "vk.pipeline.blend_attachment") {
                    VkPipelineColorBlendAttachmentState a{};
                    a.blendEnable = VK_TRUE;
                    a.colorWriteMask = 15;
                    a.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
                    a.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
                    a.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
                    a.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
                    blob.data = StageBytes(&a, 1);
                }
            }
        }
        part->occurrence = e;
        car->parts[i] = part;
    }
    return car;
}
static std::shared_ptr<AssetAssembly> SamplerFixture(bool border,
                                                     bool oversized = false)
{
    auto assembly = StageFixture();
    assembly->parts.resize(1);
    auto p = std::make_shared<AssetPart>(*assembly->parts[0]);
    auto e = std::make_shared<capture::CaptureOccurrence>(*p->occurrence);
    const std::string ps =
        oversized ? "#version 450\nlayout(binding=0)uniform sampler2D "
                    "texSamp0;\nlayout(binding=1)uniform sampler2D "
                    "texSamp1;\nlayout(location=0)out vec4 fragColor;\nvoid "
                    "main(){fragColor=texture(texSamp0,vec2(.5))+texture("
                    "texSamp1,vec2(.5));}\n" :
                    "#version 450\nlayout(binding=0)uniform sampler2D "
                    "texSamp0;\nlayout(location=0)out vec4 fragColor;\nvoid "
                    "main(){fragColor=texture(texSamp0,vec2(" +
                        std::string(border ? "2" : ".5") + "));}\n";
    e->inputs.sources[2] = StageBytes(ps.data(), ps.size());
    auto set = [&](const std::string &name, uint32_t value) {
        for (auto &r : e->inputs.registers)
            if (r.name == name) {
                r.value = value;
                return;
            }
        e->inputs.registers.push_back({ name, value });
    };
    auto real = [&](const std::string &name, float f) {
        uint32_t bits;
        std::memcpy(&bits, &f, 4);
        set(name, bits);
    };
    const int slots = oversized ? 2 : 1;
    for (int slot = 0; slot < slots; ++slot) {
        auto &t = e->inputs.textures[slot];
        t = e->inputs.textures[0];
        t.metadata.slot = slot;
        t.metadata.host_format = 37;
        t.metadata.mip_levels = oversized ? 1 : 2;
        t.metadata.width = t.metadata.height = oversized ? 1024 : 2;
        t.metadata.wrap_s = t.metadata.wrap_t = t.metadata.wrap_r =
            border ? 3 : 2;
        t.images.clear();
        for (uint32_t mip = 0; mip < t.metadata.mip_levels; ++mip) {
            auto b = std::make_shared<capture::CaptureImmutableBlock>();
            const uint32_t w = std::max(1U, t.metadata.width >> mip);
            b->bytes.resize(size_t(w) * w * 4);
            for (size_t i = 0; i < b->bytes.size(); i += 4) {
                b->bytes[i + (mip + slot) % 3] = 255;
                b->bytes[i + 3] = 255;
            }
            t.images.push_back({ mip, 0, { w, w, b } });
        }
        if (!oversized) {
            const std::string pre =
                "capture.texture" + std::to_string(slot) + ".";
            set(pre + "sampler_ready", 1);
            set(pre + "mipmap_mode", 0);
            real(pre + "min_lod_bits", 1);
            real(pre + "max_lod_bits", 1);
            real(pre + "lod_bias_bits", 0);
            real(pre + "anisotropy_bits", 1);
            for (int c = 0; c < 4; ++c)
                real(pre + "border" + std::to_string(c) + "_bits",
                     float(c + 1) * .25f);
        }
    }
    p->occurrence = e;
    assembly->parts[0] = p;
    return assembly;
}
static std::shared_ptr<AssetAssembly> DepthClipFixture()
{
    auto assembly = StageFixture();
    for (size_t i = 0; i < assembly->parts.size(); ++i) {
        auto p = std::make_shared<AssetPart>(*assembly->parts[i]);
        auto e = std::make_shared<capture::CaptureOccurrence>(*p->occurrence);
        const std::string vs =
            "#version 450\nlayout(location=0)in vec4 v0;uniform vec4 shift;"
            "layout(location=9)flat out vec4 vtxPos0;"
            "layout(location=10)flat out vec4 vtxPos1;"
            "layout(location=11)flat out vec4 vtxPos2;"
            "void "
            "main(){gl_Position=v0+shift;vtxPos0=vtxPos1=vtxPos2=gl_Position;}";
        const std::string gs =
            "#version "
            "450\nlayout(triangles)in;layout(triangle_strip,max_vertices=3)out;"
            "layout(location=9)flat in vec4 p0[];"
            "layout(location=9)flat out vec4 vtxPos0;"
            "layout(location=10)flat out vec4 vtxPos1;"
            "layout(location=11)flat out vec4 vtxPos2;"
            "void main(){for(int i=0;i<3;++i){gl_Position=gl_in[i].gl_Position;"
            "vtxPos0=p0[0];vtxPos1=p0[1];vtxPos2=p0[2];EmitVertex();}"
            "EndPrimitive();}";
        // The generated non-perspective depth bookkeeping and clipping path.
        const std::string ps =
            "#version 450\nlayout(location=9)flat in vec4 vtxPos0;"
            "layout(location=10)flat in vec4 vtxPos1;"
            "layout(location=11)flat in vec4 vtxPos2;"
            "uniform vec4 clipRange;uniform ivec2 surfaceScale;uniform vec4 "
            "paint;"
            "layout(location=0)out vec4 fragColor;"
            "float area(vec2 a,vec2 b,vec2 c){vec2 p=b-a,q=c-a;return "
            "p.x*q.y-p.y*q.x;}"
            "void main(){vec2 unscaled_xy=gl_FragCoord.xy/surfaceScale;"
            "precise float bc0=area(unscaled_xy,vtxPos1.xy,vtxPos2.xy);"
            "precise float bc1=area(unscaled_xy,vtxPos2.xy,vtxPos0.xy);"
            "precise float bc2=area(unscaled_xy,vtxPos0.xy,vtxPos1.xy);"
            "float inv_bcsum=1.0/(bc0+bc1+bc2);"
            "if(isinf(inv_bcsum)){inv_bcsum=0.0;}bc1*=inv_bcsum;bc2*=inv_bcsum;"
            "precise float "
            "zvalue=vtxPos0.z+bc1*(vtxPos1.z-vtxPos0.z)+bc2*(vtxPos2.z-vtxPos0."
            "z);"
            "if(zvalue<clipRange.z||clipRange.w<zvalue){discard;}"
            "fragColor=paint;gl_FragDepth=zvalue/clipRange.y;}";
        e->inputs.sources[1] = StageBytes(vs.data(), vs.size());
        e->inputs.sources[2] = StageBytes(ps.data(), ps.size());
        e->inputs.sources[3] = StageBytes(gs.data(), gs.size());
        for (auto &u : e->inputs.uniforms)
            if (u.name == "paint")
                u.stage = 2;
        const float clip[] = { 0, 65535, 0, 65535 };
        const int32_t scale[] = { 1, 1 };
        e->inputs.uniforms.push_back(
            { 2, 1, 4, 1, "clipRange", StageBytes(clip, 4) });
        e->inputs.uniforms.push_back(
            { 2, 2, 2, 1, "surfaceScale", StageBytes(scale, 2) });
        p->occurrence = e;
        assembly->parts[i] = p;
    }
    return assembly;
}
int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, nullptr);
    g_assert_true(SDL_Init(SDL_INIT_VIDEO));
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,
                        SDL_GL_CONTEXT_PROFILE_CORE);
    auto *window = SDL_CreateWindow("Asset viewport fixture", 128, 128,
                                    SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
    g_assert_nonnull(window);
    auto context = xemu_create_hud_gl_context(window);
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
    // Empty pixels must show contrasting purple tiles, without tinting the
    // captured material. This also catches a background that covers the mesh.
    g_assert_cmpuint(pixel(4, 4, 2), >, pixel(4, 4, 1) + 30);
    g_assert_cmpuint(pixel(36, 4, 2), >, pixel(4, 4, 2) + 60);
    g_assert_cmpuint(pixel(68, 4, 2), ==, pixel(4, 4, 2));
    auto connected = Fixture(92);
    connected->captured_placement = true;
    AssetMatrix identity{ 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
    auto relative = identity;
    relative[3] = -.7f;
    relative[11] = .2f;
    connected->anchor_from_local = { identity, relative };
    auto connected_frame = viewport.Render(connected, {}, 128, 128);
    glBindTexture(GL_TEXTURE_2D, connected_frame.texture);
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    g_assert_cmpuint(pixel(45, 64, 1), >,
                     200); // Related placement also in raw diagnostics.
    auto thumbnail = viewport.Thumbnail(Fixture(91));
    glBindTexture(GL_TEXTURE_2D, thumbnail.texture);
    std::vector<uint8_t> thumbnail_pixels(thumbnail.width * thumbnail.height *
                                          4);
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                  thumbnail_pixels.data());
    g_assert_cmpuint(thumbnail_pixels[(4 * thumbnail.width + 36) * 4 + 2], >,
                     thumbnail_pixels[(4 * thumbnail.width + 4) * 4 + 2] + 60);
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
    g_assert_false(viewport.Configure(0, 4));
    g_assert_true(viewport.Configure(16U * 1024U * 1024U, 4));
    for (uint64_t id = 34; id < 42; ++id)
        g_assert_cmpuint(viewport.Thumbnail(Fixture(id)).texture, !=, 0);
    g_assert_cmpuint(viewport.ThumbnailCapacity(), ==, 4);
    g_assert_cmpuint(viewport.ThumbnailCount(), <=, 4);
    auto assembly = Fixture(50);
    viewport.Render(assembly, {}, 512, 512);
    auto start = std::chrono::steady_clock::now();
    for (int i = 0; i < 120; ++i) {
        AssetCamera camera;
        camera.yaw = float(i) * 0.01f;
        viewport.Render(assembly, camera, 512, 512);
    }
    glFinish();
    const double seconds =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - start)
            .count();
    const char *review_environment = std::getenv("ASSET_REVIEW_CASE");
    const std::string review_case =
        review_environment ? review_environment : "all";
    const auto run_review = [&](const char *name) {
        return review_case == "all" || review_case == name;
    };
    const auto original = StageFixture();
    const auto shaded =
        viewport.Render(original, {}, 128, 128, -1, false, true);
    g_assert_cmpuint(shaded.captured_parts, ==, 2);
    glBindTexture(GL_TEXTURE_2D, shaded.texture);
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    g_assert_cmpuint(pixel(25, 64, 1), >,
                     200); // Captured paint, not diagnostic color.
    g_assert_cmpuint(pixel(25, 64, 2), >,
                     200); // Full-precision R16 sample succeeded.
    g_assert_cmpuint(pixel(45, 64, 0), >,
                     200); // Transformed nearer part occludes body.
    g_assert_cmpuint(pixel(45, 64, 1), <, 30);
    g_assert_cmpuint(pixel(45, 64, 2), >, 200);
    if (run_review("palette")) {
        auto mapped = StageFixture();
        std::array<uint8_t, 768> palette;
        for (size_t i = 0; i < 256; ++i) {
            palette[i * 3] = uint8_t(i);
            palette[i * 3 + 1] = uint8_t(i);
            palette[i * 3 + 2] = uint8_t(255 - i);
        }
        palette[51 * 3] = 11;
        palette[153 * 3] = 92;
        palette[102 * 3] = 201;
        const std::string ps = "#version 450\nlayout(location=0) in vec4 vtxD0;"
                               "layout(location=0) out vec4 fragColor;"
                               "void main(){fragColor=vtxD0;}";
        for (size_t i = 0; i < 2; ++i) {
            auto p = std::make_shared<AssetPart>(*mapped->parts[i]);
            auto e =
                std::make_shared<capture::CaptureOccurrence>(*p->occurrence);
            e->inputs.sources[2] = StageBytes(ps.data(), ps.size());
            const float paint[2][4] = { { .2f, .4f, .6f, 1 },
                                        { .6f, .2f, 0, .5f } };
            for (auto &u : e->inputs.uniforms)
                if (u.name == "paint")
                    u.data = StageBytes(paint[i], 4);
            for (auto &blob : e->inputs.blobs)
                if (i && blob.name == "vk.pipeline.blend_attachment") {
                    VkPipelineColorBlendAttachmentState b;
                    std::memcpy(&b, blob.data->bytes.data(), sizeof(b));
                    b.blendEnable = VK_TRUE;
                    b.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
                    b.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
                    blob.data = StageBytes(&b, 1);
                }
            capture::CaptureOwnedBlob dac;
            dac.name = "display.dac_palette";
            dac.data = StageBytes(palette.data(), palette.size());
            e->inputs.blobs.push_back(dac);
            p->occurrence = e;
            mapped->parts[i] = p;
        }
        const auto f = viewport.Render(mapped, {}, 128, 128, -1, false, true);
        g_assert_cmpuint(f.texture, !=, 0);
        glBindTexture(GL_TEXTURE_2D, f.texture);
        glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                      pixels.data());
        // Blend in the renderer's color space, then apply the owned display LUT
        // exactly once. Mapping each part before blending would yield R=52.
        g_assert_cmpuint(pixel(45, 64, 0), ==, 201);
        g_assert_cmpuint(pixel(45, 64, 1), >=, 75);
        g_assert_cmpuint(pixel(45, 64, 1), <=, 77);
        g_assert_cmpuint(pixel(45, 64, 2), >=, 178);
        g_assert_cmpuint(pixel(45, 64, 2), <=, 180);
        auto invalid = std::make_shared<AssetAssembly>(*mapped);
        auto p = std::make_shared<AssetPart>(*mapped->parts[1]);
        auto e = std::make_shared<capture::CaptureOccurrence>(*p->occurrence);
        const auto good_palette = e->inputs.blobs.back();
        // Missing members, duplicate entries and short storage never masquerade
        // as a complete display transform.
        e->inputs.blobs.pop_back();
        p->occurrence = e;
        invalid->parts[1] = p;
        g_assert_cmpuint(
            viewport.Render(invalid, {}, 128, 128, -1, false, true).texture, ==,
            0);
        e->inputs.blobs.push_back(good_palette);
        e->inputs.blobs.push_back(good_palette);
        g_assert_cmpuint(
            viewport.Render(invalid, {}, 128, 128, -1, false, true).texture, ==,
            0);
        e->inputs.blobs.pop_back();
        e->inputs.blobs.back().data = StageBytes(palette.data(), 767);
        g_assert_cmpuint(
            viewport.Render(invalid, {}, 128, 128, -1, false, true).texture, ==,
            0);
        palette[0] = 3;
        e->inputs.blobs.back().data =
            StageBytes(palette.data(), palette.size());
        g_assert_cmpuint(
            viewport.Render(invalid, {}, 128, 128, -1, false, true).texture, ==,
            0);
    }
    if (run_review("depth")) {
        auto clipped =
            viewport.Render(DepthClipFixture(), {}, 128, 128, -1, false, true);
        g_assert_cmpuint(clipped.captured_parts, ==, 2);
        glBindTexture(GL_TEXTURE_2D, clipped.texture);
        glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                      pixels.data());
        g_assert_cmpuint(pixel(25, 64, 1), >, 200);
        g_assert_cmpuint(pixel(45, 64, 0), >, 200);
        g_assert_cmpuint(pixel(45, 64, 1), <, 30);
    }
    if (run_review("duplicate")) {
        for (int variant = 0; variant < 3; ++variant) {
            auto bad = SamplerFixture(false);
            auto p = std::make_shared<AssetPart>(*bad->parts[0]);
            auto e =
                std::make_shared<capture::CaptureOccurrence>(*p->occurrence);
            auto duplicate = e->inputs.textures[0].images[0];
            if (variant == 2) {
                const uint8_t undersized[] = { 0, 1, 2 };
                duplicate.image.rgba = StageBytes(undersized, 3);
            } else
                duplicate.image.rgba.reset();
            if (variant == 0)
                e->inputs.textures[0].images.push_back(duplicate);
            else
                e->inputs.textures[0].images[1] = duplicate;
            p->occurrence = e;
            bad->parts[0] = p;
            auto rejected = viewport.Render(bad, {}, 128, 128, -1, false, true);
            g_assert_cmpuint(rejected.texture, ==, 0);
        }
    }
    if (run_review("count")) {
        auto bad = StageFixture();
        auto p = std::make_shared<AssetPart>(*bad->parts[0]);
        auto e = std::make_shared<capture::CaptureOccurrence>(*p->occurrence);
        for (auto &b : e->inputs.blobs)
            if (b.name == "vertex.attribute0")
                b.count = 3;
        p->occurrence = e;
        bad->parts[0] = p;
        auto rejected = viewport.Render(bad, {}, 128, 128, -1, false, true);
        g_assert_cmpuint(rejected.texture, ==, 0);
    }
    auto parked = AnimatedCarFixture(0);
    auto parked_frame = viewport.Render(parked, {}, 128, 128, -1, false, true);
    g_assert_cmpuint(parked_frame.captured_parts, ==, 4);
    glBindTexture(GL_TEXTURE_2D, parked_frame.texture);
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    g_assert_cmpuint(pixel(40, 48, 0), >, 200); // Horizontal left wheel.
    g_assert_cmpuint(pixel(40, 48, 1), <, 30);
    g_assert_cmpuint(pixel(96, 48, 0), >, 200); // Independent yellow wheel.
    g_assert_cmpuint(pixel(96, 48, 1), >, 200);
    g_assert_cmpuint(pixel(64, 73, 0), >, 100); // Glass blends over cyan body.
    g_assert_cmpuint(pixel(64, 73, 1), >, 100);
    g_assert_cmpuint(pixel(64, 73, 0), <, 150);
    auto turning = AnimatedCarFixture(1.57079632679f);
    glActiveTexture(GL_TEXTURE3);
    glBindTexture(GL_TEXTURE_2D, sentinel);
    glEnable(GL_BLEND);
    glBlendFuncSeparate(GL_ONE, GL_ONE, GL_ZERO, GL_ONE);
    glEnable(GL_STENCIL_TEST);
    glStencilMaskSeparate(GL_FRONT, 0x55);
    glStencilMaskSeparate(GL_BACK, 0xaa);
    glEnable(GL_CULL_FACE);
    glFrontFace(GL_CW);
    glCullFace(GL_FRONT);
    auto moving_frame = viewport.Render(turning, {}, 128, 128, -1, false, true);
    g_assert_cmpuint(moving_frame.captured_parts, ==, 4);
    glGetIntegerv(GL_ACTIVE_TEXTURE, &actual);
    g_assert_cmpint(actual, ==, GL_TEXTURE3);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &actual);
    g_assert_cmpuint(actual, ==, sentinel);
    glGetIntegerv(GL_BLEND_SRC_RGB, &actual);
    g_assert_cmpint(actual, ==, GL_ONE);
    glGetIntegerv(GL_STENCIL_WRITEMASK, &actual);
    g_assert_cmpint(actual, ==, 0x55);
    glGetIntegerv(GL_STENCIL_BACK_WRITEMASK, &actual);
    g_assert_cmpint(actual, ==, 0xaa);
    glGetIntegerv(GL_FRONT_FACE, &actual);
    g_assert_cmpint(actual, ==, GL_CW);
    glGetIntegerv(GL_CULL_FACE_MODE, &actual);
    g_assert_cmpint(actual, ==, GL_FRONT);
    g_assert_true(glIsEnabled(GL_CULL_FACE));
    g_assert_true(glIsEnabled(GL_STENCIL_TEST));
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, moving_frame.texture);
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    g_assert_cmpuint(pixel(40, 48, 0), <,
                     30); // Wheel turned; same fixed body/camera.
    g_assert_cmpuint(pixel(40, 48, 1), >, 200);
    g_assert_cmpuint(pixel(32, 56, 0), >, 200); // Now vertical wheel.
    g_assert_cmpuint(pixel(32, 56, 1), <, 30);
    auto lod =
        viewport.Render(SamplerFixture(false), {}, 128, 128, -1, false, true);
    g_assert_cmpuint(lod.captured_parts, ==, 1);
    glBindTexture(GL_TEXTURE_2D, lod.texture);
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    g_assert_cmpuint(pixel(25, 64, 1), >, 200);
    g_assert_cmpuint(pixel(25, 64, 0), <, 30); // mip1, not base red.
    auto border =
        viewport.Render(SamplerFixture(true), {}, 128, 128, -1, false, true);
    g_assert_cmpuint(border.captured_parts, ==, 1);
    glBindTexture(GL_TEXTURE_2D, border.texture);
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    g_assert_cmpuint(pixel(25, 64, 0), >, 60);
    g_assert_cmpuint(pixel(25, 64, 0), <, 70);
    g_assert_cmpuint(pixel(25, 64, 1), >, 120);
    g_assert_cmpuint(pixel(25, 64, 2), >, 180);
    auto gl_sampler = SamplerFixture(false);
    gl_sampler->context.backend = 1;
    auto gl_part = std::make_shared<AssetPart>(*gl_sampler->parts[0]);
    auto gl_event =
        std::make_shared<capture::CaptureOccurrence>(*gl_part->occurrence);
    for (auto &u : gl_event->inputs.uniforms)
        u.stage = 0;
    for (auto &blob : gl_event->inputs.blobs)
        if (blob.name == "vertex.attribute0")
            blob.format = GL_FLOAT;
    auto &gl_meta = gl_event->inputs.textures[0].metadata;
    gl_meta.min_filter = GL_NEAREST_MIPMAP_NEAREST;
    gl_meta.mag_filter = GL_NEAREST;
    gl_meta.wrap_s = gl_meta.wrap_t = gl_meta.wrap_r = GL_CLAMP_TO_EDGE;
    gl_meta.host_format = GL_RGBA8;
    auto reg = [&](const char *name, uint32_t value) {
        gl_event->inputs.registers.push_back({ name, value });
    };
    reg("host.depth_enabled", 1);
    reg("host.depth_write", 1);
    reg("host.depth_func", GL_LESS);
    reg("host.blend_enabled", 0);
    reg("host.blend_src_rgb", GL_ONE);
    reg("host.blend_dst_rgb", GL_ZERO);
    reg("host.blend_src_alpha", GL_ONE);
    reg("host.blend_dst_alpha", GL_ZERO);
    reg("host.blend_equation_rgb", GL_FUNC_ADD);
    reg("host.blend_equation_alpha", GL_FUNC_ADD);
    reg("host.cull_enabled", 0);
    reg("host.cull_face", GL_BACK);
    reg("host.front_face", GL_CCW);
    auto blob = [&](const char *name, const auto &value) {
        capture::CaptureOwnedBlob b;
        b.name = name;
        b.data = StageBytes(value.data(), value.size());
        gl_event->inputs.blobs.push_back(b);
    };
    blob("host.depth_range", std::array<double, 2>{ 0, 1 });
    blob("host.blend_color", std::array<float, 4>{ 0, 0, 0, 0 });
    blob("host.color_write", std::array<uint8_t, 4>{ 1, 1, 1, 1 });
    for (auto &r : gl_event->inputs.registers) {
        if (r.name == "capture.texture0.swizzle1")
            r.value = GL_GREEN;
        if (r.name == "capture.texture0.swizzle2")
            r.value = GL_BLUE;
        if (r.name == "capture.texture0.swizzle3")
            r.value = GL_ALPHA;
        if (r.name == "capture.texture0.sampler_ready")
            r.value = 0; // GL observer predates the Vulkan marker.
    }
    reg("capture.texture0.base_level", 0);
    reg("capture.texture0.max_level", 1);
    reg("capture.texture0.compare_mode", GL_NONE);
    gl_part->occurrence = gl_event;
    gl_sampler->parts[0] = gl_part;
    auto gl_lod = viewport.Render(gl_sampler, {}, 128, 128, -1, false, true);
    g_assert_cmpuint(gl_lod.captured_parts, ==, 1);
    glBindTexture(GL_TEXTURE_2D, gl_lod.texture);
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    g_assert_cmpuint(pixel(25, 64, 1), >, 200);
    g_assert_cmpuint(pixel(25, 64, 0), <, 30);
    if (run_review("max-level")) {
        gl_meta.mip_levels = 1;
        gl_meta.min_filter = GL_NEAREST;
        gl_event->inputs.textures[0].images.resize(1);
        for (auto &r : gl_event->inputs.registers) {
            if (r.name == "capture.texture0.max_level")
                r.value = 1000;
            if (r.name == "capture.texture0.min_lod_bits")
                r.value = 0;
            if (r.name == "capture.texture0.max_lod_bits") {
                const float limit = 1000.f;
                std::memcpy(&r.value, &limit, 4);
            }
        }
        auto linear =
            viewport.Render(gl_sampler, {}, 128, 128, -1, false, true);
        g_assert_cmpuint(linear.captured_parts, ==, 1);
        glBindTexture(GL_TEXTURE_2D, linear.texture);
        glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                      pixels.data());
        g_assert_cmpuint(pixel(25, 64, 0), >, 200);
        g_assert_cmpuint(pixel(25, 64, 1), <, 30);
    }
    auto over = viewport.Render(SamplerFixture(false, true), {}, 128, 128, -1,
                                false, true);
    g_assert_cmpuint(over.texture, ==,
                     0); // Both live bindings must fit together.
    g_assert_true(over.message.find("budget") != std::string::npos);
    const auto cache_bytes = moving_frame.gpu_bytes;
    const auto captured_start = std::chrono::steady_clock::now();
    for (int i = 0; i < 120; ++i)
        g_assert_cmpuint(viewport.Render(turning, {}, 512, 512, -1, false, true)
                             .captured_parts,
                         ==, 4);
    glFinish();
    const double captured_seconds =
        std::chrono::duration<double>(std::chrono::steady_clock::now() -
                                      captured_start)
            .count();
    g_assert_cmpuint(
        viewport.Render(turning, {}, 512, 512, -1, false, true).gpu_bytes, <=,
        cache_bytes + 512 * 512 * 8);
    g_print("120 captured-stage four-part fixture frames: %.3f s, %.1f FPS "
            "(not PGR2)\n",
            captured_seconds, 120 / captured_seconds);
    glFinish();
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

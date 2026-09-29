// SPDX-License-Identifier: GPL-2.0-or-later
#include "asset-browser-stage-renderer.hh"
#include "asset-browser-stage-source.hh"
#include "asset-browser-decode.hh"
#include "shader-browser-preview-gl-inputs.hh"
#include <algorithm>
#include <cstring>
#include <list>
#include <set>
#include <cmath>
namespace xemu::asset_browser {
namespace {
namespace preview = xemu::shader_browser;
using Key = preview::PreviewDigest;
struct KeyBytes {
    std::vector<uint8_t> bytes;
    template <typename T> void Add(const T &value)
    {
        const auto *p = reinterpret_cast<const uint8_t *>(&value);
        bytes.insert(bytes.end(), p, p + sizeof(value));
    }
    void Block(const capture::SharedCaptureBlock &block)
    {
        if (!block) {
            Add(uint64_t(0));
            return;
        }
        Add(uint64_t(block->bytes.size()));
        if (std::any_of(block->digest.begin(), block->digest.end(),
                        [](uint8_t b) { return b != 0; }))
            Add(block->digest);
        else
            Add(preview::ComputePreviewDigest(block->bytes.data(),
                                              block->bytes.size()));
    }
    Key Digest() const
    {
        return preview::ComputePreviewDigest(bytes.data(), bytes.size());
    }
};
const capture::CaptureOwnedBlob *Blob(const AssetPart &part,
                                      const std::string &name)
{
    for (const auto &b : part.occurrence->inputs.blobs)
        if (b.name == name)
            return &b;
    return nullptr;
}
uint32_t Reg(const AssetPart &part, const std::string &name,
             uint32_t fallback = 0)
{
    for (const auto &r : part.occurrence->inputs.registers)
        if (r.name == name)
            return r.value;
    return fallback;
}
bool HasReg(const AssetPart &part, const std::string &name)
{
    const auto &registers = part.occurrence->inputs.registers;
    return std::any_of(registers.begin(), registers.end(),
                       [&](const auto &r) { return r.name == name; });
}
GLuint Compile(GLenum type, const std::string &text, std::string *error)
{
    GLuint shader = glCreateShader(type);
    const auto *p = text.c_str();
    glShaderSource(shader, 1, &p, nullptr);
    glCompileShader(shader);
    GLint good = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &good);
    if (!good) {
        char log[4096]{};
        glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
        *error = log;
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}
uint32_t UniformStage(const std::string &name)
{
    return name.size() > 7 && name.rfind("asset", 0) == 0 && name[5] >= '1' &&
                   name[5] <= '3' && name[6] == '_' ?
               name[5] - '0' :
               0;
}
std::string UniformBase(std::string name)
{
    const auto suffix = name.find('[');
    return name.substr(0, suffix);
}
GLenum ExpectedUniform(uint32_t type, uint32_t components)
{
    preview::OwnedDrawUniform u;
    u.type = type;
    u.components = components;
    return preview::CapturedGlUniformType(u);
}
} // namespace
struct AssetStageRenderer::Impl {
    struct Attribute {
        GLint location = 0;
        bool integer = false, unsigned_values = false;
    };
    struct Uniform {
        std::string name;
        uint32_t stage = 0;
        GLenum type = 0;
        uint32_t count = 0;
        GLint location = 0;
    };
    struct Program {
        Key key{};
        GLuint id = 0;
        std::string error;
        std::vector<Attribute> attributes;
        std::vector<Uniform> uniforms;
        bool exclusive_clip = false;
        GLenum feedback_mode = GL_POINTS;
        uint32_t feedback_vertices_per_input = 1;
        uint32_t feedback_vertices_per_primitive = 1;
    };
    struct Mesh {
        Key key{};
        GLuint vao = 0, vbo = 0, ebo = 0;
        uint64_t bytes = 0;
    };
    struct Texture {
        Key key{};
        GLuint id = 0;
        GLenum target = 0;
        uint64_t bytes = 0;
    };
    std::list<Program> programs;
    std::list<Mesh> meshes;
    std::list<Texture> textures;
    struct Bounds {
        Key key{};
        capture::Bounds3 bounds;
        std::string error;
    };
    std::list<Bounds> output_bounds;
    std::vector<GLuint> draw_textures;
    uint64_t mesh_bytes = 0, texture_bytes = 0;
    uint64_t budget = 64U * 1024U * 1024U;
    void Delete(Mesh &m)
    {
        glDeleteVertexArrays(1, &m.vao);
        glDeleteBuffers(1, &m.vbo);
        glDeleteBuffers(1, &m.ebo);
        mesh_bytes -= m.bytes;
    }
    void Delete(Texture &t)
    {
        glDeleteTextures(1, &t.id);
        texture_bytes -= t.bytes;
    }
    Program *GetProgram(const AssetPart &part, uint32_t backend,
                        std::string *error)
    {
        KeyBytes bytes;
        bytes.Add(backend);
        bytes.Add(uint32_t(1));
        const auto &sources = part.occurrence->inputs.sources;
        for (size_t stage = 1; stage < 4; ++stage)
            bytes.Block(sources[stage]);
        const auto key = bytes.Digest();
        for (auto it = programs.begin(); it != programs.end(); ++it)
            if (it->key == key) {
                programs.splice(programs.end(), programs, it);
                auto &p = programs.back();
                *error = p.error;
                return p.id ? &p : nullptr;
            }
        if (programs.size() == 64) {
            glDeleteProgram(programs.front().id);
            programs.pop_front();
        }
        Program program;
        program.key = key;
        GLuint shaders[3]{};
        const GLenum types[] = { GL_VERTEX_SHADER, GL_FRAGMENT_SHADER,
                                 GL_GEOMETRY_SHADER };
        for (size_t i = 0; i < 3; ++i) {
            const auto &source = sources[i + 1];
            if (!source && i == 2)
                continue;
            if (!source) {
                program.error = "Missing captured VS/PS source";
                break;
            }
            const std::string original(source->bytes.begin(),
                                       source->bytes.end());
            if (i == 1)
                program.exclusive_clip =
                    original.find("Window-clip (Exclusive)") !=
                    std::string::npos;
            const auto adapted =
                BuildAssetStageSource(original, i + 1, !sources[3]);
            if (adapted.text.empty()) {
                program.error = adapted.error;
                break;
            }
            shaders[i] = Compile(types[i], adapted.text, &program.error);
            if (!shaders[i])
                break;
        }
        if (program.error.empty()) {
            program.id = glCreateProgram();
            for (GLuint s : shaders)
                if (s)
                    glAttachShader(program.id, s);
            const char *varying = "gl_Position";
            glTransformFeedbackVaryings(program.id, 1, &varying,
                                        GL_INTERLEAVED_ATTRIBS);
            glLinkProgram(program.id);
            GLint linked = 0;
            glGetProgramiv(program.id, GL_LINK_STATUS, &linked);
            if (!linked) {
                char log[4096]{};
                glGetProgramInfoLog(program.id, sizeof(log), nullptr, log);
                program.error = log;
            }
        }
        for (GLuint s : shaders)
            if (s)
                glDeleteShader(s);
        if (program.error.empty()) {
            if (sources[3]) {
                GLint input = 0;
                glGetProgramiv(program.id, GL_GEOMETRY_INPUT_TYPE, &input);
                if (input != GL_TRIANGLES)
                    program.error = "Host geometry stage requires an "
                                    "unsupported input topology";
                GLint output = 0, maximum = 0, invocations = 0;
                glGetProgramiv(program.id, GL_GEOMETRY_OUTPUT_TYPE, &output);
                glGetProgramiv(program.id, GL_GEOMETRY_VERTICES_OUT, &maximum);
                glGetProgramiv(program.id, GL_GEOMETRY_SHADER_INVOCATIONS,
                               &invocations);
                if (maximum <= 0 || maximum > 1024 || invocations <= 0 ||
                    invocations > 32 ||
                    (output != GL_POINTS && output != GL_LINE_STRIP &&
                     output != GL_TRIANGLE_STRIP)) {
                    program.error = "Unsupported geometry-stage output bound";
                } else {
                    const uint32_t width = output == GL_TRIANGLE_STRIP ? 3 :
                                           output == GL_LINE_STRIP     ? 2 :
                                                                         1;
                    program.feedback_mode = width == 3 ? GL_TRIANGLES :
                                            width == 2 ? GL_LINES :
                                                         GL_POINTS;
                    program.feedback_vertices_per_primitive = width;
                    program.feedback_vertices_per_input =
                        uint32_t(std::max(0, maximum - int(width) + 1)) *
                        width * uint32_t(invocations);
                }
            }
            for (GLenum stage :
                 { GL_VERTEX_SHADER, GL_FRAGMENT_SHADER, GL_GEOMETRY_SHADER }) {
                if (stage == GL_GEOMETRY_SHADER && !sources[3])
                    continue;
                GLint count = 0;
                glGetProgramStageiv(program.id, stage,
                                    GL_ACTIVE_SUBROUTINE_UNIFORMS, &count);
                if (count)
                    program.error =
                        "Captured stage has unowned subroutine bindings";
            }
            GLint count = 0;
            glGetProgramiv(program.id, GL_ACTIVE_ATTRIBUTES, &count);
            if (count < 0 || count > 16)
                program.error =
                    "Captured stage attribute count exceeds its bound";
            for (GLint i = 0; i < count && program.error.empty(); ++i) {
                char name[256]{};
                GLsizei length = 0;
                GLint size = 0;
                GLenum type = 0;
                glGetActiveAttrib(program.id, i, sizeof(name), &length, &size,
                                  &type, name);
                const GLint location = glGetAttribLocation(program.id, name);
                if (size != 1 || location < 0 || location >= 16) {
                    program.error = "Unsupported captured attribute interface";
                    break;
                }
                const bool sint = type == GL_INT || type == GL_INT_VEC2 ||
                                  type == GL_INT_VEC3 || type == GL_INT_VEC4;
                const bool uint = type == GL_UNSIGNED_INT ||
                                  type == GL_UNSIGNED_INT_VEC2 ||
                                  type == GL_UNSIGNED_INT_VEC3 ||
                                  type == GL_UNSIGNED_INT_VEC4;
                if (!sint && !uint && type != GL_FLOAT &&
                    type != GL_FLOAT_VEC2 && type != GL_FLOAT_VEC3 &&
                    type != GL_FLOAT_VEC4) {
                    program.error = "Unsupported captured numeric attribute";
                    break;
                }
                program.attributes.push_back({ location, sint || uint, uint });
            }
            glGetProgramiv(program.id, GL_ACTIVE_UNIFORMS, &count);
            if (count < 0 || count > 256)
                program.error =
                    "Captured stage uniform count exceeds its bound";
            for (GLint i = 0; i < count && program.error.empty(); ++i) {
                char name[256]{};
                GLsizei length = 0;
                GLint size = 0;
                GLenum type = 0;
                glGetActiveUniform(program.id, i, sizeof(name), &length, &size,
                                   &type, name);
                const std::string n(name, size_t(length));
                const GLint location = glGetUniformLocation(program.id, name);
                if (n == "asset_inspection_from_clip" ||
                    n == "asset_project_output" || n == "asset_viewport_extent")
                    continue;
                const auto stage = UniformStage(n);
                if (!stage || size <= 0 || size > 4096 || location < 0) {
                    program.error = "Unsupported generated uniform: " + n;
                    break;
                }
                program.uniforms.push_back(
                    { n, stage, type, uint32_t(size), location });
            }
            GLint blocks = 0;
            glGetProgramiv(program.id, GL_ACTIVE_UNIFORM_BLOCKS, &blocks);
            if (blocks)
                program.error = "Generated uniform blocks were not adapted";
            if (epoxy_gl_version() >= 43) {
                glGetProgramInterfaceiv(program.id, GL_SHADER_STORAGE_BLOCK,
                                        GL_ACTIVE_RESOURCES, &blocks);
                if (blocks)
                    program.error =
                        "Captured stage has unowned storage bindings";
                glGetProgramInterfaceiv(program.id, GL_ATOMIC_COUNTER_BUFFER,
                                        GL_ACTIVE_RESOURCES, &blocks);
                if (blocks)
                    program.error =
                        "Captured stage has unowned atomic bindings";
            }
        }
        if (!program.error.empty()) {
            glDeleteProgram(program.id);
            program.id = 0;
        }
        programs.push_back(std::move(program));
        *error = programs.back().error;
        return programs.back().id ? &programs.back() : nullptr;
    }
    Mesh *GetMesh(const AssetPart &part, const Program &program,
                  uint32_t backend, std::string *error)
    {
        if (part.vertices.size() != part.source_vertices.size() ||
            part.vertices.empty() || part.indices.empty()) {
            *error = "Missing captured vertex remap";
            return nullptr;
        }
        KeyBytes key;
        key.Add(program.key);
        for (const auto &attribute : program.attributes) {
            const auto *stream = Blob(
                part, "vertex.attribute" + std::to_string(attribute.location));
            if (!stream || !stream->data) {
                *error = "Missing active vertex stream " +
                         std::to_string(attribute.location);
                return nullptr;
            }
            key.Block(stream->data);
            key.Add(stream->format);
            key.Add(stream->stride);
            key.Add(stream->components);
            key.Add(stream->normalized);
            key.Add(stream->integer);
            key.Add(stream->count);
            key.Add(stream->offset);
            key.Add(stream->slot);
        }
        for (uint32_t i : part.source_vertices)
            key.Add(i);
        for (uint32_t i : part.indices) {
            if (i >= part.vertices.size()) {
                *error = "Invalid captured mesh index";
                return nullptr;
            }
            key.Add(i);
        }
        const auto digest = key.Digest();
        for (auto it = meshes.begin(); it != meshes.end(); ++it)
            if (it->key == digest) {
                meshes.splice(meshes.end(), meshes, it);
                return &meshes.back();
            }
        const uint64_t vertex_bytes =
            part.vertices.size() * program.attributes.size() * 16;
        const uint64_t total = vertex_bytes + part.indices.size() * 4;
        if (total > budget / 2) {
            *error = "Captured stage mesh exceeds the GPU cache budget";
            return nullptr;
        }
        while ((mesh_bytes + total > budget / 2 || meshes.size() >= 128) &&
               !meshes.empty()) {
            Delete(meshes.front());
            meshes.pop_front();
        }
        std::vector<uint8_t> vertices(vertex_bytes);
        for (size_t a = 0; a < program.attributes.size(); ++a) {
            const auto &attribute = program.attributes[a];
            const auto *stream = Blob(
                part, "vertex.attribute" + std::to_string(attribute.location));
            for (size_t v = 0; v < part.vertices.size(); ++v) {
                auto *destination =
                    vertices.data() + (a * part.vertices.size() + v) * 16;
                if (attribute.integer) {
                    std::array<uint32_t, 4> values;
                    bool signed_values = false;
                    if (!DecodeAssetIntegerAttribute(*stream, backend,
                                                     part.source_vertices[v],
                                                     &values, &signed_values) ||
                        signed_values == attribute.unsigned_values) {
                        *error = "Unsupported captured integer stream";
                        return nullptr;
                    }
                    std::memcpy(destination, values.data(), 16);
                } else {
                    std::array<float, 4> values;
                    if (!DecodeAssetAttribute(*stream, backend,
                                              part.source_vertices[v],
                                              &values)) {
                        *error = "Unsupported captured float stream";
                        return nullptr;
                    }
                    std::memcpy(destination, values.data(), 16);
                }
            }
        }
        Mesh mesh;
        mesh.key = digest;
        mesh.bytes = total;
        glGenVertexArrays(1, &mesh.vao);
        glBindVertexArray(mesh.vao);
        glGenBuffers(1, &mesh.vbo);
        glBindBuffer(GL_ARRAY_BUFFER, mesh.vbo);
        glBufferData(GL_ARRAY_BUFFER, vertices.size(), vertices.data(),
                     GL_STATIC_DRAW);
        glGenBuffers(1, &mesh.ebo);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mesh.ebo);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, part.indices.size() * 4,
                     part.indices.data(), GL_STATIC_DRAW);
        for (size_t a = 0; a < program.attributes.size(); ++a) {
            const auto &attribute = program.attributes[a];
            const auto *offset =
                reinterpret_cast<const void *>(a * part.vertices.size() * 16);
            glEnableVertexAttribArray(attribute.location);
            if (attribute.integer)
                glVertexAttribIPointer(
                    attribute.location, 4,
                    attribute.unsigned_values ? GL_UNSIGNED_INT : GL_INT, 16,
                    offset);
            else
                glVertexAttribPointer(attribute.location, 4, GL_FLOAT, GL_FALSE,
                                      16, offset);
        }
        mesh_bytes += mesh.bytes;
        meshes.push_back(mesh);
        return &meshes.back();
    }
    Texture *GetTexture(const AssetPart &part, uint32_t backend, size_t slot,
                        bool cube, std::string *error)
    {
        const auto &texture = part.occurrence->inputs.textures[slot];
        const auto &m = texture.metadata;
        if (!texture.described || !m.bound || !m.width || !m.height ||
            m.width > 2048 || m.height > 2048 || m.depth != 1 ||
            m.face_count != (cube ? 6U : 1U) || !m.mip_levels ||
            m.mip_levels > 16 || (cube && m.width != m.height)) {
            *error =
                "Missing/unsupported captured texture T" + std::to_string(slot);
            return nullptr;
        }
        const auto *storage =
            Blob(part, "texture.storage." + std::to_string(slot));
        const bool typed = backend == 2 && m.host_format == 70;
        if (typed &&
            (cube || m.mip_levels != 1 || !storage || !storage->data ||
             storage->format != 70 || storage->normalized != 1 ||
             storage->integer || storage->stride != 2 ||
             storage->components != 1 || storage->offset ||
             storage->slot != slot ||
             storage->count != size_t(m.width) * m.height ||
             storage->data->bytes.size() != size_t(m.width) * m.height * 2)) {
            *error = "Missing exact R16 texture storage";
            return nullptr;
        }
        KeyBytes bytes;
        bytes.Add(backend);
        bytes.Add(m.width);
        bytes.Add(m.height);
        bytes.Add(m.face_count);
        bytes.Add(m.mip_levels);
        bytes.Add(m.host_format);
        bytes.Add(m.min_filter);
        bytes.Add(m.mag_filter);
        bytes.Add(m.wrap_s);
        bytes.Add(m.wrap_t);
        bytes.Add(m.wrap_r);
        uint64_t image_bytes = 0;
        std::vector<const capture::CaptureOwnedTextureImage *> validated_images;
        if (typed) {
            bytes.Block(storage->data);
            image_bytes = storage->data->bytes.size();
        } else {
            if (texture.images.size() != size_t(m.mip_levels) * m.face_count) {
                *error = "Duplicate/missing captured texture mip/face";
                return nullptr;
            }
            std::set<std::pair<uint32_t, uint32_t>> image_keys;
            for (const auto &image : texture.images)
                if (image.mip_level >= m.mip_levels ||
                    image.face >= m.face_count ||
                    !image_keys.emplace(image.mip_level, image.face).second) {
                    *error = "Duplicate/invalid captured texture mip/face";
                    return nullptr;
                }
            for (uint32_t mip = 0; mip < m.mip_levels; ++mip)
                for (uint32_t face = 0; face < m.face_count; ++face) {
                    const auto image = std::find_if(
                        texture.images.begin(), texture.images.end(),
                        [&](const auto &i) {
                            return i.mip_level == mip && i.face == face;
                        });
                    const uint32_t w = std::max(1U, m.width >> mip),
                                   h = std::max(1U, m.height >> mip);
                    if (image == texture.images.end() || !image->image.rgba ||
                        image->image.width != w || image->image.height != h ||
                        image->image.rgba->bytes.size() != size_t(w) * h * 4) {
                        *error = "Missing captured texture mip/face";
                        return nullptr;
                    }
                    validated_images.push_back(&*image);
                    bytes.Block(image->image.rgba);
                    image_bytes += image->image.rgba->bytes.size();
                }
        }
        const std::string prefix =
            "capture.texture" + std::to_string(slot) + ".";
        const bool full_sampler =
            backend == 2 ? Reg(part, prefix + "sampler_ready") == 1 :
                           HasReg(part, prefix + "min_lod_bits") &&
                               HasReg(part, prefix + "max_lod_bits") &&
                               HasReg(part, prefix + "lod_bias_bits") &&
                               HasReg(part, prefix + "base_level") &&
                               HasReg(part, prefix + "max_level") &&
                               HasReg(part, prefix + "compare_mode");
        if (backend == 1 && ((!full_sampler && m.mip_levels > 1) ||
                             Reg(part, prefix + "compare_mode") != GL_NONE ||
                             m.host_format == GL_R16)) {
            *error = "Missing/unsupported OpenGL sampler or exact R16 storage";
            return nullptr;
        }
        if (backend == 2 && ((!full_sampler && m.mip_levels > 1) ||
                             Reg(part, prefix + "compare_enable") ||
                             Reg(part, prefix + "unnormalized_coordinates"))) {
            *error = "Missing or unsupported captured Vulkan sampler state";
            return nullptr;
        }
        std::array<GLint, 4> mapping{ GL_RED, GL_GREEN, GL_BLUE, GL_ALPHA };
        if (typed || backend == 1)
            for (size_t c = 0; c < 4; ++c) {
                mapping[c] = Reg(part, prefix + "swizzle" + std::to_string(c),
                                 mapping[c]);
                if (mapping[c] != 0 && mapping[c] != 1 &&
                    mapping[c] != GL_RED && mapping[c] != GL_GREEN &&
                    mapping[c] != GL_BLUE && mapping[c] != GL_ALPHA) {
                    *error = "Invalid captured texture component mapping";
                    return nullptr;
                }
            }
        bytes.Add(mapping);
        for (const auto &reg : part.occurrence->inputs.registers)
            if (reg.name.rfind(prefix, 0) == 0) {
                for (char c : reg.name)
                    bytes.Add(c);
                bytes.Add(reg.value);
            }
        const auto key = bytes.Digest();
        for (auto it = textures.begin(); it != textures.end(); ++it)
            if (it->key == key) {
                textures.splice(textures.end(), textures, it);
                return &textures.back();
            }
        if (image_bytes > budget / 2) {
            *error = "Captured texture exceeds the GPU cache budget";
            return nullptr;
        }
        while ((texture_bytes + image_bytes > budget / 2 ||
                textures.size() >= 128) &&
               !textures.empty()) {
            const auto victim = std::find_if(
                textures.begin(), textures.end(), [&](const auto &t) {
                    return std::find(draw_textures.begin(), draw_textures.end(),
                                     t.id) == draw_textures.end();
                });
            if (victim == textures.end()) {
                *error = "Simultaneous captured texture bindings exceed the "
                         "GPU cache budget";
                return nullptr;
            }
            Delete(*victim);
            textures.erase(victim);
        }
        Texture target;
        target.key = key;
        target.bytes = image_bytes;
        target.target = cube ? GL_TEXTURE_CUBE_MAP : GL_TEXTURE_2D;
        glGenTextures(1, &target.id);
        glBindTexture(target.target, target.id);
        if (typed)
            glTexImage2D(target.target, 0, GL_R16, m.width, m.height, 0, GL_RED,
                         GL_UNSIGNED_SHORT, storage->data->bytes.data());
        else
            for (const auto *validated : validated_images) {
                const auto &image = *validated;
                std::vector<uint8_t> reversed;
                const auto *pixels = image.image.rgba->bytes.data();
                if (backend == 1) {
                    reversed.resize(image.image.rgba->bytes.size());
                    const size_t row = size_t(image.image.width) * 4;
                    for (size_t y = 0; y < image.image.height; ++y)
                        std::memcpy(reversed.data() + y * row,
                                    pixels + (image.image.height - 1 - y) * row,
                                    row);
                    pixels = reversed.data();
                }
                glTexImage2D(
                    cube ? GL_TEXTURE_CUBE_MAP_POSITIVE_X + image.face :
                           target.target,
                    image.mip_level, GL_RGBA8, image.image.width,
                    image.image.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
            }
        glTexParameteriv(target.target, GL_TEXTURE_SWIZZLE_RGBA,
                         mapping.data());
        const auto filter = [&](uint32_t f) {
            return backend == 2 ? (f == 1 ? GL_LINEAR : GL_NEAREST) : GLint(f);
        };
        auto wrap = [&](uint32_t f) {
            if (backend == 2) {
                return f == 0 ? GLint(GL_REPEAT) :
                       f == 1 ? GLint(GL_MIRRORED_REPEAT) :
                       f == 3 ? GLint(GL_CLAMP_TO_BORDER) :
                       f == 4 ? GLint(GL_MIRROR_CLAMP_TO_EDGE) :
                                GLint(GL_CLAMP_TO_EDGE);
            }
            return f == GL_CLAMP_TO_BORDER || f == GL_MIRROR_CLAMP_TO_EDGE ?
                       GLint(f) :
                       preview::CapturedGlWrap(f);
        };
        const GLint mag = filter(m.mag_filter);
        GLint min = filter(m.min_filter);
        if (backend == 2 && full_sampler && m.mip_levels > 1) {
            const bool mip_linear = Reg(part, prefix + "mipmap_mode") == 1;
            min = min == GL_LINEAR ? (mip_linear ? GL_LINEAR_MIPMAP_LINEAR :
                                                   GL_LINEAR_MIPMAP_NEAREST) :
                                     (mip_linear ? GL_NEAREST_MIPMAP_LINEAR :
                                                   GL_NEAREST_MIPMAP_NEAREST);
        }
        if (min != GL_NEAREST && min != GL_LINEAR &&
            (min < GL_NEAREST_MIPMAP_NEAREST || min > GL_LINEAR_MIPMAP_LINEAR))
            min = GL_NEAREST;
        glTexParameteri(target.target, GL_TEXTURE_MIN_FILTER, min);
        glTexParameteri(target.target, GL_TEXTURE_MAG_FILTER,
                        mag == GL_LINEAR ? GL_LINEAR : GL_NEAREST);
        const uint32_t base =
            backend == 1 && full_sampler ? Reg(part, prefix + "base_level") : 0;
        const uint32_t requested_max =
            backend == 1 && full_sampler ?
                Reg(part, prefix + "max_level") :
                (full_sampler ? m.mip_levels - 1 : 0);
        const uint32_t max = std::min(requested_max, m.mip_levels - 1);
        const bool mip_filter = min != GL_NEAREST && min != GL_LINEAR;
        const bool complete_chain =
            std::max(m.width, m.height) >> (m.mip_levels - 1) <= 1;
        if (base > max ||
            (mip_filter && requested_max >= m.mip_levels && !complete_chain)) {
            glDeleteTextures(1, &target.id);
            *error = "Captured sampler mip range is invalid";
            return nullptr;
        }
        glTexParameteri(target.target, GL_TEXTURE_BASE_LEVEL, base);
        glTexParameteri(target.target, GL_TEXTURE_MAX_LEVEL, max);
        glTexParameteri(target.target, GL_TEXTURE_WRAP_S, wrap(m.wrap_s));
        glTexParameteri(target.target, GL_TEXTURE_WRAP_T, wrap(m.wrap_t));
        glTexParameteri(target.target, GL_TEXTURE_WRAP_R, wrap(m.wrap_r));
        if (full_sampler) {
            std::array<float, 4> border;
            for (size_t c = 0; c < border.size(); ++c) {
                const auto bits =
                    Reg(part, prefix + "border" + std::to_string(c) + "_bits");
                std::memcpy(&border[c], &bits, 4);
            }
            const auto bits = Reg(part, prefix + "anisotropy_bits", 0x3f800000);
            float anisotropy;
            std::memcpy(&anisotropy, &bits, 4);
            const bool supported =
                epoxy_has_gl_extension("GL_EXT_texture_filter_anisotropic") ||
                epoxy_has_gl_extension("GL_ARB_texture_filter_anisotropic");
            GLfloat maximum = 1.f;
            if (supported)
                glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &maximum);
            if (!std::all_of(border.begin(), border.end(),
                             [](float f) { return std::isfinite(f); }) ||
                !std::isfinite(anisotropy) || anisotropy < 1.f ||
                anisotropy > maximum ||
                (((backend == 2 &&
                   (m.wrap_s == 4 || m.wrap_t == 4 || m.wrap_r == 4)) ||
                  (backend == 1 && (m.wrap_s == GL_MIRROR_CLAMP_TO_EDGE ||
                                    m.wrap_t == GL_MIRROR_CLAMP_TO_EDGE ||
                                    m.wrap_r == GL_MIRROR_CLAMP_TO_EDGE))) &&
                 epoxy_gl_version() < 44 &&
                 !epoxy_has_gl_extension(
                     "GL_ARB_texture_mirror_clamp_to_edge"))) {
                glDeleteTextures(1, &target.id);
                *error = "Captured sampler exceeds HUD OpenGL support";
                return nullptr;
            }
            glTexParameterfv(target.target, GL_TEXTURE_BORDER_COLOR,
                             border.data());
            if (supported)
                glTexParameterf(target.target, GL_TEXTURE_MAX_ANISOTROPY_EXT,
                                anisotropy);
        }
        for (const auto *name :
             { "min_lod_bits", "max_lod_bits", "lod_bias_bits" }) {
            const auto bits = Reg(part, prefix + name);
            float value;
            std::memcpy(&value, &bits, 4);
            if (!std::isfinite(value)) {
                glDeleteTextures(1, &target.id);
                *error = "Nonfinite captured sampler control";
                return nullptr;
            }
            if (full_sampler)
                glTexParameterf(target.target,
                                std::strcmp(name, "min_lod_bits") == 0 ?
                                    GL_TEXTURE_MIN_LOD :
                                std::strcmp(name, "max_lod_bits") == 0 ?
                                    GL_TEXTURE_MAX_LOD :
                                    GL_TEXTURE_LOD_BIAS,
                                value);
        }
        texture_bytes += target.bytes;
        textures.push_back(target);
        return &textures.back();
    }
    bool Uniforms(Program &program, const AssetPart &part, uint32_t backend,
                  uint32_t width, uint32_t height, std::string *error)
    {
        std::vector<preview::OwnedDrawUniform> values;
        for (const auto &u : program.uniforms) {
            const auto base = UniformBase(u.name.substr(7));
            if (u.type == GL_SAMPLER_2D || u.type == GL_SAMPLER_CUBE) {
                if (base.size() != 8 || base.rfind("texSamp", 0) != 0 ||
                    base[7] < '0' || base[7] > '3' || u.count != 1) {
                    *error = "Unsupported captured sampler interface";
                    return false;
                }
                const size_t slot = base[7] - '0';
                glActiveTexture(GL_TEXTURE0 + slot);
                glBindSampler(slot, 0);
                auto *texture = GetTexture(part, backend, slot,
                                           u.type == GL_SAMPLER_CUBE, error);
                if (!texture)
                    return false;
                draw_textures.push_back(texture->id);
                glBindTexture(texture->target, texture->id);
                glUniform1i(u.location, slot);
                continue;
            }
            const auto &uniforms = part.occurrence->inputs.uniforms;
            const auto input = std::find_if(
                uniforms.begin(), uniforms.end(), [&](const auto &v) {
                    return (v.stage == u.stage ||
                            (backend == 1 && v.stage == 0)) &&
                           UniformBase(v.name) == base;
                });
            preview::OwnedDrawUniform value;
            value.name = u.name;
            value.stage = u.stage;
            if (input != uniforms.end()) {
                if (!input->data ||
                    ExpectedUniform(input->type, input->components) != u.type ||
                    input->count < u.count ||
                    input->data->bytes.size() !=
                        uint64_t(input->count) * input->components * 4 ||
                    input->data->bytes.size() > 65536) {
                    *error = "Invalid captured active uniform: " + base;
                    return false;
                }
                value.type = input->type;
                value.components = input->components;
                value.count = u.count;
                value.data.assign(input->data->bytes.begin(),
                                  input->data->bytes.begin() +
                                      size_t(u.count) * value.components * 4);
            } else if (u.stage == 1 && base == "inlineValue" &&
                       u.type == GL_FLOAT_VEC4) {
                const auto mask = Reg(part, "capture.vertices.uniform_mask");
                value.type = XEMU_SHADER_DRAW_UNIFORM_FLOAT;
                value.components = 4;
                for (size_t slot = 0; slot < 16; ++slot)
                    if (mask & (1U << slot)) {
                        const auto *current =
                            Blob(part, "vertex.current" + std::to_string(slot));
                        if (!current || !current->data ||
                            current->data->bytes.size() != 16) {
                            *error = "Missing current vertex attribute";
                            return false;
                        }
                        value.data.insert(value.data.end(),
                                          current->data->bytes.begin(),
                                          current->data->bytes.end());
                        ++value.count;
                    }
                if (value.count < u.count) {
                    *error = "Missing captured inline uniform slots";
                    return false;
                }
                value.count = u.count;
                value.data.resize(size_t(u.count) * 16);
            } else {
                *error = "Missing captured active uniform: " + base;
                return false;
            }
            // These controls describe the new inspection raster, never
            // material.
            if (u.stage == 2 && base == "clipRegion" && u.type == GL_INT_VEC4) {
                std::vector<int32_t> regions(size_t(u.count) * 4);
                if (!program.exclusive_clip)
                    for (size_t i = 0; i < u.count; ++i) {
                        regions[4 * i + 2] = width;
                        regions[4 * i + 3] = height;
                    }
                std::memcpy(value.data.data(), regions.data(),
                            value.data.size());
            } else if (u.stage == 2 && base == "surfaceScale" &&
                       u.type == GL_INT_VEC2) {
                const int32_t scale[2] = { 1, 1 };
                std::memcpy(value.data.data(), scale, 8);
            } else if (u.stage == 2 && base == "clipRange" &&
                       u.type == GL_FLOAT_VEC4) {
                // Generated depth interpolation uses the original window
                // positions, which no longer bound the inspection camera.
                // Both signs are valid here; the stage wrapper supplies the
                // inspection raster depth after the original material code.
                const float limits[2] = { -3.402823466e38f, 3.402823466e38f };
                std::memcpy(value.data.data() + 8, limits, 8);
            } else if (u.stage == 2 &&
                       (base == "depthFactor" || base == "depthOffset") &&
                       u.type == GL_FLOAT)
                std::fill(value.data.begin(), value.data.end(), 0);
            values.push_back(std::move(value));
        }
        if (preview::ApplyCapturedGlUniforms(program.id, values, true)) {
            *error = "Captured uniforms could not be applied";
            return false;
        }
        return true;
    }
};
AssetStageRenderer::AssetStageRenderer() : impl_(std::make_unique<Impl>())
{
}
AssetStageRenderer::~AssetStageRenderer() = default;
bool AssetStageRenderer::DrawPart(const AssetPart &part, uint32_t backend,
                                  const AssetMatrix &matrix, uint32_t width,
                                  uint32_t height, std::string *error,
                                  bool projected_output)
{
    if (error)
        error->clear();
    std::string local;
    if (!error)
        error = &local;
    if (epoxy_gl_version() < 45) {
        *error = "Captured generated stages require OpenGL4.5 in the HUD";
        return false;
    }
    if (!part.occurrence || !part.occurrence->inputs.complete ||
        (backend != 1 && backend != 2)) {
        *error = "Captured inputs are incomplete";
        return false;
    }
    auto *program = impl_->GetProgram(part, backend, error);
    if (!program)
        return false;
    auto *mesh = impl_->GetMesh(part, *program, backend, error);
    if (!mesh)
        return false;
    preview::OwnedDrawInputs raster_inputs;
    raster_inputs.registers = part.occurrence->inputs.registers;
    for (const auto &b : part.occurrence->inputs.blobs)
        if ((b.name.rfind("vk.", 0) == 0 || b.name.rfind("host.", 0) == 0) &&
            b.data) {
            preview::OwnedDrawBlob blob;
            blob.name = b.name;
            blob.bytes = b.data->bytes;
            raster_inputs.blobs.push_back(std::move(blob));
        }
    preview::PreviewCapturedRaster raster;
    if (!preview::DecodePreviewCapturedRaster(
            raster_inputs,
            backend == 2 ? preview::PreviewBackend::Vulkan :
                           preview::PreviewBackend::OpenGL,
            &raster, error))
        return false;
    const uint32_t required =
        preview::PreviewRasterDepth | preview::PreviewRasterBlend |
        preview::PreviewRasterColorWrite | preview::PreviewRasterCull;
    if ((raster.available & required) != required) {
        *error = "Captured raster components are missing";
        return false;
    }
    raster.scissor_enabled = false;
    raster.depth_min = 0;
    raster.depth_max = 1;
    preview::ApplyOriginalGlRaster(raster, height);
    glUseProgram(program->id);
    glUniform1i(glGetUniformLocation(program->id, "asset_project_output"),
                projected_output);
    glUniformMatrix4fv(
        glGetUniformLocation(program->id, "asset_inspection_from_clip"), 1,
        GL_TRUE, matrix.data());
    glUniform2f(glGetUniformLocation(program->id, "asset_viewport_extent"),
                width, height);
    impl_->draw_textures.clear();
    if (!impl_->Uniforms(*program, part, backend, width, height, error))
        return false;
    glEnable(GL_TEXTURE_CUBE_MAP_SEAMLESS);
    glBindVertexArray(mesh->vao);
    glDrawElements(GL_TRIANGLES, part.indices.size(), GL_UNSIGNED_INT, nullptr);
    return true;
}
bool AssetStageRenderer::OutputBounds(const AssetPart &part, uint32_t backend,
                                      capture::Bounds3 *bounds,
                                      std::string *error)
{
    if (!bounds || !error)
        return false;
    *bounds = {};
    error->clear();
    if (epoxy_gl_version() < 45 || !part.occurrence ||
        !part.occurrence->inputs.complete || (backend != 1 && backend != 2)) {
        *error =
            "Post-transform framing requires complete inputs and HUD OpenGL4.5";
        return false;
    }
    auto *program = impl_->GetProgram(part, backend, error);
    if (!program)
        return false;
    auto *mesh = impl_->GetMesh(part, *program, backend, error);
    if (!mesh)
        return false;
    KeyBytes inputs;
    inputs.Add(program->key);
    inputs.Add(mesh->key);
    for (const auto &u : part.occurrence->inputs.uniforms) {
        inputs.Add(u.stage);
        inputs.Add(u.type);
        inputs.Add(u.components);
        inputs.Add(u.count);
        inputs.Add(uint64_t(u.name.size()));
        inputs.bytes.insert(inputs.bytes.end(), u.name.begin(), u.name.end());
        inputs.Block(u.data);
    }
    for (const auto &r : part.occurrence->inputs.registers) {
        inputs.Add(uint64_t(r.name.size()));
        inputs.bytes.insert(inputs.bytes.end(), r.name.begin(), r.name.end());
        inputs.Add(r.value);
    }
    for (const auto &b : part.occurrence->inputs.blobs)
        if (b.name.rfind("texture.storage.", 0) == 0 ||
            b.name.rfind("vertex.current", 0) == 0) {
            inputs.Add(uint64_t(b.name.size()));
            inputs.bytes.insert(inputs.bytes.end(), b.name.begin(),
                                b.name.end());
            inputs.Add(b.slot);
            inputs.Add(b.count);
            inputs.Add(b.format);
            inputs.Add(b.components);
            inputs.Add(b.stride);
            inputs.Add(b.offset);
            inputs.Add(b.normalized);
            inputs.Add(b.integer);
            inputs.Block(b.data);
        }
    for (const auto &t : part.occurrence->inputs.textures) {
        const auto &m = t.metadata;
        const uint32_t properties[] = {
            m.slot,       uint32_t(m.bound), m.guest_format, m.host_format,
            m.width,      m.height,          m.depth,        m.mip_levels,
            m.face_count, m.min_filter,      m.mag_filter,   m.wrap_s,
            m.wrap_t,     m.wrap_r,          m.mip_level,    m.face
        };
        inputs.Add(properties);
        inputs.Add(m.coordinate_scale);
        for (const auto &i : t.images) {
            inputs.Add(i.mip_level);
            inputs.Add(i.face);
            inputs.Add(i.image.width);
            inputs.Add(i.image.height);
            inputs.Block(i.image.rgba);
        }
    }
    const auto key = inputs.Digest();
    for (auto it = impl_->output_bounds.begin();
         it != impl_->output_bounds.end(); ++it)
        if (it->key == key) {
            impl_->output_bounds.splice(impl_->output_bounds.end(),
                                        impl_->output_bounds, it);
            *bounds = impl_->output_bounds.back().bounds;
            *error = impl_->output_bounds.back().error;
            return bounds->valid;
        }
    const bool geometry = bool(part.occurrence->inputs.sources[3]);
    const uint64_t capacity = geometry ?
                                  uint64_t(part.indices.size() / 3) *
                                      program->feedback_vertices_per_input :
                                  part.vertices.size();
    const uint64_t size = capacity * sizeof(float) * 4;
    if (size > impl_->budget / 4) {
        *error =
            "Post-transform framing exceeds the bounded GPU readback budget";
        return false;
    }
    GLboolean active = GL_FALSE;
    glGetBooleanv(GL_TRANSFORM_FEEDBACK_ACTIVE, &active);
    GLint current_query = 0;
    glGetQueryiv(GL_TRANSFORM_FEEDBACK_PRIMITIVES_WRITTEN, GL_CURRENT_QUERY,
                 &current_query);
    if (active || current_query) {
        *error = "HUD transform feedback or its query is already active";
        return false;
    }
    if (!capacity) {
        *error = "Geometry stage emitted no positions";
        return false;
    }
    static const AssetMatrix identity{ 1, 0, 0, 0, 0, 1, 0, 0,
                                       0, 0, 1, 0, 0, 0, 0, 1 };
    glUseProgram(program->id);
    glUniform1i(glGetUniformLocation(program->id, "asset_project_output"), 0);
    glUniformMatrix4fv(
        glGetUniformLocation(program->id, "asset_inspection_from_clip"), 1,
        GL_TRUE, identity.data());
    glUniform2f(glGetUniformLocation(program->id, "asset_viewport_extent"), 1,
                1);
    impl_->draw_textures.clear();
    if (!impl_->Uniforms(*program, part, backend, 1, 1, error))
        return false;
    // Match the sampler state used by the final captured-stage draw.
    glEnable(GL_TEXTURE_CUBE_MAP_SEAMLESS);
    GLint previous_feedback, previous_buffer;
    glGetIntegerv(GL_TRANSFORM_FEEDBACK_BINDING, &previous_feedback);
    glGetIntegerv(GL_TRANSFORM_FEEDBACK_BUFFER_BINDING, &previous_buffer);
    const auto discard = glIsEnabled(GL_RASTERIZER_DISCARD);
    std::vector<std::array<float, 4>> positions;
    GLuint feedback, buffer, query;
    glGenTransformFeedbacks(1, &feedback);
    glBindTransformFeedback(GL_TRANSFORM_FEEDBACK, feedback);
    glGenBuffers(1, &buffer);
    glBindBuffer(GL_TRANSFORM_FEEDBACK_BUFFER, buffer);
    glBufferData(GL_TRANSFORM_FEEDBACK_BUFFER, size, nullptr, GL_STREAM_READ);
    GLint64 allocated = 0;
    glGetBufferParameteri64v(GL_TRANSFORM_FEEDBACK_BUFFER, GL_BUFFER_SIZE,
                             &allocated);
    if (allocated != int64_t(size)) {
        glBindTransformFeedback(GL_TRANSFORM_FEEDBACK, previous_feedback);
        glBindBuffer(GL_TRANSFORM_FEEDBACK_BUFFER, previous_buffer);
        glDeleteBuffers(1, &buffer);
        glDeleteTransformFeedbacks(1, &feedback);
        *error = "Post-transform position buffer allocation failed";
        return false;
    }
    glBindBufferBase(GL_TRANSFORM_FEEDBACK_BUFFER, 0, buffer);
    glEnable(GL_RASTERIZER_DISCARD);
    glBindVertexArray(mesh->vao);
    glGenQueries(1, &query);
    glBeginQuery(GL_TRANSFORM_FEEDBACK_PRIMITIVES_WRITTEN, query);
    glBeginTransformFeedback(program->feedback_mode);
    if (geometry)
        glDrawElements(GL_TRIANGLES, part.indices.size(), GL_UNSIGNED_INT,
                       nullptr);
    else
        glDrawArrays(GL_POINTS, 0, part.vertices.size());
    glEndTransformFeedback();
    glEndQuery(GL_TRANSFORM_FEEDBACK_PRIMITIVES_WRITTEN);
    GLuint64 written = 0;
    glGetQueryObjectui64v(query, GL_QUERY_RESULT, &written);
    const uint64_t count = written * program->feedback_vertices_per_primitive;
    if (!count)
        *error = "Geometry stage emitted no positions";
    else if (count > capacity)
        *error = "Geometry output exceeded its declared bound";
    else {
        positions.resize(size_t(count));
        glGetBufferSubData(GL_TRANSFORM_FEEDBACK_BUFFER, 0,
                           count * sizeof(float) * 4, positions.data());
    }
    glBindTransformFeedback(GL_TRANSFORM_FEEDBACK, previous_feedback);
    glBindBuffer(GL_TRANSFORM_FEEDBACK_BUFFER, previous_buffer);
    if (!discard)
        glDisable(GL_RASTERIZER_DISCARD);
    glDeleteBuffers(1, &buffer);
    glDeleteTransformFeedbacks(1, &feedback);
    glDeleteQueries(1, &query);
    // Only referenced vertices were compacted into this mesh. Preserve failure
    // evidence rather than silently ignoring non-finite shader outputs.
    for (const auto &position : positions) {
        if (!std::all_of(position.begin(), position.end(),
                         [](float x) { return std::isfinite(x); }) ||
            std::abs(position[3]) < 1e-7f) {
            *bounds = {};
            *error = "Post-transform position is non-finite or has zero w";
            break;
        }
        for (size_t axis = 0; axis < 3; ++axis) {
            const float value = position[axis] / position[3];
            if (!std::isfinite(value)) {
                *bounds = {};
                *error = "Post-transform projected position is non-finite";
                break;
            }
            if (!bounds->valid)
                bounds->minimum[axis] = bounds->maximum[axis] = value;
            else {
                bounds->minimum[axis] = std::min(bounds->minimum[axis], value);
                bounds->maximum[axis] = std::max(bounds->maximum[axis], value);
            }
        }
        if (!error->empty())
            break;
        bounds->valid = true;
    }
    if (impl_->output_bounds.size() == 128)
        impl_->output_bounds.pop_front();
    impl_->output_bounds.push_back({ key, *bounds, *error });
    return bounds->valid;
}
bool AssetStageRenderer::Configure(uint64_t budget)
{
    if (budget < 8U * 1024U * 1024U || budget > 64U * 1024U * 1024U)
        return false;
    if (budget != impl_->budget) {
        Shutdown();
        impl_->budget = budget;
    }
    return true;
}
uint64_t AssetStageRenderer::GpuBytes() const
{
    return impl_->mesh_bytes + impl_->texture_bytes;
}
void AssetStageRenderer::Shutdown()
{
    for (auto &m : impl_->meshes)
        impl_->Delete(m);
    impl_->meshes.clear();
    for (auto &t : impl_->textures)
        impl_->Delete(t);
    impl_->textures.clear();
    for (auto &p : impl_->programs)
        glDeleteProgram(p.id);
    impl_->programs.clear();
    impl_->output_bounds.clear();
}
} // namespace xemu::asset_browser

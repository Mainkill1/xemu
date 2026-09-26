// SPDX-License-Identifier: GPL-2.0-or-later
#include "shader-browser-recipe-inspector.hh"

#include <algorithm>
#include <array>
#include <cstdio>
#include <exception>
#include <fstream>
#include <string>
#include <utility>

#include <nlohmann/json.hpp>

namespace xemu::shader_browser {
namespace {

constexpr char kBase64[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
constexpr char kHex[] = "0123456789abcdef";

bool Fail(std::string *error, const char *message)
{
    if (error) {
        *error = message;
    }
    return false;
}

const char *StageName(Stage stage)
{
    switch (stage) {
    case Stage::Vertex: return "vertex";
    case Stage::Pixel: return "pixel";
    case Stage::Geometry: return "geometry";
    case Stage::FixedFunction: return "fixed-function";
    default: return nullptr;
    }
}

Stage ParseStage(const std::string &name)
{
    if (name == "vertex") return Stage::Vertex;
    if (name == "pixel") return Stage::Pixel;
    if (name == "geometry") return Stage::Geometry;
    if (name == "fixed-function") return Stage::FixedFunction;
    return Stage::Unknown;
}

uint8_t StageCode(Stage stage)
{
    switch (stage) {
    case Stage::Vertex: return 1;
    case Stage::Pixel: return 2;
    case Stage::Geometry: return 3;
    case Stage::FixedFunction: return 4;
    default: return 0;
    }
}

std::string Indexed(const char *name, unsigned index)
{
    return std::string(name) + "[" + std::to_string(index) + "]";
}

std::string Indexed2(const char *name, unsigned first, unsigned second)
{
    return Indexed(name, first) + "[" + std::to_string(second) + "]";
}

class Reader
{
public:
    Reader(const std::vector<uint8_t> &bytes, RecipeInspection *out)
        : bytes_(bytes), out_(out) {}

    bool U32(const std::string &name, uint32_t *value = nullptr)
    {
        if (bytes_.size() - position_ < 4) return false;
        uint32_t result = 0;
        for (unsigned i = 0; i < 4; ++i) {
            result |= static_cast<uint32_t>(bytes_[position_++]) << (8 * i);
        }
        out_->fields.push_back({name, std::to_string(result)});
        if (value) *value = result;
        return true;
    }

    bool I32(const std::string &name)
    {
        uint32_t value = 0;
        if (!U32(name, &value)) return false;
        out_->fields.back().value =
            std::to_string(static_cast<int32_t>(value));
        return true;
    }

    bool Bool(const std::string &name)
    {
        if (position_ == bytes_.size() || bytes_[position_] > 1) return false;
        out_->fields.push_back({name, bytes_[position_++] ? "true" : "false"});
        return true;
    }

    bool Done() const { return position_ == bytes_.size(); }

private:
    const std::vector<uint8_t> &bytes_;
    RecipeInspection *out_;
    size_t position_ = 6;
};

bool VertexCommon(Reader *reader)
{
    return reader->U32("compressed_attrs") &&
           reader->U32("uniform_attrs") &&
           reader->U32("swizzle_attrs") &&
           reader->Bool("fog_enable") &&
           reader->U32("fog_mode") &&
           reader->Bool("specular_enable") &&
           reader->Bool("separate_specular") &&
           reader->Bool("ignore_specular_alpha") &&
           reader->Bool("point_params_enable") &&
           reader->Bool("smooth_shading") &&
           reader->Bool("z_perspective") &&
           reader->U32("point_size_eighths");
}

bool FixedFunction(Reader *reader)
{
    if (!reader->Bool("normalization")) return false;
    for (unsigned i = 0; i < 4; ++i) {
        if (!reader->Bool(Indexed("texture_matrix_enable", i))) return false;
        for (unsigned j = 0; j < 4; ++j) {
            if (!reader->U32(Indexed2("texgen", i, j))) return false;
        }
    }
    if (!reader->U32("foggen") || !reader->U32("skinning") ||
        !reader->Bool("lighting")) return false;
    for (unsigned i = 0; i < 8; ++i) {
        if (!reader->U32(Indexed("light", i))) return false;
    }
    return reader->U32("emission_src") &&
           reader->U32("ambient_src") &&
           reader->U32("diffuse_src") &&
           reader->U32("specular_src") &&
           reader->Bool("local_eye");
}

bool Pixel(Reader *reader)
{
    if (!reader->U32("combiner_control") ||
        !reader->U32("shader_stage_program") ||
        !reader->U32("other_stage_input") ||
        !reader->U32("final_inputs_0") ||
        !reader->U32("final_inputs_1")) return false;
    for (unsigned i = 0; i < 8; ++i) {
        if (!reader->U32(Indexed("rgb_inputs", i)) ||
            !reader->U32(Indexed("rgb_outputs", i)) ||
            !reader->U32(Indexed("alpha_inputs", i)) ||
            !reader->U32(Indexed("alpha_outputs", i))) return false;
    }
    if (!reader->Bool("point_sprite")) return false;
    for (unsigned i = 0; i < 4; ++i) {
        if (!reader->Bool(Indexed("rect_tex", i)) ||
            !reader->Bool(Indexed("snorm_tex", i))) return false;
        for (unsigned j = 0; j < 4; ++j) {
            if (!reader->Bool(Indexed2("compare_mode", i, j))) return false;
        }
        if (!reader->Bool(Indexed("alphakill", i)) ||
            !reader->U32(Indexed("colorkey_mode", i)) ||
            !reader->U32(Indexed("conv_tex", i)) ||
            !reader->Bool(Indexed("tex_x8y24", i)) ||
            !reader->U32(Indexed("dim_tex", i)) ||
            !reader->Bool(Indexed("tex_cubemap", i))) return false;
        for (unsigned j = 0; j < 3; ++j) {
            if (!reader->U32(Indexed2("border_logical_size", i, j))) {
                return false;
            }
        }
        if (!reader->Bool(Indexed("shadow_map", i))) return false;
    }
    return reader->U32("shadow_depth_func") &&
           reader->Bool("alpha_test") &&
           reader->U32("alpha_func") &&
           reader->Bool("window_clip_exclusive") &&
           reader->Bool("smooth_shading") &&
           reader->Bool("depth_clipping") &&
           reader->Bool("z_perspective") &&
           reader->U32("surface_zeta_format") &&
           reader->U32("depth_format");
}

std::string HexBytes(const uint8_t *bytes, size_t size)
{
    std::string result(size * 2, '0');
    for (size_t i = 0; i < size; ++i) {
        result[i * 2] = kHex[bytes[i] >> 4];
        result[i * 2 + 1] = kHex[bytes[i] & 15];
    }
    return result;
}

int HexDigit(char value)
{
    if (value >= '0' && value <= '9') return value - '0';
    if (value >= 'a' && value <= 'f') return value - 'a' + 10;
    if (value >= 'A' && value <= 'F') return value - 'A' + 10;
    return -1;
}

bool ParseHexBytes(const std::string &text, uint8_t *bytes, size_t size)
{
    if (text.size() != size * 2) return false;
    for (size_t i = 0; i < size; ++i) {
        int hi = HexDigit(text[i * 2]);
        int lo = HexDigit(text[i * 2 + 1]);
        if (hi < 0 || lo < 0) return false;
        bytes[i] = static_cast<uint8_t>((hi << 4) | lo);
    }
    return true;
}

std::string EncodeBase64(const std::vector<uint8_t> &bytes)
{
    std::string encoded;
    encoded.reserve((bytes.size() + 2) / 3 * 4);
    for (size_t i = 0; i < bytes.size(); i += 3) {
        uint32_t group = static_cast<uint32_t>(bytes[i]) << 16;
        if (i + 1 < bytes.size()) group |= bytes[i + 1] << 8;
        if (i + 2 < bytes.size()) group |= bytes[i + 2];
        encoded.push_back(kBase64[(group >> 18) & 63]);
        encoded.push_back(kBase64[(group >> 12) & 63]);
        encoded.push_back(i + 1 < bytes.size() ? kBase64[(group >> 6) & 63]
                                                : '=');
        encoded.push_back(i + 2 < bytes.size() ? kBase64[group & 63] : '=');
    }
    return encoded;
}

int Base64Digit(char value)
{
    const char *found = std::find(std::begin(kBase64),
                                  std::end(kBase64) - 1, value);
    return found == std::end(kBase64) - 1 ? -1 :
           static_cast<int>(found - std::begin(kBase64));
}

bool DecodeBase64(const std::string &text, std::vector<uint8_t> *bytes)
{
    if (text.size() % 4 || text.size() >
        (kMaxCanonicalRecipeBytes + 2) / 3 * 4) return false;
    bytes->clear();
    bytes->reserve(text.size() / 4 * 3);
    for (size_t i = 0; i < text.size(); i += 4) {
        int a = Base64Digit(text[i]);
        int b = Base64Digit(text[i + 1]);
        int c = text[i + 2] == '=' ? 0 : Base64Digit(text[i + 2]);
        int d = text[i + 3] == '=' ? 0 : Base64Digit(text[i + 3]);
        if (a < 0 || b < 0 || c < 0 || d < 0 ||
            (text[i + 2] == '=' && text[i + 3] != '=') ||
            ((text[i + 2] == '=' || text[i + 3] == '=') &&
             i + 4 != text.size())) return false;
        uint32_t group = (a << 18) | (b << 12) | (c << 6) | d;
        bytes->push_back(static_cast<uint8_t>(group >> 16));
        if (text[i + 2] != '=') bytes->push_back(group >> 8);
        if (text[i + 3] != '=') bytes->push_back(group);
    }
    return bytes->size() <= kMaxCanonicalRecipeBytes;
}

} // namespace

bool InspectCanonicalRecipe(const CanonicalRecipe &recipe,
                            RecipeInspection *inspection,
                            std::string *error)
{
    if (!inspection) return Fail(error, "recipe inspection destination is null");
    *inspection = {};
    if (recipe.recipe_format_version != 1 ||
        recipe.key.hash.version != 1 || !StageCode(recipe.key.stage)) {
        return Fail(error, "unsupported canonical recipe version or stage");
    }
    const auto &bytes = recipe.bytes;
    if (bytes.size() < 6 || bytes.size() > kMaxCanonicalRecipeBytes ||
        bytes[0] != 'N' || bytes[1] != 'V' || bytes[2] != '2' ||
        bytes[3] != 'A' || bytes[4] != StageCode(recipe.key.stage) ||
        bytes[5] != (recipe.key.stage == Stage::FixedFunction ? 1 : 0)) {
        return Fail(error, "malformed canonical recipe header");
    }
    if (ComputeShaderHash(recipe.key.hash.version, recipe.key.stage,
                          recipe.recipe_format_version, bytes.data(),
                          bytes.size()) != recipe.key.hash) {
        return Fail(error, "canonical recipe hash mismatch");
    }

    inspection->stage = recipe.key.stage;
    Reader reader(bytes, inspection);
    bool valid = false;
    switch (recipe.key.stage) {
    case Stage::Vertex: {
        uint32_t count = 0;
        valid = VertexCommon(&reader) &&
                reader.U32("program_length", &count) && count <= 136;
        inspection->instruction_count = count;
        for (unsigned i = 0; valid && i < count; ++i) {
            for (unsigned j = 0; valid && j < 4; ++j) {
                valid = reader.U32(Indexed2("program_token", i, j));
            }
        }
        break;
    }
    case Stage::FixedFunction:
        valid = VertexCommon(&reader) && FixedFunction(&reader);
        break;
    case Stage::Pixel:
        valid = Pixel(&reader);
        break;
    case Stage::Geometry:
        valid = reader.U32("primitive_mode") &&
                reader.U32("polygon_front_mode") &&
                reader.U32("polygon_back_mode") &&
                reader.Bool("smooth_shading") &&
                reader.Bool("first_vertex_is_provoking") &&
                reader.Bool("z_perspective") &&
                reader.I32("tri_rot0") && reader.I32("tri_rot1");
        break;
    default:
        break;
    }
    if (!valid || !reader.Done()) {
        *inspection = {};
        return Fail(error, "malformed or trailing canonical recipe fields");
    }
    return true;
}

std::string PortableRecipeFilename(const CanonicalRecipe &recipe)
{
    const char *stage = StageName(recipe.key.stage);
    if (!stage) return {};
    return "nv2a-v" + std::to_string(recipe.key.hash.version) + "-" +
           ShaderHashHex(recipe.key.hash) + "-" + stage +
           ".xemu-shader.json";
}

bool SerializePortableRecipe(const CanonicalRecipe &recipe,
                             std::string *json, std::string *error)
{
    if (!json) return Fail(error, "recipe export destination is null");
    RecipeInspection inspected{};
    if (!InspectCanonicalRecipe(recipe, &inspected, error)) return false;

    std::vector<ShaderScope> scopes = recipe.scopes;
    std::sort(scopes.begin(), scopes.end());
    scopes.erase(std::unique(scopes.begin(), scopes.end()), scopes.end());
    nlohmann::json associations = nlohmann::json::array();
    for (const ShaderScope &scope : scopes) {
        associations.push_back({
            {"title_id", FormatTitleId(scope.title_id)},
            {"executable_fingerprint_version",
             scope.executable_fingerprint_version},
            {"executable_fingerprint",
             HexBytes(scope.executable_fingerprint.data(),
                      scope.executable_fingerprint.size())},
        });
    }
    nlohmann::json document = {
        {"schema", "xemu.shader-recipe-export.v1"},
        {"identity_version", recipe.key.hash.version},
        {"recipe_format_version", recipe.recipe_format_version},
        {"stage", StageName(recipe.key.stage)},
        {"shader_hash", ShaderHashHex(recipe.key.hash)},
        {"recipe_size", recipe.bytes.size()},
        {"recipe_base64", EncodeBase64(recipe.bytes)},
        {"associations", std::move(associations)},
    };
    *json = document.dump(2) + '\n';
    return true;
}

bool ParsePortableRecipe(const std::string &json, CanonicalRecipe *recipe,
                         std::string *error)
{
    if (!recipe) return Fail(error, "recipe import destination is null");
    try {
        nlohmann::json document = nlohmann::json::parse(json);
        if (document.at("schema").get<std::string>() !=
            "xemu.shader-recipe-export.v1") {
            return Fail(error, "unsupported recipe export schema");
        }
        CanonicalRecipe parsed{};
        parsed.key.hash.version = document.at("identity_version").get<uint32_t>();
        parsed.recipe_format_version =
            document.at("recipe_format_version").get<uint32_t>();
        parsed.key.stage = ParseStage(document.at("stage").get<std::string>());
        if (!ParseHexBytes(document.at("shader_hash").get<std::string>(),
                           parsed.key.hash.bytes.data(),
                           parsed.key.hash.bytes.size()) ||
            !DecodeBase64(document.at("recipe_base64").get<std::string>(),
                          &parsed.bytes) ||
            document.at("recipe_size").get<size_t>() != parsed.bytes.size()) {
            return Fail(error, "recipe export bytes or hash are invalid");
        }
        for (const auto &item : document.at("associations")) {
            ShaderScope scope{};
            std::string title = item.at("title_id").get<std::string>();
            uint8_t title_bytes[4] = {};
            if (!ParseHexBytes(title, title_bytes, 4) ||
                !ParseHexBytes(
                    item.at("executable_fingerprint").get<std::string>(),
                    scope.executable_fingerprint.data(),
                    scope.executable_fingerprint.size())) {
                return Fail(error, "recipe export association is invalid");
            }
            for (uint8_t byte : title_bytes) {
                scope.title_id = (scope.title_id << 8) | byte;
            }
            scope.executable_fingerprint_version =
                item.at("executable_fingerprint_version").get<uint32_t>();
            parsed.scopes.push_back(scope);
        }
        RecipeInspection inspected{};
        if (!InspectCanonicalRecipe(parsed, &inspected, error)) return false;
        std::sort(parsed.scopes.begin(), parsed.scopes.end());
        parsed.scopes.erase(std::unique(parsed.scopes.begin(),
                                        parsed.scopes.end()),
                            parsed.scopes.end());
        *recipe = std::move(parsed);
        return true;
    } catch (const std::exception &) {
        return Fail(error, "malformed recipe export JSON");
    }
}

bool ExportPortableRecipe(const CanonicalRecipe &recipe,
                          const std::filesystem::path &config_directory,
                          std::filesystem::path *exported_path,
                          std::string *error)
{
    if (config_directory.empty() || !exported_path) {
        return Fail(error, "recipe export path is unavailable");
    }
    std::string contents;
    if (!SerializePortableRecipe(recipe, &contents, error)) return false;
    CanonicalRecipe checked{};
    if (!ParsePortableRecipe(contents, &checked, error) ||
        checked.key != recipe.key || checked.bytes != recipe.bytes) {
        return Fail(error, "recipe export failed round-trip validation");
    }

    std::error_code ec;
    const auto directory = config_directory / "shader-exports";
    std::filesystem::create_directories(directory, ec);
    if (ec) return Fail(error, "unable to create shader export directory");
    const auto target = directory / PortableRecipeFilename(recipe);
    auto temporary = target;
    temporary += ".tmp";
    {
        std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
        if (!file || !file.write(contents.data(), contents.size()) ||
            !file.flush()) {
            std::filesystem::remove(temporary, ec);
            return Fail(error, "unable to write shader recipe export");
        }
    }
    std::filesystem::rename(temporary, target, ec);
    if (ec) {
        // std::filesystem::rename cannot replace an existing file on Windows.
        ec.clear();
        std::filesystem::remove(target, ec);
        if (!ec) std::filesystem::rename(temporary, target, ec);
    }
    if (ec) {
        std::filesystem::remove(temporary, ec);
        return Fail(error, "unable to publish shader recipe export");
    }
    *exported_path = target;
    return true;
}

} // namespace xemu::shader_browser

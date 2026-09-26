#include "../../ui/xui/shader-browser-recipe-inspector.hh"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <tuple>

using namespace xemu::shader_browser;

static void AppendU32(std::vector<uint8_t> *bytes, uint32_t value)
{
    for (unsigned shift = 0; shift < 32; shift += 8) {
        bytes->push_back(static_cast<uint8_t>(value >> shift));
    }
}

int main()
{
    CanonicalRecipe recipe{};
    recipe.key.stage = Stage::Geometry;
    recipe.recipe_format_version = 1;
    recipe.bytes = {'N', 'V', '2', 'A', 3, 0};
    AppendU32(&recipe.bytes, 5);  // primitive mode
    AppendU32(&recipe.bytes, 1);  // front polygon mode
    AppendU32(&recipe.bytes, 2);  // back polygon mode
    recipe.bytes.insert(recipe.bytes.end(), {1, 0, 1});
    AppendU32(&recipe.bytes, static_cast<uint32_t>(-2));
    AppendU32(&recipe.bytes, 3);
    recipe.key.hash = ComputeShaderHash(1, recipe.key.stage,
                                        recipe.recipe_format_version,
                                        recipe.bytes.data(), recipe.bytes.size());
    ShaderScope second{};
    second.title_id = 0x4d530064;
    second.executable_fingerprint_version = 1;
    second.executable_fingerprint[0] = 0x55;
    ShaderScope first{};
    first.title_id = 0x12345678;
    recipe.scopes = {second, first, second};

    RecipeInspection inspection{};
    std::string error;
    assert(InspectCanonicalRecipe(recipe, &inspection, &error));
    assert(inspection.fields.size() == 8);
    assert(inspection.fields[0].name == "primitive_mode");
    assert(inspection.fields[0].value == "5");
    assert(inspection.fields[6].value == "-2");

    std::string exported;
    assert(SerializePortableRecipe(recipe, &exported, &error));
    assert(exported.find("xemu.shader-recipe-export.v1") != std::string::npos);
    assert(exported.find("4D530064") != std::string::npos);
    CanonicalRecipe parsed{};
    assert(ParsePortableRecipe(exported, &parsed, &error));
    assert(parsed.key == recipe.key);
    assert(parsed.bytes == recipe.bytes);
    assert(parsed.scopes.size() == 2);
    assert(parsed.scopes[0].title_id == first.title_id);
    assert(PortableRecipeFilename(recipe) ==
           "nv2a-v1-" + ShaderHashHex(recipe.key.hash) +
               "-geometry.xemu-shader.json");
    const auto root = std::filesystem::temp_directory_path() /
                      "xemu-recipe-export-test";
    std::filesystem::remove_all(root);
    std::filesystem::path exported_path;
    assert(ExportPortableRecipe(recipe, root, &exported_path, &error));
    assert(exported_path.parent_path() == root / "shader-exports");
    std::ifstream exported_file(exported_path);
    std::string on_disk((std::istreambuf_iterator<char>(exported_file)),
                        std::istreambuf_iterator<char>());
    assert(ParsePortableRecipe(on_disk, &parsed, &error));
    assert(parsed.bytes == recipe.bytes);
    std::filesystem::remove_all(root);

    CanonicalRecipe invalid = recipe;
    invalid.recipe_format_version = 2;
    assert(!InspectCanonicalRecipe(invalid, &inspection, &error));
    invalid = recipe;
    invalid.bytes[18] = 2;  // Boolean smooth_shading must be 0 or 1.
    assert(!InspectCanonicalRecipe(invalid, &inspection, &error));
    invalid = recipe;
    invalid.bytes.push_back(0);
    assert(!InspectCanonicalRecipe(invalid, &inspection, &error));
    assert(!SerializePortableRecipe(invalid, &exported, &error));

    // Encoder v1 fixtures with all fields zero cover the other stage shapes.
    for (const auto &[stage, code, size] :
         {std::tuple<Stage, uint8_t, size_t>{Stage::Vertex, 1, 37},
          {Stage::Pixel, 2, 312},
          {Stage::FixedFunction, 4, 160}}) {
        CanonicalRecipe zero{};
        zero.key.stage = stage;
        zero.recipe_format_version = 1;
        zero.bytes.resize(size);
        zero.bytes[0] = 'N';
        zero.bytes[1] = 'V';
        zero.bytes[2] = '2';
        zero.bytes[3] = 'A';
        zero.bytes[4] = code;
        zero.bytes[5] = stage == Stage::FixedFunction ? 1 : 0;
        zero.key.hash = ComputeShaderHash(1, stage, 1, zero.bytes.data(),
                                          zero.bytes.size());
        assert(InspectCanonicalRecipe(zero, &inspection, &error));
        assert(SerializePortableRecipe(zero, &exported, &error));
        assert(ParsePortableRecipe(exported, &parsed, &error));
        assert(parsed.bytes == zero.bytes);
    }

    std::cout << "shader browser recipe inspector tests passed\n";
    return 0;
}

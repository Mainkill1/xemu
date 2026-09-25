#include "../../ui/xui/shader-browser-model.hh"

#include <cassert>
#include <iostream>

using namespace xemu::shader_browser;

int main()
{
    const uint8_t recipe[] = {0x10, 0x20, 0x30, 0x40};
    ShaderHash computed = ComputeShaderHash(1, Stage::Pixel, 1,
                                            recipe, sizeof(recipe));
    ShaderHash computed_again = ComputeShaderHash(1, Stage::Pixel, 1,
                                                  recipe, sizeof(recipe));
    ShaderHash other_stage = ComputeShaderHash(1, Stage::Vertex, 1,
                                               recipe, sizeof(recipe));
    ShaderHash other_recipe_version = ComputeShaderHash(1, Stage::Pixel, 2,
                                                        recipe, sizeof(recipe));
    assert(computed == computed_again);
    assert(computed != other_stage);
    assert(computed != other_recipe_version);
    const std::string computed_hex = ShaderHashHex(computed);
    if (computed_hex != "dc91a6bac7390d00440a0bfe") {
        std::cerr << "unexpected shader hash fixture: " << computed_hex << "\n";
    }
    assert(computed_hex == "dc91a6bac7390d00440a0bfe");

    ShaderHash hash{};
    hash.version = 1;
    for (size_t i = 0; i < hash.bytes.size(); ++i) {
        hash.bytes[i] = static_cast<uint8_t>(i + 1);
    }

    assert(ShaderHashHex(hash) == "0102030405060708090a0b0c");
    std::string base32 = ShaderHashBase32(hash);
    assert(base32.size() == 20);
    assert(PortableShaderHash(hash).rfind("XSH1-", 0) == 0);

    std::string game_id = GameShaderId(0x4d530064, hash);
    assert(game_id.rfind("4D530064-", 0) == 0);
    assert(ShortGameShaderId(0x4d530064, hash).rfind("4D530064-", 0) == 0);

    Entry entry{};
    entry.key.hash = hash;
    entry.key.stage = Stage::Pixel;
    ShaderScope scope{};
    scope.title_id = 0x4d530064;
    entry.scopes.push_back(scope);
    entry.stored_in_database = true;
    entry.observed_in_session = true;
    entry.draw_count = 12;
    entry.last_frame = 100;
    entry.compile_cpu.AddSample(1000000);
    entry.compile_cpu.AddSample(3000000);

    Filter filter{};
    filter.query = "4D530064";
    assert(EntryMatches(entry, filter));
    filter.query = ShaderHashHex(hash).substr(0, 8);
    assert(EntryMatches(entry, filter));
    filter.query.clear();
    filter.source = SourceFilter::StoredDatabase;
    assert(EntryMatches(entry, filter));
    filter.source = SourceFilter::ThisSession;
    assert(EntryMatches(entry, filter));

    assert(FormatDurationNs(entry.compile_cpu).find("2.00 ms avg") !=
           std::string::npos);
    assert(FormatLastUsed(95, 100, true) == "5 frames ago");

    ShaderKey same = entry.key;
    assert(same == entry.key);
    assert(ShaderKeyHash{}(same) == ShaderKeyHash{}(entry.key));

    std::cout << "shader browser model tests passed\n";
    return 0;
}

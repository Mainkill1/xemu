#include "../../ui/xui/shader-browser-presets.hh"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>

using namespace xemu::shader_browser;
namespace fs = std::filesystem;

int main()
{
    fs::path path = fs::temp_directory_path() /
        "xemu-stage3-shader-preset-test.json";
    OverrideRule rule{};
    rule.id = 10;
    rule.title_id = 0x4d530064;
    rule.shader.hash.version = 1;
    rule.shader.hash.bytes[0] = 0xab;
    rule.shader.stage = Stage::Pixel;
    rule.origin = OverrideOrigin::Session;
    rule.action = OverrideAction::SkipDraw;
    rule.revision = 7;

    std::string error;
    assert(!ExportOverridePreset("", {rule}, &error));
    size_t count = 0;
    assert(!ImportOverridePreset("", nullptr, &count, &error));
    assert(ExportOverridePreset(path.u8string(), {rule}, &error));
    OverrideStore store;
    assert(ImportOverridePreset(path.u8string(), &store, &count, &error));
    assert(count == 1);
    OverrideStoreSnapshot snapshot;
    store.CopySnapshot(&snapshot);
    assert(snapshot.rules.size() == 1);
    assert(snapshot.rules[0].origin == OverrideOrigin::Imported);
    assert(snapshot.rules[0].shader == rule.shader);
    assert(snapshot.rules[0].title_id == rule.title_id);
    assert(snapshot.rules[0].action == rule.action);

    assert(!ImportOverridePreset(path.u8string(), &store, &count, &error));
    store.CopySnapshot(&snapshot);
    assert(snapshot.rules.size() == 1);

    nlohmann::json invalid_stage;
    {
        std::ifstream input(path);
        input >> invalid_stage;
    }
    invalid_stage["rules"][0]["stage"] = 99;
    {
        std::ofstream output(path, std::ios::trunc);
        output << invalid_stage;
    }
    OverrideStore invalid;
    assert(!ImportOverridePreset(path.u8string(), &invalid, &count, &error));
    invalid.CopySnapshot(&snapshot);
    assert(snapshot.rules.empty());

    {
        std::ofstream output(path, std::ios::trunc);
        output << "{\"format\":1,\"rules\":[{}]}";
    }
    assert(!ImportOverridePreset(path.u8string(), &invalid, &count, &error));
    invalid.CopySnapshot(&snapshot);
    assert(snapshot.rules.empty());

    std::error_code ec;
    fs::remove(path, ec);
    std::cout << "shader browser preset tests passed\n";
}

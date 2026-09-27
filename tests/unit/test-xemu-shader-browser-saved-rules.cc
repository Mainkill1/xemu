#include "../../ui/xui/shader-browser-saved-rules.hh"

#include <sqlite3.h>

#include <cassert>
#include <filesystem>
#include <iostream>

using namespace xemu::shader_browser;
namespace fs = std::filesystem;

int main()
{
    fs::path base = fs::temp_directory_path() /
        "xemu-stage3-saved-rules-test";
    std::error_code ec;
    fs::remove_all(base, ec);
    fs::create_directories(base);

    OverrideStore store;
    SavedOverrideRules saved;
    std::string error;
    assert(saved.Configure(base.u8string(), true, &store, &error));

    OverrideRule rule{};
    rule.id = UINT64_C(0xf123456789abcdef);
    rule.title_id = 0x4d530064;
    rule.shader.hash.version = 1;
    rule.shader.stage = Stage::Pixel;
    for (size_t i = 0; i < rule.shader.hash.bytes.size(); ++i) {
        rule.shader.hash.bytes[i] = static_cast<uint8_t>(i + 10);
    }
    rule.restrict_build = true;
    rule.executable_fingerprint_version = 2;
    rule.executable_fingerprint[0] = 0xab;
    rule.origin = OverrideOrigin::Saved;
    rule.priority = 42;
    rule.action = OverrideAction::SkipDraw;
    rule.draw_condition.mask = DrawConditionElementCount;
    rule.draw_condition.element_count_min = 4;
    rule.draw_condition.element_count_max = 20;
    rule.revision = 5;
    assert(saved.Save(rule, &store, &error));

    std::string encoded;
    assert(EncodeSavedOverrideRule(rule, &encoded, &error));
    OverrideRule decoded{};
    assert(DecodeSavedOverrideRule(encoded, &decoded, &error));
    assert(decoded.id == rule.id && decoded.title_id == rule.title_id);
    assert(decoded.shader == rule.shader);
    assert(decoded.executable_fingerprint == rule.executable_fingerprint);
    assert(decoded.draw_condition.element_count_min == 4);
    assert(!DecodeSavedOverrideRule("{}", &decoded, &error));

    OverrideStore reloaded;
    SavedOverrideRules second;
    assert(second.Configure(base.u8string(), true, &reloaded, &error));
    OverrideStoreSnapshot snapshot;
    reloaded.CopySnapshot(&snapshot);
    assert(snapshot.rules.size() == 1 && snapshot.rules[0].id == rule.id);
    assert(snapshot.rules[0].origin == OverrideOrigin::Saved);

    assert(second.Configure(base.u8string(), false, &reloaded, &error));
    reloaded.CopySnapshot(&snapshot);
    assert(snapshot.rules.empty());
    assert(second.Configure(base.u8string(), true, &reloaded, &error));
    reloaded.CopySnapshot(&snapshot);
    assert(snapshot.rules.size() == 1);

    assert(second.Remove(rule.id, &reloaded, &error));
    reloaded.CopySnapshot(&snapshot);
    assert(snapshot.rules.empty());
    assert(second.Configure(base.u8string(), false, &reloaded, &error));
    assert(second.Configure(base.u8string(), true, &reloaded, &error));
    reloaded.CopySnapshot(&snapshot);
    assert(snapshot.rules.empty());

    assert(second.Save(rule, &reloaded, &error));
    saved.Close();
    second.Close();
    sqlite3 *db = nullptr;
    fs::path path = base / "shader-browser.db";
    assert(sqlite3_open(path.string().c_str(), &db) == SQLITE_OK);
    assert(sqlite3_exec(db,
        "INSERT INTO shader_override_rules(rule_id,rule_json) "
        "VALUES ('22','{}')", nullptr, nullptr, nullptr) == SQLITE_OK);
    sqlite3_close(db);

    SavedOverrideRules third;
    OverrideStore recovered;
    assert(!third.Configure(base.u8string(), true, &recovered, &error));
    recovered.CopySnapshot(&snapshot);
    assert(snapshot.rules.size() == 1 && snapshot.rules[0].id == rule.id);
    OverrideRule session = rule;
    session.id = 23;
    session.origin = OverrideOrigin::Session;
    assert(recovered.UpsertRule(session, &error));
    assert(third.Configure(base.u8string(), false, &recovered, &error));
    recovered.CopySnapshot(&snapshot);
    assert(snapshot.rules.size() == 1 && snapshot.rules[0].id == session.id);

    third.Close();
    fs::remove_all(base, ec);
    std::cout << "shader browser saved rule tests passed\n";
}

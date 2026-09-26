// SPDX-License-Identifier: GPL-2.0-or-later
#include "shader-browser-saved-rules.hh"

#include <nlohmann/json.hpp>
#include <sqlite3.h>

#include <algorithm>
#include <filesystem>
#include <limits>

namespace xemu::shader_browser {
namespace {

std::string Hex(const uint8_t *bytes, size_t size)
{
    static constexpr char digits[] = "0123456789abcdef";
    std::string result;
    result.reserve(size * 2);
    for (size_t i = 0; i < size; ++i) {
        result.push_back(digits[bytes[i] >> 4]);
        result.push_back(digits[bytes[i] & 15]);
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

bool ReadHex(const std::string &text, uint8_t *bytes, size_t size)
{
    if (text.size() != size * 2) return false;
    for (size_t i = 0; i < size; ++i) {
        int high = HexDigit(text[i * 2]);
        int low = HexDigit(text[i * 2 + 1]);
        if (high < 0 || low < 0) return false;
        bytes[i] = static_cast<uint8_t>((high << 4) | low);
    }
    return true;
}

bool ReadU64(const std::string &text, uint64_t *value)
{
    if (text.empty()) return false;
    uint64_t result = 0;
    for (char c : text) {
        if (c < '0' || c > '9' ||
            result > (std::numeric_limits<uint64_t>::max() - (c - '0')) / 10) {
            return false;
        }
        result = result * 10 + (c - '0');
    }
    *value = result;
    return true;
}

bool ValidRule(const OverrideRule &rule)
{
    const uint32_t stage = static_cast<uint32_t>(rule.shader.stage);
    const uint32_t action = static_cast<uint32_t>(rule.action);
    return rule.id && rule.title_id && rule.shader.hash.version &&
           stage >= static_cast<uint32_t>(Stage::Vertex) &&
           stage <= static_cast<uint32_t>(Stage::FixedFunction) &&
           action <= static_cast<uint32_t>(OverrideAction::Replacement) &&
           (!rule.restrict_build || rule.executable_fingerprint_version) &&
           (rule.action != OverrideAction::Replacement ||
            rule.replacement_id) &&
           !(rule.draw_condition.mask &
             ~(DrawConditionElementCount | DrawConditionElementRange |
               DrawConditionPrimitive)) &&
           (!(rule.draw_condition.mask & DrawConditionElementCount) ||
            rule.draw_condition.element_count_min <=
                rule.draw_condition.element_count_max);
}

void SetSqlError(sqlite3 *db, std::string *error)
{
    if (error) *error = db ? sqlite3_errmsg(db) :
                             "Unable to open saved-rule database";
}

} // namespace

bool EncodeSavedOverrideRule(const OverrideRule &rule,
                             std::string *text, std::string *error)
{
    if (!text || rule.origin != OverrideOrigin::Saved || !ValidRule(rule)) {
        if (error) *error = "Invalid saved shader override rule";
        return false;
    }
    const DrawCondition &draw = rule.draw_condition;
    nlohmann::json json = {
        {"format", 1},
        {"id", std::to_string(rule.id)},
        {"enabled", rule.enabled},
        {"title_id", rule.title_id},
        {"identity_version", rule.shader.hash.version},
        {"shader_hash", Hex(rule.shader.hash.bytes.data(),
                             rule.shader.hash.bytes.size())},
        {"stage", static_cast<uint32_t>(rule.shader.stage)},
        {"restrict_build", rule.restrict_build},
        {"fingerprint_version", rule.executable_fingerprint_version},
        {"fingerprint", Hex(rule.executable_fingerprint.data(),
                             rule.executable_fingerprint.size())},
        {"priority", rule.priority},
        {"action", static_cast<uint32_t>(rule.action)},
        {"replacement_id", std::to_string(rule.replacement_id)},
        {"revision", std::to_string(rule.revision)},
        {"draw", {
            {"mask", draw.mask},
            {"element_count_min", draw.element_count_min},
            {"element_count_max", draw.element_count_max},
            {"min_element", draw.min_element},
            {"max_element", draw.max_element},
            {"primitive_mode", draw.primitive_mode},
        }},
    };
    *text = json.dump();
    if (error) error->clear();
    return true;
}

bool DecodeSavedOverrideRule(const std::string &text,
                             OverrideRule *rule, std::string *error)
{
    if (!rule || text.size() > 64U * 1024U) {
        if (error) *error = "Saved rule is unavailable or too large";
        return false;
    }
    try {
        nlohmann::json json = nlohmann::json::parse(text);
        if (json.at("format").get<uint32_t>() != 1) {
            if (error) *error = "Unsupported saved rule format";
            return false;
        }
        OverrideRule next{};
        next.origin = OverrideOrigin::Saved;
        if (!ReadU64(json.at("id").get<std::string>(), &next.id) ||
            !ReadU64(json.at("replacement_id").get<std::string>(),
                     &next.replacement_id) ||
            !ReadU64(json.at("revision").get<std::string>(),
                     &next.revision)) {
            if (error) *error = "Saved rule has an invalid 64-bit value";
            return false;
        }
        next.enabled = json.at("enabled").get<bool>();
        next.title_id = json.at("title_id").get<uint32_t>();
        next.shader.hash.version =
            json.at("identity_version").get<uint32_t>();
        next.shader.stage = static_cast<Stage>(json.at("stage").get<uint32_t>());
        next.restrict_build = json.at("restrict_build").get<bool>();
        next.executable_fingerprint_version =
            json.at("fingerprint_version").get<uint32_t>();
        next.priority = json.at("priority").get<int32_t>();
        next.action = static_cast<OverrideAction>(
            json.at("action").get<uint32_t>());
        if (!ReadHex(json.at("shader_hash").get<std::string>(),
                     next.shader.hash.bytes.data(),
                     next.shader.hash.bytes.size()) ||
            !ReadHex(json.at("fingerprint").get<std::string>(),
                     next.executable_fingerprint.data(),
                     next.executable_fingerprint.size())) {
            if (error) *error = "Saved rule has an invalid hash";
            return false;
        }
        const auto &draw = json.at("draw");
        next.draw_condition.mask = draw.at("mask").get<uint32_t>();
        next.draw_condition.element_count_min =
            draw.at("element_count_min").get<uint32_t>();
        next.draw_condition.element_count_max =
            draw.at("element_count_max").get<uint32_t>();
        next.draw_condition.min_element =
            draw.at("min_element").get<uint32_t>();
        next.draw_condition.max_element =
            draw.at("max_element").get<uint32_t>();
        next.draw_condition.primitive_mode =
            draw.at("primitive_mode").get<uint32_t>();
        if (!ValidRule(next)) {
            if (error) *error = "Saved rule fields are invalid";
            return false;
        }
        *rule = next;
        if (error) error->clear();
        return true;
    } catch (const nlohmann::json::exception &exception) {
        if (error) *error = exception.what();
        return false;
    }
}

SavedOverrideRules::~SavedOverrideRules()
{
    Close();
}

void SavedOverrideRules::Close()
{
    if (db_) sqlite3_close(db_);
    db_ = nullptr;
    base_path_.clear();
}

bool SavedOverrideRules::Configure(const std::string &base_path, bool enabled,
                                   OverrideStore *store, std::string *error)
{
    if (!store) {
        if (error) *error = "Override store is unavailable";
        return false;
    }
    if (!enabled) {
        Close();
        store->ClearSavedRules();
        if (error) error->clear();
        return true;
    }
    if (db_ && base_path_ == base_path) return true;
    Close();
    store->ClearSavedRules();
    if (base_path.empty()) {
        if (error) *error = "Configuration directory is unavailable";
        return false;
    }
    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::u8path(base_path), ec);
    if (ec) {
        if (error) *error = ec.message();
        return false;
    }
    std::string path = (std::filesystem::u8path(base_path) /
                        "shader-browser.db").u8string();
    if (sqlite3_open_v2(path.c_str(), &db_,
                        SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE |
                            SQLITE_OPEN_FULLMUTEX,
                        nullptr) != SQLITE_OK) {
        SetSqlError(db_, error);
        Close();
        return false;
    }
    sqlite3_busy_timeout(db_, 2500);
    const char *schema =
        "CREATE TABLE IF NOT EXISTS shader_override_rules("
        "rule_id TEXT PRIMARY KEY, rule_json TEXT NOT NULL)";
    if (sqlite3_exec(db_, schema, nullptr, nullptr, nullptr) != SQLITE_OK) {
        SetSqlError(db_, error);
        Close();
        return false;
    }
    base_path_ = base_path;
    return Load(store, error);
}

bool SavedOverrideRules::Load(OverrideStore *store, std::string *error)
{
    store->ClearSavedRules();
    sqlite3_stmt *statement = nullptr;
    if (sqlite3_prepare_v2(db_,
                           "SELECT rule_id,rule_json FROM shader_override_rules",
                           -1, &statement, nullptr) != SQLITE_OK) {
        SetSqlError(db_, error);
        return false;
    }
    bool valid = true;
    int result;
    while ((result = sqlite3_step(statement)) == SQLITE_ROW) {
        const char *id = reinterpret_cast<const char *>(
            sqlite3_column_text(statement, 0));
        const char *text = reinterpret_cast<const char *>(
            sqlite3_column_text(statement, 1));
        OverrideRule rule{};
        std::string row_error;
        if (!id || !text ||
            !DecodeSavedOverrideRule(text, &rule, &row_error) ||
            std::to_string(rule.id) != id ||
            !store->UpsertRule(rule, &row_error)) {
            if (valid && error) {
                *error = "Skipped an invalid saved shader rule: " + row_error;
            }
            valid = false;
        }
    }
    if (result != SQLITE_DONE) {
        SetSqlError(db_, error);
        valid = false;
    }
    sqlite3_finalize(statement);
    if (valid && error) error->clear();
    return valid;
}

bool SavedOverrideRules::Save(const OverrideRule &rule,
                              OverrideStore *store, std::string *error)
{
    if (!db_ || !store) {
        if (error) *error = "Saved rule database is disabled";
        return false;
    }
    std::string text;
    if (!EncodeSavedOverrideRule(rule, &text, error)) return false;

    OverrideStoreSnapshot previous;
    store->CopySnapshot(&previous);
    if (!store->UpsertRule(rule, error)) return false;

    sqlite3_stmt *statement = nullptr;
    const char *sql = "INSERT OR REPLACE INTO shader_override_rules "
                      "(rule_id,rule_json) VALUES (?,?)";
    bool ok = sqlite3_prepare_v2(db_, sql, -1, &statement, nullptr) == SQLITE_OK;
    if (ok) {
        std::string id = std::to_string(rule.id);
        sqlite3_bind_text(statement, 1, id.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(statement, 2, text.c_str(), -1, SQLITE_TRANSIENT);
        ok = sqlite3_step(statement) == SQLITE_DONE;
    }
    if (!ok) {
        SetSqlError(db_, error);
        auto old = std::find_if(previous.rules.begin(), previous.rules.end(),
                                [&](const OverrideRule &candidate) {
                                    return candidate.id == rule.id;
                                });
        if (old == previous.rules.end()) {
            store->RemoveRule(rule.id);
        } else {
            std::string ignored;
            store->UpsertRule(*old, &ignored);
        }
    } else if (error) {
        error->clear();
    }
    if (statement) sqlite3_finalize(statement);
    return ok;
}

bool SavedOverrideRules::Remove(uint64_t rule_id, OverrideStore *store,
                                std::string *error)
{
    if (!db_ || !store) {
        if (error) *error = "Saved rule database is disabled";
        return false;
    }
    sqlite3_stmt *statement = nullptr;
    bool ok = sqlite3_prepare_v2(db_,
                                 "DELETE FROM shader_override_rules WHERE rule_id=?",
                                 -1, &statement, nullptr) == SQLITE_OK;
    if (ok) {
        std::string id = std::to_string(rule_id);
        sqlite3_bind_text(statement, 1, id.c_str(), -1, SQLITE_TRANSIENT);
        ok = sqlite3_step(statement) == SQLITE_DONE;
    }
    if (!ok) SetSqlError(db_, error);
    if (statement) sqlite3_finalize(statement);
    if (!ok) return false;
    OverrideStoreSnapshot snapshot;
    store->CopySnapshot(&snapshot);
    auto current = std::find_if(snapshot.rules.begin(), snapshot.rules.end(),
                                [&](const OverrideRule &candidate) {
                                    return candidate.id == rule_id;
                                });
    if (current != snapshot.rules.end() &&
        current->origin == OverrideOrigin::Saved) {
        store->RemoveRule(rule_id);
    }
    if (error) error->clear();
    return true;
}

SavedOverrideRules &GetSavedOverrideRules()
{
    static SavedOverrideRules saved;
    return saved;
}

} // namespace xemu::shader_browser

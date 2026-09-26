// SPDX-License-Identifier: GPL-2.0-or-later
#include "shader-browser-presets.hh"
#include "shader-browser-saved-rules.hh"

#include <nlohmann/json.hpp>
#include <xxhash.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <unordered_set>

namespace xemu::shader_browser {
namespace {

constexpr size_t kMaxPresetBytes = 1024U * 1024U;
constexpr size_t kMaxPresetRules = 512;
constexpr uint64_t kImportedRuleSeed = UINT64_C(0x58454d5550525354);

void SetError(std::string *error, const std::string &message)
{
    if (error) *error = message;
}

} // namespace

bool ExportOverridePreset(const std::string &path,
                          const std::vector<OverrideRule> &rules,
                          std::string *error)
{
    if (path.empty() || rules.size() > kMaxPresetRules) {
        SetError(error, "Preset path is empty or has too many rules");
        return false;
    }
    nlohmann::json document = {
        {"format", 1}, {"rules", nlohmann::json::array()},
    };
    for (const OverrideRule &rule : rules) {
        OverrideRule portable = rule;
        portable.origin = OverrideOrigin::Saved;
        std::string encoded;
        if (!EncodeSavedOverrideRule(portable, &encoded, error)) return false;
        document["rules"].push_back(nlohmann::json::parse(encoded));
    }
    std::string text = document.dump(2) + "\n";
    if (text.size() > kMaxPresetBytes) {
        SetError(error, "Preset exceeds the 1 MiB limit");
        return false;
    }
    std::filesystem::path destination = std::filesystem::u8path(path);
    std::error_code ec;
    if (!destination.parent_path().empty()) {
        std::filesystem::create_directories(destination.parent_path(), ec);
        if (ec) {
            SetError(error, "Unable to create preset directory: " +
                            ec.message());
            return false;
        }
    }
    std::filesystem::path temporary = destination;
    temporary += ".tmp";
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        output.write(text.data(), static_cast<std::streamsize>(text.size()));
        output.close();
        if (!output.good()) {
            SetError(error, "Unable to write preset file");
            std::filesystem::remove(temporary, ec);
            return false;
        }
    }
    std::filesystem::rename(temporary, destination, ec);
    if (ec) {
        SetError(error, "Unable to publish preset file: " + ec.message());
        std::filesystem::remove(temporary, ec);
        return false;
    }
    if (error) error->clear();
    return true;
}

bool ImportOverridePreset(const std::string &path, OverrideStore *store,
                          size_t *imported_count, std::string *error)
{
    if (imported_count) *imported_count = 0;
    if (!store || path.empty()) {
        SetError(error, "Preset path or override store is unavailable");
        return false;
    }
    std::error_code ec;
    auto size = std::filesystem::file_size(std::filesystem::u8path(path), ec);
    if (ec || size > kMaxPresetBytes) {
        SetError(error, ec ? "Unable to inspect preset file: " + ec.message() :
                             "Preset exceeds the 1 MiB limit");
        return false;
    }
    std::ifstream input(std::filesystem::u8path(path), std::ios::binary);
    if (!input) {
        SetError(error, "Unable to open preset file");
        return false;
    }
    std::string text(std::istreambuf_iterator<char>{input}, {});
    if (!input.eof() && input.fail()) {
        SetError(error, "Unable to read preset file");
        return false;
    }

    std::vector<OverrideRule> pending;
    std::unordered_set<uint64_t> ids;
    try {
        nlohmann::json document = nlohmann::json::parse(text);
        if (document.at("format").get<uint32_t>() != 1 ||
            !document.at("rules").is_array() ||
            document.at("rules").size() > kMaxPresetRules) {
            SetError(error, "Preset format or rule count is unsupported");
            return false;
        }
        for (const auto &item : document.at("rules")) {
            std::string encoded = item.dump();
            OverrideRule rule{};
            if (!DecodeSavedOverrideRule(encoded, &rule, error)) return false;
            rule.origin = OverrideOrigin::Imported;
            rule.id = XXH3_64bits_withSeed(encoded.data(), encoded.size(),
                                           kImportedRuleSeed);
            if (!rule.id) rule.id = 1;
            if (!ids.insert(rule.id).second) {
                SetError(error, "Preset contains duplicate rules");
                return false;
            }
            pending.push_back(rule);
        }
    } catch (const nlohmann::json::exception &exception) {
        SetError(error, exception.what());
        return false;
    }

    OverrideStoreSnapshot existing;
    store->CopySnapshot(&existing);
    for (const OverrideRule &rule : existing.rules) {
        if (ids.count(rule.id)) {
            SetError(error, "An imported rule already exists; remove it "
                            "before importing this preset again");
            return false;
        }
    }
    std::vector<uint64_t> inserted;
    for (const OverrideRule &rule : pending) {
        if (!store->UpsertRule(rule, error)) {
            for (uint64_t id : inserted) store->RemoveRule(id);
            return false;
        }
        inserted.push_back(rule.id);
    }
    if (imported_count) *imported_count = pending.size();
    if (error) error->clear();
    return true;
}

} // namespace xemu::shader_browser

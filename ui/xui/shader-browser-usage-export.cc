// SPDX-License-Identifier: GPL-2.0-or-later
#include "shader-browser-usage-export.hh"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

namespace xemu::shader_browser {
namespace {

std::string Number(uint64_t value)
{
    return std::to_string(value);
}

std::string JsonString(const std::string &value)
{
    std::string result = "\"";
    for (unsigned char c : value) {
        switch (c) {
        case '"': result += "\\\""; break;
        case '\\': result += "\\\\"; break;
        case '\n': result += "\\n"; break;
        case '\r': result += "\\r"; break;
        case '\t': result += "\\t"; break;
        default:
            if (c < 0x20) {
                char buffer[7];
                std::snprintf(buffer, sizeof(buffer), "\\u%04x", c);
                result += buffer;
            } else {
                result.push_back(static_cast<char>(c));
            }
        }
    }
    return result + '"';
}

std::vector<const Entry *> Ordered(const Snapshot &snapshot)
{
    std::vector<const Entry *> entries;
    entries.reserve(snapshot.entries.size());
    for (const Entry &entry : snapshot.entries) entries.push_back(&entry);
    std::sort(entries.begin(), entries.end(),
              [](const Entry *a, const Entry *b) { return a->key < b->key; });
    return entries;
}

std::string ScopeText(const Entry &entry)
{
    std::string result;
    std::vector<ShaderScope> scopes = entry.scopes;
    std::sort(scopes.begin(), scopes.end());
    for (const ShaderScope &scope : scopes) {
        if (!result.empty()) result += ';';
        result += FormatTitleId(scope.title_id);
        result += ':' + Number(scope.executable_fingerprint_version) + ':';
        static constexpr char hex[] = "0123456789abcdef";
        for (uint8_t byte : scope.executable_fingerprint) {
            result.push_back(hex[byte >> 4]);
            result.push_back(hex[byte & 15]);
        }
    }
    return result;
}

std::string Average(const DurationStats &stats)
{
    return stats.samples ? Number(stats.total_ns / stats.samples) : "";
}

std::string JsonOptional(const std::string &value)
{
    return value.empty() ? "null" : value;
}

} // namespace

std::string QuoteUsageCsvField(const std::string &value)
{
    if (value.find_first_of(",\"\r\n") == std::string::npos) return value;
    std::string result = "\"";
    for (char c : value) {
        if (c == '"') result.push_back('"');
        result.push_back(c);
    }
    return result + '"';
}

std::string SerializeUsageCsv(const Snapshot &snapshot)
{
    std::string out = "stage,shader_hash,scopes,observed_in_session,draw_count,last_frame,frames_ago,compile_cpu_samples,compile_cpu_average_ns,pipeline_cpu_average_ns,prepare_cpu_average_ns,foreground_stall_cpu_average_ns,gpu_execution_average_ns,route,status\n";
    for (const Entry *entry : Ordered(snapshot)) {
        const bool observed = entry->observed_in_session;
        const std::string last = observed ? Number(entry->last_frame) : "";
        const std::string age = observed && entry->last_frame <= snapshot.current_frame ?
            Number(snapshot.current_frame - entry->last_frame) : "";
        out += QuoteUsageCsvField(StageLabel(entry->key.stage)) + ',' +
               QuoteUsageCsvField(PortableShaderHash(entry->key.hash)) + ',' +
               QuoteUsageCsvField(ScopeText(*entry)) + ',' + (observed ? "true" : "false") + ',' +
               (observed ? Number(entry->draw_count) : "") + ',' + last + ',' + age + ',' +
               Number(entry->compile_cpu.samples) + ',' + Average(entry->compile_cpu) + ',' +
               Average(entry->pipeline_cpu) + ',' + Average(entry->prepare_cpu) + ',' +
               Average(entry->foreground_stall_cpu) + ',' + Average(entry->gpu_execution) + ',' +
               QuoteUsageCsvField(RouteLabel(entry->route)) + ',' +
               QuoteUsageCsvField(StatusLabel(entry->status)) + '\n';
    }
    return out;
}

std::string SerializeUsageJson(const Snapshot &snapshot)
{
    std::string out = "{\"schema\":\"xemu.shader-usage.v1\",\"snapshot_sequence\":" +
        Number(snapshot.snapshot_sequence) + ",\"session_epoch\":" +
        Number(snapshot.session_epoch) + ",\"current_frame\":" +
        Number(snapshot.current_frame) + ",\"entries\":[";
    bool first = true;
    for (const Entry *entry : Ordered(snapshot)) {
        if (!first) out += ',';
        first = false;
        const bool observed = entry->observed_in_session;
        out += "{\"stage\":" + JsonString(StageLabel(entry->key.stage)) +
            ",\"shader_hash\":" + JsonString(PortableShaderHash(entry->key.hash)) +
            ",\"scopes\":" + JsonString(ScopeText(*entry)) +
            ",\"observed_in_session\":" + (observed ? "true" : "false") +
            ",\"draw_count\":" + JsonOptional(observed ? Number(entry->draw_count) : "") +
            ",\"last_frame\":" + JsonOptional(observed ? Number(entry->last_frame) : "") +
            ",\"frames_ago\":" + JsonOptional(observed && entry->last_frame <= snapshot.current_frame ?
                                                    Number(snapshot.current_frame - entry->last_frame) : "") +
            ",\"compile_cpu_samples\":" + Number(entry->compile_cpu.samples) +
            ",\"compile_cpu_average_ns\":" + JsonOptional(Average(entry->compile_cpu)) +
            ",\"pipeline_cpu_average_ns\":" + JsonOptional(Average(entry->pipeline_cpu)) +
            ",\"prepare_cpu_average_ns\":" + JsonOptional(Average(entry->prepare_cpu)) +
            ",\"foreground_stall_cpu_average_ns\":" + JsonOptional(Average(entry->foreground_stall_cpu)) +
            ",\"gpu_execution_average_ns\":" + JsonOptional(Average(entry->gpu_execution)) +
            ",\"route\":" + JsonString(RouteLabel(entry->route)) +
            ",\"status\":" + JsonString(StatusLabel(entry->status)) + '}';
    }
    return out + "]}\n";
}

bool ExportUsageSnapshot(const Snapshot &snapshot,
                         const std::filesystem::path &config_directory,
                         bool json, std::filesystem::path *exported_path,
                         std::string *error)
{
    if (config_directory.empty()) {
        if (error) *error = "Shader export directory unavailable";
        return false;
    }
    std::error_code ec;
    const auto root = config_directory / "shader-exports";
    std::filesystem::create_directories(root, ec);
    if (ec) {
        if (error) *error = "Unable to create shader export directory: " + ec.message();
        return false;
    }
    const std::string body = json ? SerializeUsageJson(snapshot) :
                                    SerializeUsageCsv(snapshot);
    for (unsigned int suffix = 0; suffix < 1000; ++suffix) {
        std::string name = "shader-usage-session-" +
            std::to_string(snapshot.session_epoch) + "-snapshot-" +
            std::to_string(snapshot.snapshot_sequence) + "-" +
            std::to_string(suffix) + (json ? ".json" : ".csv");
        auto path = root / name;
        if (std::filesystem::exists(path, ec)) continue;
        if (ec) break;
        auto temporary = path;
        temporary += ".tmp";
        if (std::filesystem::exists(temporary, ec)) continue;
        if (ec) break;
        {
            std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
            if (!stream || !(stream << body) || !(stream.flush())) {
                std::filesystem::remove(temporary, ec);
                if (error) *error = "Unable to write shader usage export";
                return false;
            }
        }
        std::filesystem::rename(temporary, path, ec);
        if (!ec) {
            if (exported_path) *exported_path = path;
            return true;
        }
        std::filesystem::remove(temporary, ec);
        break;
    }
    if (error) *error = "Unable to choose a unique shader usage export path";
    return false;
}

} // namespace xemu::shader_browser

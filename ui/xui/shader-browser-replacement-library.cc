// SPDX-License-Identifier: GPL-2.0-or-later
#include "shader-browser-replacement-library.hh"

#include <xxhash.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <limits>
#include <map>
#include <sstream>
#include <unordered_map>

namespace xemu::shader_browser {
namespace fs = std::filesystem;
namespace {

constexpr size_t kMaxManifestBytes = 64U * 1024U;

std::string Trim(std::string value)
{
    auto whitespace = [](unsigned char c) { return std::isspace(c); };
    value.erase(value.begin(), std::find_if_not(value.begin(), value.end(), whitespace));
    value.erase(std::find_if_not(value.rbegin(), value.rend(), whitespace).base(),
                value.end());
    return value;
}

std::string Lower(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return value;
}

bool ReadFile(const fs::path &path, size_t limit, std::string *content,
              std::string *error)
{
    std::error_code ec;
    uintmax_t size = fs::file_size(path, ec);
    if (ec || size > limit) {
        if (error) {
            *error = ec ? "Unable to inspect file: " + ec.message() :
                          "File exceeds size limit";
        }
        return false;
    }
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        if (error) *error = "Unable to open file";
        return false;
    }
    content->assign(std::istreambuf_iterator<char>(file),
                    std::istreambuf_iterator<char>());
    if (!file.eof() && file.fail()) {
        if (error) *error = "Unable to read file";
        return false;
    }
    if (content->find('\0') != std::string::npos) {
        if (error) *error = "Text file contains embedded NUL";
        return false;
    }
    return true;
}

bool ParseManifest(const std::string &text,
                   std::map<std::string, std::string> *values,
                   std::string *error)
{
    std::istringstream input(text);
    std::string line;
    bool in_replacement = false;
    size_t line_number = 0;
    while (std::getline(input, line)) {
        ++line_number;
        line = Trim(line);
        if (line.empty() || line[0] == '#' || line[0] == ';') {
            continue;
        }
        if (line.front() == '[' && line.back() == ']') {
            in_replacement = Lower(Trim(line.substr(1, line.size() - 2))) ==
                             "replacement";
            continue;
        }
        if (!in_replacement) {
            continue;
        }
        size_t equals = line.find('=');
        if (equals == std::string::npos) {
            if (error) {
                *error = "Manifest line " + std::to_string(line_number) +
                         " is missing '='";
            }
            return false;
        }
        std::string key = Lower(Trim(line.substr(0, equals)));
        std::string value = Trim(line.substr(equals + 1));
        if (key.empty() || values->count(key)) {
            if (error) {
                *error = key.empty() ? "Manifest contains an empty key" :
                                       "Manifest repeats key '" + key + "'";
            }
            return false;
        }
        values->emplace(std::move(key), std::move(value));
    }
    return true;
}

bool IsValidLogicalId(const std::string &value)
{
    if (value.empty() || value.size() > 128) {
        return false;
    }
    return std::all_of(value.begin(), value.end(), [](unsigned char c) {
        return std::isalnum(c) || c == '-' || c == '_' || c == '.';
    });
}

bool IsValidEntryPoint(const std::string &value)
{
    if (value.empty() || value.size() > 128 ||
        !(std::isalpha(static_cast<unsigned char>(value.front())) ||
          value.front() == '_')) {
        return false;
    }
    return std::all_of(value.begin() + 1, value.end(),
                       [](unsigned char c) {
                           return std::isalnum(c) || c == '_';
                       });
}

bool ParseU32(const std::string &value, uint32_t *result)
{
    if (value.empty()) return false;
    uint64_t parsed = 0;
    for (unsigned char c : value) {
        if (!std::isdigit(c)) return false;
        parsed = parsed * 10 + (c - '0');
        if (parsed > std::numeric_limits<uint32_t>::max()) return false;
    }
    *result = static_cast<uint32_t>(parsed);
    return true;
}

bool HasPathPrefix(const fs::path &path, const fs::path &root)
{
    auto path_it = path.begin();
    auto root_it = root.begin();
    for (; root_it != root.end(); ++root_it, ++path_it) {
        if (path_it == path.end() || *path_it != *root_it) {
            return false;
        }
    }
    return true;
}

bool ResolvePayloadPath(const fs::path &package_root,
                        const std::string &relative_text, fs::path *resolved,
                        std::string *error)
{
    fs::path relative = fs::u8path(relative_text);
    if (relative.empty() || relative.is_absolute()) {
        if (error) *error = "Payload path must be relative";
        return false;
    }
    for (const fs::path &part : relative) {
        if (part == "..") {
            if (error) *error = "Payload path may not contain '..'";
            return false;
        }
    }
    std::error_code ec;
    fs::path canonical_root = fs::weakly_canonical(package_root, ec);
    if (ec) {
        if (error) *error = "Unable to canonicalize package directory";
        return false;
    }
    fs::path canonical_file = fs::weakly_canonical(package_root / relative, ec);
    if (ec || !HasPathPrefix(canonical_file, canonical_root) ||
        !fs::is_regular_file(canonical_file, ec)) {
        if (error) *error = "Payload path escapes the package or is not a file";
        return false;
    }
    *resolved = canonical_file;
    return true;
}

void AppendU32(std::string *frame, uint32_t value)
{
    for (unsigned shift = 0; shift < 32; shift += 8) {
        frame->push_back(static_cast<char>((value >> shift) & 0xffU));
    }
}

void AppendString(std::string *frame, const std::string &value)
{
    AppendU32(frame, static_cast<uint32_t>(value.size()));
    frame->append(value);
}

void StoreU64Le(std::array<uint8_t, 32> *target, size_t offset,
                uint64_t value)
{
    for (unsigned shift = 0; shift < 64; shift += 8) {
        (*target)[offset++] = static_cast<uint8_t>((value >> shift) & 0xffU);
    }
}

} // namespace

void ReplacementLibrary::Configure(const std::string &base_path)
{
    std::lock_guard<std::mutex> lock(mutex_);
    root_path_ = (fs::u8path(base_path) / "shader-replacements").u8string();
    ++generation_;
}

bool ReplacementLibrary::EnsureRoot(std::string *error)
{
    std::string root;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        root = root_path_;
    }
    if (root.empty()) {
        if (error) *error = "Replacement root is not configured";
        return false;
    }
    std::error_code ec;
    fs::create_directories(fs::u8path(root), ec);
    if (ec) {
        if (error) *error = "Unable to create replacement root: " + ec.message();
        return false;
    }
    return true;
}

bool ReplacementLibrary::LoadPackage(const std::string &directory,
                                     ReplacementPayload *payload,
                                     ReplacementPackageInfo *info,
                                     std::string *error) const
{
    fs::path package = fs::u8path(directory);
    std::string manifest;
    if (!ReadFile(package / "manifest.ini", kMaxManifestBytes, &manifest,
                  error)) {
        return false;
    }
    std::map<std::string, std::string> values;
    if (!ParseManifest(manifest, &values, error)) {
        return false;
    }
    auto required = [&](const char *key) -> const std::string * {
        auto it = values.find(key);
        return it == values.end() || it->second.empty() ? nullptr : &it->second;
    };
    const std::string *logical_id = required("id");
    const std::string *name = required("name");
    const std::string *stage = required("stage");
    const std::string *interface_version = required("interface_version");
    if (!logical_id || !name || !stage || !interface_version) {
        if (error) *error = "Manifest is missing id, name, stage, or interface_version";
        return false;
    }
    if (!IsValidLogicalId(*logical_id)) {
        if (error) *error = "Manifest id must use 1-128 alphanumeric, '.', '_' or '-' characters";
        return false;
    }
    if (name->size() > 256 || name->find('\0') != std::string::npos) {
        if (error) *error = "Manifest name is invalid";
        return false;
    }
    if (Lower(*stage) != "pixel") {
        if (error) *error = "Stage 3 v1 supports stage=pixel only";
        return false;
    }
    uint32_t abi = 0;
    if (!ParseU32(*interface_version, &abi) || abi != 1) {
        if (error) *error = "Only interface_version=1 is supported";
        return false;
    }
    std::string entry_point = "main";
    if (auto it = values.find("entry_point"); it != values.end()) {
        entry_point = it->second;
    }
    if (!IsValidEntryPoint(entry_point)) {
        if (error) *error = "Entry point is invalid";
        return false;
    }

    fs::path gl_path;
    fs::path vk_path;
    auto gl_it = values.find("opengl");
    auto vk_it = values.find("vulkan");
    if (gl_it == values.end() && vk_it == values.end()) {
        if (error) *error = "Manifest supplies neither OpenGL nor Vulkan source";
        return false;
    }
    if (gl_it != values.end()) {
        if (!ResolvePayloadPath(package, gl_it->second, &gl_path, error) ||
            !ReadFile(gl_path, kMaxReplacementSourceBytes,
                      &payload->opengl_source, error)) {
            return false;
        }
    }
    if (vk_it != values.end()) {
        if (!ResolvePayloadPath(package, vk_it->second, &vk_path, error) ||
            !ReadFile(vk_path, kMaxReplacementSourceBytes,
                      &payload->vulkan_source, error)) {
            return false;
        }
    }

    uint64_t id = XXH3_64bits(logical_id->data(), logical_id->size());
    if (id == 0) id = 1;
    std::string frame("XEMU-REPLACEMENT-V1", 19);
    AppendString(&frame, *logical_id);
    AppendString(&frame, entry_point);
    AppendU32(&frame, abi);
    AppendString(&frame, payload->opengl_source);
    AppendString(&frame, payload->vulkan_source);
    XXH128_hash_t digest = XXH3_128bits(frame.data(), frame.size());
    uint64_t revision = digest.low64 ^
        ((digest.high64 << 1) | (digest.high64 >> 63));
    if (revision == 0) revision = 1;

    payload->descriptor.id = id;
    payload->descriptor.name = *name;
    payload->descriptor.stage = Stage::Pixel;
    payload->descriptor.interface_version = abi;
    payload->descriptor.entry_point = entry_point;
    payload->descriptor.content_revision = revision;
    uint32_t backend_mask = BackendNone;
    if (!payload->opengl_source.empty()) backend_mask |= BackendOpenGL;
    if (!payload->vulkan_source.empty()) backend_mask |= BackendVulkan;
    payload->descriptor.backend_mask = backend_mask;
    payload->descriptor.content_hash.fill(0);
    StoreU64Le(&payload->descriptor.content_hash, 0, digest.low64);
    StoreU64Le(&payload->descriptor.content_hash, 8, digest.high64);
    payload->descriptor.opengl_source_path =
        gl_path.empty() ? "" : gl_path.u8string();
    payload->descriptor.vulkan_source_path =
        vk_path.empty() ? "" : vk_path.u8string();

    info->logical_id = *logical_id;
    info->directory = package.u8string();
    info->descriptor = payload->descriptor;
    return true;
}

bool ReplacementLibrary::Reload(OverrideStore *store, std::string *error)
{
    if (!store) {
        if (error) *error = "Override store is unavailable";
        return false;
    }

    std::string root;
    std::vector<ReplacementPackageInfo> previous;
    std::unordered_set<uint64_t> previous_ids;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        root = root_path_;
        previous = packages_;
        previous_ids = managed_ids_;
    }
    if (root.empty()) {
        if (error) *error = "Replacement root is not configured";
        return false;
    }

    std::unordered_map<std::string, ReplacementPackageInfo> previous_by_dir;
    for (const auto &package : previous) {
        previous_by_dir.emplace(package.directory, package);
    }

    std::vector<fs::path> directories;
    std::error_code ec;
    fs::path root_path = fs::u8path(root);
    if (fs::exists(root_path, ec)) {
        for (const auto &entry : fs::directory_iterator(root_path, ec)) {
            if (ec) break;
            if (entry.is_directory(ec)) directories.push_back(entry.path());
        }
    }

    std::vector<ReplacementPackageInfo> next;
    std::vector<ReplacementLibraryError> errors;
    if (ec) {
        errors.push_back({root, "Unable to enumerate replacement root: " +
                                  ec.message()});
    }
    std::sort(directories.begin(), directories.end());
    std::unordered_set<uint64_t> seen_ids;
    std::unordered_map<uint64_t, std::string> logical_by_id;

    for (const fs::path &directory : directories) {
        ReplacementPayload payload{};
        ReplacementPackageInfo info{};
        std::string package_error;
        if (!LoadPackage(directory.u8string(), &payload, &info,
                         &package_error)) {
            errors.push_back({directory.u8string(), package_error});
            auto previous_it = previous_by_dir.find(directory.u8string());
            if (previous_it != previous_by_dir.end()) {
                next.push_back(previous_it->second);
                seen_ids.insert(previous_it->second.descriptor.id);
                logical_by_id.emplace(previous_it->second.descriptor.id,
                                      previous_it->second.logical_id);
            }
            continue;
        }
        auto collision = logical_by_id.find(info.descriptor.id);
        if (collision != logical_by_id.end() &&
            collision->second != info.logical_id) {
            errors.push_back({directory.u8string(),
                              "Replacement logical ID hash collision with '" +
                                  collision->second + "'"});
            continue;
        }
        std::string store_error;
        if (!store->UpsertReplacement(payload, &store_error)) {
            errors.push_back({directory.u8string(), store_error});
            auto previous_it = previous_by_dir.find(directory.u8string());
            if (previous_it != previous_by_dir.end()) {
                next.push_back(previous_it->second);
                seen_ids.insert(previous_it->second.descriptor.id);
                logical_by_id.emplace(previous_it->second.descriptor.id,
                                      previous_it->second.logical_id);
            }
            continue;
        }
        next.push_back(info);
        seen_ids.insert(info.descriptor.id);
        logical_by_id.emplace(info.descriptor.id, info.logical_id);
    }

    for (uint64_t old_id : previous_ids) {
        if (!seen_ids.count(old_id)) {
            store->RemoveReplacement(old_id);
        }
    }
    std::sort(next.begin(), next.end(),
              [](const ReplacementPackageInfo &a,
                 const ReplacementPackageInfo &b) {
                  return a.logical_id < b.logical_id;
              });

    {
        std::lock_guard<std::mutex> lock(mutex_);
        packages_ = std::move(next);
        errors_ = std::move(errors);
        managed_ids_ = std::move(seen_ids);
        ++generation_;
        if (error) {
            *error = errors_.empty() ? "" : errors_.front().message;
        }
        return errors_.empty();
    }
}

void ReplacementLibrary::CopySnapshot(
    ReplacementLibrarySnapshot *snapshot) const
{
    if (!snapshot) return;
    std::lock_guard<std::mutex> lock(mutex_);
    snapshot->generation = generation_;
    snapshot->root_path = root_path_;
    snapshot->packages = packages_;
    snapshot->errors = errors_;
}

std::string ReplacementLibrary::RootPath() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return root_path_;
}

ReplacementLibrary &GetReplacementLibrary()
{
    static ReplacementLibrary library;
    return library;
}

} // namespace xemu::shader_browser

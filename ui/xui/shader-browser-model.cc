// SPDX-License-Identifier: GPL-2.0-or-later
#include "shader-browser-model.hh"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <limits>
#include <tuple>
#include <vector>

#include <xxhash.h>

namespace xemu::shader_browser {
namespace {

constexpr char kCrockford32[] = "0123456789ABCDEFGHJKMNPQRSTVWXYZ";

std::string Lower(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return value;
}

int CompareU64(uint64_t lhs, uint64_t rhs)
{
    return lhs < rhs ? -1 : lhs > rhs ? 1 : 0;
}

int CompareEntries(const Entry &a, const Entry &b, const Filter &filter)
{
    int result = 0;
    switch (filter.sort_key) {
    case SortKey::Draws:
        result = CompareU64(a.draw_count, b.draw_count);
        break;
    case SortKey::LastUsed:
        result = CompareU64(a.last_frame, b.last_frame);
        break;
    case SortKey::CompileTime:
        if (!!a.compile_cpu.samples != !!b.compile_cpu.samples) {
            return a.compile_cpu.samples ? -1 : 1;
        }
        result = CompareU64(a.compile_cpu.max_ns, b.compile_cpu.max_ns);
        break;
    case SortKey::ShaderId:
        result = a.key < b.key ? -1 : b.key < a.key ? 1 : 0;
        break;
    }
    if (result && filter.descending) {
        result = -result;
    }
    if (!result) {
        result = a.key < b.key ? -1 : b.key < a.key ? 1 : 0;
    }
    return result;
}

void AppendU32Le(std::vector<uint8_t> *out, uint32_t value)
{
    for (unsigned int shift = 0; shift < 32; shift += 8) {
        out->push_back(static_cast<uint8_t>((value >> shift) & 0xffU));
    }
}

void AppendU64Le(std::vector<uint8_t> *out, uint64_t value)
{
    for (unsigned int shift = 0; shift < 64; shift += 8) {
        out->push_back(static_cast<uint8_t>((value >> shift) & 0xffU));
    }
}

uint8_t StageIdentityCode(Stage stage)
{
    switch (stage) {
    case Stage::Vertex: return 1;
    case Stage::Pixel: return 2;
    case Stage::Geometry: return 3;
    case Stage::FixedFunction: return 4;
    default: return 0;
    }
}

std::string Group4(const std::string &input)
{
    std::string out;
    out.reserve(input.size() + input.size() / 4);
    for (size_t i = 0; i < input.size(); ++i) {
        if (i && (i % 4) == 0) {
            out.push_back('-');
        }
        out.push_back(input[i]);
    }
    return out;
}

} // namespace

bool ShaderHash::operator==(const ShaderHash &other) const
{
    return version == other.version && bytes == other.bytes;
}

bool ShaderHash::operator!=(const ShaderHash &other) const
{
    return !(*this == other);
}

bool ShaderHash::operator<(const ShaderHash &other) const
{
    return std::tie(version, bytes) < std::tie(other.version, other.bytes);
}

bool ShaderKey::operator==(const ShaderKey &other) const
{
    return hash == other.hash && stage == other.stage;
}

bool ShaderKey::operator!=(const ShaderKey &other) const
{
    return !(*this == other);
}

bool ShaderKey::operator<(const ShaderKey &other) const
{
    return hash < other.hash || (hash == other.hash && stage < other.stage);
}

size_t ShaderHashHasher::operator()(const ShaderHash &hash) const noexcept
{
    // This hash is only for host unordered_map bucketing. It is not the
    // portable shader identity and never leaves the process.
    uint64_t value = 1469598103934665603ULL;
    for (unsigned int shift = 0; shift < 32; shift += 8) {
        value ^= (hash.version >> shift) & 0xffU;
        value *= 1099511628211ULL;
    }
    for (uint8_t byte : hash.bytes) {
        value ^= byte;
        value *= 1099511628211ULL;
    }
    return static_cast<size_t>(value);
}

size_t ShaderKeyHash::operator()(const ShaderKey &key) const noexcept
{
    size_t hash = ShaderHashHasher{}(key.hash);
    hash ^= static_cast<size_t>(key.stage) + 0x9e3779b9U + (hash << 6) +
            (hash >> 2);
    return hash;
}

bool ShaderScope::operator==(const ShaderScope &other) const
{
    return title_id == other.title_id &&
           executable_fingerprint_version ==
               other.executable_fingerprint_version &&
           executable_fingerprint == other.executable_fingerprint;
}

bool ShaderScope::operator<(const ShaderScope &other) const
{
    return std::tie(title_id, executable_fingerprint_version,
                    executable_fingerprint) <
           std::tie(other.title_id, other.executable_fingerprint_version,
                    other.executable_fingerprint);
}

void DurationStats::AddSample(uint64_t ns)
{
    if (!samples) {
        min_ns = max_ns = ns;
    } else {
        min_ns = std::min(min_ns, ns);
        max_ns = std::max(max_ns, ns);
    }
    samples++;
    if (std::numeric_limits<uint64_t>::max() - total_ns < ns) {
        total_ns = std::numeric_limits<uint64_t>::max();
    } else {
        total_ns += ns;
    }
}

void DurationStats::Merge(const DurationStats &other)
{
    if (!other.samples) {
        return;
    }
    if (!samples) {
        *this = other;
        return;
    }
    min_ns = std::min(min_ns, other.min_ns);
    max_ns = std::max(max_ns, other.max_ns);
    samples += other.samples;
    if (std::numeric_limits<uint64_t>::max() - total_ns < other.total_ns) {
        total_ns = std::numeric_limits<uint64_t>::max();
    } else {
        total_ns += other.total_ns;
    }
}

double DurationStats::AverageNs() const
{
    return samples ? static_cast<double>(total_ns) / samples : 0.0;
}

ShaderHash ComputeShaderHash(uint32_t identity_version, Stage stage,
                             uint32_t recipe_format_version,
                             const uint8_t *recipe, size_t recipe_size)
{
    ShaderHash result{};
    result.version = identity_version;
    uint8_t stage_code = StageIdentityCode(stage);
    if (!identity_version || !stage_code || !recipe_format_version ||
        (!recipe && recipe_size)) {
        result.version = 0;
        return result;
    }

    static constexpr uint8_t kNamespace[] = {
        'X', 'E', 'M', 'U', '-', 'N', 'V', '2', 'A', '-',
        'S', 'H', 'A', 'D', 'E', 'R'
    };
    std::vector<uint8_t> framed;
    framed.reserve(sizeof(kNamespace) + 4 + 1 + 4 + 8 + recipe_size);
    framed.insert(framed.end(), std::begin(kNamespace), std::end(kNamespace));
    AppendU32Le(&framed, identity_version);
    framed.push_back(stage_code);
    AppendU32Le(&framed, recipe_format_version);
    AppendU64Le(&framed, recipe_size);
    if (recipe_size) {
        framed.insert(framed.end(), recipe, recipe + recipe_size);
    }

    XXH128_hash_t hash = XXH3_128bits(framed.data(), framed.size());
    XXH128_canonical_t canonical{};
    XXH128_canonicalFromHash(&canonical, hash);
    std::copy_n(canonical.digest, result.bytes.size(), result.bytes.begin());
    return result;
}

std::string ShaderHashHex(const ShaderHash &hash)
{
    static constexpr char digits[] = "0123456789abcdef";
    std::string result(hash.bytes.size() * 2, '0');
    for (size_t i = 0; i < hash.bytes.size(); ++i) {
        result[i * 2] = digits[hash.bytes[i] >> 4];
        result[i * 2 + 1] = digits[hash.bytes[i] & 0x0f];
    }
    return result;
}

std::string ShaderHashBase32(const ShaderHash &hash)
{
    // 96 bits -> 20 Crockford characters (last character contains four data
    // bits and one zero pad bit). This is display encoding only.
    std::string out;
    out.reserve(20);
    uint32_t buffer = 0;
    unsigned int bits = 0;
    for (uint8_t byte : hash.bytes) {
        buffer = (buffer << 8) | byte;
        bits += 8;
        while (bits >= 5) {
            bits -= 5;
            out.push_back(kCrockford32[(buffer >> bits) & 31U]);
            if (bits) {
                buffer &= (1U << bits) - 1U;
            } else {
                buffer = 0;
            }
        }
    }
    if (bits) {
        out.push_back(kCrockford32[(buffer << (5 - bits)) & 31U]);
    }
    return out;
}

std::string PortableShaderHash(const ShaderHash &hash)
{
    return "XSH" + std::to_string(hash.version) + "-" +
           Group4(ShaderHashBase32(hash));
}

std::string FormatTitleId(uint32_t title_id)
{
    char buffer[9];
    std::snprintf(buffer, sizeof(buffer), "%08X", title_id);
    return buffer;
}

std::string GameShaderId(uint32_t title_id, const ShaderHash &hash)
{
    return FormatTitleId(title_id) + "-" + Group4(ShaderHashBase32(hash));
}

std::string ShortGameShaderId(uint32_t title_id, const ShaderHash &hash)
{
    std::string encoded = ShaderHashBase32(hash);
    return FormatTitleId(title_id) + "-" + encoded.substr(0, 4) + "-" +
           encoded.substr(4, 4);
}

uint32_t PrimaryTitleId(const Entry &entry)
{
    for (const ShaderScope &scope : entry.scopes) {
        if (scope.title_id) {
            return scope.title_id;
        }
    }
    return 0;
}

const char *StageLabel(Stage stage)
{
    switch (stage) {
    case Stage::Any: return "All";
    case Stage::Vertex: return "VS";
    case Stage::Pixel: return "PS";
    case Stage::Geometry: return "GS";
    case Stage::FixedFunction: return "FF";
    case Stage::Unknown: return "?";
    }
    return "?";
}

const char *StatusLabel(Status status)
{
    switch (status) {
    case Status::NotObserved: return "Not observed";
    case Status::Normal: return "Normal";
    case Status::Hot: return "Hot";
    case Status::CompileSpike: return "Compile spike";
    case Status::Disabled: return "Disabled";
    case Status::Custom: return "Custom";
    }
    return "Unknown";
}

const char *RouteLabel(Route route)
{
    switch (route) {
    case Route::Unknown: return "Unknown";
    case Route::Specialized: return "Specialized";
    case Route::Uber: return "Uber";
    case Route::FixedFunction: return "Fixed function";
    case Route::Replacement: return "Replacement";
    case Route::Disabled: return "Disabled";
    }
    return "Unknown";
}

const char *ReadinessLabel(Readiness readiness)
{
    switch (readiness) {
    case Readiness::Unknown: return "Unknown";
    case Readiness::Ready: return "Ready";
    case Readiness::Pending: return "Pending";
    case Readiness::Failed: return "Failed";
    }
    return "Unknown";
}

const char *SourceFilterLabel(SourceFilter source)
{
    switch (source) {
    case SourceFilter::All: return "All";
    case SourceFilter::ThisSession: return "This session";
    case SourceFilter::StoredDatabase: return "Database catalog (write-behind)";
    }
    return "All";
}

bool EntryMatches(const Entry &entry, const Filter &filter)
{
    if (filter.stage != Stage::Any && entry.key.stage != filter.stage) {
        return false;
    }
    if (filter.source == SourceFilter::ThisSession &&
        !entry.observed_in_session) {
        return false;
    }
    if (filter.source == SourceFilter::StoredDatabase &&
        !entry.stored_in_database) {
        return false;
    }
    if (filter.query.empty()) {
        return true;
    }

    std::string needle = Lower(filter.query);
    std::string haystack = Lower(PortableShaderHash(entry.key.hash));
    haystack += " " + Lower(ShaderHashHex(entry.key.hash));
    for (const ShaderScope &scope : entry.scopes) {
        haystack += " " + Lower(GameShaderId(scope.title_id, entry.key.hash));
    }
    return haystack.find(needle) != std::string::npos;
}

std::vector<size_t> BuildVisibleOrder(const std::vector<Entry> &entries,
                                      const Filter &filter)
{
    std::vector<size_t> result;
    for (size_t i = 0; i < entries.size(); ++i) {
        if (EntryMatches(entries[i], filter)) {
            result.push_back(i);
        }
    }
    std::stable_sort(result.begin(), result.end(), [&](size_t lhs, size_t rhs) {
        return CompareEntries(entries[lhs], entries[rhs], filter) < 0;
    });
    return result;
}

int FindEntryByKey(const std::vector<Entry> &entries, const ShaderKey &key)
{
    for (size_t i = 0; i < entries.size(); ++i) {
        if (entries[i].key == key) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

std::string FormatDurationNs(const DurationStats &stats)
{
    if (!stats.samples) {
        return "Not sampled";
    }
    double avg = stats.AverageNs();
    char buffer[96];
    if (avg >= 1000000.0) {
        std::snprintf(buffer, sizeof(buffer), "%.2f ms avg / %.2f ms max",
                      avg / 1000000.0, stats.max_ns / 1000000.0);
    } else if (avg >= 1000.0) {
        std::snprintf(buffer, sizeof(buffer), "%.1f us avg / %.1f us max",
                      avg / 1000.0, stats.max_ns / 1000.0);
    } else {
        std::snprintf(buffer, sizeof(buffer), "%.0f ns avg / %llu ns max",
                      avg, static_cast<unsigned long long>(stats.max_ns));
    }
    return buffer;
}

std::string FormatLastUsed(uint64_t last_frame, uint64_t current_frame,
                           bool observed)
{
    if (!observed) {
        return "Not observed";
    }
    if (last_frame >= current_frame) {
        return "now";
    }
    return std::to_string(current_frame - last_frame) + " frames ago";
}

} // namespace xemu::shader_browser

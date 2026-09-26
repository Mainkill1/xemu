// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace xemu::shader_browser {

// The user-visible GameShaderID is 32-bit Xbox title ID + 96-bit shader hash.
// The 96-bit hash is the first 96 bits of XXH3-128 canonical output over a
// versioned identity frame plus the canonical recipe. The canonical recipe
// bytes remain stored so a collision can be detected instead of silently
// aliasing two shaders.
constexpr size_t kShaderHashBytes = 12;
constexpr size_t kExecutableFingerprintBytes = 32;

// Maximum canonical guest recipe accepted into the metadata database. NV2A
// recipes are expected to be far smaller; this is only a corruption/abuse
// ceiling. Generated/compiled host artifacts never use this field.
constexpr size_t kMaxCanonicalRecipeBytes = 256U * 1024U;

enum class Stage : uint8_t {
    Any,
    Vertex,
    Pixel,
    Geometry,
    FixedFunction,
    Unknown,
};

enum class Status : uint8_t {
    NotObserved,
    Normal,
    Hot,
    CompileSpike,
    Disabled,
    Custom,
};

enum class Route : uint8_t {
    Unknown,
    Specialized,
    Uber,
    FixedFunction,
    Replacement,
    Disabled,
};

enum class Readiness : uint8_t {
    Unknown,
    Ready,
    Pending,
    Failed,
};

enum class SortKey : uint8_t {
    Draws,
    LastUsed,
    CompileTime,
    ShaderId,
};

enum class SourceFilter : uint8_t {
    All,
    ThisSession,
    StoredDatabase,
};

enum Capability : uint32_t {
    CapabilityDetails = 1U << 0,
    CapabilityOverrides = 1U << 1,
    CapabilityPreview = 1U << 2,
};

struct ShaderHash {
    uint32_t version = 1;
    std::array<uint8_t, kShaderHashBytes> bytes{};

    bool operator==(const ShaderHash &other) const;
    bool operator!=(const ShaderHash &other) const;
    bool operator<(const ShaderHash &other) const;
};

struct ShaderKey {
    ShaderHash hash;
    Stage stage = Stage::Unknown;

    bool operator==(const ShaderKey &other) const;
    bool operator!=(const ShaderKey &other) const;
    bool operator<(const ShaderKey &other) const;
};

struct ShaderHashHasher {
    size_t operator()(const ShaderHash &hash) const noexcept;
};

struct ShaderKeyHash {
    size_t operator()(const ShaderKey &key) const noexcept;
};

struct ShaderScope {
    uint32_t title_id = 0;
    uint32_t executable_fingerprint_version = 0;
    std::array<uint8_t, kExecutableFingerprintBytes>
        executable_fingerprint{};

    bool operator==(const ShaderScope &other) const;
    bool operator<(const ShaderScope &other) const;
};

struct DurationStats {
    uint64_t samples = 0;
    uint64_t total_ns = 0;
    uint64_t min_ns = 0;
    uint64_t max_ns = 0;

    void AddSample(uint64_t ns);
    void Merge(const DurationStats &other);
    double AverageNs() const;
};

struct Entry {
    ShaderKey key;
    uint64_t draw_count = 0;
    uint64_t first_frame = 0;
    uint64_t last_frame = 0;
    uint32_t pipeline_variant_count = 0;
    uint32_t recipe_format_version = 0;
    size_t recipe_size = 0;
    bool observed_in_session = false;
    bool stored_in_database = false;
    Status status = Status::NotObserved;
    Route route = Route::Unknown;
    Readiness readiness = Readiness::Unknown;
    DurationStats compile_cpu;
    DurationStats pipeline_cpu;
    DurationStats prepare_cpu;
    DurationStats foreground_stall_cpu;
    DurationStats gpu_execution;
    uint64_t specialized_draws = 0;
    uint64_t uber_draws = 0;
    uint64_t replacement_draws = 0;
    uint64_t compile_failures = 0;
    std::vector<ShaderScope> scopes;
};

struct Filter {
    std::string query;
    Stage stage = Stage::Any;
    SourceFilter source = SourceFilter::All;
    SortKey sort_key = SortKey::Draws;
    bool descending = true;
};

ShaderHash ComputeShaderHash(uint32_t identity_version, Stage stage,
                             uint32_t recipe_format_version,
                             const uint8_t *recipe, size_t recipe_size);
std::string ShaderHashHex(const ShaderHash &hash);
std::string ShaderHashBase32(const ShaderHash &hash);
std::string PortableShaderHash(const ShaderHash &hash);
std::string GameShaderId(uint32_t title_id, const ShaderHash &hash);
std::string ShortGameShaderId(uint32_t title_id, const ShaderHash &hash);
std::string FormatTitleId(uint32_t title_id);
uint32_t PrimaryTitleId(const Entry &entry);
const char *StageLabel(Stage stage);
const char *StatusLabel(Status status);
const char *RouteLabel(Route route);
const char *ReadinessLabel(Readiness readiness);
const char *SourceFilterLabel(SourceFilter source);
bool EntryMatches(const Entry &entry, const Filter &filter);
std::vector<size_t> BuildVisibleOrder(const std::vector<Entry> &entries,
                                      const Filter &filter);
int FindEntryByKey(const std::vector<Entry> &entries, const ShaderKey &key);
std::string FormatDurationNs(const DurationStats &stats);
std::string FormatLastUsed(uint64_t last_frame, uint64_t current_frame,
                           bool observed);

} // namespace xemu::shader_browser

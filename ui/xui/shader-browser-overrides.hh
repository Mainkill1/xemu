// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "shader-browser-model.hh"

#include <array>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace xemu::shader_browser {

enum class OverrideAction : uint8_t {
    Normal,
    ForceUber,
    ForceSpecialized,
    SkipDraw,
    Highlight,
    Replacement,
};

enum class OverrideOrigin : uint8_t {
    Imported,
    Saved,
    Session,
};

enum class OverrideBackend : uint8_t {
    Unknown,
    OpenGL,
    Vulkan,
};

enum ReplacementBackendMask : uint32_t {
    BackendNone = 0,
    BackendOpenGL = 1U << 0,
    BackendVulkan = 1U << 1,
};

enum DrawConditionMask : uint32_t {
    DrawConditionNone = 0,
    DrawConditionElementCount = 1U << 0,
    DrawConditionElementRange = 1U << 1,
    DrawConditionPrimitive = 1U << 2,
};

struct ShaderDragPayload {
    uint32_t title_id = 0;
    uint32_t identity_version = 0;
    std::array<uint8_t, kShaderHashBytes> shader_hash{};
    uint32_t stage = 0;
};

ShaderDragPayload MakeShaderDragPayload(uint32_t title_id,
                                        const ShaderKey &key);
bool DecodeShaderDragPayload(const ShaderDragPayload &payload,
                             uint32_t *title_id, ShaderKey *key);

struct DrawFacts {
    uint32_t element_count = 0;
    uint32_t min_element = 0;
    uint32_t max_element = 0;
    uint32_t primitive_mode = 0;
};

struct DrawCondition {
    uint32_t mask = DrawConditionNone;
    uint32_t element_count_min = 0;
    uint32_t element_count_max = 0;
    uint32_t min_element = 0;
    uint32_t max_element = 0;
    uint32_t primitive_mode = 0;

    bool Matches(const DrawFacts &facts) const;
};

struct ReplacementDescriptor {
    uint64_t id = 0;
    std::string name;
    Stage stage = Stage::Unknown;
    uint32_t backend_mask = BackendNone;
    uint32_t interface_version = 0;
    std::string entry_point = "main";
    uint64_t content_revision = 0;
    std::array<uint8_t, 32> content_hash{};
    std::string opengl_source_path;
    std::string vulkan_source_path;
    bool enabled = true;
};

struct OverrideRule {
    uint64_t id = 0;
    bool enabled = true;
    uint32_t title_id = 0;
    ShaderKey shader;
    bool restrict_build = false;
    uint32_t executable_fingerprint_version = 0;
    std::array<uint8_t, kExecutableFingerprintBytes>
        executable_fingerprint{};
    OverrideOrigin origin = OverrideOrigin::Saved;
    int32_t priority = 0;
    OverrideAction action = OverrideAction::Normal;
    uint64_t replacement_id = 0;
    DrawCondition draw_condition;
    uint64_t revision = 0;
};

struct OverrideContext {
    uint32_t title_id = 0;
    uint32_t executable_fingerprint_version = 0;
    std::array<uint8_t, kExecutableFingerprintBytes>
        executable_fingerprint{};
    OverrideBackend backend = OverrideBackend::Unknown;
};

enum class OverrideResolutionStatus : uint8_t {
    None,
    Matched,
    Conflict,
    Incompatible,
};

struct RuntimePolicy {
    OverrideAction action = OverrideAction::Normal;
    uint64_t rule_id = 0;
    uint64_t rule_revision = 0;
    uint64_t replacement_id = 0;
    uint64_t replacement_revision = 0;
    DrawCondition draw_condition;
};

struct OverrideResolution {
    OverrideResolutionStatus status = OverrideResolutionStatus::None;
    RuntimePolicy policy;
    std::string message;
};

bool IsReplacementCompatible(const ShaderKey &shader,
                             const ReplacementDescriptor &replacement,
                             OverrideBackend backend,
                             std::string *reason);
bool IsOverrideActionSupported(OverrideAction action,
                               OverrideBackend backend,
                               std::string *reason);
const ShaderScope *FindCurrentBuildScope(const Entry &entry,
                                         const OverrideContext &context);
bool IsEntryCompatibleWithReplacement(const Entry &entry,
                                      uint32_t title_id,
                                      const ReplacementDescriptor &replacement,
                                      OverrideBackend backend,
                                      std::string *reason);
const char *OverrideActionLabel(OverrideAction action);
const char *OverrideResolutionLabel(OverrideResolutionStatus status);
uint32_t BackendMaskFor(OverrideBackend backend);

class OverrideIndex
{
public:
    void Rebuild(const OverrideContext &context,
                 const std::vector<OverrideRule> &rules,
                 const std::vector<ReplacementDescriptor> &replacements);
    OverrideResolution Resolve(const ShaderKey &key) const;
    uint64_t Generation() const { return generation; }
    size_t RuleCount() const { return resolved.size(); }
    bool HasMatchedRules() const;

private:
    OverrideContext context;
    uint64_t generation = 0;
    std::unordered_map<ShaderKey, OverrideResolution, ShaderKeyHash> resolved;
};

} // namespace xemu::shader_browser

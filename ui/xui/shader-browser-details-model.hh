// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "shader-browser-model.hh"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace xemu::shader_browser {

constexpr size_t kMaxDetailSourceBytes = 4U * 1024U * 1024U;
constexpr size_t kMaxDetailSourceBytesTotal = 16U * 1024U * 1024U;
constexpr size_t kMaxDetailSources = 32U;
constexpr size_t kMaxDetailVariants = 256U;
constexpr size_t kMaxDetailTextBytes = 4096U;
constexpr size_t kMaxLifecycleEvents = 128U;

enum class DetailBackend : uint8_t {
    Active,
    OpenGL,
    Vulkan,
    Unknown,
};

enum DetailRequestFlag : uint32_t {
    DetailRequestSources = 1U << 0,
    DetailRequestVariants = 1U << 1,
    DetailRequestLifecycle = 1U << 2,
    DetailRequestAll = DetailRequestSources | DetailRequestVariants |
                       DetailRequestLifecycle,
};

enum HostVariantValidField : uint32_t {
    HostVariantFrames = 1U << 0,
    HostVariantDrawCount = 1U << 1,
    HostVariantCompileTime = 1U << 2,
    HostVariantCreateTime = 1U << 3,
    HostVariantPrimitiveMode = 1U << 4,
    HostVariantColorFormat = 1U << 5,
    HostVariantDepthFormat = 1U << 6,
    HostVariantVertexLayout = 1U << 7,
    HostVariantAllValidFields = (1U << 8) - 1U,
};

enum class DetailState : uint8_t {
    Idle,
    Pending,
    Complete,
    Partial,
    Unavailable,
    Failed,
};

enum class HostSourceStage : uint8_t {
    Vertex,
    Geometry,
    Fragment,
    LinkedProgram,
    Pipeline,
    Unknown,
};

enum class HostSourceKind : uint8_t {
    Glsl,
    SpirvText,
    Metadata,
    Unknown,
};

enum class LifecycleKind : uint8_t {
    Discovered,
    SourceGenerated,
    CompileQueued,
    CompileStarted,
    CompileCompleted,
    CompileFailed,
    ModulePublished,
    PipelineRequested,
    PipelineCreated,
    PipelinePublished,
    Selected,
    Removed,
    DetailRequestCompleted,
    Unknown,
};

struct DetailRequest {
    uint64_t request_id = 0;
    ShaderKey key;
    DetailBackend backend = DetailBackend::Active;
    uint32_t flags = DetailRequestAll;
};

struct HostSource {
    DetailBackend backend = DetailBackend::Unknown;
    HostSourceStage stage = HostSourceStage::Unknown;
    HostSourceKind kind = HostSourceKind::Unknown;
    Route route = Route::Unknown;
    uint64_t artifact_id = 0;
    bool exact_runtime_source = false;
    std::string label;
    std::string text;
};

struct HostVariant {
    uint64_t variant_id = 0;
    DetailBackend backend = DetailBackend::Unknown;
    Route route = Route::Unknown;
    Readiness readiness = Readiness::Unknown;
    uint64_t first_frame = 0;
    uint64_t last_frame = 0;
    uint64_t draw_count = 0;
    uint64_t compile_time_ns = 0;
    uint64_t create_time_ns = 0;
    uint32_t valid_fields = 0;
    uint32_t primitive_mode = 0;
    uint32_t color_format = 0;
    uint32_t depth_format = 0;
    uint32_t vertex_binding_count = 0;
    uint32_t vertex_attribute_count = 0;
    std::string label;
};

struct LifecycleEvent {
    uint64_t sequence = 0;
    uint64_t frame = 0;
    LifecycleKind kind = LifecycleKind::Unknown;
    DetailBackend backend = DetailBackend::Unknown;
    Route route = Route::Unknown;
    uint64_t artifact_id = 0;
    std::string message;
};

struct DetailResult {
    uint64_t request_id = 0;
    ShaderKey key;
    DetailState state = DetailState::Failed;
    DetailBackend backend = DetailBackend::Unknown;
    std::string status;
    std::vector<HostSource> sources;
    std::vector<HostVariant> variants;
    std::vector<LifecycleEvent> lifecycle;
    uint64_t dropped_lifecycle_events = 0;
};

struct DetailSnapshot {
    uint64_t generation = 0;
    DetailRequest request;
    DetailState state = DetailState::Idle;
    DetailBackend backend = DetailBackend::Unknown;
    std::string status;
    std::vector<HostSource> sources;
    std::vector<HostVariant> variants;
    std::vector<LifecycleEvent> lifecycle;
    uint64_t dropped_lifecycle_events = 0;
};

const char *DetailBackendLabel(DetailBackend backend);
const char *DetailStateLabel(DetailState state);
const char *HostSourceStageLabel(HostSourceStage stage);
const char *HostSourceKindLabel(HostSourceKind kind);
const char *LifecycleKindLabel(LifecycleKind kind);

bool DetailBackendMatches(DetailBackend requested, DetailBackend renderer);
bool ValidateDetailResult(const DetailResult &result, std::string *error);

} // namespace xemu::shader_browser

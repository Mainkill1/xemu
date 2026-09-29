// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "shader-browser-model.hh"
#include "shader-browser-preview-scene.hh"
#include "shader-browser-draw-inputs.hh"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <memory>
#include <vector>

namespace xemu::shader_browser {

constexpr size_t kPreviewMaxRecipeBytes = 8192U;
constexpr size_t kPreviewMaxSourceBytes = 4U * 1024U * 1024U;
constexpr size_t kPreviewMaxOwnedPacketBytes = 32U * 1024U * 1024U;
constexpr uint32_t kPreviewFullExtent = 320U;
constexpr uint32_t kPreviewReducedExtent = 160U;
constexpr uint32_t kPreviewMaxWidth = 640U;
constexpr uint32_t kPreviewMaxHeight = 480U;
constexpr uint32_t kPreviewMaxCapturedWidth = 1920U;
constexpr uint32_t kPreviewMaxCapturedHeight = 1080U;
inline constexpr bool PreviewExtentWithinLimits(uint32_t width, uint32_t height,
                                                bool captured)
{
    return width && height &&
           width <= (captured ? kPreviewMaxCapturedWidth : kPreviewMaxWidth) &&
           height <= (captured ? kPreviewMaxCapturedHeight : kPreviewMaxHeight);
}
constexpr size_t kPreviewDigestBytes = 32U;

using PreviewDigest = std::array<uint8_t, kPreviewDigestBytes>;
enum class PreviewBackend : uint8_t;

enum PreviewMaterialLimitation : uint32_t {
    PreviewMaterialUnavailable = 1,
    PreviewMaterialMissingTexture = 2,
    PreviewMaterialUnsupportedTexture = 4,
    PreviewMaterialUnappliedUniform = 8,
    PreviewMaterialBudgetLimited = 16,
    PreviewMaterialBaseLevelOnly = 32,
    PreviewMaterialApproximateSampler = 64,
};

struct PreviewCapturedMaterial {
    std::array<OwnedDrawTexture, 4> textures;
    // Exact base-level R16 storage for sampled depth comparisons. Inspection
    // RGBA8 images are retained separately and must not replace this precision.
    std::array<OwnedDrawBlob, 4> texture_storage;
    std::array<std::array<uint32_t, 4>, 4> texture_storage_swizzle{
        std::array<uint32_t, 4>{ 0x1903, 0x1904, 0x1905, 0x1906 },
        std::array<uint32_t, 4>{ 0x1903, 0x1904, 0x1905, 0x1906 },
        std::array<uint32_t, 4>{ 0x1903, 0x1904, 0x1905, 0x1906 },
        std::array<uint32_t, 4>{ 0x1903, 0x1904, 0x1905, 0x1906 }
    };
    std::vector<OwnedDrawUniform> uniforms;
    uint32_t limitations = PreviewMaterialBaseLevelOnly;
};

// Leave room for both 4 MiB sources and bounded geometry in the 32 MiB packet.
constexpr size_t kPreviewMaxCapturedMaterialBytes =
    24U * 1024U * 1024U - 256U * 1024U;
std::shared_ptr<const PreviewCapturedMaterial> BuildPreviewCapturedMaterial(
    const OwnedDrawInputs &inputs,
    size_t max_bytes = kPreviewMaxCapturedMaterialBytes);
std::shared_ptr<const PreviewCapturedMaterial> BuildPreviewCapturedMaterial(
    const OwnedDrawInputs &inputs, PreviewBackend backend,
    size_t max_bytes = kPreviewMaxCapturedMaterialBytes);
PreviewDigest
ComputePreviewCapturedMaterialDigest(const PreviewCapturedMaterial &material);
std::string
DescribePreviewCapturedMaterial(const PreviewCapturedMaterial &material);
bool PreviewCapturedUniformAllowed(const OwnedDrawUniform &uniform);

struct PreviewCapturedAttribute {
    bool enabled = false;
    OwnedDrawBlob stream;
};
enum PreviewRasterComponent : uint32_t {
    PreviewRasterViewport = 1,
    PreviewRasterScissor = 2,
    PreviewRasterBlend = 4,
    PreviewRasterColorWrite = 8,
    PreviewRasterDepth = 16,
    PreviewRasterStencil = 32,
    PreviewRasterCull = 64,
    PreviewRasterDepthBias = 128,
    PreviewRasterCoverage = 256,
    PreviewRasterDepthClamp = 512,
};
// Portable numeric enums follow core Vulkan blend/compare/stencil values.
// Rectangles and destination RGBA use canonical top-down coordinates.
struct PreviewCapturedStencil {
    uint32_t fail = 0, pass = 0, depth_fail = 0, compare = 7;
    uint32_t read_mask = UINT32_MAX, write_mask = UINT32_MAX, reference = 0;
};
struct PreviewCapturedRaster {
    uint32_t available = 0, width = 0, height = 0;
    int32_t scissor_x = 0, scissor_y = 0;
    uint32_t scissor_width = 0, scissor_height = 0;
    bool scissor_enabled = false, blend_enabled = false;
    uint32_t src_rgb = 1, dst_rgb = 0, src_alpha = 1, dst_alpha = 0;
    uint32_t blend_rgb = 0, blend_alpha = 0, color_write = 15;
    std::array<float, 4> blend_color{};
    bool depth_test = false, depth_write = false, stencil_test = false;
    bool depth_clamp = false;
    uint32_t depth_compare = 1;
    double depth_min = 0, depth_max = 1;
    PreviewCapturedStencil front_stencil, back_stencil;
    uint32_t cull_mode = 0;
    bool front_ccw = true, depth_bias = false;
    float bias_constant = 0, bias_slope = 0;
};
struct PreviewCapturedPipeline {
    PreviewBackend backend{};
    // Exact backend enum: VkPrimitiveTopology or GLenum. Diagnostic mesh
    // triangulation does not alter these original camera commands.
    uint32_t guest_primitive_mode = 5, host_topology = UINT32_MAX;
    bool primitive_restart = false, host_topology_captured = false;
    uint32_t first_vertex = 0, vertex_count = 0;
    uint32_t uniform_attribute_mask = 0;
    std::array<PreviewCapturedAttribute, 16> attributes;
    std::vector<uint32_t> indices;
    std::vector<std::array<uint32_t, 2>> ranges;
    std::vector<OwnedDrawUniform> uniforms;
    std::string geometry_source;
    PreviewCapturedRaster raster;
    OwnedDrawImage color_before;
};
constexpr size_t kPreviewMaxCapturedPipelineBytes = 16U * 1024U * 1024U;
std::shared_ptr<const PreviewCapturedPipeline> BuildPreviewCapturedPipeline(
    const OwnedDrawInputs &inputs, PreviewBackend backend,
    uint32_t primitive_mode, std::string *error,
    size_t max_bytes = kPreviewMaxCapturedPipelineBytes);
PreviewDigest
ComputePreviewCapturedPipelineDigest(const PreviewCapturedPipeline &pipeline,
                                     bool layout_only = false);
bool ValidatePreviewCapturedPipeline(const PreviewCapturedPipeline &pipeline,
                                     std::string *error);
size_t PreviewCapturedAttributeElementBytes(PreviewBackend backend,
                                            const OwnedDrawBlob &stream);
bool DecodePreviewCapturedRaster(const OwnedDrawInputs &, PreviewBackend,
                                 PreviewCapturedRaster *, std::string *error);
std::string DescribePreviewCapturedRaster(const PreviewCapturedPipeline &);

enum class PreviewUpdatePolicy : uint8_t { OnDirty, Continuous };

enum class PreviewSourceVariant : uint8_t { Original, Edited };

enum class PreviewInputTarget : uint8_t {
    D0Alpha,
    UVOffsetU,
    UVOffsetV,
    ConstantR,
    ConstantG,
    ConstantB,
    ConstantA,
    Fog,
    D0RGB,
    Count,
};

struct PreviewInputBinding {
    PreviewInputTarget target = PreviewInputTarget::D0Alpha;
    bool enabled = false;
    float base = 0.0f;
    float amplitude = 0.0f;
    float period_seconds = 8.0f;
    bool operator==(const PreviewInputBinding &other) const;
};
constexpr size_t kPreviewMaxInputBindings = 9;

enum class PreviewMode : uint8_t {
    Normal,
    Uber,
    Replacement,
    Visualize,
};

enum class PreviewChannel : uint8_t {
    FinalRGBA,
    Red,
    Green,
    Blue,
    Alpha,
    UV,
    D0,
    D1,
    B0,
    B1,
    T0,
    T1,
    T2,
    T3,
    Fog,
    DepthRamp,
    FixtureAlphaMask,
    ShaderDiscard,
    Count
};
const char *PreviewChannelLabel(PreviewChannel channel);
const char *PreviewChannelProvenance(PreviewChannel channel);
bool PreviewChannelAvailable(PreviewChannel channel);
bool PreviewChannelIsDiagnostic(PreviewChannel channel);
// -1 means the unmodified RGBA result; 0..3 selects an opaque scalar view.
int PreviewChannelComponent(PreviewChannel channel);

enum class PreviewBackend : uint8_t {
    Unknown,
    OpenGL,
    Vulkan,
};

enum class PreviewPacketKind : uint8_t {
    Synthetic,
    Replay,
};

enum class PreviewReplayClass : uint8_t {
    Synthetic,
    Complete,
    Approximate,
    Unsupported,
};

enum class PreviewState : uint8_t {
    Disabled,
    Hidden,
    NoSelection,
    WaitingForInputs,
    Debouncing,
    NeedsPreparation,
    Preparing,
    Ready,
    Rendering,
    Throttled,
    Frozen,
    Unsupported,
    Failed,
    Retiring,
};

enum class PreviewPressure : uint8_t {
    Normal,
    Elevated,
    High,
    Critical,
};

enum class PreviewWorkKind : uint8_t {
    None,
    Prepare,
    Render,
};

enum class PreviewSlotState : uint8_t {
    Free,
    Rendering,
    Ready,
    DisplayLeased,
    Retiring,
};

struct PreviewSelection {
    ShaderScope scope;
    ShaderKey shader;
    uint64_t session_epoch = 0;
    uint64_t renderer_epoch = 0;
    PreviewBackend backend = PreviewBackend::Unknown;
    PreviewMode mode = PreviewMode::Normal;

    bool operator==(const PreviewSelection &other) const;
    bool operator!=(const PreviewSelection &other) const;
};

// Display compatibility ignores mode; all ownership epochs still match.
bool SamePreviewDisplayScope(const PreviewSelection &lhs,
                             const PreviewSelection &rhs);

struct PreviewCompileKey {
    PreviewSelection selection;
    PreviewSourceVariant source_variant = PreviewSourceVariant::Original;
    uint64_t draft_id = 0;
    uint64_t draft_revision = 0;
    uint64_t draft_submission_id = 0;
    uint32_t recipe_format_version = 0;
    uint32_t generator_abi = 0;
    uint32_t interface_abi = 0;
    uint64_t replacement_id = 0;
    uint64_t replacement_revision = 0;
    PreviewDigest source_digest{};
    PreviewDigest partner_digest{};
    PreviewDigest pipeline_layout_digest{};

    bool operator==(const PreviewCompileKey &other) const;
    bool operator!=(const PreviewCompileKey &other) const;
};

std::string PreviewSourceIdentity(const PreviewCompileKey &key);

struct PreviewResultKey {
    PreviewChannel channel = PreviewChannel::FinalRGBA;
    PreviewScene scene;
    PreviewRenderState render_state;
    uint64_t clock_revision = 0;
    uint64_t clock_edit_revision = 0;
    double time_seconds = 0.0;
    PreviewCompileKey compile;
    uint64_t input_revision = 0;
    uint8_t binding_count = 0;
    std::array<PreviewInputBinding, kPreviewMaxInputBindings> bindings{};
    uint64_t view_revision = 0;
    uint32_t width = 0;
    uint32_t height = 0;
    PreviewPacketKind packet_kind = PreviewPacketKind::Synthetic;
    PreviewReplayClass replay_class = PreviewReplayClass::Synthetic;
    PreviewDigest fixture_digest{};
    PreviewDigest mesh_digest{};
    PreviewDigest material_digest{};
    PreviewDigest pipeline_digest{};
    bool profile_draw = false;

    bool operator==(const PreviewResultKey &other) const;
    bool operator!=(const PreviewResultKey &other) const;
};

enum class PreviewDrawTimingStatus : uint8_t {
    Disarmed,
    Pending,
    Measured,
    Unsupported,
    Failed
};
enum class PreviewDrawTimingProvenance : uint8_t {
    None,
    SelectedPreviewInstrumented,
    ReplayInstrumented
};
constexpr uint64_t kPreviewMaxDrawTimingNs = UINT64_C(10000000000);
constexpr size_t kPreviewDrawTimingWindow = 64;
struct PreviewDrawTiming {
    PreviewResultKey result;
    PreviewDrawTimingStatus status = PreviewDrawTimingStatus::Disarmed;
    PreviewDrawTimingProvenance provenance = PreviewDrawTimingProvenance::None;
    PreviewBackend backend = PreviewBackend::Unknown;
    uint64_t nanoseconds = 0;
    uint32_t timestamp_valid_bits = 0;
    double timestamp_period_ns = 0;
    uint32_t actual_draw_commands = 0;
    std::string message;
};
// Last 64 measured event intervals; coverage counts refer to the entire
// explicit request. These are draw intervals, never individual shader stages.
struct PreviewDrawTimingDistribution {
    uint64_t requested = 0, measured = 0, pending = 0, unsupported = 0,
             failed = 0;
    uint32_t sample_count = 0;
    std::array<uint64_t, kPreviewDrawTimingWindow> samples{};
    double median_ns = 0, p95_ns = 0;
};
const char *PreviewDrawTimingStatusLabel(PreviewDrawTimingStatus);
const char *PreviewDrawTimingProvenanceLabel(PreviewDrawTimingProvenance);
bool ValidatePreviewDrawTiming(const PreviewDrawTiming &,
                               const PreviewResultKey &expected);
bool ComputePreviewDrawInterval(uint64_t start, uint64_t finish,
                                uint32_t valid_bits, double period_ns,
                                uint64_t *nanoseconds, std::string *error);
void AccumulatePreviewDrawTiming(PreviewDrawTimingDistribution *,
                                 const PreviewDrawTiming &);
void FinalizePreviewDrawTimingDistribution(PreviewDrawTimingDistribution *);

struct PreviewPacket {
    PreviewScene scene;
    PreviewRenderState render_state;
    PreviewSelection selection;
    // Descriptive provenance; source/partner digests govern compile identity.
    Route source_route = Route::Unknown;
    bool source_resident = false;
    PreviewSourceVariant source_variant = PreviewSourceVariant::Original;
    uint64_t draft_id = 0;
    uint64_t draft_revision = 0;
    uint64_t draft_submission_id = 0;
    uint32_t recipe_format_version = 0;
    std::vector<uint8_t> recipe;
    std::string source;
    std::string partner_source;
    std::vector<uint8_t> fixture_bytes;
    uint32_t generator_abi = 0;
    uint32_t interface_abi = 0;
    uint64_t replacement_id = 0;
    uint64_t replacement_revision = 0;
    PreviewDigest source_digest{};
    PreviewDigest partner_digest{};
    PreviewDigest fixture_digest{};
    PreviewCapturedMesh captured_mesh;
    PreviewDigest mesh_digest{};
    std::shared_ptr<const PreviewCapturedMaterial> captured_material;
    PreviewDigest material_digest{};
    std::shared_ptr<const PreviewCapturedPipeline> captured_pipeline;
    PreviewDigest pipeline_digest{}, pipeline_layout_digest{};
    uint64_t input_revision = 0;
    uint8_t binding_count = 0;
    std::array<PreviewInputBinding, kPreviewMaxInputBindings> bindings{};
    uint64_t view_revision = 0;
    uint32_t width = kPreviewFullExtent;
    uint32_t height = kPreviewFullExtent;
    PreviewUpdatePolicy update_policy = PreviewUpdatePolicy::OnDirty;
    PreviewPacketKind packet_kind = PreviewPacketKind::Synthetic;
    PreviewReplayClass replay_class = PreviewReplayClass::Synthetic;
    // Explicit profiling only. Disarmed packets never allocate/issue queries.
    bool profile_draw = false;
};

const char *PreviewModeLabel(PreviewMode mode);
const char *PreviewBackendLabel(PreviewBackend backend);
const char *PreviewStateLabel(PreviewState state);
const char *PreviewPressureLabel(PreviewPressure pressure);
const char *PreviewReplayClassLabel(PreviewReplayClass replay_class);

PreviewDigest ComputePreviewDigest(const uint8_t *data, size_t size);
PreviewDigest ComputeCapturedMeshDigest(const PreviewCapturedMesh &mesh);
size_t PreviewPacketOwnedBytes(const PreviewPacket &packet, bool *overflow);
bool ValidatePreviewPacket(const PreviewPacket &packet, std::string *error);
PreviewCompileKey BuildPreviewCompileKey(const PreviewPacket &packet);
PreviewResultKey BuildPreviewResultKey(const PreviewPacket &packet);

} // namespace xemu::shader_browser

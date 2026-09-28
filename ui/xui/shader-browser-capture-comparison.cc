// SPDX-License-Identifier: GPL-2.0-or-later
#include "shader-browser-capture-comparison.hh"
#include "shader-browser-capture-replay.hh"
#include "shader-browser-preview-adapter.hh"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <mutex>
#include <unordered_map>
#include <unordered_set>

namespace xemu::shader_browser {
namespace {
std::atomic<uint64_t> next_request{ 1 }, next_token{ 1 };
constexpr uint64_t kMaximumImages = 512U * 1024U * 1024U;
bool SettingsValid(const CaptureComparisonSettings &settings)
{
    return settings.maximum_rows && settings.maximum_rows <= 100000 &&
           settings.maximum_jobs &&
           settings.maximum_jobs <= settings.maximum_rows &&
           settings.packet_byte_budget &&
           settings.packet_byte_budget <= 2U * kPreviewMaxOwnedPacketBytes &&
           settings.image_byte_budget &&
           settings.image_byte_budget <= kMaximumImages;
}
bool Matches(const CaptureComparisonRequest &request,
             const CaptureOccurrence &event)
{
    if (event.type != CaptureEventType::Draw ||
        !(event.summary.scope == request.selection.scope))
        return false;
    if (request.scope == CaptureComparisonScope::SelectedOccurrence &&
        event.event_id != request.selected_event)
        return false;
    if (request.scope != CaptureComparisonScope::SelectedOccurrence &&
        (event.summary.key.frame < request.first_frame ||
         event.summary.key.frame > request.last_frame))
        return false;
    for (size_t i = 0; i < std::min<size_t>(event.summary.shader_count,
                                            event.summary.shaders.size());
         ++i)
        if (event.summary.shaders[i] == request.selection.shader)
            return true;
    return false;
}
std::string Source(const SharedCaptureBlock &block)
{
    return block ? std::string(block->bytes.begin(), block->bytes.end()) :
                   std::string{};
}
PreviewDigest SourceDigest(const SharedCaptureBlock &block)
{
    return block && block->bytes.size() <= kPreviewMaxSourceBytes ?
               ComputePreviewDigest(block->bytes.data(), block->bytes.size()) :
               PreviewDigest{};
}
template <typename Inputs>
uint32_t Register(const Inputs &inputs, const char *name, uint32_t fallback = 0)
{
    for (const auto &reg : inputs.registers)
        if (reg.name == name)
            return reg.value;
    return fallback;
}
bool SamePacketScope(const PreviewPacket &packet,
                     const CaptureComparisonRequest &request)
{
    return SamePreviewDisplayScope(packet.selection, request.selection) &&
           (!request.width || packet.width == request.width) &&
           (!request.height || packet.height == request.height);
}
bool EventRequest(const CaptureComparisonRequest &request,
                  const CaptureOccurrence &event,
                  CaptureComparisonRequest *local, std::string *error)
{
    if (event.summary.shader_count > event.summary.shaders.size()) {
        if (error)
            *error = "Dependency draw stage count exceeds its bound";
        return false;
    }
    *local = request;
    local->dependencies = CaptureComparisonDependencies::FrozenInputs;
    const ShaderKey *pixel = nullptr;
    for (size_t i = 0; i < std::min<size_t>(event.summary.shader_count,
                                            event.summary.shaders.size());
         ++i)
        if (event.summary.shaders[i].stage == Stage::Pixel) {
            if (pixel) {
                if (error)
                    *error =
                        "Dependency draw has ambiguous pixel stage identity";
                return false;
            }
            pixel = &event.summary.shaders[i];
        }
    if (!pixel || !pixel->hash.version ||
        !(event.summary.scope == request.selection.scope)) {
        if (error)
            *error = "Dependency draw has no pixel stage in the captured build";
        return false;
    }
    local->selection.shader = *pixel;
    local->selection.scope = event.summary.scope;
    return true;
}
bool SameFrozenInputs(const PreviewPacket &original,
                      const PreviewPacket &replacement)
{
    auto a = BuildPreviewResultKey(original),
         b = BuildPreviewResultKey(replacement);
    const auto &ac = a.compile, &bc = b.compile;
    if (ac.selection != bc.selection ||
        ac.recipe_format_version != bc.recipe_format_version ||
        ac.generator_abi != bc.generator_abi ||
        ac.interface_abi != bc.interface_abi ||
        ac.partner_digest != bc.partner_digest ||
        ac.pipeline_layout_digest != bc.pipeline_layout_digest ||
        original.recipe != replacement.recipe ||
        original.source_route != replacement.source_route ||
        original.source_resident != replacement.source_resident ||
        original.update_policy != PreviewUpdatePolicy::OnDirty ||
        replacement.update_policy != PreviewUpdatePolicy::OnDirty)
        return false;
    // Only the edited pixel source and its draft identity may differ. All
    // render inputs, extents and frozen vertex/geometry stage data must match.
    b.compile = a.compile;
    return a == b;
}
CaptureComparisonDifference Difference(const CaptureComparisonImage &a,
                                       const CaptureComparisonImage &b)
{
    CaptureComparisonDifference result;
    result.compared_bytes = a.rgba.size();
    result.compared_pixels = a.rgba.size() / 4;
    uint64_t sum = 0;
    for (size_t i = 0; i < a.rgba.size(); i += 4) {
        bool changed = false;
        for (size_t channel = 0; channel < 4; ++channel) {
            const uint8_t delta = uint8_t(
                std::abs(int(a.rgba[i + channel]) - int(b.rgba[i + channel])));
            sum += delta;
            result.maximum_difference =
                std::max(result.maximum_difference, delta);
            if (delta) {
                ++result.changed_bytes;
                changed = true;
            }
        }
        if (changed)
            ++result.changed_pixels;
    }
    if (result.compared_bytes)
        result.mean_difference = double(sum) / double(result.compared_bytes);
    return result;
}
} // namespace

bool CaptureComparisonIdentity::operator==(
    const CaptureComparisonIdentity &other) const
{
    return request_id == other.request_id && token == other.token &&
           event_id == other.event_id &&
           original_revision == other.original_revision &&
           replacement_revision == other.replacement_revision &&
           original_digest == other.original_digest &&
           replacement_digest == other.replacement_digest;
}

bool GetCapturedPreviewExtent(const OwnedDrawInputs &inputs,
                              PreviewBackend backend, uint32_t *width,
                              uint32_t *height, std::string *error)
{
    if (width)
        *width = 0;
    if (height)
        *height = 0;
    if (error)
        error->clear();
    auto fail = [&](const char *message) {
        if (error)
            *error = message;
        return false;
    };
    if (!width || !height)
        return fail("Captured viewport extent destinations are missing");
    const char *name =
        backend == PreviewBackend::OpenGL ? "host.viewport" : "vk.viewport";
    if (backend != PreviewBackend::OpenGL && backend != PreviewBackend::Vulkan)
        return fail("Captured viewport backend is unsupported");
    const OwnedDrawBlob *found = nullptr;
    for (const auto &blob : inputs.blobs)
        if (blob.name == name) {
            if (found)
                return fail("Captured viewport evidence is duplicated");
            found = &blob;
        }
    if (!found)
        return fail("Captured native viewport evidence is missing");
    uint32_t w = 0, h = 0;
    if (backend == PreviewBackend::OpenGL) {
        int32_t viewport[4];
        if (found->bytes.size() != sizeof(viewport))
            return fail("Captured OpenGL viewport byte length is invalid");
        std::memcpy(viewport, found->bytes.data(), sizeof(viewport));
        if (viewport[0] || viewport[1])
            return fail("Captured viewport origin offsets are unsupported");
        if (viewport[2] <= 0 || viewport[3] <= 0)
            return fail("Captured viewport extent is not positive");
        w = uint32_t(viewport[2]);
        h = uint32_t(viewport[3]);
    } else {
        float viewport[6];
        if (found->bytes.size() != sizeof(viewport))
            return fail("Captured Vulkan viewport byte length is invalid");
        std::memcpy(viewport, found->bytes.data(), sizeof(viewport));
        if (viewport[0] != 0 || viewport[1] != 0)
            return fail("Captured viewport origin offsets are unsupported");
        if (!std::isfinite(viewport[2]) || !std::isfinite(viewport[3]) ||
            viewport[2] <= 0 || viewport[3] <= 0 ||
            viewport[2] > kPreviewMaxCapturedWidth ||
            viewport[3] > kPreviewMaxCapturedHeight ||
            std::trunc(viewport[2]) != viewport[2] ||
            std::trunc(viewport[3]) != viewport[3])
            return fail("Captured Vulkan viewport requires a positive integral "
                        "extent within 1920 x 1080");
        w = uint32_t(viewport[2]);
        h = uint32_t(viewport[3]);
    }
    if (!PreviewExtentWithinLimits(w, h, true))
        return fail("Captured viewport exceeds the 1920 x 1080 native preview "
                    "limit; no clamping was applied");
    *width = w;
    *height = h;
    return true;
}

CaptureComparisonBuildOutcome BuildCaptureComparisonPackets(
    const CaptureComparisonRequest &request, const CaptureOccurrence &event,
    PreviewPacket *original, PreviewPacket *replacement, std::string *error)
{
    auto fail = [&](CaptureComparisonBuildOutcome outcome,
                    const std::string &message) {
        if (error)
            *error = message;
        return outcome;
    };
    if (!original || !replacement || event.pending || !event.inputs.complete ||
        !event.finished)
        return fail(CaptureComparisonBuildOutcome::Incomplete,
                    "Captured payload is not finalized");
    if (request.dependencies != CaptureComparisonDependencies::FrozenInputs)
        return fail(CaptureComparisonBuildOutcome::Unsupported,
                    "Producer suffix replay requires dependency closure "
                    "instrumentation");
    if (!event.emitted)
        return fail(CaptureComparisonBuildOutcome::Unsupported,
                    "This occurrence emitted no GPU draw command");
    OwnedDrawInputs owned = event.CopyInputs();
    uint32_t width = request.width, height = request.height;
    if (!width && !height &&
        !GetCapturedPreviewExtent(owned, request.selection.backend, &width,
                                  &height, error))
        return CaptureComparisonBuildOutcome::Unsupported;
    if (!width || !height)
        return fail(CaptureComparisonBuildOutcome::Unsupported,
                    "Specify both preview dimensions or neither");
    if (owned.sources[1].empty() || owned.sources[2].empty())
        return fail(CaptureComparisonBuildOutcome::Incomplete,
                    "Captured vertex and pixel sources are required");
    PreviewPacketInputs inputs;
    inputs.selection = request.selection;
    const auto route =
        Register(owned, "capture.route", uint32_t(Route::Unknown));
    inputs.source_route =
        route <= uint32_t(Route::Disabled) ? Route(route) : Route::Unknown;
    inputs.selection.mode = inputs.source_route == Route::Uber ?
                                PreviewMode::Uber :
                                PreviewMode::Normal;
    inputs.source_resident = true;
    inputs.source = owned.sources[2];
    inputs.partner_source = owned.sources[1];
    inputs.recipe.key = request.selection.shader;
    inputs.recipe.recipe_format_version = 1;
    inputs.recipe.scopes = { event.summary.scope };
    for (const auto &blob : owned.blobs)
        if (blob.name == "recipe.stage2")
            inputs.recipe.bytes = blob.bytes;
    if (inputs.recipe.bytes.empty())
        return fail(CaptureComparisonBuildOutcome::Incomplete,
                    "Captured pixel recipe is missing");
    inputs.generator_abi = Register(owned, "capture.generator_abi");
    inputs.interface_abi = Register(owned, "capture.interface_abi");
    if (!inputs.generator_abi || !inputs.interface_abi)
        return fail(CaptureComparisonBuildOutcome::Incomplete,
                    "Captured source ABI metadata is missing");
    inputs.fixture_bytes = EncodePreviewSyntheticFixture(
        MakePreviewFixture(PreviewFixtureProfile::Flat));
    inputs.scene = request.scene;
    inputs.render_state = request.render_state;
    inputs.input_revision = event.event_id;
    inputs.view_revision = event.event_id;
    inputs.width = width;
    inputs.height = height;
    inputs.captured_pipeline = BuildPreviewCapturedPipeline(
        owned, request.selection.backend, event.summary.primitive_mode, error);
    if (!inputs.captured_pipeline)
        return CaptureComparisonBuildOutcome::Unsupported;
    PreviewPacket baseline;
    if (!BuildPreviewPacket(inputs, &baseline, error))
        return CaptureComparisonBuildOutcome::Unsupported;
    auto geometry = event.CopyGeometry();
    baseline.captured_mesh = { std::move(geometry.positions),
                               std::move(geometry.indices) };
    baseline.mesh_digest = ComputeCapturedMeshDigest(baseline.captured_mesh);
    baseline.captured_material =
        BuildPreviewCapturedMaterial(owned, request.selection.backend);
    baseline.material_digest =
        ComputePreviewCapturedMaterialDigest(*baseline.captured_material);
    baseline.packet_kind = PreviewPacketKind::Replay;
    baseline.replay_class = PreviewReplayClass::Approximate;
    baseline.profile_draw = request.profile_draw;
    if (!ValidatePreviewPacket(baseline, error))
        return CaptureComparisonBuildOutcome::Unsupported;
    PreviewPacket edited = baseline;
    edited.source = request.edit.source;
    edited.source_variant = PreviewSourceVariant::Edited;
    edited.source_digest = ComputePreviewDigest(
        reinterpret_cast<const uint8_t *>(edited.source.data()),
        edited.source.size());
    edited.replacement_id = request.edit.id;
    edited.replacement_revision = request.edit.revision;
    edited.draft_id = request.edit.id;
    edited.draft_revision = request.edit.revision;
    edited.draft_submission_id = request.edit.submission_id;
    if (!ValidatePreviewPacket(edited, error))
        return CaptureComparisonBuildOutcome::Unsupported;
    *original = std::move(baseline);
    *replacement = std::move(edited);
    if (error)
        error->clear();
    return CaptureComparisonBuildOutcome::Ready;
}

struct CaptureComparisonController::Impl {
    struct Account {
        std::atomic<uint64_t> packets{ 0 }, images{ 0 };
    };
    struct Record {
        CaptureComparisonResult result;
        std::shared_ptr<const CaptureOccurrence> occurrence;
        std::shared_ptr<const PreviewPacket> original, replacement;
        CaptureComparisonPhase phase = CaptureComparisonPhase::Original;
        bool claimed = false, building = false;
    };
    mutable std::mutex mutex;
    std::atomic<uint64_t> revision{ 1 };
    CaptureComparisonState state = CaptureComparisonState::Idle;
    CaptureComparisonRequest request;
    CaptureComparisonSettings settings;
    std::shared_ptr<Account> account = std::make_shared<Account>();
    std::vector<Record> rows;
    uint64_t request_id = 0, matches = 0, omitted = 0, admitted = 0, stale = 0,
             pending_rows = 0;
    size_t cursor = 0;
    bool builder_active = false;
    std::string message;
    CaptureReplaySequence replay;
    std::shared_ptr<const CaptureReplayPlan> replay_plan;
    std::shared_ptr<const CaptureReplayDraw> replay_draw;
    std::unordered_map<uint64_t, size_t> replay_rows;
    bool Suffix() const
    {
        return request.dependencies ==
                   CaptureComparisonDependencies::ProducerSuffix &&
               bool(replay_plan);
    }
    void Resting()
    {
        if (state != CaptureComparisonState::Running)
            return;
        if (pending_rows)
            return;
        if (Suffix() && replay.Status() != CaptureReplayStatus::Completed)
            return;
        state = CaptureComparisonState::Ready;
        ++revision;
    }
    void Finish(Record &row, CaptureComparisonOutcome outcome,
                const std::string &reason)
    {
        if (row.result.outcome == CaptureComparisonOutcome::Pending &&
            outcome != CaptureComparisonOutcome::Pending)
            --pending_rows;
        row.result.outcome = outcome;
        row.result.message = reason.substr(0, 1024);
        row.claimed = row.building = false;
        row.original.reset();
        row.replacement.reset();
        ++revision;
        Resting();
    }
    void Cancel()
    {
        replay.Cancel();
        replay_draw.reset();
        replay_plan.reset();
        replay_rows.clear();
        for (auto &row : rows)
            if (row.result.outcome == CaptureComparisonOutcome::Pending)
                Finish(row, CaptureComparisonOutcome::Cancelled,
                       "Comparison request cancelled");
        state = CaptureComparisonState::Cancelled;
        ++revision;
    }
    void RejectSuffix(CaptureComparisonOutcome outcome,
                      const std::string &reason)
    {
        replay.Cancel();
        replay_draw.reset();
        replay_plan.reset();
        replay_rows.clear();
        admitted = 0;
        for (auto &row : rows)
            Finish(row, outcome, reason);
        message = reason;
        state = CaptureComparisonState::Ready;
    }
    void FailSuffix(Record *failed, CaptureComparisonOutcome outcome,
                    const std::string &reason)
    {
        replay.Cancel();
        replay_draw.reset();
        for (auto &row : rows)
            if (row.result.outcome == CaptureComparisonOutcome::Pending)
                Finish(row,
                       &row == failed ? outcome :
                                        CaptureComparisonOutcome::Incomplete,
                       &row == failed ? reason :
                                        "Dependency replay stopped before this "
                                        "comparison completed: " +
                                            reason);
        message = reason;
        state = CaptureComparisonState::Failed;
        ++revision;
    }
    void StartSuffix(const CaptureSessionSnapshot &snapshot)
    {
        if (!matches) {
            message = "No matching seed occurrences";
            Resting();
            return;
        }
        if (omitted) {
            RejectSuffix(CaptureComparisonOutcome::Incomplete,
                         "Producer suffix exceeds the seed row budget; no "
                         "partial seed selection was replayed");
            return;
        }
        try {
            std::vector<uint64_t> seeds;
            for (const auto &row : rows)
                seeds.push_back(row.occurrence->event_id);
            CaptureReplayLimits limits;
            limits.plan_byte_budget = settings.packet_byte_budget;
            limits.value_byte_budget = settings.image_byte_budget;
            auto plan = std::make_shared<CaptureReplayPlan>(
                BuildCaptureReplayPlan(snapshot, seeds, limits));
            if (plan->status != CaptureReplayStatus::Ready ||
                !plan->logical_closure_complete) {
                RejectSuffix(plan->status ==
                                     CaptureReplayStatus::BudgetExceeded ?
                                 CaptureComparisonOutcome::Incomplete :
                                 CaptureComparisonOutcome::Unsupported,
                             plan->reason);
                return;
            }
            std::vector<Record> expanded;
            std::unordered_map<uint64_t, size_t> indices;
            std::unordered_map<const CaptureImmutableBlock *, PreviewDigest>
                digests;
            const auto edit_digest = ComputePreviewDigest(
                reinterpret_cast<const uint8_t *>(request.edit.source.data()),
                request.edit.source.size());
            for (const auto &step : plan->steps) {
                if (step.description->kind !=
                    uint32_t(CaptureCommandKind::Draw))
                    continue;
                if (expanded.size() == settings.maximum_rows ||
                    expanded.size() == settings.maximum_jobs) {
                    RejectSuffix(CaptureComparisonOutcome::Incomplete,
                                 "Producer suffix draw or result row budget "
                                 "exceeded; the full chain was not admitted");
                    return;
                }
                CaptureComparisonRequest local;
                std::string error;
                const auto &event = *step.occurrence;
                if (!EventRequest(request, event, &local, &error) ||
                    !event.inputs.sources[2] || !event.inputs.sources[2]->id ||
                    event.inputs.sources[2]->bytes.empty() ||
                    event.inputs.sources[2]->bytes.size() >
                        kPreviewMaxSourceBytes) {
                    RejectSuffix(CaptureComparisonOutcome::Unsupported,
                                 error.empty() ?
                                     "Dependency pixel source is "
                                     "missing or exceeds its bound" :
                                     error);
                    return;
                }
                const auto block = event.inputs.sources[2];
                auto inserted = digests.emplace(block.get(), PreviewDigest{});
                if (inserted.second)
                    inserted.first->second = SourceDigest(block);
                Record row;
                row.occurrence = step.occurrence;
                row.result.identity = {
                    request_id,
                    next_token.fetch_add(1),
                    event.event_id,
                    block->id,
                    step.edited_seed ? request.edit.revision : block->id,
                    inserted.first->second,
                    step.edited_seed ? edit_digest : inserted.first->second
                };
                row.result.edited_seed = step.edited_seed;
                row.result.frame = event.summary.key.frame;
                row.result.capture_limitations = event.limitations;
                indices.emplace(event.event_id, expanded.size());
                expanded.push_back(std::move(row));
            }
            if (expanded.empty() || !replay.Start(plan, limits)) {
                RejectSuffix(CaptureComparisonOutcome::Incomplete,
                             "Dependency replay initialization failed: " +
                                 replay.Reason());
                return;
            }
            rows = std::move(expanded);
            replay_rows = std::move(indices);
            replay_plan = std::move(plan);
            cursor = 0;
            admitted = pending_rows = rows.size();
            message = "Original and edited logical dependency branches; raw "
                      "GPU coverage gaps remain and copies and clears are "
                      "reconstructed CPU operations";
            ++revision;
        } catch (const std::exception &error) {
            RejectSuffix(CaptureComparisonOutcome::Incomplete,
                         std::string("Dependency replay admission failed: ") +
                             error.what());
        }
    }
    bool ClaimSuffix(CaptureComparisonJob *job,
                     const CaptureComparisonPacketBuilder &builder,
                     std::unique_lock<std::mutex> &lock)
    {
        if (!Suffix() || state != CaptureComparisonState::Running ||
            replay_draw)
            return false;
        CaptureReplayDraw incoming;
        std::string error;
        if (!replay.TryClaimDraw(&incoming, &error)) {
            if (replay.Status() == CaptureReplayStatus::Completed &&
                !pending_rows)
                Resting();
            else
                FailSuffix(nullptr, CaptureComparisonOutcome::Incomplete,
                           error.empty() ? replay.Reason() : error);
            return false;
        }
        std::shared_ptr<const CaptureReplayDraw> owned_draw;
        try {
            owned_draw =
                std::make_shared<CaptureReplayDraw>(std::move(incoming));
        } catch (const std::exception &exception) {
            FailSuffix(nullptr, CaptureComparisonOutcome::Incomplete,
                       exception.what());
            return false;
        }
        const auto &draw = *owned_draw;
        auto found = replay_rows.find(draw.identity.event_id);
        if (found == replay_rows.end()) {
            FailSuffix(nullptr, CaptureComparisonOutcome::Incomplete,
                       "Dependency draw has no admitted result row");
            return false;
        }
        cursor = found->second;
        auto &row = rows[cursor];
        row.phase = draw.identity.branch ? CaptureComparisonPhase::Replacement :
                                           CaptureComparisonPhase::Original;
        if (row.result.outcome != CaptureComparisonOutcome::Pending ||
            (row.phase == CaptureComparisonPhase::Original &&
             row.result.original) ||
            (row.phase == CaptureComparisonPhase::Replacement &&
             !row.result.original)) {
            FailSuffix(
                &row, CaptureComparisonOutcome::Incomplete,
                "Dependency result phase has no matching original branch");
            return false;
        }
        CaptureComparisonRequest local;
        try {
            if (!EventRequest(request, *draw.occurrence, &local, &error)) {
                FailSuffix(&row, CaptureComparisonOutcome::Unsupported, error);
                return false;
            }
        } catch (const std::exception &exception) {
            FailSuffix(&row, CaptureComparisonOutcome::Incomplete,
                       exception.what());
            return false;
        }
        row.building = builder_active = true;
        const auto identity = row.result.identity;
        const auto phase = row.phase;
        replay_draw = owned_draw;
        lock.unlock();
        PreviewPacket original, replacement, bound;
        CaptureComparisonBuildOutcome outcome;
        try {
            outcome = builder ?
                          builder(local, *draw.occurrence, &original,
                                  &replacement, &error) :
                          BuildCaptureComparisonPackets(local, *draw.occurrence,
                                                        &original, &replacement,
                                                        &error);
            if (outcome == CaptureComparisonBuildOutcome::Ready) {
                auto &base = draw.edited_seed ? replacement : original;
                base.profile_draw = local.profile_draw;
                const bool contract =
                    SamePacketScope(base, local) &&
                    base.input_revision == identity.event_id &&
                    base.source_variant ==
                        (draw.edited_seed ? PreviewSourceVariant::Edited :
                                            PreviewSourceVariant::Original) &&
                    base.source_digest == (draw.edited_seed ?
                                               identity.replacement_digest :
                                               identity.original_digest) &&
                    base.source ==
                        (draw.edited_seed ?
                             local.edit.source :
                             Source(draw.occurrence->inputs.sources[2])) &&
                    base.partner_source ==
                        Source(draw.occurrence->inputs.sources[1]) &&
                    base.generator_abi == Register(draw.occurrence->inputs,
                                                   "capture.generator_abi") &&
                    base.interface_abi == Register(draw.occurrence->inputs,
                                                   "capture.interface_abi") &&
                    base.captured_pipeline &&
                    base.captured_pipeline->geometry_source ==
                        Source(draw.occurrence->inputs.sources[3]) &&
                    (draw.edited_seed ?
                         base.replacement_id == local.edit.id &&
                             base.replacement_revision == local.edit.revision &&
                             base.draft_id == local.edit.id &&
                             base.draft_revision == local.edit.revision &&
                             base.draft_submission_id ==
                                 local.edit.submission_id :
                         !base.replacement_id && !base.replacement_revision &&
                             !base.draft_id && !base.draft_revision &&
                             !base.draft_submission_id);
                if (!contract || !ValidatePreviewPacket(base, &error)) {
                    outcome = CaptureComparisonBuildOutcome::Incomplete;
                    if (!contract)
                        error =
                            "Dependency packet changed its event-local source, "
                            "stage, ABI or ownership identity";
                } else if (!RebindCaptureReplayPacket(draw, base, &bound,
                                                      &error)) {
                    outcome = CaptureComparisonBuildOutcome::Unsupported;
                }
            }
        } catch (const std::exception &exception) {
            outcome = CaptureComparisonBuildOutcome::Incomplete;
            error = exception.what();
        }
        lock.lock();
        builder_active = false;
        if (state != CaptureComparisonState::Running ||
            request_id != identity.request_id || cursor >= rows.size() ||
            !(rows[cursor].result.identity == identity) || !replay_draw ||
            !(replay_draw->identity == draw.identity))
            return false;
        auto &built = rows[cursor];
        built.building = false;
        if (outcome != CaptureComparisonBuildOutcome::Ready) {
            FailSuffix(&built,
                       outcome == CaptureComparisonBuildOutcome::Unsupported ?
                           CaptureComparisonOutcome::Unsupported :
                           CaptureComparisonOutcome::Incomplete,
                       error);
            return false;
        }
        bool overflow = false;
        const auto bytes = PreviewPacketOwnedBytes(bound, &overflow);
        if (overflow || bytes > settings.packet_byte_budget ||
            account->packets.load() > settings.packet_byte_budget - bytes) {
            FailSuffix(&built, CaptureComparisonOutcome::Incomplete,
                       "Dependency packet byte budget exceeded");
            return false;
        }
        std::shared_ptr<const PreviewPacket> owned;
        try {
            owned = OwnPacket(std::move(bound), bytes);
        } catch (const std::exception &exception) {
            FailSuffix(&built, CaptureComparisonOutcome::Incomplete,
                       exception.what());
            return false;
        }
        const auto expected = BuildPreviewResultKey(*owned);
        if (!replay.ExpectResult(draw.identity, expected)) {
            FailSuffix(
                &built, CaptureComparisonOutcome::Incomplete,
                "Dependency packet result does not match its logical target");
            return false;
        }
        if (phase == CaptureComparisonPhase::Original) {
            built.original = owned;
            built.result.original_key = expected;
            built.result.replay_class = owned->replay_class;
        } else {
            built.replacement = owned;
            built.result.replacement_key = expected;
            built.result.has_packet_identity = true;
        }
        built.claimed = true;
        job->identity = identity;
        job->phase = phase;
        job->edited_seed = draw.edited_seed;
        job->occurrence = draw.occurrence;
        job->packet = std::move(owned);
        job->expected_result = expected;
        ++revision;
        return true;
    }
    bool Current(const CaptureComparisonIdentity &identity,
                 CaptureComparisonPhase phase) const
    {
        return state == CaptureComparisonState::Running &&
               cursor < rows.size() &&
               rows[cursor].result.outcome ==
                   CaptureComparisonOutcome::Pending &&
               rows[cursor].claimed &&
               rows[cursor].result.identity == identity &&
               rows[cursor].phase == phase;
    }
    std::shared_ptr<const PreviewPacket> OwnPacket(PreviewPacket packet,
                                                   uint64_t bytes)
    {
        auto budget = account;
        auto owned = std::make_unique<PreviewPacket>(std::move(packet));
        account->packets.fetch_add(bytes);
        return { owned.release(), [budget, bytes](const PreviewPacket *p) {
                    delete p;
                    budget->packets.fetch_sub(bytes);
                } };
    }
    SharedComparisonImage OwnImage(CaptureComparisonImage image, uint64_t bytes)
    {
        auto budget = account;
        auto owned = std::make_unique<CaptureComparisonImage>(std::move(image));
        account->images.fetch_add(bytes);
        return { owned.release(),
                 [budget, bytes](const CaptureComparisonImage *p) {
                     delete p;
                     budget->images.fetch_sub(bytes);
                 } };
    }
};
CaptureComparisonController::CaptureComparisonController()
    : impl_(std::make_unique<Impl>())
{
}
CaptureComparisonController::~CaptureComparisonController() = default;
uint64_t CaptureComparisonController::Revision() const
{
    return impl_->revision.load();
}
uint64_t
CaptureComparisonController::Start(const CaptureComparisonRequest &request,
                                   const CaptureSessionSnapshot &snapshot,
                                   const CaptureComparisonSettings &settings)
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    auto &s = *impl_;
    s.Cancel();
    s.rows.clear();
    s.cursor = 0;
    s.matches = s.omitted = s.admitted = s.pending_rows = 0;
    s.request_id = next_request.fetch_add(1);
    s.message.clear();
    ++s.revision;
    if (!SettingsValid(settings) ||
        request.selection.shader.stage != Stage::Pixel ||
        !request.selection.shader.hash.version ||
        !request.selection.scope.title_id || !request.selection.session_epoch ||
        !request.selection.renderer_epoch ||
        !(request.selection.scope == snapshot.context.scope) ||
        (request.selection.backend != PreviewBackend::OpenGL &&
         request.selection.backend != PreviewBackend::Vulkan) ||
        uint32_t(request.selection.backend) != snapshot.context.backend ||
        !request.edit.id || !request.edit.revision ||
        !request.edit.submission_id || request.edit.source.empty() ||
        request.edit.source.size() > kPreviewMaxSourceBytes ||
        request.edit.source.find('\0') != std::string::npos ||
        (!request.width != !request.height) ||
        request.width > kPreviewMaxCapturedWidth ||
        request.height > kPreviewMaxCapturedHeight ||
        request.first_frame > request.last_frame ||
        uint32_t(request.scope) >
            uint32_t(CaptureComparisonScope::MatchingRange) ||
        uint32_t(request.dependencies) >
            uint32_t(CaptureComparisonDependencies::ProducerSuffix) ||
        (request.scope == CaptureComparisonScope::MatchingFrame &&
         request.first_frame != request.last_frame)) {
        s.state = CaptureComparisonState::Failed;
        s.message = "Invalid comparison request, renderer, source or bounds";
        return 0;
    }
    s.request = request;
    s.settings = settings;
    s.state = CaptureComparisonState::Running;
    PreviewDigest replacement_digest = ComputePreviewDigest(
        reinterpret_cast<const uint8_t *>(request.edit.source.data()),
        request.edit.source.size());
    std::unordered_set<uint64_t> ids;
    std::unordered_map<const CaptureImmutableBlock *, PreviewDigest>
        source_digests;
    for (const auto &event : snapshot.events) {
        if (!event || !Matches(request, *event))
            continue;
        ++s.matches;
        if (s.rows.size() == settings.maximum_rows) {
            ++s.omitted;
            continue;
        }
        Impl::Record row;
        row.occurrence = event;
        auto &result = row.result;
        PreviewDigest original_digest{};
        if (event->inputs.sources[2]) {
            auto inserted = source_digests.emplace(
                event->inputs.sources[2].get(), PreviewDigest{});
            if (inserted.second)
                inserted.first->second = SourceDigest(event->inputs.sources[2]);
            original_digest = inserted.first->second;
        }
        result.identity = { s.request_id,
                            next_token.fetch_add(1),
                            event->event_id,
                            event->inputs.sources[2] ?
                                event->inputs.sources[2]->id :
                                0,
                            request.edit.revision,
                            original_digest,
                            replacement_digest };
        result.frame = event->summary.key.frame;
        result.capture_limitations = event->limitations;
        auto reject = [&](CaptureComparisonOutcome outcome,
                          const char *reason) {
            result.outcome = outcome;
            result.message = reason;
        };
        if (!event->event_id || !ids.insert(event->event_id).second)
            reject(CaptureComparisonOutcome::Incomplete,
                   "Invalid or duplicate occurrence identity");
        else if (event->pending || !event->finished ||
                 !event->inputs.complete ||
                 (event->limitations &
                  (CaptureReadbackFailed | CaptureInvalidated)))
            reject(CaptureComparisonOutcome::Incomplete,
                   "Occurrence inputs are incomplete, failed or invalidated");
        else if (!event->emitted)
            reject(CaptureComparisonOutcome::Unsupported,
                   "This occurrence emitted no GPU draw command");
        else if (event->batch_id &&
                 (!event->command_recorded ||
                  event->submission != CaptureBatchOutcome::Submitted))
            reject(CaptureComparisonOutcome::Unsupported,
                   "The recorded command has no accepted GPU submission");
        else if (event->batch_id &&
                 event->completion == CaptureBatchCompletion::Failed)
            reject(
                CaptureComparisonOutcome::Incomplete,
                "GPU submission completion failed; its output is unverified");
        else if (event->limitations & (CaptureUnsupported | CaptureMalformed))
            reject(CaptureComparisonOutcome::Unsupported,
                   "Occurrence contains unsupported or malformed raw evidence");
        else if (!event->inputs.sources[2] ||
                 event->inputs.sources[2]->bytes.size() >
                     kPreviewMaxSourceBytes)
            reject(CaptureComparisonOutcome::Incomplete,
                   "Captured pixel source is missing or exceeds its bound");
        else if (s.admitted == settings.maximum_jobs)
            reject(CaptureComparisonOutcome::Incomplete,
                   "Comparison job budget exceeded; this use was not executed");
        else {
            ++s.admitted;
            ++s.pending_rows;
        }
        s.rows.push_back(std::move(row));
    }
    if (request.dependencies == CaptureComparisonDependencies::ProducerSuffix) {
        s.StartSuffix(snapshot);
        return s.request_id;
    }
    if (s.omitted)
        s.message = "Result row budget exceeded; omitted matched uses are "
                    "counted explicitly";
    else
        s.message = "Frozen captured inputs; image changes are measurements, "
                    "not a regression judgment";
    s.Resting();
    return s.request_id;
}
bool CaptureComparisonController::TryClaimJob(
    CaptureComparisonJob *job, const CaptureComparisonPacketBuilder &builder)
{
    if (!job)
        return false;
    *job = {};
    std::unique_lock<std::mutex> lock(impl_->mutex);
    auto &s = *impl_;
    if (s.builder_active)
        return false;
    if (s.request.dependencies == CaptureComparisonDependencies::ProducerSuffix)
        return s.ClaimSuffix(job, builder, lock);
    while (s.state == CaptureComparisonState::Running) {
        while (s.cursor < s.rows.size() &&
               s.rows[s.cursor].result.outcome !=
                   CaptureComparisonOutcome::Pending)
            ++s.cursor;
        if (s.cursor == s.rows.size()) {
            s.Resting();
            return false;
        }
        auto &row = s.rows[s.cursor];
        if (row.claimed || row.building)
            return false;
        if (!row.original) {
            row.building = true;
            s.builder_active = true;
            const auto identity = row.result.identity;
            const auto occurrence = row.occurrence;
            const auto request = s.request;
            lock.unlock();
            PreviewPacket original, replacement;
            std::string error;
            CaptureComparisonBuildOutcome outcome;
            try {
                outcome = builder ? builder(request, *occurrence, &original,
                                            &replacement, &error) :
                                    BuildCaptureComparisonPackets(
                                        request, *occurrence, &original,
                                        &replacement, &error);
            } catch (const std::exception &exception) {
                outcome = CaptureComparisonBuildOutcome::Incomplete;
                error = exception.what();
            }
            lock.lock();
            s.builder_active = false;
            if (s.state != CaptureComparisonState::Running ||
                identity.request_id != s.request_id ||
                s.cursor >= s.rows.size() ||
                !(s.rows[s.cursor].result.identity == identity))
                return false;
            auto &built = s.rows[s.cursor];
            built.building = false;
            if (outcome != CaptureComparisonBuildOutcome::Ready) {
                s.Finish(built,
                         outcome == CaptureComparisonBuildOutcome::Unsupported ?
                             CaptureComparisonOutcome::Unsupported :
                             CaptureComparisonOutcome::Incomplete,
                         error);
                continue;
            }
            original.profile_draw = replacement.profile_draw =
                request.profile_draw;
            const bool contract =
                SamePacketScope(original, request) &&
                SamePacketScope(replacement, request) &&
                SameFrozenInputs(original, replacement) &&
                original.input_revision == identity.event_id &&
                replacement.input_revision == identity.event_id &&
                original.source_variant == PreviewSourceVariant::Original &&
                replacement.source_variant == PreviewSourceVariant::Edited &&
                original.source_digest == identity.original_digest &&
                replacement.source_digest == identity.replacement_digest &&
                original.source == Source(occurrence->inputs.sources[2]) &&
                replacement.source == request.edit.source &&
                original.partner_source ==
                    Source(occurrence->inputs.sources[1]) &&
                replacement.replacement_id == request.edit.id &&
                replacement.replacement_revision == request.edit.revision &&
                replacement.draft_id == request.edit.id &&
                replacement.draft_revision == request.edit.revision &&
                replacement.draft_submission_id == request.edit.submission_id;
            if (!contract || !ValidatePreviewPacket(original, &error) ||
                !ValidatePreviewPacket(replacement, &error)) {
                s.Finish(built, CaptureComparisonOutcome::Incomplete,
                         contract ? error :
                                    "Packet pair changed frozen inputs, "
                                    "occurrence, source or ownership identity");
                continue;
            }
            // Each diagnostic run needs fresh owned images and timings, while
            // unchanged source and pipeline compilation can remain cached.
            original.view_revision = replacement.view_revision = identity.token;
            bool overflow_a = false, overflow_b = false;
            const uint64_t a = PreviewPacketOwnedBytes(original, &overflow_a),
                           b = PreviewPacketOwnedBytes(replacement,
                                                       &overflow_b);
            if (overflow_a || overflow_b || a > s.settings.packet_byte_budget ||
                b > s.settings.packet_byte_budget - a ||
                s.account->packets.load() >
                    s.settings.packet_byte_budget - a - b) {
                s.Finish(built, CaptureComparisonOutcome::Incomplete,
                         "Comparison packet byte budget exceeded");
                continue;
            }
            try {
                built.original = s.OwnPacket(std::move(original), a);
                built.replacement = s.OwnPacket(std::move(replacement), b);
            } catch (const std::exception &exception) {
                s.Finish(built, CaptureComparisonOutcome::Incomplete,
                         exception.what());
                continue;
            }
            built.result.original_key = BuildPreviewResultKey(*built.original);
            built.result.replacement_key =
                BuildPreviewResultKey(*built.replacement);
            built.result.has_packet_identity = true;
            built.result.replay_class = built.original->replay_class;
        }
        auto &ready = s.rows[s.cursor];
        ready.claimed = true;
        job->identity = ready.result.identity;
        job->phase = ready.phase;
        job->occurrence = ready.occurrence;
        job->packet = ready.phase == CaptureComparisonPhase::Original ?
                          ready.original :
                          ready.replacement;
        job->expected_result = ready.phase == CaptureComparisonPhase::Original ?
                                   ready.result.original_key :
                                   ready.result.replacement_key;
        job->edited_seed = ready.phase == CaptureComparisonPhase::Replacement;
        ++s.revision;
        return true;
    }
    return false;
}
bool CaptureComparisonController::StillCurrent(
    const CaptureComparisonIdentity &identity,
    CaptureComparisonPhase phase) const
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->Current(identity, phase);
}
bool CaptureComparisonController::Complete(
    CaptureComparisonCompletion completion)
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    auto &s = *impl_;
    if (!s.Current(completion.identity, completion.phase) ||
        completion.outcome == CaptureComparisonOutcome::Pending ||
        uint32_t(completion.outcome) >
            uint32_t(CaptureComparisonOutcome::Cancelled)) {
        ++s.stale;
        ++s.revision;
        return false;
    }
    auto &row = s.rows[s.cursor];
    auto finish_failure = [&](CaptureComparisonOutcome outcome,
                              const std::string &reason) {
        if (s.Suffix())
            s.FailSuffix(&row, outcome, reason);
        else
            s.Finish(row, outcome, reason);
    };
    const auto &expected =
        completion.phase == CaptureComparisonPhase::Original ?
            row.result.original_key :
            row.result.replacement_key;
    if (completion.result != expected) {
        ++s.stale;
        ++s.revision;
        return false;
    }
    if (expected.profile_draw &&
        completion.outcome == CaptureComparisonOutcome::Completed &&
        (!ValidatePreviewDrawTiming(completion.draw_timing, expected) ||
         completion.draw_timing.status == PreviewDrawTimingStatus::Pending)) {
        ++s.stale;
        ++s.revision;
        return false;
    }
    if (!expected.profile_draw)
        completion.draw_timing = {};
    if (completion.outcome != CaptureComparisonOutcome::Completed) {
        if (expected.profile_draw) {
            PreviewDrawTiming timing;
            timing.result = expected;
            timing.backend = expected.compile.selection.backend;
            timing.provenance = PreviewDrawTimingProvenance::ReplayInstrumented;
            timing.status =
                completion.outcome == CaptureComparisonOutcome::Unsupported ?
                    PreviewDrawTimingStatus::Unsupported :
                    PreviewDrawTimingStatus::Failed;
            timing.message = "No measured draw interval: " +
                             completion.message.substr(0, 900);
            (completion.phase == CaptureComparisonPhase::Original ?
                 row.result.original_timing :
                 row.result.replacement_timing) = std::move(timing);
        }
        finish_failure(completion.outcome, completion.message);
        return true;
    }
    if (s.Suffix())
        (completion.phase == CaptureComparisonPhase::Original ?
             row.result.original_timing :
             row.result.replacement_timing) = completion.draw_timing;
    auto &image = completion.image;
    const uint64_t bytes = uint64_t(expected.width) * expected.height * 4;
    if (image.width != expected.width || image.height != expected.height ||
        image.rgba.size() != bytes) {
        finish_failure(
            CaptureComparisonOutcome::Incomplete,
            "Result readback dimensions or owned RGBA byte count mismatch");
        return true;
    }
    const uint64_t charge =
        image.rgba.capacity() + sizeof(CaptureComparisonImage);
    if (charge > s.settings.image_byte_budget ||
        s.account->images.load() > s.settings.image_byte_budget - charge) {
        finish_failure(CaptureComparisonOutcome::Incomplete,
                       "Comparison result image byte budget exceeded");
        return true;
    }
    SharedComparisonImage owned;
    try {
        owned = s.OwnImage(std::move(image), charge);
    } catch (const std::exception &exception) {
        finish_failure(CaptureComparisonOutcome::Incomplete, exception.what());
        return true;
    }
    if (s.Suffix()) {
        std::string error;
        try {
            if (!s.replay_draw ||
                !s.replay.Complete(s.replay_draw->identity, expected,
                                   { owned->width, owned->height, owned->rgba },
                                   &error)) {
                finish_failure(CaptureComparisonOutcome::Incomplete,
                               error.empty() ? s.replay.Reason() : error);
                return true;
            }
        } catch (const std::exception &exception) {
            finish_failure(CaptureComparisonOutcome::Incomplete,
                           exception.what());
            return true;
        }
        s.replay_draw.reset();
    }
    if (completion.phase == CaptureComparisonPhase::Original) {
        row.result.original = std::move(owned);
        row.result.original_timing = std::move(completion.draw_timing);
        row.phase = CaptureComparisonPhase::Replacement;
        row.claimed = false;
        if (s.Suffix())
            row.original.reset();
        ++s.revision;
    } else {
        row.result.replacement = std::move(owned);
        row.result.replacement_timing = std::move(completion.draw_timing);
        row.result.difference =
            Difference(*row.result.original, *row.result.replacement);
        s.Finish(row, CaptureComparisonOutcome::Completed,
                 s.Suffix() ?
                     "Reconstructed logical dependency branches "
                     "compared; raw GPU coverage gaps remain" :
                     "Frozen inputs compared; original VS/GS with partial "
                     "material/raster and destination limitations");
    }
    return true;
}
void CaptureComparisonController::Cancel()
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->Cancel();
}
void CaptureComparisonController::Invalidate(const PreviewSelection &selection)
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (!SamePreviewDisplayScope(selection, impl_->request.selection))
        impl_->Cancel();
}
CaptureComparisonSnapshot CaptureComparisonController::Snapshot() const
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    const auto &s = *impl_;
    CaptureComparisonSnapshot result;
    result.state = s.state;
    result.request_id = s.request_id;
    result.revision = s.revision.load();
    result.matched_events = s.matches;
    result.omitted_events = s.omitted;
    result.admitted_jobs = s.admitted;
    result.stale_completions = s.stale;
    result.packet_bytes = s.account->packets.load();
    result.image_bytes = s.account->images.load();
    if (s.replay_plan) {
        result.replay_steps = s.replay_plan->steps.size();
        result.replay_plan_bytes = s.replay_plan->retained_bytes;
        result.replay_value_bytes = s.replay.RetainedValueBytes();
    }
    result.message = s.message;
    for (const auto &row : s.rows) {
        result.results.push_back(row.result);
        ++result.outcomes[size_t(row.result.outcome)];
        if (s.request.profile_draw) {
            auto count = [&](PreviewDrawTiming &timing,
                             const PreviewResultKey &key,
                             PreviewDrawTimingDistribution *distribution) {
                if (timing.status == PreviewDrawTimingStatus::Disarmed) {
                    timing.result = key;
                    timing.backend = s.request.selection.backend;
                    timing.provenance =
                        PreviewDrawTimingProvenance::ReplayInstrumented;
                    timing.status = row.result.outcome ==
                                            CaptureComparisonOutcome::Pending ?
                                        PreviewDrawTimingStatus::Pending :
                                        PreviewDrawTimingStatus::Unsupported;
                    timing.message =
                        row.result.outcome ==
                                CaptureComparisonOutcome::Pending ?
                            "Awaiting this occurrence's instrumented draw" :
                            "No GPU draw interval for this phase: " +
                                row.result.message.substr(0, 900);
                }
                AccumulatePreviewDrawTiming(distribution, timing);
            };
            auto &copied = result.results.back();
            count(copied.original_timing, copied.original_key,
                  &result.original_timing);
            count(copied.replacement_timing, copied.replacement_key,
                  &result.replacement_timing);
        }
    }
    FinalizePreviewDrawTimingDistribution(&result.original_timing);
    FinalizePreviewDrawTimingDistribution(&result.replacement_timing);
    return result;
}

} // namespace xemu::shader_browser

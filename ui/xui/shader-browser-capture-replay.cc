// SPDX-License-Identifier: GPL-2.0-or-later
#include "shader-browser-capture-replay.hh"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <limits>
#include <mutex>
#include <set>
#include <tuple>

namespace xemu::shader_browser {
namespace {
std::atomic<uint64_t> next_replay_identity{ 1 };
uint64_t NewReplayIdentity()
{
    uint64_t value = next_replay_identity.load(std::memory_order_relaxed);
    while (value && value < UINT64_MAX) {
        if (next_replay_identity.compare_exchange_weak(
                value, value + 1, std::memory_order_relaxed))
            return value;
    }
    return 0;
}
bool Fail(std::string *error, const char *message)
{
    if (error)
        *error = message;
    return false;
}
bool Image(const XemuShaderCaptureReplayBinding &binding)
{
    return binding.image.format != XEMU_SHADER_CAPTURE_REPLAY_FORMAT_UNKNOWN;
}
bool SupportedImage(const XemuShaderCaptureReplayImage &image)
{
    if (!PreviewExtentWithinLimits(image.width, image.height, true) ||
        image.mip_level || image.layer || image.mip_levels != 1 ||
        image.layers != 1 || image.samples != 1 ||
        (image.format != XEMU_SHADER_CAPTURE_REPLAY_RGBA8_UNORM &&
         image.format != XEMU_SHADER_CAPTURE_REPLAY_BGRA8_UNORM))
        return false;
    for (uint32_t i = 0; i < 4; ++i)
        if (image.sample_swizzle[i] != i ||
            image.storage_to_rgba[i] !=
                (image.format == XEMU_SHADER_CAPTURE_REPLAY_BGRA8_UNORM &&
                         (i == 0 || i == 2) ?
                     2 - i :
                     i))
            return false;
    return image.coordinate_origin <= XEMU_SHADER_CAPTURE_REPLAY_BOTTOM_UP;
}
uint64_t ValueBytes(const CaptureReplayBinding &binding)
{
    if (Image(binding.description))
        return uint64_t(binding.description.image.width) *
               binding.description.image.height * 4;
    uint64_t bytes = 0;
    for (const auto &version : binding.versions)
        bytes += version.range.size;
    return bytes;
}
CaptureReplayVersionKey Key(const CaptureResourceVersion &version)
{
    return { version.domain_id, version.allocation_id, version.reset_generation,
             version.version, version.range };
}
bool SameVersion(const CaptureReplayVersionKey &a,
                 const CaptureReplayVersionKey &b)
{
    return std::tie(a.domain, a.allocation, a.reset, a.version) ==
           std::tie(b.domain, b.allocation, b.reset, b.version);
}
template <typename Map> auto Covering(Map &values, CaptureReplayVersionKey key)
{
    auto first = key;
    first.range = {};
    for (auto it = values.lower_bound(first);
         it != values.end() && SameVersion(it->first, key); ++it)
        if (it->first.range.offset <= key.range.offset &&
            key.range.size <= it->first.range.size &&
            key.range.offset - it->first.range.offset <=
                it->first.range.size - key.range.size)
            return it;
    return values.end();
}
bool SameImage(const XemuShaderCaptureReplayImage &a,
               const XemuShaderCaptureReplayImage &b)
{
    return a.width == b.width && a.height == b.height &&
           a.mip_level == b.mip_level && a.layer == b.layer &&
           a.mip_levels == b.mip_levels && a.layers == b.layers &&
           a.samples == b.samples && a.format == b.format &&
           std::equal(a.storage_to_rgba, a.storage_to_rgba + 4,
                      b.storage_to_rgba);
}
const CaptureReplayBinding *Find(const CaptureReplayStep &step, uint32_t role,
                                 bool write)
{
    const CaptureReplayBinding *found = nullptr;
    for (const auto &binding : step.bindings)
        if (binding.description.role == role &&
            bool(binding.description.resource.write) == write) {
            if (found)
                return nullptr;
            found = &binding;
        }
    return found;
}
bool WholeCopy(const CaptureReplayStep &step)
{
    const auto *target =
        Find(step, XEMU_SHADER_CAPTURE_REPLAY_COPY_DESTINATION, true);
    const auto &description = *step.description;
    return target && Image(target->description) && !description.destination_x &&
           !description.destination_y &&
           description.width == target->description.image.width &&
           description.height == target->description.image.height;
}
bool ClearColor(const CaptureReplayDescription &description,
                std::array<uint8_t, 4> *color)
{
    static_assert(sizeof(float) == sizeof(uint32_t) &&
                  std::numeric_limits<float>::is_iec559);
    if (description.clear_color_mask != 0xF)
        return false;
    for (size_t i = 0; i < color->size(); ++i) {
        float value;
        std::memcpy(&value, description.clear_color_bits + i, sizeof(value));
        if (!std::isfinite(value))
            return false;
        const double scaled = std::clamp(double(value), 0.0, 1.0) * 255.0;
        const auto lower = uint32_t(std::floor(scaled));
        const double fraction = scaled - lower;
        // Canonical UNORM reconstruction uses nearest, ties to even. Vulkan
        // permits adjacent integer quantization; this is not driver-bit proof.
        (*color)[i] =
            uint8_t(lower + (fraction > .5 || (fraction == .5 && (lower & 1))));
    }
    return true;
}
bool WholeClear(const CaptureReplayStep &step)
{
    const auto *target = Find(step, XEMU_SHADER_CAPTURE_REPLAY_COLOR, true);
    const auto &description = *step.description;
    return target && Image(target->description) &&
           description.clear_color_mask == 0xF && !description.destination_x &&
           !description.destination_y &&
           description.width == target->description.image.width &&
           description.height == target->description.image.height;
}
bool UnusedDestination(const CaptureReplayStep &step,
                       const CaptureReplayBinding &binding)
{
    return !binding.description.resource.write &&
           ((step.description->kind ==
                 uint32_t(CaptureCommandKind::ImageCopy) &&
             binding.description.role ==
                 XEMU_SHADER_CAPTURE_REPLAY_COPY_DESTINATION &&
             WholeCopy(step)) ||
            (step.description->kind == uint32_t(CaptureCommandKind::Clear) &&
             binding.description.role == XEMU_SHADER_CAPTURE_REPLAY_COLOR &&
             WholeClear(step)));
}
SharedCaptureBlock
OwnedCheckpoint(const CaptureOccurrence &event,
                const XemuShaderCaptureReplayBinding &binding)
{
    if (binding.role == XEMU_SHADER_CAPTURE_REPLAY_COLOR)
        return event.inputs.before.width == binding.image.width &&
                       event.inputs.before.height == binding.image.height ?
                   event.inputs.before.rgba :
                   nullptr;
    if (binding.role == XEMU_SHADER_CAPTURE_REPLAY_TEXTURE &&
        binding.slot < 4) {
        for (const auto &image : event.inputs.textures[binding.slot].images)
            if (image.mip_level == binding.image.mip_level &&
                image.face == binding.image.layer &&
                image.image.width == binding.image.width &&
                image.image.height == binding.image.height)
                return image.image.rgba;
    }
    for (const auto &blob : event.inputs.blobs)
        if (blob.name == binding.blob_name)
            return blob.data;
    return {};
}
bool KnownIdentity(const CaptureResourceVersion &version, bool image)
{
    return version.domain_id && version.allocation_id && version.version &&
           version.range.size &&
           (version.provenance == CaptureResourceProvenance::Known ||
            (image &&
             version.provenance == CaptureResourceProvenance::UnknownCoverage));
}
} // namespace

bool CaptureReplayVersionKey::operator<(
    const CaptureReplayVersionKey &other) const
{
    return std::tie(domain, allocation, reset, version, range.offset,
                    range.size) <
           std::tie(other.domain, other.allocation, other.reset, other.version,
                    other.range.offset, other.range.size);
}
bool CaptureReplayIdentity::operator==(const CaptureReplayIdentity &other) const
{
    return run == other.run && token == other.token &&
           event_id == other.event_id && sequence == other.sequence &&
           branch == other.branch;
}

CaptureReplayPlan
BuildCaptureReplayPlan(const CaptureSessionSnapshot &snapshot,
                       const CaptureReplayDescriptions &descriptions,
                       const std::vector<uint64_t> &seeds,
                       const CaptureReplayLimits &limits)
{
    CaptureReplayPlan plan;
    auto reject = [&](const char *reason, bool budget = false) {
        plan.status = budget ? CaptureReplayStatus::BudgetExceeded :
                               CaptureReplayStatus::Unsupported;
        plan.reason = reason;
        plan.steps.clear();
        return plan;
    };
    if (!snapshot.execution_order_complete || seeds.empty() ||
        seeds.size() > limits.maximum_steps ||
        limits.plan_byte_budget > UINT64_C(1024) * 1024 * 1024 ||
        limits.value_byte_budget > UINT64_C(1024) * 1024 * 1024 ||
        !limits.maximum_steps || !limits.maximum_bindings ||
        !limits.plan_byte_budget)
        return reject(
            "Verified execution order and bounded seeds are required");
    try {
        auto graph =
            BuildCaptureSessionResourceGraph(snapshot, limits.plan_byte_budget);
        auto resource_limits = CaptureSessionResourceLimits(snapshot.settings);
        resource_limits.max_job_events = limits.maximum_steps;
        resource_limits.byte_budget = limits.plan_byte_budget;
        auto closure =
            PlanCaptureResourceReplacement(graph, seeds, resource_limits);
        if (graph.invalid_input || closure.invalid_input)
            return reject("Resource closure identity is invalid");
        if (graph.budget_exceeded || closure.budget_exceeded ||
            closure.events.size() > limits.maximum_steps)
            return reject("Resource closure exceeds its bound", true);
        std::map<uint64_t, std::shared_ptr<const CaptureOccurrence>>
            occurrences;
        for (const auto &event : snapshot.events)
            occurrences.emplace(event->event_id, event);
        std::set<uint64_t> affected(closure.events.begin(),
                                    closure.events.end());
        std::set<std::pair<const CaptureImmutableBlock *, uint64_t>> blocks;
        std::map<CaptureReplayVersionKey, XemuShaderCaptureReplayImage>
            produced;
        std::map<CaptureReplayVersionKey,
                 std::pair<XemuShaderCaptureReplayImage, SharedCaptureBlock>>
            checkpoint_values;
        size_t binding_count = 0;
        uint64_t bytes = sizeof(plan);
        for (uint64_t id : closure.events) {
            auto event = occurrences.find(id);
            auto description = descriptions.find(id);
            if (event == occurrences.end() ||
                description == descriptions.end() || !description->second ||
                !event->second->resource_evidence)
                return reject(
                    "A retained dependency has no logical command description");
            const auto &occurrence = *event->second;
            const bool proven_host =
                occurrence.command_recorded &&
                occurrence.command_phase ==
                    CaptureCommandPhase::HostPreparation &&
                occurrence.command.kind == CaptureCommandKind::CpuUpload &&
                occurrence.submission == CaptureBatchOutcome::None &&
                occurrence.completion == CaptureBatchCompletion::None;
            if (occurrence.pending || !occurrence.finished ||
                !occurrence.inputs.complete || !occurrence.resource_finalized ||
                (occurrence.limitations &
                 (CaptureUnsupported | CaptureMalformed |
                  CaptureReadbackFailed | CaptureInvalidated)) ||
                (occurrence.type == CaptureEventType::Draw &&
                 !occurrence.emitted) ||
                (occurrence.batch_id && !proven_host &&
                 (!occurrence.command_recorded ||
                  occurrence.submission != CaptureBatchOutcome::Submitted ||
                  occurrence.completion != CaptureBatchCompletion::Completed)))
                return reject(
                    "Dependency command or owned input completion is unproven");
            if (!ValidateCaptureReplayDescription(*description->second,
                                                  &plan.reason))
                return reject("Dependency description is malformed");
            CaptureReplayStep step;
            step.occurrence = event->second;
            step.description = std::make_shared<const CaptureReplayDescription>(
                *description->second);
            step.edited_seed =
                std::find(seeds.begin(), seeds.end(), id) != seeds.end();
            const auto kind = CaptureCommandKind(step.description->kind);
            if (kind == CaptureCommandKind::Clear &&
                (occurrence.type != CaptureEventType::Clear ||
                 !occurrence.emitted || !occurrence.batch_id ||
                 !occurrence.command_ordinal || !occurrence.queue_ordinal ||
                 !occurrence.command_recorded ||
                 occurrence.command_phase != CaptureCommandPhase::Main ||
                 occurrence.command.kind != kind ||
                 occurrence.submission != CaptureBatchOutcome::Submitted ||
                 occurrence.completion != CaptureBatchCompletion::Completed ||
                 occurrence.backend_result || occurrence.completion_result))
                return reject(
                    "GPU color clear emission and completion are unproven");
            if ((kind == CaptureCommandKind::Draw &&
                 occurrence.type != CaptureEventType::Draw) ||
                (kind == CaptureCommandKind::CpuUpload &&
                 occurrence.type != CaptureEventType::Upload) ||
                ((kind == CaptureCommandKind::ImageCopy ||
                  kind == CaptureCommandKind::BufferCopy) &&
                 occurrence.type != CaptureEventType::Copy) ||
                (occurrence.batch_id && occurrence.command.kind != kind))
                return reject(
                    "Logical operation differs from its recorded occurrence");
            if (kind != CaptureCommandKind::Draw &&
                kind != CaptureCommandKind::ImageCopy &&
                kind != CaptureCommandKind::Clear &&
                kind != CaptureCommandKind::CpuUpload &&
                kind != CaptureCommandKind::BufferCopy)
                return reject("Dependency operation has no supported executor");
            std::set<std::pair<size_t, bool>> described;
            for (const auto &binding : step.description->bindings) {
                if (++binding_count > limits.maximum_bindings)
                    return reject("Dependency binding count exceeds its bound",
                                  true);
                size_t index = 0, ordinal = 0;
                for (; index < occurrence.resource_evidence->accesses.size();
                     ++index) {
                    const auto &access =
                        occurrence.resource_evidence->accesses[index];
                    const auto &versions =
                        binding.resource.write ? access.writes : access.reads;
                    if (access.kind == binding.resource.kind &&
                        access.slot == binding.resource.slot &&
                        !versions.empty() &&
                        ordinal++ == binding.resource.ordinal)
                        break;
                }
                if (index == occurrence.resource_evidence->accesses.size() ||
                    !described.emplace(index, bool(binding.resource.write))
                         .second)
                    return reject(
                        "Dependency selector is missing or duplicated");
                const auto &access =
                    occurrence.resource_evidence->accesses[index];
                CaptureReplayBinding resolved;
                resolved.description = binding;
                resolved.access_index = index;
                resolved.versions =
                    binding.resource.write ? access.writes : access.reads;
                if ((binding.role == XEMU_SHADER_CAPTURE_REPLAY_COLOR &&
                     binding.resource.kind !=
                         XEMU_SHADER_CAPTURE_RESOURCE_COLOR) ||
                    (binding.role == XEMU_SHADER_CAPTURE_REPLAY_TEXTURE &&
                     binding.resource.kind !=
                         XEMU_SHADER_CAPTURE_RESOURCE_TEXTURE) ||
                    ((binding.role ==
                          XEMU_SHADER_CAPTURE_REPLAY_VERTEX_STREAM ||
                      binding.role == XEMU_SHADER_CAPTURE_REPLAY_INDICES) &&
                     binding.resource.kind !=
                         XEMU_SHADER_CAPTURE_RESOURCE_BUFFER))
                    return reject("Logical binding role differs from its "
                                  "actual resource kind");
                if ((binding.role == XEMU_SHADER_CAPTURE_REPLAY_COLOR ||
                     binding.role == XEMU_SHADER_CAPTURE_REPLAY_TEXTURE) &&
                    !Image(binding))
                    return reject("Logical image binding has no supported "
                                  "format description");
                if (!Image(binding) && resolved.versions.size() != 1)
                    return reject("Fragmented buffer bindings require an "
                                  "unsupported reconstruction");
                if (Image(binding) && (!SupportedImage(binding.image) ||
                                       resolved.versions.size() != 1))
                    return reject("Only fully described single-sample base "
                                  "RGBA8 images are supported");
                step.bindings.push_back(std::move(resolved));
            }
            for (size_t index = 0;
                 index < occurrence.resource_evidence->accesses.size();
                 ++index) {
                const auto &access =
                    occurrence.resource_evidence->accesses[index];
                if ((!access.reads.empty() &&
                     !described.count({ index, false })) ||
                    (!access.writes.empty() &&
                     !described.count({ index, true })))
                    return reject(
                        "A dependency access lacks logical binding proof");
            }
            if (kind == CaptureCommandKind::Draw) {
                const auto *before =
                    Find(step, XEMU_SHADER_CAPTURE_REPLAY_COLOR, false);
                const auto *after =
                    Find(step, XEMU_SHADER_CAPTURE_REPLAY_COLOR, true);
                if (!before || !after || !Image(before->description) ||
                    !SameImage(before->description.image,
                               after->description.image))
                    return reject("Draw color destination and preserved extent "
                                  "are unproven");
            } else if (kind == CaptureCommandKind::Clear) {
                const auto *before =
                    Find(step, XEMU_SHADER_CAPTURE_REPLAY_COLOR, false);
                const auto *after =
                    Find(step, XEMU_SHADER_CAPTURE_REPLAY_COLOR, true);
                const auto &clear = *step.description;
                std::array<uint8_t, 4> color;
                if (!before || !after || step.bindings.size() != 2 ||
                    !Image(before->description) || !Image(after->description) ||
                    !SameImage(before->description.image,
                               after->description.image) ||
                    before->description.slot || after->description.slot ||
                    before->description.resource.slot ||
                    after->description.resource.slot ||
                    before->description.resource.ordinal ||
                    after->description.resource.ordinal ||
                    before->versions[0].domain_id !=
                        after->versions[0].domain_id ||
                    before->versions[0].allocation_id !=
                        after->versions[0].allocation_id ||
                    before->versions[0].reset_generation !=
                        after->versions[0].reset_generation ||
                    before->versions[0].range.offset !=
                        after->versions[0].range.offset ||
                    before->versions[0].range.size !=
                        after->versions[0].range.size ||
                    !clear.width || !clear.height ||
                    clear.destination_x > after->description.image.width ||
                    clear.width >
                        after->description.image.width - clear.destination_x ||
                    clear.destination_y > after->description.image.height ||
                    clear.height >
                        after->description.image.height - clear.destination_y ||
                    clear.source_x || clear.source_y || clear.source_offset ||
                    clear.destination_offset || clear.bytes ||
                    !ClearColor(clear, &color))
                    return reject("Clear color, full channel mask, region or "
                                  "preserved target is unproven");
            } else if (kind == CaptureCommandKind::ImageCopy) {
                const auto *source =
                    Find(step, XEMU_SHADER_CAPTURE_REPLAY_COPY_SOURCE, false);
                const auto *target = Find(
                    step, XEMU_SHADER_CAPTURE_REPLAY_COPY_DESTINATION, true);
                const auto &copy = *step.description;
                if (!source || !target || !Image(source->description) ||
                    !Image(target->description) ||
                    source->description.image.format !=
                        target->description.image.format ||
                    !copy.width || !copy.height ||
                    copy.source_x > source->description.image.width ||
                    copy.width >
                        source->description.image.width - copy.source_x ||
                    copy.source_y > source->description.image.height ||
                    copy.height >
                        source->description.image.height - copy.source_y ||
                    copy.destination_x > target->description.image.width ||
                    copy.width >
                        target->description.image.width - copy.destination_x ||
                    copy.destination_y > target->description.image.height ||
                    copy.height >
                        target->description.image.height - copy.destination_y ||
                    (!WholeCopy(step) &&
                     !Find(step, XEMU_SHADER_CAPTURE_REPLAY_COPY_DESTINATION,
                           false)))
                    return reject("Image copy region, format or preserved "
                                  "target is unproven");
                if (source->versions[0].allocation_id ==
                        target->versions[0].allocation_id &&
                    copy.source_x < copy.destination_x + copy.width &&
                    copy.destination_x < copy.source_x + copy.width &&
                    copy.source_y < copy.destination_y + copy.height &&
                    copy.destination_y < copy.source_y + copy.height)
                    return reject("Overlapping image self-copy has no defined "
                                  "native proof");
            } else {
                const auto *target = Find(
                    step, XEMU_SHADER_CAPTURE_REPLAY_COPY_DESTINATION, true);
                const auto *source =
                    Find(step, XEMU_SHADER_CAPTURE_REPLAY_COPY_SOURCE, false);
                const auto &copy = *step.description;
                if (!target || Image(target->description) || !copy.bytes ||
                    target->versions[0].range.offset !=
                        copy.destination_offset ||
                    target->versions[0].range.size != copy.bytes ||
                    (kind == CaptureCommandKind::BufferCopy &&
                     (!source || Image(source->description) ||
                      source->versions[0].range.offset != copy.source_offset ||
                      source->versions[0].range.size != copy.bytes)) ||
                    (kind == CaptureCommandKind::CpuUpload && source))
                    return reject(
                        "CPU upload or buffer-copy owned range is unproven");
                if (source &&
                    source->versions[0].allocation_id ==
                        target->versions[0].allocation_id &&
                    copy.source_offset < copy.destination_offset + copy.bytes &&
                    copy.destination_offset < copy.source_offset + copy.bytes)
                    return reject("Overlapping buffer self-copy has no defined "
                                  "native proof");
            }
            for (auto &binding : step.bindings) {
                const auto role = binding.description.role;
                const bool write = binding.description.resource.write;
                if ((kind == CaptureCommandKind::Draw &&
                     ((write && role != XEMU_SHADER_CAPTURE_REPLAY_COLOR) ||
                      (!write && role != XEMU_SHADER_CAPTURE_REPLAY_COLOR &&
                       role != XEMU_SHADER_CAPTURE_REPLAY_TEXTURE &&
                       role != XEMU_SHADER_CAPTURE_REPLAY_VERTEX_STREAM &&
                       role != XEMU_SHADER_CAPTURE_REPLAY_INDICES))) ||
                    (kind == CaptureCommandKind::Clear &&
                     role != XEMU_SHADER_CAPTURE_REPLAY_COLOR) ||
                    (kind == CaptureCommandKind::CpuUpload &&
                     (!write ||
                      role != XEMU_SHADER_CAPTURE_REPLAY_COPY_DESTINATION)) ||
                    ((kind == CaptureCommandKind::ImageCopy ||
                      kind == CaptureCommandKind::BufferCopy) &&
                     (role != XEMU_SHADER_CAPTURE_REPLAY_COPY_SOURCE &&
                      role != XEMU_SHADER_CAPTURE_REPLAY_COPY_DESTINATION)) ||
                    (role == XEMU_SHADER_CAPTURE_REPLAY_COPY_SOURCE && write) ||
                    (kind == CaptureCommandKind::BufferCopy &&
                     (role == XEMU_SHADER_CAPTURE_REPLAY_COPY_DESTINATION) !=
                         write))
                    return reject("Dependency operation has an unexecutable "
                                  "read/write role");
                if (UnusedDestination(step, binding))
                    continue;
                for (const auto &version : binding.versions) {
                    const bool external =
                        !binding.description.resource.write &&
                        !affected.count(version.producer_event);
                    const bool initial_checkpoint =
                        external && binding.description.checkpoint &&
                        version.domain_id && version.allocation_id &&
                        version.range.size &&
                        version.provenance ==
                            CaptureResourceProvenance::MissingProducer;
                    if (!KnownIdentity(version, Image(binding.description)) &&
                        !initial_checkpoint)
                        return reject("Dependency contains missing provenance "
                                      "or an ambiguous alias");
                    if (!binding.description.resource.write && !external) {
                        auto input = Covering(produced, Key(version));
                        if (input == produced.end() ||
                            (Image(binding.description) &&
                             (!SameImage(input->second,
                                         binding.description.image) ||
                              input->first.range.offset !=
                                  version.range.offset ||
                              input->first.range.size != version.range.size)))
                            return reject("A descendant version has no earlier "
                                          "matching logical output");
                    }
                    if (external) {
                        if (!binding.description.checkpoint)
                            return reject("An external version has no explicit "
                                          "owned checkpoint");
                        binding.checkpoint =
                            OwnedCheckpoint(occurrence, binding.description);
                        if (!binding.checkpoint ||
                            binding.checkpoint->bytes.size() !=
                                ValueBytes(binding) ||
                            (!Image(binding.description) &&
                             binding.checkpoint->id != version.snapshot_block))
                            return reject("External checkpoint bytes or extent "
                                          "are unavailable");
                        const auto previous =
                            checkpoint_values.find(Key(version));
                        if (previous != checkpoint_values.end() &&
                            (previous->second.second->bytes !=
                                 binding.checkpoint->bytes ||
                             (Image(binding.description) &&
                              !SameImage(previous->second.first,
                                         binding.description.image))))
                            return reject("Checkpoints for one immutable "
                                          "version contradict each other");
                        checkpoint_values.emplace(
                            Key(version),
                            std::make_pair(binding.description.image,
                                           binding.checkpoint));
                    }
                    if (kind == CaptureCommandKind::CpuUpload &&
                        binding.description.resource.write) {
                        binding.checkpoint =
                            OwnedCheckpoint(occurrence, binding.description);
                        if (Image(binding.description) || !binding.checkpoint ||
                            binding.checkpoint->id != version.snapshot_block ||
                            binding.checkpoint->bytes.size() !=
                                ValueBytes(binding))
                            return reject("CPU upload requires its exact owned "
                                          "byte payload");
                    }
                }
                bytes += sizeof(binding) + binding.versions.capacity() *
                                               sizeof(CaptureResourceVersion);
            }
            bytes += (step.bindings.capacity() - step.bindings.size()) *
                     sizeof(CaptureReplayBinding);
            for (const auto &binding : step.bindings)
                if (binding.description.resource.write)
                    for (const auto &version : binding.versions)
                        if (!produced
                                 .emplace(Key(version),
                                          binding.description.image)
                                 .second)
                            return reject(
                                "Logical output version is duplicated");
            const auto charge_block = [&](const SharedCaptureBlock &block) {
                if (block && blocks.emplace(block.get(), block->id).second)
                    bytes += sizeof(CaptureImmutableBlock) + 128 +
                             block->bytes.capacity();
            };
            charge_block(occurrence.geometry.positions);
            charge_block(occurrence.geometry.indices);
            charge_block(occurrence.inputs.before.rgba);
            charge_block(occurrence.inputs.after.rgba);
            for (const auto &block : occurrence.inputs.sources)
                charge_block(block);
            for (const auto &blob : occurrence.inputs.blobs)
                charge_block(blob.data);
            for (const auto &uniform : occurrence.inputs.uniforms)
                charge_block(uniform.data);
            for (const auto &texture : occurrence.inputs.textures)
                for (const auto &image : texture.images)
                    charge_block(image.image.rgba);
            bytes +=
                sizeof(step) + CaptureOccurrenceDescriptorBytes(occurrence);
            if (step.description != occurrence.replay_description)
                bytes += sizeof(CaptureReplayDescription) + 128 +
                         step.description->bindings.capacity() *
                             sizeof(XemuShaderCaptureReplayBinding);
            if (bytes > limits.plan_byte_budget)
                return reject("Retained replay plan exceeds its byte bound",
                              true);
            plan.steps.push_back(std::move(step));
        }
        bytes += (plan.steps.capacity() - plan.steps.size()) *
                 sizeof(CaptureReplayStep);
        if (bytes > limits.plan_byte_budget)
            return reject(
                "Retained replay plan capacity exceeds its byte bound", true);
        plan.retained_bytes = bytes;
        plan.logical_closure_complete = true;
        plan.status = CaptureReplayStatus::Ready;
        plan.reason = "Reconstructed canonical RGBA8 dependency sequence; raw "
                      "allocation gaps retained";
    } catch (const std::bad_alloc &) {
        return reject("Replay planning allocation failed", true);
    }
    return plan;
}
CaptureReplayPlan BuildCaptureReplayPlan(const CaptureSessionSnapshot &snapshot,
                                         const std::vector<uint64_t> &seeds,
                                         const CaptureReplayLimits &limits)
{
    try {
        CaptureReplayDescriptions descriptions;
        for (const auto &event : snapshot.events)
            if (event && event->replay_description)
                descriptions.emplace(event->event_id,
                                     event->replay_description);
        return BuildCaptureReplayPlan(snapshot, descriptions, seeds, limits);
    } catch (const std::bad_alloc &) {
        CaptureReplayPlan plan;
        plan.status = CaptureReplayStatus::BudgetExceeded;
        plan.reason = "Replay description index allocation failed";
        return plan;
    }
}

bool RebindCaptureReplayPacket(const CaptureReplayDraw &draw,
                               const PreviewPacket &base, PreviewPacket *out,
                               std::string *error)
{
    if (!out || !base.captured_pipeline || !base.captured_material)
        return Fail(
            error,
            "Dependency draw requires owned original pipeline and material");
    auto packet = base;
    auto pipeline =
        std::make_shared<PreviewCapturedPipeline>(*base.captured_pipeline);
    auto material =
        std::make_shared<PreviewCapturedMaterial>(*base.captured_material);
    if (pipeline->raster.available != 1023 || pipeline->raster.depth_test ||
        pipeline->raster.stencil_test)
        return Fail(error, "Dependency replay requires complete raster proof "
                           "without depth/stencil");
    bool destination = false;
    std::set<uint32_t> textures, attributes;
    bool indices = pipeline->indices.empty();
    for (const auto &input : draw.inputs) {
        const auto &binding = input.description;
        if (!input.bytes)
            return Fail(error, "Dependency read version has no owned value");
        switch (binding.role) {
        case XEMU_SHADER_CAPTURE_REPLAY_COLOR:
            pipeline->color_before = { binding.image.width,
                                       binding.image.height, *input.bytes };
            destination = true;
            break;
        case XEMU_SHADER_CAPTURE_REPLAY_TEXTURE: {
            if (binding.slot >= material->textures.size() ||
                !textures.insert(binding.slot).second)
                return Fail(error,
                            "Dependency texture slot is invalid or duplicated");
            auto &texture = material->textures[binding.slot];
            if (!texture.described || !texture.metadata.bound ||
                texture.metadata.width != binding.image.width ||
                texture.metadata.height != binding.image.height ||
                texture.metadata.mip_levels != 1 ||
                texture.metadata.face_count != 1 || texture.metadata.depth != 1)
                return Fail(error, "Dependency texture layout differs from its "
                                   "captured binding");
            texture.images = { { 0,
                                 0,
                                 { binding.image.width, binding.image.height,
                                   *input.bytes } } };
            break;
        }
        case XEMU_SHADER_CAPTURE_REPLAY_VERTEX_STREAM:
            if (binding.slot >= pipeline->attributes.size() ||
                !attributes.insert(binding.slot).second ||
                pipeline->attributes[binding.slot].stream.name !=
                    binding.blob_name ||
                pipeline->attributes[binding.slot].stream.bytes.size() !=
                    input.bytes->size())
                return Fail(error,
                            "Dependency vertex stream layout is unavailable");
            pipeline->attributes[binding.slot].stream.bytes = *input.bytes;
            break;
        case XEMU_SHADER_CAPTURE_REPLAY_INDICES:
            if (indices || input.bytes->size() !=
                               pipeline->indices.size() * sizeof(uint32_t))
                return Fail(
                    error,
                    "Dependency index range differs from the captured draw");
            std::memcpy(pipeline->indices.data(), input.bytes->data(),
                        input.bytes->size());
            indices = true;
            break;
        default:
            return Fail(error, "Unsupported read role in dependency draw");
        }
    }
    if (!destination || !indices)
        return Fail(error,
                    "Dependency destination or index binding is missing");
    for (size_t slot = 0; slot < material->textures.size(); ++slot)
        if (material->textures[slot].metadata.bound && !textures.count(slot))
            return Fail(error, "A bound texture has no exact captured version");
    for (size_t slot = 0; slot < pipeline->attributes.size(); ++slot)
        if (pipeline->attributes[slot].enabled && !attributes.count(slot))
            return Fail(error, "A vertex stream has no exact captured version");
    if (material->limitations &
        (PreviewMaterialUnavailable | PreviewMaterialUnsupportedTexture |
         PreviewMaterialUnappliedUniform | PreviewMaterialBudgetLimited |
         PreviewMaterialApproximateSampler))
        return Fail(error,
                    "Captured material contains unsupported approximations");
    packet.captured_pipeline = std::move(pipeline);
    packet.captured_material = std::move(material);
    packet.pipeline_digest =
        ComputePreviewCapturedPipelineDigest(*packet.captured_pipeline);
    packet.pipeline_layout_digest =
        ComputePreviewCapturedPipelineDigest(*packet.captured_pipeline, true);
    packet.material_digest =
        ComputePreviewCapturedMaterialDigest(*packet.captured_material);
    packet.input_revision = draw.identity.token;
    packet.captured_mesh = {};
    packet.mesh_digest = {};
    // Reconstructed logical operations do not strengthen raw replay fidelity.
    packet.replay_class = PreviewReplayClass::Approximate;
    if (!ValidatePreviewPacket(packet, error))
        return false;
    *out = std::move(packet);
    return true;
}

struct CaptureReplaySequence::Impl {
    static constexpr uint64_t kNodeCharge = 512;
    struct Value {
        std::shared_ptr<const std::vector<uint8_t>> bytes;
        XemuShaderCaptureReplayImage image{};
    };
    struct Account {
        std::atomic<uint64_t> bytes{ 0 };
    };
    mutable std::mutex mutex;
    std::shared_ptr<const CaptureReplayPlan> plan;
    CaptureReplayLimits limits;
    std::shared_ptr<Account> account = std::make_shared<Account>();
    std::map<CaptureReplayVersionKey, Value> values[2];
    CaptureReplayStatus status = CaptureReplayStatus::Cancelled;
    uint64_t run = 0, token = 0;
    size_t cursor = 0;
    uint32_t branch = 0;
    bool claimed = false, expected = false;
    CaptureReplayIdentity identity;
    PreviewResultKey result;
    std::string reason;
    bool Charge(uint64_t bytes)
    {
        if (bytes > limits.value_byte_budget ||
            account->bytes.load() > limits.value_byte_budget - bytes)
            return false;
        account->bytes.fetch_add(bytes);
        return true;
    }
    void ClearValues()
    {
        const uint64_t nodes = values[0].size() + values[1].size();
        values[0].clear();
        values[1].clear();
        account->bytes.fetch_sub(nodes * kNodeCharge);
    }
    void Stop(CaptureReplayStatus state, const char *message)
    {
        status = state;
        claimed = expected = false;
        reason = message;
    }
    std::shared_ptr<const std::vector<uint8_t>> Own(std::vector<uint8_t> bytes)
    {
        const uint64_t charge = sizeof(bytes) + bytes.capacity() + 128;
        if (charge > limits.value_byte_budget ||
            account->bytes.load() > limits.value_byte_budget - charge)
            return {};
        auto owned = std::make_unique<std::vector<uint8_t>>(std::move(bytes));
        auto accounting = account;
        account->bytes.fetch_add(charge);
        return { owned.release(),
                 [accounting, charge](const std::vector<uint8_t> *p) {
                     delete p;
                     accounting->bytes.fetch_sub(charge);
                 } };
    }
    bool Current(const CaptureReplayIdentity &candidate) const
    {
        return status == CaptureReplayStatus::Running && claimed &&
               identity == candidate;
    }
    std::shared_ptr<const std::vector<uint8_t>>
    Read(const CaptureReplayBinding &binding)
    {
        if (binding.versions.size() != 1)
            return {};
        const auto key = Key(binding.versions[0]);
        auto found = Covering(values[branch], key);
        if (found == values[branch].end() ||
            (Image(binding.description) &&
             (!SameImage(found->second.image, binding.description.image) ||
              found->first.range.offset != key.range.offset ||
              found->first.range.size != key.range.size)))
            return {};
        if (!Image(binding.description) &&
            (found->first.range.offset != key.range.offset ||
             found->first.range.size != key.range.size)) {
            const uint64_t offset =
                key.range.offset - found->first.range.offset;
            if (offset > found->second.bytes->size() ||
                key.range.size > found->second.bytes->size() - offset)
                return {};
            if (key.range.size + 128 + sizeof(std::vector<uint8_t>) >
                    limits.value_byte_budget ||
                account->bytes.load() > limits.value_byte_budget -
                                            key.range.size - 128 -
                                            sizeof(std::vector<uint8_t>)) {
                Stop(CaptureReplayStatus::BudgetExceeded,
                     "Replay subrange exceeds its byte bound");
                return {};
            }
            auto slice = Own(std::vector<uint8_t>(
                found->second.bytes->begin() + offset,
                found->second.bytes->begin() + offset + key.range.size));
            if (!slice)
                Stop(CaptureReplayStatus::BudgetExceeded,
                     "Replay subrange exceeds its byte bound");
            return slice;
        }
        return found->second.bytes;
    }
    bool Publish(const CaptureReplayBinding &binding,
                 std::shared_ptr<const std::vector<uint8_t>> bytes)
    {
        if (binding.versions.size() != 1 || !bytes)
            return false;
        const auto key = Key(binding.versions[0]);
        if (values[branch].count(key) || !Charge(kNodeCharge))
            return false;
        try {
            values[branch].emplace(
                key, Value{ std::move(bytes), binding.description.image });
            return true;
        } catch (...) {
            account->bytes.fetch_sub(kNodeCharge);
            throw;
        }
    }
    bool Clear(const CaptureReplayStep &step)
    {
        const auto *target = Find(step, XEMU_SHADER_CAPTURE_REPLAY_COLOR, true);
        std::array<uint8_t, 4> color;
        if (!target || !ClearColor(*step.description, &color))
            return false;
        const uint64_t needed = ValueBytes(*target) +
                                sizeof(std::vector<uint8_t>) + 128 +
                                kNodeCharge;
        if (needed > limits.value_byte_budget ||
            account->bytes.load() > limits.value_byte_budget - needed) {
            Stop(CaptureReplayStatus::BudgetExceeded,
                 "Replay clear exceeds its remaining byte bound");
            return false;
        }
        std::vector<uint8_t> output(ValueBytes(*target));
        if (!WholeClear(step)) {
            const auto *before =
                Find(step, XEMU_SHADER_CAPTURE_REPLAY_COLOR, false);
            auto original = before ? Read(*before) : nullptr;
            if (!original || original->size() != output.size()) {
                if (status == CaptureReplayStatus::Running)
                    Stop(CaptureReplayStatus::Unsupported,
                         "Partial clear has no current branch destination "
                         "version");
                return false;
            }
            output = *original;
        }
        const auto &description = *step.description;
        const auto &image = target->description.image;
        for (uint32_t y = 0; y < description.height; ++y) {
            uint32_t row = description.destination_y + y;
            if (image.coordinate_origin == XEMU_SHADER_CAPTURE_REPLAY_BOTTOM_UP)
                row = image.height - 1 - row;
            for (uint32_t x = 0; x < description.width; ++x)
                std::copy(color.begin(), color.end(),
                          output.begin() + (uint64_t(row) * image.width +
                                            description.destination_x + x) *
                                               4);
        }
        auto owned = Own(std::move(output));
        if (!owned || !Publish(*target, std::move(owned))) {
            Stop(CaptureReplayStatus::BudgetExceeded,
                 "Replay clear value publication exceeds its byte bound");
            return false;
        }
        return true;
    }
    bool Copy(const CaptureReplayStep &step)
    {
        if (step.description->kind == uint32_t(CaptureCommandKind::Clear))
            return Clear(step);
        if (step.description->kind == uint32_t(CaptureCommandKind::CpuUpload)) {
            const auto *target =
                Find(step, XEMU_SHADER_CAPTURE_REPLAY_COPY_DESTINATION, true);
            if (!target || !target->checkpoint)
                return false;
            auto bytes = std::shared_ptr<const std::vector<uint8_t>>(
                target->checkpoint, &target->checkpoint->bytes);
            if (!Publish(*target, std::move(bytes))) {
                Stop(CaptureReplayStatus::BudgetExceeded,
                     "Replay upload index exceeds its byte bound");
                return false;
            }
            return true;
        }
        const auto *source =
            Find(step, XEMU_SHADER_CAPTURE_REPLAY_COPY_SOURCE, false);
        const auto *target =
            Find(step, XEMU_SHADER_CAPTURE_REPLAY_COPY_DESTINATION, true);
        if (!source || !target)
            return false;
        auto input = Read(*source);
        if (!input)
            return false;
        const uint64_t needed = ValueBytes(*target) +
                                sizeof(std::vector<uint8_t>) + 128 +
                                kNodeCharge;
        if (needed > limits.value_byte_budget ||
            account->bytes.load() > limits.value_byte_budget - needed) {
            Stop(CaptureReplayStatus::BudgetExceeded,
                 "Replay copy exceeds its remaining byte bound");
            return false;
        }
        std::vector<uint8_t> output;
        const auto &description = *step.description;
        if (step.description->kind == uint32_t(CaptureCommandKind::ImageCopy)) {
            output.resize(ValueBytes(*target));
            if (!WholeCopy(step)) {
                const auto *before = Find(
                    step, XEMU_SHADER_CAPTURE_REPLAY_COPY_DESTINATION, false);
                auto original = before ? Read(*before) : nullptr;
                if (!original || original->size() != output.size())
                    return false;
                output = *original;
            }
            const auto &src = source->description.image,
                       &dst = target->description.image;
            for (uint32_t y = 0; y < description.height; ++y) {
                uint32_t sy = description.source_y + y,
                         dy = description.destination_y + y;
                if (src.coordinate_origin ==
                    XEMU_SHADER_CAPTURE_REPLAY_BOTTOM_UP)
                    sy = src.height - 1 - sy;
                if (dst.coordinate_origin ==
                    XEMU_SHADER_CAPTURE_REPLAY_BOTTOM_UP)
                    dy = dst.height - 1 - dy;
                std::copy_n(
                    input->data() +
                        (uint64_t(sy) * src.width + description.source_x) * 4,
                    uint64_t(description.width) * 4,
                    output.data() +
                        (uint64_t(dy) * dst.width + description.destination_x) *
                            4);
            }
        } else {
            if (description.bytes != input->size() ||
                description.bytes != ValueBytes(*target))
                return false;
            output = *input;
        }
        auto owned = Own(std::move(output));
        if (!owned) {
            Stop(CaptureReplayStatus::BudgetExceeded,
                 "Replay value byte bound exceeded");
            return false;
        }
        return Publish(*target, std::move(owned));
    }
};
CaptureReplaySequence::CaptureReplaySequence() : impl_(std::make_unique<Impl>())
{
}
CaptureReplaySequence::~CaptureReplaySequence() = default;
uint64_t
CaptureReplaySequence::Start(std::shared_ptr<const CaptureReplayPlan> plan,
                             const CaptureReplayLimits &limits)
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    auto &s = *impl_;
    s.run = NewReplayIdentity();
    s.Stop(CaptureReplayStatus::Cancelled, "Replay replaced");
    s.ClearValues();
    s.plan.reset();
    s.account = std::make_shared<Impl::Account>();
    s.cursor = s.branch = 0;
    s.limits = limits;
    if (!plan || plan->status != CaptureReplayStatus::Ready ||
        !plan->logical_closure_complete || !limits.value_byte_budget ||
        !s.run || plan->steps.size() > limits.maximum_steps ||
        plan->retained_bytes > limits.plan_byte_budget)
        return 0;
    s.plan = std::move(plan);
    try {
        for (const auto &step : s.plan->steps)
            for (const auto &binding : step.bindings)
                if (binding.checkpoint && !binding.description.resource.write) {
                    auto bytes = std::shared_ptr<const std::vector<uint8_t>>(
                        binding.checkpoint, &binding.checkpoint->bytes);
                    for (auto &values : s.values) {
                        const auto key = Key(binding.versions[0]);
                        if (values.count(key))
                            continue;
                        if (!s.Charge(Impl::kNodeCharge)) {
                            s.ClearValues();
                            s.Stop(CaptureReplayStatus::BudgetExceeded,
                                   "Replay checkpoint index exceeds its byte "
                                   "bound");
                            return 0;
                        }
                        try {
                            values.emplace(
                                key, Impl::Value{ bytes,
                                                  binding.description.image });
                        } catch (...) {
                            s.account->bytes.fetch_sub(Impl::kNodeCharge);
                            throw;
                        }
                    }
                }
        // Checkpoints are already charged to the retained plan. Value budget
        // measures newly owned replay values, including copies retained by
        // jobs.
        s.status = CaptureReplayStatus::Running;
        s.reason.clear();
    } catch (const std::bad_alloc &) {
        s.ClearValues();
        s.Stop(CaptureReplayStatus::BudgetExceeded,
               "Replay initialization allocation failed");
        return 0;
    }
    return s.run;
}
bool CaptureReplaySequence::TryClaimDraw(CaptureReplayDraw *out,
                                         std::string *error)
{
    if (!out)
        return false;
    std::lock_guard<std::mutex> lock(impl_->mutex);
    auto &s = *impl_;
    *out = {};
    if (s.status != CaptureReplayStatus::Running || s.claimed)
        return false;
    try {
        while (s.branch < 2) {
            if (s.cursor == s.plan->steps.size()) {
                ++s.branch;
                s.cursor = 0;
                continue;
            }
            const auto &step = s.plan->steps[s.cursor];
            if (step.description->kind != uint32_t(CaptureCommandKind::Draw)) {
                if (!s.Copy(step)) {
                    if (s.status == CaptureReplayStatus::Running)
                        s.Stop(
                            CaptureReplayStatus::Unsupported,
                            "Dependency operation lacks a current owned input "
                            "version");
                    return Fail(error, s.reason.c_str());
                }
                ++s.cursor;
                continue;
            }
            CaptureReplayDraw draw;
            s.token = NewReplayIdentity();
            if (!s.token) {
                s.Stop(CaptureReplayStatus::Failed,
                       "Replay token namespace exhausted");
                return false;
            }
            draw.identity = { s.run, s.token, step.occurrence->event_id,
                              step.occurrence->resource_evidence->sequence,
                              s.branch };
            draw.occurrence = step.occurrence;
            draw.edited_seed = step.edited_seed && s.branch == 1;
            for (const auto &binding : step.bindings)
                if (!binding.description.resource.write) {
                    auto bytes = s.Read(binding);
                    if (!bytes) {
                        if (s.status == CaptureReplayStatus::Running)
                            s.Stop(CaptureReplayStatus::Unsupported,
                                   "Draw input is missing its branch-owned "
                                   "produced version");
                        return Fail(error, s.reason.c_str());
                    }
                    draw.inputs.push_back(
                        { binding.description, std::move(bytes) });
                }
            s.identity = draw.identity;
            s.claimed = true;
            s.expected = false;
            *out = std::move(draw);
            return true;
        }
        s.Stop(
            CaptureReplayStatus::Completed,
            "Reconstructed original and edited dependency sequences completed");
    } catch (const std::bad_alloc &) {
        s.Stop(CaptureReplayStatus::BudgetExceeded,
               "Replay claim allocation failed");
        return Fail(error, s.reason.c_str());
    }
    return false;
}
bool CaptureReplaySequence::ExpectResult(const CaptureReplayIdentity &identity,
                                         const PreviewResultKey &result)
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    auto &s = *impl_;
    const auto *target = s.Current(identity) ?
                             Find(s.plan->steps[s.cursor],
                                  XEMU_SHADER_CAPTURE_REPLAY_COLOR, true) :
                             nullptr;
    if (!target || s.expected || result.channel != PreviewChannel::FinalRGBA ||
        result.packet_kind != PreviewPacketKind::Replay ||
        result.width != target->description.image.width ||
        result.height != target->description.image.height)
        return false;
    s.result = result;
    s.expected = true;
    return true;
}
bool CaptureReplaySequence::Complete(const CaptureReplayIdentity &identity,
                                     const PreviewResultKey &result,
                                     OwnedDrawImage image, std::string *error)
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    auto &s = *impl_;
    if (!s.Current(identity) || !s.expected || result != s.result)
        return Fail(error, "Stale or mismatched dependency result");
    const auto *target =
        Find(s.plan->steps[s.cursor], XEMU_SHADER_CAPTURE_REPLAY_COLOR, true);
    if (!target || image.width != result.width ||
        image.height != result.height ||
        image.rgba.size() != uint64_t(result.width) * result.height * 4) {
        s.Stop(CaptureReplayStatus::Failed,
               "Native dependency result dimensions differ");
        return Fail(error, s.reason.c_str());
    }
    try {
        auto bytes = s.Own(std::move(image.rgba));
        if (!bytes) {
            s.Stop(CaptureReplayStatus::BudgetExceeded,
                   "Replay value byte bound exceeded");
            return Fail(error, s.reason.c_str());
        }
        if (!s.Publish(*target, std::move(bytes))) {
            s.Stop(CaptureReplayStatus::BudgetExceeded,
                   "Dependency output version cannot be admitted");
            return Fail(error, s.reason.c_str());
        }
        ++s.cursor;
        s.claimed = s.expected = false;
    } catch (const std::bad_alloc &) {
        s.Stop(CaptureReplayStatus::BudgetExceeded,
               "Replay publication allocation failed");
        return Fail(error, s.reason.c_str());
    }
    return true;
}
void CaptureReplaySequence::Cancel()
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->Stop(CaptureReplayStatus::Cancelled, "Dependency replay cancelled");
    impl_->ClearValues();
    impl_->plan.reset();
}
CaptureReplayStatus CaptureReplaySequence::Status() const
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->status;
}
uint64_t CaptureReplaySequence::RetainedValueBytes() const
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->account->bytes.load();
}
std::string CaptureReplaySequence::Reason() const
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->reason;
}
} // namespace xemu::shader_browser

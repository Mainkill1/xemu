// SPDX-License-Identifier: GPL-2.0-or-later
#include "shader-browser-capture-session.hh"

#include <nlohmann/json.hpp>
#include <xxhash.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <fstream>
#include <map>
#include <mutex>
#include <unordered_map>
#include <unordered_set>

namespace xemu::shader_browser {
namespace {
constexpr uint64_t kMaximumBudget = UINT64_C(1024) * 1024 * 1024;
constexpr size_t kMaximumBlob = 16U * 1024U * 1024U;
std::atomic<uint64_t> next_event_id{ 1 };
std::atomic<uint64_t> next_resource_owner{ 1 };
std::atomic<uint64_t> next_batch_id{ 1 };
uint64_t NewBatchId()
{
    uint64_t value = next_batch_id.load(std::memory_order_relaxed);
    while (value && value < (UINT64_C(1) << 63)) {
        if (next_batch_id.compare_exchange_weak(value, value + 1,
                                                std::memory_order_relaxed))
            return value;
    }
    return 0;
}
CaptureDigest Digest(const void *data, size_t bytes)
{
    const XXH128_hash_t hash = XXH3_128bits(data, bytes);
    XXH128_canonical_t canonical;
    XXH128_canonicalFromHash(&canonical, hash);
    CaptureDigest digest;
    std::copy_n(canonical.digest, digest.size(), digest.begin());
    return digest;
}
std::vector<uint8_t> Bytes(const SharedCaptureBlock &block)
{
    return block ? block->bytes : std::vector<uint8_t>{};
}
std::vector<uint64_t> RetainedFrames(
    const std::vector<std::shared_ptr<const CaptureOccurrence>> &events)
{
    std::vector<uint64_t> frames;
    for (const auto &event : events) {
        if (event &&
            (frames.empty() || frames.back() != event->summary.key.frame))
            frames.push_back(event->summary.key.frame);
    }
    std::sort(frames.begin(), frames.end());
    frames.erase(std::unique(frames.begin(), frames.end()), frames.end());
    return frames;
}
OwnedDrawImage Image(const CaptureOwnedImage &image)
{
    return { image.width, image.height, Bytes(image.rgba) };
}
size_t NameLength(const char *name)
{
    if (!name)
        return 0;
    size_t size = 0;
    while (size < 256 && name[size])
        ++size;
    return size < 256 ? size : 0;
}
bool ValidSettings(const CaptureSessionSettings &settings)
{
    if (settings.live_stage_sets.size() > 128 ||
        (settings.mode != CaptureSessionMode::LiveDrawInputs &&
         (!settings.live_stage_sets.empty() || settings.live_continuous)) ||
        (settings.live_continuous &&
         (settings.history_frames < 2 || settings.history_frames > 4)))
        return false;
    for (const auto &set : settings.live_stage_sets) {
        if (set.empty() || set.size() > kCapturedShaderSlots)
            return false;
        for (size_t i = 0; i < set.size(); ++i) {
            if (uint32_t(set[i].stage) >= uint32_t(Stage::Unknown))
                return false;
            for (size_t j = 0; j < i; ++j)
                if (set[i].stage == set[j].stage)
                    return false;
        }
    }
    const auto limits = CaptureSessionPreTriggerLimits(settings);
    return settings.cpu_byte_budget &&
           settings.cpu_byte_budget <= kMaximumBudget &&
           settings.per_event_byte_budget &&
           settings.per_event_byte_budget <= kDrawInputBudget &&
           settings.disk_byte_budget &&
           settings.disk_byte_budget <= kMaximumBudget &&
           settings.event_budget && settings.event_budget <= 1000000 &&
           settings.history_frames && settings.history_frames <= 600 &&
           settings.post_frames <= 600 &&
           settings.post_trigger_reserve_percent <= 99 &&
           (settings.mode != CaptureSessionMode::RollingAnimation ||
            !settings.post_frames || !settings.post_trigger_reserve_percent ||
            (limits.cpu_bytes && limits.events)) &&
           (settings.mode == CaptureSessionMode::NextFrame ||
            settings.mode == CaptureSessionMode::RollingAnimation ||
            settings.mode == CaptureSessionMode::LiveDrawInputs);
}
bool ValidExecutionReceipt(const CaptureOccurrence &event)
{
    if (!event.batch_id &&
        (event.command_ordinal || event.command_recorded ||
         event.queue_ordinal || event.submission != CaptureBatchOutcome::None ||
         event.completion != CaptureBatchCompletion::None ||
         event.backend_result || event.completion_result))
        return false;
    if (event.completion == CaptureBatchCompletion::Completed &&
        (!event.batch_id || !event.command_recorded ||
         event.submission != CaptureBatchOutcome::Submitted ||
         event.command_phase == CaptureCommandPhase::HostPreparation ||
         !event.queue_ordinal))
        return false;
    return true;
}
} // namespace

CaptureResourceLimits
CaptureSessionResourceLimits(const CaptureSessionSettings &settings)
{
    CaptureResourceLimits limits;
    limits.max_events = limits.max_job_events = settings.event_budget;
    limits.max_job_work = 16777216;
    limits.byte_budget =
        std::min<uint64_t>(64U * 1024U * 1024U, settings.cpu_byte_budget);
    return limits;
}
CaptureResourceGraph
BuildCaptureSessionResourceGraph(const CaptureSessionSnapshot &snapshot,
                                 uint64_t byte_budget)
{
    auto limits = CaptureSessionResourceLimits(snapshot.settings);
    if (byte_budget)
        limits.byte_budget = std::min(limits.byte_budget, byte_budget);
    uint64_t validation_overhead =
        snapshot.events.size() * 2 * sizeof(SharedCaptureResourceEvent);
    for (const auto &event : snapshot.events)
        if (event && event->batch_id)
            validation_overhead += 512;
    if (validation_overhead >= limits.byte_budget) {
        CaptureResourceGraph graph;
        graph.budget_exceeded = true;
        return graph;
    }
    limits.byte_budget -= validation_overhead;
    std::vector<SharedCaptureResourceEvent> events;
    events.reserve(snapshot.events.size());
    uint64_t diagnostic_sequence = 0;
    for (const auto &event : snapshot.events)
        if (event && event->resource_evidence)
            diagnostic_sequence = std::max(diagnostic_sequence,
                                           event->resource_evidence->sequence);
    uint64_t observation = 0;
    std::unordered_map<uint64_t, const CaptureOccurrence *> batch_events;
    for (const auto &event : snapshot.events) {
        if (!event || !ValidExecutionReceipt(*event) || !event->event_id ||
            event->event_id <= observation ||
            event->event_id >= kCaptureSessionTokenBit ||
            (event->resource_evidence &&
             (event->resource_evidence->event_id != event->event_id ||
              (snapshot.resource_domain &&
               event->resource_evidence->domain_id !=
                   snapshot.resource_domain)))) {
            CaptureResourceGraph graph;
            graph.invalid_input = true;
            return graph;
        }
        observation = event->event_id;
        if (event->batch_id)
            batch_events.emplace(event->event_id, event.get());
        if (event->resource_evidence)
            events.push_back(event->resource_evidence);
        else if (!event->batch_id) {
            if (diagnostic_sequence == UINT64_MAX) {
                CaptureResourceGraph graph;
                graph.invalid_input = true;
                return graph;
            }
            auto diagnostic = std::make_shared<CaptureResourceEvent>();
            diagnostic->event_id = event->event_id;
            diagnostic->domain_id =
                snapshot.resource_domain ? snapshot.resource_domain : 1;
            diagnostic->sequence = ++diagnostic_sequence;
            if (event->type == CaptureEventType::Draw ||
                event->type == CaptureEventType::Clear ||
                event->type == CaptureEventType::Copy ||
                event->type == CaptureEventType::Upload ||
                event->type == CaptureEventType::Resolve) {
                CaptureResourceAccess access{};
                access.type = CaptureResourceOperationType::Read;
                CaptureResourceVersion gap;
                gap.domain_id = diagnostic->domain_id;
                gap.provenance = CaptureResourceProvenance::UnknownView;
                access.reads.push_back(gap);
                diagnostic->accesses.push_back(std::move(access));
            }
            events.push_back(std::move(diagnostic));
        }
    }
    auto graph = BuildCaptureResourceGraph(events, limits);
    graph.execution_order_complete =
        snapshot.execution_order_complete && !snapshot.pending_events;
    struct BatchOrder {
        uint64_t queue = 0, ordinal = 0;
        CaptureCommandPhase phase = CaptureCommandPhase::HostPreparation;
        bool gpu = false;
    };
    std::unordered_map<uint64_t, BatchOrder> batch_order;
    std::unordered_map<uint64_t, uint64_t> queue_owner;
    uint64_t last_queue = 0;
    for (const auto &evidence : graph.events) {
        auto found = batch_events.find(evidence->event_id);
        if (found == batch_events.end())
            continue;
        const auto &event = *found->second;
        if (!event.command_recorded || !event.command_ordinal ||
            !event.resource_finalized) {
            graph.invalid_input = true;
            return graph;
        }
        if (event.command_phase == CaptureCommandPhase::HostPreparation)
            continue;
        auto &batch = batch_order[event.batch_id];
        auto queue = queue_owner.emplace(event.queue_ordinal, event.batch_id);
        if ((!queue.second && queue.first->second != event.batch_id) ||
            event.submission != CaptureBatchOutcome::Submitted ||
            !event.queue_ordinal ||
            (event.type == CaptureEventType::Draw && !event.emitted) ||
            event.queue_ordinal < last_queue ||
            (batch.gpu && (batch.queue != event.queue_ordinal ||
                           event.command_phase < batch.phase ||
                           (event.command_phase == batch.phase &&
                            event.command_ordinal <= batch.ordinal)))) {
            graph.invalid_input = true;
            return graph;
        }
        batch = { event.queue_ordinal, event.command_ordinal,
                  event.command_phase, true };
        last_queue = event.queue_ordinal;
    }
    return graph;
}

OwnedDrawGeometry CaptureOccurrence::CopyGeometry() const
{
    OwnedDrawGeometry result;
    if (geometry.positions &&
        geometry.positions->bytes.size() ==
            geometry.position_count * sizeof(std::array<float, 4>)) {
        result.positions.resize(geometry.position_count);
        std::memcpy(result.positions.data(), geometry.positions->bytes.data(),
                    geometry.positions->bytes.size());
    }
    if (geometry.indices && geometry.indices->bytes.size() ==
                                geometry.index_count * sizeof(uint32_t)) {
        result.indices.resize(geometry.index_count);
        std::memcpy(result.indices.data(), geometry.indices->bytes.data(),
                    geometry.indices->bytes.size());
    }
    return result;
}
OwnedDrawInputs CaptureOccurrence::CopyInputs() const
{
    OwnedDrawInputs result;
    result.complete = inputs.complete;
    result.before = Image(inputs.before);
    result.after = Image(inputs.after);
    result.registers = inputs.registers;
    for (size_t i = 0; i < inputs.textures.size(); ++i) {
        auto &out = result.textures[i];
        const auto &in = inputs.textures[i];
        out.described = in.described;
        out.metadata = in.metadata;
        for (const auto &image : in.images)
            out.images.push_back(
                { image.mip_level, image.face, Image(image.image) });
    }
    for (const auto &uniform : inputs.uniforms)
        result.uniforms.push_back({ uniform.stage, uniform.name, uniform.type,
                                    uniform.components, uniform.count,
                                    Bytes(uniform.data) });
    for (size_t i = 0; i < inputs.sources.size(); ++i)
        if (inputs.sources[i])
            result.sources[i].assign(inputs.sources[i]->bytes.begin(),
                                     inputs.sources[i]->bytes.end());
    for (const auto &blob : inputs.blobs)
        result.blobs.push_back(
            { blob.name, Bytes(blob.data), blob.slot, blob.format,
              blob.components, blob.stride, blob.count, blob.offset,
              uint32_t(blob.normalized), uint32_t(blob.integer) });
    return result;
}

struct CaptureSession::Impl {
    struct Account {
        std::atomic<uint64_t> bytes{ 0 };
    };
    struct Record {
        CaptureOccurrence event;
        struct ResourceInput {
            XemuShaderCaptureResource descriptor;
            SharedCaptureBlock snapshot;
        };
        std::vector<ResourceInput> resource_operations;
        std::shared_ptr<Account> account;
        uint64_t charge = 0, reserved = 0;
        uint64_t batch_handle = 0;
        ~Record()
        {
            account->bytes.fetch_sub(charge + reserved);
        }
    };
    struct Batch {
        std::shared_ptr<Account> account;
        uint64_t generation = 0, charge = 0, archive_id = 0;
        bool submitted = false;
        std::vector<uint64_t> members;
        ~Batch()
        {
            account->bytes.fetch_sub(charge);
        }
    };
    std::unordered_map<uint64_t, std::shared_ptr<Batch>> batches;
    uint64_t owner_bytes = 0, last_queue_ordinal = 0, next_archive_batch = 0;
    mutable std::mutex mutex;
    std::atomic<bool> active{ false };
    std::atomic<uint64_t> revision{ 1 };
    CaptureSessionSettings settings;
    CaptureSessionContext context;
    CaptureSessionState state = CaptureSessionState::Cancelled;
    std::string reason;
    std::shared_ptr<Account> account = std::make_shared<Account>();
    std::vector<std::shared_ptr<Record>> events;
    std::unordered_map<uint64_t, std::shared_ptr<Record>> pending;
    std::unordered_map<uint64_t, std::shared_ptr<Record>> by_id;
    std::map<CaptureDigest,
             std::vector<std::weak_ptr<const CaptureImmutableBlock>>>
        pool;
    std::unordered_map<ShaderKey, std::vector<uint64_t>, ShaderKeyHash> uses;
    struct Owner {
        uint64_t allocation, view, bytes;
        uint32_t flags;
    };
    std::unique_ptr<CaptureResourceTimeline> resources;
    std::unordered_map<uint64_t, Owner> owners;
    std::unordered_set<uint64_t> retired_owners;
    uint64_t next_block = 1, total_events = 0, frame = 0, trigger = 0;
    uint64_t claim_generation = 0;
    bool have_boundary = false, checkpoint_taken = false;
    bool frame_window_complete = false;
    uint64_t UsedBytes() const
    {
        return account->bytes.load() +
               (resources ? resources->AccountedBytes() : 0);
    }
    bool PreTriggerReserve() const
    {
        return settings.mode == CaptureSessionMode::RollingAnimation &&
               state == CaptureSessionState::Recording &&
               settings.post_frames && settings.post_trigger_reserve_percent;
    }
    uint64_t ByteLimit() const
    {
        return PreTriggerReserve() ?
                   CaptureSessionPreTriggerLimits(settings).cpu_bytes :
                   settings.cpu_byte_budget;
    }
    uint64_t EventLimit() const
    {
        return PreTriggerReserve() ?
                   CaptureSessionPreTriggerLimits(settings).events :
                   settings.event_budget;
    }
    bool Fits(uint64_t bytes, uint64_t additional_events = 0) const
    {
        return bytes <= ByteLimit() && UsedBytes() <= ByteLimit() - bytes &&
               additional_events <= EventLimit() &&
               events.size() <= EventLimit() - additional_events;
    }
    bool MakeRoom(uint64_t bytes, uint64_t additional_events = 0)
    {
        if (Fits(bytes, additional_events))
            return true;
        if (!PreTriggerReserve() || bytes > ByteLimit() ||
            additional_events > EventLimit())
            return false;
        uint64_t after = 0;
        bool have_after = false;
        size_t attempts = 0;
        size_t work = 0;
        while (++attempts <= settings.event_budget) {
            uint64_t candidate = UINT64_MAX;
            for (const auto &record : events) {
                const auto at = record->event.summary.key.frame;
                if (at < frame && (!have_after || at > after))
                    candidate = std::min(candidate, at);
            }
            if (candidate == UINT64_MAX)
                break;
            after = candidate;
            have_after = true;
            std::unordered_set<uint64_t> retained;
            if (!RetentionClosure(candidate, retained, true, &work))
                return false;
            const bool protected_frame = std::any_of(
                events.begin(), events.end(), [&](const auto &record) {
                    const auto &event = record->event;
                    return event.summary.key.frame == candidate &&
                           (retained.count(event.event_id) || event.pending ||
                            !event.finished || !event.inputs.complete ||
                            !event.resource_finalized ||
                            event.completion ==
                                CaptureBatchCompletion::Pending);
                });
            if (protected_frame)
                continue;
            events.erase(
                std::remove_if(events.begin(), events.end(),
                               [&](const auto &record) {
                                   return record->event.summary.key.frame ==
                                          candidate;
                               }),
                events.end());
            if (resources)
                resources->RetainEvents(
                    std::vector<uint64_t>(retained.begin(), retained.end()));
            // by_id owns records too. Outside snapshots and blocks keep their
            // charges until their actual final owner releases them.
            Reindex();
            ++revision;
            if (Fits(bytes, additional_events))
                return true;
        }
        return false;
    }
    bool Admit(uint64_t bytes, uint64_t additional_events, const char *message,
               Record *record = nullptr)
    {
        const bool pre = PreTriggerReserve();
        if (MakeRoom(bytes, additional_events))
            return true;
        if (!pre || state != CaptureSessionState::BudgetExceeded)
            Exhaust(pre ? std::string("Post-trigger reserve: ") + message +
                              "; protected evidence or external owners prevent "
                              "eviction" :
                          std::string(message),
                    record);
        return false;
    }

    void Exhaust(const std::string &message, Record *record = nullptr)
    {
        ++revision;
        state = CaptureSessionState::BudgetExceeded;
        active.store(false, std::memory_order_release);
        if (reason.empty())
            reason = message;
        if (record) {
            if (record->event.failure.empty())
                record->event.failure = message;
            record->event.limitations |= CaptureReadbackFailed;
        }
    }
    bool Charge(Record &record, size_t bytes)
    {
        if (!Admit(bytes, 0, "CPU byte budget exceeded", &record)) {
            return false;
        }
        account->bytes.fetch_add(bytes);
        record.charge += bytes;
        ++revision;
        return true;
    }
    Record *Lookup(uint64_t token)
    {
        auto found = pending.find(token);
        return found == pending.end() ? nullptr : found->second.get();
    }
    bool BuildResources(Record &record, bool emitted,
                        std::vector<CaptureResourceOperation> &operations)
    {
        if (!resources)
            return false;
        for (const auto &staged : record.resource_operations) {
            const auto &input = staged.descriptor;
            const bool write =
                input.access == XEMU_SHADER_CAPTURE_RESOURCE_PARTIAL_WRITE ||
                input.access == XEMU_SHADER_CAPTURE_RESOURCE_FULL_WRITE ||
                input.access == XEMU_SHADER_CAPTURE_RESOURCE_UNCERTAIN_WRITE;
            if (write && record.event.type == CaptureEventType::Draw &&
                !emitted)
                continue;
            CaptureResourceOperation operation;
            operation.range = { input.offset, input.size };
            operation.kind = input.kind;
            operation.slot = input.slot;
            operation.flags =
                input.flags & XEMU_SHADER_CAPTURE_RESOURCE_OPAQUE_EXTENT;
            if (staged.snapshot) {
                operation.snapshot_block = staged.snapshot->id;
                operation.content_digest = staged.snapshot->digest;
            }
            const bool valid_owner =
                input.owner && input.byte_size &&
                input.access <= XEMU_SHADER_CAPTURE_RESOURCE_RELEASE &&
                !(input.flags & ~XEMU_SHADER_CAPTURE_RESOURCE_OPAQUE_EXTENT);
            if (!valid_owner)
                record.event.limitations |=
                    CaptureMalformed | CaptureMissingDependencies;
            auto found = valid_owner ? owners.find(input.owner) : owners.end();
            if (valid_owner && found == owners.end() &&
                !retired_owners.count(input.owner)) {
                Owner owner{ resources->ReserveAllocationId(),
                             resources->ReserveViewId(), input.byte_size,
                             input.flags };
                constexpr uint64_t owner_charge = sizeof(Owner) + 256;
                if (!owner.allocation || !owner.view ||
                    owners.size() >= CaptureSessionResourceLimits(settings)
                                         .max_allocations) {
                    Exhaust("Resource owner allocation budget exceeded",
                            &record);
                    return false;
                }
                if (!Admit(owner_charge, 0,
                           "Resource owner allocation budget exceeded",
                           &record))
                    return false;
                account->bytes.fetch_add(owner_charge);
                owner_bytes += owner_charge;
                CaptureResourceOperation allocate;
                allocate.type = CaptureResourceOperationType::Allocate;
                allocate.allocation_id = owner.allocation;
                allocate.byte_size = owner.bytes;
                allocate.kind = input.kind;
                allocate.flags = input.flags;
                operations.push_back(allocate);
                CaptureResourceOperation alias;
                alias.type = CaptureResourceOperationType::Alias;
                alias.view_id = owner.view;
                alias.aliases = { { owner.allocation, { 0, owner.bytes } } };
                alias.kind = input.kind;
                alias.flags = input.flags;
                operations.push_back(std::move(alias));
                found = owners.emplace(input.owner, owner).first;
            }
            if (found != owners.end()) {
                if (found->second.bytes != input.byte_size ||
                    found->second.flags != input.flags) {
                    record.event.limitations |=
                        CaptureMalformed | CaptureMissingDependencies;
                    record.event.failure = "Backing extent changed without a "
                                           "new owner incarnation";
                } else {
                    operation.view_id = found->second.view;
                    operation.allocation_id = found->second.allocation;
                }
            }
            switch (input.access) {
            case XEMU_SHADER_CAPTURE_RESOURCE_READ:
                operation.type = CaptureResourceOperationType::Read;
                break;
            case XEMU_SHADER_CAPTURE_RESOURCE_PARTIAL_WRITE:
                operation.type = CaptureResourceOperationType::PartialWrite;
                break;
            case XEMU_SHADER_CAPTURE_RESOURCE_FULL_WRITE:
                operation.type = CaptureResourceOperationType::FullWrite;
                break;
            case XEMU_SHADER_CAPTURE_RESOURCE_UNCERTAIN_WRITE:
                operation.type = CaptureResourceOperationType::UncertainWrite;
                break;
            case XEMU_SHADER_CAPTURE_RESOURCE_RELEASE:
                operation.type = CaptureResourceOperationType::Release;
                if (input.owner)
                    retired_owners.insert(input.owner);
                if (!operation.allocation_id) {
                    operation.type = CaptureResourceOperationType::Read;
                    operation.view_id = 0;
                }
                break;
            default:
                record.event.limitations |= CaptureMalformed;
                operation.type = CaptureResourceOperationType::Read;
                operation.view_id = 0;
            }
            if ((input.flags & XEMU_SHADER_CAPTURE_RESOURCE_OPAQUE_EXTENT) &&
                (input.byte_size != 1 || input.offset || input.size != 1 ||
                 (write && input.access !=
                               XEMU_SHADER_CAPTURE_RESOURCE_UNCERTAIN_WRITE))) {
                operation.view_id = 0;
                record.event.limitations |= CaptureMalformed;
            }
            operations.push_back(std::move(operation));
        }
        return true;
    }
    void CheckEvidence(Record &record)
    {
        if (!record.event.resource_evidence) {
            record.event.limitations |=
                CaptureMissingDependencies | CaptureMalformed;
            Exhaust(resources->Status() ==
                            CaptureResourceModelStatus::BudgetExceeded ?
                        "Resource timeline budget exceeded" :
                        "Resource command ordering or descriptor is invalid",
                    &record);
        } else {
            bool gap = false;
            for (const auto &access :
                 record.event.resource_evidence->accesses) {
                for (const auto &read : access.reads)
                    gap |= read.provenance != CaptureResourceProvenance::Known;
                for (const auto &write : access.writes)
                    gap |= write.provenance != CaptureResourceProvenance::Known;
            }
            if (!gap && !(record.event.limitations & CaptureMalformed))
                record.event.limitations &= ~CaptureMissingDependencies;
            else
                record.event.limitations |= CaptureMissingDependencies;
            if (account->bytes.load() + resources->AccountedBytes() >
                ByteLimit())
                Exhaust("Resource evidence exceeds recorder CPU budget",
                        &record);
        }
    }
    void CommitResources(Record &record, bool emitted)
    {
        if (record.resource_operations.empty() || !resources)
            return;
        std::vector<CaptureResourceOperation> operations;
        if (!BuildResources(record, emitted, operations))
            return;
        if (record.resource_operations.empty() &&
            (record.event.type == CaptureEventType::Draw ||
             record.event.type == CaptureEventType::Copy ||
             record.event.type == CaptureEventType::Upload ||
             record.event.type == CaptureEventType::Clear ||
             record.event.type == CaptureEventType::Resolve)) {
            CaptureResourceOperation gap;
            gap.type = CaptureResourceOperationType::Read;
            operations.push_back(gap);
        }
        const auto result = resources->ApplyBatch(
            { { record.event.event_id, std::move(operations) } },
            UsedBytes() >= ByteLimit() ? 0 : ByteLimit() - UsedBytes());
        record.event.resource_evidence =
            result.empty() ? SharedCaptureResourceEvent{} : result.front();
        CheckEvidence(record);
        record.resource_operations.clear();
    }
    bool CommitBatch(Batch &batch, bool accepted)
    {
        std::vector<Record *> ordered;
        for (const auto token : batch.members) {
            auto found = by_id.find(token & ~kCaptureSessionTokenBit);
            auto *record = found == by_id.end() ? nullptr : found->second.get();
            if (!record || !record->event.finished)
                return false;
            if (record->event.resource_finalized &&
                record->event.command_phase ==
                    CaptureCommandPhase::HostPreparation)
                continue;
            if (record->event.command_recorded &&
                (accepted || record->event.command_phase ==
                                 CaptureCommandPhase::HostPreparation) &&
                (record->event.type != CaptureEventType::Draw ||
                 record->event.emitted))
                ordered.push_back(record);
        }
        if (ordered.empty())
            return true;
        std::sort(
            ordered.begin(), ordered.end(), [](const auto *a, const auto *b) {
                return a->event.command_phase != b->event.command_phase ?
                           a->event.command_phase < b->event.command_phase :
                           a->event.command_ordinal < b->event.command_ordinal;
            });
        uint64_t scratch = 512 + owners.size() * (sizeof(Owner) + 128) +
                           retired_owners.size() * 64 + ordered.size() * 128;
        for (const auto *record : ordered)
            scratch += record->resource_operations.size() *
                       (sizeof(CaptureResourceOperation) + 256);
        if (!Admit(scratch, 0,
                   "Batch transaction metadata exceeds CPU budget")) {
            return false;
        }
        account->bytes.fetch_add(scratch);
        struct Scratch {
            std::shared_ptr<Account> account;
            uint64_t bytes;
            ~Scratch()
            {
                account->bytes.fetch_sub(bytes);
            }
        } reservation{ account, scratch };
        auto saved_owners = owners;
        auto saved_retired = retired_owners;
        const uint64_t saved_owner_bytes = owner_bytes;
        const auto rollback = [&] {
            owners.swap(saved_owners);
            retired_owners.swap(saved_retired);
            account->bytes.fetch_sub(owner_bytes - saved_owner_bytes);
            owner_bytes = saved_owner_bytes;
        };
        try {
            std::vector<CaptureResourceCommand> commands;
            commands.reserve(ordered.size());
            for (auto *record : ordered) {
                CaptureResourceCommand command;
                command.event_id = record->event.event_id;
                if (!BuildResources(*record, record->event.emitted,
                                    command.operations)) {
                    rollback();
                    return false;
                }
                if (record->resource_operations.empty()) {
                    CaptureResourceOperation gap;
                    gap.type = CaptureResourceOperationType::Read;
                    command.operations.push_back(gap);
                }
                commands.push_back(std::move(command));
            }
            auto evidence = resources->ApplyBatch(
                commands,
                UsedBytes() >= ByteLimit() ? 0 : ByteLimit() - UsedBytes());
            if (evidence.size() != ordered.size()) {
                rollback();
                Exhaust("Batch resource transaction failed");
                return false;
            }
            for (size_t i = 0; i < ordered.size(); ++i) {
                ordered[i]->event.resource_evidence = evidence[i];
                bool gap = false;
                for (const auto &access : evidence[i]->accesses) {
                    for (const auto &read : access.reads)
                        gap |=
                            read.provenance != CaptureResourceProvenance::Known;
                    for (const auto &write : access.writes)
                        gap |= write.provenance !=
                               CaptureResourceProvenance::Known;
                }
                if (!gap && !(ordered[i]->event.limitations & CaptureMalformed))
                    ordered[i]->event.limitations &=
                        ~CaptureMissingDependencies;
                else
                    ordered[i]->event.limitations |= CaptureMissingDependencies;
            }
            return true;
        } catch (...) {
            rollback();
            throw;
        }
    }
    bool RetentionClosure(uint64_t first,
                          std::unordered_set<uint64_t> &retained,
                          bool exclude_frame = false,
                          size_t *total_work = nullptr)
    {
        std::vector<uint64_t> jobs = resources ?
                                         resources->LiveProducerEvents() :
                                         std::vector<uint64_t>{};
        for (const auto &batch : batches)
            for (const auto token : batch.second->members)
                jobs.push_back(token & ~kCaptureSessionTokenBit);
        for (const auto &record : events)
            if (record->event.pending ||
                (exclude_frame ? record->event.summary.key.frame != first :
                                 record->event.summary.key.frame >= first))
                jobs.push_back(record->event.event_id);
        size_t local_work = 0;
        size_t &work = total_work ? *total_work : local_work;
        while (!jobs.empty()) {
            uint64_t id = jobs.back();
            jobs.pop_back();
            if (!retained.insert(id).second)
                continue;
            if (++work > CaptureSessionResourceLimits(settings).max_job_work ||
                retained.size() > settings.event_budget) {
                Exhaust("Resource producer retention closure exceeds recorder "
                        "budget");
                return false;
            }
            auto found = by_id.find(id);
            if (found == by_id.end())
                continue; // Producer already unavailable: reference remains a
                          // gap.
            const auto &evidence = found->second->event.resource_evidence;
            if (evidence)
                for (const auto &access : evidence->accesses)
                    for (const auto &read : access.reads) {
                        if (++work > CaptureSessionResourceLimits(settings)
                                         .max_job_work) {
                            Exhaust("Resource producer retention work budget "
                                    "exceeded");
                            return false;
                        }
                        if (read.producer_event)
                            jobs.push_back(read.producer_event);
                    }
        }
        return true;
    }
    bool RetainDependencies(uint64_t first)
    {
        std::unordered_set<uint64_t> retained;
        if (!RetentionClosure(first, retained))
            return false;
        events.erase(std::remove_if(events.begin(), events.end(),
                                    [&](const auto &record) {
                                        return !retained.count(
                                            record->event.event_id);
                                    }),
                     events.end());
        if (resources)
            resources->RetainEvents(
                std::vector<uint64_t>(retained.begin(), retained.end()));
        return true;
    }
    void RetainLiveFrames(uint64_t first)
    {
        std::unordered_set<uint64_t> pending_frames;
        for (const auto &entry : pending)
            pending_frames.insert(entry.second->event.summary.key.frame);
        events.erase(std::remove_if(events.begin(), events.end(),
                                    [&](const auto &record) {
                                        const auto frame =
                                            record->event.summary.key.frame;
                                        return frame < first &&
                                               !pending_frames.count(frame);
                                    }),
                     events.end());
        // Live input inspection deliberately has no replay dependency closure.
        // Keep pending frames intact and leave pruned producer origins unknown.
        if (resources) {
            std::vector<uint64_t> retained;
            for (const auto &record : events)
                retained.push_back(record->event.event_id);
            resources->RetainEvents(retained);
        }
    }
    bool Store(Record &record, SharedCaptureBlock &destination,
               const void *data, size_t size)
    {
        const size_t previous = destination ? destination->bytes.size() : 0;
        const uint64_t remaining = record.event.payload_bytes - previous;
        if (size > settings.per_event_byte_budget ||
            remaining > settings.per_event_byte_budget - size) {
            Exhaust("Per-event byte budget exceeded", &record);
            return false;
        }
        if (!size) {
            destination.reset();
            record.event.payload_bytes = remaining;
            return true;
        }
        if (!data) {
            record.event.limitations |= CaptureMalformed;
            return false;
        }
        const CaptureDigest digest = Digest(data, size);
        SharedCaptureBlock block;
        bool reservation_consumed = false;
        auto found = pool.find(digest);
        if (found != pool.end()) {
            auto &candidates = found->second;
            for (auto it = candidates.begin(); it != candidates.end();) {
                auto candidate = it->lock();
                if (!candidate) {
                    it = candidates.erase(it);
                    continue;
                }
                if (candidate->bytes.size() == size &&
                    !std::memcmp(candidate->bytes.data(), data, size)) {
                    block = std::move(candidate);
                    break;
                }
                ++it;
            }
        }
        if (!block) {
            const uint64_t charge = size + sizeof(CaptureImmutableBlock);
            const uint64_t consumed = std::min<uint64_t>(record.reserved, size);
            if (!Admit(charge - consumed, 0, "CPU byte budget exceeded",
                       &record)) {
                return false;
            }
            account->bytes.fetch_sub(consumed);
            record.reserved -= consumed;
            try {
                auto owned = std::make_unique<CaptureImmutableBlock>();
                owned->id = next_block++;
                owned->digest = digest;
                owned->bytes.assign(static_cast<const uint8_t *>(data),
                                    static_cast<const uint8_t *>(data) + size);
                account->bytes.fetch_add(charge);
                auto budget = account;
                block = SharedCaptureBlock(
                    owned.release(),
                    [budget, charge](const CaptureImmutableBlock *p) {
                        delete p;
                        budget->bytes.fetch_sub(charge);
                    });
                pool[digest].push_back(block);
            } catch (const std::exception &) {
                block.reset();
                auto empty = pool.find(digest);
                if (empty != pool.end() && empty->second.empty())
                    pool.erase(empty);
                account->bytes.fetch_add(consumed);
                record.reserved += consumed;
                Exhaust("CPU capture allocation failed", &record);
                return false;
            }
            reservation_consumed = true;
        }
        destination = std::move(block);
        record.event.payload_bytes = remaining + size;
        const uint64_t consumed = reservation_consumed ?
                                      0 :
                                      std::min<uint64_t>(record.reserved, size);
        account->bytes.fetch_sub(consumed);
        record.reserved -= consumed;
        ++revision;
        return true;
    }
    void Publish(uint64_t token, Record &record)
    {
        if (!record.event.finished || !record.event.inputs.complete ||
            !record.event.resource_finalized ||
            record.event.completion == CaptureBatchCompletion::Pending)
            return;
        record.event.pending = false;
        account->bytes.fetch_sub(record.reserved);
        record.reserved = 0;
        pending.erase(token);
        ++revision;
        if (state == CaptureSessionState::Finalizing && pending.empty())
            state = CaptureSessionState::Ready;
    }
    void Finalize()
    {
        ++revision;
        active.store(false, std::memory_order_release);
        if (state == CaptureSessionState::Recording ||
            state == CaptureSessionState::Triggered)
            state = pending.empty() ? CaptureSessionState::Ready :
                                      CaptureSessionState::Finalizing;
    }
    void CancelPending(const std::string &message)
    {
        active.store(false, std::memory_order_release);
        for (auto &entry : pending) {
            auto &record = *entry.second;
            if (record.event.batch_id) {
                if (record.event.submission == CaptureBatchOutcome::Pending)
                    record.event.submission = CaptureBatchOutcome::Detached;
                record.event.completion = CaptureBatchCompletion::Failed;
            }
            record.event.limitations |= CaptureInvalidated;
            record.event.failure = message;
            record.event.pending = false;
            account->bytes.fetch_sub(record.reserved);
            record.reserved = 0;
        }
        pending.clear();
        batches.clear();
        state = CaptureSessionState::Cancelled;
        reason = message;
        ++revision;
    }
    void Reindex()
    {
        uses.clear();
        by_id.clear();
        for (const auto &record : events) {
            by_id.emplace(record->event.event_id, record);
            for (size_t i = 0; i < record->event.summary.shader_count; ++i)
                uses[record->event.summary.shaders[i]].push_back(
                    record->event.event_id);
        }
    }
    void BoundaryEvent(uint64_t frame,
                       CaptureEventType type = CaptureEventType::FrameBoundary)
    {
        if (!Admit(0, 1, "Event budget exceeded at guest frame boundary")) {
            return;
        }
        auto record = std::make_shared<Record>();
        record->account = account;
        if (!Charge(*record, sizeof(Record) + sizeof(void *) * 8))
            return;
        auto &event = record->event;
        event.event_id = next_event_id.fetch_add(1);
        event.type = type;
        event.summary.scope = context.scope;
        event.summary.key = { context.session_epoch, context.renderer_epoch,
                              frame, 0, 0 };
        event.finished = true;
        event.pending = false;
        event.inputs.complete = true;
        event.limitations = CaptureMissingDependencies;
        if (resources && (type == CaptureEventType::ResetBoundary ||
                          type == CaptureEventType::TitleBoundary ||
                          type == CaptureEventType::SaveStateBoundary)) {
            CaptureResourceOperation reset;
            reset.type = CaptureResourceOperationType::Reset;
            event.resource_evidence =
                resources->Apply(event.event_id, { reset });
            if (!event.resource_evidence)
                Exhaust("Resource reset boundary exceeded recorder budget",
                        record.get());
            owners.clear();
        }
        event.host_timestamp_ns =
            uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(
                         std::chrono::steady_clock::now().time_since_epoch())
                         .count());
        events.push_back(record);
        by_id.emplace(event.event_id, record);
        ++total_events;
        ++revision;
    }
    static std::shared_ptr<const CaptureOccurrence>
    SnapshotRecord(const std::shared_ptr<Record> &record)
    {
        const auto &event = record->event;
        if (!event.pending)
            return { record, &record->event };
        auto metadata = std::make_shared<CaptureOccurrence>();
        metadata->event_id = event.event_id;
        metadata->type = event.type;
        metadata->summary = event.summary;
        metadata->limitations = event.limitations;
        metadata->finished = event.finished;
        metadata->emitted = event.emitted;
        metadata->failure = event.failure;
        metadata->payload_bytes = event.payload_bytes;
        metadata->observed_checkpoint = event.observed_checkpoint;
        metadata->host_timestamp_ns = event.host_timestamp_ns;
        metadata->resource_evidence = event.resource_evidence;
        metadata->batch_id = event.batch_id;
        metadata->command_ordinal = event.command_ordinal;
        metadata->queue_ordinal = event.queue_ordinal;
        metadata->command_phase = event.command_phase;
        metadata->command = event.command;
        metadata->replay_description = event.replay_description;
        metadata->submission = event.submission;
        metadata->completion = event.completion;
        metadata->command_recorded = event.command_recorded;
        metadata->resource_finalized = event.resource_finalized;
        metadata->backend_result = event.backend_result;
        metadata->completion_result = event.completion_result;
        return metadata;
    }
};

CaptureSession::CaptureSession() : impl_(std::make_unique<Impl>())
{
}
CaptureSession::~CaptureSession() = default;
CaptureSession &GetCaptureSession()
{
    static CaptureSession session;
    return session;
}
extern "C" uint64_t xemu_shader_capture_resource_new_owner(void)
{
    uint64_t owner = next_resource_owner.load(std::memory_order_relaxed);
    while (owner != UINT64_MAX) {
        if (next_resource_owner.compare_exchange_weak(
                owner, owner + 1, std::memory_order_relaxed))
            return owner;
    }
    return 0;
}
extern "C" int
xemu_shader_capture_session_resource(uint64_t token,
                                     const XemuShaderCaptureResource *resource)
{
    if (!(token & kCaptureSessionTokenBit) || !resource)
        return 0;
    try {
        return GetCaptureSession().StageResource(token, *resource);
    } catch (...) {
        GetCaptureSession().AbortAllocationFailure(0, token);
        return 0;
    }
}
extern "C" int xemu_shader_capture_session_resource_snapshot(
    uint64_t token, const XemuShaderCaptureResource *resource,
    const char *blob_name)
{
    if (!(token & kCaptureSessionTokenBit) || !resource ||
        !NameLength(blob_name))
        return 0;
    try {
        return GetCaptureSession().StageResource(token, *resource, blob_name);
    } catch (...) {
        GetCaptureSession().AbortAllocationFailure(0, token);
        return 0;
    }
}
extern "C" void xemu_shader_capture_session_resource_release(uint64_t owner,
                                                             uint64_t byte_size,
                                                             uint32_t kind,
                                                             uint32_t flags)
{
    if (!owner || !GetCaptureSession().Active())
        return;
    uint64_t generation = 0;
    GetCaptureSession().Context(&generation);
    try {
        GetCaptureSession().ReleaseResource(owner, byte_size, kind, flags,
                                            generation);
    } catch (...) {
        GetCaptureSession().AbortAllocationFailure(generation);
    }
}
extern "C" int xemu_shader_capture_session_resource_read_snapshot(
    uint64_t token, uint32_t kind, uint32_t slot, const char *blob_name)
{
    if (!(token & kCaptureSessionTokenBit) || !NameLength(blob_name))
        return 0;
    try {
        return GetCaptureSession().AttachResourceReadSnapshot(token, kind, slot,
                                                              blob_name);
    } catch (...) {
        GetCaptureSession().AbortAllocationFailure(0, token);
        return 0;
    }
}
extern "C" int xemu_shader_capture_session_describe_replay(
    uint64_t token, const XemuShaderCaptureReplayDescription *description)
{
    if (!(token & kCaptureSessionTokenBit) || !description)
        return 0;
    try {
        return GetCaptureSession().DescribeReplay(token, *description);
    } catch (...) {
        GetCaptureSession().AbortAllocationFailure(0, token);
        return 0;
    }
}
bool CaptureSession::Active() const
{
    return impl_->active.load(std::memory_order_acquire);
}
CaptureSessionContext CaptureSession::Context(uint64_t *claim_generation) const
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (claim_generation)
        *claim_generation = impl_->claim_generation;
    return impl_->context;
}
uint64_t CaptureSession::Revision() const
{
    return impl_->revision.load(std::memory_order_acquire);
}
bool CaptureSession::Start(const CaptureSessionContext &context,
                           const CaptureSessionSettings &settings)
{
    return StartInternal(context, settings, false, nullptr);
}
bool CaptureSession::TryStart(const CaptureSessionContext &context,
                              const CaptureSessionSettings &settings,
                              uint64_t *claim_generation)
{
    return StartInternal(context, settings, true, claim_generation);
}
bool CaptureSession::StartInternal(const CaptureSessionContext &context,
                                   const CaptureSessionSettings &settings,
                                   bool idle_only, uint64_t *claim_generation)
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    auto &s = *impl_;
    if (idle_only && (s.active.load() || !s.pending.empty()))
        return false;
    s.active.store(false, std::memory_order_release);
    s.pending.clear();
    s.batches.clear();
    s.owner_bytes = s.last_queue_ordinal = s.next_archive_batch = 0;
    s.events.clear();
    s.pool.clear();
    s.uses.clear();
    s.by_id.clear();
    s.owners.clear();
    s.retired_owners.clear();
    s.resources.reset();
    s.account = std::make_shared<Impl::Account>();
    s.context = context;
    s.settings = settings;
    if (ValidSettings(settings))
        s.resources = std::make_unique<CaptureResourceTimeline>(
            CaptureSessionResourceLimits(settings));
    s.total_events = s.trigger = 0;
    s.next_block = 1;
    s.frame = context.current_frame;
    s.have_boundary = false;
    s.frame_window_complete = false;
    s.checkpoint_taken = false;
    s.reason.clear();
    ++s.revision;
    if (s.claim_generation == UINT64_MAX) {
        s.state = CaptureSessionState::Failed;
        s.reason = "Capture claim generation exhausted";
        return false;
    }
    ++s.claim_generation;
    if (!ValidSettings(settings) || !context.scope_generation ||
        !context.session_epoch || !context.renderer_epoch ||
        !context.generation) {
        s.state = CaptureSessionState::Failed;
        s.reason = "Invalid capture context or settings";
        return false;
    }
    s.state = CaptureSessionState::Recording;
    s.active.store(true, std::memory_order_release);
    if (claim_generation)
        *claim_generation = s.claim_generation;
    return true;
}
void CaptureSession::GuestFrameBoundary(uint64_t frame,
                                        uint64_t expected_generation)
{
    if (!Active())
        return;
    std::lock_guard<std::mutex> lock(impl_->mutex);
    auto &s = *impl_;
    if (!s.active.load() ||
        (expected_generation && expected_generation != s.claim_generation))
        return;
    if (s.have_boundary && frame < s.frame) {
        s.CancelPending(
            "Guest frame counter moved backwards; capture invalidated");
        return;
    }
    if (s.have_boundary && frame == s.frame)
        return;
    if (s.settings.mode != CaptureSessionMode::RollingAnimation &&
        !s.settings.live_continuous && s.have_boundary && frame > s.frame) {
        s.frame_window_complete = true;
        s.Finalize();
        return;
    }
    if (s.state == CaptureSessionState::Triggered && frame > s.trigger &&
        frame - s.trigger > s.settings.post_frames) {
        s.frame_window_complete = true;
        s.Finalize();
        return;
    }
    s.frame = frame;
    s.have_boundary = true;
    s.checkpoint_taken = false;
    ++s.revision;
    if ((s.settings.mode == CaptureSessionMode::RollingAnimation ||
         s.settings.live_continuous) &&
        s.state == CaptureSessionState::Recording) {
        const uint64_t first = frame >= s.settings.history_frames ?
                                   frame - s.settings.history_frames + 1 :
                                   0;
        if (s.settings.live_continuous)
            s.RetainLiveFrames(first);
        else if (!s.RetainDependencies(first))
            return;
        s.Reindex();
        for (auto it = s.pool.begin(); it != s.pool.end();) {
            auto &items = it->second;
            items.erase(
                std::remove_if(items.begin(), items.end(),
                               [](const auto &item) { return item.expired(); }),
                items.end());
            if (items.empty())
                it = s.pool.erase(it);
            else
                ++it;
        }
    }
    s.BoundaryEvent(frame);
}
bool CaptureSession::Mark()
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    auto &s = *impl_;
    if (s.state != CaptureSessionState::Recording ||
        s.settings.mode != CaptureSessionMode::RollingAnimation)
        return false;
    s.state = CaptureSessionState::Triggered;
    s.trigger = s.frame;
    ++s.revision;
    return true;
}
void CaptureSession::Stop()
{
    StopIfCurrent(0);
}
bool CaptureSession::StopIfCurrent(uint64_t claim_generation)
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (claim_generation && claim_generation != impl_->claim_generation)
        return false;
    impl_->Finalize();
    return true;
}
void CaptureSession::Cancel()
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->CancelPending("Capture cancelled before completion");
}
void CaptureSession::AbortAllocationFailure(uint64_t expected_generation,
                                            uint64_t token) noexcept
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    auto &s = *impl_;
    if ((expected_generation && expected_generation != s.claim_generation) ||
        (token && !s.Lookup(token)))
        return;
    s.active.store(false, std::memory_order_release);
    for (auto &entry : s.pending) {
        auto &record = *entry.second;
        if (record.event.batch_id) {
            if (record.event.submission == CaptureBatchOutcome::Pending)
                record.event.submission = CaptureBatchOutcome::Detached;
            record.event.completion = CaptureBatchCompletion::Failed;
        }
        record.event.limitations |= CaptureReadbackFailed | CaptureInvalidated;
        record.event.pending = false;
        record.event.failure.clear();
        s.account->bytes.fetch_sub(record.reserved);
        record.reserved = 0;
    }
    s.pending.clear();
    s.batches.clear();
    s.state = CaptureSessionState::BudgetExceeded;
    s.reason.clear();
    ++s.revision;
}
void CaptureSession::Invalidate(uint64_t scope, uint64_t session,
                                uint64_t renderer, uint64_t expected_generation)
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (expected_generation && expected_generation != impl_->claim_generation)
        return;
    if ((impl_->active.load() || !impl_->pending.empty()) &&
        (scope != impl_->context.scope_generation ||
         session != impl_->context.session_epoch ||
         renderer != impl_->context.renderer_epoch)) {
        impl_->BoundaryEvent(impl_->frame,
                             scope != impl_->context.scope_generation ||
                                     session != impl_->context.session_epoch ?
                                 CaptureEventType::TitleBoundary :
                                 CaptureEventType::ResetBoundary);
        impl_->CancelPending("Capture context changed before completion");
    }
}
uint64_t CaptureSession::BeginOccurrence(const DrawCaptureSummary &summary,
                                         CaptureEventType type,
                                         uint32_t limitations,
                                         uint64_t expected_generation,
                                         uint64_t expected_scope_generation)
{
    if (!Active())
        return 0;
    std::lock_guard<std::mutex> lock(impl_->mutex);
    auto &s = *impl_;
    if (!s.active.load())
        return 0;
    if (expected_generation && expected_generation != s.claim_generation)
        return 0;
    if (expected_generation &&
        (expected_scope_generation != s.context.scope_generation ||
         summary.key.session_epoch != s.context.session_epoch ||
         summary.key.renderer_epoch != s.context.renderer_epoch ||
         !(summary.scope == s.context.scope))) {
        s.BoundaryEvent(s.frame, expected_scope_generation !=
                                             s.context.scope_generation ||
                                         summary.key.session_epoch !=
                                             s.context.session_epoch ||
                                         !(summary.scope == s.context.scope) ?
                                     CaptureEventType::TitleBoundary :
                                     CaptureEventType::ResetBoundary);
        s.CancelPending("Capture context changed before completion");
        return 0;
    }
    if (s.settings.mode != CaptureSessionMode::RollingAnimation &&
        !s.have_boundary)
        return 0;
    if (summary.key.session_epoch != s.context.session_epoch ||
        summary.key.renderer_epoch != s.context.renderer_epoch ||
        !(summary.scope == s.context.scope) ||
        summary.shader_count > kCapturedShaderSlots)
        return 0;
    if (s.settings.mode == CaptureSessionMode::LiveDrawInputs) {
        if (type != CaptureEventType::Draw)
            return 0;
        if (!s.settings.live_stage_sets.empty() &&
            std::none_of(
                s.settings.live_stage_sets.begin(),
                s.settings.live_stage_sets.end(), [&](const auto &set) {
                    if (set.size() != summary.shader_count)
                        return false;
                    return std::all_of(
                        set.begin(), set.end(), [&](const auto &key) {
                            return std::find(summary.shaders.begin(),
                                             summary.shaders.begin() +
                                                 summary.shader_count,
                                             key) != summary.shaders.begin() +
                                                         summary.shader_count;
                        });
                }))
            return 0;
        limitations |= CaptureMissingDependencies;
    }
    // Animation acquisition can begin in the middle of a guest frame. The
    // first owner-side event establishes that frame before a manual mark;
    // the UI's earlier frame snapshot may be stale.
    if (!s.have_boundary &&
        s.settings.mode == CaptureSessionMode::RollingAnimation)
        s.frame = summary.key.frame;
    if (!s.Admit(
            0, 1,
            "Event budget exceeded; acquisition stopped before this event")) {
        return 0;
    }
    auto record = std::make_shared<Impl::Record>();
    record->account = s.account;
    size_t charge = sizeof(Impl::Record) + sizeof(void *) * 8 +
                    summary.resources.size() * sizeof(ResourceTouch) +
                    summary.segments.size() * sizeof(DrawSegmentSummary);
    for (const auto &segment : summary.segments)
        charge += segment.primitive_indices.size() * sizeof(uint32_t);
    if (!s.Charge(*record, charge))
        return 0;
    auto &event = record->event;
    event.event_id = next_event_id.fetch_add(1);
    if (!event.event_id || event.event_id >= kCaptureSessionTokenBit) {
        s.state = CaptureSessionState::Failed;
        s.active.store(false);
        s.reason = "Capture token namespace exhausted";
        return 0;
    }
    event.summary = summary;
    event.type = type;
    event.limitations = limitations;
    event.host_timestamp_ns =
        uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(
                     std::chrono::steady_clock::now().time_since_epoch())
                     .count());
    event.observed_checkpoint =
        s.settings.mode != CaptureSessionMode::LiveDrawInputs &&
        type == CaptureEventType::Draw &&
        (s.settings.maximum_evidence || !s.checkpoint_taken);
    if (type == CaptureEventType::Draw)
        s.checkpoint_taken = true;
    const uint64_t token = kCaptureSessionTokenBit | event.event_id;
    s.pending.emplace(token, record);
    s.events.push_back(record);
    ++s.total_events;
    s.by_id.emplace(event.event_id, record);
    ++s.revision;
    for (size_t i = 0; i < summary.shader_count; ++i)
        s.uses[summary.shaders[i]].push_back(event.event_id);
    return token;
}
uint64_t CaptureSession::BeginBatch(uint64_t expected_generation)
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    auto &s = *impl_;
    if (!s.active.load() ||
        (expected_generation && expected_generation != s.claim_generation))
        return 0;
    if (s.batches.size() >= s.EventLimit())
        return 0;
    const uint64_t charge = sizeof(Impl::Batch) + 256;
    if (!s.Admit(charge, 0, "Batch ownership exceeds CPU budget")) {
        return 0;
    }
    auto batch = std::make_shared<Impl::Batch>();
    batch->account = s.account;
    batch->generation = s.claim_generation;
    if (s.next_archive_batch >= kCaptureSessionTokenBit - 1)
        return 0;
    batch->archive_id = ++s.next_archive_batch;
    const uint64_t id = NewBatchId();
    if (!id || id >= kCaptureSessionTokenBit)
        return 0;
    s.batches.emplace(id, batch);
    batch->charge = charge;
    s.account->bytes.fetch_add(charge);
    ++s.revision;
    return id;
}
bool CaptureSession::BatchCurrent(uint64_t batch) const
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    auto found = impl_->batches.find(batch);
    return found != impl_->batches.end() &&
           found->second->generation == impl_->claim_generation;
}
bool CaptureSession::HoldForBatch(uint64_t batch, uint64_t token)
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    auto &s = *impl_;
    auto found = s.batches.find(batch);
    auto *record = s.Lookup(token);
    if (found == s.batches.end() ||
        found->second->generation != s.claim_generation ||
        found->second->submitted || !record || record->event.finished ||
        record->event.batch_id)
        return false;
    const uint64_t charge = 2 * sizeof(uint64_t);
    if (found->second->members.size() >= s.EventLimit()) {
        s.Exhaust("Batch member metadata exceeds CPU budget", record);
        return false;
    }
    if (!s.Admit(charge, 0, "Batch member metadata exceeds CPU budget", record))
        return false;
    found->second->charge += charge;
    s.account->bytes.fetch_add(charge);
    found->second->members.push_back(token);
    record->batch_handle = batch;
    record->event.batch_id = found->second->archive_id;
    record->event.resource_finalized = false;
    record->event.submission = CaptureBatchOutcome::Pending;
    record->event.completion = CaptureBatchCompletion::Pending;
    ++s.revision;
    return true;
}
bool CaptureSession::DescribeCommand(
    uint64_t token, const CaptureCommandDescription &description)
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    auto *record = impl_->Lookup(token);
    if (!record || record->event.finished ||
        description.kind > CaptureCommandKind::Checkpoint ||
        description.source_offset > UINT64_MAX - description.bytes ||
        description.destination_offset > UINT64_MAX - description.bytes)
        return false;
    record->event.command = description;
    ++impl_->revision;
    return true;
}
bool CaptureSession::DescribeReplay(
    uint64_t token, const XemuShaderCaptureReplayDescription &description)
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    auto *record = impl_->Lookup(token);
    if (!record || record->event.finished || record->event.replay_description ||
        description.binding_count > XEMU_SHADER_CAPTURE_REPLAY_MAX_BINDINGS)
        return false;
    const uint64_t charge = sizeof(CaptureReplayDescription) + 128 +
                            2 * uint64_t(description.binding_count) *
                                sizeof(XemuShaderCaptureReplayBinding);
    if (!impl_->Charge(*record, charge))
        return false;
    auto owned = std::make_unique<CaptureReplayDescription>();
    owned->kind = description.kind;
    owned->bindings.assign(description.bindings,
                           description.bindings + description.binding_count);
    owned->source_x = description.source_x;
    owned->source_y = description.source_y;
    owned->destination_x = description.destination_x;
    owned->destination_y = description.destination_y;
    owned->width = description.width;
    owned->height = description.height;
    owned->source_offset = description.source_offset;
    owned->destination_offset = description.destination_offset;
    owned->bytes = description.bytes;
    std::copy_n(description.clear_color_bits, 4, owned->clear_color_bits);
    owned->clear_color_mask = description.clear_color_mask;
    if (!ValidateCaptureReplayDescription(*owned, nullptr)) {
        impl_->account->bytes.fetch_sub(charge);
        record->charge -= charge;
        return false;
    }
    auto account = impl_->account;
    record->charge -= charge;
    record->event.replay_description = {
        owned.release(),
        [account, charge](const CaptureReplayDescription *value) {
            delete value;
            account->bytes.fetch_sub(charge);
        }
    };
    ++impl_->revision;
    return true;
}
bool CaptureSession::RecordBatchCommand(uint64_t batch, uint64_t token,
                                        CaptureCommandPhase phase,
                                        uint64_t ordinal)
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    auto &s = *impl_;
    auto found = s.batches.find(batch);
    auto *record = s.Lookup(token);
    if (found == s.batches.end() ||
        found->second->generation != s.claim_generation ||
        found->second->submitted || !record || record->batch_handle != batch ||
        record->event.command_recorded || !ordinal ||
        phase > CaptureCommandPhase::Main)
        return false;
    for (const auto member : found->second->members) {
        const auto *prior = s.Lookup(member);
        if (prior && prior->event.command_recorded &&
            prior->event.command_phase == phase &&
            prior->event.command_ordinal == ordinal)
            return false;
    }
    record->event.command_recorded = true;
    record->event.command_phase = phase;
    record->event.command_ordinal = ordinal;
    ++s.revision;
    return true;
}
bool CaptureSession::SubmitBatch(uint64_t batch, bool accepted,
                                 uint64_t queue_ordinal, int32_t backend_result)
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    auto &s = *impl_;
    auto found = s.batches.find(batch);
    if (found == s.batches.end() ||
        found->second->generation != s.claim_generation ||
        found->second->submitted)
        return false;
    if (accepted && (!queue_ordinal || queue_ordinal <= s.last_queue_ordinal))
        return false;
    auto owned_batch = found->second;
    owned_batch->submitted = true;
    if (accepted)
        s.last_queue_ordinal = queue_ordinal;
    // Preserve the backend receipt before any allocating graph work. Even an
    // allocation-free abort after unwinding must retain known submission.
    for (const auto token : owned_batch->members) {
        auto *record = s.Lookup(token);
        if (!record)
            continue;
        const bool host =
            record->event.command_recorded &&
            record->event.command_phase == CaptureCommandPhase::HostPreparation;
        const bool gpu = record->event.command_recorded && !host &&
                         (record->event.type != CaptureEventType::Draw ||
                          record->event.emitted);
        record->event.submission =
            host || !gpu ? CaptureBatchOutcome::None :
            accepted     ? CaptureBatchOutcome::Submitted :
                           CaptureBatchOutcome::SubmissionFailed;
        record->event.completion = gpu && accepted ?
                                       CaptureBatchCompletion::Pending :
                                       CaptureBatchCompletion::None;
        record->event.backend_result = backend_result;
        record->event.queue_ordinal = accepted ? queue_ordinal : 0;
    }
    const bool committed = s.CommitBatch(*owned_batch, accepted);
    if (!committed && s.state != CaptureSessionState::BudgetExceeded)
        s.Exhaust("Batch resource publication incomplete");
    for (const auto token : owned_batch->members) {
        auto *record = s.Lookup(token);
        if (!record)
            continue;
        auto &event = record->event;
        const bool host =
            event.command_recorded &&
            event.command_phase == CaptureCommandPhase::HostPreparation;
        const bool gpu =
            event.command_recorded && !host &&
            (event.type != CaptureEventType::Draw || event.emitted);
        event.submission = host || !gpu ? CaptureBatchOutcome::None :
                           accepted     ? CaptureBatchOutcome::Submitted :
                                          CaptureBatchOutcome::SubmissionFailed;
        event.completion = gpu && accepted ? CaptureBatchCompletion::Pending :
                                             CaptureBatchCompletion::None;
        event.backend_result = backend_result;
        event.queue_ordinal = accepted ? queue_ordinal : 0;
        event.resource_finalized = true;
        if (!committed) {
            event.limitations |=
                CaptureMissingDependencies | CaptureReadbackFailed;
            event.resource_evidence.reset();
        }
        record->resource_operations.clear();
        s.Publish(token, *record);
    }
    if (!accepted)
        s.batches.erase(batch);
    ++s.revision;
    return committed;
}
bool CaptureSession::RetireBatch(uint64_t batch, bool completed,
                                 int32_t backend_result)
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    auto &s = *impl_;
    auto found = s.batches.find(batch);
    if (found == s.batches.end() ||
        found->second->generation != s.claim_generation ||
        !found->second->submitted)
        return false;
    auto owned_batch = found->second;
    for (const auto token : owned_batch->members) {
        auto *record = s.Lookup(token);
        if (!record ||
            record->event.completion != CaptureBatchCompletion::Pending)
            continue;
        record->event.completion = completed ?
                                       CaptureBatchCompletion::Completed :
                                       CaptureBatchCompletion::Failed;
        record->event.completion_result = backend_result;
        if (!completed)
            record->event.limitations |= CaptureReadbackFailed;
        s.Publish(token, *record);
    }
    s.batches.erase(batch);
    ++s.revision;
    return true;
}
bool CaptureSession::AbortBatch(uint64_t batch, CaptureBatchOutcome outcome,
                                int32_t backend_result)
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    auto &s = *impl_;
    auto found = s.batches.find(batch);
    if (found == s.batches.end() ||
        found->second->generation != s.claim_generation ||
        found->second->submitted ||
        (outcome != CaptureBatchOutcome::Aborted &&
         outcome != CaptureBatchOutcome::Detached))
        return false;
    auto owned_batch = found->second;
    for (const auto token : owned_batch->members) {
        if (auto *record = s.Lookup(token)) {
            record->event.submission = outcome;
            record->event.backend_result = backend_result;
        }
    }
    const bool committed = s.CommitBatch(*owned_batch, false);
    for (const auto token : owned_batch->members) {
        auto *record = s.Lookup(token);
        if (!record)
            continue;
        record->event.submission = outcome;
        record->event.completion = CaptureBatchCompletion::None;
        record->event.backend_result = backend_result;
        record->event.resource_finalized = true;
        record->event.limitations |= CaptureInvalidated;
        if (!committed)
            record->event.limitations |= CaptureMissingDependencies;
        record->event.finished = record->event.inputs.complete = true;
        record->resource_operations.clear();
        s.Publish(token, *record);
    }
    s.batches.erase(batch);
    ++s.revision;
    return committed;
}
bool CaptureSession::WantsInputs(uint64_t token) const
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->Lookup(token) != nullptr;
}
bool CaptureSession::WantsImages(uint64_t token) const
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    auto *r = impl_->Lookup(token);
    return r && r->event.observed_checkpoint;
}
bool CaptureSession::HasGeometry(uint64_t token) const
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    auto *r = impl_->Lookup(token);
    return r && r->event.geometry.positions && r->event.geometry.indices;
}
bool CaptureSession::ReadbackPressure(uint64_t headroom) const
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->active.load() && !impl_->Fits(headroom);
}
bool CaptureSession::ReservePayload(uint64_t token, size_t bytes)
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    auto *r = impl_->Lookup(token);
    if (!r)
        return false;
    auto &s = *impl_;
    if (bytes > s.settings.per_event_byte_budget ||
        r->event.payload_bytes + r->reserved >
            s.settings.per_event_byte_budget - bytes) {
        s.Exhaust("In-flight readback byte budget exceeded", r);
        return false;
    }
    if (!s.Admit(bytes, 0, "In-flight readback byte budget exceeded", r))
        return false;
    s.account->bytes.fetch_add(bytes);
    r->reserved += bytes;
    ++s.revision;
    return true;
}
bool CaptureSession::StageGeometry(uint64_t token,
                                   const OwnedDrawGeometry &geometry)
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    auto *r = impl_->Lookup(token);
    if (!r)
        return false;
    if (!impl_->Store(*r, r->event.geometry.positions,
                      geometry.positions.data(),
                      geometry.positions.size() * sizeof(std::array<float, 4>)))
        return false;
    r->event.geometry.position_count = geometry.positions.size();
    if (!impl_->Store(*r, r->event.geometry.indices, geometry.indices.data(),
                      geometry.indices.size() * sizeof(uint32_t)))
        return false;
    r->event.geometry.index_count = geometry.indices.size();
    for (const auto &position : geometry.positions)
        for (float value : position)
            if (!std::isfinite(value))
                r->event.limitations |= CaptureMalformed;
    for (uint32_t index : geometry.indices)
        if (index >= geometry.positions.size())
            r->event.limitations |= CaptureMalformed;
    return true;
}
bool CaptureSession::StageImage(uint64_t token, bool before,
                                const XemuShaderDrawImage &image)
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    auto *r = impl_->Lookup(token);
    if (!r)
        return false;
    auto &out = before ? r->event.inputs.before : r->event.inputs.after;
    if (!impl_->Store(*r, out.rgba, image.rgba, image.byte_count))
        return false;
    out.width = image.width;
    out.height = image.height;
    if (uint64_t(image.width) * image.height * 4 != image.byte_count)
        r->event.limitations |= CaptureMalformed;
    return true;
}
bool CaptureSession::StageTexture(uint64_t token,
                                  const XemuShaderDrawTexture &texture)
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    auto *r = impl_->Lookup(token);
    if (!r)
        return false;
    if (texture.slot >= 4) {
        r->event.limitations |= CaptureUnsupported;
        return false;
    }
    auto &out = r->event.inputs.textures[texture.slot];
    out.described = true;
    out.metadata = texture;
    out.metadata.image = {};
    if (texture.image.byte_count) {
        auto found = std::find_if(
            out.images.begin(), out.images.end(), [&](const auto &item) {
                return item.mip_level == texture.mip_level &&
                       item.face == texture.face;
            });
        if (found == out.images.end()) {
            if (out.images.size() >= 96 ||
                !impl_->Charge(*r, sizeof(CaptureOwnedTextureImage)))
                return false;
            out.images.push_back({ texture.mip_level, texture.face, {} });
            found = std::prev(out.images.end());
        }
        if (!impl_->Store(*r, found->image.rgba, texture.image.rgba,
                          texture.image.byte_count))
            return false;
        found->image.width = texture.image.width;
        found->image.height = texture.image.height;
        if (uint64_t(texture.image.width) * texture.image.height * 4 !=
            texture.image.byte_count)
            r->event.limitations |= CaptureMalformed;
    }
    return true;
}
bool CaptureSession::StageUniform(uint64_t token,
                                  const XemuShaderDrawUniform &uniform)
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    auto *r = impl_->Lookup(token);
    if (!r)
        return false;
    const size_t length = NameLength(uniform.name);
    if (!length || r->event.inputs.uniforms.size() >= 256) {
        r->event.limitations |= CaptureMalformed;
        return false;
    }
    if (!impl_->Charge(*r, sizeof(CaptureOwnedUniform) + length))
        return false;
    CaptureOwnedUniform out{ uniform.stage,
                             uniform.type,
                             uniform.components,
                             uniform.count,
                             std::string(uniform.name, length),
                             {} };
    if (!impl_->Store(*r, out.data, uniform.data, uniform.byte_count))
        return false;
    r->event.inputs.uniforms.push_back(std::move(out));
    return true;
}
bool CaptureSession::StageSource(uint64_t token, uint32_t stage,
                                 const char *source, size_t bytes)
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    auto *r = impl_->Lookup(token);
    if (!r || stage >= r->event.inputs.sources.size())
        return false;
    return impl_->Store(*r, r->event.inputs.sources[stage], source, bytes);
}
bool CaptureSession::StageRegister(uint64_t token, const char *name,
                                   uint32_t value)
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    auto *r = impl_->Lookup(token);
    const size_t length = NameLength(name);
    if (!r || !length || r->event.inputs.registers.size() >= 4096)
        return false;
    if (!impl_->Charge(*r, sizeof(OwnedDrawRegister) + length))
        return false;
    r->event.inputs.registers.push_back({ std::string(name, length), value });
    return true;
}
bool CaptureSession::StageBlob(uint64_t token, const XemuShaderDrawBlob &blob)
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    auto *r = impl_->Lookup(token);
    if (!r)
        return false;
    const size_t length = NameLength(blob.name);
    if (!length || blob.byte_count > kMaximumBlob ||
        r->event.inputs.blobs.size() >= 128) {
        impl_->Exhaust("Raw evidence block or descriptor budget exceeded", r);
        return false;
    }
    if (!impl_->Charge(*r, sizeof(CaptureOwnedBlob) + length))
        return false;
    CaptureOwnedBlob out;
    out.name.assign(blob.name, length);
    out.slot = blob.slot;
    out.format = blob.format;
    out.components = blob.components;
    out.stride = blob.stride;
    out.count = blob.count;
    out.offset = blob.offset;
    out.normalized = blob.normalized;
    out.integer = blob.integer;
    if (!impl_->Store(*r, out.data, blob.data, blob.byte_count))
        return false;
    r->event.inputs.blobs.push_back(std::move(out));
    return true;
}
bool CaptureSession::StageResource(uint64_t token,
                                   const XemuShaderCaptureResource &resource,
                                   const char *snapshot_blob)
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    auto *record = impl_->Lookup(token);
    if (!record || record->event.finished)
        return false;
    if (record->resource_operations.size() >= 64) {
        impl_->Exhaust("Per-event resource descriptor budget exceeded", record);
        return false;
    }
    if (!impl_->Charge(*record, 2 * sizeof(Impl::Record::ResourceInput)))
        return false;
    SharedCaptureBlock snapshot;
    if (snapshot_blob) {
        for (const auto &blob : record->event.inputs.blobs)
            if (blob.name == snapshot_blob) {
                snapshot = blob.data;
                break;
            }
        if (!snapshot || snapshot->bytes.size() != resource.size ||
            (resource.access == XEMU_SHADER_CAPTURE_RESOURCE_FULL_WRITE &&
             (resource.offset || resource.size != resource.byte_size)) ||
            (resource.flags & XEMU_SHADER_CAPTURE_RESOURCE_OPAQUE_EXTENT)) {
            snapshot.reset();
            record->event.limitations |=
                CaptureMalformed | CaptureMissingDependencies;
            record->event.failure =
                "Resource snapshot blob is missing or its extent is invalid";
            XemuShaderCaptureResource gap = resource;
            gap.owner = 0;
            gap.access = XEMU_SHADER_CAPTURE_RESOURCE_READ;
            record->resource_operations.push_back({ gap, {} });
        }
    }
    record->resource_operations.push_back({ resource, snapshot });
    ++impl_->revision;
    return true;
}
bool CaptureSession::AttachResourceReadSnapshot(uint64_t token, uint32_t kind,
                                                uint32_t slot,
                                                const char *blob_name)
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    auto *record = impl_->Lookup(token);
    if (!record || !record->event.resource_evidence || !blob_name)
        return false;
    const CaptureOwnedBlob *blob = nullptr;
    for (const auto &input : record->event.inputs.blobs)
        if (input.name == blob_name) {
            blob = &input;
            break;
        }
    if (!blob || !blob->data)
        return false;
    bool attached = false;
    uint64_t charge = sizeof(CaptureResourceEvent) + 128;
    for (const auto &access : record->event.resource_evidence->accesses)
        charge += sizeof(access) * 2 + 32 +
                  (access.reads.size() + access.writes.size()) *
                      sizeof(CaptureResourceVersion) * 2 +
                  access.aliases.size() * sizeof(CaptureResourceAlias) * 2;
    if (!impl_->Admit(charge, 0,
                      "Resource readback metadata exceeds recorder budget",
                      record)) {
        return false;
    }
    auto owned = std::make_unique<CaptureResourceEvent>(
        *record->event.resource_evidence);
    for (auto &access : owned->accesses) {
        if (access.kind != kind || access.slot != slot ||
            access.type != CaptureResourceOperationType::Read ||
            access.range.offset != blob->offset ||
            access.range.size != blob->data->bytes.size())
            continue;
        for (auto &read : access.reads) {
            read.snapshot_block = blob->data->id;
            read.snapshot_range = { blob->offset, blob->data->bytes.size() };
            read.content_digest = blob->data->digest;
            attached = true;
        }
    }
    if (!attached)
        return false;
    impl_->account->bytes.fetch_add(charge);
    auto account = impl_->account;
    record->event.resource_evidence = SharedCaptureResourceEvent(
        owned.release(), [account, charge](const CaptureResourceEvent *event) {
            delete event;
            account->bytes.fetch_sub(charge);
        });
    ++impl_->revision;
    return true;
}
void CaptureSession::ReleaseResource(uint64_t owner, uint64_t byte_size,
                                     uint32_t kind, uint32_t flags,
                                     uint64_t expected_generation)
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    auto &s = *impl_;
    if (!owner || !s.active.load() ||
        s.settings.mode == CaptureSessionMode::LiveDrawInputs ||
        (expected_generation && expected_generation != s.claim_generation) ||
        (s.settings.mode == CaptureSessionMode::NextFrame && !s.have_boundary))
        return;
    const size_t previous = s.events.size();
    s.BoundaryEvent(s.frame, CaptureEventType::AllocationBoundary);
    if (s.events.size() == previous)
        return;
    auto &record = *s.events.back();
    if (!s.Charge(record, 2 * sizeof(Impl::Record::ResourceInput)))
        return;
    record.resource_operations.push_back(
        { { owner, byte_size, 0, byte_size,
            XEMU_SHADER_CAPTURE_RESOURCE_RELEASE, kind, 0, flags },
          {} });
    s.CommitResources(record, false);
}
bool CaptureSession::Finish(uint64_t token, bool emitted, uint32_t primitive,
                            uint32_t vertices, uint32_t indices)
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    auto *r = impl_->Lookup(token);
    if (!r || r->event.finished)
        return false;
    if (!r->event.batch_id ||
        (r->event.command_recorded &&
         r->event.command_phase == CaptureCommandPhase::HostPreparation)) {
        impl_->CommitResources(*r, emitted);
        if (r->event.batch_id) {
            r->event.resource_finalized = true;
            r->event.submission = CaptureBatchOutcome::None;
            r->event.completion = CaptureBatchCompletion::None;
        }
    }
    r->event.finished = true;
    r->event.emitted = emitted;
    if (!emitted)
        r->event.summary.key.submission = 0;
    r->event.summary.primitive_mode = primitive;
    r->event.summary.vertex_count = vertices;
    r->event.summary.index_count = indices;
    if (!emitted && r->event.type == CaptureEventType::Draw) {
        uint32_t outcome =
            1; // Older instrumentation supplied no outcome register.
        for (auto it = r->event.inputs.registers.rbegin();
             it != r->event.inputs.registers.rend(); ++it) {
            if (it->name == "capture.outcome") {
                outcome = it->value;
                break;
            }
        }
        if (outcome == 1)
            r->event.limitations |= CaptureSuppressed;
        else if (outcome == 2)
            r->event.limitations |= CaptureUnsupported;
        else
            r->event.limitations |= CaptureMalformed;
    }
    if (r->event.geometry.positions && r->event.geometry.indices)
        r->event.summary.completeness = CaptureCompleteness::GeometrySnapshot;
    ++impl_->revision;
    impl_->Publish(token, *r);
    return true;
}
bool CaptureSession::InputsComplete(uint64_t token)
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    auto *r = impl_->Lookup(token);
    if (!r)
        return false;
    r->event.inputs.complete = true;
    impl_->Publish(token, *r);
    return true;
}
void CaptureSession::Fail(uint64_t token, const std::string &reason)
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    auto *r = impl_->Lookup(token);
    if (!r)
        return;
    r->event.failure = reason.substr(0, 1024);
    r->event.limitations |= CaptureReadbackFailed;
    r->event.inputs.complete = true;
    ++impl_->revision;
    impl_->Publish(token, *r);
}
void CaptureSession::BudgetExceeded(uint64_t token, const std::string &reason)
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    auto *record = impl_->Lookup(token);
    if (token && !record)
        return;
    impl_->Exhaust(reason.substr(0, 1024), record);
}
bool CaptureSession::SnapshotCompletedLiveFrame(
    uint64_t after_frame, CaptureSessionSnapshot *out,
    uint64_t expected_generation) const
{
    if (!out)
        return false;
    std::lock_guard<std::mutex> lock(impl_->mutex);
    const auto &s = *impl_;
    if (!expected_generation || expected_generation != s.claim_generation ||
        !s.settings.live_continuous || !s.have_boundary)
        return false;
    std::map<uint64_t, bool> frames;
    for (const auto &record : s.events) {
        const auto &event = record->event;
        const auto frame = event.summary.key.frame;
        if (frame <= after_frame || frame >= s.frame)
            continue;
        if (event.type == CaptureEventType::FrameBoundary)
            frames.emplace(frame, true);
        if (event.pending || !event.finished || !event.inputs.complete ||
            !event.resource_finalized)
            frames[frame] = false;
    }
    const auto ready =
        std::find_if(frames.rbegin(), frames.rend(),
                     [](const auto &frame) { return frame.second; });
    if (ready == frames.rend())
        return false;
    CaptureSessionSnapshot snapshot;
    snapshot.state = CaptureSessionState::Ready;
    snapshot.frame_window_complete = true;
    snapshot.execution_order_complete = false;
    snapshot.settings = s.settings;
    snapshot.context = s.context;
    snapshot.cpu_bytes = s.UsedBytes();
    snapshot.resource_domain = s.resources ? s.resources->DomainId() : 0;
    snapshot.first_frame = snapshot.last_frame = ready->first;
    snapshot.has_frame_range = true;
    snapshot.retained_frames = { ready->first };
    for (const auto &record : s.events)
        if (record->event.summary.key.frame == ready->first)
            snapshot.events.push_back(Impl::SnapshotRecord(record));
    snapshot.total_events = snapshot.events.size();
    *out = std::move(snapshot);
    return true;
}
CaptureSessionSnapshot CaptureSession::Snapshot() const
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    const auto &s = *impl_;
    CaptureSessionSnapshot snapshot;
    snapshot.state = s.state;
    snapshot.frame_window_complete = s.frame_window_complete;
    snapshot.settings = s.settings;
    snapshot.execution_order_complete =
        s.settings.mode != CaptureSessionMode::LiveDrawInputs;
    snapshot.context = s.context;
    snapshot.reason = s.reason;
    snapshot.trigger_frame = s.trigger;
    snapshot.cpu_bytes = s.account->bytes.load() +
                         (s.resources ? s.resources->AccountedBytes() : 0);
    snapshot.resource_domain = s.resources ? s.resources->DomainId() : 0;
    snapshot.pending_events = s.pending.size();
    snapshot.total_events = s.total_events;
    for (const auto &record : s.events) {
        snapshot.reserved_bytes += record->reserved;
        const auto &event = record->event;
        if (!snapshot.has_frame_range) {
            snapshot.first_frame = snapshot.last_frame =
                event.summary.key.frame;
            snapshot.has_frame_range = true;
        } else {
            snapshot.first_frame =
                std::min(snapshot.first_frame, event.summary.key.frame);
            snapshot.last_frame =
                std::max(snapshot.last_frame, event.summary.key.frame);
        }
        snapshot.events.push_back(Impl::SnapshotRecord(record));
    }
    for (const auto &bucket : s.pool)
        for (const auto &block : bucket.second)
            if (!block.expired())
                ++snapshot.unique_blocks;
    snapshot.retained_frames = RetainedFrames(snapshot.events);
    return snapshot;
}
std::vector<uint64_t> CaptureSession::Uses(const ShaderKey &shader,
                                           uint64_t first, uint64_t last) const
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    std::vector<uint64_t> result;
    auto found = impl_->uses.find(shader);
    if (found == impl_->uses.end())
        return result;
    for (uint64_t id : found->second) {
        auto record = impl_->by_id.find(id);
        if (record != impl_->by_id.end() &&
            record->second->event.summary.key.frame >= first &&
            record->second->event.summary.key.frame <= last)
            result.push_back(id);
    }
    return result;
}
std::shared_ptr<const CaptureOccurrence> CaptureSession::Find(uint64_t id) const
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    auto found = impl_->by_id.find(id);
    return found == impl_->by_id.end() ?
               std::shared_ptr<const CaptureOccurrence>{} :
               Impl::SnapshotRecord(found->second);
}

namespace {
using Json = nlohmann::json;
uint32_t FloatBits(float value)
{
    uint32_t bits;
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}
float BitsFloat(uint32_t bits)
{
    float value;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}
Json ScopeJson(const ShaderScope &scope)
{
    return Json::array({ scope.title_id, scope.executable_fingerprint_version,
                         scope.executable_fingerprint });
}
Json KeyJson(const DrawEventKey &key)
{
    return Json::array({ key.session_epoch, key.renderer_epoch, key.frame,
                         key.draw, key.submission });
}
Json RangeJson(const AddressRange &range)
{
    return Json::array({ range.address, range.length });
}
Json SummaryJson(const DrawCaptureSummary &summary)
{
    Json out{ { "key", KeyJson(summary.key) },
              { "scope", ScopeJson(summary.scope) },
              { "domain", summary.domain },
              { "completeness", summary.completeness },
              { "primitive", summary.primitive_mode },
              { "vertices", summary.vertex_count },
              { "indices", summary.index_count },
              { "primitives", summary.primitive_count },
              { "segmentation_complete", summary.segmentation_complete },
              { "batched_geometry_suspected",
                summary.batched_geometry_suspected },
              { "shaders", Json::array() },
              { "resources", Json::array() },
              { "segments", Json::array() } };
    for (size_t i = 0; i < summary.shader_count; ++i) {
        const auto &shader = summary.shaders[i];
        out["shaders"].push_back(Json::array(
            { shader.hash.version, shader.hash.bytes, shader.stage }));
    }
    for (const auto &touch : summary.resources) {
        const auto &r = touch.resource;
        out["resources"].push_back(
            { { "kind", r.kind },
              { "guest", RangeJson(r.guest) },
              { "storage_id", r.storage_id },
              { "storage_range", RangeJson(r.storage_range) },
              { "descriptor", r.descriptor_digest },
              { "content", r.content_digest },
              { "slot", touch.slot },
              { "access", touch.access },
              { "read_version", touch.read_version },
              { "write_version", touch.write_version } });
    }
    for (const auto &segment : summary.segments) {
        std::array<uint32_t, 3> minimum{}, maximum{};
        for (size_t i = 0; i < 3; ++i) {
            minimum[i] = FloatBits(segment.bounds.minimum[i]);
            maximum[i] = FloatBits(segment.bounds.maximum[i]);
        }
        out["segments"].push_back(
            { { "key", KeyJson(segment.key.draw) },
              { "segment", segment.key.segment },
              { "origin", segment.origin },
              { "first_primitive", segment.first_primitive },
              { "primitive_count", segment.primitive_count },
              { "first_index", segment.first_index },
              { "index_count", segment.index_count },
              { "vertex_span", RangeJson(segment.vertex_span) },
              { "index_span", RangeJson(segment.index_span) },
              { "geometry", segment.geometry_digest },
              { "transform", segment.transform_digest },
              { "skinning", segment.skinning_digest },
              { "minimum", minimum },
              { "maximum", maximum },
              { "bounds_valid", segment.bounds.valid },
              { "bounds_space", segment.bounds_space_digest },
              { "primitive_indices", segment.primitive_indices },
              { "confirmed_object", segment.confirmed_object_id } });
    }
    return out;
}
Json ResourceVersionJson(const CaptureResourceVersion &version)
{
    return Json::array({ version.domain_id, version.allocation_id,
                         version.version, version.reset_generation,
                         version.producer_event, version.producer_sequence,
                         version.producer_operation, version.range.offset,
                         version.range.size, version.provenance,
                         version.snapshot_block, version.snapshot_range.offset,
                         version.snapshot_range.size, version.content_digest });
}
Json ResourceEventJson(const SharedCaptureResourceEvent &event)
{
    if (!event)
        return nullptr;
    Json accesses = Json::array();
    for (const auto &access : event->accesses) {
        Json aliases = Json::array(), reads = Json::array(),
             writes = Json::array();
        for (const auto &alias : access.aliases)
            aliases.push_back(Json::array(
                { alias.allocation_id, alias.range.offset, alias.range.size }));
        for (const auto &read : access.reads)
            reads.push_back(ResourceVersionJson(read));
        for (const auto &write : access.writes)
            writes.push_back(ResourceVersionJson(write));
        accesses.push_back(
            Json::array({ access.type, access.allocation_id, access.view_id,
                          access.guest_address, access.range.offset,
                          access.range.size, aliases, reads, writes,
                          access.kind, access.slot, access.flags }));
    }
    return Json::array({ event->domain_id, event->event_id, event->sequence,
                         event->reset_generation, accesses });
}
Json ReplayDescriptionJson(const SharedCaptureReplayDescription &description)
{
    if (!description)
        return nullptr;
    if (!ValidateCaptureReplayDescription(*description, nullptr))
        throw std::runtime_error("Malformed logical replay description");
    Json bindings = Json::array();
    for (const auto &binding : description->bindings) {
        const auto &selector = binding.resource;
        const auto &image = binding.image;
        bindings.push_back(Json::array(
            { Json::array({ selector.kind, selector.slot, selector.write,
                            selector.ordinal }),
              binding.role, binding.slot, binding.checkpoint,
              std::string(binding.blob_name),
              Json::array(
                  { image.width, image.height, image.mip_level, image.layer,
                    image.mip_levels, image.layers, image.samples, image.format,
                    image.coordinate_origin,
                    Json::array(
                        { image.storage_to_rgba[0], image.storage_to_rgba[1],
                          image.storage_to_rgba[2], image.storage_to_rgba[3] }),
                    Json::array({ image.sample_swizzle[0],
                                  image.sample_swizzle[1],
                                  image.sample_swizzle[2],
                                  image.sample_swizzle[3] }) }) }));
    }
    return { { "version", 1 },
             { "command",
               Json::array({ description->kind, std::move(bindings),
                             description->source_x, description->source_y,
                             description->destination_x,
                             description->destination_y, description->width,
                             description->height, description->source_offset,
                             description->destination_offset,
                             description->bytes }) },
             { "clear", Json::array({ description->clear_color_bits[0],
                                      description->clear_color_bits[1],
                                      description->clear_color_bits[2],
                                      description->clear_color_bits[3],
                                      description->clear_color_mask }) } };
}
struct PackageWriter {
    std::map<uint64_t, SharedCaptureBlock> blocks;
    uint64_t Ref(const SharedCaptureBlock &block)
    {
        if (!block)
            return 0;
        auto found = blocks.find(block->id);
        if (found != blocks.end() && found->second != block &&
            (found->second->digest != block->digest ||
             found->second->bytes != block->bytes))
            throw std::runtime_error("Conflicting immutable block ID");
        blocks[block->id] = block;
        return block->id;
    }
    Json ImageJson(const CaptureOwnedImage &image)
    {
        return Json::array({ image.width, image.height, Ref(image.rgba) });
    }
    Json EventJson(const CaptureOccurrence &event)
    {
        const auto &inputs = event.inputs;
        Json out{ { "id", event.event_id },
                  { "type", event.type },
                  { "summary", SummaryJson(event.summary) },
                  { "limitations", event.limitations },
                  { "finished", event.finished },
                  { "emitted", event.emitted },
                  { "pending", event.pending },
                  { "checkpoint", event.observed_checkpoint },
                  { "host_timestamp_ns", event.host_timestamp_ns },
                  { "failure", event.failure },
                  { "payload_bytes", event.payload_bytes },
                  { "geometry", Json::array({ Ref(event.geometry.positions),
                                              Ref(event.geometry.indices),
                                              event.geometry.position_count,
                                              event.geometry.index_count }) },
                  { "complete", inputs.complete },
                  { "before", ImageJson(inputs.before) },
                  { "after", ImageJson(inputs.after) },
                  { "sources", Json::array() },
                  { "uniforms", Json::array() },
                  { "registers", Json::array() },
                  { "textures", Json::array() },
                  { "blobs", Json::array() } };
        out["resource_evidence"] = ResourceEventJson(event.resource_evidence);
        out["replay_description"] =
            ReplayDescriptionJson(event.replay_description);
        out["execution"] = Json::array(
            { event.batch_id, event.command_phase, event.command_ordinal,
              event.command_recorded, event.submission, event.completion,
              event.backend_result, event.completion_result,
              event.resource_finalized, event.queue_ordinal,
              Json::array({ event.command.kind, event.command.source_offset,
                            event.command.destination_offset,
                            event.command.bytes }) });
        for (const auto &source : inputs.sources)
            out["sources"].push_back(Ref(source));
        for (const auto &uniform : inputs.uniforms)
            out["uniforms"].push_back(Json::array(
                { uniform.stage, uniform.name, uniform.type, uniform.components,
                  uniform.count, Ref(uniform.data) }));
        for (const auto &reg : inputs.registers)
            out["registers"].push_back(Json::array({ reg.name, reg.value }));
        for (const auto &texture : inputs.textures) {
            const auto &t = texture.metadata;
            Json item{ { "described", texture.described },
                       { "metadata",
                         Json::array({ t.slot, uint32_t(t.bound),
                                       t.guest_format, t.host_format, t.width,
                                       t.height, t.depth, t.mip_levels,
                                       t.face_count, t.min_filter, t.mag_filter,
                                       t.wrap_s, t.wrap_t, t.wrap_r,
                                       FloatBits(t.coordinate_scale),
                                       t.mip_level, t.face }) },
                       { "images", Json::array() } };
            for (const auto &image : texture.images)
                item["images"].push_back(Json::array(
                    { image.mip_level, image.face, ImageJson(image.image) }));
            out["textures"].push_back(std::move(item));
        }
        for (const auto &blob : inputs.blobs)
            out["blobs"].push_back(Json::array(
                { blob.name, blob.slot, blob.format, blob.components,
                  blob.stride, blob.count, blob.offset, blob.normalized,
                  blob.integer, Ref(blob.data) }));
        return out;
    }
};
uint64_t U(const Json &value, uint64_t maximum = UINT64_MAX)
{
    if ((!value.is_number_unsigned() && !value.is_number_integer()) ||
        (value.is_number_integer() && !value.is_number_unsigned() &&
         value.get<int64_t>() < 0))
        throw std::runtime_error("Expected unsigned capture value");
    uint64_t out = value.get<uint64_t>();
    if (out > maximum)
        throw std::runtime_error("Capture value exceeds limit");
    return out;
}
uint32_t U32(const Json &value)
{
    return uint32_t(U(value, UINT32_MAX));
}
void Array(const Json &value, size_t maximum)
{
    if (!value.is_array() || value.size() > maximum)
        throw std::runtime_error("Capture array exceeds limit");
}
void ExactArray(const Json &value, size_t count)
{
    Array(value, count);
    if (value.size() != count)
        throw std::runtime_error("Invalid capture array length");
}
std::string Text(const Json &value, size_t maximum)
{
    if (!value.is_string())
        throw std::runtime_error("Expected capture string");
    auto out = value.get<std::string>();
    if (out.size() > maximum)
        throw std::runtime_error("Capture string exceeds limit");
    return out;
}
template <size_t N> std::array<uint8_t, N> ByteArray(const Json &value)
{
    ExactArray(value, N);
    std::array<uint8_t, N> result;
    for (size_t i = 0; i < N; ++i)
        result[i] = uint8_t(U(value[i], 255));
    return result;
}
ShaderScope ReadScope(const Json &value)
{
    ExactArray(value, 3);
    ShaderScope result;
    result.title_id = U32(value[0]);
    result.executable_fingerprint_version = U32(value[1]);
    result.executable_fingerprint =
        ByteArray<kExecutableFingerprintBytes>(value[2]);
    return result;
}
DrawEventKey ReadKey(const Json &value)
{
    ExactArray(value, 5);
    return { U(value[0]), U(value[1]), U(value[2]), U32(value[3]),
             U(value[4]) };
}
AddressRange ReadRange(const Json &value)
{
    ExactArray(value, 2);
    return { U(value[0]), U(value[1]) };
}
DrawCaptureSummary ReadSummary(const Json &value)
{
    DrawCaptureSummary out;
    out.key = ReadKey(value.at("key"));
    out.scope = ReadScope(value.at("scope"));
    out.domain =
        DrawDomain(U(value.at("domain"), uint64_t(DrawDomain::ScreenSpace)));
    out.completeness =
        CaptureCompleteness(U(value.at("completeness"),
                              uint64_t(CaptureCompleteness::CompleteReplay)));
    out.primitive_mode = U32(value.at("primitive"));
    out.vertex_count = U32(value.at("vertices"));
    out.index_count = U32(value.at("indices"));
    out.primitive_count = U32(value.at("primitives"));
    out.segmentation_complete = value.at("segmentation_complete").get<bool>();
    out.batched_geometry_suspected =
        value.at("batched_geometry_suspected").get<bool>();
    const auto &shaders = value.at("shaders");
    Array(shaders, kCapturedShaderSlots);
    out.shader_count = uint8_t(shaders.size());
    for (size_t i = 0; i < shaders.size(); ++i) {
        ExactArray(shaders[i], 3);
        out.shaders[i].hash.version = U32(shaders[i][0]);
        out.shaders[i].hash.bytes = ByteArray<kShaderHashBytes>(shaders[i][1]);
        out.shaders[i].stage =
            Stage(U(shaders[i][2], uint64_t(Stage::Unknown)));
    }
    const auto &resources = value.at("resources");
    Array(resources, kCaptureMaxResourcesPerDraw);
    for (const auto &item : resources) {
        ResourceTouch touch;
        auto &r = touch.resource;
        r.kind = ResourceKind(
            U(item.at("kind"), uint64_t(ResourceKind::DepthStencilTarget)));
        r.guest = ReadRange(item.at("guest"));
        r.storage_id = U(item.at("storage_id"));
        r.storage_range = ReadRange(item.at("storage_range"));
        r.descriptor_digest =
            ByteArray<kCaptureDigestBytes>(item.at("descriptor"));
        r.content_digest = ByteArray<kCaptureDigestBytes>(item.at("content"));
        touch.slot = U32(item.at("slot"));
        touch.access = ResourceAccess(
            U(item.at("access"), uint64_t(ResourceAccess::DerivedFrom)));
        touch.read_version = U(item.at("read_version"));
        touch.write_version = U(item.at("write_version"));
        out.resources.push_back(touch);
    }
    const auto &segments = value.at("segments");
    Array(segments, kCaptureMaxAnalysisSegments);
    for (const auto &item : segments) {
        DrawSegmentSummary segment;
        segment.key.draw = ReadKey(item.at("key"));
        segment.key.segment = U32(item.at("segment"));
        segment.origin = DrawSegmentOrigin(
            U(item.at("origin"), uint64_t(DrawSegmentOrigin::UserDefined)));
        segment.first_primitive = U32(item.at("first_primitive"));
        segment.primitive_count = U32(item.at("primitive_count"));
        segment.first_index = U32(item.at("first_index"));
        segment.index_count = U32(item.at("index_count"));
        segment.vertex_span = ReadRange(item.at("vertex_span"));
        segment.index_span = ReadRange(item.at("index_span"));
        segment.geometry_digest =
            ByteArray<kCaptureDigestBytes>(item.at("geometry"));
        segment.transform_digest =
            ByteArray<kCaptureDigestBytes>(item.at("transform"));
        segment.skinning_digest =
            ByteArray<kCaptureDigestBytes>(item.at("skinning"));
        segment.bounds_space_digest =
            ByteArray<kCaptureDigestBytes>(item.at("bounds_space"));
        ExactArray(item.at("minimum"), 3);
        ExactArray(item.at("maximum"), 3);
        for (size_t i = 0; i < 3; ++i) {
            segment.bounds.minimum[i] = BitsFloat(U32(item.at("minimum")[i]));
            segment.bounds.maximum[i] = BitsFloat(U32(item.at("maximum")[i]));
        }
        segment.bounds.valid = item.at("bounds_valid").get<bool>();
        Array(item.at("primitive_indices"), kCaptureMaxSegmentationIndices / 3);
        for (const auto &index : item.at("primitive_indices"))
            segment.primitive_indices.push_back(U32(index));
        segment.confirmed_object_id = U(item.at("confirmed_object"));
        out.segments.push_back(std::move(segment));
    }
    return out;
}
uint64_t ArchiveEventOwnedBytes(const CaptureOccurrence &event)
{
    uint64_t bytes = sizeof(event) + 128;
    const auto names = [&](const auto &values) {
        uint64_t size = values.capacity() * sizeof(values[0]);
        for (const auto &value : values)
            size += value.name.capacity() + 1;
        return size;
    };
    bytes += event.summary.resources.capacity() * sizeof(ResourceTouch);
    bytes += event.summary.segments.capacity() * sizeof(DrawSegmentSummary);
    for (const auto &segment : event.summary.segments)
        bytes += segment.primitive_indices.capacity() * sizeof(uint32_t);
    bytes += names(event.inputs.uniforms);
    bytes += names(event.inputs.registers);
    bytes += names(event.inputs.blobs);
    bytes += event.failure.capacity() + 1;
    for (const auto &texture : event.inputs.textures)
        bytes += texture.images.capacity() * sizeof(CaptureOwnedTextureImage);
    if (event.replay_description)
        bytes += sizeof(CaptureReplayDescription) + 128 +
                 2 * event.replay_description->bindings.capacity() *
                     sizeof(XemuShaderCaptureReplayBinding);
    if (event.resource_evidence) {
        const auto &evidence = *event.resource_evidence;
        bytes += sizeof(evidence) + 128;
        bytes += evidence.accesses.capacity() * sizeof(CaptureResourceAccess);
        for (const auto &access : evidence.accesses) {
            bytes += access.aliases.capacity() * sizeof(CaptureResourceAlias);
            bytes += (access.reads.capacity() + access.writes.capacity()) *
                     sizeof(CaptureResourceVersion);
        }
    }
    return bytes;
}

// Inspect JSON without building its DOM. Charge spare container capacity,
// node/control overhead and string buffers conservatively before materializing
// the page DOM. Owned event descriptors are charged separately at admission.
// This is an admission estimate, not a bound on allocator fragmentation/RSS.
class ArchiveJsonBudget : public nlohmann::json_sax<Json> {
public:
    explicit ArchiveJsonBudget(uint64_t limit, bool settings)
        : limit_(limit), settings_(settings)
    {
    }
    uint64_t used = 0, declared_budget = 0;
    bool null() override
    {
        return Node();
    }
    bool boolean(bool) override
    {
        return Node();
    }
    bool number_integer(number_integer_t value) override
    {
        if (value >= 0)
            Declared(uint64_t(value));
        return Node();
    }
    bool number_unsigned(number_unsigned_t value) override
    {
        Declared(value);
        return Node();
    }
    bool number_float(number_float_t, const string_t &) override
    {
        return Node();
    }
    bool string(string_t &value) override
    {
        return Node() && Add(value.size() * 2 + 32);
    }
    bool binary(binary_t &value) override
    {
        return Node() && Add(value.capacity());
    }
    bool start_object(size_t) override
    {
        if (settings_ && depth_ == 1 && settings_key_)
            settings_depth_ = 2;
        return Start();
    }
    bool key(string_t &value) override
    {
        if (settings_ && depth_ == 1)
            settings_key_ = value == "settings";
        if (settings_depth_ && depth_ == settings_depth_)
            cpu_key_ = value == "cpu_bytes";
        return Add(value.size() * 2 + 32);
    }
    bool end_object() override
    {
        if (depth_ == settings_depth_)
            settings_depth_ = 0;
        --depth_;
        return true;
    }
    bool start_array(size_t) override
    {
        return Start();
    }
    bool end_array() override
    {
        --depth_;
        return true;
    }
    bool parse_error(size_t, const std::string &,
                     const nlohmann::detail::exception &) override
    {
        return false;
    }

private:
    uint64_t limit_;
    size_t depth_ = 0, settings_depth_ = 0;
    bool settings_, settings_key_ = false, cpu_key_ = false;
    bool Add(uint64_t amount)
    {
        if (amount > limit_ - used)
            return false;
        used += amount;
        return true;
    }
    bool Node()
    {
        return Add(128);
    }
    bool Start()
    {
        return ++depth_ <= 64 && Node();
    }
    void Declared(uint64_t value)
    {
        if (settings_depth_ && depth_ == settings_depth_ && cpu_key_)
            declared_budget = value;
    }
};
struct ArchiveParsedJson {
    Json value;
    uint64_t charge = 0;
};
ArchiveParsedJson ParseArchiveJson(const std::vector<uint8_t> &bytes,
                                   uint64_t budget, bool settings = false)
{
    // Input storage, lexer buffers and parser stack temporaries remain
    // bounded even when scanning rejects before DOM construction.
    if (bytes.size() > budget / 5)
        throw std::runtime_error("Capture JSON parsing exceeds CPU budget");
    const uint64_t temporary = bytes.size() * 5;
    ArchiveJsonBudget scan(budget - temporary, settings);
    if (!Json::sax_parse(bytes, &scan))
        throw std::runtime_error("Invalid or over-budget capture JSON");
    const uint64_t charge = temporary + scan.used;
    if (settings && scan.declared_budget && charge > scan.declared_budget)
        throw std::runtime_error("Capture metadata parsing exceeds CPU budget");
    return { Json::parse(bytes), charge };
}

struct PackageReader {
    bool require_execution = false;
    std::map<uint64_t, SharedCaptureBlock> blocks;
    SharedCaptureBlock Ref(const Json &value)
    {
        uint64_t id = U(value);
        if (!id)
            return {};
        auto found = blocks.find(id);
        if (found == blocks.end())
            throw std::runtime_error("Missing immutable capture block");
        return found->second;
    }
    CaptureOwnedImage ReadImage(const Json &value)
    {
        ExactArray(value, 3);
        return { U32(value[0]), U32(value[1]), Ref(value[2]) };
    }
    CaptureResourceVersion ReadResourceVersion(const Json &value)
    {
        ExactArray(value, 14);
        CaptureResourceVersion version;
        version.domain_id = U(value[0]);
        version.allocation_id = U(value[1]);
        version.version = U(value[2]);
        version.reset_generation = U(value[3]);
        version.producer_event = U(value[4], kCaptureSessionTokenBit - 1);
        version.producer_sequence = U(value[5]);
        version.producer_operation = U32(value[6]);
        version.range = { U(value[7]), U(value[8]) };
        version.provenance = CaptureResourceProvenance(
            U(value[9], uint64_t(CaptureResourceProvenance::UnknownCoverage)));
        version.snapshot_block = U(value[10]);
        version.snapshot_range = { U(value[11]), U(value[12]) };
        version.content_digest = ByteArray<16>(value[13]);
        if (version.snapshot_block) {
            const auto block = Ref(value[10]);
            if (!version.snapshot_range.size ||
                version.snapshot_range.size != block->bytes.size() ||
                version.content_digest != block->digest ||
                version.snapshot_range.offset >
                    UINT64_MAX - version.snapshot_range.size ||
                version.range.offset < version.snapshot_range.offset ||
                version.range.offset - version.snapshot_range.offset >
                    version.snapshot_range.size ||
                version.range.size >
                    version.snapshot_range.size -
                        (version.range.offset - version.snapshot_range.offset))
                throw std::runtime_error(
                    "Invalid resource snapshot ownership or extent");
        }
        return version;
    }
    SharedCaptureResourceEvent ReadResourceEvent(const Json &value)
    {
        if (value.is_null())
            return {};
        ExactArray(value, 5);
        auto event = std::make_shared<CaptureResourceEvent>();
        event->domain_id = U(value[0]);
        event->event_id = U(value[1], kCaptureSessionTokenBit - 1);
        event->sequence = U(value[2]);
        event->reset_generation = U(value[3]);
        Array(value[4], 256);
        size_t references = 0;
        for (const auto &item : value[4]) {
            ExactArray(item, 12);
            CaptureResourceAccess access{};
            access.type = CaptureResourceOperationType(
                U(item[0],
                  uint64_t(CaptureResourceOperationType::UncertainWrite)));
            access.allocation_id = U(item[1]);
            access.view_id = U(item[2]);
            access.guest_address = U(item[3]);
            access.range = { U(item[4]), U(item[5]) };
            for (size_t i = 6; i <= 8; ++i) {
                Array(item[i], 16384);
                if (item[i].size() > 16384 - references)
                    throw std::runtime_error(
                        "Resource reference budget exceeded");
                references += item[i].size();
            }
            for (const auto &alias : item[6]) {
                ExactArray(alias, 3);
                access.aliases.push_back(
                    { U(alias[0]), { U(alias[1]), U(alias[2]) } });
            }
            for (const auto &read : item[7])
                access.reads.push_back(ReadResourceVersion(read));
            for (const auto &write : item[8])
                access.writes.push_back(ReadResourceVersion(write));
            access.kind = U32(item[9]);
            access.slot = U32(item[10]);
            access.flags =
                U(item[11], XEMU_SHADER_CAPTURE_RESOURCE_OPAQUE_EXTENT);
            for (const auto &read : access.reads)
                if (read.domain_id != event->domain_id)
                    throw std::runtime_error(
                        "Resource reference has a different capture domain");
            for (const auto &write : access.writes)
                if (write.domain_id != event->domain_id ||
                    (write.producer_event &&
                     (write.producer_event != event->event_id ||
                      write.producer_sequence != event->sequence ||
                      write.producer_operation != event->accesses.size())))
                    throw std::runtime_error(
                        "Resource write producer ownership is invalid");
            event->accesses.push_back(std::move(access));
        }
        return event;
    }
    std::shared_ptr<const CaptureOccurrence> ReadEvent(const Json &value)
    {
        auto out = std::make_shared<CaptureOccurrence>();
        auto &inputs = out->inputs;
        out->event_id = U(value.at("id"), kCaptureSessionTokenBit - 1);
        if (require_execution && !value.contains("execution"))
            throw std::runtime_error("Missing command batch evidence");
        if (value.contains("execution")) {
            const auto &execution = value.at("execution");
            ExactArray(execution, 11);
            out->batch_id = U(execution[0], kCaptureSessionTokenBit - 1);
            out->command_phase = CaptureCommandPhase(
                U(execution[1], uint64_t(CaptureCommandPhase::Main)));
            out->command_ordinal = U(execution[2]);
            out->command_recorded = execution[3].get<bool>();
            out->submission = CaptureBatchOutcome(
                U(execution[4], uint64_t(CaptureBatchOutcome::Detached)));
            out->completion = CaptureBatchCompletion(
                U(execution[5], uint64_t(CaptureBatchCompletion::Failed)));
            const auto signed_result = [](const Json &number) {
                if (!number.is_number_integer())
                    throw std::runtime_error("Invalid backend result");
                const auto numeric = number.get<int64_t>();
                if (numeric < INT32_MIN || numeric > INT32_MAX)
                    throw std::runtime_error("Invalid backend result range");
                return int32_t(numeric);
            };
            out->backend_result = signed_result(execution[6]);
            out->completion_result = signed_result(execution[7]);
            out->resource_finalized = execution[8].get<bool>();
            out->queue_ordinal = U(execution[9]);
            ExactArray(execution[10], 4);
            out->command.kind = CaptureCommandKind(
                U(execution[10][0], uint64_t(CaptureCommandKind::Checkpoint)));
            out->command.source_offset = U(execution[10][1]);
            out->command.destination_offset = U(execution[10][2]);
            out->command.bytes = U(execution[10][3]);
            if (!ValidExecutionReceipt(*out) ||
                out->submission == CaptureBatchOutcome::Pending ||
                out->completion == CaptureBatchCompletion::Pending ||
                (out->command_recorded &&
                 (!out->batch_id || !out->command_ordinal)) ||
                (out->submission == CaptureBatchOutcome::Submitted &&
                 (!out->command_recorded || !out->queue_ordinal ||
                  out->command_phase == CaptureCommandPhase::HostPreparation ||
                  out->completion == CaptureBatchCompletion::None)) ||
                out->command.source_offset > UINT64_MAX - out->command.bytes ||
                out->command.destination_offset >
                    UINT64_MAX - out->command.bytes)
                throw std::runtime_error(
                    "Invalid or unfinished command batch evidence");
        }
        if (value.contains("resource_evidence")) {
            out->resource_evidence =
                ReadResourceEvent(value.at("resource_evidence"));
            if (out->resource_evidence &&
                out->resource_evidence->event_id != out->event_id)
                throw std::runtime_error(
                    "Resource event belongs to a different occurrence");
        }
        if (value.contains("replay_description") &&
            !value.at("replay_description").is_null()) {
            const auto &replay = value.at("replay_description");
            if (U(replay.at("version"), 1) != 1)
                throw std::runtime_error(
                    "Unsupported logical replay description version");
            const auto &command = replay.at("command");
            ExactArray(command, 11);
            auto description = std::make_shared<CaptureReplayDescription>();
            description->kind = U32(command[0]);
            Array(command[1], XEMU_SHADER_CAPTURE_REPLAY_MAX_BINDINGS);
            description->bindings.reserve(command[1].size());
            for (const auto &item : command[1]) {
                ExactArray(item, 6);
                ExactArray(item[0], 4);
                ExactArray(item[5], 11);
                XemuShaderCaptureReplayBinding binding{};
                binding.resource = { U32(item[0][0]), U32(item[0][1]),
                                     U32(item[0][2]), U32(item[0][3]) };
                binding.role = U32(item[1]);
                binding.slot = U32(item[2]);
                binding.checkpoint = U32(item[3]);
                const auto blob = Text(item[4], sizeof(binding.blob_name) - 1);
                std::copy(blob.begin(), blob.end(), binding.blob_name);
                const auto &image = item[5];
                binding.image.width = U32(image[0]);
                binding.image.height = U32(image[1]);
                binding.image.mip_level = U32(image[2]);
                binding.image.layer = U32(image[3]);
                binding.image.mip_levels = U32(image[4]);
                binding.image.layers = U32(image[5]);
                binding.image.samples = U32(image[6]);
                binding.image.format = U32(image[7]);
                binding.image.coordinate_origin = U32(image[8]);
                ExactArray(image[9], 4);
                ExactArray(image[10], 4);
                for (size_t i = 0; i < 4; ++i) {
                    binding.image.storage_to_rgba[i] = U32(image[9][i]);
                    binding.image.sample_swizzle[i] = U32(image[10][i]);
                }
                description->bindings.push_back(binding);
            }
            description->source_x = U32(command[2]);
            description->source_y = U32(command[3]);
            description->destination_x = U32(command[4]);
            description->destination_y = U32(command[5]);
            description->width = U32(command[6]);
            description->height = U32(command[7]);
            description->source_offset = U(command[8]);
            description->destination_offset = U(command[9]);
            description->bytes = U(command[10]);
            if (replay.contains("clear")) {
                const auto &clear = replay.at("clear");
                ExactArray(clear, 5);
                for (size_t i = 0; i < 4; ++i)
                    description->clear_color_bits[i] = U32(clear[i]);
                description->clear_color_mask = U32(clear[4]);
            }
            if (!ValidateCaptureReplayDescription(*description, nullptr))
                throw std::runtime_error(
                    "Malformed logical replay description");
            out->replay_description = std::move(description);
        }
        out->type = CaptureEventType(
            U(value.at("type"), uint64_t(CaptureEventType::SaveStateBoundary)));
        out->summary = ReadSummary(value.at("summary"));
        out->limitations = U32(value.at("limitations"));
        out->finished = value.at("finished").get<bool>();
        out->emitted = value.at("emitted").get<bool>();
        out->pending = value.at("pending").get<bool>();
        out->observed_checkpoint = value.at("checkpoint").get<bool>();
        out->host_timestamp_ns = U(value.at("host_timestamp_ns"));
        out->failure = Text(value.at("failure"), 1024);
        out->payload_bytes = U(value.at("payload_bytes"), kDrawInputBudget);
        const auto &geometry = value.at("geometry");
        ExactArray(geometry, 4);
        out->geometry.positions = Ref(geometry[0]);
        out->geometry.indices = Ref(geometry[1]);
        out->geometry.position_count = U(geometry[2], kDrawInputBudget / 16);
        out->geometry.index_count = U(geometry[3], kDrawInputBudget / 4);
        if ((out->geometry.positions ?
                 out->geometry.positions->bytes.size() :
                 0) != out->geometry.position_count * 16 ||
            (out->geometry.indices ? out->geometry.indices->bytes.size() : 0) !=
                out->geometry.index_count * 4)
            throw std::runtime_error("Invalid raw geometry length");
        inputs.complete = value.at("complete").get<bool>();
        inputs.before = ReadImage(value.at("before"));
        inputs.after = ReadImage(value.at("after"));
        ExactArray(value.at("sources"), inputs.sources.size());
        for (size_t i = 0; i < inputs.sources.size(); ++i)
            inputs.sources[i] = Ref(value.at("sources")[i]);
        Array(value.at("uniforms"), 256);
        for (const auto &item : value.at("uniforms")) {
            ExactArray(item, 6);
            inputs.uniforms.push_back({ U32(item[0]), U32(item[2]),
                                        U32(item[3]), U32(item[4]),
                                        Text(item[1], 255), Ref(item[5]) });
        }
        Array(value.at("registers"), 4096);
        for (const auto &item : value.at("registers")) {
            ExactArray(item, 2);
            inputs.registers.push_back({ Text(item[0], 255), U32(item[1]) });
        }
        ExactArray(value.at("textures"), inputs.textures.size());
        for (size_t i = 0; i < inputs.textures.size(); ++i) {
            const auto &item = value.at("textures")[i];
            auto &out_texture = inputs.textures[i];
            out_texture.described = item.at("described").get<bool>();
            const auto &m = item.at("metadata");
            ExactArray(m, 17);
            auto &t = out_texture.metadata;
            t.slot = U32(m[0]);
            uint32_t bound_bits = U32(m[1]);
            std::memcpy(&t.bound, &bound_bits, sizeof(bound_bits));
            t.guest_format = U32(m[2]);
            t.host_format = U32(m[3]);
            t.width = U32(m[4]);
            t.height = U32(m[5]);
            t.depth = U32(m[6]);
            t.mip_levels = U32(m[7]);
            t.face_count = U32(m[8]);
            t.min_filter = U32(m[9]);
            t.mag_filter = U32(m[10]);
            t.wrap_s = U32(m[11]);
            t.wrap_t = U32(m[12]);
            t.wrap_r = U32(m[13]);
            t.coordinate_scale = BitsFloat(U32(m[14]));
            t.mip_level = U32(m[15]);
            t.face = U32(m[16]);
            Array(item.at("images"), 96);
            for (const auto &image : item.at("images")) {
                ExactArray(image, 3);
                out_texture.images.push_back(
                    { U32(image[0]), U32(image[1]), ReadImage(image[2]) });
            }
        }
        Array(value.at("blobs"), 128);
        for (const auto &item : value.at("blobs")) {
            ExactArray(item, 10);
            CaptureOwnedBlob blob;
            blob.name = Text(item[0], 255);
            blob.slot = U32(item[1]);
            blob.format = U32(item[2]);
            blob.components = U32(item[3]);
            blob.stride = U32(item[4]);
            blob.count = U32(item[5]);
            blob.offset = U(item[6]);
            blob.normalized = U32(item[7]);
            blob.integer = U32(item[8]);
            blob.data = Ref(item[9]);
            if (blob.data && blob.data->bytes.size() > kMaximumBlob)
                throw std::runtime_error("Raw evidence block exceeds limit");
            inputs.blobs.push_back(std::move(blob));
        }
        if (!out->event_id || out->pending)
            throw std::runtime_error(
                "Unfinalized or invalid capture occurrence");
        uint64_t payload_bytes = 0;
        const auto count = [&payload_bytes](const SharedCaptureBlock &block) {
            if (block)
                payload_bytes += block->bytes.size();
        };
        count(out->geometry.positions);
        count(out->geometry.indices);
        count(inputs.before.rgba);
        count(inputs.after.rgba);
        for (const auto &block : inputs.sources)
            count(block);
        for (const auto &uniform : inputs.uniforms)
            count(uniform.data);
        for (const auto &texture : inputs.textures)
            for (const auto &image : texture.images)
                count(image.image.rgba);
        for (const auto &blob : inputs.blobs)
            count(blob.data);
        if (payload_bytes != out->payload_bytes)
            throw std::runtime_error(
                "Capture occurrence payload byte count mismatch");
        return out;
    }
};
Json SettingsJson(const CaptureSessionSettings &settings)
{
    Json sets = Json::array();
    for (const auto &set : settings.live_stage_sets) {
        Json stages = Json::array();
        for (const auto &key : set)
            stages.push_back(
                Json::array({ key.hash.version, key.hash.bytes, key.stage }));
        sets.push_back(std::move(stages));
    }
    return { { "mode", settings.mode },
             { "live_stage_sets", std::move(sets) },
             { "live_continuous", settings.live_continuous },
             { "cpu_bytes", settings.cpu_byte_budget },
             { "event_bytes", settings.per_event_byte_budget },
             { "disk_bytes", settings.disk_byte_budget },
             { "events", settings.event_budget },
             { "history", settings.history_frames },
             { "post", settings.post_frames },
             { "post_trigger_reserve_percent",
               settings.post_trigger_reserve_percent },
             { "maximum_evidence", settings.maximum_evidence } };
}
CaptureSessionSettings ReadSettings(const Json &value)
{
    CaptureSessionSettings settings;
    settings.mode = CaptureSessionMode(U(value.at("mode"), 2));
    if (value.contains("live_continuous"))
        settings.live_continuous = value.at("live_continuous").get<bool>();
    if (value.contains("live_stage_sets")) {
        const auto &sets = value.at("live_stage_sets");
        Array(sets, 128);
        for (const auto &stages : sets) {
            Array(stages, kCapturedShaderSlots);
            std::vector<ShaderKey> set;
            for (const auto &item : stages) {
                ExactArray(item, 3);
                ShaderKey key;
                key.hash.version = U32(item[0]);
                key.hash.bytes = ByteArray<kShaderHashBytes>(item[1]);
                key.stage = Stage(U(item[2], uint64_t(Stage::Unknown)));
                set.push_back(key);
            }
            settings.live_stage_sets.push_back(std::move(set));
        }
    }
    settings.cpu_byte_budget = U(value.at("cpu_bytes"), kMaximumBudget);
    settings.per_event_byte_budget =
        U(value.at("event_bytes"), kDrawInputBudget);
    settings.disk_byte_budget = U(value.at("disk_bytes"), kMaximumBudget);
    settings.event_budget = U32(value.at("events"));
    settings.history_frames = U32(value.at("history"));
    settings.post_frames = U32(value.at("post"));
    settings.post_trigger_reserve_percent =
        value.contains("post_trigger_reserve_percent") ?
            U32(value.at("post_trigger_reserve_percent")) :
            0;
    settings.maximum_evidence = value.at("maximum_evidence").get<bool>();
    if (!ValidSettings(settings))
        throw std::runtime_error("Invalid package capture settings");
    return settings;
}
void Error(std::string *error, const std::string &message)
{
    if (error)
        *error = message;
}
constexpr size_t kMetadataBudget = 32U * 1024U * 1024U;
void FileCheckpoint(CaptureFileControl *control, CaptureFilePhase phase,
                    uint64_t completed = 0, uint64_t total = 0)
{
    if (control && !control->Checkpoint(phase, completed, total))
        throw std::runtime_error("Capture file operation cancelled");
}
void WriteFile(const std::filesystem::path &path, const void *data, size_t size,
               CaptureFileControl *control = nullptr,
               CaptureFilePhase phase = CaptureFilePhase::WritingResources)
{
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file)
        throw std::runtime_error("Failed writing capture package");
    for (size_t offset = 0; offset < size;) {
        FileCheckpoint(control, phase, offset, size);
        const size_t count = std::min<size_t>(1024 * 1024, size - offset);
        if (!file.write(static_cast<const char *>(data) + offset, count))
            throw std::runtime_error("Failed writing capture package");
        offset += count;
    }
    file.flush();
    if (!file)
        throw std::runtime_error("Failed writing capture package");
    FileCheckpoint(control, phase, size, size);
}
std::vector<uint8_t>
ReadFile(const std::filesystem::path &path, uint64_t maximum,
         CaptureFileControl *control = nullptr,
         CaptureFilePhase phase = CaptureFilePhase::ReadingResources)
{
    if (std::filesystem::symlink_status(path).type() !=
        std::filesystem::file_type::regular)
        throw std::runtime_error(
            "Capture package path is not a regular owned file");
    const uint64_t size = std::filesystem::file_size(path);
    if (size > maximum)
        throw std::runtime_error("Capture package file exceeds limit");
    std::vector<uint8_t> bytes(size);
    std::ifstream file(path, std::ios::binary);
    if (!file)
        throw std::runtime_error("Failed reading capture package");
    for (size_t offset = 0; offset < size;) {
        FileCheckpoint(control, phase, offset, size);
        const size_t count = std::min<size_t>(1024 * 1024, size - offset);
        if (!file.read(reinterpret_cast<char *>(bytes.data()) + offset, count))
            throw std::runtime_error("Failed reading capture package");
        offset += count;
    }
    FileCheckpoint(control, phase, size, size);
    return bytes;
}
} // namespace

uint64_t CaptureOccurrenceDescriptorBytes(const CaptureOccurrence &event)
{
    return ArchiveEventOwnedBytes(event);
}
bool CaptureSession::Save(const std::filesystem::path &directory,
                          std::string *error, CaptureFileControl *control) const
{
    return SaveSnapshot(Snapshot(), directory, error, control);
}
bool CaptureSession::SaveSnapshot(const CaptureSessionSnapshot &snapshot,
                                  const std::filesystem::path &directory,
                                  std::string *error,
                                  CaptureFileControl *control)
{
    if (error)
        error->clear();
    std::filesystem::path temporary;
    bool owns_temporary = false;
    try {
        FileCheckpoint(control, CaptureFilePhase::Preparing);
        if (snapshot.pending_events ||
            snapshot.state == CaptureSessionState::Recording ||
            snapshot.state == CaptureSessionState::Triggered ||
            snapshot.state == CaptureSessionState::Finalizing)
            throw std::runtime_error(
                "Capture must stop and finish pending readbacks before saving");
        if (snapshot.annotations.size() > 4U * 1024U * 1024U ||
            !ValidSettings(snapshot.settings) ||
            snapshot.events.size() > snapshot.settings.event_budget ||
            directory.empty() || std::filesystem::exists(directory))
            throw std::runtime_error(
                "Invalid or existing capture package destination");
        PackageWriter writer;
        FileCheckpoint(control, CaptureFilePhase::Validating);
        const auto resource_graph = BuildCaptureSessionResourceGraph(snapshot);
        if (resource_graph.invalid_input || resource_graph.budget_exceeded)
            throw std::runtime_error("Invalid or over-budget resource graph");
        const auto frames = RetainedFrames(snapshot.events);
        Json metadata{
            { "format", "xemu-capture-session" },
            { "version", 3 },
            { "state", snapshot.state },
            { "settings", SettingsJson(snapshot.settings) },
            { "scope", ScopeJson(snapshot.context.scope) },
            { "context", Json::array({ snapshot.context.scope_generation,
                                       snapshot.context.session_epoch,
                                       snapshot.context.renderer_epoch,
                                       snapshot.context.generation,
                                       snapshot.context.current_frame,
                                       snapshot.context.backend }) },
            { "range", Json::array({ !frames.empty(),
                                     frames.empty() ? 0 : frames.front(),
                                     frames.empty() ? 0 : frames.back(),
                                     snapshot.trigger_frame }) },
            { "retained_frames", frames },
            { "reason", snapshot.reason },
            { "annotations", snapshot.annotations },
            { "total_events", snapshot.total_events },
            { "resource_domain", snapshot.resource_domain },
            { "execution_order_complete", snapshot.execution_order_complete },
            { "frame_window_complete", snapshot.frame_window_complete },
            { "event_count", snapshot.events.size() },
            { "event_pages", Json::array() },
            { "blocks", Json::array() }
        };
        temporary = directory;
        temporary += ".tmp-" + std::to_string(next_event_id.fetch_add(1));
        if (!directory.parent_path().empty())
            std::filesystem::create_directories(directory.parent_path());
        owns_temporary = std::filesystem::create_directory(temporary);
        if (!owns_temporary)
            throw std::runtime_error("Temporary capture path already exists");
        std::filesystem::create_directory(temporary / "blocks");
        std::filesystem::create_directory(temporary / "events");
        uint64_t page_bytes = 0;
        uint32_t page_count = 0;
        std::string page = "[";
        auto flush_page = [&] {
            if (!page_count)
                return;
            page += "]";
            if (page.size() > kMetadataBudget ||
                page.size() > snapshot.settings.disk_byte_budget - page_bytes)
                throw std::runtime_error(
                    "Capture event page or disk budget exceeded");
            const std::string file =
                std::to_string(metadata["event_pages"].size()) + ".json";
            metadata["event_pages"].push_back(
                { { "file", file },
                  { "bytes", page.size() },
                  { "count", page_count },
                  { "digest",
                    Digest(reinterpret_cast<const uint8_t *>(page.data()),
                           page.size()) } });
            WriteFile(temporary / "events" / file, page.data(), page.size(),
                      control, CaptureFilePhase::IndexingEvents);
            page_bytes += page.size();
            page = "[";
            page_count = 0;
        };
        uint64_t indexed_events = 0;
        for (const auto &event : snapshot.events) {
            FileCheckpoint(control, CaptureFilePhase::IndexingEvents,
                           indexed_events++, snapshot.events.size());
            if (!event || event->pending)
                throw std::runtime_error("Unfinalized capture event");
            const std::string text = writer.EventJson(*event).dump();
            if (page_count &&
                (page_count >= 256 ||
                 page.size() + text.size() + 2 > 4U * 1024U * 1024U))
                flush_page();
            if (page_count)
                page += ",";
            page += text;
            ++page_count;
        }
        flush_page();
        uint64_t raw_bytes = 0;
        for (const auto &item : writer.blocks) {
            FileCheckpoint(control, CaptureFilePhase::WritingResources);
            const auto &block = item.second;
            const std::string file = std::to_string(block->id) + ".raw";
            if (block->bytes.size() >
                snapshot.settings.disk_byte_budget - page_bytes - raw_bytes)
                throw std::runtime_error("Capture disk budget exceeded");
            raw_bytes += block->bytes.size();
            metadata["blocks"].push_back({ { "id", block->id },
                                           { "digest", block->digest },
                                           { "file", file },
                                           { "bytes", block->bytes.size() } });
        }
        const std::string text = metadata.dump();
        if (text.size() > kMetadataBudget ||
            text.size() >
                snapshot.settings.disk_byte_budget - page_bytes - raw_bytes)
            throw std::runtime_error(
                "Capture metadata or disk budget exceeded");
        for (const auto &item : writer.blocks)
            WriteFile(
                temporary / "blocks" / (std::to_string(item.first) + ".raw"),
                item.second->bytes.data(), item.second->bytes.size(), control);
        WriteFile(temporary / "metadata.json", text.data(), text.size(),
                  control, CaptureFilePhase::WritingMetadata);
        if (control && !control->BeginPublication())
            throw std::runtime_error(
                "Capture file operation cancelled before publication");
        std::filesystem::rename(temporary, directory);
        if (control)
            control->Finish(true);
        return true;
    } catch (const std::exception &exception) {
        if (owns_temporary) {
            std::error_code ignored;
            std::filesystem::remove_all(temporary, ignored);
        }
        Error(error, exception.what());
        if (control)
            control->Finish(false);
        return false;
    }
}
bool CaptureSession::Reopen(const std::filesystem::path &directory,
                            CaptureSessionSnapshot *result, std::string *error,
                            CaptureFileControl *control)
{
    if (error)
        error->clear();
    if (!result) {
        if (control)
            control->Finish(false);
        Error(error, "No capture snapshot destination");
        return false;
    }
    *result = {};
    try {
        FileCheckpoint(control, CaptureFilePhase::ReadingManifest);
        if (std::filesystem::symlink_status(directory).type() !=
                std::filesystem::file_type::directory ||
            std::filesystem::symlink_status(directory / "blocks").type() !=
                std::filesystem::file_type::directory)
            throw std::runtime_error("Invalid capture package directory");
        auto bytes = ReadFile(directory / "metadata.json", kMetadataBudget,
                              control, CaptureFilePhase::ReadingManifest);
        const auto parsed_metadata =
            ParseArchiveJson(bytes, kMaximumBudget, true);
        const Json &metadata = parsed_metadata.value;
        const uint64_t version = U(metadata.at("version"));
        if (metadata.at("format") != "xemu-capture-session" ||
            (version != 1 && version != 2 && version != 3))
            throw std::runtime_error("Unsupported capture package version");
        CaptureSessionSnapshot snapshot;
        snapshot.execution_order_complete =
            version >= 3 && metadata.at("execution_order_complete").get<bool>();
        snapshot.frame_window_complete =
            metadata.value("frame_window_complete", false);
        snapshot.settings = ReadSettings(metadata.at("settings"));
        snapshot.state = CaptureSessionState(
            U(metadata.at("state"), uint64_t(CaptureSessionState::Failed)));
        if (snapshot.state == CaptureSessionState::Recording ||
            snapshot.state == CaptureSessionState::Triggered ||
            snapshot.state == CaptureSessionState::Finalizing)
            throw std::runtime_error(
                "Package contains an unfinished capture session");
        snapshot.context.scope = ReadScope(metadata.at("scope"));
        const auto &context = metadata.at("context");
        ExactArray(context, 6);
        snapshot.context.scope_generation = U(context[0]);
        snapshot.context.session_epoch = U(context[1]);
        snapshot.context.renderer_epoch = U(context[2]);
        snapshot.context.generation = U(context[3]);
        snapshot.context.current_frame = U(context[4]);
        snapshot.context.backend = U32(context[5]);
        if (!snapshot.context.scope_generation ||
            !snapshot.context.session_epoch ||
            !snapshot.context.renderer_epoch || !snapshot.context.generation)
            throw std::runtime_error("Invalid capture package context");
        ExactArray(metadata.at("range"), 4);
        snapshot.has_frame_range = metadata.at("range")[0].get<bool>();
        snapshot.first_frame = U(metadata.at("range")[1]);
        snapshot.last_frame = U(metadata.at("range")[2]);
        snapshot.trigger_frame = U(metadata.at("range")[3]);
        snapshot.reason = Text(metadata.at("reason"), 1024);
        if (metadata.contains("annotations"))
            snapshot.annotations =
                Text(metadata.at("annotations"), 4U * 1024U * 1024U);
        if (metadata.contains("resource_domain"))
            snapshot.resource_domain = U(metadata.at("resource_domain"));
        snapshot.total_events = U(metadata.at("total_events"));
        const uint64_t event_count =
            version == 1 ?
                metadata.at("events").size() :
                U(metadata.at("event_count"), snapshot.settings.event_budget);
        if (version == 1)
            Array(metadata.at("events"), snapshot.settings.event_budget);
        else {
            Array(metadata.at("event_pages"), snapshot.settings.event_budget);
            if (std::filesystem::symlink_status(directory / "events").type() !=
                std::filesystem::file_type::directory)
                throw std::runtime_error("Invalid event page directory");
        }
        Array(metadata.at("blocks"), snapshot.settings.event_budget * 1024U);
        PackageReader reader;
        reader.require_execution = version >= 3;
        uint64_t disk_bytes = bytes.size();
        // Event pointer arrays, retained/declaration frame vectors, the ID
        // index, ownership controls and strings. Page text is temporary;
        // materialized vector/string capacities are charged per occurrence.
        snapshot.cpu_bytes = sizeof(snapshot) + snapshot.reason.capacity() + 1 +
                             snapshot.annotations.capacity() + 1 +
                             event_count * (2 * sizeof(snapshot.events[0]) +
                                            2 * sizeof(uint64_t) + 128);
        const auto remaining = [&](uint64_t temporary = 0) {
            const auto budget = snapshot.settings.cpu_byte_budget;
            if (snapshot.cpu_bytes > budget ||
                parsed_metadata.charge > budget - snapshot.cpu_bytes ||
                temporary >
                    budget - snapshot.cpu_bytes - parsed_metadata.charge)
                throw std::runtime_error("Capture metadata exceeds CPU budget");
            return budget - snapshot.cpu_bytes - parsed_metadata.charge -
                   temporary;
        };
        if (disk_bytes > snapshot.settings.disk_byte_budget ||
            parsed_metadata.charge > snapshot.settings.cpu_byte_budget)
            throw std::runtime_error(
                "Capture metadata exceeds configured budgets");
        remaining();
        snapshot.events.reserve(event_count);
        for (const auto &item : metadata.at("blocks")) {
            const uint64_t id = U(item.at("id"));
            const uint64_t size = U(item.at("bytes"), kDrawInputBudget);
            const std::string file = Text(item.at("file"), 64);
            if (!id || file != std::to_string(id) + ".raw" ||
                reader.blocks.count(id))
                throw std::runtime_error(
                    "Invalid or duplicate capture block path");
            const uint64_t charge = size + sizeof(CaptureImmutableBlock) + 128;
            if (size > snapshot.settings.disk_byte_budget - disk_bytes ||
                charge > remaining())
                throw std::runtime_error(
                    "Capture package exceeds configured budgets");
            auto block = std::make_shared<CaptureImmutableBlock>();
            block->id = id;
            block->digest = ByteArray<kCaptureDigestBytes>(item.at("digest"));
            block->bytes = ReadFile(directory / "blocks" / file, size, control,
                                    CaptureFilePhase::ReadingResources);
            if (block->bytes.size() != size ||
                Digest(block->bytes.data(), size) != block->digest)
                throw std::runtime_error(
                    "Capture block size or digest mismatch");
            disk_bytes += size;
            snapshot.cpu_bytes +=
                block->bytes.capacity() + sizeof(CaptureImmutableBlock) + 128;
            reader.blocks.emplace(id, std::move(block));
        }
        std::map<uint64_t, bool> ids;
        uint64_t page_charge = 0;
        auto append_event = [&](const Json &item) {
            FileCheckpoint(control, CaptureFilePhase::ReadingEvents,
                           snapshot.events.size(), event_count);
            auto event = reader.ReadEvent(item);
            const uint64_t owned = ArchiveEventOwnedBytes(*event);
            if (owned > remaining(page_charge))
                throw std::runtime_error(
                    "Capture occurrence descriptors exceed CPU budget");
            if ((!snapshot.events.empty() &&
                 snapshot.events.back()->event_id >= event->event_id) ||
                !ids.emplace(event->event_id, true).second ||
                event->payload_bytes >
                    snapshot.settings.per_event_byte_budget ||
                event->summary.key.session_epoch !=
                    snapshot.context.session_epoch ||
                event->summary.key.renderer_epoch !=
                    snapshot.context.renderer_epoch ||
                !(event->summary.scope == snapshot.context.scope))
                throw std::runtime_error(
                    "Invalid capture occurrence ownership");
            snapshot.cpu_bytes += owned;
            snapshot.events.push_back(std::move(event));
        };
        if (version == 1) {
            for (const auto &item : metadata.at("events"))
                append_event(item);
        } else {
            size_t page_index = 0;
            for (const auto &description : metadata.at("event_pages")) {
                const uint64_t count = U(description.at("count"), 256);
                const uint64_t size =
                    U(description.at("bytes"), kMetadataBudget);
                const std::string file = Text(description.at("file"), 64);
                if (!count || file != std::to_string(page_index++) + ".json" ||
                    count > event_count - snapshot.events.size() ||
                    size > snapshot.settings.disk_byte_budget - disk_bytes ||
                    size > remaining())
                    throw std::runtime_error(
                        "Capture event page exceeds ownership or budgets");
                auto contents =
                    ReadFile(directory / "events" / file, size, control,
                             CaptureFilePhase::ReadingEvents);
                if (contents.size() != size ||
                    Digest(contents.data(), contents.size()) !=
                        ByteArray<kCaptureDigestBytes>(
                            description.at("digest")))
                    throw std::runtime_error(
                        "Capture event page size or digest mismatch");
                disk_bytes += size;
                const auto parsed_page =
                    ParseArchiveJson(contents, remaining());
                page_charge = parsed_page.charge;
                const Json &events = parsed_page.value;
                ExactArray(events, count);
                for (const auto &item : events)
                    append_event(item);
                page_charge = 0;
            }
            if (snapshot.events.size() != event_count)
                throw std::runtime_error("Capture event page count mismatch");
        }
        bool have_range = false;
        uint64_t first = 0, last = 0;
        for (const auto &event : snapshot.events) {
            const uint64_t frame = event->summary.key.frame;
            if (!have_range) {
                first = last = frame;
                have_range = true;
            } else {
                first = std::min(first, frame);
                last = std::max(last, frame);
            }
        }
        if (have_range != snapshot.has_frame_range ||
            first != snapshot.first_frame || last != snapshot.last_frame ||
            snapshot.total_events < snapshot.events.size())
            throw std::runtime_error(
                "Capture package retained range or count mismatch");
        snapshot.retained_frames = RetainedFrames(snapshot.events);
        Array(metadata.at("retained_frames"), snapshot.events.size());
        std::vector<uint64_t> declared_frames;
        for (const auto &frame : metadata.at("retained_frames"))
            declared_frames.push_back(U(frame));
        if (declared_frames != snapshot.retained_frames)
            throw std::runtime_error(
                "Capture package retained frame coverage mismatch");
        snapshot.unique_blocks = reader.blocks.size();
        const uint64_t validation_budget = remaining();
        if (!validation_budget)
            throw std::runtime_error(
                "Capture resource validation exceeds CPU budget");
        FileCheckpoint(control, CaptureFilePhase::Validating);
        const auto resource_graph =
            BuildCaptureSessionResourceGraph(snapshot, validation_budget);
        if (resource_graph.invalid_input || resource_graph.budget_exceeded)
            throw std::runtime_error(
                "Invalid or over-budget archived resource graph");
        if (control && !control->BeginPublication())
            throw std::runtime_error(
                "Capture file operation cancelled before publication");
        *result = std::move(snapshot);
        if (control)
            control->Finish(true);
        return true;
    } catch (const std::exception &exception) {
        Error(error, exception.what());
        if (control)
            control->Finish(false);
        return false;
    }
}

} // namespace xemu::shader_browser

// SPDX-License-Identifier: GPL-2.0-or-later
#include "shader-browser-capture-resources.hh"

#include <algorithm>
#include <atomic>
#include <new>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace xemu::shader_browser {
namespace {
std::atomic<uint64_t> next_domain{ 1 };
uint64_t NewDomain()
{
    uint64_t value = next_domain.load(std::memory_order_relaxed);
    while (value != UINT64_MAX) {
        if (next_domain.compare_exchange_weak(value, value + 1,
                                              std::memory_order_relaxed))
            return value;
    }
    return 0;
}
bool Valid(CaptureResourceRange range)
{
    return range.size && range.offset <= UINT64_MAX - range.size;
}
bool Fits(CaptureResourceRange range, uint64_t bytes)
{
    return Valid(range) && range.offset <= bytes &&
           range.size <= bytes - range.offset;
}
bool Contains(CaptureResourceRange outer, CaptureResourceRange inner)
{
    return Valid(outer) && Valid(inner) && inner.offset >= outer.offset &&
           inner.offset - outer.offset <= outer.size &&
           inner.size <= outer.size - (inner.offset - outer.offset);
}
bool ValidLimits(const CaptureResourceLimits &l)
{
    return l.max_events && l.max_events <= 1000000 && l.max_allocations &&
           l.max_allocations <= 1000000 && l.max_views &&
           l.max_views <= 1000000 && l.max_fragments &&
           l.max_fragments <= 1048576 && l.max_accesses_per_event &&
           l.max_accesses_per_event <= 65536 && l.max_references_per_event &&
           l.max_references_per_event <= 1048576 && l.max_edges &&
           l.max_edges <= 1048576 && l.max_job_events &&
           l.max_job_events <= 1000000 && l.max_job_work &&
           l.max_job_work <= 16777216 && l.byte_budget &&
           l.byte_budget <= UINT64_C(1024) * 1024 * 1024;
}
struct Work {
    size_t used = 0, limit;
    bool Add(size_t count = 1)
    {
        if (count > limit - used)
            return false;
        used += count;
        return true;
    }
};
uint64_t EventBytes(const CaptureResourceEvent &event)
{
    // Spare vector capacity and conservative node/ownership overhead. Payload
    // blocks themselves remain owned and accounted by CaptureSession.
    uint64_t bytes =
        sizeof(event) + 128 +
        event.accesses.size() * (2 * sizeof(CaptureResourceAccess) + 32);
    for (const auto &access : event.accesses) {
        bytes += 2 * (access.reads.size() + access.writes.size()) *
                 sizeof(CaptureResourceVersion);
        bytes += 2 * access.aliases.size() * sizeof(CaptureResourceAlias);
    }
    return bytes;
}
struct VersionKey {
    uint64_t allocation, version;
    bool operator==(const VersionKey &other) const
    {
        return allocation == other.allocation && version == other.version;
    }
};
struct VersionHash {
    size_t operator()(const VersionKey &key) const
    {
        return std::hash<uint64_t>{}(key.allocation) ^
               (std::hash<uint64_t>{}(key.version) << 1);
    }
};
CaptureResourceVersion Unknown(CaptureResourceProvenance reason,
                               CaptureResourceRange range,
                               uint64_t allocation = 0)
{
    CaptureResourceVersion ref;
    ref.allocation_id = allocation;
    ref.range = range;
    ref.provenance = reason;
    return ref;
}
void ReplaceFragments(std::vector<CaptureResourceVersion> &fragments,
                      const CaptureResourceVersion &version)
{
    std::vector<CaptureResourceVersion> result;
    result.reserve(fragments.size() + 2);
    const uint64_t start = version.range.offset,
                   end = start + version.range.size;
    bool inserted = false;
    for (const auto &fragment : fragments) {
        uint64_t first = fragment.range.offset,
                 last = first + fragment.range.size;
        if (last <= start) {
            result.push_back(fragment);
            continue;
        }
        if (first < start) {
            auto prefix = fragment;
            prefix.range.size = start - first;
            result.push_back(prefix);
        }
        if (!inserted) {
            result.push_back(version);
            inserted = true;
        }
        if (first >= end) {
            result.push_back(fragment);
        } else if (last > end) {
            auto suffix = fragment;
            suffix.range = { end, last - end };
            result.push_back(suffix);
        }
    }
    if (!inserted)
        result.push_back(version);
    fragments = std::move(result);
}
} // namespace

struct CaptureResourceTimeline::Impl {
    struct Account {
        std::atomic<uint64_t> bytes{ 0 };
    };
    struct Allocation {
        uint64_t bytes = 0, generation = 0, next_version = 0;
        std::vector<CaptureResourceVersion> fragments;
    };
    struct View {
        bool proven = false;
        std::vector<CaptureResourceAlias> aliases;
    };
    using Allocations = std::unordered_map<uint64_t, Allocation>;
    using Views = std::unordered_map<uint64_t, View>;
    CaptureResourceLimits limits;
    CaptureResourceModelStatus status = CaptureResourceModelStatus::Ready;
    std::shared_ptr<Account> account = std::make_shared<Account>();
    Allocations allocations;
    Views views;
    std::vector<SharedCaptureResourceEvent> events;
    uint64_t next_allocation = 1, next_view = 1, last_allocation = 0;
    uint64_t last_event = 0, sequence = 0, generation = 1, state_bytes = 0;
    uint64_t poison_sequence = 0;
    uint32_t poison_operation = 0;
    size_t fragments = 0;
    uint64_t domain = NewDomain();
    ~Impl()
    {
        account->bytes.fetch_sub(state_bytes);
    }
    static uint64_t Bytes(const Allocation &a)
    {
        return sizeof(a) + 96 +
               2 * a.fragments.size() * sizeof(CaptureResourceVersion);
    }
    static uint64_t Bytes(const View &v)
    {
        return sizeof(v) + 96 +
               2 * v.aliases.size() * sizeof(CaptureResourceAlias);
    }
};
CaptureResourceTimeline::CaptureResourceTimeline(
    const CaptureResourceLimits &limits)
    : impl_(std::make_unique<Impl>())
{
    impl_->limits = limits;
    if (!ValidLimits(limits) || !impl_->domain)
        impl_->status = CaptureResourceModelStatus::InvalidInput;
}
CaptureResourceTimeline::~CaptureResourceTimeline() = default;
uint64_t CaptureResourceTimeline::ReserveAllocationId()
{
    if (impl_->status != CaptureResourceModelStatus::Ready ||
        impl_->next_allocation == UINT64_MAX)
        return 0;
    return impl_->next_allocation++;
}
uint64_t CaptureResourceTimeline::ReserveViewId()
{
    if (impl_->status != CaptureResourceModelStatus::Ready ||
        impl_->next_view == UINT64_MAX)
        return 0;
    return impl_->next_view++;
}
SharedCaptureResourceEvent CaptureResourceTimeline::Apply(
    uint64_t event_id, const std::vector<CaptureResourceOperation> &operations)
{
    if (!event_id || event_id <= impl_->last_event) {
        impl_->status = CaptureResourceModelStatus::InvalidInput;
        return {};
    }
    try {
        auto result = ApplyBatch({ { event_id, operations } });
        return result.empty() ? SharedCaptureResourceEvent{} : result.front();
    } catch (const std::bad_alloc &) {
        impl_->status = CaptureResourceModelStatus::BudgetExceeded;
        return {};
    }
}
std::vector<SharedCaptureResourceEvent> CaptureResourceTimeline::ApplyBatch(
    const std::vector<CaptureResourceCommand> &commands,
    uint64_t additional_byte_budget)
{
    auto &state = *impl_;
    if (state.status != CaptureResourceModelStatus::Ready || commands.empty())
        return {};
    const auto stop = [&](CaptureResourceModelStatus status) {
        state.status = status;
        return std::vector<SharedCaptureResourceEvent>{};
    };
    if (commands.size() > state.limits.max_events - state.events.size() ||
        commands.size() > UINT64_MAX - state.sequence)
        return stop(CaptureResourceModelStatus::BudgetExceeded);
    try {
        const uint64_t retained = state.account->bytes.load();
        if (retained > state.limits.byte_budget)
            return stop(CaptureResourceModelStatus::BudgetExceeded);
        const uint64_t allowance = std::min(
            additional_byte_budget, state.limits.byte_budget - retained);
        uint64_t scratch_bytes = 0;
        const auto scratch = [&](uint64_t bytes) {
            if (bytes > allowance - scratch_bytes)
                return false;
            scratch_bytes += bytes;
            return true;
        };
        if (!scratch(commands.size() *
                     (2 * sizeof(CaptureResourceEvent) + 256)))
            return stop(CaptureResourceModelStatus::BudgetExceeded);
        // Only touched state is copied. After destination reserve succeeds,
        // node merges and vector moves publish this transaction atomically.
        Impl::Allocations changed_allocations;
        Impl::Views changed_views;
        std::unordered_set<uint64_t> removed_allocations, removed_views;
        bool reset = false, exhausted = false;
        uint64_t generation = state.generation,
                 last_allocation = state.last_allocation;
        uint64_t poison_sequence = state.poison_sequence;
        uint32_t poison_operation = state.poison_operation;
        Work work{ 0, state.limits.max_job_work };
        std::vector<CaptureResourceEvent> prepared;
        prepared.reserve(commands.size());
        std::unordered_set<uint64_t> ids;
        if (std::any_of(commands.begin(), commands.end(),
                        [&](const auto &command) {
                            return command.event_id <= state.last_event;
                        }))
            for (const auto &prior : state.events) {
                if (!work.Add() || !scratch(64))
                    return stop(CaptureResourceModelStatus::BudgetExceeded);
                ids.insert(prior->event_id);
            }
        for (const auto &command : commands) {
            if (!work.Add() || !scratch(64))
                return stop(CaptureResourceModelStatus::BudgetExceeded);
            if (!command.event_id || !ids.insert(command.event_id).second)
                return stop(CaptureResourceModelStatus::InvalidInput);
            const auto &operations = command.operations;
            if (operations.size() > state.limits.max_accesses_per_event)
                return stop(CaptureResourceModelStatus::BudgetExceeded);
            const uint64_t event_id = command.event_id;
            size_t references = 0;
            CaptureResourceEvent event{
                event_id, state.sequence + prepared.size() + 1, generation, {}
            };
            event.accesses.reserve(operations.size());
            event.domain_id = state.domain;
            const auto allocation = [&](uint64_t id) -> Impl::Allocation * {
                auto changed = changed_allocations.find(id);
                if (changed != changed_allocations.end())
                    return &changed->second;
                if (reset || removed_allocations.count(id))
                    return nullptr;
                auto original = state.allocations.find(id);
                if (original == state.allocations.end())
                    return nullptr;
                if (!work.Add(original->second.fragments.size()) ||
                    !scratch(Impl::Bytes(original->second))) {
                    exhausted = true;
                    return nullptr;
                }
                return &changed_allocations.emplace(id, original->second)
                            .first->second;
            };
            const auto view = [&](uint64_t id) -> const Impl::View * {
                auto changed = changed_views.find(id);
                if (changed != changed_views.end())
                    return &changed->second;
                if (reset || removed_views.count(id))
                    return nullptr;
                auto found = state.views.find(id);
                return found == state.views.end() ? nullptr : &found->second;
            };
            for (size_t index = 0; index < operations.size(); ++index) {
                const auto &op = operations[index];
                if (!scratch(2 * sizeof(CaptureResourceAccess) + 128 +
                             2 * op.aliases.size() *
                                 sizeof(CaptureResourceAlias)))
                    return stop(CaptureResourceModelStatus::BudgetExceeded);
                if (!work.Add() ||
                    op.aliases.size() >
                        state.limits.max_references_per_event - references)
                    return stop(CaptureResourceModelStatus::BudgetExceeded);
                CaptureResourceAccess access{ op.type,    op.allocation_id,
                                              op.view_id, op.guest_address,
                                              op.range,   op.aliases,
                                              {},         {} };
                access.kind = op.kind;
                access.slot = op.slot;
                access.flags = op.flags;
                switch (op.type) {
                case CaptureResourceOperationType::Allocate: {
                    uint64_t id = op.allocation_id;
                    if (!id || id >= state.next_allocation ||
                        id <= last_allocation || !op.byte_size)
                        return stop(CaptureResourceModelStatus::InvalidInput);
                    last_allocation = id;
                    if (!scratch(sizeof(Impl::Allocation) + 256 +
                                 2 * sizeof(CaptureResourceVersion)))
                        return stop(CaptureResourceModelStatus::BudgetExceeded);
                    Impl::Allocation a;
                    a.bytes = op.byte_size;
                    a.generation = generation;
                    auto origin =
                        Unknown(CaptureResourceProvenance::MissingProducer,
                                { 0, op.byte_size }, id);
                    origin.reset_generation = generation;
                    origin.domain_id = state.domain;
                    a.fragments.push_back(origin);
                    changed_allocations.emplace(id, std::move(a));
                    access.range = { 0, op.byte_size };
                    break;
                }
                case CaptureResourceOperationType::Alias:
                    if (!op.view_id || op.view_id >= state.next_view)
                        return stop(CaptureResourceModelStatus::InvalidInput);
                    if (!scratch(sizeof(Impl::View) + 256 +
                                 2 * op.aliases.size() *
                                     sizeof(CaptureResourceAlias)))
                        return stop(CaptureResourceModelStatus::BudgetExceeded);
                    changed_views[op.view_id] = {
                        op.alias_proven && op.aliases.size() == 1, op.aliases
                    };
                    removed_views.erase(op.view_id);
                    break;
                case CaptureResourceOperationType::RemoveAlias:
                    changed_views.erase(op.view_id);
                    removed_views.insert(op.view_id);
                    break;
                case CaptureResourceOperationType::Release:
                    changed_allocations.erase(op.allocation_id);
                    removed_allocations.insert(op.allocation_id);
                    break;
                case CaptureResourceOperationType::Reset:
                    if (generation == UINT64_MAX)
                        return stop(CaptureResourceModelStatus::InvalidInput);
                    ++generation;
                    reset = true;
                    changed_allocations.clear();
                    changed_views.clear();
                    removed_allocations.clear();
                    removed_views.clear();
                    poison_sequence = 0;
                    poison_operation = 0;
                    break;
                case CaptureResourceOperationType::Read:
                case CaptureResourceOperationType::PartialWrite:
                case CaptureResourceOperationType::UncertainWrite:
                case CaptureResourceOperationType::FullWrite: {
                    const bool read =
                        op.type == CaptureResourceOperationType::Read;
                    const Impl::View *v = view(op.view_id);
                    auto &output = read ? access.reads : access.writes;
                    if (!v || v->aliases.empty()) {
                        output.push_back(Unknown(
                            CaptureResourceProvenance::UnknownView, op.range));
                        if (!read) {
                            // An unidentified target can affect any existing
                            // backing. Later proven writes restore only their
                            // covered bytes.
                            poison_sequence = event.sequence;
                            poison_operation = index;
                        }
                        break;
                    }
                    for (const auto &alias : v->aliases) {
                        if (!work.Add())
                            return stop(
                                CaptureResourceModelStatus::BudgetExceeded);
                        auto *a = allocation(alias.allocation_id);
                        auto relative =
                            op.type == CaptureResourceOperationType::FullWrite ?
                                CaptureResourceRange{ 0, alias.range.size } :
                                op.range;
                        if (!a) {
                            output.push_back(Unknown(
                                CaptureResourceProvenance::RetiredAllocation,
                                relative, alias.allocation_id));
                            continue;
                        }
                        bool valid = Fits(alias.range, a->bytes) &&
                                     Fits(relative, alias.range.size);
                        auto range =
                            valid ? CaptureResourceRange{ alias.range.offset +
                                                              relative.offset,
                                                          relative.size } :
                                    CaptureResourceRange{ 0, a->bytes };
                        auto provenance =
                            !valid ?
                                CaptureResourceProvenance::InvalidRange :
                            !v->proven ?
                                CaptureResourceProvenance::AmbiguousAlias :
                            op.type == CaptureResourceOperationType::
                                           UncertainWrite ?
                                CaptureResourceProvenance::UnknownCoverage :
                                CaptureResourceProvenance::Known;
                        if (read) {
                            if (provenance !=
                                CaptureResourceProvenance::Known) {
                                output.push_back(Unknown(provenance, range,
                                                         alias.allocation_id));
                                continue;
                            }
                            for (const auto &fragment : a->fragments) {
                                if (!work.Add())
                                    return stop(CaptureResourceModelStatus::
                                                    BudgetExceeded);
                                uint64_t first = std::max(
                                    range.offset, fragment.range.offset);
                                uint64_t end =
                                    std::min(range.offset + range.size,
                                             fragment.range.offset +
                                                 fragment.range.size);
                                if (first >= end)
                                    continue;
                                auto ref = fragment;
                                ref.range = { first, end - first };
                                if (op.snapshot_block) {
                                    ref.snapshot_block = op.snapshot_block;
                                    ref.snapshot_range = range;
                                    ref.content_digest = op.content_digest;
                                }
                                if (poison_sequence &&
                                    (ref.producer_sequence < poison_sequence ||
                                     (ref.producer_sequence ==
                                          poison_sequence &&
                                      ref.producer_operation <=
                                          poison_operation)))
                                    ref.provenance =
                                        CaptureResourceProvenance::UnknownView;
                                if (!scratch(2 *
                                             sizeof(CaptureResourceVersion)))
                                    return stop(CaptureResourceModelStatus::
                                                    BudgetExceeded);
                                output.push_back(ref);
                            }
                        } else {
                            if (a->next_version == UINT64_MAX ||
                                !work.Add(a->fragments.size()))
                                return stop(
                                    CaptureResourceModelStatus::BudgetExceeded);
                            auto version =
                                Unknown(provenance, range, alias.allocation_id);
                            version.version = ++a->next_version;
                            version.reset_generation = generation;
                            version.domain_id = state.domain;
                            version.producer_event = event_id;
                            version.producer_sequence = event.sequence;
                            version.producer_operation = index;
                            version.snapshot_block = op.snapshot_block;
                            version.snapshot_range = range;
                            version.content_digest = op.content_digest;
                            if (!scratch(4 * sizeof(CaptureResourceVersion) +
                                         Impl::Bytes(*a)))
                                return stop(
                                    CaptureResourceModelStatus::BudgetExceeded);
                            output.push_back(version);
                            ReplaceFragments(a->fragments, version);
                        }
                    }
                    break;
                }
                default:
                    return stop(CaptureResourceModelStatus::InvalidInput);
                }
                for (auto &reference : access.reads)
                    reference.domain_id = state.domain;
                for (auto &reference : access.writes)
                    reference.domain_id = state.domain;
                size_t count = access.reads.size() + access.writes.size() +
                               access.aliases.size();
                if (exhausted ||
                    count > state.limits.max_references_per_event - references)
                    return stop(CaptureResourceModelStatus::BudgetExceeded);
                references += count;
                event.accesses.push_back(std::move(access));
            }
            prepared.push_back(std::move(event));
        }
        size_t allocation_count = reset ? 0 : state.allocations.size();
        size_t view_count = reset ? 0 : state.views.size();
        size_t fragment_count = reset ? 0 : state.fragments;
        uint64_t state_bytes = reset ? 0 : state.state_bytes;
        const auto remove_allocation = [&](uint64_t id) {
            auto old = state.allocations.find(id);
            if (!reset && old != state.allocations.end()) {
                --allocation_count;
                fragment_count -= old->second.fragments.size();
                state_bytes -= Impl::Bytes(old->second);
            }
        };
        const auto remove_view = [&](uint64_t id) {
            auto old = state.views.find(id);
            if (!reset && old != state.views.end()) {
                --view_count;
                state_bytes -= Impl::Bytes(old->second);
            }
        };
        for (const auto &entry : changed_allocations) {
            remove_allocation(entry.first);
            ++allocation_count;
            fragment_count += entry.second.fragments.size();
            state_bytes += Impl::Bytes(entry.second);
        }
        for (uint64_t id : removed_allocations)
            remove_allocation(id);
        for (const auto &entry : changed_views) {
            remove_view(entry.first);
            ++view_count;
            state_bytes += Impl::Bytes(entry.second);
        }
        for (uint64_t id : removed_views)
            remove_view(id);
        uint64_t event_bytes = 0;
        for (const auto &event : prepared) {
            const auto bytes = EventBytes(event);
            if (bytes > state.limits.byte_budget - event_bytes)
                return stop(CaptureResourceModelStatus::BudgetExceeded);
            event_bytes += bytes;
        }
        const uint64_t model_budget = retained + allowance;
        uint64_t retained_bytes = retained - state.state_bytes;
        if (allocation_count > state.limits.max_allocations ||
            view_count > state.limits.max_views ||
            fragment_count > state.limits.max_fragments ||
            state_bytes > model_budget ||
            event_bytes > model_budget - state_bytes ||
            retained_bytes > model_budget - state_bytes - event_bytes)
            return stop(CaptureResourceModelStatus::BudgetExceeded);
        state.allocations.reserve(allocation_count);
        state.views.reserve(view_count);
        if (state.events.size() + prepared.size() > state.events.capacity())
            state.events.reserve(
                std::min(state.limits.max_events,
                         std::max(state.events.size() + prepared.size(),
                                  state.events.capacity() * 2)));
        std::vector<SharedCaptureResourceEvent> published;
        published.reserve(prepared.size());
        for (auto &event : prepared) {
            const auto charge = EventBytes(event);
            auto owned =
                std::make_unique<CaptureResourceEvent>(std::move(event));
            state.account->bytes.fetch_add(charge);
            published.emplace_back(owned.release(),
                                   [account = state.account,
                                    charge](const CaptureResourceEvent *data) {
                                       delete data;
                                       account->bytes.fetch_sub(charge);
                                   });
        }
        if (reset) {
            state.allocations.clear();
            state.views.clear();
        }
        for (const auto &entry : changed_allocations)
            state.allocations.erase(entry.first);
        for (uint64_t id : removed_allocations)
            state.allocations.erase(id);
        for (const auto &entry : changed_views)
            state.views.erase(entry.first);
        for (uint64_t id : removed_views)
            state.views.erase(id);
        state.allocations.merge(changed_allocations);
        state.views.merge(changed_views);
        state.account->bytes.fetch_sub(state.state_bytes);
        state.account->bytes.fetch_add(state_bytes);
        state.state_bytes = state_bytes;
        state.fragments = fragment_count;
        state.generation = generation;
        state.last_allocation = last_allocation;
        for (const auto &event : published)
            state.last_event = std::max(state.last_event, event->event_id);
        state.sequence = published.back()->sequence;
        state.poison_sequence = poison_sequence;
        state.poison_operation = poison_operation;
        state.events.insert(state.events.end(), published.begin(),
                            published.end());
        return published;
    } catch (const std::bad_alloc &) {
        state.status = CaptureResourceModelStatus::BudgetExceeded;
        throw;
    }
}
void CaptureResourceTimeline::EvictBefore(uint64_t sequence)
{
    auto &events = impl_->events;
    events.erase(events.begin(),
                 std::lower_bound(events.begin(), events.end(), sequence,
                                  [](const auto &event, uint64_t boundary) {
                                      return event->sequence < boundary;
                                  }));
}
std::vector<SharedCaptureResourceEvent> CaptureResourceTimeline::Events() const
{
    return impl_->events;
}
void CaptureResourceTimeline::RetainEvents(const std::vector<uint64_t> &ids)
{
    std::unordered_set<uint64_t> retained(ids.begin(), ids.end());
    auto &events = impl_->events;
    events.erase(std::remove_if(events.begin(), events.end(),
                                [&](const auto &event) {
                                    return !retained.count(event->event_id);
                                }),
                 events.end());
}
std::vector<uint64_t> CaptureResourceTimeline::LiveProducerEvents() const
{
    std::vector<uint64_t> ids;
    for (const auto &item : impl_->allocations)
        for (const auto &fragment : item.second.fragments)
            if (fragment.producer_event)
                ids.push_back(fragment.producer_event);
    std::sort(ids.begin(), ids.end());
    ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
    return ids;
}
CaptureResourceModelStatus CaptureResourceTimeline::Status() const
{
    return impl_->status;
}
uint64_t CaptureResourceTimeline::AccountedBytes() const
{
    return impl_->account->bytes.load();
}
uint64_t CaptureResourceTimeline::DomainId() const
{
    return impl_->domain;
}

CaptureResourceGraph
BuildCaptureResourceGraph(const std::vector<SharedCaptureResourceEvent> &events,
                          const CaptureResourceLimits &limits)
{
    CaptureResourceGraph graph;
    if (!ValidLimits(limits)) {
        graph.invalid_input = true;
        return graph;
    }
    if (events.size() > limits.max_events) {
        graph.budget_exceeded = true;
        return graph;
    }
    try {
        graph.events = events;
        std::sort(graph.events.begin(), graph.events.end(),
                  [](const auto &a, const auto &b) {
                      return a && b ? a->sequence < b->sequence : bool(b);
                  });
        struct Producer {
            size_t event;
            const CaptureResourceVersion *version;
        };
        std::unordered_map<VersionKey, Producer, VersionHash> producers;
        std::unordered_set<VersionKey, VersionHash> ambiguous;
        Work work{ 0, limits.max_job_work };
        uint64_t bytes = 0, last_sequence = 0, domain = 0;
        for (size_t i = 0; i < graph.events.size(); ++i) {
            const auto &event = graph.events[i];
            if (!event || !event->event_id || !event->sequence ||
                !event->domain_id || (domain && domain != event->domain_id) ||
                event->sequence <= last_sequence ||
                !graph.event_index.emplace(event->event_id, i).second) {
                graph.invalid_input = true;
                return graph;
            }
            last_sequence = event->sequence;
            domain = event->domain_id;
            if (event->accesses.size() > limits.max_accesses_per_event) {
                graph.budget_exceeded = true;
                return graph;
            }
            uint64_t charge = EventBytes(*event);
            if (!work.Add() || charge > limits.byte_budget - bytes) {
                graph.budget_exceeded = true;
                return graph;
            }
            bytes += charge;
            size_t references = 0;
            for (size_t access_index = 0; access_index < event->accesses.size();
                 ++access_index) {
                const auto &access = event->accesses[access_index];
                size_t count = access.reads.size() + access.writes.size() +
                               access.aliases.size();
                if (!work.Add(1 + count) ||
                    count > limits.max_references_per_event - references) {
                    graph.budget_exceeded = true;
                    return graph;
                }
                references += count;
                for (const auto &version : access.writes) {
                    if (version.provenance !=
                            CaptureResourceProvenance::Known &&
                        version.provenance !=
                            CaptureResourceProvenance::UnknownCoverage)
                        continue;
                    if (!version.allocation_id || !version.version ||
                        version.domain_id != domain || !Valid(version.range) ||
                        version.producer_event != event->event_id ||
                        version.producer_sequence != event->sequence ||
                        version.producer_operation != access_index) {
                        graph.invalid_input = true;
                        return graph;
                    }
                    VersionKey key{ version.allocation_id, version.version };
                    if (!producers.emplace(key, Producer{ i, &version }).second)
                        ambiguous.insert(key);
                }
            }
        }
        graph.inputs.resize(events.size());
        graph.consumers.resize(events.size());
        graph.gap_indexes.resize(events.size());
        for (size_t i = 0; i < graph.events.size(); ++i) {
            const auto &event = graph.events[i];
            for (size_t access_index = 0; access_index < event->accesses.size();
                 ++access_index) {
                const auto &access = event->accesses[access_index];
                const auto gap = [&](size_t fragment,
                                     CaptureResourceProvenance reason,
                                     bool write = false) {
                    graph.gap_indexes[i].push_back(graph.gaps.size());
                    graph.gaps.push_back(
                        { event->event_id, static_cast<uint32_t>(access_index),
                          static_cast<uint32_t>(fragment), reason, write });
                };
                for (size_t fragment = 0; fragment < access.writes.size();
                     ++fragment)
                    if (access.writes[fragment].provenance !=
                        CaptureResourceProvenance::Known)
                        gap(fragment, access.writes[fragment].provenance, true);
                for (size_t fragment = 0; fragment < access.reads.size();
                     ++fragment) {
                    if (!work.Add()) {
                        graph.budget_exceeded = true;
                        return graph;
                    }
                    const auto &ref = access.reads[fragment];
                    if (ref.provenance != CaptureResourceProvenance::Known) {
                        gap(fragment, ref.provenance);
                        if (ref.provenance !=
                            CaptureResourceProvenance::UnknownCoverage)
                            continue;
                    }
                    if (!ref.allocation_id || !ref.version ||
                        ref.domain_id != domain || !Valid(ref.range)) {
                        gap(fragment, CaptureResourceProvenance::InvalidRange);
                        continue;
                    }
                    VersionKey key{ ref.allocation_id, ref.version };
                    if (ambiguous.count(key)) {
                        gap(fragment,
                            CaptureResourceProvenance::AmbiguousAlias);
                        continue;
                    }
                    auto found = producers.find(key);
                    if (found == producers.end()) {
                        gap(fragment,
                            CaptureResourceProvenance::ProducerNotRetained);
                        continue;
                    }
                    const auto &producer = found->second;
                    const auto &source = *producer.version;
                    if (source.producer_event != ref.producer_event ||
                        source.producer_sequence != ref.producer_sequence ||
                        source.reset_generation != ref.reset_generation ||
                        source.producer_operation != ref.producer_operation ||
                        !Contains(source.range, ref.range) ||
                        producer.event > i ||
                        (producer.event == i &&
                         source.producer_operation >= access_index)) {
                        graph.invalid_input = true;
                        return graph;
                    }
                    if (producer.event == i)
                        continue;
                    if (graph.edges.size() >= limits.max_edges ||
                        sizeof(CaptureResourceDependency) + 2 * sizeof(size_t) >
                            limits.byte_budget - bytes) {
                        graph.budget_exceeded = true;
                        return graph;
                    }
                    bytes +=
                        sizeof(CaptureResourceDependency) + 2 * sizeof(size_t);
                    size_t edge = graph.edges.size();
                    graph.edges.push_back(
                        { source.producer_event, event->event_id,
                          ref.allocation_id, ref.version, ref.range,
                          static_cast<uint32_t>(access_index),
                          static_cast<uint32_t>(fragment),
                          ref.provenance ==
                              CaptureResourceProvenance::UnknownCoverage });
                    graph.inputs[i].push_back(edge);
                    graph.consumers[producer.event].push_back(edge);
                }
            }
        }
    } catch (const std::bad_alloc &) {
        graph.budget_exceeded = true;
    }
    return graph;
}
CaptureResourceClosure TraceCaptureResources(
    const CaptureResourceGraph &graph, const std::vector<uint64_t> &seeds,
    CaptureResourceTrace direction, const CaptureResourceLimits &limits)
{
    CaptureResourceClosure result;
    result.invalid_input = graph.invalid_input || !ValidLimits(limits) ||
                           (direction != CaptureResourceTrace::Inputs &&
                            direction != CaptureResourceTrace::Influence);
    result.budget_exceeded = graph.budget_exceeded;
    if (result.invalid_input || result.budget_exceeded)
        return result;
    if (seeds.size() > limits.max_job_events ||
        graph.events.size() > limits.max_events ||
        graph.edges.size() > limits.max_edges) {
        result.budget_exceeded = true;
        return result;
    }
    if (graph.event_index.size() != graph.events.size() ||
        graph.inputs.size() != graph.events.size() ||
        graph.consumers.size() != graph.events.size() ||
        graph.gap_indexes.size() != graph.events.size()) {
        result.invalid_input = true;
        return result;
    }
    try {
        Work work{ 0, limits.max_job_work };
        std::unordered_set<size_t> selected;
        std::vector<size_t> pending;
        uint64_t bytes = 0;
        const auto charge = [&](uint64_t amount) {
            if (amount > limits.byte_budget - bytes) {
                result.budget_exceeded = true;
                return false;
            }
            bytes += amount;
            return true;
        };
        const auto add_event = [&](uint64_t id) {
            if (!work.Add()) {
                result.budget_exceeded = true;
                return false;
            }
            auto found = graph.event_index.find(id);
            if (found == graph.event_index.end() ||
                found->second >= graph.events.size() ||
                !graph.events[found->second] ||
                graph.events[found->second]->event_id != id) {
                result.invalid_input = true;
                return false;
            }
            if (!selected.count(found->second)) {
                if (selected.size() >= limits.max_job_events || !charge(128)) {
                    result.budget_exceeded = true;
                    return false;
                }
                selected.insert(found->second);
                pending.push_back(found->second);
            }
            return true;
        };
        for (uint64_t seed : seeds)
            if (!add_event(seed))
                return result;
        for (size_t cursor = 0; cursor < pending.size(); ++cursor) {
            if (!work.Add()) {
                result.budget_exceeded = true;
                return result;
            }
            uint64_t current = graph.events[pending[cursor]]->event_id;
            const auto &adjacent = direction == CaptureResourceTrace::Inputs ?
                                       graph.inputs[pending[cursor]] :
                                       graph.consumers[pending[cursor]];
            for (size_t edge : adjacent) {
                if (edge >= graph.edges.size()) {
                    result.invalid_input = true;
                    return result;
                }
                const auto &dependency = graph.edges[edge];
                uint64_t expected = direction == CaptureResourceTrace::Inputs ?
                                        dependency.consumer_event :
                                        dependency.producer_event;
                uint64_t next = direction == CaptureResourceTrace::Inputs ?
                                    dependency.producer_event :
                                    dependency.consumer_event;
                if (expected != current) {
                    result.invalid_input = true;
                    return result;
                }
                if (!add_event(next))
                    return result;
            }
        }
        std::sort(pending.begin(), pending.end());
        std::unordered_set<VersionKey, VersionHash> produced;
        for (size_t index : pending) {
            const auto &event = *graph.events[index];
            result.events.push_back(event.event_id);
            for (const auto &access : event.accesses) {
                if (!work.Add()) {
                    result.budget_exceeded = true;
                    return result;
                }
                for (const auto &output : access.writes) {
                    if (!work.Add() || !charge(2 * sizeof(output) + 64)) {
                        result.budget_exceeded = true;
                        return result;
                    }
                    result.invalidated_outputs.push_back(output);
                    if (output.allocation_id && output.version)
                        produced.insert(
                            { output.allocation_id, output.version });
                }
            }
        }
        for (size_t index : pending) {
            const auto &event = *graph.events[index];
            for (const auto &access : event.accesses) {
                if (!work.Add()) {
                    result.budget_exceeded = true;
                    return result;
                }
                for (const auto &input : access.reads) {
                    if (!work.Add()) {
                        result.budget_exceeded = true;
                        return result;
                    }
                    if (!produced.count(
                            { input.allocation_id, input.version })) {
                        if (!charge(2 * sizeof(input)))
                            return result;
                        result.external_inputs.push_back(input);
                    }
                }
            }
            for (size_t gap : graph.gap_indexes[index]) {
                if (!work.Add() || !charge(2 * sizeof(CaptureResourceGap))) {
                    result.budget_exceeded = true;
                    return result;
                }
                if (gap >= graph.gaps.size() ||
                    graph.gaps[gap].event_id != event.event_id) {
                    result.invalid_input = true;
                    return result;
                }
                result.gaps.push_back(graph.gaps[gap]);
            }
        }
        result.provenance_complete =
            result.gaps.empty() && graph.execution_order_complete;
    } catch (const std::bad_alloc &) {
        result.budget_exceeded = true;
    }
    return result;
}
CaptureResourceClosure
PlanCaptureResourceReplacement(const CaptureResourceGraph &graph,
                               const std::vector<uint64_t> &seeds,
                               const CaptureResourceLimits &limits)
{
    return TraceCaptureResources(graph, seeds, CaptureResourceTrace::Influence,
                                 limits);
}
} // namespace xemu::shader_browser

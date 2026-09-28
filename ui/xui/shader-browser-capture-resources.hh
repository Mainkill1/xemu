// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

namespace xemu::shader_browser {

// These are capture-local IDs. The adapter reserves a fresh allocation ID for
// each backing incarnation, including address reuse and private host backing.
// Addresses, content digests, block deduplication, and engine object ownership
// do not establish allocation identity. Only an adapter-proven alias joins it.
struct CaptureResourceRange {
    uint64_t offset = 0, size = 0;
};
enum class CaptureResourceProvenance : uint8_t {
    Known,
    MissingProducer,
    AmbiguousAlias,
    UnknownView,
    InvalidRange,
    RetiredAllocation,
    ProducerNotRetained,
    UnknownCoverage,
};
struct CaptureResourceVersion {
    uint64_t domain_id = 0;
    uint64_t allocation_id = 0, version = 0, reset_generation = 0;
    uint64_t producer_event = 0, producer_sequence = 0;
    uint32_t producer_operation = 0;
    CaptureResourceRange range;
    CaptureResourceProvenance provenance =
        CaptureResourceProvenance::MissingProducer;
    // Optional immutable byte evidence supplied by the adapter. These fields
    // describe the original snapshot extent even when range is later split.
    // This model does not own/validate payload bytes or infer identity from
    // them.
    uint64_t snapshot_block = 0;
    CaptureResourceRange snapshot_range;
    std::array<uint8_t, 16> content_digest{};
};
struct CaptureResourceAlias {
    uint64_t allocation_id = 0;
    // Canonical allocation bytes mapped by the view.
    CaptureResourceRange range;
};
enum class CaptureResourceOperationType : uint8_t {
    Allocate,
    Alias,
    RemoveAlias,
    Read,
    PartialWrite,
    FullWrite,
    Release,
    Reset,
    UncertainWrite,
};
struct CaptureResourceOperation {
    CaptureResourceOperationType type = CaptureResourceOperationType::Read;
    uint64_t allocation_id = 0, view_id = 0;
    uint64_t byte_size = 0, guest_address = 0;
    CaptureResourceRange range; // Relative to the view; unused for FullWrite.
    bool alias_proven = true;
    std::vector<CaptureResourceAlias> aliases;
    uint64_t snapshot_block = 0;
    std::array<uint8_t, 16> content_digest{};
    uint32_t kind = 0, slot = 0, flags = 0;
};
struct CaptureResourceAccess {
    CaptureResourceOperationType type;
    uint64_t allocation_id = 0, view_id = 0, guest_address = 0;
    CaptureResourceRange range;
    std::vector<CaptureResourceAlias> aliases;
    std::vector<CaptureResourceVersion> reads, writes;
    uint32_t kind = 0, slot = 0, flags = 0;
};
struct CaptureResourceEvent {
    uint64_t event_id = 0, sequence = 0, reset_generation = 0;
    std::vector<CaptureResourceAccess> accesses;
    uint64_t domain_id = 0;
};
struct CaptureResourceCommand {
    uint64_t event_id = 0;
    std::vector<CaptureResourceOperation> operations;
};
using SharedCaptureResourceEvent = std::shared_ptr<const CaptureResourceEvent>;

struct CaptureResourceLimits {
    size_t max_events = 8192, max_allocations = 16384, max_views = 32768;
    size_t max_fragments = 262144, max_accesses_per_event = 256;
    size_t max_references_per_event = 16384, max_edges = 262144;
    size_t max_job_events = 8192, max_job_work = 1048576;
    uint64_t byte_budget = 64U * 1024U * 1024U;
};
enum class CaptureResourceModelStatus : uint8_t {
    Ready,
    InvalidInput,
    BudgetExceeded,
};

// One timeline belongs to one capture context; serialize its domain_id with
// every event/reference. Combining unrelated capture domains is rejected.
// Domain IDs are process-local: isolate or remap independently recorded archive
// domains when loading several files into one library.
// Apply runs on the serialized capture thread, in actual command order, using
// strictly increasing execution sequences. Read-before-write operations
// describe blended or read/modify/write draws. Write coverage must be proven by
// the adapter; uncertain coverage must use an unproven alias, never a guessed
// full write. Events remain immutable after Apply and survive allocation reuse,
// reset, and eviction. Budget/invalid-order failure stops admission without
// partial writes.
class CaptureResourceTimeline {
public:
    explicit CaptureResourceTimeline(const CaptureResourceLimits & = {});
    ~CaptureResourceTimeline();
    CaptureResourceTimeline(const CaptureResourceTimeline &) = delete;
    CaptureResourceTimeline &
    operator=(const CaptureResourceTimeline &) = delete;
    // Allocate reserved IDs in increasing order; released IDs cannot return.
    uint64_t ReserveAllocationId();
    uint64_t ReserveViewId();
    SharedCaptureResourceEvent
    Apply(uint64_t event_id, const std::vector<CaptureResourceOperation> &);
    // Commands are supplied in proven execution order, independently of their
    // CPU observation IDs. The complete batch commits atomically. Additional
    // allowance bounds conservative transaction memory and retained growth;
    // allocation failure rolls back, stops admission and throws bad_alloc.
    std::vector<SharedCaptureResourceEvent>
    ApplyBatch(const std::vector<CaptureResourceCommand> &,
               uint64_t additional_byte_budget = UINT64_MAX);
    void EvictBefore(uint64_t sequence);
    void RetainEvents(const std::vector<uint64_t> &event_ids);
    std::vector<uint64_t> LiveProducerEvents() const;
    std::vector<SharedCaptureResourceEvent> Events() const;
    CaptureResourceModelStatus Status() const;
    uint64_t AccountedBytes() const;
    uint64_t DomainId() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

struct CaptureResourceDependency {
    uint64_t producer_event = 0, consumer_event = 0;
    uint64_t allocation_id = 0, version = 0;
    CaptureResourceRange range;
    uint32_t access_index = 0, fragment_index = 0;
    // This edge conservatively follows a version whose write coverage was
    // unobservable. Its gap remains explicit; it is not exact byte causation.
    bool uncertain = false;
};
struct CaptureResourceGap {
    uint64_t event_id = 0;
    uint32_t access_index = 0, fragment_index = 0;
    CaptureResourceProvenance reason;
    bool write = false;
};
struct CaptureResourceGraph {
    std::vector<SharedCaptureResourceEvent> events;
    std::vector<CaptureResourceDependency> edges;
    std::vector<CaptureResourceGap> gaps;
    std::unordered_map<uint64_t, size_t> event_index;
    // Dense event indexes. Each adjacency entry names an edge/gap index.
    std::vector<std::vector<size_t>> inputs, consumers;
    std::vector<std::vector<size_t>> gap_indexes;
    bool invalid_input = false, budget_exceeded = false;
    bool execution_order_complete = true;
};
// Supply only the selected retained range. Producers outside it are reported
// as missing retained evidence even if an owned input snapshot exists. The
// graph is indexed by allocation/version and event, never by address/hash.
CaptureResourceGraph
BuildCaptureResourceGraph(const std::vector<SharedCaptureResourceEvent> &,
                          const CaptureResourceLimits & = {});
enum class CaptureResourceTrace : uint8_t { Inputs, Influence };
struct CaptureResourceClosure {
    // Includes seeds, sorted by actual event sequence for execution planning.
    std::vector<uint64_t> events;
    std::vector<CaptureResourceVersion> external_inputs, invalidated_outputs;
    std::vector<CaptureResourceGap> gaps;
    bool provenance_complete = false, invalid_input = false;
    bool budget_exceeded = false;
};
CaptureResourceClosure
TraceCaptureResources(const CaptureResourceGraph &,
                      const std::vector<uint64_t> &seeds, CaptureResourceTrace,
                      const CaptureResourceLimits & = {});
// A replacement reruns seeds and every captured downstream consumer. Produced
// versions of those jobs are invalidated, and are never frozen external inputs.
// Unaffected external versions may be reused only after payload validation by
// the executor. Provenance completeness is not exact replay/pixel causation.
CaptureResourceClosure
PlanCaptureResourceReplacement(const CaptureResourceGraph &,
                               const std::vector<uint64_t> &seeds,
                               const CaptureResourceLimits & = {});

} // namespace xemu::shader_browser

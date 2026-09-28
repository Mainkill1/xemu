// SPDX-License-Identifier: GPL-2.0-or-later
#include "../../ui/xui/shader-browser-capture-resources.hh"

#include <algorithm>
#include <cstdlib>
#include <iostream>

using namespace xemu::shader_browser;

#define CHECK(condition)                                         \
    do {                                                         \
        if (!(condition)) {                                      \
            std::cerr << __LINE__ << ": " << #condition << "\n"; \
            std::exit(1);                                        \
        }                                                        \
    } while (0)

static CaptureResourceOperation Op(CaptureResourceOperationType type,
                                   uint64_t view = 0,
                                   CaptureResourceRange range = {})
{
    CaptureResourceOperation result;
    result.type = type;
    result.view_id = view;
    result.range = range;
    return result;
}
struct Allocation {
    uint64_t allocation, view;
};
static Allocation Allocate(CaptureResourceTimeline &timeline, uint64_t event,
                           uint64_t bytes = 16, uint64_t address = 0x1000)
{
    Allocation result{ timeline.ReserveAllocationId(),
                       timeline.ReserveViewId() };
    auto allocation = Op(CaptureResourceOperationType::Allocate);
    allocation.allocation_id = result.allocation;
    allocation.byte_size = bytes;
    allocation.guest_address = address;
    auto alias = Op(CaptureResourceOperationType::Alias, result.view);
    alias.aliases = { { result.allocation, { 0, bytes } } };
    CHECK(timeline.Apply(event, { allocation, alias }));
    return result;
}
static SharedCaptureResourceEvent Write(CaptureResourceTimeline &timeline,
                                        uint64_t event, uint64_t view,
                                        CaptureResourceRange range = {})
{
    auto operation =
        Op(range.size ? CaptureResourceOperationType::PartialWrite :
                        CaptureResourceOperationType::FullWrite,
           view, range);
    operation.snapshot_block = 17;
    operation.content_digest[0] = 99;
    auto result = timeline.Apply(event, { operation });
    CHECK(result);
    return result;
}
static SharedCaptureResourceEvent Read(CaptureResourceTimeline &timeline,
                                       uint64_t event, uint64_t view,
                                       CaptureResourceRange range = { 0, 16 })
{
    auto result = timeline.Apply(
        event, { Op(CaptureResourceOperationType::Read, view, range) });
    CHECK(result);
    return result;
}
static void TestOverwriteAndReplacement()
{
    CaptureResourceTimeline timeline;
    auto texture = Allocate(timeline, 1);
    auto first_write = Write(timeline, 2, texture.view);
    auto first_read = Read(timeline, 3, texture.view);
    Write(timeline, 4, texture.view);
    auto last_read = Read(timeline, 5, texture.view);
    CHECK(first_read->accesses[0].reads[0].producer_event == 2);
    CHECK(last_read->accesses[0].reads[0].producer_event == 4);
    CHECK(first_read->accesses[0].reads[0].version !=
          last_read->accesses[0].reads[0].version);
    CHECK(first_write->accesses[0].writes[0].snapshot_block == 17);
    auto graph = BuildCaptureResourceGraph(timeline.Events());
    CHECK(!graph.invalid_input && !graph.budget_exceeded && graph.gaps.empty());
    CHECK(graph.edges.size() == 2);
    auto replacement = PlanCaptureResourceReplacement(graph, { 2 });
    CHECK(replacement.provenance_complete);
    CHECK(replacement.events == std::vector<uint64_t>({ 2, 3 }));
    CHECK(replacement.external_inputs.empty());
    CHECK(replacement.invalidated_outputs.size() == 1);
}
static void TestAddressReuseAndEqualContent()
{
    CaptureResourceTimeline timeline;
    auto old = Allocate(timeline, 1);
    Write(timeline, 2, old.view);
    auto remembered = Read(timeline, 3, old.view);
    auto release = Op(CaptureResourceOperationType::Release);
    release.allocation_id = old.allocation;
    CHECK(timeline.Apply(4, { release }));
    auto reused = Allocate(timeline, 5);
    CHECK(old.allocation != reused.allocation);
    auto unknown = Read(timeline, 6, reused.view);
    CHECK(unknown->accesses[0].reads[0].provenance ==
          CaptureResourceProvenance::MissingProducer);
    Write(timeline, 7, reused.view); // Same digest and deduplicated bytes.
    Read(timeline, 8, reused.view);
    auto stale = Read(timeline, 9, old.view);
    CHECK(stale->accesses[0].reads[0].provenance ==
          CaptureResourceProvenance::RetiredAllocation);
    CHECK(remembered->accesses[0].reads[0].allocation_id == old.allocation);
    auto graph = BuildCaptureResourceGraph(timeline.Events());
    auto influenced = PlanCaptureResourceReplacement(graph, { 2 });
    CHECK(influenced.events == std::vector<uint64_t>({ 2, 3 }));
}
static void TestAliasAndPartialCoverage()
{
    CaptureResourceTimeline timeline;
    auto backing = Allocate(timeline, 1);
    Write(timeline, 2, backing.view);
    uint64_t subview = timeline.ReserveViewId();
    auto alias = Op(CaptureResourceOperationType::Alias, subview);
    alias.aliases = { { backing.allocation, { 4, 8 } } };
    CHECK(timeline.Apply(3, { alias }));
    Write(timeline, 4, subview, { 2, 4 });
    auto read = Read(timeline, 5, backing.view);
    const auto &fragments = read->accesses[0].reads;
    CHECK(fragments.size() == 3);
    CHECK(fragments[0].range.offset == 0 && fragments[0].range.size == 6);
    CHECK(fragments[1].range.offset == 6 && fragments[1].range.size == 4);
    CHECK(fragments[2].range.offset == 10 && fragments[2].range.size == 6);
    CHECK(fragments[0].producer_event == 2 &&
          fragments[1].producer_event == 4 && fragments[2].producer_event == 2);
    CHECK(fragments[2].snapshot_range.size == 16);
    auto graph = BuildCaptureResourceGraph(timeline.Events());
    auto closure =
        TraceCaptureResources(graph, { 5 }, CaptureResourceTrace::Inputs);
    CHECK(closure.events == std::vector<uint64_t>({ 2, 4, 5 }));
    CHECK(closure.provenance_complete);
}
static void TestAmbiguousAliasPoisoning()
{
    CaptureResourceTimeline timeline;
    auto first = Allocate(timeline, 1);
    auto second = Allocate(timeline, 2);
    Write(timeline, 3, first.view);
    Write(timeline, 4, second.view);
    auto ambiguous =
        Op(CaptureResourceOperationType::Alias, timeline.ReserveViewId());
    ambiguous.alias_proven = false;
    ambiguous.aliases = { { first.allocation, { 0, 16 } },
                          { second.allocation, { 0, 16 } } };
    CHECK(timeline.Apply(5, { ambiguous }));
    Write(timeline, 6, ambiguous.view_id, { 4, 4 });
    auto read = Read(timeline, 7, first.view);
    CHECK(read->accesses[0].reads.size() == 3);
    CHECK(read->accesses[0].reads[1].provenance ==
          CaptureResourceProvenance::AmbiguousAlias);
    auto other = Read(timeline, 8, second.view);
    CHECK(other->accesses[0].reads[1].provenance ==
          CaptureResourceProvenance::AmbiguousAlias);
    auto graph = BuildCaptureResourceGraph(timeline.Events());
    auto closure =
        TraceCaptureResources(graph, { 7 }, CaptureResourceTrace::Inputs);
    CHECK(!closure.provenance_complete && !closure.gaps.empty());
    CHECK(closure.events == std::vector<uint64_t>({ 3, 7 }));
}
static void TestEvictedProducerAndReset()
{
    CaptureResourceTimeline timeline;
    auto allocation = Allocate(timeline, 1);
    Write(timeline, 2, allocation.view);
    auto retained = Read(timeline, 3, allocation.view);
    timeline.EvictBefore(3);
    auto graph = BuildCaptureResourceGraph(timeline.Events());
    CHECK(graph.edges.empty() && graph.gaps.size() == 1);
    CHECK(graph.gaps[0].reason ==
          CaptureResourceProvenance::ProducerNotRetained);
    CHECK(!TraceCaptureResources(graph, { 3 }, CaptureResourceTrace::Inputs)
               .provenance_complete);
    CHECK(timeline.Apply(4, { Op(CaptureResourceOperationType::Reset) }));
    auto stale = Read(timeline, 5, allocation.view);
    CHECK(stale->accesses[0].reads[0].provenance ==
          CaptureResourceProvenance::UnknownView);
    auto next = Allocate(timeline, 6);
    CHECK(next.allocation != allocation.allocation);
    CHECK(retained->accesses[0].reads[0].producer_event == 2);
}
static void TestReplacementRecomputesConsumers()
{
    CaptureResourceTimeline timeline;
    auto target = Allocate(timeline, 1);
    auto composite = Allocate(timeline, 2);
    Write(timeline, 3, target.view);
    CHECK(timeline.Apply(
        4, { Op(CaptureResourceOperationType::Read, target.view, { 0, 16 }),
             Op(CaptureResourceOperationType::FullWrite, composite.view) }));
    Read(timeline, 5, composite.view);
    auto graph = BuildCaptureResourceGraph(timeline.Events());
    auto replacement = PlanCaptureResourceReplacement(graph, { 3 });
    CHECK(replacement.events == std::vector<uint64_t>({ 3, 4, 5 }));
    CHECK(replacement.invalidated_outputs.size() == 2);
    CHECK(replacement.external_inputs.empty());
    CHECK(replacement.provenance_complete);
}
static void TestBudgetsAndAtomicAdmission()
{
    CaptureResourceLimits limits;
    limits.max_events = 2;
    CaptureResourceTimeline timeline(limits);
    auto allocation = Allocate(timeline, 1);
    auto immutable = Write(timeline, 2, allocation.view);
    CHECK(!timeline.Apply(
        3, { Op(CaptureResourceOperationType::FullWrite, allocation.view) }));
    CHECK(timeline.Status() == CaptureResourceModelStatus::BudgetExceeded);
    CHECK(timeline.Events().size() == 2);
    CHECK(immutable->accesses[0].writes[0].version == 1);
    auto graph = BuildCaptureResourceGraph(timeline.Events());
    limits.max_job_work = 1;
    auto bounded = PlanCaptureResourceReplacement(graph, { 2 }, limits);
    CHECK(bounded.budget_exceeded && !bounded.provenance_complete);
}
static void TestIndexedQueryAndReadWriteOrder()
{
    CaptureResourceTimeline timeline;
    auto allocation = Allocate(timeline, 1);
    Write(timeline, 2, allocation.view);
    for (uint64_t event = 3; event <= 202; ++event)
        Read(timeline, event, allocation.view);
    auto graph = BuildCaptureResourceGraph(timeline.Events());
    CaptureResourceLimits query_limits;
    query_limits.max_job_work = 10;
    auto leaf = PlanCaptureResourceReplacement(graph, { 202 }, query_limits);
    CHECK(leaf.provenance_complete && !leaf.budget_exceeded);
    CHECK(leaf.events == std::vector<uint64_t>({ 202 }));
    CHECK(leaf.external_inputs.size() == 1);

    CHECK(timeline.Apply(
        203,
        {
            Op(CaptureResourceOperationType::PartialWrite, 0, { 0, 1 }),
            Op(CaptureResourceOperationType::PartialWrite, allocation.view,
               { 0, 4 }),
            Op(CaptureResourceOperationType::Read, allocation.view, { 0, 16 }),
        }));
    auto ordered = timeline.Events().back();
    const auto &reads = ordered->accesses[2].reads;
    CHECK(reads.size() == 2);
    CHECK(reads[0].provenance == CaptureResourceProvenance::Known);
    CHECK(reads[0].producer_operation == 1);
    CHECK(reads[1].provenance == CaptureResourceProvenance::UnknownView);
    graph = BuildCaptureResourceGraph(timeline.Events());
    CHECK(!graph.invalid_input && !graph.gaps.empty());
}
static void TestInvalidRangesAndEscapedOwnership()
{
    CaptureResourceTimeline timeline;
    auto allocation = Allocate(timeline, 1);
    auto held = Write(timeline, 2, allocation.view);
    timeline.EvictBefore(3);
    uint64_t with_owner = timeline.AccountedBytes();
    CHECK(held->accesses[0].writes[0].producer_event == 2);
    held.reset();
    CHECK(timeline.AccountedBytes() < with_owner);
    auto invalid = Read(timeline, 3, allocation.view, { UINT64_MAX, 2 });
    CHECK(invalid->accesses[0].reads[0].provenance ==
          CaptureResourceProvenance::InvalidRange);
    Write(timeline, 4, allocation.view, { 15, 2 });
    auto poisoned = Read(timeline, 5, allocation.view);
    CHECK(poisoned->accesses[0].reads.size() == 1);
    CHECK(poisoned->accesses[0].reads[0].provenance ==
          CaptureResourceProvenance::InvalidRange);

    CaptureResourceLimits tiny;
    tiny.byte_budget = 1;
    CaptureResourceTimeline bounded(tiny);
    CHECK(!bounded.Apply(1, {}));
    CHECK(bounded.AccountedBytes() == 0);
    CHECK(bounded.Events().empty());
}
static void TestObservedInputsAndMalformedGraph()
{
    CaptureResourceTimeline timeline;
    auto allocation = Allocate(timeline, 1);
    auto read =
        Op(CaptureResourceOperationType::Read, allocation.view, { 4, 4 });
    read.snapshot_block = 99;
    read.content_digest[0] = 7;
    auto observation = timeline.Apply(2, { read });
    CHECK(observation);
    const auto &input = observation->accesses[0].reads[0];
    CHECK(input.snapshot_block == 99 && input.snapshot_range.offset == 4 &&
          input.snapshot_range.size == 4);
    CHECK(input.provenance == CaptureResourceProvenance::MissingProducer);
    auto graph = BuildCaptureResourceGraph(timeline.Events());
    CHECK(!TraceCaptureResources(graph, { 2 }, CaptureResourceTrace::Inputs)
               .provenance_complete);

    Write(timeline, 3, allocation.view);
    auto consumer = Read(timeline, 4, allocation.view);
    auto malformed = std::make_shared<CaptureResourceEvent>(*consumer);
    malformed->accesses[0].reads[0].producer_event = 12345;
    auto events = timeline.Events();
    events.back() = malformed;
    CHECK(BuildCaptureResourceGraph(events).invalid_input);
    events.push_back(events.front());
    CHECK(BuildCaptureResourceGraph(events).invalid_input);
    CHECK(PlanCaptureResourceReplacement(graph, { 999 }).invalid_input);
}
static void TestCaptureDomainIsolation()
{
    CaptureResourceTimeline first, second;
    Allocate(first, 1);
    Allocate(second, 1);
    auto other = std::make_shared<CaptureResourceEvent>(*second.Events()[0]);
    other->event_id = 2;
    other->sequence = 2;
    auto events = first.Events();
    events.push_back(other);
    CHECK(BuildCaptureResourceGraph(events).invalid_input);
}
static void TestBatchOrderAndAtomicFailure()
{
    CaptureResourceTimeline timeline;
    auto buffer = Allocate(timeline, 1);
    auto batch = timeline.ApplyBatch(
        { { 30, { Op(CaptureResourceOperationType::FullWrite, buffer.view) } },
          { 10,
            { Op(CaptureResourceOperationType::Read, buffer.view,
                 { 0, 16 }) } } });
    CHECK(batch.size() == 2);
    CHECK(batch[0]->sequence < batch[1]->sequence);
    CHECK(batch[1]->accesses[0].reads[0].producer_event == 30);
    CHECK(!BuildCaptureResourceGraph(timeline.Events()).invalid_input);
    auto held = batch[1];
    const auto bytes = timeline.AccountedBytes();
    const auto events = timeline.Events().size();
    CHECK(
        timeline
            .ApplyBatch(
                { { 40,
                    { Op(CaptureResourceOperationType::FullWrite,
                         buffer.view) } },
                  { 41,
                    { Op(static_cast<CaptureResourceOperationType>(255)) } } })
            .empty());
    CHECK(timeline.Events().size() == events);
    CHECK(timeline.AccountedBytes() == bytes);
    CHECK(held->accesses[0].reads[0].producer_event == 30);
}

static void TestBatchBudgetAndInterleavedSubmission()
{
    CaptureResourceTimeline timeline;
    auto buffer = Allocate(timeline, 1);
    Write(timeline, 30, buffer.view);
    auto main = timeline.ApplyBatch({ { 10,
                                        { Op(CaptureResourceOperationType::Read,
                                             buffer.view, { 0, 16 }) } } });
    CHECK(main.size() == 1 &&
          main[0]->accesses[0].reads[0].producer_event == 30);
    CHECK(!BuildCaptureResourceGraph(timeline.Events()).invalid_input);
    const auto bytes = timeline.AccountedBytes();
    const auto count = timeline.Events().size();
    CHECK(timeline
              .ApplyBatch({ { 40,
                              { Op(CaptureResourceOperationType::FullWrite,
                                   buffer.view) } } },
                          1)
              .empty());
    CHECK(timeline.Status() == CaptureResourceModelStatus::BudgetExceeded);
    CHECK(timeline.AccountedBytes() == bytes &&
          timeline.Events().size() == count);
}

int main()
{
    TestBatchBudgetAndInterleavedSubmission();
    TestBatchOrderAndAtomicFailure();
    TestOverwriteAndReplacement();
    TestAddressReuseAndEqualContent();
    TestAliasAndPartialCoverage();
    TestAmbiguousAliasPoisoning();
    TestEvictedProducerAndReset();
    TestReplacementRecomputesConsumers();
    TestBudgetsAndAtomicAdmission();
    TestIndexedQueryAndReadWriteOrder();
    TestInvalidRangesAndEscapedOwnership();
    TestObservedInputsAndMalformedGraph();
    TestCaptureDomainIsolation();
    std::cout
        << "capture resource versions/dependency closures: 13 cases passed\n";
}

// SPDX-License-Identifier: GPL-2.0-or-later
#include "../../ui/xui/shader-browser-capture-session.hh"

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <fstream>
#include <nlohmann/json.hpp>

using namespace xemu::shader_browser;
#define CHECK(x)                                   \
    do {                                           \
        if (!(x)) {                                \
            std::cerr << __LINE__ << ": " #x "\n"; \
            std::abort();                          \
        }                                          \
    } while (0)

static CaptureSessionContext Context()
{
    CaptureSessionContext context;
    context.scope.title_id = 17;
    context.scope_generation = 1;
    context.generation = 4;
    context.session_epoch = 2;
    context.renderer_epoch = 3;
    return context;
}
static uint64_t Begin(CaptureSession &session, uint64_t frame,
                      CaptureEventType type = CaptureEventType::Draw)
{
    DrawCaptureSummary summary;
    summary.scope = Context().scope;
    summary.key = { 2, 3, frame, 1, 1 };
    const auto token = session.BeginOccurrence(summary, type);
    if (!token) {
        const auto snapshot = session.Snapshot();
        std::cerr << "Begin frame " << frame << ": " << snapshot.reason
                  << "; bytes " << snapshot.cpu_bytes << "/"
                  << snapshot.settings.cpu_byte_budget << "; events "
                  << snapshot.events.size() << "/"
                  << snapshot.settings.event_budget << "\n";
    }
    CHECK(token);
    return token;
}
static void Finish(CaptureSession &session, uint64_t token, bool emitted = true)
{
    CHECK(session.Finish(token, emitted, 5, 3, 0));
    CHECK(session.InputsComplete(token));
}
static void Observe(CaptureSession &session, uint64_t token, uint64_t owner,
                    uint32_t access, uint64_t offset = 0, uint64_t size = 64)
{
    XemuShaderCaptureResource resource{
        owner, 64, offset, size, access, 1, 0, 0
    };
    CHECK(session.StageResource(token, resource));
}

static void test_owner_versions_archive_and_dependency_retention()
{
    CaptureSession session;
    CaptureSessionSettings settings;
    settings.mode = CaptureSessionMode::RollingAnimation;
    settings.history_frames = 1;
    CHECK(session.Start(Context(), settings));
    auto upload = Begin(session, 1, CaptureEventType::Upload);
    Observe(session, upload, 100, XEMU_SHADER_CAPTURE_RESOURCE_FULL_WRITE);
    Finish(session, upload);
    auto first = Begin(session, 2);
    Observe(session, first, 100, XEMU_SHADER_CAPTURE_RESOURCE_READ);
    Finish(session, first);
    session.GuestFrameBoundary(2);
    CHECK(session.Find(upload & ~kCaptureSessionTokenBit));
    auto overwrite = Begin(session, 2, CaptureEventType::Copy);
    Observe(session, overwrite, 100, XEMU_SHADER_CAPTURE_RESOURCE_PARTIAL_WRITE,
            16, 16);
    Finish(session, overwrite);
    auto second = Begin(session, 3);
    Observe(session, second, 100, XEMU_SHADER_CAPTURE_RESOURCE_READ);
    Finish(session, second);
    session.GuestFrameBoundary(3);
    session.Stop();
    auto snapshot = session.Snapshot();
    auto graph = BuildCaptureSessionResourceGraph(snapshot);
    CHECK(!graph.invalid_input && !graph.budget_exceeded);
    auto closure = TraceCaptureResources(
        graph, { second & ~kCaptureSessionTokenBit },
        CaptureResourceTrace::Inputs, CaptureSessionResourceLimits(settings));
    CHECK(closure.provenance_complete && closure.events.size() == 3);
    CHECK(closure.events.front() == (upload & ~kCaptureSessionTokenBit));
    const auto path = std::filesystem::temp_directory_path() /
                      "xemu-resource-session-archive-test";
    std::filesystem::remove_all(path);
    snapshot.annotations = "resource graph annotation survives";
    std::string error;
    CHECK(CaptureSession::SaveSnapshot(snapshot, path, &error));
    CaptureSessionSnapshot reopened;
    CHECK(CaptureSession::Reopen(path, &reopened, &error));
    CHECK(reopened.annotations == snapshot.annotations);
    auto reopened_graph = BuildCaptureSessionResourceGraph(reopened);
    CHECK(reopened_graph.edges.size() == graph.edges.size());
    CHECK(!reopened_graph.invalid_input && !reopened_graph.budget_exceeded);
    auto plan = PlanCaptureResourceReplacement(
        reopened_graph, { overwrite & ~kCaptureSessionTokenBit },
        CaptureSessionResourceLimits(settings));
    CHECK(plan.events.size() == 2 && plan.invalidated_outputs.size() == 1);
    for (const auto &input : plan.external_inputs)
        CHECK(input.producer_event != (overwrite & ~kCaptureSessionTokenBit));
    std::filesystem::remove_all(path);
}

static void test_unknown_coverage_suppression_and_owner_reuse()
{
    CaptureSession session;
    CaptureSessionSettings settings;
    settings.mode = CaptureSessionMode::RollingAnimation;
    CHECK(session.Start(Context(), settings));
    auto initial = Begin(session, 1, CaptureEventType::Upload);
    Observe(session, initial, 200, XEMU_SHADER_CAPTURE_RESOURCE_FULL_WRITE);
    Finish(session, initial);
    auto suppressed = Begin(session, 1);
    Observe(session, suppressed, 200,
            XEMU_SHADER_CAPTURE_RESOURCE_UNCERTAIN_WRITE);
    Finish(session, suppressed, false);
    auto raster = Begin(session, 1);
    Observe(session, raster, 200, XEMU_SHADER_CAPTURE_RESOURCE_READ);
    Observe(session, raster, 200, XEMU_SHADER_CAPTURE_RESOURCE_UNCERTAIN_WRITE);
    Finish(session, raster);
    auto consumer = Begin(session, 1);
    Observe(session, consumer, 200, XEMU_SHADER_CAPTURE_RESOURCE_READ);
    Finish(session, consumer);
    auto fresh = Begin(session, 1);
    Observe(session, fresh, 201, XEMU_SHADER_CAPTURE_RESOURCE_READ);
    Finish(session, fresh);
    session.Stop();
    auto graph = BuildCaptureSessionResourceGraph(session.Snapshot());
    auto plan = PlanCaptureResourceReplacement(
        graph, { raster & ~kCaptureSessionTokenBit },
        CaptureSessionResourceLimits(settings));
    CHECK(!plan.provenance_complete && plan.events.size() == 2);
    auto a =
        session.Find(initial & ~kCaptureSessionTokenBit)->resource_evidence;
    auto b = session.Find(fresh & ~kCaptureSessionTokenBit)->resource_evidence;
    CHECK(a->accesses.back().writes[0].allocation_id !=
          b->accesses.back().reads[0].allocation_id);
    CHECK(session.Find(suppressed & ~kCaptureSessionTokenBit)
              ->resource_evidence->accesses.empty());
}

static void test_observed_large_event_count_is_indexed()
{
    CaptureSession session;
    CaptureSessionSettings settings;
    settings.mode = CaptureSessionMode::RollingAnimation;
    settings.event_budget = 30000;
    settings.post_trigger_reserve_percent = 0; // Exercise the full index bound.
    CHECK(session.Start(Context(), settings));
    auto upload = Begin(session, 1, CaptureEventType::Upload);
    Observe(session, upload, 300, XEMU_SHADER_CAPTURE_RESOURCE_FULL_WRITE);
    Finish(session, upload);
    uint64_t last = 0;
    for (unsigned i = 1; i < 25570; ++i) {
        last = Begin(session, 1);
        Observe(session, last, 300, XEMU_SHADER_CAPTURE_RESOURCE_READ);
        Finish(session, last);
    }
    session.Stop();
    auto graph = BuildCaptureSessionResourceGraph(session.Snapshot());
    CHECK(!graph.invalid_input && !graph.budget_exceeded &&
          graph.events.size() == 25570);
    CHECK(graph.edges.size() == 25569);
    auto limits = CaptureSessionResourceLimits(settings);
    limits.max_job_work = 16;
    auto closure =
        TraceCaptureResources(graph, { last & ~kCaptureSessionTokenBit },
                              CaptureResourceTrace::Inputs, limits);
    CHECK(closure.provenance_complete && closure.events.size() == 2);
}
static void test_owned_upload_snapshot_release_and_invalid_archive()
{
    CaptureSession session;
    CaptureSessionSettings settings;
    settings.mode = CaptureSessionMode::RollingAnimation;
    CHECK(session.Start(Context(), settings));
    auto upload = Begin(session, 1, CaptureEventType::Upload);
    uint8_t bytes[16] = { 7, 8, 9 };
    XemuShaderDrawBlob blob{};
    blob.name = "resource.buffer.upload";
    blob.data = bytes;
    blob.byte_count = sizeof(bytes);
    CHECK(session.StageBlob(upload, blob));
    XemuShaderCaptureResource write{ 400,
                                     64,
                                     16,
                                     16,
                                     XEMU_SHADER_CAPTURE_RESOURCE_PARTIAL_WRITE,
                                     XEMU_SHADER_CAPTURE_RESOURCE_BUFFER,
                                     0,
                                     0 };
    CHECK(session.StageResource(upload, write, blob.name));
    bytes[0] = 0;
    Finish(session, upload);
    auto consumer = Begin(session, 1);
    write.access = XEMU_SHADER_CAPTURE_RESOURCE_READ;
    CHECK(session.StageResource(consumer, write));
    Finish(session, consumer);
    auto captured = session.Find(consumer & ~kCaptureSessionTokenBit);
    const auto &ref = captured->resource_evidence->accesses.back().reads[0];
    CHECK(ref.snapshot_block && ref.snapshot_range.offset == 16 &&
          ref.snapshot_range.size == 16);
    CHECK(session.Find(upload & ~kCaptureSessionTokenBit)
              ->inputs.blobs[0]
              .data->bytes[0] == 7);
    session.ReleaseResource(400, 64, XEMU_SHADER_CAPTURE_RESOURCE_BUFFER, 0);
    auto stale = Begin(session, 1);
    CHECK(session.StageResource(stale, write));
    Finish(session, stale);
    CHECK(session.Find(stale & ~kCaptureSessionTokenBit)
              ->resource_evidence->accesses.back()
              .reads[0]
              .provenance == CaptureResourceProvenance::RetiredAllocation);
    session.Stop();
    auto snapshot = session.Snapshot();
    const auto path = std::filesystem::temp_directory_path() /
                      "xemu-resource-owned-upload-test";
    std::filesystem::remove_all(path);
    std::string error;
    CHECK(CaptureSession::SaveSnapshot(snapshot, path, &error));
    CaptureSessionSnapshot reopened;
    CHECK(CaptureSession::Reopen(path, &reopened, &error));
    CHECK(reopened.events[0]->inputs.blobs[0].data->bytes[0] == 7);
    std::filesystem::remove_all(path);
    auto changed = std::make_shared<CaptureOccurrence>(*snapshot.events[0]);
    auto evidence =
        std::make_shared<CaptureResourceEvent>(*changed->resource_evidence);
    evidence->accesses.back().writes[0].producer_sequence += 10;
    changed->resource_evidence = evidence;
    snapshot.events[0] = changed;
    CHECK(!CaptureSession::SaveSnapshot(snapshot, path, &error));
    CHECK(!error.empty() && !std::filesystem::exists(path));
}

static void test_deduplicated_bytes_keep_distinct_backing_identity()
{
    CaptureSession session;
    CaptureSessionSettings settings;
    settings.mode = CaptureSessionMode::RollingAnimation;
    CHECK(session.Start(Context(), settings));
    uint8_t bytes[16] = { 1, 2, 3 };
    uint64_t uploads[2];
    for (unsigned i = 0; i < 2; ++i) {
        uploads[i] = Begin(session, 1, CaptureEventType::Upload);
        XemuShaderDrawBlob blob{};
        blob.name = "resource.buffer.upload";
        blob.data = bytes;
        blob.byte_count = sizeof(bytes);
        CHECK(session.StageBlob(uploads[i], blob));
        XemuShaderCaptureResource write{
            500 + i,
            sizeof(bytes),
            0,
            sizeof(bytes),
            XEMU_SHADER_CAPTURE_RESOURCE_FULL_WRITE,
            XEMU_SHADER_CAPTURE_RESOURCE_BUFFER,
            0,
            0
        };
        CHECK(session.StageResource(uploads[i], write, blob.name));
        Finish(session, uploads[i]);
    }
    auto a = session.Find(uploads[0] & ~kCaptureSessionTokenBit);
    auto b = session.Find(uploads[1] & ~kCaptureSessionTokenBit);
    CHECK(a->inputs.blobs[0].data == b->inputs.blobs[0].data);
    CHECK(a->resource_evidence->accesses.back().writes[0].allocation_id !=
          b->resource_evidence->accesses.back().writes[0].allocation_id);
    auto consumer = Begin(session, 1);
    XemuShaderCaptureResource read{ 501,
                                    sizeof(bytes),
                                    0,
                                    sizeof(bytes),
                                    XEMU_SHADER_CAPTURE_RESOURCE_READ,
                                    XEMU_SHADER_CAPTURE_RESOURCE_BUFFER,
                                    0,
                                    0 };
    CHECK(session.StageResource(consumer, read));
    Finish(session, consumer);
    session.Stop();
    auto graph = BuildCaptureSessionResourceGraph(session.Snapshot());
    CHECK(!graph.invalid_input && graph.edges.size() == 1);
    CHECK(graph.edges[0].producer_event == b->event_id);
    auto plan = PlanCaptureResourceReplacement(graph, { a->event_id });
    CHECK(plan.provenance_complete && plan.events.size() == 1);
}

static void test_late_readback_keeps_bound_version_and_immutable_evidence()
{
    CaptureSession session;
    CaptureSessionSettings settings;
    settings.mode = CaptureSessionMode::RollingAnimation;
    CHECK(session.Start(Context(), settings));
    auto upload = Begin(session, 1, CaptureEventType::Upload);
    Observe(session, upload, 600, XEMU_SHADER_CAPTURE_RESOURCE_FULL_WRITE);
    Finish(session, upload);
    auto draw = Begin(session, 1);
    Observe(session, draw, 600, XEMU_SHADER_CAPTURE_RESOURCE_READ, 16, 16);
    CHECK(session.Finish(draw, true, 5, 3, 0));
    auto old_evidence =
        session.Find(draw & ~kCaptureSessionTokenBit)->resource_evidence;
    const auto old_version = old_evidence->accesses.back().reads[0].version;
    auto overwrite = Begin(session, 1, CaptureEventType::Upload);
    Observe(session, overwrite, 600, XEMU_SHADER_CAPTURE_RESOURCE_FULL_WRITE);
    Finish(session, overwrite);
    uint8_t bytes[16] = { 9, 8, 7 };
    XemuShaderDrawBlob blob{};
    blob.name = "vertex.attribute0";
    blob.data = bytes;
    blob.byte_count = sizeof(bytes);
    blob.offset = 16;
    CHECK(session.StageBlob(draw, blob));
    CHECK(session.AttachResourceReadSnapshot(draw, 1, 0, blob.name));
    CHECK(session.InputsComplete(draw));
    auto captured = session.Find(draw & ~kCaptureSessionTokenBit);
    const auto &read = captured->resource_evidence->accesses.back().reads[0];
    CHECK(read.version == old_version);
    CHECK(read.producer_event == (upload & ~kCaptureSessionTokenBit));
    CHECK(read.snapshot_block == captured->inputs.blobs[0].data->id);
    CHECK(read.snapshot_range.offset == 16 && read.snapshot_range.size == 16);
    CHECK(!old_evidence->accesses.back().reads[0].snapshot_block);
    bytes[0] = 0;
    CHECK(captured->inputs.blobs[0].data->bytes[0] == 9);
    session.Stop();
    auto snapshot = session.Snapshot();
    auto graph = BuildCaptureSessionResourceGraph(snapshot);
    CHECK(!graph.invalid_input && graph.edges.size() == 1);
    CHECK(graph.edges[0].producer_event == (upload & ~kCaptureSessionTokenBit));
    const auto path = std::filesystem::temp_directory_path() /
                      "xemu-resource-late-readback-test";
    std::filesystem::remove_all(path);
    std::string error;
    CHECK(CaptureSession::SaveSnapshot(snapshot, path, &error));
    CaptureSessionSnapshot reopened;
    CHECK(CaptureSession::Reopen(path, &reopened, &error));
    CHECK(!BuildCaptureSessionResourceGraph(reopened).invalid_input);
    std::filesystem::remove_all(path);
}

static void test_owned_input_snapshot_preserves_missing_producer()
{
    CaptureSession session;
    CaptureSessionSettings settings;
    settings.mode = CaptureSessionMode::RollingAnimation;
    CHECK(session.Start(Context(), settings));
    auto draw = Begin(session, 1);
    uint8_t bytes[16] = { 4, 5, 6 };
    XemuShaderDrawBlob blob{};
    blob.name = "vertex.indices";
    blob.data = bytes;
    blob.byte_count = sizeof(bytes);
    CHECK(session.StageBlob(draw, blob));
    XemuShaderCaptureResource resource{ 700,
                                        64,
                                        0,
                                        sizeof(bytes),
                                        XEMU_SHADER_CAPTURE_RESOURCE_READ,
                                        XEMU_SHADER_CAPTURE_RESOURCE_BUFFER,
                                        16,
                                        0 };
    CHECK(session.StageResource(draw, resource, blob.name));
    Finish(session, draw);
    session.Stop();
    auto snapshot = session.Snapshot();
    const auto &read =
        snapshot.events[0]->resource_evidence->accesses.back().reads[0];
    CHECK(read.provenance == CaptureResourceProvenance::MissingProducer);
    CHECK(read.snapshot_block == snapshot.events[0]->inputs.blobs[0].data->id);
    auto graph = BuildCaptureSessionResourceGraph(snapshot);
    auto closure =
        TraceCaptureResources(graph, { draw & ~kCaptureSessionTokenBit },
                              CaptureResourceTrace::Inputs);
    CHECK(!closure.invalid_input && !closure.provenance_complete);
    CHECK(closure.gaps.size() == 1 &&
          closure.gaps[0].reason == CaptureResourceProvenance::MissingProducer);
    auto plan = PlanCaptureResourceReplacement(
        graph, { draw & ~kCaptureSessionTokenBit });
    CHECK(!plan.invalid_input && !plan.provenance_complete);
    const auto path = std::filesystem::temp_directory_path() /
                      "xemu-resource-missing-producer-snapshot-test";
    std::filesystem::remove_all(path);
    std::string error;
    CHECK(CaptureSession::SaveSnapshot(snapshot, path, &error));
    CaptureSessionSnapshot reopened;
    CHECK(CaptureSession::Reopen(path, &reopened, &error));
    CHECK(reopened.events[0]->CopyInputs().blobs[0].bytes[0] == 4);
    auto reopened_graph = BuildCaptureSessionResourceGraph(reopened);
    CHECK(!reopened_graph.invalid_input && reopened_graph.gaps.size() == 1);
    CHECK(reopened_graph.gaps[0].reason ==
          CaptureResourceProvenance::MissingProducer);
    std::filesystem::remove_all(path);
}

static void test_batch_order_lifecycle_and_roundtrip()
{
    CaptureSession session;
    CaptureSessionSettings settings;
    settings.mode = CaptureSessionMode::RollingAnimation;
    settings.history_frames = 1;
    CHECK(session.Start(Context(), settings));
    uint64_t run = 0;
    session.Context(&run);
    const auto batch = session.BeginBatch(run);
    CHECK(batch);
    auto copy = Begin(session, 1, CaptureEventType::Copy);
    CHECK(session.HoldForBatch(batch, copy));
    Observe(session, copy, 800, XEMU_SHADER_CAPTURE_RESOURCE_READ);
    Observe(session, copy, 801, XEMU_SHADER_CAPTURE_RESOURCE_FULL_WRITE);
    auto draw = Begin(session, 1);
    CHECK(session.HoldForBatch(batch, draw));
    Observe(session, draw, 801, XEMU_SHADER_CAPTURE_RESOURCE_READ);
    auto upload = Begin(session, 1, CaptureEventType::Upload);
    CHECK(session.HoldForBatch(batch, upload));
    uint8_t data[64] = { 19 };
    XemuShaderDrawBlob blob{};
    blob.name = "resource.buffer.upload";
    blob.data = data;
    blob.byte_count = sizeof(data);
    CHECK(session.StageBlob(upload, blob));
    XemuShaderCaptureResource write{
        800, 64, 0, 64, XEMU_SHADER_CAPTURE_RESOURCE_FULL_WRITE, 1, 0, 0
    };
    CHECK(session.StageResource(upload, write, blob.name));
    CHECK(session.RecordBatchCommand(batch, upload,
                                     CaptureCommandPhase::HostPreparation, 1));
    CHECK(session.RecordBatchCommand(batch, copy,
                                     CaptureCommandPhase::Auxiliary, 1));
    CHECK(
        session.RecordBatchCommand(batch, draw, CaptureCommandPhase::Main, 1));
    CHECK(
        !session.RecordBatchCommand(batch, draw, CaptureCommandPhase::Main, 2));
    Finish(session, copy);
    Finish(session, draw);
    Finish(session, upload);
    auto old = session.Find(draw & ~kCaptureSessionTokenBit);
    CHECK(old->pending && !old->resource_evidence);
    CHECK(session.Find(upload & ~kCaptureSessionTokenBit)
              ->inputs.blobs[0]
              .data->bytes[0] == 19);
    session.GuestFrameBoundary(4);
    CHECK(session.Find(upload & ~kCaptureSessionTokenBit));
    CHECK(session.SubmitBatch(batch, true, 1));
    auto submitted = session.Find(draw & ~kCaptureSessionTokenBit);
    CHECK(submitted->pending &&
          submitted->submission == CaptureBatchOutcome::Submitted);
    CHECK(
        submitted->resource_evidence->accesses.back().reads[0].producer_event ==
        (copy & ~kCaptureSessionTokenBit));
    CHECK(old->pending && !old->resource_evidence);
    CHECK(session.RetireBatch(batch, true));
    session.Stop();
    auto snapshot = session.Snapshot();
    CHECK(snapshot.state == CaptureSessionState::Ready &&
          !snapshot.pending_events);
    auto graph = BuildCaptureSessionResourceGraph(snapshot);
    CHECK(!graph.invalid_input && graph.edges.size() == 2);
    auto closure =
        TraceCaptureResources(graph, { draw & ~kCaptureSessionTokenBit },
                              CaptureResourceTrace::Inputs);
    CHECK(closure.provenance_complete);
    CHECK(closure.events ==
          std::vector<uint64_t>({ upload & ~kCaptureSessionTokenBit,
                                  copy & ~kCaptureSessionTokenBit,
                                  draw & ~kCaptureSessionTokenBit }));
    const auto path =
        std::filesystem::temp_directory_path() / "xemu-resource-batch-test";
    std::filesystem::remove_all(path);
    std::string error;
    CHECK(CaptureSession::SaveSnapshot(snapshot, path, &error));
    CaptureSessionSnapshot reopened;
    CHECK(CaptureSession::Reopen(path, &reopened, &error));
    CHECK(reopened.execution_order_complete);
    CHECK(!BuildCaptureSessionResourceGraph(reopened).invalid_input);
    CHECK(reopened.events.front()->event_id ==
          (copy & ~kCaptureSessionTokenBit));
    auto corrupted =
        std::make_shared<CaptureOccurrence>(*reopened.events.front());
    corrupted->command_phase = CaptureCommandPhase::Main;
    reopened.events.front() = corrupted;
    CHECK(BuildCaptureSessionResourceGraph(reopened).invalid_input);
    std::filesystem::remove_all(path);
    CHECK(session.Start(Context(), settings));
    const auto fresh = session.BeginBatch();
    CHECK(fresh && fresh != batch);
    auto fresh_draw = Begin(session, 1);
    CHECK(session.HoldForBatch(fresh, fresh_draw));
    CHECK(session.Find(fresh_draw & ~kCaptureSessionTokenBit)->batch_id == 1);
    Finish(session, fresh_draw, false);
    CHECK(!session.SubmitBatch(batch, false, 2));
    CHECK(!session.RetireBatch(batch, false));
    CHECK(!session.AbortBatch(batch));
    CHECK(!session.BeginBatch(run));
    CHECK(session.Active());
    CHECK(session.AbortBatch(fresh));
}

static void test_batch_failure_preserves_owned_evidence()
{
    CaptureSession session;
    CaptureSessionSettings settings;
    settings.mode = CaptureSessionMode::RollingAnimation;
    CHECK(session.Start(Context(), settings));
    auto batch = session.BeginBatch();
    auto draw = Begin(session, 1);
    CHECK(session.HoldForBatch(batch, draw));
    Observe(session, draw, 900, XEMU_SHADER_CAPTURE_RESOURCE_FULL_WRITE);
    CHECK(
        session.RecordBatchCommand(batch, draw, CaptureCommandPhase::Main, 1));
    Finish(session, draw);
    CHECK(session.SubmitBatch(batch, false, 0, -4));
    auto failed = session.Find(draw & ~kCaptureSessionTokenBit);
    CHECK(!failed->pending &&
          failed->submission == CaptureBatchOutcome::SubmissionFailed);
    CHECK(!failed->resource_evidence);
    CHECK(failed->backend_result == -4);
    CHECK(session.Active());
    auto pending = session.BeginBatch();
    auto token = Begin(session, 1);
    CHECK(session.HoldForBatch(pending, token));
    Finish(session, token);
    session.Cancel();
    CHECK(session.Find(token & ~kCaptureSessionTokenBit)->submission ==
          CaptureBatchOutcome::Detached);
    CHECK(!session.SubmitBatch(pending, true, 2));
}

static void
test_failed_submission_keeps_host_upload_and_failed_fence_keeps_submission()
{
    CaptureSession session;
    CaptureSessionSettings settings;
    settings.mode = CaptureSessionMode::RollingAnimation;
    CHECK(session.Start(Context(), settings));
    auto batch = session.BeginBatch();
    auto host = Begin(session, 1, CaptureEventType::Upload);
    CHECK(session.HoldForBatch(batch, host));
    Observe(session, host, 950, XEMU_SHADER_CAPTURE_RESOURCE_FULL_WRITE);
    CHECK(session.RecordBatchCommand(batch, host,
                                     CaptureCommandPhase::HostPreparation, 1));
    Finish(session, host);
    auto copy = Begin(session, 1, CaptureEventType::Copy);
    CHECK(session.HoldForBatch(batch, copy));
    Observe(session, copy, 950, XEMU_SHADER_CAPTURE_RESOURCE_READ);
    Observe(session, copy, 951, XEMU_SHADER_CAPTURE_RESOURCE_FULL_WRITE);
    CHECK(session.RecordBatchCommand(batch, copy,
                                     CaptureCommandPhase::Auxiliary, 1));
    CHECK(session.DescribeCommand(
        copy, { CaptureCommandKind::BufferCopy, 0, 0, 64 }));
    Finish(session, copy);
    CHECK(session.SubmitBatch(batch, false, 0, -4));
    CHECK(session.Find(host & ~kCaptureSessionTokenBit)->resource_evidence);
    auto failed = session.Find(copy & ~kCaptureSessionTokenBit);
    CHECK(!failed->resource_evidence && failed->command.bytes == 64);
    auto consumer = Begin(session, 1);
    Observe(session, consumer, 950, XEMU_SHADER_CAPTURE_RESOURCE_READ);
    Finish(session, consumer);
    CHECK(session.Find(consumer & ~kCaptureSessionTokenBit)
              ->resource_evidence->accesses.back()
              .reads[0]
              .producer_event == (host & ~kCaptureSessionTokenBit));
    batch = session.BeginBatch();
    auto draw = Begin(session, 1);
    CHECK(session.HoldForBatch(batch, draw));
    Observe(session, draw, 950, XEMU_SHADER_CAPTURE_RESOURCE_READ);
    CHECK(
        session.RecordBatchCommand(batch, draw, CaptureCommandPhase::Main, 1));
    CHECK(session.Finish(draw, true, 5, 3, 0));
    auto suppressed = Begin(session, 1);
    CHECK(session.HoldForBatch(batch, suppressed));
    Observe(session, suppressed, 950, XEMU_SHADER_CAPTURE_RESOURCE_FULL_WRITE);
    Finish(session, suppressed, false);
    CHECK(session.SubmitBatch(batch, true, 2));
    CHECK(!session.Find(suppressed & ~kCaptureSessionTokenBit)
               ->resource_evidence);
    auto before_fence = session.Find(draw & ~kCaptureSessionTokenBit);
    CHECK(session.RetireBatch(batch, false, -4));
    CHECK(session.Find(draw & ~kCaptureSessionTokenBit)->pending);
    CHECK(session.InputsComplete(draw));
    auto retired = session.Find(draw & ~kCaptureSessionTokenBit);
    CHECK(!retired->pending &&
          retired->submission == CaptureBatchOutcome::Submitted);
    CHECK(retired->completion == CaptureBatchCompletion::Failed &&
          retired->completion_result == -4);
    CHECK(retired->resource_evidence == before_fence->resource_evidence);
    CHECK(before_fence->completion == CaptureBatchCompletion::Pending);
}

static uint64_t StageBudgetBatch(CaptureSession &session,
                                 const CaptureSessionSettings &settings)
{
    CHECK(session.Start(Context(), settings));
    auto batch = session.BeginBatch();
    auto draw = Begin(session, 1);
    CHECK(session.HoldForBatch(batch, draw));
    for (unsigned i = 0; i < 8; ++i)
        Observe(session, draw, 1000 + i,
                XEMU_SHADER_CAPTURE_RESOURCE_FULL_WRITE);
    CHECK(
        session.RecordBatchCommand(batch, draw, CaptureCommandPhase::Main, 1));
    Finish(session, draw);
    return batch;
}
static void test_batch_budget_failure_has_no_partial_publication()
{
    CaptureSession calibration;
    CaptureSessionSettings settings;
    settings.mode = CaptureSessionMode::RollingAnimation;
    // Isolate the total transaction budget; pre-trigger quotas have their own
    // tests below.
    settings.post_trigger_reserve_percent = 0;
    StageBudgetBatch(calibration, settings);
    settings.cpu_byte_budget = calibration.Snapshot().cpu_bytes + 128;
    CaptureSession session;
    auto batch = StageBudgetBatch(session, settings);
    auto held = session.Snapshot().events[0];
    CHECK(!session.SubmitBatch(batch, true, 1));
    CHECK(session.Snapshot().state == CaptureSessionState::BudgetExceeded);
    CHECK(session.Snapshot().events[0]->submission ==
          CaptureBatchOutcome::Submitted);
    CHECK(!session.Snapshot().events[0]->resource_evidence);
    CHECK(session.RetireBatch(batch, true));
    CHECK(!session.Snapshot().pending_events);
    CHECK(!held->resource_evidence && held->pending);
    CHECK(session.Snapshot().cpu_bytes <= settings.cpu_byte_budget);
}

static void test_file_cancel_and_legacy_order_are_explicit()
{
    CaptureSession session;
    CaptureSessionSettings settings;
    settings.mode = CaptureSessionMode::RollingAnimation;
    CHECK(session.Start(Context(), settings));
    auto token = Begin(session, 1, CaptureEventType::Upload);
    Observe(session, token, 1100, XEMU_SHADER_CAPTURE_RESOURCE_FULL_WRITE);
    Finish(session, token);
    session.Stop();
    const auto path = std::filesystem::temp_directory_path() /
                      "xemu-resource-file-control-test";
    std::filesystem::remove_all(path);
    std::string error;
    CaptureFileControl save;
    CHECK(save.RequestCancel());
    CHECK(!session.Save(path, &error, &save));
    CHECK(!std::filesystem::exists(path) && save.Progress().finished &&
          !save.Progress().success);
    CHECK(session.Save(path, &error));
    CaptureFileControl reopen;
    CHECK(reopen.RequestCancel());
    CaptureSessionSnapshot result;
    CHECK(!CaptureSession::Reopen(path, &result, &error, &reopen));
    CHECK(result.events.empty() && reopen.Progress().finished &&
          !reopen.Progress().success);
    nlohmann::json metadata;
    std::ifstream(path / "metadata.json") >> metadata;
    metadata["version"] = 2;
    metadata.erase("execution_order_complete");
    std::ofstream(path / "metadata.json", std::ios::trunc) << metadata.dump();
    CHECK(CaptureSession::Reopen(path, &result, &error));
    CHECK(!result.execution_order_complete);
    auto graph = BuildCaptureSessionResourceGraph(result);
    CHECK(!graph.execution_order_complete);
    CHECK(!TraceCaptureResources(graph, { token & ~kCaptureSessionTokenBit },
                                 CaptureResourceTrace::Inputs)
               .provenance_complete);
    std::filesystem::remove_all(path);
}

static void test_archive_rejects_contradictory_execution_receipts()
{
    CaptureSession session;
    CaptureSessionSettings settings;
    settings.mode = CaptureSessionMode::RollingAnimation;
    CHECK(session.Start(Context(), settings));
    auto token = Begin(session, 1, CaptureEventType::AllocationBoundary);
    Finish(session, token, false);
    session.Stop();
    const auto path = std::filesystem::temp_directory_path() /
                      "xemu-resource-execution-receipt-test";
    std::filesystem::remove_all(path);
    std::string error;
    CHECK(session.Save(path, &error));
    nlohmann::json metadata, page;
    std::ifstream(path / "metadata.json") >> metadata;
    std::ifstream(path / "events" / "0.json") >> page;
    metadata["version"] = 1;
    metadata["events"] = page;
    metadata.erase("event_pages");
    metadata.erase("execution_order_complete");
    const auto valid = metadata;
    CaptureSessionSnapshot result;
    auto reopen = [&](const nlohmann::json &value) {
        std::ofstream(path / "metadata.json", std::ios::trunc) << value.dump();
        return CaptureSession::Reopen(path, &result, &error);
    };
    CHECK(reopen(valid));
    // Preserve old archives which have no execution description at all.
    auto legacy = valid;
    legacy["events"][0].erase("execution");
    CHECK(reopen(legacy) && !result.execution_order_complete);
    for (unsigned malformed = 0; malformed < 11; ++malformed) {
        auto changed = valid;
        auto &execution = changed["events"][0]["execution"];
        if (malformed < 4) {
            execution[0] = 1;
            execution[5] = uint32_t(CaptureBatchCompletion::Completed);
            if (malformed == 1) {
                execution[1] = uint32_t(CaptureCommandPhase::HostPreparation);
                execution[2] = 1;
                execution[3] = true;
            } else if (malformed >= 2) {
                execution[2] = 1;
                execution[3] = true;
                execution[4] = uint32_t(CaptureBatchOutcome::Submitted);
                if (malformed == 3)
                    execution[1] =
                        uint32_t(CaptureCommandPhase::HostPreparation);
            }
        } else {
            // An unbatched legacy or synchronous occurrence cannot carry a
            // queue receipt, completion, ordinal, or backend result.
            const unsigned field[] = { 2, 3, 4, 5, 6, 7, 9 };
            execution[field[malformed - 4]] = field[malformed - 4] == 3 ?
                                                  nlohmann::json(true) :
                                                  nlohmann::json(1);
        }
        CHECK(!reopen(changed));
        CHECK(result.events.empty() && !error.empty());
    }
    std::filesystem::remove_all(path);
}

static void test_logical_replay_description_is_owned_bounded_and_archived()
{
    CaptureSession session;
    CaptureSessionSettings settings;
    settings.mode = CaptureSessionMode::RollingAnimation;
    CHECK(session.Start(Context(), settings));
    auto token = Begin(session, 1, CaptureEventType::Copy);
    XemuShaderCaptureReplayDescription description{};
    description.kind = XEMU_SHADER_CAPTURE_COMMAND_IMAGE_COPY;
    description.binding_count = 1;
    description.width = 3;
    description.height = 4;
    description.bindings[0].role = XEMU_SHADER_CAPTURE_REPLAY_COPY_SOURCE;
    description.bindings[0].resource.kind = XEMU_SHADER_CAPTURE_RESOURCE_COLOR;
    description.bindings[0].image.width = 3;
    description.bindings[0].image.height = 4;
    description.bindings[0].image.format =
        XEMU_SHADER_CAPTURE_REPLAY_RGBA8_UNORM;
    description.bindings[0].image.samples = 4; // Preserve unsupported evidence.
    const auto before = session.Snapshot().cpu_bytes;
    CHECK(session.DescribeReplay(token, description));
    CHECK(session.Snapshot().cpu_bytes > before);
    description.width = 99;
    CHECK(!session.DescribeReplay(token, description));
    Finish(session, token, false);
    CHECK(session.Find(token & ~kCaptureSessionTokenBit)
              ->replay_description->width == 3);
    CHECK(!session.DescribeReplay(token, description));
    session.Stop();
    const auto path = std::filesystem::temp_directory_path() /
                      "xemu-resource-logical-description-test";
    std::filesystem::remove_all(path);
    std::string error;
    CHECK(session.Save(path, &error));
    CaptureSessionSnapshot result;
    CHECK(CaptureSession::Reopen(path, &result, &error));
    CHECK(result.events[0]->replay_description &&
          result.events[0]->replay_description->width == 3 &&
          result.events[0]->replay_description->bindings[0].image.samples == 4);
    CHECK(session.Start(Context(), settings));
    CHECK(!session.DescribeReplay(token, description));
    const auto fresh = Begin(session, 1);
    description.binding_count = XEMU_SHADER_CAPTURE_REPLAY_MAX_BINDINGS + 1;
    CHECK(!session.DescribeReplay(fresh, description));
    CHECK(session.Active());
    std::filesystem::remove_all(path);
}

static void test_host_preparation_is_visible_to_interleaved_auxiliary_batch()
{
    CaptureSession session;
    CaptureSessionSettings settings;
    settings.mode = CaptureSessionMode::RollingAnimation;
    settings.history_frames = 1;
    CHECK(session.Start(Context(), settings));
    const auto main_batch = session.BeginBatch();
    auto main_copy = Begin(session, 1, CaptureEventType::Copy);
    CHECK(session.HoldForBatch(main_batch, main_copy));
    Observe(session, main_copy, 1200, XEMU_SHADER_CAPTURE_RESOURCE_READ);
    Observe(session, main_copy, 1201, XEMU_SHADER_CAPTURE_RESOURCE_FULL_WRITE);
    CHECK(session.RecordBatchCommand(main_batch, main_copy,
                                     CaptureCommandPhase::Auxiliary, 1));
    Finish(session, main_copy);
    auto draw = Begin(session, 1);
    CHECK(session.HoldForBatch(main_batch, draw));
    Observe(session, draw, 1201, XEMU_SHADER_CAPTURE_RESOURCE_READ);
    CHECK(session.RecordBatchCommand(main_batch, draw,
                                     CaptureCommandPhase::Main, 1));
    Finish(session, draw);
    auto upload = Begin(session, 1, CaptureEventType::Upload);
    CHECK(session.HoldForBatch(main_batch, upload));
    Observe(session, upload, 1200, XEMU_SHADER_CAPTURE_RESOURCE_FULL_WRITE);
    CHECK(session.RecordBatchCommand(main_batch, upload,
                                     CaptureCommandPhase::HostPreparation, 1));
    Finish(session, upload);
    CHECK(!session.Find(upload & ~kCaptureSessionTokenBit)->pending);
    auto auxiliary_batch = session.BeginBatch();
    auto auxiliary_copy = Begin(session, 1, CaptureEventType::Copy);
    CHECK(session.HoldForBatch(auxiliary_batch, auxiliary_copy));
    Observe(session, auxiliary_copy, 1200, XEMU_SHADER_CAPTURE_RESOURCE_READ);
    Observe(session, auxiliary_copy, 1202,
            XEMU_SHADER_CAPTURE_RESOURCE_FULL_WRITE);
    CHECK(session.RecordBatchCommand(auxiliary_batch, auxiliary_copy,
                                     CaptureCommandPhase::Auxiliary, 1));
    Finish(session, auxiliary_copy);
    CHECK(session.SubmitBatch(auxiliary_batch, true, 1));
    CHECK(session.RetireBatch(auxiliary_batch, true));
    CHECK(session.Find(auxiliary_copy & ~kCaptureSessionTokenBit)
              ->resource_evidence->accesses.back()
              .writes[0]
              .producer_event == (auxiliary_copy & ~kCaptureSessionTokenBit));
    session.ReleaseResource(1202, 64, XEMU_SHADER_CAPTURE_RESOURCE_BUFFER, 0);
    // A later host overwrite removes the upload from live producer roots.
    // Unresolved batch membership still owns it across rolling eviction.
    auto overwrite = Begin(session, 1, CaptureEventType::Upload);
    Observe(session, overwrite, 1200, XEMU_SHADER_CAPTURE_RESOURCE_FULL_WRITE);
    Finish(session, overwrite);
    session.GuestFrameBoundary(4);
    CHECK(session.Find(upload & ~kCaptureSessionTokenBit));
    CHECK(session.SubmitBatch(main_batch, true, 2));
    CHECK(session.RetireBatch(main_batch, true));
    session.Stop();
    auto graph = BuildCaptureSessionResourceGraph(session.Snapshot());
    CHECK(!graph.invalid_input);
    CHECK(session.Find(main_copy & ~kCaptureSessionTokenBit)
              ->resource_evidence->accesses.front()
              .reads[0]
              .producer_event == (overwrite & ~kCaptureSessionTokenBit));
}

static CaptureSessionSettings ReserveSettings()
{
    CaptureSessionSettings settings;
    settings.mode = CaptureSessionMode::RollingAnimation;
    settings.history_frames = 60;
    settings.post_frames = 1;
    settings.post_trigger_reserve_percent = 50;
    return settings;
}
static void
test_post_reserve_evicts_whole_unpinned_frames_and_mark_releases_it()
{
    CaptureSession session;
    auto settings = ReserveSettings();
    settings.event_budget = 8;
    CHECK(session.Start(Context(), settings));
    session.GuestFrameBoundary(1);
    const auto old = Begin(session, 1);
    Finish(session, old);
    session.GuestFrameBoundary(2);
    Finish(session, Begin(session, 2));
    Finish(session, Begin(session, 2));
    CHECK(session.Active());
    CHECK(session.Snapshot().events.size() <= 4);
    CHECK(!session.Find(old & ~kCaptureSessionTokenBit));
    CHECK(session.Snapshot().retained_frames == std::vector<uint64_t>{ 2 });
    CHECK(session.Mark());
    for (unsigned i = 0; i < 3; ++i)
        Finish(session, Begin(session, 2));
    session.GuestFrameBoundary(3);
    Finish(session, Begin(session, 3));
    CHECK(session.Snapshot().events.size() == 8);
    session.GuestFrameBoundary(4);
    CHECK(session.Snapshot().state == CaptureSessionState::Ready);
    CHECK(
        (session.Snapshot().retained_frames == std::vector<uint64_t>{ 2, 3 }));
}
static void test_post_byte_reserve_tracks_owned_blocks_and_dedup()
{
    CaptureSession session;
    auto settings = ReserveSettings();
    settings.cpu_byte_budget = 64U * 1024U;
    CHECK(session.Start(Context(), settings));
    std::vector<char> bytes(16U * 1024U, 'a');
    session.GuestFrameBoundary(1);
    const auto old = Begin(session, 1);
    CHECK(session.StageSource(old, 2, bytes.data(), bytes.size()));
    Finish(session, old);
    const auto current = Begin(session, 1);
    // Identical immutable bytes consume one retained block.
    CHECK(session.StageSource(current, 2, bytes.data(), bytes.size()));
    Finish(session, current);
    CHECK(session.Snapshot().unique_blocks == 1);
    session.GuestFrameBoundary(2);
    const auto fresh = Begin(session, 2);
    bytes[0] = 'b';
    CHECK(session.StageSource(fresh, 2, bytes.data(), bytes.size()));
    Finish(session, fresh);
    CHECK(session.Snapshot().cpu_bytes <= 32U * 1024U);
    CHECK(!session.Find(old & ~kCaptureSessionTokenBit));
    CHECK(session.Mark());
    const auto post = Begin(session, 3);
    bytes[0] = 'c';
    CHECK(session.StageSource(post, 2, bytes.data(), bytes.size()));
    Finish(session, post);
    CHECK(session.Snapshot().cpu_bytes > 32U * 1024U &&
          session.Snapshot().cpu_bytes <= settings.cpu_byte_budget);
}
static void test_post_reserve_cannot_evict_pending_batch_or_dependency_prefix()
{
    {
        CaptureSession session;
        auto settings = ReserveSettings();
        settings.event_budget = 4;
        CHECK(session.Start(Context(), settings));
        session.GuestFrameBoundary(1);
        const auto batch = session.BeginBatch();
        const auto pending = Begin(session, 1, CaptureEventType::Copy);
        CHECK(batch && session.HoldForBatch(batch, pending));
        CHECK(session.ReservePayload(pending, 64));
        CHECK(session.Finish(pending, false, 0, 0, 0));
        session.GuestFrameBoundary(2);
        const auto snapshot = session.Snapshot();
        CHECK(snapshot.state == CaptureSessionState::BudgetExceeded);
        CHECK(snapshot.events.size() == 2 && snapshot.pending_events == 1 &&
              snapshot.reserved_bytes == 64);
        CHECK(session.Find(pending & ~kCaptureSessionTokenBit));
        CHECK(session.BatchCurrent(batch) && !session.Mark());
        CHECK(session.AbortBatch(batch));
        CHECK(!session.Snapshot().pending_events &&
              !session.Snapshot().reserved_bytes &&
              !session.InputsComplete(pending));
    }
    {
        CaptureSession session;
        auto settings = ReserveSettings();
        settings.event_budget = 6;
        CHECK(session.Start(Context(), settings));
        const auto upload = Begin(session, 1, CaptureEventType::Upload);
        Observe(session, upload, 1800, XEMU_SHADER_CAPTURE_RESOURCE_FULL_WRITE);
        Finish(session, upload);
        const auto consumer = Begin(session, 2);
        Observe(session, consumer, 1800, XEMU_SHADER_CAPTURE_RESOURCE_READ);
        Observe(session, consumer, 1801,
                XEMU_SHADER_CAPTURE_RESOURCE_FULL_WRITE);
        Finish(session, consumer);
        Finish(session, Begin(session, 2));
        session.GuestFrameBoundary(3);
        CHECK(session.Snapshot().state == CaptureSessionState::BudgetExceeded);
        CHECK(session.Find(upload & ~kCaptureSessionTokenBit) &&
              session.Find(consumer & ~kCaptureSessionTokenBit));
        const auto graph = BuildCaptureSessionResourceGraph(session.Snapshot());
        const auto closure = TraceCaptureResources(
            graph, { consumer & ~kCaptureSessionTokenBit },
            CaptureResourceTrace::Inputs);
        CHECK(closure.provenance_complete && closure.events.size() == 2);
        CHECK(!session.Mark());
    }
}
static void test_post_reserve_does_not_credit_externally_owned_evicted_bytes()
{
    CaptureSession session;
    auto settings = ReserveSettings();
    settings.cpu_byte_budget = 64U * 1024U;
    CHECK(session.Start(Context(), settings));
    std::vector<char> bytes(16U * 1024U, 'a');
    session.GuestFrameBoundary(1);
    const auto old = Begin(session, 1);
    CHECK(session.StageSource(old, 2, bytes.data(), bytes.size()));
    Finish(session, old);
    auto retained = session.Find(old & ~kCaptureSessionTokenBit);
    CHECK(retained && retained->inputs.sources[2]);
    session.GuestFrameBoundary(2);
    const auto current = Begin(session, 2);
    bytes[0] = 'b';
    CHECK(!session.StageSource(current, 2, bytes.data(), bytes.size()));
    CHECK(session.Snapshot().state == CaptureSessionState::BudgetExceeded);
    CHECK(!session.Find(old & ~kCaptureSessionTokenBit));
    CHECK(retained->inputs.sources[2]->bytes[0] == 'a');
    const auto before_release = session.Snapshot().cpu_bytes;
    retained.reset();
    CHECK(session.Snapshot().cpu_bytes + 16U * 1024U <= before_release);
    CHECK(!session.Mark());
}
static void test_post_reserve_all_metadata_admissions_share_the_byte_ceiling()
{
    for (unsigned admission = 0; admission < 4; ++admission) {
        CaptureSession session;
        auto settings = ReserveSettings();
        settings.cpu_byte_budget = 64U * 1024U;
        CHECK(session.Start(Context(), settings));
        const auto token = Begin(session, 1);
        CHECK(session.ReservePayload(token, 32U * 1024U -
                                                session.Snapshot().cpu_bytes));
        CHECK(session.Snapshot().cpu_bytes == 32U * 1024U);
        if (admission == 0)
            CHECK(!session.BeginBatch());
        else if (admission == 1)
            CHECK(!session.StageRegister(token, "quota", 1));
        else if (admission == 2) {
            XemuShaderCaptureReplayDescription description{};
            description.kind = uint32_t(CaptureCommandKind::Draw);
            CHECK(!session.DescribeReplay(token, description));
        } else {
            const char value = 'x';
            CHECK(!session.StageSource(token, 2, &value, 1));
        }
        CHECK(session.Snapshot().state == CaptureSessionState::BudgetExceeded);
        CHECK(session.Snapshot().cpu_bytes == 32U * 1024U);
    }
}
static void test_post_reserve_settings_legacy_compatibility_and_clear_bits()
{
    const auto path = std::filesystem::temp_directory_path() /
                      "xemu-post-reserve-settings-test";
    std::filesystem::remove_all(path);
    CaptureSession session;
    auto settings = ReserveSettings();
    CHECK(session.Start(Context(), settings));
    const auto token = Begin(session, 1, CaptureEventType::Clear);
    XemuShaderCaptureReplayDescription description{};
    description.kind = uint32_t(CaptureCommandKind::Clear);
    description.clear_color_bits[0] = 0x3f800000;
    description.clear_color_bits[1] = 0x7fc01234;
    description.clear_color_bits[2] = 0x80000000;
    description.clear_color_mask = 5;
    CHECK(session.DescribeReplay(token, description));
    Finish(session, token, false);
    session.Stop();
    std::string error;
    CHECK(session.Save(path, &error));
    CaptureSessionSnapshot reopened;
    CHECK(CaptureSession::Reopen(path, &reopened, &error));
    CHECK(reopened.settings.post_trigger_reserve_percent == 50);
    CHECK(reopened.events[0]->replay_description->clear_color_bits[0] ==
          0x3f800000);
    CHECK(reopened.events[0]->replay_description->clear_color_bits[1] ==
          0x7fc01234);
    CHECK(reopened.events[0]->replay_description->clear_color_bits[2] ==
          0x80000000);
    CHECK(reopened.events[0]->replay_description->clear_color_mask == 5);
    nlohmann::json metadata;
    std::ifstream(path / "metadata.json") >> metadata;
    metadata["settings"].erase("post_trigger_reserve_percent");
    std::ofstream(path / "metadata.json", std::ios::trunc) << metadata.dump();
    CHECK(CaptureSession::Reopen(path, &reopened, &error));
    CHECK(reopened.settings.post_trigger_reserve_percent == 0);
    nlohmann::json page;
    std::ifstream(path / "events" / "0.json") >> page;
    // Legacy inline transport lets this test exercise field validation rather
    // than fail the newer page digest before reaching the reader.
    metadata["version"] = 1;
    metadata["events"] = page;
    metadata.erase("event_pages");
    metadata.erase("execution_order_complete");
    metadata["events"][0]["replay_description"].erase("clear");
    std::ofstream(path / "metadata.json", std::ios::trunc) << metadata.dump();
    CHECK(CaptureSession::Reopen(path, &reopened, &error));
    CHECK(reopened.events[0]->replay_description->clear_color_mask == 0 &&
          reopened.events[0]->replay_description->clear_color_bits[0] == 0);
    metadata["events"][0]["replay_description"]["clear"] =
        nlohmann::json::array({ 0, 0 });
    std::ofstream(path / "metadata.json", std::ios::trunc) << metadata.dump();
    CHECK(!CaptureSession::Reopen(path, &reopened, &error));
    CHECK(reopened.events.empty());
    std::filesystem::remove_all(path);
    settings.post_trigger_reserve_percent = 100;
    CHECK(!session.Start(Context(), settings));
    settings.post_trigger_reserve_percent = 50;
    settings.event_budget = 1;
    CHECK(!session.Start(Context(), settings));
    for (unsigned bypass = 0; bypass < 3; ++bypass) {
        settings = ReserveSettings();
        settings.event_budget = 2;
        if (bypass == 0)
            settings.post_trigger_reserve_percent = 0;
        else if (bypass == 1)
            settings.post_frames = 0;
        else {
            settings.mode = CaptureSessionMode::NextFrame;
            settings.event_budget = 3;
        }
        CHECK(session.Start(Context(), settings));
        if (bypass == 2)
            session.GuestFrameBoundary(1);
        Finish(session, Begin(session, 1));
        Finish(session, Begin(session, 1));
        CHECK(session.Active());
    }
}

static void test_post_reserve_transaction_keeps_the_actual_submission_receipt()
{
    CaptureSession calibration;
    auto settings = ReserveSettings();
    StageBudgetBatch(calibration, settings);
    settings.cpu_byte_budget = 2 * (calibration.Snapshot().cpu_bytes + 128);
    CaptureSession session;
    const auto batch = StageBudgetBatch(session, settings);
    CHECK(!session.SubmitBatch(batch, true, 1));
    const auto snapshot = session.Snapshot();
    CHECK(snapshot.state == CaptureSessionState::BudgetExceeded);
    CHECK(snapshot.cpu_bytes <= settings.cpu_byte_budget / 2);
    CHECK(snapshot.events[0]->submission == CaptureBatchOutcome::Submitted &&
          snapshot.events[0]->queue_ordinal == 1);
    CHECK(!snapshot.events[0]->resource_evidence);
    CHECK(session.RetireBatch(batch, true));
    CHECK(!session.Snapshot().pending_events);
}
static void test_mark_preserves_pending_reservations_without_double_charging()
{
    CaptureSession session;
    auto settings = ReserveSettings();
    settings.cpu_byte_budget = 64U * 1024U;
    CHECK(session.Start(Context(), settings));
    const auto token = Begin(session, 1);
    std::vector<char> bytes(16U * 1024U, 'a');
    CHECK(session.ReservePayload(token, bytes.size()));
    const auto before = session.Snapshot().cpu_bytes;
    CHECK(session.Mark());
    CHECK(session.Snapshot().cpu_bytes == before &&
          session.Snapshot().reserved_bytes == bytes.size());
    CHECK(session.StageSource(token, 2, bytes.data(), bytes.size()));
    CHECK(session.Snapshot().cpu_bytes < before + 1024 &&
          !session.Snapshot().reserved_bytes);
    Finish(session, token);
    CHECK(session.Find(token & ~kCaptureSessionTokenBit)
              ->inputs.sources[2]
              ->bytes[0] == 'a');
}

int main()
{
    test_post_reserve_evicts_whole_unpinned_frames_and_mark_releases_it();
    test_post_byte_reserve_tracks_owned_blocks_and_dedup();
    test_post_reserve_cannot_evict_pending_batch_or_dependency_prefix();
    test_post_reserve_does_not_credit_externally_owned_evicted_bytes();
    test_post_reserve_all_metadata_admissions_share_the_byte_ceiling();
    test_post_reserve_settings_legacy_compatibility_and_clear_bits();
    test_post_reserve_transaction_keeps_the_actual_submission_receipt();
    test_mark_preserves_pending_reservations_without_double_charging();
    test_logical_replay_description_is_owned_bounded_and_archived();
    test_archive_rejects_contradictory_execution_receipts();
    test_host_preparation_is_visible_to_interleaved_auxiliary_batch();
    test_failed_submission_keeps_host_upload_and_failed_fence_keeps_submission();
    test_batch_budget_failure_has_no_partial_publication();
    test_file_cancel_and_legacy_order_are_explicit();
    test_batch_order_lifecycle_and_roundtrip();
    test_batch_failure_preserves_owned_evidence();
    test_owner_versions_archive_and_dependency_retention();
    test_unknown_coverage_suppression_and_owner_reuse();
    test_observed_large_event_count_is_indexed();
    test_owned_upload_snapshot_release_and_invalid_archive();
    test_deduplicated_bytes_keep_distinct_backing_identity();
    test_late_readback_keeps_bound_version_and_immutable_evidence();
    test_owned_input_snapshot_preserves_missing_producer();
    std::cout << "capture session resource integration: 23 cases passed\n";
}

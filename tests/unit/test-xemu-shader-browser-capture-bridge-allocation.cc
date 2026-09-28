// SPDX-License-Identifier: GPL-2.0-or-later
#include "../../ui/xui/shader-browser-capture-session.hh"
#include "../../ui/xui/shader-browser-draw-request.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <utility>

using namespace xemu::shader_browser;

#define CHECK(expression)                                           \
    do {                                                            \
        if (!(expression)) {                                        \
            std::fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, \
                         #expression);                              \
            std::abort();                                           \
        }                                                           \
    } while (0)

namespace {
enum class AllocationFailure { None, Once, Always };
thread_local AllocationFailure allocation_failure = AllocationFailure::None;
thread_local size_t failed_allocations = 0;
} // namespace

void *operator new(size_t bytes)
{
    if (allocation_failure != AllocationFailure::None) {
        if (allocation_failure == AllocationFailure::Once)
            allocation_failure = AllocationFailure::None;
        ++failed_allocations;
        throw std::bad_alloc();
    }
    if (void *allocation = std::malloc(bytes ? bytes : 1))
        return allocation;
    throw std::bad_alloc();
}
void *operator new[](size_t bytes)
{
    return ::operator new(bytes);
}
void operator delete(void *allocation) noexcept
{
    std::free(allocation);
}
void operator delete[](void *allocation) noexcept
{
    std::free(allocation);
}
void operator delete(void *allocation, size_t) noexcept
{
    std::free(allocation);
}
void operator delete[](void *allocation, size_t) noexcept
{
    std::free(allocation);
}

namespace {
struct ActiveCapture {
    CaptureSessionContext context;
    uint64_t pending_token;
    uint64_t pending_event;
    uint64_t revision;
    uint64_t total_events;
};

ActiveCapture StartCapture()
{
    xemu_shader_draw_request_cancel();
    auto &session = GetCaptureSession();
    CaptureSessionContext context;
    context.scope.title_id = 17;
    context.scope_generation = 1;
    context.session_epoch = 2;
    context.renderer_epoch = 3;
    context.generation = 4;
    CHECK(session.Start(context));
    session.GuestFrameBoundary(7);
    DrawCaptureSummary summary;
    summary.scope = context.scope;
    summary.key = { 2, 3, 7, 1, 1 };
    summary.shader_count = 1;
    summary.shaders[0].stage = Stage::Pixel;
    summary.shaders[0].hash.bytes[0] = 0x71;
    const uint64_t token = session.BeginOccurrence(summary);
    CHECK(token & kCaptureSessionTokenBit);
    CHECK(session.Finish(token, true, 5, 3, 0));
    CHECK(session.ReservePayload(token, 64));
    const auto snapshot = session.Snapshot();
    CHECK(snapshot.pending_events == 1 && snapshot.reserved_bytes == 64);
    CHECK(snapshot.events.size() == 2);
    CHECK(snapshot.events[0]->type == CaptureEventType::FrameBoundary);
    CHECK(snapshot.events[1]->type == CaptureEventType::Draw);
    CHECK(session.Active());
    return { context, token, snapshot.events[1]->event_id, session.Revision(),
             snapshot.total_events };
}

template <typename Callback> void FailOneAllocation(Callback &&callback)
{
    failed_allocations = 0;
    allocation_failure = AllocationFailure::Once;
    bool escaped = false;
    try {
        std::forward<Callback>(callback)();
    } catch (...) {
        escaped = true;
    }
    allocation_failure = AllocationFailure::None;
    CHECK(failed_allocations == 1);
    CHECK(!escaped);
}

void AssertAborted(const ActiveCapture &capture)
{
    auto &session = GetCaptureSession();
    CHECK(!session.Active());
    CHECK(session.Revision() > capture.revision);
    CHECK(!xemu_shader_draw_request_wants_inputs(capture.pending_token));
    const auto snapshot = session.Snapshot();
    CHECK(snapshot.state == CaptureSessionState::BudgetExceeded);
    CHECK(snapshot.pending_events == 0 && snapshot.reserved_bytes == 0);
    CHECK(snapshot.total_events == capture.total_events);
    CHECK(snapshot.events.size() == 2);
    CHECK(snapshot.events[0]->type == CaptureEventType::FrameBoundary);
    const auto &draw = *snapshot.events[1];
    CHECK(draw.type == CaptureEventType::Draw);
    CHECK(draw.event_id == capture.pending_event);
    CHECK(draw.finished && draw.emitted && !draw.pending);
    CHECK((draw.limitations & (CaptureReadbackFailed | CaptureInvalidated)) ==
          (CaptureReadbackFailed | CaptureInvalidated));
}

void TestClaimEpochMismatchAllocationFailure()
{
    const auto capture = StartCapture();
    XemuShaderDrawIdentity identity{};
    identity.stage = 2;
    identity.identity_hash[0] = 0x71;
    uint64_t token = UINT64_MAX;
    FailOneAllocation([&] {
        token = xemu_shader_draw_request_claim(
            capture.context.scope_generation,
            capture.context.renderer_epoch + 1, &identity, 1, 7, 2, 2);
    });
    CHECK(token == 0);
    AssertAborted(capture);
    std::puts("C claim invalidation allocation failure: PASS");
}

void TestBeginEventEpochMismatchAllocationFailure()
{
    const auto capture = StartCapture();
    uint64_t token = UINT64_MAX;
    FailOneAllocation([&] {
        token = xemu_shader_capture_session_begin_event(
            XEMU_SHADER_CAPTURE_CLEAR, 7, 2, 2,
            capture.context.scope_generation + 1,
            capture.context.renderer_epoch);
    });
    CHECK(token == 0);
    AssertAborted(capture);
    std::puts("C begin-event invalidation allocation failure: PASS");
}

void TestBudgetFailureMessageAllocationFailure()
{
    const auto capture = StartCapture();
    FailOneAllocation([&] {
        xemu_shader_capture_session_fail_budget(
            capture.pending_token,
            "Readback payload allocation exceeded the capture budget");
    });
    AssertAborted(capture);
    std::puts("C budget failure message allocation failure: PASS");
}

void TestAbortDoesNotAllocate()
{
    const auto capture = StartCapture();
    static_assert(
        noexcept(std::declval<CaptureSession &>().AbortAllocationFailure()));
    failed_allocations = 0;
    allocation_failure = AllocationFailure::Always;
    GetCaptureSession().AbortAllocationFailure();
    allocation_failure = AllocationFailure::None;
    CHECK(failed_allocations == 0);
    AssertAborted(capture);
    std::puts("Capture allocation abort does not allocate: PASS");
}

void AssertStillRecording(const ActiveCapture &capture)
{
    auto &session = GetCaptureSession();
    CHECK(session.Active());
    CHECK(session.Revision() == capture.revision);
    CHECK(session.WantsInputs(capture.pending_token));
    const auto snapshot = session.Snapshot();
    CHECK(snapshot.state == CaptureSessionState::Recording);
    CHECK(snapshot.pending_events == 1 && snapshot.reserved_bytes == 64);
    CHECK(snapshot.total_events == capture.total_events);
    CHECK(snapshot.events.size() == 2);
    CHECK(snapshot.events[1]->event_id == capture.pending_event);
    CHECK(snapshot.events[1]->pending);
    CHECK(!(snapshot.events[1]->limitations & CaptureInvalidated));
}

void TestStaleTokenFailureDoesNotAbortRearm()
{
    const auto old = StartCapture();
    const auto current = StartCapture();
    CHECK(!GetCaptureSession().WantsInputs(old.pending_token));
    FailOneAllocation([&] {
        xemu_shader_capture_session_fail_budget(
            old.pending_token,
            "A stale readback failed after an identical context was rearmed");
    });
    AssertStillRecording(current);
    GetCaptureSession().AbortAllocationFailure();
    std::puts("Stale C token allocation failure preserves rearm: PASS");
}

void TestStaleGenerationAbortDoesNotAbortRearm()
{
    const auto old = StartCapture();
    uint64_t old_run = 0;
    const auto old_context = GetCaptureSession().Context(&old_run);
    const auto current = StartCapture();
    uint64_t current_run = 0;
    const auto current_context = GetCaptureSession().Context(&current_run);
    CHECK(old_context.generation == current_context.generation);
    CHECK(old_run && old_run != current_run);
    failed_allocations = 0;
    allocation_failure = AllocationFailure::Always;
    GetCaptureSession().AbortAllocationFailure(old_run);
    GetCaptureSession().AbortAllocationFailure(0, old.pending_token);
    allocation_failure = AllocationFailure::None;
    CHECK(failed_allocations == 0);
    AssertStillRecording(current);
    GetCaptureSession().AbortAllocationFailure();
    std::puts("Stale generation/token allocation abort preserves rearm: PASS");
}

void TestBatchBeginAllocationFailure()
{
    const auto capture = StartCapture();
    uint64_t batch = UINT64_MAX;
    FailOneAllocation([&] {
        batch = xemu_shader_capture_session_batch_begin(
            capture.context.scope_generation, capture.context.renderer_epoch);
    });
    CHECK(batch == 0);
    AssertAborted(capture);
    std::puts("C batch creation allocation failure: PASS");
}

void TestBatchBeginInvalidationAllocationFailure()
{
    const auto capture = StartCapture();
    uint64_t batch = UINT64_MAX;
    FailOneAllocation([&] {
        batch = xemu_shader_capture_session_batch_begin(
            capture.context.scope_generation + 1,
            capture.context.renderer_epoch);
    });
    CHECK(batch == 0);
    AssertAborted(capture);
    std::puts("C batch invalidation allocation failure: PASS");
}

void TestStaleBatchCallbacksPreserveRearm()
{
    const auto old = StartCapture();
    uint64_t batch = xemu_shader_capture_session_batch_begin(
        old.context.scope_generation, old.context.renderer_epoch);
    CHECK(batch);
    const auto current = StartCapture();
    failed_allocations = 0;
    allocation_failure = AllocationFailure::Always;
    CHECK(!xemu_shader_capture_session_batch_current(batch));
    CHECK(
        !xemu_shader_capture_session_batch_hold(batch, current.pending_token));
    CHECK(!xemu_shader_capture_session_batch_record(
        batch, current.pending_token, XEMU_SHADER_CAPTURE_MAIN, 1));
    CHECK(!xemu_shader_capture_session_batch_submit(batch, 1, 1, 0));
    CHECK(!xemu_shader_capture_session_batch_retire(batch, 0, -4));
    CHECK(!xemu_shader_capture_session_batch_abort(
        batch, XEMU_SHADER_CAPTURE_BATCH_ABORTED, -4));
    CHECK(!xemu_shader_capture_session_describe_command(
        old.pending_token, XEMU_SHADER_CAPTURE_COMMAND_DRAW, 0, 0, 0));
    allocation_failure = AllocationFailure::None;
    CHECK(failed_allocations == 0);
    AssertStillRecording(current);
    GetCaptureSession().AbortAllocationFailure();
    std::puts("Stale C batch callbacks preserve identical-context rearm: PASS");
}

void TestBatchSubmitAllocationFailure()
{
    const auto capture = StartCapture();
    uint64_t batch = xemu_shader_capture_session_batch_begin(
        capture.context.scope_generation, capture.context.renderer_epoch);
    CHECK(batch);
    uint64_t token = xemu_shader_capture_session_begin_event(
        XEMU_SHADER_CAPTURE_DRAW, 7, 2, 2, capture.context.scope_generation,
        capture.context.renderer_epoch);
    CHECK(token && xemu_shader_capture_session_batch_hold(batch, token));
    CHECK(xemu_shader_capture_session_batch_record(
        batch, token, XEMU_SHADER_CAPTURE_MAIN, 1));
    CHECK(xemu_shader_draw_request_finish(token, 1, 5, 3, 0));
    CHECK(xemu_shader_draw_request_inputs_complete(token));
    int accepted = 1;
    FailOneAllocation([&] {
        accepted = xemu_shader_capture_session_batch_submit(batch, 1, 1, 0);
    });
    CHECK(!accepted && !GetCaptureSession().Active());
    const auto snapshot = GetCaptureSession().Snapshot();
    CHECK(snapshot.state == CaptureSessionState::BudgetExceeded);
    if (snapshot.pending_events || snapshot.reserved_bytes)
        std::fprintf(stderr,
                     "Batch allocation abort retained %zu pending / "
                     "%llu reserved bytes\n",
                     snapshot.pending_events,
                     static_cast<unsigned long long>(snapshot.reserved_bytes));
    CHECK(snapshot.pending_events == 0 && snapshot.reserved_bytes == 0);
    CHECK(snapshot.total_events == capture.total_events + 1);
    const auto event =
        GetCaptureSession().Find(token & ~kCaptureSessionTokenBit);
    CHECK(event && (event->limitations & CaptureInvalidated));
    CHECK(!event->resource_evidence);
    std::puts(
        "C batch submission allocation failure has no GPU producers: PASS");
}
} // namespace

int main(int argc, char **argv)
{
    if (argc == 1 || !std::strcmp(argv[1], "claim"))
        TestClaimEpochMismatchAllocationFailure();
    if (argc == 1 || !std::strcmp(argv[1], "begin-event"))
        TestBeginEventEpochMismatchAllocationFailure();
    if (argc == 1 || !std::strcmp(argv[1], "fail-budget"))
        TestBudgetFailureMessageAllocationFailure();
    if (argc == 1 || !std::strcmp(argv[1], "abort"))
        TestAbortDoesNotAllocate();
    if (argc == 1 || !std::strcmp(argv[1], "stale-token"))
        TestStaleTokenFailureDoesNotAbortRearm();
    if (argc == 1 || !std::strcmp(argv[1], "stale-generation"))
        TestStaleGenerationAbortDoesNotAbortRearm();
    if (argc == 1 || !std::strcmp(argv[1], "batch-begin"))
        TestBatchBeginAllocationFailure();
    if (argc == 1 || !std::strcmp(argv[1], "batch-invalidate"))
        TestBatchBeginInvalidationAllocationFailure();
    if (argc == 1 || !std::strcmp(argv[1], "stale-batch"))
        TestStaleBatchCallbacksPreserveRearm();
    if (argc == 1 || !std::strcmp(argv[1], "batch-submit"))
        TestBatchSubmitAllocationFailure();
}

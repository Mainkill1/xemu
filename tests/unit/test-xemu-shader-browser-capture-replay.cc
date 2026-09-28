// SPDX-License-Identifier: GPL-2.0-or-later
#include "../../ui/xui/shader-browser-capture-replay.hh"
#include "../../ui/xui/shader-browser-preview-adapter.hh"
#include <cstdlib>
#include <iostream>

using namespace xemu::shader_browser;
#define CHECK(x)                                   \
    do {                                           \
        if (!(x)) {                                \
            std::cerr << __LINE__ << ": " #x "\n"; \
            std::abort();                          \
        }                                          \
    } while (0)

static SharedCaptureBlock Pixels(uint8_t value)
{
    auto block = std::make_shared<CaptureImmutableBlock>();
    block->id = value + 1;
    block->bytes.assign(16, value);
    return block;
}
static CaptureResourceOperation
Op(CaptureResourceOperationType type, uint64_t allocation, uint64_t view,
   uint32_t kind = XEMU_SHADER_CAPTURE_RESOURCE_COLOR)
{
    CaptureResourceOperation op;
    op.type = type;
    op.allocation_id = allocation;
    op.view_id = view;
    op.byte_size = 16;
    op.range = { 0, 16 };
    op.kind = kind;
    return op;
}
static XemuShaderCaptureReplayBinding Binding(uint32_t role, bool write,
                                              uint32_t ordinal = 0)
{
    XemuShaderCaptureReplayBinding binding{};
    binding.role = role;
    binding.resource = { XEMU_SHADER_CAPTURE_RESOURCE_COLOR, 0, write,
                         ordinal };
    if (role == XEMU_SHADER_CAPTURE_REPLAY_TEXTURE ||
        role == XEMU_SHADER_CAPTURE_REPLAY_COPY_DESTINATION)
        binding.resource.kind = XEMU_SHADER_CAPTURE_RESOURCE_TEXTURE;
    binding.image.width = binding.image.height = 2;
    binding.image.mip_levels = binding.image.layers = binding.image.samples = 1;
    binding.image.format = XEMU_SHADER_CAPTURE_REPLAY_RGBA8_UNORM;
    for (uint32_t i = 0; i < 4; ++i)
        binding.image.storage_to_rgba[i] = binding.image.sample_swizzle[i] = i;
    return binding;
}
struct Fixture {
    CaptureSessionSnapshot snapshot;
    CaptureReplayDescriptions descriptions;
    explicit Fixture(bool initialize_texture = false,
                     bool initialize_producer = true, bool with_clear = false)
    {
        CaptureResourceTimeline timeline;
        const auto a = timeline.ReserveAllocationId(),
                   b = timeline.ReserveAllocationId(),
                   c = timeline.ReserveAllocationId();
        const auto av = timeline.ReserveViewId(), bv = timeline.ReserveViewId(),
                   cv = timeline.ReserveViewId();
        auto allocation = [&](uint64_t id, uint64_t view) {
            auto op = Op(CaptureResourceOperationType::Allocate, id, view);
            return op;
        };
        auto alias = [&](uint64_t id, uint64_t view) {
            auto op = Op(CaptureResourceOperationType::Alias, id, view);
            op.aliases = { { id, { 0, 16 } } };
            return op;
        };
        std::vector<CaptureResourceOperation> initial_ops = {
            allocation(a, av),
            allocation(b, bv),
            allocation(c, cv),
            alias(a, av),
            alias(b, bv),
            alias(c, cv),
            Op(CaptureResourceOperationType::FullWrite, c, cv)
        };
        if (initialize_producer)
            initial_ops.push_back(
                Op(CaptureResourceOperationType::FullWrite, a, av));
        if (initialize_texture)
            initial_ops.push_back(Op(CaptureResourceOperationType::FullWrite, b,
                                     bv, XEMU_SHADER_CAPTURE_RESOURCE_TEXTURE));
        const auto initial = timeline.Apply(1, initial_ops);
        std::vector<CaptureResourceCommand> commands = {
            { 10,
              { Op(CaptureResourceOperationType::Read, a, av),
                Op(CaptureResourceOperationType::UncertainWrite, a, av) } },
            { 2,
              { Op(CaptureResourceOperationType::Read, a, av),
                Op(CaptureResourceOperationType::Read, b, bv,
                   XEMU_SHADER_CAPTURE_RESOURCE_TEXTURE),
                Op(CaptureResourceOperationType::UncertainWrite, b, bv,
                   XEMU_SHADER_CAPTURE_RESOURCE_TEXTURE) } },
            { 12,
              { Op(CaptureResourceOperationType::Read, b, bv,
                   XEMU_SHADER_CAPTURE_RESOURCE_TEXTURE),
                Op(CaptureResourceOperationType::Read, c, cv),
                Op(CaptureResourceOperationType::UncertainWrite, c, cv) } }
        };
        if (with_clear)
            commands.insert(commands.begin() + 1,
                            { 4,
                              { Op(CaptureResourceOperationType::Read, a, av),
                                Op(CaptureResourceOperationType::UncertainWrite,
                                   a, av) } });
        auto events = timeline.ApplyBatch(commands);
        CHECK(events.size() == (with_clear ? 4 : 3));
        snapshot.resource_domain = timeline.DomainId();
        snapshot.context.backend = uint32_t(PreviewBackend::OpenGL);
        auto occurrence = [&](SharedCaptureResourceEvent evidence) {
            auto out = std::make_shared<CaptureOccurrence>();
            out->event_id = evidence->event_id;
            out->resource_evidence = evidence;
            out->finished = out->inputs.complete = out->emitted = true;
            out->pending = false;
            out->limitations = 0;
            out->inputs.before = { 2, 2, Pixels(0) };
            out->inputs.textures[0].described = true;
            out->inputs.textures[0].metadata.bound = true;
            out->inputs.textures[0].images.push_back(
                { 0, 0, { 2, 2, Pixels(99) } });
            return out;
        };
        auto producer = occurrence(events[0]),
             copy = occurrence(events[with_clear ? 2 : 1]),
             consumer = occurrence(events[with_clear ? 3 : 2]);
        copy->type = CaptureEventType::Copy;
        auto baseline = occurrence(initial);
        baseline->type = CaptureEventType::AllocationBoundary;
        snapshot.events = { baseline, copy, producer, consumer };
        auto draw = std::make_shared<CaptureReplayDescription>();
        draw->kind = uint32_t(CaptureCommandKind::Draw);
        auto before = Binding(XEMU_SHADER_CAPTURE_REPLAY_COLOR, false);
        before.checkpoint = 1;
        draw->bindings = { before,
                           Binding(XEMU_SHADER_CAPTURE_REPLAY_COLOR, true) };
        descriptions[10] = draw;
        auto image_copy = std::make_shared<CaptureReplayDescription>();
        image_copy->kind = uint32_t(CaptureCommandKind::ImageCopy);
        image_copy->width = image_copy->height = 2;
        image_copy->bindings = {
            Binding(XEMU_SHADER_CAPTURE_REPLAY_COPY_SOURCE, false),
            Binding(XEMU_SHADER_CAPTURE_REPLAY_COPY_DESTINATION, false),
            Binding(XEMU_SHADER_CAPTURE_REPLAY_COPY_DESTINATION, true)
        };
        descriptions[2] = image_copy;
        auto tail = std::make_shared<CaptureReplayDescription>();
        tail->kind = uint32_t(CaptureCommandKind::Draw);
        auto texture = Binding(XEMU_SHADER_CAPTURE_REPLAY_TEXTURE, false);
        texture.checkpoint = 1; // Never permits captured descendant fallback.
        before.resource.ordinal = 0;
        tail->bindings = { texture, before,
                           Binding(XEMU_SHADER_CAPTURE_REPLAY_COLOR, true) };
        descriptions[12] = tail;
        if (with_clear) {
            auto clear = occurrence(events[1]);
            clear->type = CaptureEventType::Clear;
            clear->batch_id = clear->command_ordinal = clear->queue_ordinal = 1;
            clear->command_recorded = true;
            clear->command.kind = CaptureCommandKind::Clear;
            clear->submission = CaptureBatchOutcome::Submitted;
            clear->completion = CaptureBatchCompletion::Completed;
            // This captured old destination must never replace a produced
            // version.
            clear->inputs.before = { 2, 2, Pixels(99) };
            snapshot.events.insert(snapshot.events.begin() + 2, clear);
            auto clear_description =
                std::make_shared<CaptureReplayDescription>();
            clear_description->kind = uint32_t(CaptureCommandKind::Clear);
            clear_description->destination_x = 1;
            clear_description->width = clear_description->height = 1;
            clear_description->clear_color_mask = 0xF;
            const float color[] = { 0, 1, .5F, 1 };
            std::memcpy(clear_description->clear_color_bits, color,
                        sizeof(color));
            auto preserved = Binding(XEMU_SHADER_CAPTURE_REPLAY_COLOR, false);
            preserved.checkpoint = 1;
            clear_description->bindings = {
                preserved, Binding(XEMU_SHADER_CAPTURE_REPLAY_COLOR, true)
            };
            descriptions[4] = clear_description;
        }
    }
    std::shared_ptr<const CaptureReplayPlan> Plan() const
    {
        return std::make_shared<CaptureReplayPlan>(
            BuildCaptureReplayPlan(snapshot, descriptions, { 10 }));
    }
};
static PreviewResultKey Result(const CaptureReplayDraw &draw)
{
    PreviewResultKey key;
    key.width = key.height = 2;
    key.packet_kind = PreviewPacketKind::Replay;
    key.input_revision = draw.identity.event_id;
    return key;
}
static void Finish(CaptureReplaySequence &sequence,
                   const CaptureReplayDraw &draw, uint8_t value)
{
    std::string error;
    const auto result = Result(draw);
    CHECK(sequence.ExpectResult(draw.identity, result));
    CHECK(sequence.Complete(draw.identity, result,
                            { 2, 2, std::vector<uint8_t>(16, value) }, &error));
}
static void test_actual_output_propagates_independent_branches()
{
    Fixture fixture;
    auto plan = fixture.Plan();
    CHECK(plan->status == CaptureReplayStatus::Ready);
    CHECK(plan->logical_closure_complete && plan->steps.size() == 3);
    CHECK(plan->steps[0].occurrence->event_id == 10 &&
          plan->steps[1].occurrence->event_id == 2);
    CaptureReplaySequence sequence;
    CHECK(sequence.Start(plan));
    CaptureReplayDraw draw;
    std::string error;
    for (unsigned branch = 0; branch < 2; ++branch) {
        CHECK(sequence.TryClaimDraw(&draw, &error));
        CHECK(draw.identity.branch == branch && draw.identity.event_id == 10);
        CHECK(draw.inputs[0].bytes->front() == 0);
        Finish(sequence, draw, branch ? 7 : 3);
        CHECK(sequence.TryClaimDraw(&draw, &error));
        CHECK(draw.identity.event_id == 12 && !draw.edited_seed);
        CHECK(draw.inputs[0].bytes->front() == (branch ? 7 : 3));
        CHECK(draw.inputs[0].bytes->front() != 99);
        CHECK(draw.inputs[1].bytes->front() == 0);
        Finish(sequence, draw, branch ? 11 : 5);
    }
    CHECK(!sequence.TryClaimDraw(&draw, &error));
    CHECK(sequence.Status() == CaptureReplayStatus::Completed);
}
static void test_missing_description_and_legacy_are_unsupported()
{
    Fixture fixture;
    fixture.descriptions.erase(2);
    CHECK(fixture.Plan()->status == CaptureReplayStatus::Unsupported);
    fixture = Fixture();
    fixture.snapshot.execution_order_complete = false;
    CHECK(fixture.Plan()->status == CaptureReplayStatus::Unsupported);
}
static void test_partial_clear_preserves_branch_output_before_consumer()
{
    Fixture fixture(false, true, true);
    auto plan = fixture.Plan();
    if (plan->status != CaptureReplayStatus::Ready)
        std::cerr << "Clear proof: " << plan->reason << '\n';
    CHECK(plan->status == CaptureReplayStatus::Ready);
    CHECK(plan->logical_closure_complete && plan->steps.size() == 4);
    CHECK(plan->steps[1].occurrence->event_id == 4);
    CaptureReplaySequence sequence;
    CHECK(sequence.Start(plan));
    CaptureReplayDraw draw;
    std::string error;
    for (unsigned branch = 0; branch < 2; ++branch) {
        CHECK(sequence.TryClaimDraw(&draw, &error));
        CHECK(draw.identity.event_id == 10);
        const uint8_t produced = branch ? 7 : 3;
        Finish(sequence, draw, produced);
        CHECK(sequence.TryClaimDraw(&draw, &error));
        CHECK(draw.identity.event_id == 12);
        const auto &pixels = *draw.inputs[0].bytes;
        for (size_t pixel = 0; pixel < 4; ++pixel)
            if (pixel == 1) {
                CHECK(pixels[pixel * 4] == 0 && pixels[pixel * 4 + 1] == 255 &&
                      pixels[pixel * 4 + 2] == 128 &&
                      pixels[pixel * 4 + 3] == 255);
            } else
                for (size_t channel = 0; channel < 4; ++channel)
                    CHECK(pixels[pixel * 4 + channel] == produced);
        Finish(sequence, draw, produced);
    }
    CHECK(!sequence.TryClaimDraw(&draw, &error));
    CHECK(sequence.Status() == CaptureReplayStatus::Completed);
}
static void test_full_clear_cuts_unowned_destination_and_keeps_bgra_canonical()
{
    Fixture fixture(false, true, true);
    auto clear =
        std::make_shared<CaptureReplayDescription>(*fixture.descriptions[4]);
    clear->destination_x = 0;
    clear->width = clear->height = 2;
    clear->bindings[0].checkpoint = 0;
    // Clamping finite values outside UNORM's range is part of its conversion.
    const float colors[] = { 2, -2, 0, 1 };
    std::memcpy(clear->clear_color_bits, colors, sizeof(colors));
    fixture.descriptions[4] = clear;
    for (auto &entry : fixture.descriptions) {
        auto description =
            std::make_shared<CaptureReplayDescription>(*entry.second);
        for (auto &binding : description->bindings) {
            binding.image.format = XEMU_SHADER_CAPTURE_REPLAY_BGRA8_UNORM;
            binding.image.storage_to_rgba[0] = 2;
            binding.image.storage_to_rgba[2] = 0;
        }
        entry.second = description;
    }
    auto plan = std::make_shared<CaptureReplayPlan>(
        BuildCaptureReplayPlan(fixture.snapshot, fixture.descriptions, { 4 }));
    CHECK(plan->status == CaptureReplayStatus::Ready);
    CHECK(plan->steps.size() == 3 && plan->steps[0].occurrence->event_id == 4);
    CHECK(!plan->steps[0].bindings[0].checkpoint);
    CaptureReplaySequence sequence;
    CHECK(sequence.Start(plan));
    CaptureReplayDraw draw;
    std::string error;
    for (unsigned branch = 0; branch < 2; ++branch) {
        CHECK(sequence.TryClaimDraw(&draw, &error));
        CHECK(draw.identity.event_id == 12 && draw.identity.branch == branch);
        const auto &pixels = *draw.inputs[0].bytes;
        for (size_t i = 0; i < pixels.size(); i += 4)
            CHECK(pixels[i] == 255 && pixels[i + 1] == 0 &&
                  pixels[i + 2] == 0 && pixels[i + 3] == 255);
        Finish(sequence, draw, 0);
    }
    CHECK(!sequence.TryClaimDraw(&draw, &error));
    CHECK(sequence.Status() == CaptureReplayStatus::Completed);
    clear =
        std::make_shared<CaptureReplayDescription>(*fixture.descriptions[4]);
    clear->width = 1;
    fixture.descriptions[4] = clear;
    CHECK(BuildCaptureReplayPlan(fixture.snapshot, fixture.descriptions, { 4 })
              .status == CaptureReplayStatus::Unsupported);
}
static void test_clear_rejects_unknown_parameters_and_unaccepted_gpu_receipts()
{
    for (unsigned invalid = 0; invalid < 20; ++invalid) {
        Fixture fixture(false, true, true);
        auto description = std::make_shared<CaptureReplayDescription>(
            *fixture.descriptions[4]);
        auto where = std::find_if(
            fixture.snapshot.events.begin(), fixture.snapshot.events.end(),
            [](const auto &event) { return event->event_id == 4; });
        CHECK(where != fixture.snapshot.events.end());
        auto clear = std::make_shared<CaptureOccurrence>(**where);
        switch (invalid) {
        case 0:
            description->clear_color_mask = 0;
            break;
        case 1:
            description->clear_color_mask = 7;
            break;
        case 2:
            description->clear_color_mask = 0x1F;
            break;
        case 3:
            description->clear_color_bits[0] = 0x7FC00000;
            break;
        case 4:
            description->clear_color_bits[0] = 0x7F800000;
            break;
        case 5:
            description->width = 0;
            break;
        case 6:
            description->destination_y = 2;
            break;
        case 7:
            description->source_x = 1;
            break;
        case 8:
            description->bytes = 1;
            break;
        case 9:
            description->bindings[1].image.samples = 2;
            break;
        case 10:
            description->bindings[1].image.layer = 1;
            break;
        case 11:
            description->bindings[1].image.sample_swizzle[0] = 5;
            break;
        case 12:
            clear->emitted = false;
            break;
        case 13:
            clear->command_recorded = false;
            break;
        case 14:
            clear->batch_id = 0;
            break;
        case 15:
            clear->submission = CaptureBatchOutcome::Aborted;
            break;
        case 16:
            clear->completion = CaptureBatchCompletion::Failed;
            break;
        case 17:
            clear->command.kind = CaptureCommandKind::Draw;
            break;
        case 18:
            clear->command_phase = CaptureCommandPhase::HostPreparation;
            break;
        case 19:
            clear->completion = CaptureBatchCompletion::Pending;
            break;
        }
        fixture.descriptions[4] = description;
        *where = clear;
        CHECK(fixture.Plan()->status == CaptureReplayStatus::Unsupported);
        // A refused logical plan keeps the original raw occurrence and bits.
        CHECK((*where)->resource_evidence && description->clear_color_bits[3]);
    }
}
static void test_clear_orientation_and_budget_refusal_are_bounded()
{
    Fixture fixture(false, true, true);
    for (auto &entry : fixture.descriptions) {
        auto description =
            std::make_shared<CaptureReplayDescription>(*entry.second);
        for (auto &binding : description->bindings)
            binding.image.coordinate_origin =
                XEMU_SHADER_CAPTURE_REPLAY_BOTTOM_UP;
        entry.second = description;
    }
    CaptureReplaySequence sequence;
    CHECK(sequence.Start(fixture.Plan()));
    CaptureReplayDraw draw;
    std::string error;
    CHECK(sequence.TryClaimDraw(&draw, &error));
    Finish(sequence, draw, 7);
    CHECK(sequence.TryClaimDraw(&draw, &error));
    CHECK(draw.identity.event_id == 12);
    CHECK((*draw.inputs[0].bytes)[4] == 7);
    CHECK((*draw.inputs[0].bytes)[12] == 0 &&
          (*draw.inputs[0].bytes)[13] == 255 &&
          (*draw.inputs[0].bytes)[14] == 128 &&
          (*draw.inputs[0].bytes)[15] == 255);
    fixture = Fixture(false, true, true);
    const auto plan = fixture.Plan();
    CHECK(sequence.Start(plan));
    CHECK(sequence.TryClaimDraw(&draw, &error));
    Finish(sequence, draw, 3);
    const uint64_t producer_bytes = sequence.RetainedValueBytes();
    CaptureReplayLimits limits;
    limits.value_byte_budget = producer_bytes;
    CaptureReplaySequence bounded;
    CHECK(bounded.Start(plan, limits));
    CHECK(bounded.TryClaimDraw(&draw, &error));
    Finish(bounded, draw, 3);
    CHECK(!bounded.TryClaimDraw(&draw, &error));
    CHECK(bounded.Status() == CaptureReplayStatus::BudgetExceeded);
    CHECK(bounded.RetainedValueBytes() == producer_bytes);
    CHECK(bounded.Reason().find("clear") != std::string::npos);
}
static void test_stale_completion_and_budget_are_transactional()
{
    Fixture fixture;
    CaptureReplaySequence sequence;
    auto plan = fixture.Plan();
    CHECK(sequence.Start(plan));
    CaptureReplayDraw old, current;
    std::string error;
    CHECK(sequence.TryClaimDraw(&old, &error));
    CHECK(sequence.ExpectResult(old.identity, Result(old)));
    sequence.Cancel();
    CHECK(sequence.Start(plan));
    CHECK(sequence.TryClaimDraw(&current, &error));
    CHECK(!sequence.Complete(old.identity, Result(old),
                             { 2, 2, std::vector<uint8_t>(16, 9) }, &error));
    CHECK(sequence.Status() == CaptureReplayStatus::Running);
    const uint64_t retained = sequence.RetainedValueBytes();
    CHECK(sequence.ExpectResult(current.identity, Result(current)));
    auto wrong = Result(current);
    ++wrong.input_revision;
    CHECK(!sequence.Complete(current.identity, wrong,
                             { 2, 2, std::vector<uint8_t>(16, 9) }, &error));
    CHECK(sequence.RetainedValueBytes() == retained);
    CaptureReplayLimits limits;
    limits.value_byte_budget = std::max<uint64_t>(retained, 1);
    sequence.Cancel();
    CHECK(sequence.Start(plan, limits));
    CHECK(sequence.TryClaimDraw(&current, &error));
    CHECK(sequence.ExpectResult(current.identity, Result(current)));
    CHECK(!sequence.Complete(current.identity, Result(current),
                             { 2, 2, std::vector<uint8_t>(16, 9) }, &error));
    CHECK(sequence.Status() == CaptureReplayStatus::BudgetExceeded);
}
static void test_copy_orientation_partial_target_and_missing_proof()
{
    Fixture fixture(true);
    auto copy =
        std::make_shared<CaptureReplayDescription>(*fixture.descriptions[2]);
    copy->width = copy->height = 1;
    copy->destination_x = 1;
    copy->bindings[1].checkpoint = 1;
    std::strcpy(copy->bindings[1].blob_name, "copy.before");
    auto occurrence =
        std::make_shared<CaptureOccurrence>(*fixture.snapshot.events[1]);
    CaptureOwnedBlob blob;
    blob.name = "copy.before";
    blob.data = Pixels(99);
    occurrence->inputs.blobs.push_back(blob);
    fixture.snapshot.events[1] = occurrence;
    fixture.descriptions[2] = copy;
    auto plan = fixture.Plan();
    CHECK(plan->status == CaptureReplayStatus::Ready);
    CaptureReplaySequence sequence;
    CHECK(sequence.Start(plan));
    CaptureReplayDraw draw;
    std::string error;
    CHECK(sequence.TryClaimDraw(&draw, &error));
    Finish(sequence, draw, 7);
    CHECK(sequence.TryClaimDraw(&draw, &error));
    CHECK((*draw.inputs[0].bytes)[0] == 99 && (*draw.inputs[0].bytes)[4] == 7 &&
          (*draw.inputs[0].bytes)[8] == 99);
    // The same partial command without its unaffected target checkpoint fails.
    copy->bindings[1].checkpoint = 0;
    CHECK(fixture.Plan()->status == CaptureReplayStatus::Unsupported);
    fixture = Fixture();
    copy = std::make_shared<CaptureReplayDescription>(*fixture.descriptions[2]);
    copy->bindings[0].image.coordinate_origin =
        XEMU_SHADER_CAPTURE_REPLAY_BOTTOM_UP;
    fixture.descriptions[2] = copy;
    plan = fixture.Plan();
    CHECK(plan->status == CaptureReplayStatus::Ready);
    CHECK(sequence.Start(plan));
    CHECK(sequence.TryClaimDraw(&draw, &error));
    auto result = Result(draw);
    CHECK(sequence.ExpectResult(draw.identity, result));
    std::vector<uint8_t> rows(16, 1);
    std::fill(rows.begin() + 8, rows.end(), 2);
    CHECK(sequence.Complete(draw.identity, result, { 2, 2, rows }, &error));
    CHECK(sequence.TryClaimDraw(&draw, &error));
    CHECK((*draw.inputs[0].bytes)[0] == 2 && (*draw.inputs[0].bytes)[8] == 1);
    copy->width = 3;
    CHECK(fixture.Plan()->status == CaptureReplayStatus::Unsupported);
    copy->width = 2;
    copy->bindings[2].image.format = XEMU_SHADER_CAPTURE_REPLAY_BGRA8_UNORM;
    copy->bindings[2].image.storage_to_rgba[0] = 2;
    copy->bindings[2].image.storage_to_rgba[2] = 0;
    CHECK(fixture.Plan()->status == CaptureReplayStatus::Unsupported);
}
static void test_packet_rebinding_recomputes_owned_inputs_and_rejects_depth()
{
    Fixture fixture;
    CaptureReplaySequence sequence;
    CHECK(sequence.Start(fixture.Plan()));
    CaptureReplayDraw draw;
    std::string error;
    CHECK(sequence.TryClaimDraw(&draw, &error));
    Finish(sequence, draw, 7);
    CHECK(sequence.TryClaimDraw(&draw, &error));
    PreviewPacketInputs input;
    input.selection.scope.title_id = 1;
    input.selection.session_epoch = 2;
    input.selection.renderer_epoch = 3;
    input.selection.backend = PreviewBackend::OpenGL;
    const uint8_t recipe[] = { 1, 2, 3 };
    input.selection.shader = { ComputeShaderHash(1, Stage::Pixel, 1, recipe,
                                                 sizeof(recipe)),
                               Stage::Pixel };
    input.recipe.key = input.selection.shader;
    input.recipe.recipe_format_version = 1;
    input.recipe.bytes.assign(recipe, recipe + sizeof(recipe));
    input.source_resident = true;
    input.source_route = Route::Specialized;
    input.source = "#version 400\nout vec4 color;void main(){color=vec4(1);}";
    input.partner_source =
        "#version 400\nvoid main(){gl_Position=vec4(0,0,0,1);}";
    input.generator_abi = input.interface_abi = 1;
    input.width = input.height = 2;
    input.fixture_bytes = EncodePreviewSyntheticFixture(
        MakePreviewFixture(PreviewFixtureProfile::Flat));
    PreviewPacket base;
    CHECK(BuildPreviewPacket(input, &base, &error));
    auto pipeline = std::make_shared<PreviewCapturedPipeline>();
    pipeline->backend = PreviewBackend::OpenGL;
    // This CPU fixture uses legacy GL triangle-list topology.
    pipeline->host_topology = 4;
    pipeline->vertex_count = 3;
    pipeline->ranges = { { 0, 3 } };
    pipeline->raster.available = 1023;
    pipeline->raster.width = pipeline->raster.height = 2;
    pipeline->color_before = { 2, 2, std::vector<uint8_t>(16, 99) };
    auto material = std::make_shared<PreviewCapturedMaterial>();
    auto &texture = material->textures[0];
    texture.described = true;
    texture.metadata.bound = 1;
    texture.metadata.width = texture.metadata.height = 2;
    texture.metadata.depth = texture.metadata.face_count =
        texture.metadata.mip_levels = 1;
    texture.images = { { 0, 0, { 2, 2, std::vector<uint8_t>(16, 99) } } };
    base.packet_kind = PreviewPacketKind::Replay;
    base.replay_class = PreviewReplayClass::Approximate;
    base.captured_pipeline = pipeline;
    base.captured_material = material;
    base.pipeline_digest = ComputePreviewCapturedPipelineDigest(*pipeline);
    base.pipeline_layout_digest =
        ComputePreviewCapturedPipelineDigest(*pipeline, true);
    base.material_digest = ComputePreviewCapturedMaterialDigest(*material);
    CHECK(ValidatePreviewPacket(base, &error));
    PreviewPacket rebound;
    CHECK(RebindCaptureReplayPacket(draw, base, &rebound, &error));
    CHECK(rebound.captured_pipeline->color_before.rgba[0] == 0);
    CHECK(rebound.captured_material->textures[0].images[0].image.rgba[0] == 7);
    CHECK(base.captured_pipeline->color_before.rgba[0] == 99 &&
          base.captured_material->textures[0].images[0].image.rgba[0] == 99);
    CHECK(rebound.material_digest != base.material_digest &&
          rebound.pipeline_digest != base.pipeline_digest &&
          rebound.input_revision == draw.identity.token);
    pipeline->raster.depth_test = true;
    CHECK(!RebindCaptureReplayPacket(draw, base, &rebound, &error));
    pipeline->raster.depth_test = false;
    draw.inputs.erase(draw.inputs.begin());
    CHECK(!RebindCaptureReplayPacket(draw, base, &rebound, &error));
}
static void test_cpu_upload_buffer_copy_and_subrange_versions()
{
    CaptureResourceTimeline timeline;
    const auto a = timeline.ReserveAllocationId(),
               b = timeline.ReserveAllocationId(),
               color = timeline.ReserveAllocationId();
    const auto av = timeline.ReserveViewId(), bv = timeline.ReserveViewId(),
               cv = timeline.ReserveViewId();
    auto allocation = [&](uint64_t id, uint64_t view, uint64_t size) {
        auto op = Op(CaptureResourceOperationType::Allocate, id, view);
        op.byte_size = size;
        return op;
    };
    auto alias = [&](uint64_t id, uint64_t view, uint64_t size) {
        auto op = Op(CaptureResourceOperationType::Alias, id, view);
        op.aliases = { { id, { 0, size } } };
        return op;
    };
    const auto initial = timeline.Apply(
        1, { allocation(a, av, 64), allocation(b, bv, 64),
             allocation(color, cv, 16), alias(a, av, 64), alias(b, bv, 64),
             alias(color, cv, 16),
             Op(CaptureResourceOperationType::FullWrite, color, cv) });
    auto payload = std::make_shared<CaptureImmutableBlock>();
    payload->id = 42;
    payload->bytes.resize(64);
    for (size_t i = 0; i < payload->bytes.size(); ++i)
        payload->bytes[i] = uint8_t(i);
    auto upload = Op(CaptureResourceOperationType::FullWrite, a, av,
                     XEMU_SHADER_CAPTURE_RESOURCE_BUFFER);
    upload.snapshot_block = payload->id;
    auto read = Op(CaptureResourceOperationType::Read, a, av,
                   XEMU_SHADER_CAPTURE_RESOURCE_BUFFER);
    read.range = { 8, 16 };
    auto write = Op(CaptureResourceOperationType::PartialWrite, b, bv,
                    XEMU_SHADER_CAPTURE_RESOURCE_BUFFER);
    write.range = { 24, 16 };
    auto attr = Op(CaptureResourceOperationType::Read, b, bv,
                   XEMU_SHADER_CAPTURE_RESOURCE_BUFFER);
    attr.range = { 28, 8 };
    auto events = timeline.ApplyBatch(
        { { 10, { upload } },
          { 2, { read, write } },
          { 12,
            { attr, Op(CaptureResourceOperationType::Read, color, cv),
              Op(CaptureResourceOperationType::UncertainWrite, color,
                 cv) } } });
    CHECK(events.size() == 3);
    auto event = [](SharedCaptureResourceEvent resource,
                    CaptureEventType type) {
        auto occurrence = std::make_shared<CaptureOccurrence>();
        occurrence->event_id = resource->event_id;
        occurrence->resource_evidence = resource;
        occurrence->type = type;
        occurrence->finished = occurrence->inputs.complete = true;
        occurrence->pending = false;
        occurrence->emitted = type == CaptureEventType::Draw;
        occurrence->limitations = 0;
        occurrence->inputs.before = { 2, 2, Pixels(0) };
        return occurrence;
    };
    auto host = event(events[0], CaptureEventType::Upload);
    host->batch_id = host->command_ordinal = 1;
    host->command_recorded = true;
    host->command_phase = CaptureCommandPhase::HostPreparation;
    host->command.kind = CaptureCommandKind::CpuUpload;
    CaptureOwnedBlob blob;
    blob.name = "resource.buffer.upload";
    blob.data = payload;
    host->inputs.blobs.push_back(blob);
    CaptureSessionSnapshot snapshot;
    snapshot.resource_domain = timeline.DomainId();
    snapshot.events = { event(initial, CaptureEventType::AllocationBoundary),
                        event(events[1], CaptureEventType::Copy), host,
                        event(events[2], CaptureEventType::Draw) };
    auto buffer = [](uint32_t role, bool is_write) {
        XemuShaderCaptureReplayBinding binding{};
        binding.role = role;
        binding.resource = { XEMU_SHADER_CAPTURE_RESOURCE_BUFFER, 0, is_write,
                             0 };
        return binding;
    };
    auto desc = std::make_shared<CaptureReplayDescription>();
    desc->kind = uint32_t(CaptureCommandKind::CpuUpload);
    desc->bytes = 64;
    auto target = buffer(XEMU_SHADER_CAPTURE_REPLAY_COPY_DESTINATION, true);
    std::strcpy(target.blob_name, "resource.buffer.upload");
    desc->bindings = { target };
    CaptureReplayDescriptions descriptions;
    descriptions[10] = desc;
    desc = std::make_shared<CaptureReplayDescription>();
    desc->kind = uint32_t(CaptureCommandKind::BufferCopy);
    desc->source_offset = 8;
    desc->destination_offset = 24;
    desc->bytes = 16;
    desc->bindings = { buffer(XEMU_SHADER_CAPTURE_REPLAY_COPY_SOURCE, false),
                       buffer(XEMU_SHADER_CAPTURE_REPLAY_COPY_DESTINATION,
                              true) };
    descriptions[2] = desc;
    desc = std::make_shared<CaptureReplayDescription>();
    desc->kind = uint32_t(CaptureCommandKind::Draw);
    auto before = Binding(XEMU_SHADER_CAPTURE_REPLAY_COLOR, false);
    before.checkpoint = 1;
    auto vertex = buffer(XEMU_SHADER_CAPTURE_REPLAY_VERTEX_STREAM, false);
    std::strcpy(vertex.blob_name, "vertex.attribute0");
    desc->bindings = { vertex, before,
                       Binding(XEMU_SHADER_CAPTURE_REPLAY_COLOR, true) };
    descriptions[12] = desc;
    auto plan = std::make_shared<CaptureReplayPlan>(
        BuildCaptureReplayPlan(snapshot, descriptions, { 10 }));
    CHECK(plan->status == CaptureReplayStatus::Ready);
    CaptureReplaySequence sequence;
    CHECK(sequence.Start(plan));
    CaptureReplayDraw draw;
    std::string error;
    CHECK(sequence.TryClaimDraw(&draw, &error));
    CHECK(draw.identity.event_id == 12 && draw.inputs[0].bytes->size() == 8);
    CHECK(draw.inputs[0].bytes->front() == 12 &&
          draw.inputs[0].bytes->back() == 19);
    payload->bytes[12] = 99; // The copied produced version remains immutable.
    CHECK(draw.inputs[0].bytes->front() == 12);
}
static void test_explicit_checkpoint_cut_preserves_raw_gap_and_capacity_bound()
{
    Fixture fixture(false, false);
    auto plan = fixture.Plan();
    CHECK(plan->status == CaptureReplayStatus::Ready &&
          plan->logical_closure_complete);
    auto graph = BuildCaptureSessionResourceGraph(fixture.snapshot);
    CHECK(!graph.gaps.empty());
    CHECK(!PlanCaptureResourceReplacement(graph, { 10 }).provenance_complete);
    auto description =
        std::make_shared<CaptureReplayDescription>(*fixture.descriptions[10]);
    description->bindings[0].checkpoint = 0;
    fixture.descriptions[10] = description;
    CHECK(fixture.Plan()->status == CaptureReplayStatus::Unsupported);
    fixture = Fixture();
    const uint64_t original = fixture.Plan()->retained_bytes;
    auto occurrence =
        std::make_shared<CaptureOccurrence>(*fixture.snapshot.events[2]);
    auto block = std::make_shared<CaptureImmutableBlock>(
        *occurrence->inputs.before.rgba);
    block->bytes.reserve(65536);
    occurrence->inputs.before.rgba = block;
    fixture.snapshot.events[2] = occurrence;
    plan = fixture.Plan();
    CHECK(plan->status == CaptureReplayStatus::Ready &&
          plan->retained_bytes >= original + 65520);
    CaptureReplayLimits limits;
    limits.plan_byte_budget = plan->retained_bytes - 1;
    CHECK(BuildCaptureReplayPlan(fixture.snapshot, fixture.descriptions, { 10 },
                                 limits)
              .status == CaptureReplayStatus::BudgetExceeded);
}
int main()
{
    test_actual_output_propagates_independent_branches();
    test_partial_clear_preserves_branch_output_before_consumer();
    test_full_clear_cuts_unowned_destination_and_keeps_bgra_canonical();
    test_clear_rejects_unknown_parameters_and_unaccepted_gpu_receipts();
    test_clear_orientation_and_budget_refusal_are_bounded();
    test_missing_description_and_legacy_are_unsupported();
    test_stale_completion_and_budget_are_transactional();
    test_copy_orientation_partial_target_and_missing_proof();
    test_packet_rebinding_recomputes_owned_inputs_and_rejects_depth();
    test_cpu_upload_buffer_copy_and_subrange_versions();
    test_explicit_checkpoint_cut_preserves_raw_gap_and_capacity_bound();
    std::cout << "capture logical dependency replay: 11 cases passed\n";
}

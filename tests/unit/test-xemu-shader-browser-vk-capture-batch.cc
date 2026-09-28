// SPDX-License-Identifier: GPL-2.0-or-later
#include "../../ui/xui/shader-browser-capture-session.hh"

#include <cstdio>
#include <cstdlib>
#include <filesystem>

using namespace xemu::shader_browser;
extern "C" int xemu_test_vk_capture_batch_native(uint64_t tokens[4],
                                                 int stop_before_aux);
extern "C" void xemu_test_vk_capture_stop_at_admission(void);
extern "C" int xemu_test_vk_capture_image_copy(uint64_t tokens[3],
                                               uint64_t clears[3]);
extern "C" uint64_t xemu_test_vk_last_event_token(void)
{
    return GetCaptureSession().Snapshot().events.back()->event_id |
           kCaptureSessionTokenBit;
}
extern "C" void xemu_test_vk_capture_stop(void)
{
    GetCaptureSession().Stop();
}
#define CHECK(expression)                                           \
    do {                                                            \
        if (!(expression)) {                                        \
            std::fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, \
                         #expression);                              \
            std::abort();                                           \
        }                                                           \
    } while (0)

static bool ReadsProducer(const CaptureOccurrence &event, uint64_t producer)
{
    CHECK(event.resource_evidence);
    for (const auto &access : event.resource_evidence->accesses)
        for (const auto &read : access.reads)
            if (read.producer_event == producer &&
                read.provenance == CaptureResourceProvenance::Known)
                return true;
    return false;
}

int main()
{
    auto &session = GetCaptureSession();
    CaptureSessionContext context{};
    context.scope.title_id = 17;
    context.scope_generation = 1;
    context.session_epoch = 2;
    context.renderer_epoch = 3;
    context.generation = 4;
    CHECK(session.Start(context));
    session.GuestFrameBoundary(7);
    uint64_t tokens[4]{};
    const int result = xemu_test_vk_capture_batch_native(tokens, 0);
    if (result == 77)
        return result;
    CHECK(result == 0);
    session.Stop();
    auto snapshot = session.Snapshot();
    CHECK(snapshot.state == CaptureSessionState::Ready);
    CHECK(snapshot.pending_events == 0 && snapshot.reserved_bytes == 0);
    std::shared_ptr<const CaptureOccurrence> copies[4];
    for (size_t i = 0; i < 4; ++i) {
        copies[i] = session.Find(tokens[i] & ~kCaptureSessionTokenBit);
        CHECK(copies[i] && copies[i]->command_recorded);
        CHECK(copies[i]->command.kind == CaptureCommandKind::BufferCopy);
        CHECK(copies[i]->command.bytes == 16);
        CHECK(copies[i]->submission == CaptureBatchOutcome::Submitted);
        CHECK(copies[i]->completion == CaptureBatchCompletion::Completed);
        CHECK(copies[i]->resource_finalized && !copies[i]->pending);
        CHECK(copies[i]->backend_result == 0 &&
              copies[i]->completion_result == 0);
    }
    CHECK(copies[0]->command_phase == CaptureCommandPhase::Auxiliary);
    CHECK(copies[1]->command_phase == CaptureCommandPhase::Auxiliary);
    CHECK(copies[2]->command_phase == CaptureCommandPhase::Main);
    CHECK(copies[0]->batch_id == copies[1]->batch_id &&
          copies[0]->batch_id == copies[2]->batch_id);
    CHECK(copies[3]->batch_id != copies[2]->batch_id);
    CHECK(copies[0]->queue_ordinal == 1 && copies[2]->queue_ordinal == 1 &&
          copies[3]->queue_ordinal == 2);
    CHECK(copies[0]->resource_evidence->sequence <
          copies[1]->resource_evidence->sequence);
    CHECK(copies[1]->resource_evidence->sequence <
          copies[2]->resource_evidence->sequence);
    CHECK(ReadsProducer(*copies[2], copies[0]->event_id));
    CHECK(copies[2]->inputs.blobs.empty()); // GPU content was never inferred
                                            // from its host mapping.
    CHECK(copies[0]->inputs.blobs.size() == 1 &&
          copies[0]->inputs.blobs[0].data &&
          copies[0]->inputs.blobs[0].data->bytes[0] == 5);
    CHECK(copies[1]->inputs.blobs.size() == 1 &&
          copies[1]->inputs.blobs[0].data &&
          copies[1]->inputs.blobs[0].data->bytes[0] == 21);
    size_t uploads = 0;
    for (const auto &event : snapshot.events) {
        CHECK(event->type != CaptureEventType::Draw);
        if (event->command.kind == CaptureCommandKind::CpuUpload) {
            CHECK(event->command_phase == CaptureCommandPhase::HostPreparation);
            CHECK(event->resource_finalized && !event->pending);
            CHECK(event->submission == CaptureBatchOutcome::None);
            CHECK(event->inputs.blobs.size() == 1);
            const auto &data = event->inputs.blobs[0].data->bytes;
            CHECK(data.size() == 16 && (data[0] == 5 || data[0] == 21));
            ++uploads;
        }
    }
    CHECK(uploads == 3);
    std::string error;
    const auto directory =
        std::filesystem::temp_directory_path() /
        ("xemu-vk-capture-batch-" + std::to_string(snapshot.resource_domain));
    CHECK(session.Save(directory, &error));
    CaptureSessionSnapshot reopened;
    CHECK(CaptureSession::Reopen(directory, &reopened, &error));
    CHECK(reopened.events.size() == snapshot.events.size());
    for (size_t i = 0; i < reopened.events.size(); ++i) {
        CHECK(reopened.events[i]->command.kind ==
              snapshot.events[i]->command.kind);
        CHECK(reopened.events[i]->command_phase ==
              snapshot.events[i]->command_phase);
        CHECK(reopened.events[i]->submission == snapshot.events[i]->submission);
        CHECK(reopened.events[i]->completion == snapshot.events[i]->completion);
    }
    std::filesystem::remove_all(directory);
    CHECK(session.Start(context));
    session.GuestFrameBoundary(7);
    uint64_t late_tokens[4]{};
    CHECK(xemu_test_vk_capture_batch_native(late_tokens, 1) == 0);
    auto late = session.Snapshot();
    CHECK(late.state == CaptureSessionState::Ready);
    CHECK(late.pending_events == 0 && late.reserved_bytes == 0);
    CHECK(late_tokens[3] == 0);
    for (size_t i = 0; i < 3; ++i) {
        auto event = session.Find(late_tokens[i] & ~kCaptureSessionTokenBit);
        CHECK(event && event->command_recorded && event->resource_finalized);
        CHECK(event->submission == CaptureBatchOutcome::Submitted);
        CHECK(event->completion == CaptureBatchCompletion::Completed);
    }
    auto late_main = session.Find(late_tokens[2] & ~kCaptureSessionTokenBit);
    CHECK(ReadsProducer(*late_main, late_tokens[0] & ~kCaptureSessionTokenBit));
    std::puts("Native Vulkan Stop-before-deferred-copy retirement: PASS");
    CHECK(session.Start(context));
    session.GuestFrameBoundary(7);
    xemu_test_vk_capture_stop_at_admission();
    auto stopped = session.Snapshot();
    CHECK(stopped.state == CaptureSessionState::Ready);
    CHECK(stopped.pending_events == 0 && stopped.reserved_bytes == 0);
    CHECK(stopped.events.back()->finished && !stopped.events.back()->pending);
    CHECK(!stopped.events.back()->command_recorded);
    std::puts(
        "Native Vulkan Stop during batch admission terminalizes token: PASS");
    CHECK(session.Start(context));
    session.GuestFrameBoundary(7);
    uint64_t image_tokens[3]{};
    uint64_t clear_tokens[3]{};
    CHECK(xemu_test_vk_capture_image_copy(image_tokens, clear_tokens) == 0);
    session.Stop();
    auto images = session.Snapshot();
    CHECK(images.state == CaptureSessionState::Ready);
    for (size_t i = 0; i < 3; ++i) {
        auto event = session.Find(clear_tokens[i] & ~kCaptureSessionTokenBit);
        CHECK(event && event->type == CaptureEventType::Clear);
        CHECK(event->emitted && event->command_recorded &&
              event->resource_finalized);
        CHECK(event->command.kind == CaptureCommandKind::Clear);
        CHECK(event->command_phase == CaptureCommandPhase::Main);
        CHECK(event->submission == CaptureBatchOutcome::Submitted &&
              event->completion == CaptureBatchCompletion::Completed);
        CHECK(event->inputs.blobs.size() == 2);
        CHECK(event->inputs.blobs[0].name == "capture.vk.clear.attachments");
        CHECK(event->inputs.blobs[1].name == "capture.vk.clear.rect");
        for (const auto &access : event->resource_evidence->accesses)
            for (const auto &write : access.writes)
                CHECK(write.provenance ==
                      CaptureResourceProvenance::UnknownCoverage);
        if (i == 2) {
            CHECK(!event->replay_description);
            bool antialiasing = false, unsupported = false;
            for (const auto &reg : event->inputs.registers) {
                antialiasing |=
                    reg.name == "capture.clear.guest.antialiasing" &&
                    reg.value == 1;
                unsupported |=
                    reg.name == "capture.logical.description.status" &&
                    reg.value == 1;
            }
            CHECK(antialiasing && unsupported);
            continue;
        }
        CHECK(event->replay_description);
        const auto &clear = *event->replay_description;
        CHECK(clear.kind == XEMU_SHADER_CAPTURE_COMMAND_CLEAR);
        CHECK(clear.clear_color_mask == 15 && clear.bindings.size() == 2);
        CHECK(clear.destination_x == (i == 0 ? 0 : 1));
        CHECK(clear.destination_y == 0);
        CHECK(clear.width == (i == 0 ? 3 : 1));
        CHECK(clear.height == (i == 0 ? 3 : 2));
        const uint32_t color[] = { i == 0 ? 0x3f800000U : 0U, 0U,
                                   i == 1 ? 0x3f800000U : 0U,
                                   i == 0 ? 0x3f800000U : 0U };
        for (size_t channel = 0; channel < 4; ++channel)
            CHECK(clear.clear_color_bits[channel] == color[channel]);
        const uint32_t attachment[] = { 1,        0,        color[0],
                                        color[1], color[2], color[3] };
        const uint32_t rect[] = { i == 0 ? 0U : 1U, 0, i == 0 ? 3U : 1U,
                                  i == 0 ? 3U : 2U, 0, 1 };
        CHECK(event->inputs.blobs[0].data->bytes.size() == sizeof(attachment));
        CHECK(std::memcmp(event->inputs.blobs[0].data->bytes.data(), attachment,
                          sizeof(attachment)) == 0);
        CHECK(event->inputs.blobs[1].data->bytes.size() == sizeof(rect));
        CHECK(std::memcmp(event->inputs.blobs[1].data->bytes.data(), rect,
                          sizeof(rect)) == 0);
        for (size_t binding = 0; binding < 2; ++binding) {
            CHECK(clear.bindings[binding].role ==
                  XEMU_SHADER_CAPTURE_REPLAY_COLOR);
            CHECK(clear.bindings[binding].resource.kind ==
                  XEMU_SHADER_CAPTURE_RESOURCE_COLOR);
            CHECK(clear.bindings[binding].resource.write == binding);
            CHECK(!clear.bindings[binding].checkpoint);
            CHECK(clear.bindings[binding].image.format ==
                  (i == 0 ? XEMU_SHADER_CAPTURE_REPLAY_RGBA8_UNORM :
                            XEMU_SHADER_CAPTURE_REPLAY_BGRA8_UNORM));
        }
    }
    for (size_t i = 0; i < 3; ++i) {
        auto event = session.Find(image_tokens[i] & ~kCaptureSessionTokenBit);
        CHECK(event && event->command_recorded && event->resource_finalized);
        CHECK(event->command.kind == CaptureCommandKind::ImageCopy);
        CHECK(event->submission == CaptureBatchOutcome::Submitted);
        CHECK(event->completion == CaptureBatchCompletion::Completed);
        CHECK(event->inputs.blobs.size() == 1);
        CHECK(event->inputs.blobs[0].name == "capture.vk.image.copy");
        for (const auto &access : event->resource_evidence->accesses)
            for (const auto &write : access.writes)
                CHECK(write.provenance ==
                      CaptureResourceProvenance::UnknownCoverage);
        if (i < 2) {
            CHECK(event->replay_description);
            const auto &description = *event->replay_description;
            CHECK(description.kind == XEMU_SHADER_CAPTURE_COMMAND_IMAGE_COPY);
            CHECK(description.width == (i == 0 ? 2 : 3));
            CHECK(description.height == (i == 0 ? 2 : 3));
            CHECK(description.source_x == (i == 0 ? 1 : 0));
            CHECK(description.source_y == (i == 0 ? 1 : 0));
            CHECK(description.destination_x == 0);
            CHECK(description.destination_y == (i == 0 ? 1 : 0));
            CHECK(description.bindings.size() == 3);
            CHECK(description.bindings[0].role ==
                  XEMU_SHADER_CAPTURE_REPLAY_COPY_SOURCE);
            CHECK(description.bindings[0].resource.kind ==
                  XEMU_SHADER_CAPTURE_RESOURCE_COLOR);
            CHECK(description.bindings[0].resource.write == 0);
            CHECK(description.bindings[1].role ==
                  XEMU_SHADER_CAPTURE_REPLAY_COPY_DESTINATION);
            CHECK(description.bindings[1].resource.kind ==
                  XEMU_SHADER_CAPTURE_RESOURCE_TEXTURE);
            CHECK(description.bindings[1].resource.write == 1);
            CHECK(description.bindings[0].image.width == 3 &&
                  description.bindings[0].image.height == 3);
            CHECK(description.bindings[1].image.width == (i == 0 ? 4 : 3));
            for (const auto &binding : description.bindings) {
                CHECK(binding.image.format ==
                      (i == 0 ? XEMU_SHADER_CAPTURE_REPLAY_RGBA8_UNORM :
                                XEMU_SHADER_CAPTURE_REPLAY_BGRA8_UNORM));
                CHECK(binding.image.samples == 1 && binding.image.layers == 1 &&
                      binding.image.mip_levels == 1);
                CHECK(binding.image.coordinate_origin ==
                      XEMU_SHADER_CAPTURE_REPLAY_TOP_DOWN);
                CHECK(!binding.checkpoint && !binding.resource.ordinal);
                for (uint32_t channel = 0; channel < 4; ++channel) {
                    CHECK(binding.image.storage_to_rgba[channel] ==
                          (i == 1 && channel < 3 ? 2 - channel : channel));
                    CHECK(binding.image.sample_swizzle[channel] == channel);
                }
            }
            CHECK(description.bindings[2].resource.write == 0);
        } else {
            CHECK(!event->replay_description);
            bool unsupported = false;
            for (const auto &value : event->inputs.registers)
                unsupported |=
                    value.name == "capture.logical.description.status" &&
                    value.value == 1;
            CHECK(unsupported);
        }
    }
    const auto image_directory =
        std::filesystem::temp_directory_path() /
        ("xemu-vk-image-description-" + std::to_string(images.resource_domain));
    CHECK(session.Save(image_directory, &error));
    CaptureSessionSnapshot reopened_images;
    CHECK(CaptureSession::Reopen(image_directory, &reopened_images, &error));
    CHECK(reopened_images.events.size() == images.events.size());
    for (size_t i = 0; i < images.events.size(); ++i) {
        const auto &original = images.events[i]->replay_description;
        const auto &reopened_description =
            reopened_images.events[i]->replay_description;
        CHECK(bool(original) == bool(reopened_description));
        if (original) {
            CHECK(original->bindings.size() ==
                  reopened_description->bindings.size());
            CHECK(original->width == reopened_description->width);
            CHECK(original->height == reopened_description->height);
            CHECK(original->source_x == reopened_description->source_x);
            CHECK(original->source_y == reopened_description->source_y);
            CHECK(original->destination_x ==
                  reopened_description->destination_x);
            CHECK(original->destination_y ==
                  reopened_description->destination_y);
            CHECK(original->clear_color_mask ==
                  reopened_description->clear_color_mask);
            for (size_t channel = 0; channel < 4; ++channel)
                CHECK(original->clear_color_bits[channel] ==
                      reopened_description->clear_color_bits[channel]);
            for (size_t binding = 0; binding < original->bindings.size();
                 ++binding) {
                const auto &before = original->bindings[binding];
                const auto &after = reopened_description->bindings[binding];
                CHECK(before.resource.kind == after.resource.kind &&
                      before.resource.write == after.resource.write);
                CHECK(before.image.format == after.image.format &&
                      before.image.samples == after.image.samples);
                for (size_t channel = 0; channel < 4; ++channel)
                    CHECK(before.image.storage_to_rgba[channel] ==
                          after.image.storage_to_rgba[channel]);
            }
        }
    }
    std::filesystem::remove_all(image_directory);
    std::puts(
        "Native Vulkan logical image-copy/Draw metadata and archive: PASS");
    std::puts("Native Vulkan full/partial color clear, raw evidence and "
              "archive: PASS");
    std::puts("Native Vulkan staging append, auxiliary-before-main copies, "
              "standalone submit, immutable bytes and archive receipts: PASS");
}

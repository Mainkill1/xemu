// SPDX-License-Identifier: GPL-2.0-or-later
#include "../../ui/xui/shader-browser-capture-file-control.hh"

#include <cstdio>
#include <cstdlib>
#include <thread>

using namespace xemu::shader_browser;
#define CHECK(...)                                                     \
    do {                                                               \
        if (!(__VA_ARGS__)) {                                          \
            std::fprintf(stderr, "CHECK failed at %d: %s\n", __LINE__, \
                         #__VA_ARGS__);                                \
            std::abort();                                              \
        }                                                              \
    } while (0)

int main()
{
    // A cancelled writer must never publish its temporary directory.
    CaptureFileControl cancelled;
    CHECK(cancelled.Checkpoint(CaptureFilePhase::WritingResources, 2, 8));
    CHECK(cancelled.RequestCancel());
    CHECK(!cancelled.Checkpoint(CaptureFilePhase::WritingResources, 3, 8));
    CHECK(!cancelled.BeginPublication());
    cancelled.Finish(false);
    const auto stopped = cancelled.Progress();
    CHECK(stopped.cancel_requested && stopped.finished && !stopped.success);
    CHECK(!stopped.publication_started);
    CHECK(stopped.completed == 2 && stopped.total == 8);

    // Cancellation cannot report success after an atomic publication starts.
    CaptureFileControl committed;
    CHECK(committed.BeginPublication());
    CHECK(!committed.RequestCancel());
    committed.Finish(true);
    const auto saved = committed.Progress();
    CHECK(saved.publication_started && saved.finished && saved.success);
    CHECK(!saved.cancel_requested);
    CHECK(!committed.BeginPublication());

    // Exactly one of cancellation/publication wins even with concurrent UI
    // input and worker completion; neither may overwrite the other's outcome.
    for (unsigned iteration = 0; iteration < 256; ++iteration) {
        CaptureFileControl raced;
        bool cancellation = false, publication = false;
        std::thread ui([&] { cancellation = raced.RequestCancel(); });
        std::thread worker([&] { publication = raced.BeginPublication(); });
        ui.join();
        worker.join();
        CHECK(cancellation != publication);
        raced.Finish(publication);
        const auto result = raced.Progress();
        CHECK(result.finished && result.success == publication);
        CHECK(result.cancel_requested == cancellation);
        CHECK(result.publication_started == publication);
    }
    std::puts("Capture file cancellation/publication tests passed");
}

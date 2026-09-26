// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "shader-browser-preview-backend.hh"
#include <atomic>
#include <string>
#include <vector>

namespace xemu::shader_browser {
// Called exclusively on the private preview worker. No game renderer handles,
// loader dispatch state, pipeline cache, or command stream are used.
class PreviewVkExecutor {
public:
    PreviewVkExecutor();
    ~PreviewVkExecutor();
    PreviewVkExecutor(const PreviewVkExecutor &) = delete;
    PreviewVkExecutor &operator=(const PreviewVkExecutor &) = delete;
    bool Prepare(const PreviewWorkItem &work, std::string *error,
                 bool *unsupported);
    bool Render(const PreviewWorkItem &work, const std::atomic<bool> &stop,
                std::vector<uint8_t> *rgba, std::string *error);

#ifdef XEMU_PREVIEW_VK_TESTING
    // Deterministic native failure coverage; call only on the owning worker.
    void FailNextSamplerCreationForTest();
    bool HasSamplerSettingsForTest(bool linear, bool repeat) const;
#endif

private:
    struct Impl;
    Impl *impl_;
};
} // namespace xemu::shader_browser

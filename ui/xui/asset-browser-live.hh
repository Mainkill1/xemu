// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "asset-browser-controller.hh"
#include <future>

namespace xemu::asset_browser {
struct AssetLiveSettings {
    capture::CaptureSessionSettings capture;
    AssetLimits assets;
    uint64_t sample_interval_ns = UINT64_C(33000000);
    uint64_t progress_timeout_ns = UINT64_C(15000000000);
};
class AssetLiveCapture {
public:
    explicit AssetLiveCapture(capture::CaptureSession &);
    ~AssetLiveCapture();
    bool Enable(const capture::CaptureSessionContext &, uint64_t now_ns,
                const AssetLiveSettings & = {},
                const AssetController *selection = nullptr);
    void Disable();
    void Tick(const capture::CaptureSessionContext &, uint64_t now_ns,
              AssetController &);
    bool Enabled() const;
    const std::string &Message() const;
    uint64_t LastCaptureNs() const;
    uint64_t OwnedGeneration() const;

private:
    void RefreshFilter(const AssetController &);
    struct Result {
        AssetCatalog catalog;
        uint64_t generation = 0, claim_generation = 0;
        bool pending = false, valid = false;
    };
    capture::CaptureSession &session_;
    capture::CaptureSessionContext context_;
    AssetLiveSettings settings_;
    std::future<Result> job_;
    uint64_t owned_ = 0, started_ = 0, next_ = 0, last_capture_ = 0;
    bool enabled_ = false;
    std::string message_;
};
} // namespace xemu::asset_browser

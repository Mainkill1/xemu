// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "asset-browser-model.hh"

namespace xemu::asset_browser {
enum class AssetSelectionState : uint8_t {
    Empty,
    Captured,
    FollowingCandidate,
    Frozen,
    Missing,
    Ambiguous,
    Incomplete,
    ScopeChanged
};
class AssetController {
public:
    uint64_t Begin(const capture::CaptureSessionContext &,
                   bool reset_selection = false);
    uint64_t Generation() const;
    bool Publish(AssetCatalog, uint64_t generation);
    bool Select(uint64_t entry);
    bool Assemble(const std::vector<uint64_t> &, const std::string &label);
    bool Rename(const std::string &label);
    bool RememberSelected();
    bool Recall(size_t index);
    void Forget(size_t index);
    const std::vector<std::shared_ptr<const AssetAssembly>> &
    NamedAssemblies() const;
    void Pin(bool);
    void Freeze(bool);
    void Invalidate();
    bool Pinned() const;
    bool Frozen() const;
    AssetSelectionState State() const;
    const std::string &Message() const;
    const AssetCatalog &Catalog() const;
    std::shared_ptr<const AssetAssembly> Selected() const;
    std::shared_ptr<const capture::CaptureSessionSnapshot>
    SelectedRecording() const;

private:
    uint64_t generation_ = 0;
    capture::CaptureSessionContext context_;
    AssetCatalog catalog_;
    std::shared_ptr<const AssetAssembly> selected_;
    std::shared_ptr<const capture::CaptureSessionSnapshot> selected_recording_;
    std::vector<std::shared_ptr<const AssetAssembly>> named_;
    std::vector<std::shared_ptr<const capture::CaptureSessionSnapshot>>
        named_recordings_;
    bool pinned_ = false, frozen_ = false;
    AssetSelectionState state_ = AssetSelectionState::Empty;
    std::string message_;
};
bool SameAssetContext(const capture::CaptureSessionContext &,
                      const capture::CaptureSessionContext &);
} // namespace xemu::asset_browser

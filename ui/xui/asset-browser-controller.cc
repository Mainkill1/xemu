// SPDX-License-Identifier: GPL-2.0-or-later
#include "asset-browser-controller.hh"
#include <algorithm>
#include <cstring>
#include <map>
#include <set>
#include <limits>
namespace xemu::asset_browser {
namespace {
bool SameGeometry(const AssetPart &a, const AssetPart &b)
{
    if (a.status != AssetStatus::Ready || b.status != AssetStatus::Ready ||
        a.geometry_signature != b.geometry_signature ||
        a.indices != b.indices || a.vertices.size() != b.vertices.size())
        return false;
    for (size_t i = 0; i < a.vertices.size(); ++i)
        if (std::memcmp(a.vertices[i].position.data(),
                        b.vertices[i].position.data(),
                        sizeof(a.vertices[i].position)))
            return false;
    return true;
}
} // namespace
static bool FitsRecordings(
    const std::vector<std::shared_ptr<const capture::CaptureSessionSnapshot>>
        &recordings)
{
    uint64_t remaining = 256U * 1024U * 1024U;
    std::set<const capture::CaptureSessionSnapshot *> seen;
    for (const auto &recording : recordings) {
        if (!recording || !seen.insert(recording.get()).second)
            continue;
        if (recording->cpu_bytes > remaining)
            return false;
        remaining -= recording->cpu_bytes;
        for (const auto &event : recording->events) {
            const auto bytes =
                event ? capture::CaptureOccurrenceDescriptorBytes(*event) : 0;
            if (bytes > remaining)
                return false;
            remaining -= bytes;
        }
    }
    return true;
}
static bool FitsDecodedAssemblies(
    const std::vector<std::shared_ptr<const AssetAssembly>> &assemblies)
{
    uint64_t remaining = 128U * 1024U * 1024U;
    std::set<const AssetPart *> seen;
    for (const auto &assembly : assemblies)
        for (const auto &part : assembly->parts) {
            if (!seen.insert(part.get()).second)
                continue;
            if (!part ||
                part->vertices.size() > remaining / sizeof(AssetVertex))
                return false;
            remaining -= part->vertices.size() * sizeof(AssetVertex);
            if (part->indices.size() > remaining / sizeof(uint32_t))
                return false;
            remaining -= part->indices.size() * sizeof(uint32_t);
        }
    return true;
}
uint64_t AssetController::Begin(const capture::CaptureSessionContext &context,
                                bool reset_selection)
{
    if (generation_ == UINT64_MAX)
        return 0;
    if (reset_selection || !SameAssetContext(context, context_)) {
        selected_.reset();
        selected_recording_.reset();
        pinned_ = false;
        named_.clear();
        named_recordings_.clear();
    }
    context_ = context;
    catalog_ = {};
    catalog_.context = context;
    frozen_ = false;
    message_.clear();
    state_ =
        selected_ ? AssetSelectionState::Captured : AssetSelectionState::Empty;
    return ++generation_;
}
uint64_t AssetController::Generation() const
{
    return generation_;
}
bool AssetController::Publish(AssetCatalog catalog, uint64_t generation)
{
    if (!generation || generation != generation_ || frozen_)
        return false;
    if (!SameAssetContext(catalog.context, context_)) {
        Invalidate();
        return false;
    }
    if (catalog.frame < catalog_.frame)
        return false;
    catalog_ = std::move(catalog);
    if (!selected_ || !pinned_)
        return true;
    if (!catalog_.complete_frame) {
        state_ = AssetSelectionState::Incomplete;
        message_ = "Partial capture; retaining the last coherent assembly";
        return true;
    }
    std::map<capture::CaptureDigest, std::vector<SharedAssetPart>> candidates;
    for (const auto &part : catalog_.parts)
        if (part->status == AssetStatus::Ready)
            candidates[part->geometry_signature].push_back(part);
    std::vector<uint64_t> ids;
    std::set<uint64_t> claimed;
    for (const auto &anchor : selected_->parts) {
        SharedAssetPart match;
        for (const auto &candidate : candidates[anchor->geometry_signature]) {
            if (!SameGeometry(*anchor, *candidate))
                continue;
            if (match) {
                state_ = AssetSelectionState::Ambiguous;
                message_ = "Several draws match this part; retained view is "
                           "stale. Confirm the intended occurrence.";
                return true;
            }
            match = candidate;
        }
        if (!match || !claimed.insert(match->id).second) {
            state_ = AssetSelectionState::Missing;
            message_ = "A part is missing or changed LOD; retaining the last "
                       "coherent assembly";
            return true;
        }
        ids.push_back(match->id);
    }
    auto assembly = MakeAssetAssembly(catalog_, ids, selected_->label,
                                      selected_->user_confirmed);
    if (assembly.parts.size() != selected_->parts.size() ||
        !assembly.complete) {
        state_ = AssetSelectionState::Incomplete;
        message_ =
            "Incomplete correspondence; retaining the last coherent assembly";
        return true;
    }
    auto previous = selected_;
    selected_ = std::make_shared<const AssetAssembly>(std::move(assembly));
    selected_recording_ = catalog_.recording;
    bool retention_limited = false;
    for (size_t i = 0; i < named_.size(); ++i)
        if (named_[i] == previous) {
            auto recordings = named_recordings_;
            recordings[i] = selected_recording_;
            auto assemblies = named_;
            assemblies[i] = selected_;
            if (FitsRecordings(recordings) &&
                FitsDecodedAssemblies(assemblies)) {
                named_ = std::move(assemblies);
                named_recordings_ = std::move(recordings);
            } else
                retention_limited = true;
        }
    state_ = AssetSelectionState::FollowingCandidate;
    message_ = "Unique geometry correspondence is inferred; engine instance "
               "ownership is unverified";
    if (retention_limited)
        message_ += "; named retention budget reached, saved name remains on "
                    "its prior frame";
    return true;
}
bool AssetController::Select(uint64_t id)
{
    const auto found =
        std::find_if(catalog_.entries.begin(), catalog_.entries.end(),
                     [id](const auto &entry) { return entry.id == id; });
    if (found == catalog_.entries.end())
        return false;
    selected_ = std::make_shared<const AssetAssembly>(*found);
    selected_recording_ = catalog_.recording;
    pinned_ = false;
    state_ =
        frozen_ ? AssetSelectionState::Frozen : AssetSelectionState::Captured;
    message_.clear();
    return true;
}
bool AssetController::Assemble(const std::vector<uint64_t> &ids,
                               const std::string &label)
{
    if (ids.empty() || ids.size() > 256 || label.size() > 255)
        return false;
    auto assembly = MakeAssetAssembly(catalog_, ids, label, true);
    if (assembly.parts.empty())
        return false;
    selected_ = std::make_shared<const AssetAssembly>(std::move(assembly));
    selected_recording_ = catalog_.recording;
    state_ =
        frozen_ ? AssetSelectionState::Frozen : AssetSelectionState::Captured;
    message_.clear();
    return true;
}
bool AssetController::Rename(const std::string &label)
{
    if (!selected_ || label.size() > 255)
        return false;
    auto assembly = *selected_;
    assembly.label = label;
    selected_ = std::make_shared<const AssetAssembly>(std::move(assembly));
    return true;
}
bool AssetController::RememberSelected()
{
    if (!selected_ || selected_->label.empty() || !selected_recording_) {
        message_ = "A named assembly requires its owned recording";
        return false;
    }
    auto replacement = named_;
    auto recordings = named_recordings_;
    auto found = std::find_if(
        replacement.begin(), replacement.end(),
        [&](const auto &entry) { return entry->label == selected_->label; });
    if (found == replacement.end()) {
        replacement.push_back(selected_);
        recordings.push_back(selected_recording_);
    } else {
        recordings[size_t(found - replacement.begin())] = selected_recording_;
        *found = selected_;
    }
    if (replacement.size() > 32)
        return false;
    if (!FitsRecordings(recordings)) {
        message_ =
            "Named assemblies exceed the 256 MiB retained recording budget";
        return false;
    }
    if (!FitsDecodedAssemblies(replacement)) {
        message_ =
            "Named assemblies exceed the 128 MiB decoded geometry budget";
        return false;
    }
    named_ = std::move(replacement);
    named_recordings_ = std::move(recordings);
    return true;
}
bool AssetController::Recall(size_t index)
{
    if (index >= named_.size() ||
        !SameAssetContext(named_[index]->context, context_))
        return false;
    selected_ = named_[index];
    selected_recording_ = named_recordings_[index];
    pinned_ = false;
    state_ =
        frozen_ ? AssetSelectionState::Frozen : AssetSelectionState::Captured;
    message_ = "Retained named assembly; enable Follow to search for a new "
               "correspondence";
    return true;
}
void AssetController::Forget(size_t index)
{
    if (index < named_.size()) {
        named_.erase(named_.begin() + index);
        named_recordings_.erase(named_recordings_.begin() + index);
    }
}
const std::vector<std::shared_ptr<const AssetAssembly>> &
AssetController::NamedAssemblies() const
{
    return named_;
}
void AssetController::Pin(bool enabled)
{
    pinned_ = enabled && bool(selected_);
}
void AssetController::Freeze(bool enabled)
{
    if (enabled == frozen_)
        return;
    if (generation_ != UINT64_MAX)
        ++generation_;
    frozen_ = enabled;
    state_ = enabled   ? AssetSelectionState::Frozen :
             selected_ ? AssetSelectionState::Captured :
                         AssetSelectionState::Empty;
    message_ = enabled ? "Frozen owned captured assembly" : "Captured assembly";
}
void AssetController::Invalidate()
{
    if (generation_ != UINT64_MAX)
        ++generation_;
    frozen_ = true;
    state_ = AssetSelectionState::ScopeChanged;
    message_ = "Game or renderer changed; the retained assembly is frozen";
}
bool AssetController::Pinned() const
{
    return pinned_;
}
bool AssetController::Frozen() const
{
    return frozen_;
}
AssetSelectionState AssetController::State() const
{
    return state_;
}
const std::string &AssetController::Message() const
{
    return message_;
}
const AssetCatalog &AssetController::Catalog() const
{
    return catalog_;
}
std::shared_ptr<const AssetAssembly> AssetController::Selected() const
{
    return selected_;
}
std::shared_ptr<const capture::CaptureSessionSnapshot>
AssetController::SelectedRecording() const
{
    return selected_recording_;
}
bool SameAssetContext(const capture::CaptureSessionContext &a,
                      const capture::CaptureSessionContext &b)
{
    return a.scope == b.scope && a.scope_generation == b.scope_generation &&
           a.session_epoch == b.session_epoch &&
           a.renderer_epoch == b.renderer_epoch && a.backend == b.backend;
}
} // namespace xemu::asset_browser

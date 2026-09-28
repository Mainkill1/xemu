// SPDX-License-Identifier: GPL-2.0-or-later
#include "asset-browser-export.hh"
#include "asset-browser-controller.hh"
#include "shader-browser-capture-inspection.hh"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <set>
#include <stdexcept>
namespace xemu::asset_browser {
namespace {
using Json = nlohmann::json;
bool Fail(std::string *error, const std::string &message)
{
    if (error)
        *error = message;
    return false;
}
void Check(capture::CaptureFileControl *control,
           capture::CaptureFilePhase phase)
{
    if (control && !control->Checkpoint(phase))
        throw std::runtime_error("Asset file operation cancelled");
}
} // namespace
bool SaveAssetRecording(const AssetCatalog &catalog,
                        const AssetAssembly *selected,
                        const std::filesystem::path &path, std::string *error,
                        capture::CaptureFileControl *control)
{
    if (error)
        error->clear();
    try {
        Check(control, capture::CaptureFilePhase::Preparing);
        if (!catalog.recording)
            throw std::runtime_error("An owned capture recording is required");
        auto snapshot = *catalog.recording;
        capture::CaptureInspectionController annotations;
        if (!annotations.Open(snapshot.context.generation, snapshot))
            throw std::runtime_error(annotations.Error());
        if (!snapshot.annotations.empty() &&
            !annotations.RestoreAnnotations(snapshot.annotations, error))
            throw std::runtime_error(error ? *error :
                                             "Invalid capture annotations");
        std::string text;
        if (!annotations.ExportAnnotations(&text, error))
            throw std::runtime_error(error ? *error :
                                             "Cannot export annotations");
        auto document = Json::parse(text);
        Json ids = Json::array();
        if (selected) {
            if (selected->parts.size() > 256 || selected->label.size() > 255 ||
                !SameAssetContext(selected->context, snapshot.context))
                throw std::runtime_error(
                    "Selected assembly belongs to a different capture scope");
            for (const auto &part : selected->parts) {
                if (!part || !part->occurrence ||
                    std::find(snapshot.events.begin(), snapshot.events.end(),
                              part->occurrence) == snapshot.events.end())
                    throw std::runtime_error(
                        "Selected assembly is from an older capture; save the "
                        "selected assembly separately");
                ids.push_back(part->id);
            }
        }
        document["asset_browser"] = {
            { "version", 1 },
            { "label", selected ? selected->label : "" },
            { "frame", selected ? selected->frame : catalog.frame },
            { "parts", ids },
            { "user_confirmed", selected && selected->user_confirmed }
        };
        snapshot.annotations = document.dump();
        return capture::CaptureSession::SaveSnapshot(snapshot, path, error,
                                                     control);
    } catch (const std::exception &exception) {
        if (control)
            control->Finish(false);
        return Fail(error, exception.what());
    }
}
bool SaveAssetAssembly(const AssetAssembly &assembly,
                       const std::filesystem::path &path, std::string *error,
                       capture::CaptureFileControl *control)
{
    if (assembly.parts.empty() || assembly.parts.size() > 256 ||
        (control &&
         !control->Checkpoint(capture::CaptureFilePhase::Preparing))) {
        if (control)
            control->Finish(false);
        return Fail(error,
                    "Assembly is empty, over budget or export was cancelled");
    }
    capture::CaptureSessionSnapshot snapshot;
    snapshot.context = assembly.context;
    snapshot.state = capture::CaptureSessionState::Cancelled;
    snapshot.execution_order_complete = false;
    snapshot.reason = "Selected asset inputs only; ordered operations and "
                      "dependencies outside these parts are omitted. This "
                      "package is not a complete frame replay";
    std::set<uint64_t> seen;
    for (const auto &part : assembly.parts) {
        if (!part || !part->occurrence || part->occurrence->pending ||
            !seen.insert(part->id).second) {
            if (control)
                control->Finish(false);
            return Fail(
                error,
                "Assembly has missing, pending or duplicate owned parts");
        }
        snapshot.events.push_back(part->occurrence);
    }
    std::sort(
        snapshot.events.begin(), snapshot.events.end(),
        [](const auto &a, const auto &b) { return a->event_id < b->event_id; });
    snapshot.total_events = snapshot.events.size();
    auto catalog = BuildAssetCatalog(snapshot);
    return SaveAssetRecording(catalog, &assembly, path, error, control);
}
bool ReopenAssetRecording(const std::filesystem::path &path,
                          AssetCatalog *catalog,
                          std::shared_ptr<const AssetAssembly> *selected,
                          std::string *error,
                          capture::CaptureFileControl *control)
{
    if (catalog)
        *catalog = {};
    if (selected)
        selected->reset();
    if (error)
        error->clear();
    if (!catalog || !selected) {
        if (control)
            control->Finish(false);
        return Fail(error, "Asset destination is missing");
    }
    try {
        Check(control, capture::CaptureFilePhase::ReadingManifest);
        capture::CaptureSessionSnapshot snapshot;
        if (!capture::CaptureSession::Reopen(path, &snapshot, error)) {
            if (control)
                control->Finish(false);
            return false;
        }
        Check(control, capture::CaptureFilePhase::Validating);
        auto result = BuildAssetCatalog(snapshot);
        std::shared_ptr<const AssetAssembly> assembly;
        if (!snapshot.annotations.empty()) {
            capture::CaptureInspectionController annotations;
            if (!annotations.Open(snapshot.context.generation, snapshot) ||
                !annotations.RestoreAnnotations(snapshot.annotations, error))
                throw std::runtime_error(error && !error->empty() ?
                                             *error :
                                             "Invalid capture annotations");
            auto document = Json::parse(snapshot.annotations);
            if (document.contains("asset_browser")) {
                const auto &asset = document.at("asset_browser");
                if (asset.at("version") != 1 ||
                    !asset.at("label").is_string() ||
                    !asset.at("parts").is_array() ||
                    asset.at("parts").size() > 256 ||
                    !asset.at("frame").is_number_unsigned() ||
                    !asset.at("user_confirmed").is_boolean())
                    throw std::runtime_error("Invalid asset annotation schema");
                const auto label = asset.at("label").get<std::string>();
                if (label.size() > 255)
                    throw std::runtime_error("Asset label exceeds budget");
                std::vector<uint64_t> ids;
                std::set<uint64_t> unique;
                for (const auto &id : asset.at("parts")) {
                    if (!id.is_number_unsigned() ||
                        !unique.insert(id.get<uint64_t>()).second)
                        throw std::runtime_error(
                            "Invalid or duplicate asset member");
                    ids.push_back(id.get<uint64_t>());
                }
                if (!ids.empty()) {
                    if (asset.at("frame").get<uint64_t>() != result.frame)
                        throw std::runtime_error(
                            "Asset annotation references another frame");
                    auto restored = MakeAssetAssembly(
                        result, ids, label,
                        asset.at("user_confirmed").get<bool>());
                    if (restored.parts.size() != ids.size())
                        throw std::runtime_error(
                            "Asset annotation references missing parts");
                    assembly = std::make_shared<const AssetAssembly>(
                        std::move(restored));
                }
            }
        }
        if (control && !control->BeginPublication())
            throw std::runtime_error("Asset open cancelled");
        *catalog = std::move(result);
        *selected = std::move(assembly);
        if (control)
            control->Finish(true);
        return true;
    } catch (const std::exception &exception) {
        if (control)
            control->Finish(false);
        return Fail(error, exception.what());
    }
}
bool ReopenAssetAssembly(const std::filesystem::path &path,
                         AssetAssembly *assembly, std::string *error)
{
    if (!assembly)
        return Fail(error, "Assembly destination is missing");
    *assembly = {};
    AssetCatalog catalog;
    std::shared_ptr<const AssetAssembly> selected;
    if (!ReopenAssetRecording(path, &catalog, &selected, error) || !selected)
        return false;
    *assembly = *selected;
    return true;
}
} // namespace xemu::asset_browser

// SPDX-License-Identifier: GPL-2.0-or-later
#include "asset-browser.hh"
#include "asset-browser-viewport.hh"
#include "asset-browser-placement.hh"
#include "asset-browser-export.hh"
#include <imgui.h>
#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>
#include <set>
#include <chrono>
#include <cstring>

using namespace xemu::asset_browser;
struct AssetBrowserWindow::Impl {
    struct FileResult {
        AssetCatalog catalog;
        std::shared_ptr<const AssetAssembly> selected;
        uint64_t generation = 0;
        bool opened = false, success = false, remember = true;
        std::string message;
    };
    AssetController controller;
    AssetLiveCapture live;
    AssetViewport viewport;
    ContextSource context;
    ShaderSink shader;
    PausedSource paused;
    AssetCamera camera{ .55f, .45f, 1.5f };
    ImGuiTextFilter filter;
    std::set<uint64_t> checked;
    uint64_t checked_frame = 0;
    char label[256] = "My car";
    char path[1024] = "asset-capture";
    char glb_path[1024] = "asset.glb";
    bool was_open = false, inspector = true, wire = false, largest = true;
    bool captured_stages = true;
    bool projected_output = false;
    std::weak_ptr<const AssetAssembly> camera_selection;
    uint64_t pose_frame = 0, pose_started_ns = 0, pose_samples = 0,
             pose_updated_ns = 0;
    double pose_hz = 0;
    int texture_slot = -1;
    std::string message;
    SDL_Window *owner_window = nullptr;
    SDL_GLContext owner_context = nullptr;
    std::future<FileResult> file;
    std::shared_ptr<capture::CaptureFileControl> file_control;
    uint64_t file_generation = 0;
    bool opening = false;
    uint64_t inspected_part = 0;
    int image_slot = 0;
    int capture_mib = 256, decoded_mib = 128, mesh_mib = 256;
    int event_limit = 32768, part_limit = 2048, vertex_limit = 1048576;
    int index_limit = 3145728, sample_ms = 33, thumbnails = 24;
    enum class FileAction { SaveFrame, SaveAssembly, Glb, ScratchGlb, Open };
    void StartFile(FileAction action)
    {
        if (file.valid())
            return;
        auto assembly = controller.Selected();
        if ((action == FileAction::SaveAssembly || action == FileAction::Glb ||
             action == FileAction::ScratchGlb) &&
            !assembly) {
            message = "Select an assembly first";
            return;
        }
        if (action != FileAction::ScratchGlb &&
            !(action == FileAction::Glb ? glb_path[0] : path[0])) {
            message = "Enter a capture directory or GLB filename";
            return;
        }
        auto catalog = controller.Catalog();
        if (action == FileAction::SaveFrame && controller.SelectedRecording()) {
            catalog.recording = controller.SelectedRecording();
            catalog.context = catalog.recording->context;
            if (assembly)
                catalog.frame = assembly->frame;
        }
        std::filesystem::path destination;
        try {
            if (action == FileAction::ScratchGlb) {
                char *preferences = SDL_GetPrefPath("xemu", "xemu");
                if (!preferences) {
                    message = "Cannot locate scratch GLB folder: " +
                              std::string(SDL_GetError());
                    return;
                }
                destination = std::filesystem::u8path(preferences) /
                              "asset-browser" / "scratch.glb";
                SDL_free(preferences);
                std::filesystem::create_directories(destination.parent_path());
            } else
                destination = std::filesystem::u8path(
                    action == FileAction::Glb ? glb_path : path);
        } catch (const std::exception &exception) {
            message = exception.what();
            return;
        }
        opening = action == FileAction::Open;
        if (opening) {
            live.Disable();
            controller.Freeze(true);
        }
        auto control = std::make_shared<capture::CaptureFileControl>();
        const auto generation = ++file_generation;
        try {
            file = std::async(std::launch::async, [action, assembly,
                                                   catalog = std::move(catalog),
                                                   destination, control,
                                                   generation] {
                FileResult result;
                result.generation = generation;
                std::string error;
                try {
                    switch (action) {
                    case FileAction::SaveFrame:
                        result.success = SaveAssetRecording(
                            catalog, assembly.get(), destination, &error,
                            control.get());
                        break;
                    case FileAction::SaveAssembly:
                        result.success = SaveAssetAssembly(
                            *assembly, destination, &error, control.get());
                        break;
                    case FileAction::Glb:
                    case FileAction::ScratchGlb:
                        result.success = ExportAssetGlb(
                            *assembly, destination, &error, control.get(),
                            action == FileAction::ScratchGlb);
                        break;
                    case FileAction::Open:
                        result.success = ReopenAssetRecording(
                            destination, &result.catalog, &result.selected,
                            &error, control.get());
                        result.opened = result.success;
                        break;
                    }
                } catch (const std::exception &exception) {
                    error = exception.what();
                    control->Finish(false);
                }
                result.message =
                    result.success ?
                        (action == FileAction::Open ? "Opened " : "Saved ") +
                            std::filesystem::absolute(destination).u8string() :
                        error;
                return result;
            });
            file_control = std::move(control);
        } catch (const std::exception &exception) {
            opening = false;
            message = exception.what();
        }
    }
    Impl(capture::CaptureSession &session, ContextSource source,
         ShaderSink sink, PausedSource paused_source)
        : live(session), context(std::move(source)), shader(std::move(sink)),
          paused(std::move(paused_source))
    {
    }
};
AssetBrowserWindow::AssetBrowserWindow(capture::CaptureSession &session,
                                       ContextSource source, ShaderSink sink,
                                       PausedSource paused)
    : impl_(std::make_unique<Impl>(session, std::move(source), std::move(sink),
                                   std::move(paused)))
{
}
AssetBrowserWindow::~AssetBrowserWindow() = default;
bool AssetBrowserWindow::InspectCatalog(AssetCatalog catalog)
{
    impl_->live.Disable();
    ++impl_->file_generation;
    if (impl_->file_control)
        impl_->file_control->RequestCancel();
    impl_->checked.clear();
    impl_->checked_frame = 0;
    impl_->inspected_part = 0;
    auto generation = impl_->controller.Begin(catalog.context, true);
    const bool accepted =
        impl_->controller.Publish(std::move(catalog), generation);
    m_is_open = accepted;
    return accepted;
}
std::shared_ptr<const AssetAssembly> AssetBrowserWindow::Selected() const
{
    return impl_->controller.Selected();
}
bool AssetBrowserWindow::InspectOccurrence(
    std::shared_ptr<const capture::CaptureOccurrence> occurrence,
    const capture::CaptureSessionContext &context,
    std::shared_ptr<const capture::CaptureSessionSnapshot> recording)
{
    auto &s = *impl_;
    if (!occurrence || occurrence->pending || s.file.valid())
        return false;
    s.live.Disable();
    s.controller.Freeze(true);
    const auto generation = ++s.file_generation;
    try {
        s.file = std::async(std::launch::async, [occurrence, context, recording,
                                                 generation] {
            Impl::FileResult result;
            result.generation = generation;
            result.remember = false;
            capture::CaptureSessionSnapshot selected;
            selected.context = context;
            selected.events = { occurrence };
            result.catalog = BuildAssetCatalog(selected);
            if (recording)
                result.catalog.recording = recording;
            if (!result.catalog.entries.empty()) {
                result.selected = std::make_shared<const AssetAssembly>(
                    result.catalog.entries.front());
                result.opened = result.success = true;
                result.message = "Selected captured occurrence; use Fit and "
                                 "camera controls to inspect it.";
            } else {
                result.message = "This occurrence has no supported geometry: ";
                if (!result.catalog.parts.empty())
                    result.message += result.catalog.parts.front()->reason;
            }
            return result;
        });
    } catch (const std::exception &exception) {
        s.message = exception.what();
        return false;
    }
    s.opening = true;
    s.captured_stages = true;
    s.projected_output = true;
    s.camera = {};
    s.message = "Preparing captured occurrence view";
    m_is_open = true;
    return true;
}
void AssetBrowserWindow::Shutdown()
{
    impl_->live.Disable();
    ++impl_->file_generation;
    if (impl_->file_control)
        impl_->file_control->RequestCancel();
    if (impl_->owner_context) {
        auto *previous_window = SDL_GL_GetCurrentWindow();
        auto previous = SDL_GL_GetCurrentContext();
        if (SDL_GL_MakeCurrent(impl_->owner_window, impl_->owner_context)) {
            impl_->viewport.Shutdown();
            if (previous && previous != impl_->owner_context)
                SDL_GL_MakeCurrent(previous_window, previous);
        }
        impl_->owner_window = nullptr;
        impl_->owner_context = nullptr;
    }
    impl_->was_open = false;
}
void AssetBrowserWindow::Draw()
{
    auto &s = *impl_;
    if (!m_is_open) {
        if (s.was_open)
            Shutdown();
        return;
    }
    s.was_open = true;
    if (!s.owner_context) {
        s.owner_context = SDL_GL_GetCurrentContext();
        s.owner_window = SDL_GL_GetCurrentWindow();
    }
    const uint64_t now = SDL_GetTicksNS();
    if (s.file.valid() &&
        s.file.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        try {
            auto result = s.file.get();
            if (result.generation == s.file_generation) {
                s.message = result.message;
                if (result.opened) {
                    InspectCatalog(std::move(result.catalog));
                    if (result.selected) {
                        std::vector<uint64_t> ids{ result.selected->id };
                        for (const auto &part : result.selected->parts)
                            if (part->id != result.selected->id)
                                ids.push_back(part->id);
                        if (result.remember) {
                            s.controller.Assemble(ids, result.selected->label);
                            s.controller.RememberSelected();
                        } else
                            s.controller.Select(result.selected->id);
                    }
                    s.controller.Freeze(true);
                }
            }
        } catch (const std::exception &exception) {
            s.message = exception.what();
        }
        s.opening = false;
        s.file_control.reset();
    }
    auto context = s.context();
    s.live.Tick(context, now, s.controller, s.paused && s.paused());
    ImGui::SetNextWindowSize(ImVec2(1100, 700), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Asset Browser", &m_is_open)) {
        s.live.Disable();
        s.viewport.Shutdown();
        ImGui::End();
        return;
    }
    bool live = s.live.Enabled();
    const bool toggle_live = ImGui::Checkbox("Live discovery", &live);
    ImGui::SameLine();
    const bool retry_live = !live && ImGui::Button("Retry discovery");
    if (toggle_live || retry_live) {
        live |= retry_live;
        if (live) {
            if (!context.scope.title_id)
                s.message = "Load a game before starting discovery";
            else {
                if (s.controller.Frozen())
                    s.controller.Freeze(false);
                if (!s.controller.Generation() ||
                    !SameAssetContext(s.controller.Catalog().context, context))
                    s.controller.Begin(context);
                AssetLiveSettings settings;
                settings.capture.event_budget = uint32_t(s.event_limit);
                settings.capture.cpu_byte_budget = uint64_t(s.capture_mib)
                                                   << 20;
                settings.assets.maximum_parts = size_t(s.part_limit);
                settings.assets.maximum_vertices = size_t(s.vertex_limit);
                settings.assets.maximum_indices = size_t(s.index_limit);
                settings.assets.decoded_byte_budget = uint64_t(s.decoded_mib)
                                                      << 20;
                settings.sample_interval_ns = uint64_t(s.sample_ms) * 1000000;
                s.live.Enable(context, now, settings, &s.controller);
                s.message.clear();
            }
        } else
            s.live.Disable();
    }
    ImGui::SameLine();
    if (ImGui::Button("Freeze discovery")) {
        s.live.Disable();
        s.controller.Freeze(true);
    }
    ImGui::SameLine();
    bool pinned = s.controller.Pinned();
    if (ImGui::Checkbox("Follow selected assembly", &pinned))
        s.controller.Pin(pinned);
    ImGui::SameLine();
    ImGui::Checkbox("Inspector", &s.inspector);
    if (!s.live.Message().empty())
        ImGui::TextWrapped("%s", s.live.Message().c_str());
    if (!s.message.empty())
        ImGui::TextWrapped("%s", s.message.c_str());
    if (ImGui::CollapsingHeader("Capture settings")) {
        auto limit = [](const char *label, int &value, int minimum,
                        int maximum) {
            ImGui::SliderInt(label, &value, minimum, maximum, "%d",
                             ImGuiSliderFlags_AlwaysClamp);
        };
        ImGui::BeginDisabled(s.live.Enabled());
        ImGui::TextUnformatted(
            "Stop Live discovery before changing acquisition budgets.");
        ImGui::TextWrapped(
            "Capture memory includes shader/state metadata and pending "
            "readbacks, in addition to geometry and textures.");
        limit("Capture memory (MiB)", s.capture_mib, 32, 512);
        limit("Decoded geometry (MiB)", s.decoded_mib, 16, 256);
        limit("Events per capture", s.event_limit, 512, 32768);
        limit("Parts per frame", s.part_limit, 64, 8192);
        limit("Vertices per part", s.vertex_limit, 4096, 1048576);
        limit("Triangle indices per part", s.index_limit, 12288, 3145728);
        limit("Sample interval (ms)", s.sample_ms, 33, 2000);
        ImGui::EndDisabled();
        limit("GPU mesh cache (MiB)", s.mesh_mib, 16, 256);
        limit("Thumbnail cache", s.thumbnails, 4, 64);
        ImGui::TextUnformatted(
            "Collapse or close stops discovery and releases GPU caches.");
        ImGui::TextUnformatted(
            "Named recordings: 256 MiB; named geometry: 128 MiB.");
    }
    s.viewport.Configure(uint64_t(s.mesh_mib) << 20, size_t(s.thumbnails));
    if (ImGui::CollapsingHeader("How to view your car",
                                ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::TextWrapped(
            "1. Start Live discovery in a race, then Freeze discovery when "
            "parts appear. The game keeps running.");
        ImGui::TextWrapped("2. Select a body-shaped thumbnail on the left. "
                           "Up/Down inspects nearby entries; these entries are "
                           "draw parts, so a car needs several parts.");
        ImGui::TextWrapped(
            "3. Use Suggest related parts, inspect the suggested "
            "wheels/glass/body, and check only parts that belong to your car.");
        ImGui::TextWrapped(
            "4. Name the selection and click Assemble checked parts. Enable "
            "Follow selected assembly and Live discovery for motion; the "
            "viewer keeps its camera angle.");
        ImGui::TextWrapped(
            "5. Use the Inspector to open a part's captured draw in Shader "
            "Browser for its stages, inputs, and GLSL replacement editor.");
        ImGui::TextWrapped(
            "Related parts and following use geometry evidence; player-car "
            "identity is not verified. A capture limit stops discovery, not "
            "the game. Retry keeps your current budgets.");
    }
    ImGui::SetNextItemWidth(350);
    ImGui::InputText("Capture directory", s.path, sizeof(s.path));
    ImGui::BeginDisabled(s.file.valid());
    if (ImGui::Button("Save captured frame"))
        s.StartFile(Impl::FileAction::SaveFrame);
    ImGui::SameLine();
    if (ImGui::Button("Extract selected inputs"))
        s.StartFile(Impl::FileAction::SaveAssembly);
    ImGui::SameLine();
    if (ImGui::Button("Update scratch GLB"))
        s.StartFile(Impl::FileAction::ScratchGlb);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Export the selection to xemu's scratch.glb. Each "
                          "successful export replaces the previous scratch.");
    ImGui::SameLine();
    if (ImGui::Button("Save GLB as..."))
        ImGui::OpenPopup("Save GLB copy");
    ImGui::SameLine();
    if (ImGui::Button("Open capture"))
        s.StartFile(Impl::FileAction::Open);
    ImGui::EndDisabled();
    if (ImGui::BeginPopupModal("Save GLB copy", nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("Choose a filename to keep this selection.");
        ImGui::SetNextItemWidth(500);
        ImGui::InputText("GLB filename", s.glb_path, sizeof(s.glb_path));
        ImGui::BeginDisabled(s.file.valid() || !s.glb_path[0]);
        if (ImGui::Button("Save copy")) {
            s.StartFile(Impl::FileAction::Glb);
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Cancel"))
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    if (s.file_control) {
        auto progress = s.file_control->Progress();
        ImGui::Text("File work: %llu / %llu",
                    (unsigned long long)progress.completed,
                    (unsigned long long)progress.total);
        ImGui::SameLine();
        if (ImGui::Button("Cancel file work") &&
            !s.file_control->RequestCancel())
            s.message = "Output publication has begun; waiting for completion";
    }
    const auto &catalog = s.controller.Catalog();
    if (s.checked_frame != catalog.frame) {
        s.checked.clear();
        s.checked_frame = catalog.frame;
    }
    ImGui::Text("Frame %llu | %zu captured parts | %.1f MiB decoded | %s",
                (unsigned long long)catalog.frame, catalog.parts.size(),
                double(catalog.decoded_bytes) / (1 << 20),
                catalog.complete_frame ? "Coherent captured input frame" :
                                         "Incomplete frame");
    std::vector<size_t> visible;
    for (size_t i = 0; i < catalog.entries.size(); ++i) {
        const auto &entry = catalog.entries[i];
        const std::string text = entry.label + " E" + std::to_string(entry.id);
        if (s.filter.PassFilter(text.c_str()))
            visible.push_back(i);
    }
    if (s.largest)
        std::stable_sort(
            visible.begin(), visible.end(), [&](size_t a, size_t b) {
                auto count = [](const AssetAssembly &entry) {
                    size_t n = 0;
                    for (const auto &part : entry.parts)
                        n += part->indices.size();
                    return n;
                };
                return count(catalog.entries[a]) > count(catalog.entries[b]);
            });
    auto selected = s.controller.Selected();
    int scroll_row = -1;
    // Live navigation updates the selected occurrence immediately and keeps
    // stable IDs.
    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
        !ImGui::GetIO().WantTextInput &&
        !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId |
                                         ImGuiPopupFlags_AnyPopupLevel) &&
        !s.opening && !visible.empty()) {
        auto found =
            std::find_if(visible.begin(), visible.end(), [&](size_t i) {
                return selected && catalog.entries[i].id == selected->id;
            });
        size_t row =
            found == visible.end() ? 0 : size_t(found - visible.begin());
        bool move = false;
        if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) {
            row = found == visible.end() ?
                      0 :
                      std::min(row + 1, visible.size() - 1);
            move = true;
        }
        if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) {
            row = row ? row - 1 : 0;
            move = true;
        }
        if (ImGui::IsKeyPressed(ImGuiKey_PageDown)) {
            row = std::min(row + 10, visible.size() - 1);
            move = true;
        }
        if (ImGui::IsKeyPressed(ImGuiKey_PageUp)) {
            row = row > 10 ? row - 10 : 0;
            move = true;
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Home)) {
            row = 0;
            move = true;
        }
        if (ImGui::IsKeyPressed(ImGuiKey_End)) {
            row = visible.size() - 1;
            move = true;
        }
        if (move) {
            s.controller.Select(catalog.entries[visible[row]].id);
            scroll_row = int(row);
        }
    }
    const float width = ImGui::GetContentRegionAvail().x;
    const float left = std::clamp(width * .25f, 210.f, 310.f);
    ImGui::BeginChild("Assets", ImVec2(left, 0), true);
    ImGui::BeginDisabled(s.opening);
    s.filter.Draw("Find", left - 20);
    ImGui::Checkbox("Largest geometry first", &s.largest);
    if (!s.controller.NamedAssemblies().empty()) {
        ImGui::TextUnformatted("Named assemblies");
        const auto &named = s.controller.NamedAssemblies();
        for (size_t i = 0; i < named.size(); ++i) {
            ImGui::PushID(int(i));
            if (ImGui::Selectable(named[i]->label.c_str(),
                                  s.controller.Selected() == named[i]))
                s.controller.Recall(i);
            ImGui::PopID();
        }
        ImGui::Separator();
    }
    ImGui::Text("%zu / %zu entries", visible.size(), catalog.entries.size());
    ImGui::BeginChild("Part list", ImVec2(0, -110));
    ImGuiListClipper clipper;
    clipper.Begin(int(visible.size()), 82);
    size_t submitted_thumbnails = 0;
    if (scroll_row >= 0)
        clipper.IncludeItemByIndex(scroll_row);
    while (clipper.Step())
        for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
            const auto &entry = catalog.entries[visible[row]];
            ImGui::PushID(int(entry.id));
            auto owned = std::make_shared<const AssetAssembly>(entry);
            // ImGui consumes these IDs after Draw returns. Do not evict a
            // thumbnail referenced by an earlier row in this same UI frame.
            auto thumb = submitted_thumbnails < s.viewport.ThumbnailCapacity() ?
                             s.viewport.Thumbnail(owned) :
                             AssetViewportFrame{};
            ++submitted_thumbnails;
            if (thumb.texture)
                ImGui::Image((ImTextureID)(intptr_t)thumb.texture,
                             ImVec2(96, 72), ImVec2(0, 1), ImVec2(1, 0));
            else
                ImGui::Dummy(ImVec2(96, 72));
            if (ImGui::IsItemClicked())
                s.controller.Select(entry.id);
            ImGui::SameLine();
            ImGui::BeginGroup();
            bool include = s.checked.count(entry.id) != 0;
            if (ImGui::Checkbox("##include", &include)) {
                if (include)
                    s.checked.insert(entry.id);
                else
                    s.checked.erase(entry.id);
            }
            ImGui::SameLine();
            selected = s.controller.Selected();
            std::string title = entry.label + "##select";
            if (ImGui::Selectable(title.c_str(),
                                  selected && entry.id == selected->id, 0,
                                  ImVec2(0, 18)))
                s.controller.Select(entry.id);
            ImGui::Text("E%llu | %zu parts", (unsigned long long)entry.id,
                        entry.parts.size());
            if (!entry.parts.empty())
                ImGui::TextUnformatted(
                    AssetStatusLabel(entry.parts.front()->status));
            ImGui::TextDisabled(thumb.captured_parts ? "Shader output" :
                                                       "Raw inputs");
            if (ImGui::IsItemHovered()) {
                ImGui::BeginTooltip();
                ImGui::TextWrapped("%s", thumb.message.c_str());
                ImGui::EndTooltip();
            }
            ImGui::EndGroup();
            if (row == scroll_row)
                ImGui::SetScrollHereY();
            ImGui::PopID();
        }
    ImGui::EndChild();
    if (ImGui::Button("Suggest related parts")) {
        const auto anchor = s.controller.Selected();
        if (anchor) {
            const auto ids = SuggestRelatedAssetParts(catalog, anchor->id);
            s.checked = { ids.begin(), ids.end() };
            s.message =
                ids.empty() ?
                    "Captured placement is unavailable for this part" :
                    "Nearby parts and neighboring passes are suggestions. "
                    "Inspect and confirm checked membership.";
        }
    }
    ImGui::SameLine();
    ImGui::TextUnformatted("Confirm membership below");
    ImGui::InputText("Name", s.label, sizeof(s.label));
    if (ImGui::Button("Assemble checked parts")) {
        std::vector<uint64_t> parts;
        const auto anchor = s.controller.Selected();
        if (anchor && s.checked.count(anchor->id))
            parts.push_back(anchor->id);
        for (const auto &entry : catalog.entries)
            if (s.checked.count(entry.id))
                for (const auto &part : entry.parts)
                    parts.push_back(part->id);
        if (!s.controller.Assemble(parts, s.label))
            s.message = s.controller.Message();
        else {
            s.controller.Pin(true);
            if (!s.controller.RememberSelected())
                s.message = s.controller.Message();
        }
    }
    ImGui::Text("%zu checked", s.checked.size());
    ImGui::EndDisabled();
    ImGui::EndChild();
    ImGui::SameLine();
    const float inspector = s.inspector && width > 850 ? 270 : 0;
    ImGui::BeginChild("Model view",
                      ImVec2(std::max(200.f, width - left - inspector - 24), 0),
                      true);
    selected = s.controller.Selected();
    if (selected) {
        const bool projected =
            s.captured_stages &&
            (s.projected_output || !selected->captured_placement);
        // Browsing another occurrence must not inherit an offscreen pan.
        // Live following deliberately retains its camera across new poses.
        if (s.camera_selection.lock() != selected && !s.controller.Pinned())
            s.camera =
                projected ? AssetCamera{} : AssetCamera{ .55f, .45f, 1.f };
        s.camera_selection = selected;
        ImGui::Text("%s | frame %llu | %zu parts", selected->label.c_str(),
                    (unsigned long long)selected->frame,
                    selected->parts.size());
        if (ImGui::Button("Apply name")) {
            s.controller.Rename(s.label);
            if (!s.controller.RememberSelected())
                s.message = s.controller.Message();
        }
        ImGui::SameLine();
        if (ImGui::Button("Fit") ||
            (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
             !ImGui::GetIO().WantTextInput &&
             ImGui::IsKeyPressed(ImGuiKey_F))) {
            s.camera.zoom = 1;
            s.camera.pan_x = s.camera.pan_y = 0;
        }
        ImGui::SameLine();
        if (ImGui::Button("Front"))
            s.camera = {};
        ImGui::SameLine();
        if (ImGui::Button("Side"))
            s.camera = { 1.5708f, 0, 1 };
        ImGui::SameLine();
        if (ImGui::Button("Top"))
            s.camera = { 0, 1.5708f, 1 };
        ImGui::Checkbox("Wireframe", &s.wire);
        ImGui::Checkbox("Captured shaders and materials", &s.captured_stages);
        if (s.captured_stages &&
            ImGui::Checkbox("Frame shader output", &s.projected_output))
            s.camera = s.projected_output ? AssetCamera{} :
                                            AssetCamera{ .55f, .45f, 1.f };
        ImGui::SetNextItemWidth(160);
        ImGui::SliderFloat("Zoom", &s.camera.zoom, .1f, 5.f, "%.2fx");
        ImGui::TextDisabled(
            "Left drag: orbit | Right drag: pan | Wheel: zoom | F: fit");
        static const char *textures[] = {
            "Auto captured texture", "T0",  "T1", "T2", "T3",
            "Vertex color",          "Clay"
        };
        int choice = s.texture_slot == -1 ? 0 :
                     s.texture_slot == -2 ? 5 :
                     s.texture_slot == -3 ? 6 :
                                            s.texture_slot + 1;
        if (!s.captured_stages &&
            ImGui::Combo("Diagnostic material", &choice, textures, 7))
            s.texture_slot = choice == 0 ? -1 :
                             choice == 5 ? -2 :
                             choice == 6 ? -3 :
                                           choice - 1;
        ImGui::TextWrapped("%s", s.controller.Message().c_str());
        auto size = ImGui::GetContentRegionAvail();
        size.y = std::max(120.f, size.y - 110);
        auto frame = s.viewport.Render(
            selected, s.camera, uint32_t(std::clamp(size.x, 1.f, 2048.f)),
            uint32_t(std::clamp(size.y, 1.f, 2048.f)), s.texture_slot, s.wire,
            s.captured_stages, s.projected_output);
        if (selected->frame != s.pose_frame) {
            s.pose_updated_ns = now;
            if (!s.pose_started_ns || selected->frame < s.pose_frame) {
                s.pose_started_ns = now;
                s.pose_samples = 0;
                s.pose_hz = 0;
            } else
                ++s.pose_samples;
            s.pose_frame = selected->frame;
            if (now - s.pose_started_ns >= UINT64_C(1000000000)) {
                s.pose_hz =
                    double(s.pose_samples) * 1e9 / (now - s.pose_started_ns);
                s.pose_started_ns = now;
                s.pose_samples = 0;
            }
        }
        if (!s.live.Enabled() || now - s.pose_updated_ns > UINT64_C(1000000000))
            s.pose_hz = 0;
        ImGui::TextWrapped(
            "%zu/%zu captured stages | HUD %.1f FPS | pose updates %.1f/s",
            frame.captured_parts, selected->parts.size(),
            ImGui::GetIO().Framerate, s.pose_hz);
        uint64_t oldest = UINT64_MAX;
        for (const auto &part : selected->parts)
            if (part->occurrence && part->occurrence->host_timestamp_ns)
                oldest = std::min(oldest, part->occurrence->host_timestamp_ns);
        const uint64_t steady_now =
            uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(
                         std::chrono::steady_clock::now().time_since_epoch())
                         .count());
        if (s.live.Enabled() && oldest != UINT64_MAX && steady_now >= oldest)
            ImGui::Text("Oldest displayed draw %.2f s ago",
                        double(steady_now - oldest) / 1e9);
        else
            ImGui::TextUnformatted("Captured pose");
        if (frame.texture) {
            ImGui::Image((ImTextureID)(intptr_t)frame.texture, size,
                         ImVec2(0, 1), ImVec2(1, 0));
            if (ImGui::IsItemHovered()) {
                const auto &io = ImGui::GetIO();
                if (ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
                    s.camera.yaw += io.MouseDelta.x * .01f;
                    s.camera.pitch = std::clamp(
                        s.camera.pitch + io.MouseDelta.y * .01f, -1.55f, 1.55f);
                }
                if (ImGui::IsMouseDragging(ImGuiMouseButton_Right)) {
                    s.camera.pan_x += io.MouseDelta.x * 2 / size.x;
                    s.camera.pan_y -= io.MouseDelta.y * 2 / size.y;
                }
                s.camera.zoom = std::clamp(
                    s.camera.zoom * std::pow(1.15f, io.MouseWheel), .1f, 20.f);
            }
        }
        ImGui::TextWrapped("%s", frame.message.c_str());
    } else
        ImGui::TextWrapped(
            "Enable Live discovery, then choose a captured part. Freeze "
            "discovery and check body, wheel and glass parts to assemble your "
            "car. Captured draws are not engine asset names.");
    ImGui::EndChild();
    if (inspector) {
        ImGui::SameLine();
        ImGui::BeginChild("Selected parts", ImVec2(0, 0), true);
        if (selected)
            for (const auto &part : selected->parts) {
                ImGui::PushID(int(part->id));
                if (ImGui::CollapsingHeader(
                        ("E" + std::to_string(part->id)).c_str(),
                        ImGuiTreeNodeFlags_DefaultOpen)) {
                    ImGui::Text("%zu vertices | %zu triangles",
                                part->vertices.size(),
                                part->indices.size() / 3);
                    ImGui::TextWrapped("%s", part->reason.c_str());
                    if (part->occurrence) {
                        const auto &event = *part->occurrence;
                        ImGui::Text(
                            "Draw %u | emission %llu", event.summary.key.draw,
                            (unsigned long long)event.summary.key.submission);
                        for (size_t slot = 0; slot < 4; ++slot) {
                            const auto &tex = event.inputs.textures[slot];
                            ImGui::Text("T%zu: %s %ux%u | %zu images", slot,
                                        tex.metadata.bound ? "bound" :
                                                             "unbound",
                                        tex.metadata.width, tex.metadata.height,
                                        tex.images.size());
                            if (tex.metadata.bound) {
                                ImGui::PushID(int(slot));
                                if (ImGui::Button("View captured texture")) {
                                    s.inspected_part = part->id;
                                    s.image_slot = int(slot);
                                }
                                ImGui::PopID();
                            }
                        }
                        if (s.inspected_part == part->id) {
                            auto texture = s.viewport.TextureImage(
                                part, selected->context.backend, s.image_slot);
                            if (texture.texture) {
                                const float image_width = std::min(
                                    240.f, ImGui::GetContentRegionAvail().x);
                                ImGui::Image(
                                    (ImTextureID)(intptr_t)texture.texture,
                                    ImVec2(image_width,
                                           std::min(240.f, image_width *
                                                               texture.height /
                                                               texture.width)));
                            }
                            ImGui::TextWrapped("%s", texture.message.c_str());
                        }
                        if (ImGui::TreeNode("Stages and inputs")) {
                            ImGui::TextWrapped(
                                "Captured shaders and materials executes the "
                                "owned vertex, pixel and host geometry stages. "
                                "The inspection camera replaces window/depth "
                                "bookkeeping; scene and destination "
                                "dependencies "
                                "may be missing. Disable it for raw-input "
                                "diagnostics. Open the exact draw for "
                                "original-camera replay.");
                            for (size_t stage :
                                 { size_t(1), size_t(2), size_t(3) }) {
                                const auto &source =
                                    event.inputs.sources[stage];
                                const char *name =
                                    stage == 1 ? "Captured vertex GLSL" :
                                    stage == 2 ? "Captured pixel GLSL" :
                                                 "Host geometry/emulation GLSL";
                                if (ImGui::TreeNode(name)) {
                                    if (source) {
                                        ImGui::BeginChild(
                                            name, ImVec2(0, 180), true,
                                            ImGuiWindowFlags_HorizontalScrollbar);
                                        ImGui::TextUnformatted(
                                            reinterpret_cast<const char *>(
                                                source->bytes.data()),
                                            reinterpret_cast<const char *>(
                                                source->bytes.data() +
                                                source->bytes.size()));
                                        ImGui::EndChild();
                                    } else
                                        ImGui::TextUnformatted(
                                            "Source was not captured for this "
                                            "stage");
                                    ImGui::TreePop();
                                }
                            }
                            ImGui::Text(
                                "%zu uniforms | %zu raw streams/state blobs",
                                event.inputs.uniforms.size(),
                                event.inputs.blobs.size());
                            for (const auto &uniform : event.inputs.uniforms)
                                if (ImGui::TreeNode(uniform.name.c_str())) {
                                    ImGui::Text("Stage %u | type %u | %u "
                                                "components x %u",
                                                uniform.stage, uniform.type,
                                                uniform.components,
                                                uniform.count);
                                    if (uniform.data) {
                                        const auto &bytes = uniform.data->bytes;
                                        ImGui::BeginChild("Raw uniform words",
                                                          ImVec2(0, 120), true);
                                        ImGuiListClipper words;
                                        words.Begin(int(bytes.size() / 4));
                                        while (words.Step())
                                            for (int word = words.DisplayStart;
                                                 word < words.DisplayEnd;
                                                 ++word) {
                                                uint32_t bits =
                                                    uint32_t(bytes[word * 4]) |
                                                    uint32_t(
                                                        bytes[word * 4 + 1])
                                                        << 8 |
                                                    uint32_t(
                                                        bytes[word * 4 + 2])
                                                        << 16 |
                                                    uint32_t(
                                                        bytes[word * 4 + 3])
                                                        << 24;
                                                float value;
                                                std::memcpy(&value, &bits, 4);
                                                ImGui::Text(
                                                    "[%d] 0x%08X | float %.9g",
                                                    word, bits, value);
                                            }
                                        ImGui::EndChild();
                                    }
                                    ImGui::TreePop();
                                }
                            for (const auto &blob : event.inputs.blobs)
                                ImGui::TextWrapped(
                                    "%s: %u vertices, stride %u, format %u, "
                                    "%u components",
                                    blob.name.c_str(), blob.count, blob.stride,
                                    blob.format, blob.components);
                            ImGui::TreePop();
                        }
                        if (ImGui::Button("Inspect exact draw / GLSL")) {
                            s.message =
                                s.shader(part->occurrence, selected->context,
                                         s.controller.SelectedRecording());
                            if (s.message.empty())
                                s.message = "Exact owned draw opened in Shader "
                                            "Browser; saving a replacement "
                                            "does not enable it";
                        }
                    }
                    if (selected->parts.size() > 1 &&
                        ImGui::Button("Remove part")) {
                        std::vector<uint64_t> ids;
                        for (const auto &other : selected->parts)
                            if (other != part)
                                ids.push_back(other->id);
                        if (!s.controller.Assemble(ids, selected->label))
                            s.message =
                                "Removal requires the assembly's frame to "
                                "remain in the current catalog. Freeze "
                                "discovery before editing membership";
                    }
                }
                ImGui::PopID();
            }
        ImGui::EndChild();
    }
    ImGui::End();
}

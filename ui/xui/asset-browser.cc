// SPDX-License-Identifier: GPL-2.0-or-later
#include "asset-browser.hh"
#include "asset-browser-viewport.hh"
#include <imgui.h>
#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>
#include <set>

using namespace xemu::asset_browser;
struct AssetBrowserWindow::Impl {
    AssetController controller;
    AssetLiveCapture live;
    AssetViewport viewport;
    ContextSource context;
    ShaderSink shader;
    AssetCamera camera;
    ImGuiTextFilter filter;
    std::set<uint64_t> checked;
    uint64_t checked_frame = 0;
    char label[256] = "My car";
    bool was_open = false, inspector = true, wire = false, largest = true;
    int texture_slot = -1;
    std::string message;
    SDL_Window *owner_window = nullptr;
    SDL_GLContext owner_context = nullptr;
    Impl(capture::CaptureSession &session, ContextSource source,
         ShaderSink sink)
        : live(session), context(std::move(source)), shader(std::move(sink))
    {
    }
};
AssetBrowserWindow::AssetBrowserWindow(capture::CaptureSession &session,
                                       ContextSource source, ShaderSink sink)
    : impl_(std::make_unique<Impl>(session, std::move(source), std::move(sink)))
{
}
AssetBrowserWindow::~AssetBrowserWindow() = default;
bool AssetBrowserWindow::InspectCatalog(AssetCatalog catalog)
{
    impl_->live.Disable();
    auto generation = impl_->controller.Begin(catalog.context);
    const bool accepted =
        impl_->controller.Publish(std::move(catalog), generation);
    m_is_open = accepted;
    return accepted;
}
std::shared_ptr<const AssetAssembly> AssetBrowserWindow::Selected() const
{
    return impl_->controller.Selected();
}
void AssetBrowserWindow::Shutdown()
{
    impl_->live.Disable();
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
    auto context = s.context();
    s.live.Tick(context, now, s.controller);
    ImGui::SetNextWindowSize(ImVec2(1100, 700), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Asset Browser", &m_is_open)) {
        ImGui::End();
        return;
    }
    bool live = s.live.Enabled();
    if (ImGui::Checkbox("Live discovery", &live)) {
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
                settings.capture.event_budget = 32768;
                s.live.Enable(context, now, settings);
                s.message = s.live.Message();
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
    ImGui::TextWrapped("%s", s.live.Enabled() ? s.live.Message().c_str() :
                                                s.message.c_str());
    ImGui::TextWrapped(
        "Freeze discovery to choose parts; the game keeps running. Follow uses "
        "geometry evidence, not a verified player-car ID.");
    const auto &catalog = s.controller.Catalog();
    if (s.checked_frame != catalog.frame) {
        s.checked.clear();
        s.checked_frame = catalog.frame;
    }
    ImGui::Text("Frame %llu | %zu captured parts | %.1f MiB decoded | %s",
                (unsigned long long)catalog.frame, catalog.parts.size(),
                double(catalog.decoded_bytes) / (1 << 20),
                catalog.complete_frame ? "Complete captured frame" :
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
    // Live navigation updates the selected occurrence immediately and keeps
    // stable IDs.
    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
        !ImGui::GetIO().WantTextInput &&
        !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId |
                                         ImGuiPopupFlags_AnyPopupLevel) &&
        !visible.empty()) {
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
        if (move)
            s.controller.Select(catalog.entries[visible[row]].id);
    }
    const float width = ImGui::GetContentRegionAvail().x;
    const float left = std::clamp(width * .25f, 210.f, 310.f);
    ImGui::BeginChild("Assets", ImVec2(left, 0), true);
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
    while (clipper.Step())
        for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
            const auto &entry = catalog.entries[visible[row]];
            ImGui::PushID(int(entry.id));
            auto owned = std::make_shared<const AssetAssembly>(entry);
            auto thumb = s.viewport.Thumbnail(owned);
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
                                  selected && selected->id == entry.id, 0,
                                  ImVec2(0, 18)))
                s.controller.Select(entry.id);
            ImGui::Text("E%llu | %zu parts", (unsigned long long)entry.id,
                        entry.parts.size());
            if (!entry.parts.empty())
                ImGui::TextUnformatted(
                    AssetStatusLabel(entry.parts.front()->status));
            ImGui::EndGroup();
            ImGui::PopID();
        }
    ImGui::EndChild();
    ImGui::InputText("Name", s.label, sizeof(s.label));
    if (ImGui::Button("Assemble checked parts")) {
        std::vector<uint64_t> parts;
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
    ImGui::EndChild();
    ImGui::SameLine();
    const float inspector = s.inspector && width > 850 ? 270 : 0;
    ImGui::BeginChild("Model view",
                      ImVec2(std::max(200.f, width - left - inspector - 24), 0),
                      true);
    selected = s.controller.Selected();
    if (selected) {
        ImGui::Text("%s | frame %llu | %zu parts", selected->label.c_str(),
                    (unsigned long long)selected->frame,
                    selected->parts.size());
        if (ImGui::Button("Apply name")) {
            s.controller.Rename(s.label);
            if (!s.controller.RememberSelected())
                s.message = s.controller.Message();
        }
        ImGui::SameLine();
        if (ImGui::Button("Reset camera"))
            s.camera = {};
        ImGui::SameLine();
        ImGui::Checkbox("Wireframe", &s.wire);
        static const char *textures[] = {
            "Auto captured texture", "T0",  "T1", "T2", "T3",
            "Vertex color",          "Clay"
        };
        int choice = s.texture_slot == -1 ? 0 :
                     s.texture_slot == -2 ? 5 :
                     s.texture_slot == -3 ? 6 :
                                            s.texture_slot + 1;
        if (ImGui::Combo("Material", &choice, textures, 7))
            s.texture_slot = choice == 0 ? -1 :
                             choice == 5 ? -2 :
                             choice == 6 ? -3 :
                                           choice - 1;
        ImGui::TextWrapped("%s", s.controller.Message().c_str());
        auto size = ImGui::GetContentRegionAvail();
        size.y = std::max(120.f, size.y - 65);
        auto frame = s.viewport.Render(
            selected, s.camera, uint32_t(std::clamp(size.x, 1.f, 2048.f)),
            uint32_t(std::clamp(size.y, 1.f, 2048.f)), s.texture_slot, s.wire);
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
        ImGui::Text("%zu/%zu textured | viewport %.1f FPS | capture age %.2f s",
                    frame.textured_parts, frame.drawn_parts,
                    ImGui::GetIO().Framerate,
                    s.live.LastCaptureNs() ?
                        double(now - s.live.LastCaptureNs()) / 1e9 :
                        0);
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
                        }
                        if (ImGui::Button("Inspect exact draw / GLSL")) {
                            s.shader(part->occurrence, selected->context);
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

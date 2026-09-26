// SPDX-License-Identifier: GPL-2.0-or-later
#include "shader-browser.hh"

#include "common.hh"
#include "viewport-manager.hh"
#include "../xemu-settings.h"
#include "hw/xbox/nv2a/pgraph/shader-browser-flush.h"
#include "hw/xbox/nv2a/debug.h"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <iterator>
#include <string>

using namespace xemu::shader_browser;

ShaderBrowserWindow shader_browser_window;

namespace {

uint64_t PreviewNowNs()
{
    return static_cast<uint64_t>(g_get_monotonic_time()) * UINT64_C(1000);
}

Stage StageFromIndex(int index)
{
    static constexpr Stage stages[] = {
        Stage::Any, Stage::Vertex, Stage::Pixel,
        Stage::Geometry, Stage::FixedFunction, Stage::Unknown,
    };
    return index >= 0 && index < static_cast<int>(std::size(stages)) ?
               stages[index] : Stage::Any;
}

SourceFilter SourceFromIndex(int index)
{
    static constexpr SourceFilter sources[] = {
        SourceFilter::All,
        SourceFilter::ThisSession,
        SourceFilter::StoredDatabase,
    };
    return index >= 0 && index < static_cast<int>(std::size(sources)) ?
               sources[index] : SourceFilter::All;
}

SortKey SortFromIndex(int index)
{
    static constexpr SortKey keys[] = {
        SortKey::Draws,
        SortKey::LastUsed,
        SortKey::CompileTime,
        SortKey::ShaderId,
    };
    return index >= 0 && index < static_cast<int>(std::size(keys)) ?
               keys[index] : SortKey::Draws;
}

std::string UnsignedText(uint64_t value)
{
    char buffer[64];
    std::snprintf(buffer, sizeof(buffer), "%llu",
                  static_cast<unsigned long long>(value));
    return buffer;
}

std::string BytesText(uint64_t bytes)
{
    char buffer[64];
    if (bytes >= 1024ULL * 1024ULL * 1024ULL) {
        std::snprintf(buffer, sizeof(buffer), "%.2f GiB",
                      bytes / (1024.0 * 1024.0 * 1024.0));
    } else if (bytes >= 1024ULL * 1024ULL) {
        std::snprintf(buffer, sizeof(buffer), "%.2f MiB",
                      bytes / (1024.0 * 1024.0));
    } else if (bytes >= 1024ULL) {
        std::snprintf(buffer, sizeof(buffer), "%.2f KiB", bytes / 1024.0);
    } else {
        std::snprintf(buffer, sizeof(buffer), "%llu bytes",
                      static_cast<unsigned long long>(bytes));
    }
    return buffer;
}

void DrawKeyValue(const char *key, const std::string &value)
{
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    ImGui::TextUnformatted(key);
    ImGui::TableSetColumnIndex(1);
    ImGui::TextWrapped("%s", value.c_str());
}

std::string ScopeSummary(const Entry &entry)
{
    if (entry.scopes.empty()) {
        return "No title/build scope recorded";
    }
    if (entry.scopes.size() == 1) {
        return entry.scopes[0].title_id ?
                   FormatTitleId(entry.scopes[0].title_id) : "Unknown title";
    }
    return std::to_string(entry.scopes.size()) + " title/build scopes";
}

uint32_t DisplayTitleId(const Entry &entry, uint32_t current_title_id)
{
    for (const ShaderScope &scope : entry.scopes) {
        if (scope.title_id && scope.title_id == current_title_id) {
            return current_title_id;
        }
    }
    return PrimaryTitleId(entry);
}

std::string FullDisplayId(const Entry &entry, uint32_t current_title_id)
{
    uint32_t title_id = DisplayTitleId(entry, current_title_id);
    return title_id ? GameShaderId(title_id, entry.key.hash) :
                      PortableShaderHash(entry.key.hash);
}

std::string ShortDisplayId(const Entry &entry, uint32_t current_title_id)
{
    uint32_t title_id = DisplayTitleId(entry, current_title_id);
    if (title_id) {
        return ShortGameShaderId(title_id, entry.key.hash);
    }
    std::string encoded = ShaderHashBase32(entry.key.hash);
    return "XSH" + std::to_string(entry.key.hash.version) + "-" +
           encoded.substr(0, 4) + "-" + encoded.substr(4, 4);
}

bool OpenDirectory(const std::string &path, std::string *error)
{
    if (path.empty()) {
        if (error) *error = "Directory path is unavailable";
        return false;
    }
    GError *glib_error = nullptr;
    char *uri = g_filename_to_uri(path.c_str(), nullptr, &glib_error);
    if (!uri) {
        if (error) {
            *error = glib_error ? glib_error->message :
                                  "Unable to create directory URI";
        }
        g_clear_error(&glib_error);
        return false;
    }
    bool ok = SDL_OpenURL(uri);
    g_free(uri);
    if (!ok && error) {
        *error = std::string("Unable to open directory: ") + SDL_GetError();
    }
    return ok;
}

void DrawDurationRows(const Entry &entry)
{
    DrawKeyValue("Compile CPU", FormatDurationNs(entry.compile_cpu));
    DrawKeyValue("Pipeline CPU", FormatDurationNs(entry.pipeline_cpu));
    DrawKeyValue("Prepare CPU", FormatDurationNs(entry.prepare_cpu));
    DrawKeyValue("Foreground stall",
                 FormatDurationNs(entry.foreground_stall_cpu));
    DrawKeyValue("GPU execution", FormatDurationNs(entry.gpu_execution));
}

} // namespace

ShaderBrowserWindow::ShaderBrowserWindow()
    : m_is_open(false),
      m_next_poll_ms(0),
      m_has_selection(false),
      m_filter_dirty(true),
      m_window_was_open(false),
      m_collection_requested(true),
      m_current_title_only(true),
      m_stage_filter(0),
      m_source_filter(0),
      m_sort_key(0),
      m_sort_descending(true)
{
    m_preview_colors = {{{1.0f, 0.25f, 0.25f, 1.0f},
                         {0.25f, 1.0f, 0.25f, 1.0f},
                         {0.25f, 0.25f, 1.0f, 1.0f},
                         {1.0f, 1.0f, 1.0f, 1.0f}}};
}

void ShaderBrowserWindow::Draw()
{
    Provider &provider = GetProvider();
    if (!m_is_open) {
        GetPreviewService().SetVisible(false, PreviewNowNs());
        if (m_window_was_open) {
            provider.SetLiveCollectionEnabled(false);
            GetDetailStore().Clear();
            m_window_was_open = false;
        }
        return;
    }

    if (!m_window_was_open) {
        m_window_was_open = true;
        provider.SetLiveCollectionEnabled(m_collection_requested);
        m_has_selection = false;
        GetDetailStore().Clear();
        m_next_poll_ms = 0;
    }
    RefreshSnapshot();
    GetDetailStore().CopySnapshotIfChanged(m_detail_snapshot.generation,
                                            &m_detail_snapshot);
    if (m_has_selection && m_detail_snapshot.state == DetailState::Idle) {
        GetDetailStore().Request(m_selected_key, DetailBackend::Active);
        pgraph_shader_browser_request_details();
    }

    float scale = g_viewport_mgr.m_scale;
    ImVec2 work = ImGui::GetMainViewport()->WorkSize;
    ImVec2 initial(std::min(960.0f * scale, work.x * 0.92f),
                   std::min(640.0f * scale, work.y * 0.92f));
    ImVec2 minimum(std::min(720.0f * scale, work.x * 0.92f),
                   std::min(460.0f * scale, work.y * 0.92f));
    ImGui::SetNextWindowSize(initial, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSizeConstraints(minimum, work);

    ImGuiWindowFlags window_flags = ImGuiWindowFlags_NoCollapse;
    if (xemu_hud_shader_browser_external()) {
        const ImGuiViewport *viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(viewport->WorkPos, ImGuiCond_Always);
        ImGui::SetNextWindowSize(viewport->WorkSize, ImGuiCond_Always);
        window_flags |= ImGuiWindowFlags_NoTitleBar |
                        ImGuiWindowFlags_NoResize |
                        ImGuiWindowFlags_NoMove;
    }

    if (!ImGui::Begin("Shader Browser", &m_is_open, window_flags)) {
        ImGui::End();
        GetPreviewService().SetVisible(false, PreviewNowNs());
        if (!m_is_open) {
            provider.SetLiveCollectionEnabled(false);
            GetDetailStore().Clear();
            m_window_was_open = false;
        }
        return;
    }

    ImVec2 available = ImGui::GetContentRegionAvail();
    float left_width = std::clamp(available.x * 0.48f,
                                  320.0f * scale,
                                  std::max(320.0f * scale,
                                           available.x - 330.0f * scale));
    if (ImGui::BeginChild("##shader-selector", ImVec2(left_width, 0), true)) {
        DrawSelector();
    }
    ImGui::EndChild();

    ImGui::SameLine();
    if (ImGui::BeginChild("##shader-content", ImVec2(0, 0), true)) {
        DrawRightPane();
    }
    ImGui::EndChild();
    ImGui::End();

    if (!m_is_open) {
        GetPreviewService().SetVisible(false, PreviewNowNs());
        provider.SetLiveCollectionEnabled(false);
        GetDetailStore().Clear();
        m_window_was_open = false;
    }
}

void ShaderBrowserWindow::RefreshSnapshot()
{
    uint64_t now_ms = SDL_GetTicks();
    if (now_ms < m_next_poll_ms) {
        return;
    }
    m_next_poll_ms = now_ms + 250;

    Snapshot next;
    if (!GetProvider().CopySnapshot(&next)) {
        m_action_message = "Unable to copy shader snapshot";
        return;
    }
    m_snapshot = std::move(next);
    m_filter_dirty = true;

    if (m_has_selection &&
        FindEntryByKey(m_snapshot.entries, m_selected_key) < 0) {
        m_has_selection = false;
        m_has_recipe = false;
        m_selected_recipe = {};
        m_recipe_inspection = {};
        GetDetailStore().Clear();
    }
    if (!m_has_selection && !m_snapshot.entries.empty()) {
        SelectEntry(m_snapshot.entries.front());
    }
}

void ShaderBrowserWindow::RebuildVisibleOrder()
{
    Filter filter{};
    filter.query = m_search;
    filter.stage = StageFromIndex(m_stage_filter);
    filter.source = SourceFromIndex(m_source_filter);
    filter.sort_key = SortFromIndex(m_sort_key);
    filter.descending = m_sort_descending;
    m_visible_order = BuildVisibleOrder(m_snapshot.entries, filter);
    if (m_current_title_only && m_snapshot.current_title_id) {
        m_visible_order.erase(std::remove_if(
            m_visible_order.begin(), m_visible_order.end(),
            [this](size_t index) {
                const Entry &entry = m_snapshot.entries[index];
                return std::none_of(
                    entry.scopes.begin(), entry.scopes.end(),
                    [this](const ShaderScope &scope) {
                        return scope.title_id != 0 &&
                               scope.title_id == m_snapshot.current_title_id;
                    });
            }), m_visible_order.end());
    }
    if (m_has_selection && std::none_of(
            m_visible_order.begin(), m_visible_order.end(),
            [this](size_t index) {
                return m_snapshot.entries[index].key == m_selected_key;
            })) {
        m_has_selection = false;
    }
    if (!m_has_selection && !m_visible_order.empty()) {
        SelectEntry(m_snapshot.entries[m_visible_order.front()]);
    }
    m_filter_dirty = false;
}

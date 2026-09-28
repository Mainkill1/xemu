#include "shader-browser-source-summary.hh"

namespace {
void DrawGlslSourceSummary(const HostSource &source, bool show_interfaces)
{
    if (source.kind != HostSourceKind::Glsl) {
        ImGui::TextDisabled("Lexical summary unavailable for %s.",
                            HostSourceKindLabel(source.kind));
        return;
    }
    static GlslSourceSummaryCache cache;
    const auto summary = cache.Get(source.text);
    if (summary->status == GlslSourceSummaryStatus::TooLarge) {
        ImGui::TextDisabled("Source summary unavailable: exceeds 4 MiB.");
        return;
    }
    ImGui::Text("%s | %zu physical lines", BytesText(summary->bytes).c_str(),
                summary->lines);
    ImGui::Text("Texture fetch call sites: %u | Derivative call sites: %u",
                summary->texture_calls, summary->derivative_calls);
    ImGui::Text("Conditional statement heads: %u | Loop heads: %u",
                summary->conditional_statements, summary->loop_heads);
    ImGui::Text(
        "Discard statements: %u | Vertex emission / primitive end: %u / %u",
        summary->discard_statements, summary->emit_vertex_calls,
        summary->end_primitive_calls);
    ImGui::Text("Global in / out / uniform declarations: %u%s",
                summary->interface_declarations,
                summary->interfaces_truncated ? " (first 256 retained)" : "");
    if (summary->status == GlslSourceSummaryStatus::Malformed)
        ImGui::TextDisabled("Partial summary: malformed lexical structure.");
    DrawMutedWrapped(
        "Lexical GLSL structure; comments, strings and preprocessor directives "
        "are excluded. Macros are not expanded and conditional compilation is "
        "not evaluated. Counts do not establish dynamic branches, executed "
        "instructions, compiler instruction counts or register pressure.");
    if (show_interfaces && !summary->interfaces.empty() &&
        ImGui::TreeNode("##glsl-global-interface",
                        "Global interface declarations")) {
        for (const auto &declaration : summary->interfaces)
            ImGui::TextWrapped("%s%s", declaration.text.c_str(),
                               declaration.truncated ? " ... (truncated)" : "");
        ImGui::TreePop();
    }
}

void DrawSelectedPreviewTiming(const ShaderKey &key)
{
    PreviewStatus status;
    GetPreviewService().CopyStatus(&status);
    if (!status.has_displayed_source ||
        status.displayed_compile.selection.shader != key) {
        ImGui::TextDisabled("No displayed preview result for this shader.");
        return;
    }
    const auto &timing = status.draw_timing;
    if (!ValidatePreviewDrawTiming(timing, status.displayed_result)) {
        ImGui::TextDisabled(
            "No timing attached to this exact displayed result.");
        return;
    }
    ImGui::Text("Selected preview whole-pipeline GPU interval: %s",
                PreviewDrawTimingStatusLabel(timing.status));
    if (timing.status == PreviewDrawTimingStatus::Measured)
        ImGui::Text("%llu ns | %u draw commands",
                    static_cast<unsigned long long>(timing.nanoseconds),
                    timing.actual_draw_commands);
    ImGui::TextDisabled("Provenance: %s",
                        PreviewDrawTimingProvenanceLabel(timing.provenance));
    if (!timing.message.empty())
        ImGui::TextWrapped("%s", timing.message.c_str());
    DrawMutedWrapped(
        "Instrumented result interval for the complete active draw pipeline. "
        "It does not isolate this shader stage or measure an ordinary game "
        "draw.");
}
} // namespace

void ShaderBrowserWindow::DrawOverview()
{
    const Entry *entry = SelectedEntry();
    if (!entry) return;

    const std::string full_id =
        FullDisplayId(*entry, m_snapshot.current_title_id);
    const std::string canonical = PortableShaderHash(entry->key.hash);
    const float scale = g_viewport_mgr.m_scale;
    if (ImGui::BeginTable("##overview-summary", 3,
                          ImGuiTableFlags_BordersInnerV |
                          ImGuiTableFlags_SizingStretchSame)) {
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextDisabled("OBSERVED DRAWS");
        ImGui::Text("%s", entry->observed_in_session ?
            UnsignedText(entry->draw_count).c_str() : "Not observed");
        ImGui::TableNextColumn();
        ImGui::TextDisabled("LAST USED");
        ImGui::Text("%s", FormatLastUsed(entry->last_frame,
            m_snapshot.current_frame, entry->observed_in_session).c_str());
        ImGui::TableNextColumn();
        ImGui::TextDisabled("CURRENT ROUTE");
        ImGui::Text("%s", RouteLabel(entry->route));
        ImGui::EndTable();
    }
    ImGui::SeparatorText("Session measured draw cost");
    ImGui::TextDisabled(
        "Ordinary, uninstrumented game draw cost: unavailable.");
    bool has_gpu_cost = false;
    for (const auto &variant : m_snapshot.binding_variants) {
        if (std::find(variant.members.begin(), variant.members.end(),
                      entry->key) == variant.members.end())
            continue;
        const auto &gpu = variant.timing[XEMU_SHADER_BROWSER_PERF_DRAW_GPU];
        if (!gpu.samples)
            continue;
        has_gpu_cost = true;
        ImGui::Text("%s / %s: %s",
                    variant.backend == XEMU_SHADER_BROWSER_BACKEND_GL ?
                        "OpenGL" :
                        "Vulkan",
                    RouteLabel(variant.route), FormatDurationNs(gpu).c_str());
    }
    if (!has_gpu_cost)
        ImGui::TextDisabled("No GPU interval samples yet. Enable measurement "
                            "and run the game.");
    DrawMutedWrapped("Complete draw intervals involving this stage. Shared "
                     "stage values are not additive. "
                     "Capture readbacks are excluded; profiling is "
                     "instrumented, not an ordinary gameplay baseline.");
    ImGui::Spacing();
    const bool overview_columns =
        ImGui::GetContentRegionAvail().x >= 900.0f * scale;
    if (overview_columns) ImGui::Columns(2, "##overview-columns", false);
    ImGui::SeparatorText("Identity & provenance");
    if (ImGui::BeginTable("##shader-identity", 2,
                          ImGuiTableFlags_BordersInnerV |
                          ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("##name", ImGuiTableColumnFlags_WidthFixed,
                                150.0f * g_viewport_mgr.m_scale);
        ImGui::TableSetupColumn("##value",
                                ImGuiTableColumnFlags_WidthStretch);
        DrawKeyValue("Game shader ID", full_id);
        DrawKeyValue("Canonical hash", canonical);
        DrawKeyValue("Stage", StageLabel(entry->key.stage));
        DrawKeyValue("Title/build scope", ScopeSummary(*entry));
        DrawKeyValue("Canonical recipe",
                     entry->recipe_format_version ?
                         "Format v" +
                             std::to_string(entry->recipe_format_version) +
                             " | " + BytesText(entry->recipe_size) :
                         "Identity only");
        DrawKeyValue("Catalog status",
                     m_snapshot.database_write_failed ?
                         "Unconfirmed after database writer error" :
                         entry->stored_in_database ?
                             "Catalogued (write-behind)" :
                             "Not catalogued");
        const bool detail_current =
            m_detail_snapshot.request.key == entry->key &&
            (m_detail_snapshot.state == DetailState::Complete ||
             m_detail_snapshot.state == DetailState::Partial);
        DrawKeyValue("Generated host source",
                     detail_current && !m_detail_snapshot.sources.empty() ?
                         (m_detail_snapshot.sources.front().
                              exact_runtime_source ?
                              "Resident renderer source" :
                              "Reconstructed source; build association "
                              "unverified") :
                         "Not available in selected details");
        ImGui::EndTable();
    }

    if (overview_columns) ImGui::NextColumn();
    ImGui::SeparatorText("Observation window");
    if (ImGui::BeginTable("##shader-observation", 2,
                          ImGuiTableFlags_BordersInnerV |
                          ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("##name", ImGuiTableColumnFlags_WidthFixed,
                                150.0f * g_viewport_mgr.m_scale);
        ImGui::TableSetupColumn("##value",
                                ImGuiTableColumnFlags_WidthStretch);
        DrawKeyValue("Session draws",
                     entry->observed_in_session ?
                         UnsignedText(entry->draw_count) :
                         "Not observed this session");
        DrawKeyValue("First guest frame",
                     entry->observed_in_session ?
                         UnsignedText(entry->first_frame) :
                         "Not observed");
        DrawKeyValue("Last used",
                     FormatLastUsed(entry->last_frame,
                                    m_snapshot.current_frame,
                                    entry->observed_in_session));
        DrawKeyValue("Current guest frame",
                     UnsignedText(m_snapshot.current_frame));
        DrawKeyValue("Current route", RouteLabel(entry->route));
        DrawDurationRows(*entry, !m_snapshot.timing_session_id.empty());
        DrawKeyValue("Readiness", ReadinessLabel(entry->readiness));
        ImGui::EndTable();
    }
    if (overview_columns) ImGui::Columns(1);
    ImGui::TextDisabled(
        "Session observations are separate from preview rendering.");

    ImGui::TextDisabled(
        "Timing source: %s",
        m_snapshot.timing_session_id.empty() ? "Live" : "Recorded session");
    ImGui::SeparatorText("Stage and preview");
    if (entry->key.stage == Stage::Pixel) {
        DrawMutedWrapped(
            "Pixel shaders calculate fragment color from interpolated vertex "
            "outputs, textures and constants. The scene runs this stage with "
            "captured geometry or synthetic inputs; missing game inputs make "
            "its appearance approximate.");
    } else if (entry->key.stage == Stage::Vertex ||
               entry->key.stage == Stage::FixedFunction) {
        DrawMutedWrapped(
            "The vertex stage transforms game vertices and produces colors, "
            "texture coordinates and fog inputs for the pixel stage. Viewing "
            "its result requires the matching vertex layout, transform "
            "constants and linked pixel shader. A complete captured pipeline "
            "runs its bound stages and original camera, so the final output "
            "includes the vertex stage's effect. Source & variants contains "
            "the generated GLSL and runtime layout.");
    } else if (entry->key.stage == Stage::Geometry) {
        DrawMutedWrapped(
            "Xbox NV2A has no programmable geometry shader. This host stage is "
            "generated by xemu to expand Xbox primitives for OpenGL/Vulkan. "
            "A complete captured pipeline runs the expansion stage with the "
            "bound vertex and pixel stages and original camera. The final "
            "output includes its effect. Source & variants shows expansion "
            "GLSL "
            "and runtime pipeline information.");
    }

    ImGui::SeparatorText("Generated GLSL structure");
    if (m_detail_snapshot.request.key == entry->key &&
        !m_detail_snapshot.sources.empty()) {
        const auto &source = m_detail_snapshot.sources[std::min(
            m_selected_source, m_detail_snapshot.sources.size() - 1)];
        ImGui::TextDisabled("%s | %s", source.label.c_str(),
                            HostSourceStageLabel(source.stage));
        DrawGlslSourceSummary(source, false);
    } else {
        ImGui::TextDisabled("No selected generated GLSL source available.");
    }
    if (entry->key.stage == Stage::Vertex && m_has_recipe &&
        m_selected_recipe.key == entry->key)
        ImGui::Text("Canonical guest vertex program instructions: %u",
                    m_recipe_inspection.instruction_count);
    ImGui::SeparatorText("Selected preview timing");
    DrawSelectedPreviewTiming(entry->key);

    if (m_snapshot.timing_session_id.empty()) {
        bool measure =
            g_config.shader_browser.profiling.monitoring_level == 2 &&
            g_config.shader_browser.profiling.gpu_timing;
        if (ImGui::Checkbox("Measure GPU draw cost", &measure)) {
            g_config.shader_browser.profiling.monitoring_level =
                measure ? 2 : 1;
            g_config.shader_browser.profiling.cpu_timing = measure;
            g_config.shader_browser.profiling.gpu_timing = measure;
            ShaderBrowserApplyProfilingSettings();
            xemu_settings_save();
        }
        DrawMutedWrapped(
            "Run or advance the game to collect samples. GPU cost covers the "
            "matching complete draw pipeline, including its vertex and pixel "
            "stages; it is not an isolated stage measurement. Sampling is "
            "bounded and results are read asynchronously.");
    }

    ImGui::SeparatorText("Stage timing by capture context");
    bool has_stage_profile = false;
    for (const StageProfile &profile : m_snapshot.stage_profiles) {
        if (!(profile.key == entry->key))
            continue;
        has_stage_profile = true;
        ImGui::PushID(&profile);
        ImGui::Text("%s / %s",
                    profile.backend == XEMU_SHADER_BROWSER_BACKEND_GL ?
                        "OpenGL" :
                        "Vulkan",
                    RouteLabel(profile.route));
        ImGui::TextDisabled(
            "Provenance: %s%s%s%s",
            profile.flags & XEMU_SHADER_BROWSER_SAMPLE_FOREGROUND ?
                "foreground " :
                "",
            profile.flags & XEMU_SHADER_BROWSER_SAMPLE_BACKGROUND ?
                "background " :
                "",
            profile.flags & XEMU_SHADER_BROWSER_SAMPLE_CACHED ? "cached " : "",
            profile.flags & XEMU_SHADER_BROWSER_SAMPLE_SAMPLED ? "sampled" :
                                                                 "");
        static const char *names[] = {
            "",
            "Source generation CPU",
            "Stage compile CPU",
            "Module CPU",
        };
        for (size_t metric = 1; metric < profile.timing.size(); ++metric) {
            if (!profile.timing[metric].samples)
                continue;
            ImGui::Text("%s: %s", names[metric],
                        FormatDurationNs(profile.timing[metric]).c_str());
        }
        ImGui::PopID();
    }
    if (!has_stage_profile) {
        ImGui::TextDisabled("No stage timing sample in this session.");
    }

    ImGui::SeparatorText("Shared host variants");
    ImGui::TextDisabled(
        "Shared values include all member shaders; do not sum across stages.");
    ImGui::TextDisabled("Foreground stall classifies link/pipeline work and "
                        "overlaps that cost.");
    bool has_binding = false;
    for (const BindingVariant &variant : m_snapshot.binding_variants) {
        if (std::find(variant.members.begin(), variant.members.end(),
                      entry->key) == variant.members.end())
            continue;
        has_binding = true;
        ImGui::PushID(&variant);
        ImGui::Text(
            "%s / %s / %s variant %llu",
            variant.backend == XEMU_SHADER_BROWSER_BACKEND_GL ? "OpenGL" :
                                                                "Vulkan",
            RouteLabel(variant.route),
            variant.variant_kind == XEMU_SHADER_BROWSER_VARIANT_PIPELINE ?
                "pipeline" :
                "binding",
            static_cast<unsigned long long>(variant.variant_id));
        std::string members;
        for (const ShaderKey &member : variant.members) {
            if (!members.empty())
                members += ", ";
            members += StageLabel(member.stage);
        }
        ImGui::TextDisabled("Members: %s", members.c_str());
        ImGui::TextDisabled(
            "Provenance: %s%s%s%s",
            variant.flags & XEMU_SHADER_BROWSER_SAMPLE_FOREGROUND ?
                "foreground " :
                "",
            variant.flags & XEMU_SHADER_BROWSER_SAMPLE_BACKGROUND ?
                "background " :
                "",
            variant.flags & XEMU_SHADER_BROWSER_SAMPLE_CACHED ? "cached " : "",
            variant.flags & XEMU_SHADER_BROWSER_SAMPLE_SAMPLED ? "sampled" :
                                                                 "");
        static const char *names[] = {
            "",
            "",
            "",
            "",
            "Link / pipeline CPU",
            "Foreground stall",
            "Draw flush CPU",
            "Sampled GPU draw interval",
            "Binding preparation CPU",
            "Background queue delay",
        };
        for (size_t metric = 4; metric < variant.timing.size(); ++metric) {
            const DurationStats &timing = variant.timing[metric];
            if (timing.samples) {
                ImGui::Text("%s: %s", names[metric],
                            FormatDurationNs(timing).c_str());
                continue;
            }
            const bool recorded = !m_snapshot.timing_session_id.empty();
            const bool gpu = metric == XEMU_SHADER_BROWSER_PERF_DRAW_GPU;
            const bool enabled =
                recorded ||
                (g_config.shader_browser.profiling.monitoring_level == 2 &&
                 (gpu ? g_config.shader_browser.profiling.gpu_timing :
                        g_config.shader_browser.profiling.cpu_timing));
            const bool creation =
                metric != XEMU_SHADER_BROWSER_PERF_DRAW_SUBMIT_CPU && !gpu;
            const uint32_t pending =
                variant.backend == XEMU_SHADER_BROWSER_BACKEND_GL ?
                    m_snapshot.pending_gl_gpu_queries :
                    m_snapshot.pending_vk_gpu_queries;
            const bool supported =
                variant.backend == XEMU_SHADER_BROWSER_BACKEND_GL ?
                    m_snapshot.gl_gpu_supported :
                    m_snapshot.vk_gpu_supported;
            const char *reason =
                !enabled          ? "Profiling disabled" :
                recorded          ? "No recorded sample" :
                gpu && !supported ? "Unsupported by backend/device" :
                gpu && pending    ? "GPU result pending" :
                creation          ? "Created before profiling or cache hit" :
                                    "No eligible sampled draw yet";
            ImGui::TextDisabled("%s: %s", names[metric], reason);
        }
        ImGui::TextDisabled(
            "Sampled CPU/GPU intervals: %llu / %llu",
            static_cast<unsigned long long>(variant.sampled_cpu_draws),
            static_cast<unsigned long long>(variant.sampled_gpu_draws));
        ImGui::PopID();
    }
    if (!has_binding) {
        ImGui::TextDisabled("No shared host variant sample in this session.");
        if (m_snapshot.dropped_performance_samples) {
            ImGui::TextDisabled(
                "Some profiling samples were dropped; reduce the sample rate.");
        }
    }

    if (!entry->scopes.empty() &&
        ImGui::CollapsingHeader("Known title/build associations")) {
        for (const ShaderScope &scope : entry->scopes) {
            const std::string title = scope.title_id ?
                FormatTitleId(scope.title_id) : "Unknown";
            ImGui::BulletText(
                "Title %s | executable fingerprint v%u | %02X%02X%02X%02X...",
                title.c_str(), scope.executable_fingerprint_version,
                scope.executable_fingerprint[0],
                scope.executable_fingerprint[1],
                scope.executable_fingerprint[2],
                scope.executable_fingerprint[3]);
        }
    }
}

void ShaderBrowserWindow::DrawUsageTiming()
{
    const Entry *entry = SelectedEntry();
    if (!entry) return;
    ImGui::SeparatorText("Observed logical draws");
    ImGui::Text("This session: %s",
                entry->observed_in_session ?
                    UnsignedText(entry->draw_count).c_str() :
                    "Not observed");
    ImGui::TextDisabled(
        "Current route: %s. Route counters overlap and do not partition "
        "the draw total.",
        RouteLabel(entry->route));
    if (entry->observed_in_session) {
        ImGui::Text("Uber-path counter: %llu",
                    static_cast<unsigned long long>(entry->uber_draws));
        ImGui::Text("Specialized-path counter: %llu",
                    static_cast<unsigned long long>(
                        entry->specialized_draws));
        ImGui::TextDisabled(
            "The specialized-path counter also includes some skipped and "
            "replacement observations. Separate skip/replacement totals "
            "are not collected.");
    }
    ImGui::Text("First / last guest frame: %s / %s",
                entry->observed_in_session ?
                    UnsignedText(entry->first_frame).c_str() :
                    "Not observed",
                entry->observed_in_session ?
                    UnsignedText(entry->last_frame).c_str() :
                    "Not observed");

    ImGui::SeparatorText("Host preparation timings");
    if (ImGui::BeginTable("##shader-cpu-timing", 5,
                          ImGuiTableFlags_Borders |
                          ImGuiTableFlags_RowBg |
                          ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("Operation", ImGuiTableColumnFlags_WidthFixed,
                                175.0f * g_viewport_mgr.m_scale);
        ImGui::TableSetupColumn("Samples", ImGuiTableColumnFlags_WidthFixed,
                                75.0f * g_viewport_mgr.m_scale);
        ImGui::TableSetupColumn("Total ns", ImGuiTableColumnFlags_WidthFixed,
                                95.0f * g_viewport_mgr.m_scale);
        ImGui::TableSetupColumn("Mean ns", ImGuiTableColumnFlags_WidthFixed,
                                85.0f * g_viewport_mgr.m_scale);
        ImGui::TableSetupColumn("Min / max ns");
        ImGui::TableHeadersRow();
        const struct {
            const char *name;
            const DurationStats *stats;
        } rows[] = {
            {"Compile CPU", &entry->compile_cpu},
            {"Pipeline creation CPU", &entry->pipeline_cpu},
            {"Prepare CPU", &entry->prepare_cpu},
            {"Foreground stall CPU", &entry->foreground_stall_cpu},
        };
        for (const auto &row : rows) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(row.name);
            ImGui::TableNextColumn();
            if (row.stats->samples)
                ImGui::Text("%llu", static_cast<unsigned long long>(
                    row.stats->samples));
            else ImGui::TextDisabled("Not sampled");
            ImGui::TableNextColumn();
            if (row.stats->samples)
                ImGui::Text("%llu", static_cast<unsigned long long>(
                    row.stats->total_ns));
            else ImGui::TextDisabled("-");
            ImGui::TableNextColumn();
            if (row.stats->samples)
                ImGui::Text("%.0f", row.stats->AverageNs());
            else ImGui::TextDisabled("-");
            ImGui::TableNextColumn();
            if (row.stats->samples)
                ImGui::Text("%llu / %llu",
                    static_cast<unsigned long long>(row.stats->min_ns),
                    static_cast<unsigned long long>(row.stats->max_ns));
            else ImGui::TextDisabled("-");
        }
        ImGui::EndTable();
    }
    ImGui::TextDisabled(
        "Completed CPU samples only. These are not GPU execution or "
        "gameplay frame-time measurements.");
    ImGui::SeparatorText("Execution measurements");
    DrawSelectedPreviewTiming(entry->key);
    ImGui::TextDisabled(
        "Ordinary, uninstrumented game draw cost: unavailable.");
    ImGui::TextDisabled("Stage-exclusive GPU duration and percentiles, "
                        "invocations, and visible pixels: "
                        "not collected.");
}

void ShaderBrowserWindow::DrawOverrides()
{
    const Entry *entry = SelectedEntry();
    if (!entry) return;
    ImGui::TextDisabled(
        "Rules affect matching game draws only after explicit Apply. "
        "Hiding a catalog row and editing a preview draft do not apply a rule.");
    ImGui::TextWrapped("Scope: %s | %s",
                       FullDisplayId(*entry,
                                     m_snapshot.current_title_id).c_str(),
                       StageLabel(entry->key.stage));
    GetShaderOverrideUi().DrawPanel(
        *entry, m_snapshot.entries, m_snapshot.current_title_id,
        &m_action_message);
    ImGui::SeparatorText("Rule status");
    if (!m_action_message.empty())
        ImGui::TextWrapped("%s", m_action_message.c_str());
    ImGui::TextDisabled(
        "Requested and effective renderer actions are reported separately "
        "above. Rules may match every use of this shader identity.");
}

void ShaderBrowserWindow::DrawSettings()
{
    const bool settings_columns =
        ImGui::GetContentRegionAvail().x >=
        900.0f * g_viewport_mgr.m_scale;
    if (settings_columns) ImGui::Columns(2, "##settings-columns", false);
    ImGui::SeparatorText("Usage collection");
    const bool monitoring_off =
        g_config.shader_browser.profiling.monitoring_level == 0;
    ImGui::BeginDisabled(monitoring_off);
    if (ImGui::Checkbox("Collect live usage while this window is open",
                        &m_collection_requested)) {
        GetProvider().SetLiveCollectionEnabled(m_collection_requested);
        m_snapshot.live_collection_enabled = m_collection_requested;
    }
    DrawMutedWrapped(
        "Provider collection is independent of catalog visibility and "
        "preview work.");
    ImGui::SetNextItemWidth(180.0f * g_viewport_mgr.m_scale);
    if (ShaderCombo("Recent catalog window", &table_view.recent_index,
                    "Any time\0Last 30 frames\0Last 120 frames\0"
                    "Last 600 frames\0")) {
        m_filter_dirty = true;
    }
    DrawMutedWrapped(
        "The selected shader remains selected when it ages out of the list.");

    ImGui::SeparatorText("Preview & editing");
    DrawWorkbenchSharedSettings();

    ImGui::SeparatorText("Exports");
    DrawMutedWrapped(
        "Usage exports contain provider observations, not preview draws or "
        "unmeasured GPU data.");
    if (ImGui::Button("Usage CSV")) {
        const char *base = xemu_settings_get_base_path();
        const std::filesystem::path root = base ?
            std::filesystem::u8path(base) : std::filesystem::path();
        std::filesystem::path path;
        std::string error;
        m_action_message = ExportUsageSnapshot(m_snapshot, root, false,
                                               &path, &error) ?
            "Exported usage CSV to " + path.u8string() : error;
    }
    ImGui::SameLine();
    if (ImGui::Button("Usage JSON")) {
        const char *base = xemu_settings_get_base_path();
        const std::filesystem::path root = base ?
            std::filesystem::u8path(base) : std::filesystem::path();
        std::filesystem::path path;
        std::string error;
        m_action_message = ExportUsageSnapshot(m_snapshot, root, true,
                                               &path, &error) ?
            "Exported usage JSON to " + path.u8string() : error;
    }
    const Entry *entry = SelectedEntry();
    if (entry && entry->recipe_size) {
        ImGui::SameLine();
        if (ImGui::Button("Selected recipe")) ExportRecipe(*entry);
    }
    DrawMutedWrapped(
        "Generated GLSL and synthetic test exports are in their source "
        "and Live Preview views.");

    if (settings_columns) ImGui::NextColumn();
    ImGui::SeparatorText("Session overrides & replacement library");
    ImGui::EndDisabled();
    if (monitoring_off) {
        ImGui::TextDisabled(
            "Live monitoring is Off. Change it in the Advanced menu.");
    }
    ImGui::TextDisabled(
        "Live browsing works without database persistence. Persistent storage "
        "is opt-in and never runs on the draw hot path.");

    GetShaderOverrideUi().DrawSettings(&m_action_message);

    ImGui::SeparatorText("Profiling state");
    const bool vulkan =
        g_config.display.renderer == CONFIG_DISPLAY_RENDERER_VULKAN;
    const bool supported =
        vulkan ? m_snapshot.vk_gpu_supported : m_snapshot.gl_gpu_supported;
    const uint32_t pending = vulkan ? m_snapshot.pending_vk_gpu_queries :
                                      m_snapshot.pending_gl_gpu_queries;
    ImGui::Text("Active backend: %s", vulkan ? "Vulkan" : "OpenGL");
    ImGui::Text("GPU timestamps: %s",
                supported ? "Supported" :
                            "Unsupported by active backend/device");
    ImGui::Text("Pending GPU queries: %u", pending);
    ImGui::Text("Dropped profiling samples: %llu",
                static_cast<unsigned long long>(
                    m_snapshot.dropped_performance_samples));
    ImGui::TextDisabled(
        "Monitoring level and sample budgets are in Advanced settings.");

    ImGui::SeparatorText("Timing source");
    const SessionSummary *selected_session = nullptr;
    for (const SessionSummary &session : m_snapshot.timing_sessions) {
        if (session.session_id == m_snapshot.timing_session_id) {
            selected_session = &session;
            break;
        }
    }
    std::vector<std::string> source_names{ "Live session" };
    int source_index = 0;
    for (const SessionSummary &session : m_snapshot.timing_sessions) {
        source_names.push_back(
            (session.ended_unix_ms ? "Previous: " : "Current: ") +
            session.session_id + " | " + session.renderer);
        if (selected_session == &session)
            source_index = int(source_names.size() - 1);
    }
    std::vector<const char *> source_labels;
    for (const auto &name : source_names)
        source_labels.push_back(name.c_str());
    if (ShaderCombo("Timing source", &source_index, source_labels.data(),
                    int(source_labels.size()))) {
        GetProvider().SetTimingSession(
            source_index ?
                m_snapshot.timing_sessions[source_index - 1].session_id :
                "");
        m_next_poll_ms = 0;
    }
    if (selected_session) {
        ImGui::TextDisabled("Host: %s | CPU: %s",
                            selected_session->host_os.empty() ?
                                "Unknown" :
                                selected_session->host_os.c_str(),
                            selected_session->cpu_model.empty() ?
                                "Unknown" :
                                selected_session->cpu_model.c_str());
        ImGui::TextDisabled("GPU: %s | driver: %s | vendor/device: %04X/%04X",
                            selected_session->gpu_name.empty() ?
                                "Unknown" :
                                selected_session->gpu_name.c_str(),
                            selected_session->gpu_driver.empty() ?
                                "Unknown" :
                                selected_session->gpu_driver.c_str(),
                            selected_session->gpu_vendor_id,
                            selected_session->gpu_device_id);
        ImGui::TextDisabled(
            "Revision: %s | resolution scale: %u | uber: %s | cache: %s",
            selected_session->xemu_revision.c_str(),
            selected_session->internal_resolution_scale,
            selected_session->ubershader_mode.c_str(),
            selected_session->shader_cache_enabled ? "On" : "Off");
    }

    ImGui::SeparatorText("Persistence");
    bool database_enabled = g_config.shader_browser.database.enabled;
    if (ImGui::Checkbox("Enable persistent shader database",
                        &database_enabled)) {
        g_config.shader_browser.database.enabled = database_enabled;
        ApplyPersistenceSettings();
    }
    DrawMutedWrapped(
        "Stores shader identity, canonical guest recipes, title/build links, "
        "and lightweight metadata beside the xemu configuration. Disabling "
        "this setting does not delete existing data.");

    ImGui::BeginDisabled(!g_config.shader_browser.database.enabled);
    bool record_sessions =
        g_config.shader_browser.database.record_performance_sessions;
    if (ImGui::Checkbox("Record per-session performance history",
                        &record_sessions)) {
        g_config.shader_browser.database.record_performance_sessions =
            record_sessions;
        ApplyPersistenceSettings();
    }
    DrawMutedWrapped(
        "Keeps each game run separate. Min/max/count/total are stored; "
        "averages are derived per session instead of across the shader's life.");

    bool save_artifacts =
        g_config.shader_browser.database.save_external_artifacts;
    if (ImGui::Checkbox("Save generated/compiled shader artifacts externally",
                        &save_artifacts)) {
        g_config.shader_browser.database.save_external_artifacts =
            save_artifacts;
        ApplyPersistenceSettings();
    }
    DrawMutedWrapped(
        "Large GLSL/SPIR-V/compiler artifacts are files under the artifact "
        "folder. SQLite stores only their metadata and relative path so the "
        "files can be inspected or deleted manually.");
    ImGui::EndDisabled();

    if (ImGui::BeginTable("##database-settings", 2,
                          ImGuiTableFlags_BordersInnerV |
                          ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("##name", ImGuiTableColumnFlags_WidthFixed,
                                155.0f * g_viewport_mgr.m_scale);
        ImGui::TableSetupColumn("##value",
                                ImGuiTableColumnFlags_WidthStretch);
        DrawKeyValue("Database status", m_snapshot.database_status.empty() ?
                     "Disabled" : m_snapshot.database_status);
        DrawKeyValue("Database path", m_snapshot.database_path.empty() ?
                     "Not opened" : m_snapshot.database_path);
        DrawKeyValue("Artifact folder", m_snapshot.artifact_root.empty() ?
                     "Not configured" : m_snapshot.artifact_root);
        DrawKeyValue("Schema version",
                     UnsignedText(m_snapshot.database_schema_version));
        DrawKeyValue("Journal", m_snapshot.database_journal_mode.empty() ?
                     "-" : m_snapshot.database_journal_mode);
        DrawKeyValue("Catalogued shaders",
                     UnsignedText(m_snapshot.stored_shader_count));
        DrawKeyValue("Known titles", UnsignedText(m_snapshot.title_count));
        DrawKeyValue("Performance sessions",
                     UnsignedText(m_snapshot.session_count));
        DrawKeyValue("Session shader rows",
                     UnsignedText(m_snapshot.session_stat_count));
        DrawKeyValue("External artifacts",
                     UnsignedText(m_snapshot.artifact_count));
        DrawKeyValue("Rejected artifacts",
                     UnsignedText(m_snapshot.rejected_artifact_count));
        DrawKeyValue("Failed artifact writes",
                     UnsignedText(m_snapshot.failed_artifact_count));
        if (m_snapshot.failed_artifact_count) {
            DrawKeyValue("Last artifact error",
                         m_snapshot.last_artifact_error);
        }
        DrawKeyValue("Database size", BytesText(m_snapshot.database_bytes));
        DrawKeyValue("WAL size", BytesText(m_snapshot.database_wal_bytes));
        DrawKeyValue("Artifact size",
                     BytesText(m_snapshot.external_artifact_bytes));
        DrawKeyValue("Pending DB writes",
                     UnsignedText(m_snapshot.pending_database_writes));
        DrawKeyValue("Pending live observations",
                     UnsignedText(m_snapshot.pending_observation_count));
        DrawKeyValue("Live session epoch",
                     UnsignedText(m_snapshot.session_epoch));
        ImGui::EndTable();
    }

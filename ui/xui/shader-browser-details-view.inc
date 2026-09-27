// SPDX-License-Identifier: GPL-2.0-or-later

void ShaderBrowserWindow::DrawShaderDetails()
{
    if (!SelectedEntry()) {
        ImGui::TextDisabled("Select a shader to inspect it.");
        return;
    }
    const ImGuiTabItemFlags override_flags = m_jump_to_overrides ?
        ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None;
    if (ImGui::BeginTabBar("##shader-detail-tabs")) {
        if (ImGui::BeginTabItem("Overview")) {
            if (ImGui::BeginChild("##details-overview", ImVec2(0, 0)))
                DrawOverview();
            ImGui::EndChild();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Usage & timing")) {
            if (ImGui::BeginChild("##details-usage", ImVec2(0, 0)))
                DrawUsageTiming();
            ImGui::EndChild();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Source & variants")) {
            DrawSourceVariants();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Overrides", nullptr, override_flags)) {
            if (ImGui::BeginChild("##details-overrides", ImVec2(0, 0)))
                DrawOverrides();
            ImGui::EndChild();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("History")) {
            if (ImGui::BeginChild("##details-history", ImVec2(0, 0))) {
                ImGui::TextDisabled(
                    "Resident lifecycle events for this shader; not a per-draw stream.");
                DrawLifecycle();
            }
            ImGui::EndChild();
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    m_jump_to_overrides = false;
}

void ShaderBrowserWindow::DrawSourceVariants()
{
    if (ImGui::BeginTabBar("##details-source-tabs")) {
        if (ImGui::BeginTabItem("Generated host source")) {
            if (ImGui::BeginChild("##details-host-body", ImVec2(0, 0)))
                DrawHost();
            ImGui::EndChild();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Canonical guest recipe")) {
            if (ImGui::BeginChild("##details-guest-body", ImVec2(0, 0)))
                DrawGuest();
            ImGui::EndChild();
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
}

void ShaderBrowserWindow::DrawGuest()
{
    if (!m_has_recipe) {
        ImGui::TextDisabled("Unavailable: %s", m_recipe_error.c_str());
        return;
    }
    ImGui::Text("Canonical recipe v%u | %s | %s",
                m_selected_recipe.recipe_format_version,
                StageLabel(m_selected_recipe.key.stage),
                BytesText(m_selected_recipe.bytes.size()).c_str());
    if (ImGui::SmallButton("Export recipe")) {
        const Entry *entry = SelectedEntry();
        if (entry) ExportRecipe(*entry);
    }
    ImGui::SameLine();
    ImGui::TextDisabled("Stored beside xemu.toml in shader-exports/");
    if (m_recipe_inspection.instruction_count) {
        ImGui::Text("Vertex instructions: %u",
                    m_recipe_inspection.instruction_count);
    }
    if (ImGui::BeginTable("##guest-fields", 2,
                          ImGuiTableFlags_Borders |
                          ImGuiTableFlags_RowBg |
                          ImGuiTableFlags_ScrollY |
                          ImGuiTableFlags_SizingStretchProp,
                          ImVec2(0, std::max(
                              180.0f * g_viewport_mgr.m_scale,
                              ImGui::GetContentRegionAvail().y)))) {
        ImGui::TableSetupColumn("Canonical field", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableHeadersRow();
        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(m_recipe_inspection.fields.size()));
        while (clipper.Step()) {
            for (int row = clipper.DisplayStart; row < clipper.DisplayEnd;
                 ++row) {
                const GuestField &field = m_recipe_inspection.fields[row];
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextUnformatted(field.name.c_str());
                ImGui::TableSetColumnIndex(1);
                ImGui::TextUnformatted(field.value.c_str());
            }
        }
        ImGui::EndTable();
    }
}

void ShaderBrowserWindow::DrawHost()
{
    const DetailSnapshot &detail = m_detail_snapshot;
    if (detail.request.key != m_selected_key ||
        detail.state == DetailState::Idle) {
        ImGui::TextDisabled("Waiting for selected shader details.");
        return;
    }
    ImGui::Text("%s | %s", DetailBackendLabel(detail.backend),
                DetailStateLabel(detail.state));
    if (!detail.status.empty()) {
        ImGui::TextWrapped("%s", detail.status.c_str());
    }
    if (detail.state == DetailState::Pending) {
        ImGui::TextDisabled("Inspecting resident renderer data...");
        return;
    }
    if (detail.sources.empty() && detail.variants.empty()) {
        ImGui::TextDisabled("Unavailable: no resident host source or variant.");
        return;
    }

    if (!detail.variants.empty() &&
        ImGui::CollapsingHeader("Resident variants", ImGuiTreeNodeFlags_DefaultOpen)) {
        const float variant_height = std::clamp(
            ImGui::GetContentRegionAvail().y * 0.3f,
            105.0f * g_viewport_mgr.m_scale,
            170.0f * g_viewport_mgr.m_scale);
        if (ImGui::BeginChild("##resident-variants",
                              ImVec2(0, variant_height), true)) {
            for (const HostVariant &variant : detail.variants) {
                ImGui::PushID(static_cast<int>(variant.variant_id));
                if (ImGui::TreeNode("##variant", "%s | %s | %s",
                                    variant.label.c_str(), RouteLabel(variant.route),
                                    ReadinessLabel(variant.readiness))) {
                    if (ImGui::BeginTable("##variant-fields", 2,
                                          ImGuiTableFlags_SizingStretchProp)) {
                        ImGui::TableSetupColumn("Field");
                        ImGui::TableSetupColumn("Value");
                        DrawKeyValue("Variant ID", UnsignedText(variant.variant_id));
                        if (variant.valid_fields & HostVariantFrames) {
                            DrawKeyValue("First/last frame",
                                         UnsignedText(variant.first_frame) + " / " +
                                         UnsignedText(variant.last_frame));
                        }
                        if (variant.valid_fields & HostVariantDrawCount) {
                            DrawKeyValue("Draws", UnsignedText(variant.draw_count));
                        }
                        if (variant.valid_fields & HostVariantCompileTime) {
                            DrawKeyValue("Shared program creation CPU (ns)",
                                         UnsignedText(variant.compile_time_ns));
                        }
                        if (variant.valid_fields & HostVariantCreateTime) {
                            DrawKeyValue("Shared binding preparation CPU (ns)",
                                         UnsignedText(variant.create_time_ns));
                        }
                        if (variant.valid_fields & HostVariantPrimitiveMode) {
                            DrawKeyValue("Primitive mode",
                                         UnsignedText(variant.primitive_mode));
                        }
                        if (variant.valid_fields & HostVariantColorFormat) {
                            DrawKeyValue("Color format",
                                         UnsignedText(variant.color_format));
                        }
                        if (variant.valid_fields & HostVariantDepthFormat) {
                            DrawKeyValue("Depth format",
                                         UnsignedText(variant.depth_format));
                        }
                        if (variant.valid_fields & HostVariantVertexLayout) {
                            DrawKeyValue("Vertex bindings/attributes",
                                         UnsignedText(variant.vertex_binding_count) +
                                         " / " +
                                         UnsignedText(variant.vertex_attribute_count));
                        }
                        ImGui::EndTable();
                    }
                    ImGui::TreePop();
                }
                ImGui::PopID();
            }
        }
        ImGui::EndChild();
    }

    if (detail.sources.empty()) return;
    ImGui::SeparatorText("Host source");
    m_selected_source = std::min(m_selected_source, detail.sources.size() - 1);
    const HostSource &selected = detail.sources[m_selected_source];
    if (ImGui::BeginCombo("##host-source", selected.label.c_str())) {
        for (size_t i = 0; i < detail.sources.size(); ++i) {
            const HostSource &source = detail.sources[i];
            ImGui::PushID(static_cast<int>(i));
            if (ImGui::Selectable(source.label.c_str(), i == m_selected_source)) {
                m_selected_source = i;
                m_source_offsets_generation = 0;
            }
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }
    const HostSource &source = detail.sources[m_selected_source];
    ImGui::TextDisabled("%s | %s | %s | %s",
                        HostSourceStageLabel(source.stage),
                        HostSourceKindLabel(source.kind),
                        RouteLabel(source.route),
                        source.exact_runtime_source ? "resident" :
                                                      "reconstructed");
    ImGui::TextDisabled("Process-local artifact ID: %llu",
                        static_cast<unsigned long long>(source.artifact_id));
    ImGui::TextDisabled("Generated source build association is unverified.");
    const Entry *selected_entry = SelectedEntry();
    const bool glsl_available = selected_entry &&
        detail.request.key == m_selected_key &&
        source.kind == HostSourceKind::Glsl && !source.text.empty() &&
        source.text.size() <= kMaxDetailSourceBytes &&
        source.text.find('\0') == std::string::npos;
    ImGui::BeginDisabled(!glsl_available);
    if (ImGui::SmallButton("Copy generated GLSL")) {
        ImGui::SetClipboardText(source.text.c_str());
        m_action_message = "Copied generated GLSL (not a canonical recipe)";
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Export generated GLSL")) {
        const char *base = xemu_settings_get_base_path();
        std::filesystem::path path;
        std::string error;
        if (base && ExportGeneratedGlsl(*selected_entry, source,
                                        std::filesystem::u8path(base),
                                        &path, &error)) {
            m_action_message = "Exported generated GLSL and provenance to " +
                               path.u8string();
        } else {
            m_action_message = error.empty() ?
                "Generated GLSL export path unavailable" : error;
        }
    }
    ImGui::EndDisabled();
    if (m_source_offsets_generation != detail.generation ||
        m_source_line_offsets.empty()) {
        m_source_line_offsets.clear();
        m_source_line_offsets.push_back(0);
        for (size_t i = 0; i < source.text.size(); ++i) {
            if (source.text[i] == '\n' && i + 1 < source.text.size()) {
                m_source_line_offsets.push_back(i + 1);
            }
        }
        m_source_offsets_generation = detail.generation;
    }
    const float source_height = std::max(
        180.0f * g_viewport_mgr.m_scale,
        ImGui::GetContentRegionAvail().y);
    if (ImGui::BeginChild("##host-source-lines", ImVec2(0, source_height), true,
                          ImGuiWindowFlags_HorizontalScrollbar)) {
        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(m_source_line_offsets.size()));
        while (clipper.Step()) {
            for (int line = clipper.DisplayStart; line < clipper.DisplayEnd;
                 ++line) {
                size_t start = m_source_line_offsets[line];
                size_t end = line + 1 <
                                     static_cast<int>(m_source_line_offsets.size()) ?
                                 m_source_line_offsets[line + 1] :
                                 source.text.size();
                if (end > start && source.text[end - 1] == '\n') --end;
                ImGui::TextUnformatted(source.text.data() + start,
                                       source.text.data() + end);
            }
        }
    }
    ImGui::EndChild();
}

void ShaderBrowserWindow::DrawLifecycle()
{
    const DetailSnapshot &detail = m_detail_snapshot;
    if (detail.request.key != m_selected_key ||
        detail.state == DetailState::Idle ||
        detail.state == DetailState::Pending) {
        ImGui::TextDisabled("Waiting for selected shader lifecycle.");
        return;
    }
    if (detail.lifecycle.empty()) {
        ImGui::TextDisabled("Unavailable: no resident lifecycle history.");
        return;
    }
    ImGui::Text("%zu events | %llu older events dropped",
                detail.lifecycle.size(),
                static_cast<unsigned long long>(
                    detail.dropped_lifecycle_events));
    ImGuiListClipper clipper;
    clipper.Begin(static_cast<int>(detail.lifecycle.size()));
    while (clipper.Step()) {
        for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
            const LifecycleEvent &event = detail.lifecycle[row];
            ImGui::Text("#%llu frame %llu | %s | %s",
                        static_cast<unsigned long long>(event.sequence),
                        static_cast<unsigned long long>(event.frame),
                        LifecycleKindLabel(event.kind),
                        event.message.c_str());
        }
    }
}

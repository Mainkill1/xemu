// SPDX-License-Identifier: GPL-2.0-or-later
// Include from shader-browser.cc after common.hh / shader-browser.hh.
#include "shader-browser-glsl-editor.hh"
#include "shader-browser-workbench-draft.hh"

#include <algorithm>
#include <string>

namespace xemu::shader_browser {

struct WorkbenchEditorDockState {
    WorkbenchEditorDockState()
    {
        for (size_t i = 0; i < bindings.size(); ++i) {
            bindings[i].target = static_cast<PreviewInputTarget>(i);
            bindings[i].base =
                (i == 0 || (i >= 3 && i <= 6) || i == 8) ?
                    1.0f : 0.0f;
        }
    }
    bool open = false;
    bool auto_compile = true;
    PreviewSourceVariant variant = PreviewSourceVariant::Original;
    std::string edit_buffer;
    uint64_t buffer_revision = 0;
    GlslEditorModel original_editor;
    PreviewDigest original_digest{};
    bool original_loaded = false;
    bool draft_view_valid = false;
    std::string search;
    size_t search_offset = 0;
    size_t goto_line = 0;
    std::array<PreviewInputBinding, kPreviewMaxInputBindings> bindings{};
};

struct WorkbenchEditorDockAction {
    bool create_copy = false;
    bool manual_compile = false;
};

static ImVec4 GlslTokenColor(GlslTokenKind kind)
{
    switch (kind) {
    case GlslTokenKind::Keyword: return ImVec4(0.58f, 0.75f, 1.0f, 1.0f);
    case GlslTokenKind::Number: return ImVec4(0.98f, 0.71f, 0.44f, 1.0f);
    case GlslTokenKind::Comment: return ImVec4(0.57f, 0.72f, 0.57f, 1.0f);
    case GlslTokenKind::Preprocessor:
        return ImVec4(0.89f, 0.64f, 0.91f, 1.0f);
    }
    return ImVec4(1, 1, 1, 1);
}

// Call from the collapsed Inputs drawer. A changed binding requires a new
// result packet, never a GLSL recompile.
static bool DrawWorkbenchBindingControls(WorkbenchEditorDockState *state)
{
    if (!state) return false;
    bool changed = false;
    ImGui::TextDisabled("Synthetic clock bindings; unused inputs may remain static");
    for (size_t i = 0; i < state->bindings.size(); ++i) {
        PreviewInputBinding &binding = state->bindings[i];
        ImGui::PushID(static_cast<int>(i));
        changed |= ImGui::Checkbox(PreviewInputTargetName(binding.target),
                                    &binding.enabled);
        if (binding.enabled) {
            ImGui::SameLine();
            changed |= ImGui::DragFloat("Base", &binding.base, 0.01f,
                                         -16.0f, 16.0f);
            changed |= ImGui::DragFloat("Amplitude", &binding.amplitude,
                                         0.01f, -16.0f, 16.0f);
            changed |= ImGui::DragFloat("Period (s)",
                                         &binding.period_seconds, 0.05f,
                                         0.05f, 3600.0f);
        }
        ImGui::PopID();
    }
    return changed;
}

// The colored source view is clipped; the ImGui edit field below supplies
// selection, clipboard, IME, undo, and native keyboard behavior.
static void DrawWorkbenchSourceLines(GlslEditorModel *editor,
                                     WorkbenchEditorDockState *state)
{
    if (ImGui::BeginChild("##workbench-source-lines", ImVec2(0, 145), true,
                          ImGuiWindowFlags_HorizontalScrollbar)) {
        if (state->goto_line && state->goto_line <= editor->LineCount()) {
            ImGui::SetScrollY((state->goto_line - 1) *
                              ImGui::GetTextLineHeightWithSpacing());
            state->goto_line = 0;
        }
        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(editor->LineCount()));
        while (clipper.Step()) {
            for (int line = clipper.DisplayStart; line < clipper.DisplayEnd;
                 ++line) {
                std::string_view text = editor->Line(line);
                const auto tokens = editor->TokenizeLine(line);
                ImGui::TextDisabled("%5d", line + 1);
                ImGui::SameLine();
                size_t cursor = 0;
                auto draw = [](std::string_view span, const ImVec4 *color) {
                    if (span.empty()) return;
                    if (color) ImGui::PushStyleColor(ImGuiCol_Text, *color);
                    ImGui::TextUnformatted(span.data(), span.data() + span.size());
                    if (color) ImGui::PopStyleColor();
                    ImGui::SameLine(0, 0);
                };
                for (const GlslToken &token : tokens) {
                    draw(text.substr(cursor, token.start - cursor), nullptr);
                    ImVec4 color = GlslTokenColor(token.kind);
                    draw(text.substr(token.start, token.end - token.start),
                         &color);
                    cursor = token.end;
                }
                draw(text.substr(cursor), nullptr);
                if (text.empty()) ImGui::TextUnformatted(" ");
                ImGui::NewLine();
            }
        }
    }
    ImGui::EndChild();
}

static WorkbenchEditorDockAction DrawWorkbenchEditorDock(
    const GeneratedSourceSnapshot *generated, WorkbenchDraft *draft,
    GlslEditorModel *editor, WorkbenchEditorDockState *state,
    uint64_t now_ns)
{
    WorkbenchEditorDockAction action{};
    if (!state || !editor) return action;
    ImGui::SeparatorText("Generated GLSL / private draft");
    const bool has_draft = draft && draft->Id();
    const GeneratedSourceSnapshot *original = has_draft ?
        &draft->Base() : generated;
    if (original) {
        ImGui::TextDisabled("%s %s | %s | %s | TitleID %08X",
            original->resident ? "Resident GLSL" : "Reconstructed GLSL",
            HostSourceStageLabel(original->stage),
            PreviewBackendLabel(original->backend),
            RouteLabel(original->route), original->scope.title_id);
        ImGui::TextWrapped("xemu-generated GLSL; not game-authored%s",
            original->build_scope_verified ? "" :
                " | selected build association unverified");
        if (generated && ImGui::Button("Create editable copy"))
            action.create_copy = true;
    } else {
        ImGui::TextDisabled("No generated GLSL for this selection");
        return action;
    }
    if (!state->original_loaded || state->original_digest != original->digest) {
        state->original_loaded = state->original_editor.SetSource(original->text);
        if (state->original_loaded) state->original_digest = original->digest;
    }
    if (has_draft) {
        int variant = static_cast<int>(state->variant);
        if (ImGui::RadioButton("Original", variant == 0)) variant = 0;
        ImGui::SameLine();
        if (ImGui::RadioButton("Edited", variant == 1)) variant = 1;
        state->variant = static_cast<PreviewSourceVariant>(variant);
    } else {
        state->variant = PreviewSourceVariant::Original;
        ImGui::TextDisabled("Original GLSL (read-only)");
    }
    const bool edited = has_draft &&
        state->variant == PreviewSourceVariant::Edited;
    GlslEditorModel *shown_editor = &state->original_editor;
    if (edited) {
        if (state->buffer_revision != draft->Revision()) {
            state->edit_buffer = draft->Text();
            state->buffer_revision = draft->Revision();
            state->draft_view_valid = false;
        }
        if (!state->draft_view_valid)
            state->draft_view_valid = editor->SetSource(state->edit_buffer);
        shown_editor = editor;
        ImGui::Checkbox("Auto-compile", &state->auto_compile);
        ImGui::SameLine();
        if (ImGui::Button("Compile")) action.manual_compile = true;
    }
    if (has_draft) {
        ImGui::TextUnformatted(draft->StatusLabel().c_str());
        if (edited) {
            ImGui::SameLine();
            ImGui::TextDisabled("Edit GLSL");
        }
    }
    if (edited) {
        if (ImGui::InputTextMultiline("##editable-glsl", &state->edit_buffer,
                                       ImVec2(-1, 125))) {
            if (draft->Edit(state->edit_buffer, now_ns)) {
                state->buffer_revision = draft->Revision();
                state->draft_view_valid =
                    editor->SetSource(state->edit_buffer);
            }
        }
    }
    if (edited && !draft->Diagnostics().empty()) {
        ImGui::SeparatorText("Compiler messages");
        const std::string &log = draft->Diagnostics();
        size_t begin = 0;
        while (begin < log.size()) {
            size_t end = log.find('\n', begin);
            if (end == std::string::npos) end = log.size();
            GlslDiagnostic diagnostic = ParseGlslDiagnosticLine(
                log.substr(begin, end - begin));
            if (diagnostic.line &&
                diagnostic.line <= shown_editor->LineCount()) {
                if (ImGui::SmallButton(("Line " +
                    std::to_string(diagnostic.line) + "##" +
                    std::to_string(begin)).c_str())) {
                    state->goto_line = diagnostic.line;
                }
                ImGui::SameLine();
            }
            ImGui::TextWrapped("%s", diagnostic.message.c_str());
            begin = end + 1;
        }
    }
    if (!edited && !state->original_loaded) {
        ImGui::TextDisabled("Original GLSL cannot be displayed");
        return action;
    }
    if (ImGui::CollapsingHeader("Syntax view (read-only)",
                               ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::InputText("Search GLSL", &state->search);
        ImGui::SameLine();
        if (ImGui::Button("Next") && !state->search.empty()) {
            auto offset = shown_editor->FindNext(state->search,
                                                 state->search_offset);
            if (offset) {
                state->search_offset = *offset;
                state->goto_line = shown_editor->LineForOffset(*offset) + 1;
            }
        }
        DrawWorkbenchSourceLines(shown_editor, state);
    }
    return action;
}

} // namespace xemu::shader_browser

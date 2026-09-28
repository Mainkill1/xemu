// SPDX-License-Identifier: GPL-2.0-or-later
// Included after common.hh on the HUD context shared by the workbench.
namespace {
struct CapturedImageCache {
    GLuint texture = 0;
    uint64_t revision = 0;
    int selection = -1;
};
CapturedImageCache captured_output_cache, captured_texture_cache;

void ReleaseCapturedImages()
{
    for (CapturedImageCache *cache :
         { &captured_output_cache, &captured_texture_cache }) {
        if (cache->texture)
            glDeleteTextures(1, &cache->texture);
        *cache = {};
    }
}

void DrawCapturedImage(const xemu::shader_browser::OwnedDrawImage &image,
                       uint64_t revision, int selection, float max_width,
                       float max_height, bool resource)
{
    if (!image.width || !image.height ||
        image.rgba.size() != size_t(image.width) * image.height * 4) {
        ImGui::TextWrapped(
            resource ? "This bound resource has no supported image snapshot." :
                       "No observed image is available. Find a usable draw; "
                       "unsupported targets retain metadata.");
        return;
    }
    CapturedImageCache &cache =
        resource ? captured_texture_cache : captured_output_cache;
    if (!cache.texture || cache.revision != revision ||
        cache.selection != selection) {
        GLint texture = 0, unpack_buffer = 0, alignment = 0, row_length = 0;
        GLint skip_pixels = 0, skip_rows = 0;
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &texture);
        glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING, &unpack_buffer);
        glGetIntegerv(GL_UNPACK_ALIGNMENT, &alignment);
        glGetIntegerv(GL_UNPACK_ROW_LENGTH, &row_length);
        glGetIntegerv(GL_UNPACK_SKIP_PIXELS, &skip_pixels);
        glGetIntegerv(GL_UNPACK_SKIP_ROWS, &skip_rows);
        if (!cache.texture)
            glGenTextures(1, &cache.texture);
        glBindTexture(GL_TEXTURE_2D, cache.texture);
        glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
        glPixelStorei(GL_UNPACK_SKIP_PIXELS, 0);
        glPixelStorei(GL_UNPACK_SKIP_ROWS, 0);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, image.width, image.height, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, image.rgba.data());
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glBindTexture(GL_TEXTURE_2D, texture);
        glBindBuffer(GL_PIXEL_UNPACK_BUFFER, unpack_buffer);
        glPixelStorei(GL_UNPACK_ALIGNMENT, alignment);
        glPixelStorei(GL_UNPACK_ROW_LENGTH, row_length);
        glPixelStorei(GL_UNPACK_SKIP_PIXELS, skip_pixels);
        glPixelStorei(GL_UNPACK_SKIP_ROWS, skip_rows);
        cache.revision = revision;
        cache.selection = selection;
    }
    const float scale =
        std::min(max_width / image.width, max_height / image.height);
    const ImVec2 size(std::max(1.0f, image.width * scale),
                      std::max(1.0f, image.height * scale));
    ImGui::Image(static_cast<ImTextureID>(cache.texture), size);
}

struct CapturedTextureExportUi {
    std::future<std::pair<std::filesystem::path, std::string>> job;
    std::shared_ptr<xemu::shader_browser::CaptureFileControl> control;
    std::filesystem::path output;
    std::string message;
};
CapturedTextureExportUi captured_texture_export;

void DrawTextureExport(
    const xemu::shader_browser::OwnedDrawTexture &texture,
    const xemu::shader_browser::CaptureExportIdentity *identity,
    const xemu::shader_browser::DrawEventKey *event)
{
    using namespace xemu::shader_browser;
    auto &state = captured_texture_export;
    if (state.job.valid() && state.job.wait_for(std::chrono::seconds(0)) ==
                                 std::future_status::ready) {
        try {
            auto result = state.job.get();
            state.output = std::move(result.first);
            state.message = std::move(result.second);
        } catch (const std::exception &error) {
            if (state.control)
                state.control->Finish(false);
            state.message =
                std::string("Texture export failed: ") + error.what();
        }
    }
    ImGui::BeginDisabled(state.job.valid() || !identity || !event);
    if (ImGui::Button("Export captured texture")) {
        try {
            const char *base = xemu_settings_get_base_path();
            if (!base)
                throw std::runtime_error("Texture export path is unavailable.");
            const auto parent =
                std::filesystem::u8path(base) / "shader-captures";
            const auto control = std::make_shared<CaptureFileControl>();
            state.control = control;
            state.output.clear();
            state.message.clear();
            state.job = std::async(
                std::launch::async, [texture, identity = *identity,
                                     event = *event, parent, control] {
                    std::filesystem::path path;
                    std::string error;
                    std::filesystem::create_directories(parent);
                    const bool success = ExportCapturedTexturePackage(
                        texture, identity, event, parent, &path, &error,
                        control.get());
                    return std::make_pair(
                        path, success ?
                                  "Exported captured T" +
                                      std::to_string(texture.metadata.slot) +
                                      " from F" + std::to_string(event.frame) +
                                      " / D" + std::to_string(event.draw) +
                                      ": " + path.u8string() :
                                  error);
                });
        } catch (const std::exception &error) {
            state.message =
                std::string("Cannot start texture export: ") + error.what();
        }
    }
    ImGui::EndDisabled();
    ImGui::TextDisabled("PNG + RGBA8 bytes for each captured mip / face.");
    if (state.job.valid() && state.control) {
        const auto progress = state.control->Progress();
        ImGui::TextDisabled("%s%s", CaptureFilePhaseLabel(progress.phase),
                            progress.cancel_requested ? " — cancelling…" : "…");
        if (progress.total)
            ImGui::ProgressBar(
                float(double(progress.completed) / progress.total),
                ImVec2(-1, 0));
        ImGui::BeginDisabled(progress.cancel_requested || progress.finished ||
                             progress.publication_started);
        if (ImGui::SmallButton("Cancel texture export"))
            state.control->RequestCancel();
        ImGui::EndDisabled();
    }
    if (!state.message.empty())
        ImGui::TextWrapped("%s", state.message.c_str());
    if (!state.output.empty() &&
        ImGui::SmallButton("Open texture export folder")) {
        std::string error;
        if (!OpenDirectory(state.output.u8string(), &error))
            state.message = error;
    }
}

void DrawCapturedInputs(
    const xemu::shader_browser::OwnedDrawInputs &inputs, uint64_t revision,
    int *slot, int *selected_image,
    const xemu::shader_browser::CaptureExportIdentity *identity = nullptr,
    const xemu::shader_browser::DrawEventKey *event = nullptr)
{
    using namespace xemu::shader_browser;
    ImGui::PushTextWrapPos(0.0f);
    ImGui::SeparatorText("Captured draw fidelity");
    size_t bound = 0, captured = 0;
    for (const auto &texture : inputs.textures) {
        if (!texture.described || !texture.metadata.bound)
            continue;
        ++bound;
        if (!texture.images.empty())
            ++captured;
    }
    ImGui::TextDisabled("Observed output: %s",
                        inputs.after.rgba.empty() ? "Missing" : "Captured");
    ImGui::TextDisabled("Vertex / pixel / host geometry source: %s / %s / %s",
                        inputs.sources[1].empty() && inputs.sources[4].empty() ?
                            "Missing" :
                            "Captured",
                        inputs.sources[2].empty() ? "Missing" : "Captured",
                        inputs.sources[3].empty() ? "Not present" : "Captured");
    ImGui::TextDisabled("Texture slots: %zu/%zu captured", captured, bound);
    ImGui::TextDisabled("Uniforms: %zu | State fields: %zu",
                        inputs.uniforms.size(), inputs.registers.size());
    ImGui::TextDisabled(
        "Replay fidelity: partial; original camera can use captured vertex "
        "stages. Raster and destination dependencies remain incomplete.");
    if (ImGui::CollapsingHeader("Textures", ImGuiTreeNodeFlags_DefaultOpen)) {
        const char *slots[] = { "T0", "T1", "T2", "T3" };
        if (ShaderCombo("Slot", slot, slots, 4))
            *selected_image = 0;
        const auto &texture = inputs.textures[*slot];
        if (!texture.described || !texture.metadata.bound) {
            ImGui::TextDisabled("Not bound / not described");
        } else {
            const auto &m = texture.metadata;
            ImGui::Text("%u x %u | %u mips | %u faces", m.width, m.height,
                        m.mip_levels, m.face_count);
            ImGui::TextDisabled("Guest format 0x%X | Host format 0x%X",
                                m.guest_format, m.host_format);
            ImGui::TextDisabled("Filter %u/%u | Wrap %u/%u/%u", m.min_filter,
                                m.mag_filter, m.wrap_s, m.wrap_t, m.wrap_r);
            DrawTextureExport(texture, identity, event);
            if (!texture.images.empty()) {
                *selected_image = std::clamp(*selected_image, 0,
                                             int(texture.images.size() - 1));
                auto label = [](const OwnedDrawTextureImage &image) {
                    return "Mip " + std::to_string(image.mip_level) +
                           " / face " + std::to_string(image.face);
                };
                std::vector<std::string> image_names;
                for (const auto &image : texture.images)
                    image_names.push_back(label(image));
                std::vector<const char *> image_labels;
                for (const auto &name : image_names)
                    image_labels.push_back(name.c_str());
                ShaderCombo("Image", selected_image, image_labels.data(),
                            int(image_labels.size()));
                DrawCapturedImage(texture.images[*selected_image].image,
                                  revision, *slot * 100 + *selected_image,
                                  ImGui::GetContentRegionAvail().x,
                                  140.0f * g_viewport_mgr.m_scale, true);
            } else {
                ImGui::TextWrapped("Image missing or unsupported. Export "
                                   "retains the available metadata.");
            }
        }
        ImGui::TextDisabled(
            "Extract draw package exports every captured mip and face.");
    }
    if (ImGui::CollapsingHeader("Constants / uniforms")) {
        for (const auto &uniform : inputs.uniforms) {
            ImGui::PushID(&uniform);
            ImGui::Text("%s [%u x %u]", uniform.name.c_str(),
                        uniform.components, uniform.count);
            std::string values;
            const size_t n = std::min<size_t>(uniform.data.size() / 4, 16);
            for (size_t i = 0; i < n; ++i) {
                if (i)
                    values += ", ";
                char text[48];
                if (uniform.type == XEMU_SHADER_DRAW_UNIFORM_INT) {
                    int32_t value;
                    std::memcpy(&value, uniform.data.data() + i * 4, 4);
                    std::snprintf(text, sizeof(text), "%d", value);
                } else if (uniform.type == XEMU_SHADER_DRAW_UNIFORM_UINT) {
                    uint32_t value;
                    std::memcpy(&value, uniform.data.data() + i * 4, 4);
                    std::snprintf(text, sizeof(text), "%u", value);
                } else {
                    float value;
                    std::memcpy(&value, uniform.data.data() + i * 4, 4);
                    std::snprintf(text, sizeof(text), "%.6g", double(value));
                }
                values += text;
            }
            if (uniform.data.size() / 4 > n)
                values += ", ...";
            ImGui::TextWrapped("%s", values.c_str());
            ImGui::PopID();
        }
        if (inputs.uniforms.empty())
            ImGui::TextDisabled("No supported active uniform snapshot.");
    }
    if (ImGui::CollapsingHeader("Render state / capture status")) {
        for (const auto &value : inputs.registers)
            ImGui::Text("%s = 0x%08X", value.name.c_str(), value.value);
        ImGui::TextWrapped("Raw latched and host capture fields. These are "
                           "inspection evidence; "
                           "free-camera preview does not restore the original "
                           "raster pipeline.");
    }
    ImGui::PopTextWrapPos();
}
} // namespace

// SPDX-License-Identifier: GPL-2.0-or-later
#include "shader-browser-details-model.hh"

#include <sstream>

namespace xemu::shader_browser {
namespace {

bool ValidateText(const char *field, const std::string &value,
                  std::string *error)
{
    if (value.size() > kMaxDetailTextBytes) {
        if (error) {
            std::ostringstream os;
            os << field << " exceeds " << kMaxDetailTextBytes << " bytes";
            *error = os.str();
        }
        return false;
    }
    if (value.find('\0') != std::string::npos) {
        if (error) {
            *error = std::string(field) + " contains an embedded NUL";
        }
        return false;
    }
    return true;
}

} // namespace

const char *DetailBackendLabel(DetailBackend backend)
{
    switch (backend) {
    case DetailBackend::Active:
        return "Active renderer";
    case DetailBackend::OpenGL:
        return "OpenGL";
    case DetailBackend::Vulkan:
        return "Vulkan";
    case DetailBackend::Unknown:
        return "Unknown";
    }
    return "Unknown";
}

const char *DetailStateLabel(DetailState state)
{
    switch (state) {
    case DetailState::Idle:
        return "Idle";
    case DetailState::Pending:
        return "Pending";
    case DetailState::Complete:
        return "Complete";
    case DetailState::Partial:
        return "Partial";
    case DetailState::Unavailable:
        return "Unavailable";
    case DetailState::Failed:
        return "Failed";
    }
    return "Failed";
}

const char *HostSourceStageLabel(HostSourceStage stage)
{
    switch (stage) {
    case HostSourceStage::Vertex:
        return "Vertex";
    case HostSourceStage::Geometry:
        return "Geometry";
    case HostSourceStage::Fragment:
        return "Fragment";
    case HostSourceStage::LinkedProgram:
        return "Linked program";
    case HostSourceStage::Pipeline:
        return "Pipeline";
    case HostSourceStage::Unknown:
        return "Unknown";
    }
    return "Unknown";
}

const char *HostSourceKindLabel(HostSourceKind kind)
{
    switch (kind) {
    case HostSourceKind::Glsl:
        return "GLSL";
    case HostSourceKind::SpirvText:
        return "SPIR-V text";
    case HostSourceKind::Metadata:
        return "Metadata";
    case HostSourceKind::Unknown:
        return "Unknown";
    }
    return "Unknown";
}

const char *LifecycleKindLabel(LifecycleKind kind)
{
    switch (kind) {
    case LifecycleKind::Discovered:
        return "Discovered";
    case LifecycleKind::SourceGenerated:
        return "Source generated";
    case LifecycleKind::CompileQueued:
        return "Compile queued";
    case LifecycleKind::CompileStarted:
        return "Compile started";
    case LifecycleKind::CompileCompleted:
        return "Compile completed";
    case LifecycleKind::CompileFailed:
        return "Compile failed";
    case LifecycleKind::ModulePublished:
        return "Module published";
    case LifecycleKind::PipelineRequested:
        return "Pipeline requested";
    case LifecycleKind::PipelineCreated:
        return "Pipeline created";
    case LifecycleKind::PipelinePublished:
        return "Pipeline published";
    case LifecycleKind::Selected:
        return "Selected";
    case LifecycleKind::Removed:
        return "Removed";
    case LifecycleKind::DetailRequestCompleted:
        return "Detail request completed";
    case LifecycleKind::Unknown:
        return "Unknown";
    }
    return "Unknown";
}

bool DetailBackendMatches(DetailBackend requested, DetailBackend renderer)
{
    if (renderer != DetailBackend::OpenGL &&
        renderer != DetailBackend::Vulkan) {
        return false;
    }
    return requested == DetailBackend::Active || requested == renderer;
}

bool ValidateDetailResult(const DetailResult &result, std::string *error)
{
    if (result.request_id == 0) {
        if (error) {
            *error = "detail result request ID is zero";
        }
        return false;
    }
    if (result.state != DetailState::Complete &&
        result.state != DetailState::Partial &&
        result.state != DetailState::Unavailable &&
        result.state != DetailState::Failed) {
        if (error) {
            *error = "detail result has a non-terminal state";
        }
        return false;
    }
    if (!ValidateText("detail status", result.status, error)) {
        return false;
    }

    for (const HostSource &source : result.sources) {
        if (source.text.size() > kMaxDetailSourceBytes) {
            if (error) {
                *error = "host source exceeds 4 MiB";
            }
            return false;
        }
        if (source.text.find('\0') != std::string::npos) {
            if (error) {
                *error = "host source contains an embedded NUL";
            }
            return false;
        }
        if (!ValidateText("host source label", source.label, error)) {
            return false;
        }
    }

    for (const HostVariant &variant : result.variants) {
        if (!ValidateText("host variant label", variant.label, error)) {
            return false;
        }
    }

    for (const LifecycleEvent &event : result.lifecycle) {
        if (!ValidateText("lifecycle message", event.message, error)) {
            return false;
        }
    }

    return true;
}

} // namespace xemu::shader_browser

// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "shader-browser-capture-file-control.hh"
#include "shader-browser-draw-capture.hh"
#include "shader-browser-draw-inputs.hh"
#include "shader-browser-preview-model.hh"

#include <filesystem>
#include <string>

namespace xemu::shader_browser {

struct CaptureExportIdentity {
    ShaderKey shader;
    ShaderScope scope;
    PreviewBackend backend = PreviewBackend::Unknown;
};

// The difference is the absolute RGB change with opaque alpha, in top-down
// RGBA8. On failure the destination is cleared.
bool MakeCapturedDrawDifference(const OwnedDrawImage &before,
                                const OwnedDrawImage &after,
                                OwnedDrawImage *difference,
                                std::string *error = nullptr);

// Publishes one unique directory atomically inside an existing parent. The
// package owns decoded host inputs; it is neither an engine asset nor a full
// guest draw replay. The result path is cleared on failure.
bool ExportCapturedDrawPackage(const OwnedDrawInputs &inputs,
                               const PreviewCapturedMesh &mesh,
                               const DrawCaptureSummary &summary,
                               const CaptureExportIdentity &identity,
                               const std::filesystem::path &parent_directory,
                               std::filesystem::path *result_directory,
                               std::string *error = nullptr);

// Exports this owned host-decoded texture's captured mip/face images as PNG and
// exact RGBA8 bytes, with draw/scope provenance. It never resolves a live
// handle.
bool ExportCapturedTexturePackage(const OwnedDrawTexture &texture,
                                  const CaptureExportIdentity &identity,
                                  const DrawEventKey &event,
                                  const std::filesystem::path &parent_directory,
                                  std::filesystem::path *result_directory,
                                  std::string *error = nullptr,
                                  CaptureFileControl *control = nullptr);

} // namespace xemu::shader_browser

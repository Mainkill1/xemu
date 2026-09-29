#include "shader-browser-stage3-ui.hh"
#include "shader-browser-preview-service.hh"
#include "shader-browser-preview-adapter.hh"
#include "shader-browser-preview-gl.hh"
#include "shader-browser-session-provider.hh"
#include "shader-browser-workbench-apply.hh"
#include "shader-browser-workbench-bundle.hh"
#include "shader-browser-preview-alpha.hh"
#include "shader-browser-draw-request.h"
#include "shader-browser-draw-request.hh"
#include "shader-browser-capture-export.hh"
#include "shader-browser-capture-file-control.hh"
#include "shader-browser-capture-session.hh"
#include "shader-browser-capture-comparison.hh"
#include "shader-browser-capture-inspection.hh"
#include "asset-browser.hh"
#include <future>
#include <map>
#include "shader-browser-override-runtime.h"
#include <memory>
#include "common.hh"
#include "shader-browser-selection-ui.hh"
#include "shader-browser-list-navigation.hh"
#include "xemu-hud.h"
#include "shader-browser-workbench-editor-ui.inc"
#include "shader-browser-workbench-scene-ui.inc"
static void CancelWorkbenchCaptureWhenHidden();
static void UseLiveShaderSelection();
static bool IsInspectingCapturedOccurrence();
static const xemu::shader_browser::Entry *InspectedCapturedEntry();
namespace {
static void PollAllShaderCapture();
static bool ConfirmCaptureClose();
static void DrawCaptureCloseChoice(bool *open);
static bool CaptureComparisonVisible();
static std::string OpenFittedCapturedOccurrence(
    const xemu::shader_browser::PreviewSelection &);
static const char *CaptureFilePhaseLabel(xemu::shader_browser::CaptureFilePhase);
} // namespace
#include "shader-browser-part1.inc"
#include "shader-browser-part2.inc"
#include "shader-browser-part3.inc"
#include "shader-browser-part4.inc"
#include "shader-browser-capture-comparison-ui.inc"
#include "shader-browser-capture-inspection-ui.inc"
#include "shader-browser-capture-workspace-ui.inc"
#include "shader-browser-details-view.inc"

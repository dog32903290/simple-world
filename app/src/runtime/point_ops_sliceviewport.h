// runtime/point_ops_sliceviewport — SliceViewPort (TiXL render/transform): render a Command subtree into
// ONE grid cell — a sub-viewport rect + a per-mode CameraToClipSpace scale, with RequestedResolution pushed
// to the cell size. Command-rail STAMP op (the Camera/Group per-item stamp precedent). runtime leaf.
//
// v1 SCOPE (named fork-sliceviewport-repeatview-only): RepeatView mode (M11/M22 clip scale, no M31/M32 crop
// offset). SliceView/FitProjection (cs:75-93 crop) are a deferred follow-on. The viewport rect + the
// RequestedResolution push are mode-independent (all three modes share them), so those are fully faithful.
#pragma once

#include <cstdint>
#include <map>
#include <string>

namespace sw {

struct RenderResolution;

// Register the "SliceViewPort" cmd op; called by registerDrawPointOps.
void registerSliceViewPortOp();

// The RequestedResolution this op PUSHES around its subtree cook = the CELL size (prevRes / cellCounts,
// clamped [1,16384]) — TiXL SliceViewPort.cs:40-41,103. Shared by the flat + resident cook drivers (the
// resolveSetRequestedResolution precedent — the driver pushes BEFORE cooking the subtree). `current` = the
// ambient RequestedResolution the cell divides.
RenderResolution resolveSliceViewPortResolution(const std::map<std::string, float>& params,
                                                RenderResolution current);

// Compute the cell VIEWPORT rect + RepeatView clip M11/M22 scale for this op's params at the given output
// resolution (SliceViewPort.cs:32-73, RepeatView branch). outViewport = {x,y,w,h} in output PIXELS;
// outClipScale = {M11mul, M22mul}. Pure function (no encoder) so the golden can assert the closed form and
// the cook can stamp it. `resW/resH` = the AMBIENT RequestedResolution (the grid divides this, cs:31,40).
void computeSliceViewPortCell(const std::map<std::string, float>& params, uint32_t resW, uint32_t resH,
                              float outViewport[4], float outClipScale[2]);

// Test-only tooth: when true cookSliceViewPort SKIPS the viewport stamp (hasViewport stays false) — as if the
// SliceViewPort effect were unwired → the subtree draws full-target instead of into its cell → the golden's
// "content lands in the cell, not outside it" assertion goes RED. OFF in production. CPU op flag.
bool& sliceViewPortDisableStampForTest();

// --selftest entry (point_ops_sliceviewport_golden.cpp).
int runSliceViewPortSelfTest(bool injectBug);

}  // namespace sw

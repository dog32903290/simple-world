#pragma once
// mathv_ref_pointsimulation — CPU scalar oracle for TiXL PointSimulation (points/sim).
//
// TRANSCRIBED from external/tixl (SHA 395c4c55)
// Operators/Lib/Assets/shaders/points/combine/PointSimulation.hlsl — NOT derived from sw's MSL kernel
// (app/shaders/pointsimulation.metal intentionally never opened while writing this file).
//   cbuffer Params (MixOriginal,Reset) :10-14
//   main() body                        :17-48
//   qSlerp (Rotation blend, :46)        -> mathv_ref_shared_quat.h::quat::qSlerp (SAME
//     external/tixl shared/quat-functions.hlsl:162-215 this file already transcribes — reused per
//     MATH_VERIFY_WORKFLOW.md §1.4 "大依賴閉包獨立成共享 oracle header", not re-transcribed here).
//
// SEMANTICS (faithfully transcribed):
//   :20-23 Reset>0.5 -> ResultPoints[i]=SourcePoints[i], done (unconditional reset).
//   :25-29 bound guard: i.x>=sourcePointcount -> ResultPoints[i] UNCHANGED (early return, in-place
//     UAV — the point retains whatever it already held; not zeroed, not touched).
//   :31-38 isnan TRIPLE-OR reset trap: isnan(orgW=SourcePoints.W) OR isnan(currentW=ResultPoints.W)
//     OR isnan(ResultPoints.Position.x) [NOTE: checks the CURRENT side's Position.x only — NOT
//     SourcePoints.Position.x; this asymmetry is transcribed exactly, not "fixed" to check both] ->
//     ResultPoints[i]=SourcePoints[i] (full reset from source), done.
//   :40-46 otherwise: every field (W/Position/Color/Stretch/Selected) is lerp(current, source,
//     MixOriginal); Rotation is qSlerp(current, source, MixOriginal) — current=ResultPoints (the
//     "a" argument), source=SourcePoints (the "b" argument), MATCHING the lerp arg order exactly
//     (current first, source second, in every one of the five/six blended fields).
//
// PROVENANCE (GOLDEN_STANDARD.md P5-safe oracle 判準): zero metal include, zero app/shaders/
// reference, zero sw math helper (mathv_ref_shared_quat.h is itself a P5-safe TRANSCRIBED oracle) —
// pure host arithmetic transcribed from the HLSL text above.
//
// ZONE: shell-tier mathv support (pure math; app/src/ root, no runtime/platform/Metal dependency).
#include <cmath>
#include <cstdint>

#include "mathv_ref_shared_quat.h"

namespace sw {
namespace mathv_ref {

struct PointSimulationParams {
  float mixOriginal;
  float reset;  // authored as a bool in TiXL; transcribed as the raw float compare (:20 `Reset>0.5`)
};

// One point's full field set (all six are blended/copied — unlike SimBlendTo/AppendPoints, this
// kernel touches EVERY field, so the fuzz TU's SCOPE can legitimately stay narrower than "all six"
// only if it says so explicitly; this ref models all six for completeness).
struct PointSimulationPoint {
  float pos[3];
  float w;
  float rot[4];  // x,y,z,w (quaternion)
  float color[4];
  float stretch[3];
  float selected;
};

// pointSimulationOne — HLSL main():17-48 for ONE dispatch index `idx`.
//   current       = ResultPoints[idx] BEFORE this dispatch (in-place UAV "a" side)
//   src           = SourcePoints[idx] ("b" side)
//   sourcePointcount = the GetDimensions element count (host-fed; see fuzz TU for how this maps to
//                    the transpiled kernel's `Count*64u` byte-size substitution, §10.5①)
inline void pointSimulationOne(uint32_t idx, const PointSimulationPoint& current,
                               const PointSimulationPoint& src, uint32_t sourcePointcount,
                               const PointSimulationParams& p, PointSimulationPoint& out) {
  if (p.reset > 0.5f) { out = src; return; }              // :20-23
  if (idx >= sourcePointcount) { out = current; return; } // :25-29 in-place UAV: untouched
  const float orgW = src.w, currentW = current.w;         // :31-32
  if (std::isnan(orgW) || std::isnan(currentW) || std::isnan(current.pos[0])) {  // :34
    out = src;                                             // :35-36
    return;
  }
  const float t = p.mixOriginal;
  out.w = current.w + (src.w - current.w) * t;             // :40 lerp(currentW, orgW, t)
  for (int k = 0; k < 3; ++k)
    out.pos[k] = current.pos[k] + (src.pos[k] - current.pos[k]) * t;  // :41
  for (int k = 0; k < 4; ++k)
    out.color[k] = current.color[k] + (src.color[k] - current.color[k]) * t;  // :42
  for (int k = 0; k < 3; ++k)
    out.stretch[k] = current.stretch[k] + (src.stretch[k] - current.stretch[k]) * t;  // :43
  out.selected = current.selected + (src.selected - current.selected) * t;  // :44
  quat::Quat a{current.rot[0], current.rot[1], current.rot[2], current.rot[3]};
  quat::Quat b{src.rot[0], src.rot[1], src.rot[2], src.rot[3]};
  quat::Quat q = quat::qSlerp(a, b, t);                     // :46 qSlerp(current, source, t)
  out.rot[0] = q.x; out.rot[1] = q.y; out.rot[2] = q.z; out.rot[3] = q.w;
}

}  // namespace mathv_ref
}  // namespace sw

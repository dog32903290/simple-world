// @tixl: SplinePoints   (point/combine family — even arc-length resample of a cardinal cubic spline)
// SplinePoints — COMBINE op: joins all wired Points inputs into one ordered control-point polygon,
// fits a cardinal cubic-bezier spline THROUGH those control points, then resamples `count` output
// points EVENLY BY ARC LENGTH along the spline. Faithful CPU port of TiXL SplinePoints.
//
// Reference (BACKWARD-TRACED, Cut-58 lesson — real behavior, not assumption):
//   external/tixl/Operators/Lib/point/combine/SplinePoints.cs       (slots + clamps + Update)
//   external/tixl/Core/Utils/Splines/BezierPointSpline.cs           (SamplePointsEvenly — THE math)
//   external/tixl/Core/Utils/Splines/Bezier.cs                      (GetPoint 4-arg cubic)
//   external/tixl/Core/Utils/MathUtils.cs:617                       (LookAt quaternion)
//
// WHAT IT IS (the spline, exact):
//   The control points come from the joined Points input list (.cs: Points.CollectedInputs joined,
//   needs >= 2). The curve is NOT a chain of independent Bezier handles you author — it is a CARDINAL
//   spline: SampleCubicBezier(t) maps t in [0,1] onto segment i (tt = t*(N-2), i=floor(tt), local
//   t = tt-i) between control[i]..control[i+1], with the two interior control handles DERIVED from the
//   neighbors and the Curvature param (h0/h1 in the header). End conditions: at the first segment
//   pLast := pA (no left neighbor); at the last segment pNext := pB (no right neighbor). Curvature is
//   the cardinal tension knob (larger => flatter handles, the spline hugs the control polygon).
//
// EVEN ARC-LENGTH RESAMPLE (BezierPointSpline.SamplePointsEvenly, ported 1:1):
//   1. Pre-sample the WHOLE spline at `preSampleSteps` uniform-in-t stations, accumulating chord
//      length into lengthList[k] (k=1..preSampleSteps-1). lengthList[0] stays 0 (C# new float[]
//      zero-init, NEVER written — faithfully reproduced below).
//   2. For output index in [0,count): wantedLength = totalLength*index/(count-1) + 0.0002; walk
//      walkedIndex forward until lengthList[walkedIndex+1] >= wantedLength; locally interpolate a
//      fractional pre-sample station; t = (walkedIndex + fraction)/(preSampleSteps-1); sample the
//      spline at (t - 0.0002). This makes output spacing UNIFORM IN ARC LENGTH, not in t.
//   3. Per output point: Position = spline(t-0.0002); F1 = 1; Orientation = LookAt(normalize(dPos),
//      -upVector); Color = linear-interp of control-point colors at t; Scale = (1,1,1); F2 = 1.
//
// PARAM clamps (.cs:24-28, verbatim): preSampleSteps -> clamp(5,100); count -> clamp(1,1000).
//
// COUNT POLICY (.cs:28 + the SamplePointsEvenly result array): output count = SampleCount param
//   (clamped 1..1000), INDEPENDENT of the input control-point count. This op is NOT a sum/concat:
//   the inputs are the spline's CONTROL polygon, the output is the resampled curve. The driver sizes
//   c.output to the "Count" Float port when a spec exposes one; this self-contained leaf's golden
//   drives c.count directly (cook-only, no NodeSpec yet — same pattern as BlendPoints).
//
// FAITHFUL EDGE (NOT a fork): count==1 makes wantedLength = totalLength*0/(1-1) = 0/0 = NaN in TiXL
//   (no guard). We reproduce it verbatim. The .cs clamps count >= 1, so this is a real TiXL latent
//   edge; callers use count >= 2.
//
// Pure-CPU op: no kernel, no .metal. The cook writes SwPoint directly into the PointGraph-owned
// (StorageModeShared) output buffer. Math + golden share point_ops_splinepoints.h (≤400 ratchet).
#include "runtime/point_ops.h"

#include <cstring>
#include <vector>

#include <Metal/Metal.hpp>

#include "runtime/point_graph.h"             // PointCookCtx, registerPointOp, cookParam/cookVecN
#include "runtime/point_ops_splinepoints.h"  // shared spline math (header-only)
#include "runtime/tixl_point.h"              // SwPoint (64B)

namespace sw {

using namespace splinepoints_detail;

// Cook: gather control polygon from the joined Points inputs, fit + even-arc-length resample.
void cookSplinePoints(PointCookCtx& c) {
  if (!c.output || c.count == 0) return;

  const uint32_t outCount = c.count;  // == clamp(SampleCount,1,1000) sized by the driver
  // .cs:24,28 clamps; preSampleSteps default 25 (the .t3 default), curvature default 4, up = +Y.
  int preSampleSteps = (int)std::lround(cookParam(c, "PreSampleSteps", 25.0f));
  preSampleSteps = (int)clampf((float)preSampleSteps, 5.0f, 100.0f);
  float curvature = cookParam(c, "Curvature", 4.0f);
  float upDef[3] = {0.0f, 1.0f, 0.0f};
  float up[3];
  cookVecN(c, "UpVector", upDef, 3, up);
  V3 upVector = {up[0], up[1], up[2]};

  // Join all wired Points inputs in port order = the control polygon (.cs Points.CollectedInputs).
  std::vector<V3> pos;
  std::vector<float> f1;
  std::vector<V4> col;
  for (int in = 0; in < c.inputCount; ++in) {
    const MTL::Buffer* buf = c.inputs ? c.inputs[in] : nullptr;
    uint32_t n = (c.inputCounts) ? c.inputCounts[in] : 0u;
    if (!buf || n == 0) continue;
    const SwPoint* sp = reinterpret_cast<const SwPoint*>(const_cast<MTL::Buffer*>(buf)->contents());
    for (uint32_t k = 0; k < n; ++k) {
      pos.push_back({sp[k].Position.x, sp[k].Position.y, sp[k].Position.z});
      f1.push_back(sp[k].FX1);
      col.push_back({sp[k].Color.x, sp[k].Color.y, sp[k].Color.z, sp[k].Color.w});
    }
  }

  SwPoint* out = reinterpret_cast<SwPoint*>(c.output->contents());

  // .cs:37-43 — need >= 2 control points; else the op emits no buffer (we zero the bag).
  if (pos.size() < 2) {
    std::memset(out, 0, (size_t)outCount * sizeof(SwPoint));
    return;
  }

  // --- BezierPointSpline.SamplePointsEvenly, ported 1:1 ---
  std::vector<float> lengthList(preSampleSteps, 0.0f);  // [0] stays 0 (C# zero-init, never written)

  float totalLength = 0.0f;
  V3 lastPoint = sampleCubicBezier(0.0f, curvature, pos, f1);
  for (int k = 1; k < preSampleSteps; ++k) {
    float t = (float)k / (float)preSampleSteps;
    V3 newPoint = sampleCubicBezier(t, curvature, pos, f1);
    totalLength += dist(newPoint, lastPoint);
    lastPoint = newPoint;
    lengthList[k] = totalLength;
  }

  int walkedIndex = 0;
  V3 lastPos = sampleCubicBezier(0.0f, curvature, pos, f1);

  for (uint32_t index = 0; index < outCount; ++index) {
    float wantedLength = totalLength * (float)index / (float)(outCount - 1) + 0.0002f;

    while (wantedLength > lengthList[walkedIndex + 1] && walkedIndex < preSampleSteps - 2 &&
           walkedIndex < (int)lengthList.size() - 2) {
      walkedIndex++;
    }

    float l0 = lengthList[walkedIndex];
    float l1 = lengthList[walkedIndex + 1];
    float deltaL = l1 - l0;
    float fraction = (wantedLength - l0) / (deltaL + 0.00001f);
    float t = ((float)walkedIndex + fraction) / (float)(preSampleSteps - 1);

    V3 p = sampleCubicBezier(t - 0.0002f, curvature, pos, f1);
    V3 d = sub(p, lastPos);
    lastPos = p;

    V4 q = lookAt(normalize(d), scl(upVector, -1.0f));  // LookAt(normalize(d), -upVector)
    V4 color = sampleLinearColors(t, col);

    SwPoint& o = out[index];
    o = SwPoint{};
    o.Position = {p.x, p.y, p.z};
    o.FX1 = 1.0f;  // result[index].F1 = 1
    o.Rotation = {q.x, q.y, q.z, q.w};
    o.Color = {color.x, color.y, color.z, color.w};
    o.Scale = {1.0f, 1.0f, 1.0f};
    o.FX2 = 1.0f;  // result[index].F2 = 1
  }
}

void registerSplinePointsOp() {
  // Output count = SampleCount param (the "Count" Float port the driver sizes c.output from); NOT a
  // sum/concat of the control-point inputs. No countFromFirstPointsInput / countTransform.
  registerPointOp("SplinePoints", cookSplinePoints);
}

}  // namespace sw

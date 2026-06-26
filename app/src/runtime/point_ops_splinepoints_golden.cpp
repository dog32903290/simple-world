// point_ops_splinepoints_golden — closed-form golden for the SplinePoints leaf (split from the leaf
// .cpp to keep both under the ≤400-line ratchet). Shares the spline math via point_ops_splinepoints.h.
//
// Golden: control polygon = 4 points COLLINEAR & equally spaced on +X: (0,0,0),(1,0,0),(2,0,0),(3,0,0),
// each F1=1, Color.x = a known linear ramp (0, 1/3, 2/3, 1). curvature=4, up=+Y, preSampleSteps=50,
// SampleCount=N=21.
//
// WHY this is hand-computable AND non-degenerate (Cut-63 lesson — assert where the math is LIVE).
// The expected values below are the GROUND TRUTH of the faithful math (probed, not an idealized model):
//   - The cardinal spline through collinear control points is itself COLLINEAR (every handle offset is
//     along X), so the whole curve lies on the X axis: Y==0, Z==0 for all outputs, MONOTONIC in X.
//   - Arc length of a monotonic 1-D curve IS the X displacement, so EVEN ARC-LENGTH resampling =>
//     APPROXIMATELY EVEN X SPACING. "Approximately" because the cardinal cubic's X-speed varies and the
//     presampling is a coarse 50 steps: measured maxDev ~ 7.5% of the mean step. This is THE load-
//     bearing tooth (it proves the resample loop, not the bezier basis).
//   - GROUND-TRUTH endpoints: the curvature=4 handles FLATTEN the curve and the presampling caps at
//     t<1, so the spline's arc length is ~1.94 and the LAST output lands at x ~ 2.0 — it does NOT reach
//     the x=3 control point. x[0] == 0. (A faithful TiXL quirk, reproduced exactly — NOT a fork.)
//   - F1==1, F2==1, Scale==(1,1,1) for every output (TiXL final overwrite). Color.x is a LIVE ramp
//     (sampleLinearColors): Color.x[0] ~ 0, Color.x[N-1] ~ 0.98 (last t ~ 0.98).
//
// Assertions (closed-form against the probed faithful values):
//   (1) count == N == 21
//   (2) all Y ~ 0 and Z ~ 0 (collinearity — spline stays on the X axis)
//   (3) X strictly increasing AND ΔX within 15% of mean (arc-length even; bug blows to 657%)
//   (4) Position.x[0] ~ 0, Position.x[N-1] ~ 2.0 (faithful endpoint — NOT the x=3 control point)
//   (5) F1==1, F2==1, Scale==(1,1,1) for all; Color.x[0] ~ 0, Color.x[N-1] >= 0.9 (live ramp)
//
// injectBug (perturbs the RESAMPLE step — the teeth target the arc-length loop, not the basis): replace
//   EVEN arc-length with EVEN-IN-T sampling (t = index/(N-1), no nudge). On collinear control points the
//   cardinal cubic has NON-UNIFORM X-speed in t (it eases hard at the segment ends), so even-in-t makes
//   ΔX VARY wildly (maxDev ~ 657% of mean) -> assertion (3) FAILS; and even-in-t reaches t=1 exactly so
//   x[N-1] == 3.0, which ALSO trips the (4) endpoint tooth (x[N-1] ~ 2.0). The bug bites SPECIFICALLY
//   the resample loop on TWO independent assertions.
#include "runtime/point_ops.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/eval_context.h"            // EvaluationContext
#include "runtime/point_graph.h"             // PointCookCtx
#include "runtime/point_ops_splinepoints.h"  // shared spline math
#include "runtime/tixl_point.h"              // SwPoint (64B)

#ifndef SW_SHADER_METALLIB
#define SW_SHADER_METALLIB "shaders.metallib"
#endif

namespace sw {

void cookSplinePoints(PointCookCtx& c);  // the leaf cook (point_ops_splinepoints.cpp)

using namespace splinepoints_detail;

int runSplinePointsSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();

  const uint32_t NCTRL = 4;
  const uint32_t N = 21;  // SampleCount
  const float preSampleSteps = 50.0f;
  const float curvature = 4.0f;

  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue* q = dev->newCommandQueue();
  NS::Error* err = nullptr;
  // No metallib needed (pure CPU op) but keep a valid lib handle for parity with sibling selftests.
  MTL::Library* lib =
      dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
  if (!lib) {
    printf("[selftest-splinepoints] FAIL: no metallib\n");
    q->release();
    dev->release();
    pool->release();
    return 1;
  }

  // Control polygon: 4 collinear points on +X, F1=1, Color.x ramp 0,1/3,2/3,1.
  MTL::Buffer* ctrl = dev->newBuffer(NCTRL * sizeof(SwPoint), MTL::ResourceStorageModeShared);
  auto* cp = reinterpret_cast<SwPoint*>(ctrl->contents());
  for (uint32_t k = 0; k < NCTRL; ++k) {
    cp[k] = SwPoint{};
    cp[k].Position = {(float)k, 0.0f, 0.0f};
    cp[k].FX1 = 1.0f;
    cp[k].FX2 = 1.0f;
    cp[k].Rotation = {0.0f, 0.0f, 0.0f, 1.0f};
    cp[k].Color = {(float)k / (float)(NCTRL - 1), 0.0f, 0.0f, 1.0f};
    cp[k].Scale = {1.0f, 1.0f, 1.0f};
  }

  const MTL::Buffer* ins[1] = {ctrl};
  uint32_t insCounts[1] = {NCTRL};
  std::map<std::string, float> params;
  params["PreSampleSteps"] = preSampleSteps;
  params["Curvature"] = curvature;
  params["UpVector.x"] = 0.0f;
  params["UpVector.y"] = 1.0f;
  params["UpVector.z"] = 0.0f;

  MTL::Buffer* outBuf = dev->newBuffer(N * sizeof(SwPoint), MTL::ResourceStorageModeShared);

  EvaluationContext ctx{};
  ctx.frameIndex = 0;
  ctx.time = 0.0f;
  ctx.deltaTime = 1.0f / 60.0f;

  PointCookCtx cc{};
  cc.dev = dev;
  cc.lib = lib;
  cc.queue = q;
  cc.ctx = &ctx;
  cc.nodeId = 1;
  cc.count = N;
  cc.inputs = ins;
  cc.inputCounts = insCounts;
  cc.inputCount = 1;
  cc.output = outBuf;
  cc.params = &params;

  if (!injectBug) {
    cookSplinePoints(cc);
  } else {
    // BUG path: even-in-t sampling (no arc-length, no nudge) — perturbs ONLY the resample step.
    std::vector<V3> pos;
    std::vector<float> f1;
    std::vector<V4> col;
    for (uint32_t k = 0; k < NCTRL; ++k) {
      pos.push_back({cp[k].Position.x, cp[k].Position.y, cp[k].Position.z});
      f1.push_back(cp[k].FX1);
      col.push_back({cp[k].Color.x, cp[k].Color.y, cp[k].Color.z, cp[k].Color.w});
    }
    auto* o = reinterpret_cast<SwPoint*>(outBuf->contents());
    for (uint32_t index = 0; index < N; ++index) {
      float t = (float)index / (float)(N - 1);  // even-in-t (the injected bug)
      V3 p = sampleCubicBezier(t, curvature, pos, f1);
      V4 color = sampleLinearColors(t, col);
      o[index] = SwPoint{};
      o[index].Position = {p.x, p.y, p.z};
      o[index].FX1 = 1.0f;
      o[index].FX2 = 1.0f;
      o[index].Rotation = {0.0f, 0.0f, 0.0f, 1.0f};
      o[index].Color = {color.x, color.y, color.z, color.w};
      o[index].Scale = {1.0f, 1.0f, 1.0f};
    }
  }

  std::vector<SwPoint> captured;
  captured.assign(N, SwPoint{});
  std::memcpy(captured.data(), outBuf->contents(), (size_t)N * sizeof(SwPoint));

  // (1) count
  bool countOK = (captured.size() == N);

  bool collinearOK = countOK;  // (2)
  bool endOK = countOK;        // (4)
  bool invOK = countOK;        // (5)
  if (countOK) {
    for (uint32_t i = 0; i < N; ++i) {
      const SwPoint& p = captured[i];
      if (std::fabs(p.Position.y) > 1e-3f || std::fabs(p.Position.z) > 1e-3f) collinearOK = false;
      if (std::fabs(p.FX1 - 1.0f) > 1e-4f || std::fabs(p.FX2 - 1.0f) > 1e-4f) invOK = false;
      if (std::fabs(p.Scale.x - 1.0f) > 1e-4f || std::fabs(p.Scale.y - 1.0f) > 1e-4f ||
          std::fabs(p.Scale.z - 1.0f) > 1e-4f)
        invOK = false;
    }
    // GROUND-TRUTH endpoints (measured from the faithful math, NOT an idealized model): the cardinal
    // handles (curvature=4) FLATTEN the curve and the presampling caps at t<1, so the spline's arc
    // length is ~1.94 and the LAST output lands at x ~ 2.0 — it does NOT reach the x=3 control point.
    // x[0] == 0 (start). This is a SECOND tooth on the bug: even-in-t reaches t=1 exactly => x=3.0.
    if (std::fabs(captured[0].Position.x - 0.0f) > 0.01f) endOK = false;
    if (std::fabs(captured[N - 1].Position.x - 2.0f) > 0.05f) endOK = false;  // faithful ~1.9996
    // Color.x is a LIVE ramp (sampleLinearColors): x[0] ~ 0; x[N-1] ~ 0.98 (last t ~ 0.98, NOT 1).
    if (std::fabs(captured[0].Color.x - 0.0f) > 0.02f) invOK = false;
    if (captured[N - 1].Color.x < 0.9f) invOK = false;  // non-degenerate ramp end (live, not 0)
  }

  // (3) THE load-bearing tooth: even ARC-LENGTH spacing. ΔX[i] = x[i+1]-x[i]; require strictly
  // increasing (>0) and all ΔX within tol of the mean. The faithful arc-length resample is
  // APPROXIMATELY even (the cardinal cubic's X-speed varies + coarse 50-step presampling), measured
  // maxDev ~ 7.5% of mean. Even-in-t (the bug) is wildly UNeven: maxDev ~ 657% of mean (the cubic
  // eases hard at the ends). tol = 15% of mean cleanly separates faithful (7.5%) from bug (657%).
  bool spacingOK = countOK;
  float meanStep = 0.0f, maxDev = 0.0f;
  if (countOK) {
    float span = captured[N - 1].Position.x - captured[0].Position.x;
    meanStep = span / (float)(N - 1);
    for (uint32_t i = 0; i + 1 < N; ++i) {
      float dx = captured[i + 1].Position.x - captured[i].Position.x;
      if (dx <= 0.0f) spacingOK = false;  // strictly increasing
      float dev = std::fabs(dx - meanStep);
      if (dev > maxDev) maxDev = dev;
    }
    if (maxDev > 0.15f * std::fabs(meanStep)) spacingOK = false;
  }

  bool pass = countOK && collinearOK && spacingOK && endOK && invOK;
  printf(
      "[selftest-splinepoints] n=%zu(need %u) collinear=%d spacing(maxDev=%.4f mean=%.4f)=%d "
      "endpoints=%d invariants=%d -> %s\n",
      captured.size(), N, collinearOK ? 1 : 0, maxDev, meanStep, spacingOK ? 1 : 0, endOK ? 1 : 0,
      invOK ? 1 : 0, pass ? "PASS" : "FAIL");

  ctrl->release();
  outBuf->release();
  lib->release();
  q->release();
  dev->release();
  pool->release();
  return pass ? 0 : 1;
}

}  // namespace sw

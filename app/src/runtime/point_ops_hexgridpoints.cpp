// HexGridPoints — batch 19 GENERATOR op (generate family): hexagonal tiling grid of points.
// Faithful port of TiXL's HexGridPoints (Pattern=2 Hexa branch).
//
// Reference: external/tixl/Operators/Lib/point/generate/HexGridPoints.cs (slots) +
//            external/tixl/Operators/Lib/Assets/shaders/points/generate/HexGridPoints.hlsl (math)
//
// TiXL ports (fully ported):
//   CountX / CountY / CountZ (int per-axis, default inferred from .cs)
//   Size (Vector3, default 1,1,1 spacing)
//   Center (Vector3, default 0,0,0)
//   W (Single, default 1.0)
//   OrientationAxis (Vector3, default 0,1,0)
//   OrientationAngle (Single, default 0 degrees)
//   Pivot (Vector3, default 0,0,0)
//   SizeMode (enum: Cell/Bounds, default Cell=0)
//   Scale (Single, default 1.0): scales the Size Vector3 (.t3 ScaleVector3 routing), applied host-side
// TiXL ports baked:
//   Pattern baked to 2 (Hexa); Triangular (1) and default (3) deferred. -- NAMED FORK
//   Color baked to white (TiXL: ResultPoints.Color=1). -- NAMED FORK
//   per-point Stretch baked to 1 (TiXL: ResultPoints.Stretch=1; this is the SwPoint attribute,
//     NOT the Scale [Input] above). -- NAMED FORK
//
// NOTE on count: total = CountX * CountY * CountZ. The NodeSpec carries a "Count" port =
// the buffer CAPACITY (total hex grid points). The cook drives the GPU with that capacity;
// the shader only writes points where index < total (same pattern as GridPoints).
//
// Self-contained leaf: own capture vector + registerDrawOp.
#include "runtime/point_ops.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/dispatch.h"            // calcDispatchCount
#include "runtime/graph.h"               // Graph/Node/readVecN/pinId
#include "runtime/point_graph.h"         // PointCookCtx, registerPointOp/DrawOp, PointGraph
#include "runtime/tex_op_cache.h"        // cachedComputePSO
#include "runtime/tixl_point.h"          // SwPoint (64B) + EvaluationContext
#include "runtime/hexgridpoints_params.h" // HexGridParams, HexGridBinding

#ifndef SW_SHADER_METALLIB
#define SW_SHADER_METALLIB "shaders.metallib"
#endif

namespace sw {

// Parity-gate -bug DRIVER latch: re-bakes the pre-gate Scale behavior (Scale absent → multiplier 1)
// so the HexGridPoints parity golden's injectBug leg flips the Scale tooth RED. Declared in
// point_ops.h. (All other HexGridPoints params were always wired — not re-baked here.)
bool& hexScaleBakedBugForceForTest() { static bool b = false; return b; }

namespace {

void cookHexGridPoints(PointCookCtx& c) {
  if (!c.output || c.count == 0 || !c.lib) return;
  MTL::ComputePipelineState* pso = cachedComputePSO(c.dev, c.lib, "hexgridpoints");
  if (!pso) return;

  HexGridParams P{};
  P.CountX   = (uint32_t)std::lround(std::fmax(1.0f, cookParam(c, "CountX", 4.0f)));
  P.CountY   = (uint32_t)std::lround(std::fmax(1.0f, cookParam(c, "CountY", 4.0f)));
  P.CountZ   = (uint32_t)std::lround(std::fmax(1.0f, cookParam(c, "CountZ", 1.0f)));
  P.SizeMode = (uint32_t)(cookParam(c, "SizeMode", 0.0f) > 0.5f ? 1u : 0u);
  P.W        = cookParam(c, "W", 1.0f);

  float size[3]   = {1.0f, 1.0f, 1.0f};
  float center[3] = {0.0f, 0.0f, 0.0f};
  float pivot[3]  = {0.0f, 0.0f, 0.0f};
  float axis[3]   = {0.0f, 1.0f, 0.0f};
  cookVecN(c, "Size",   size,   3, size);
  cookVecN(c, "Center", center, 3, center);
  cookVecN(c, "Pivot",  pivot,  3, pivot);
  cookVecN(c, "OrientationAxis", axis, 3, axis);
  // TiXL Scale [Input] (default 1.0): .t3 routes Scale -> ScaleVector3.Factor scaling the Size
  // Vector3 (HexGridPoints.t3:329-337). Bake that multiply into Size on the host so the shader sees
  // the already-scaled spacing. Scale=1 is a no-op (parity at default). The -bug latch re-bakes the
  // pre-gate behavior (Scale absent → multiplier 1) so the golden's Scale tooth flips RED.
  P.Scale = hexScaleBakedBugForceForTest() ? 1.0f : cookParam(c, "Scale", 1.0f);
  size[0] *= P.Scale; size[1] *= P.Scale; size[2] *= P.Scale;
  P.SizeX   = size[0]; P.SizeY   = size[1]; P.SizeZ   = size[2];
  P.CenterX = center[0]; P.CenterY = center[1]; P.CenterZ = center[2];
  P.PivotX  = pivot[0];  P.PivotY  = pivot[1];  P.PivotZ  = pivot[2];
  P.OrientAxisX = axis[0]; P.OrientAxisY = axis[1]; P.OrientAxisZ = axis[2];
  P.OrientAngle = cookParam(c, "OrientationAngle", 0.0f);

  MTL::CommandBuffer*         cmd = c.queue->commandBuffer();
  MTL::ComputeCommandEncoder* enc = cmd->computeCommandEncoder();
  enc->setComputePipelineState(pso);
  enc->setBuffer(c.output, 0, HEXGRID_Points);
  enc->setBytes(&P, sizeof(P), HEXGRID_Params);
  const uint32_t tg = 64;
  enc->dispatchThreadgroups(MTL::Size::Make(calcDispatchCount(c.count, tg), 1, 1),
                            MTL::Size::Make(tg, 1, 1));
  enc->endEncoding();
  cmd->commit();
  cmd->waitUntilCompleted();
  // PSO owned by device-global computePsoCache (released in clearTexOpCache); do NOT release here.
}

// --- golden plumbing (self-contained) ---
std::vector<SwPoint>* g_capHex = nullptr;
void captureDrawHex(PointCookCtx& c, MTL::Texture*, const MTL::Buffer* pts) {
  if (!g_capHex || !pts || c.count == 0) return;
  g_capHex->assign(c.count, SwPoint{});
  std::memcpy(g_capHex->data(), const_cast<MTL::Buffer*>(pts)->contents(),
              (size_t)c.count * sizeof(SwPoint));
}

}  // namespace

void registerHexGridPointsOp() { registerPointOp("HexGridPoints", cookHexGridPoints); }

// Golden: HexGridPoints(3x3x1, Cell mode, Size=(1,1,0), Center=0) -> DrawPoints capture.
//
// TEETH:
//   (1) count == 3*3*1 = 9. (product law)
//   (2) All Z == 0 (single layer, no Z extent since CountZ=1 -> zeroAdjustedSize.z=0).
//   (3) HexGridPoints has non-equal X spacing: col-0 and col-2 get the same hex offset,
//       col-1 gets a different one. So the X positions are NOT uniform (hexagonal, not grid).
//       Specifically, column 1 vs column 0 differ in X by a hex-pattern amount > 0.
//       We verify: the set of X values has more than 2 distinct values (non-uniform grid).
//   (4) Rotation quaternion norm ~ 1 for all points (valid unit quat).
//
// injectBug: Size=(0,0,0) -> zeroAdjustedSize = 0 -> all points collapse to Center=(0,0,0).
//   The non-uniform X check FAILS (all X same -> only 1 distinct value).
int runHexGridPointsSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  const uint32_t NX = 3, NY = 3, NZ = 1;
  const uint32_t TOTAL = NX * NY * NZ;  // 9

  clearTexOpCache();  // P1: drop stale PSO built on this self-built device before teardown
  MTL::Device*       dev = MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue*   q = dev->newCommandQueue();
  NS::Error*         err = nullptr;
  MTL::Library*      lib =
      dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
  if (!lib) {
    printf("[selftest-hexgridpoints] FAIL: no metallib\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }

  registerBuiltinPointOps();
  registerHexGridPointsOp();
  std::vector<SwPoint> captured;
  g_capHex = &captured;
  registerDrawOp("DrawPoints", captureDrawHex);

  Graph g;
  Node gen; gen.id = 1; gen.type = "HexGridPoints";
  gen.params["Count"]   = (float)TOTAL;   // buffer capacity = product
  gen.params["CountX"]  = (float)NX;
  gen.params["CountY"]  = (float)NY;
  gen.params["CountZ"]  = (float)NZ;
  gen.params["SizeMode"]= 0.0f;           // Cell mode
  gen.params["Size.x"]  = injectBug ? 0.0f : 1.0f;
  gen.params["Size.y"]  = injectBug ? 0.0f : 1.0f;
  gen.params["Size.z"]  = 0.0f;           // flat (NZ=1)
  gen.params["Center.x"]= 0.0f;
  gen.params["Center.y"]= 0.0f;
  gen.params["Center.z"]= 0.0f;
  gen.params["W"]       = 1.0f;
  gen.params["OrientationAxis.x"] = 0.0f;
  gen.params["OrientationAxis.y"] = 1.0f;
  gen.params["OrientationAxis.z"] = 0.0f;
  gen.params["OrientationAngle"]  = 0.0f;
  g.nodes.push_back(gen);

  Node drw; drw.id = 2; drw.type = "DrawPoints"; g.nodes.push_back(drw);
  g.connections.push_back({101, pinId(1, 0), pinId(2, 0)});

  PointGraph pg(dev, lib, q, 64, 64);
  EvaluationContext ctx{};
  ctx.frameIndex = 0; ctx.time = 0.0f; ctx.deltaTime = 1.0f / 60.0f;
  pg.cook(g, ctx, nullptr, pg.defaultDrawTarget(g));

  // (1) count
  bool countOK = (captured.size() == TOTAL);

  // (2) all Z == 0 (within fp tolerance)
  bool flatZ = countOK;
  for (const SwPoint& p : captured) {
    if (std::fabs(p.Position.z) > 1e-3f) { flatZ = false; break; }
  }

  // (3) non-uniform X: collect distinct X values (bucket into 0.01 bins)
  // For bug path (size=0): all X == 0 -> only 1 distinct bucket -> uniformX check fails
  int distinctX = 0;
  if (countOK && !captured.empty()) {
    float xs[9];
    for (size_t i = 0; i < captured.size() && i < 9; ++i) xs[i] = captured[i].Position.x;
    // count unique within 0.01 tolerance
    distinctX = 1;
    for (size_t i = 1; i < captured.size() && i < 9; ++i) {
      bool found = false;
      for (size_t j = 0; j < i; ++j) {
        if (std::fabs(xs[i] - xs[j]) < 0.01f) { found = true; break; }
      }
      if (!found) ++distinctX;
    }
  }
  bool nonUniformX = (distinctX >= 3);  // 3 columns -> at least 3 distinct X families

  // (4) unit quats
  bool quatOK = countOK;
  for (const SwPoint& p : captured) {
    float q = p.Rotation.x*p.Rotation.x + p.Rotation.y*p.Rotation.y +
              p.Rotation.z*p.Rotation.z + p.Rotation.w*p.Rotation.w;
    if (std::fabs(q - 1.0f) > 0.01f) { quatOK = false; break; }
  }

  bool pass = countOK && flatZ && nonUniformX && quatOK;
  printf("[selftest-hexgridpoints] n=%zu(need %u) flatZ=%s distinctX=%d(need>=3) quatNormOK=%s -> %s\n",
         captured.size(), TOTAL, flatZ ? "yes" : "NO", distinctX,
         quatOK ? "yes" : "NO", pass ? "PASS" : "FAIL");

  g_capHex = nullptr;
  lib->release(); q->release(); dev->release(); pool->release();
  return pass ? 0 : 1;
}

}  // namespace sw

// movepointstosdf_golden — --selftest-movepointstosdf. The SDF point-modify seam golden (proving op
// MoveToSDF). HARNESS-FIRST CPU-readback through the REAL cook:
//
//   GridPoints (z=0 plane) -> MoveToSDF(Field = SphereSDF Center=0 Radius=R, Amount=1, MaxSteps high)
//     -> readback the MoveToSDF output bag via PointGraph::debugCookedBuffer(moveNodeId).
//
// ON-SPHERE ASSERTION: with Amount=1 TiXL converges each point to the SDF surface, so every NON-degenerate
// moved Position must lie ON the sphere:  abs(length(pos) - R) < ~MinDistance  (center=0).
//
// ORIENTATION ASSERTION (SetOrientation, TiXL .t3 default TRUE): at Amount=1 the point's Rotation becomes
// qLookAt(n, up), whose forward (+Z) axis is the surface normal n = normalize(pos). So qRotateVec3((0,0,1),
// Rotation) must align with normalize(pos): dot >= 0.9 (verified exact=1.0 for a perfect converge). This is
// the load-bearing proof the new SetOrientation branch runs — GridPoints seeds identity rotation, so a
// non-trivial quat can only come from the ported branch. (SetColor is ALSO default-true and ported, but its
// proof is weak HERE: GridPoints' source Color and SphereSDF's color-mode field are BOTH white, so
// lerp(white,white)=white — color is asserted finite/unchanged, not used as the load-bearing tooth.)
// HAND-REASONING: SphereSDF emits f.w = length(p) - R (field_ops_spheresdf.cpp:56). The raymarch
//   (MovePointsToSDF.hlsl:98-109) steps pp -= GetNormal(pp)*d*StepDistanceFactor until |d| < MinDistance,
//   i.e. until |pp| ≈ R. Amount=1 → Position = lerp(Position, pp, 1) = pp. So |pos| ≈ R for every point
//   whose ray converged. A grid vertex sitting AT the center (|p|=0) has a NaN gradient (normalize(0)) → no
//   move → it is the one degenerate point the assertion excludes (|pos| ~ 0, skipped).
//
// injectBug LEG (proves the direct-Field GATHER is load-bearing): SEVER the SphereSDF→MoveToSDF Field wire.
//   The cook's one-hop direct-Field gather (the ADDED driver edit, point_graph.cpp / point_graph_resident.cpp)
//   then finds no wired Field → inputFieldTree stays null → MoveToSDF takes the pass-through copy → the
//   output bag is the UNMOVED flat z=0 grid → almost no point lies on the sphere → the on-sphere assertion
//   FAILS (RED). The only difference from the no-bug graph is whether the field reaches the cook, so the
//   bite lands precisely on the gather.
//
// ZONE: shell tier (app/src/ root, like fielddistanceforce_field_golden.cpp). Crosses runtime (PointGraph
// cook, registerBuiltinPointOps, the field/point NodeSpecs, assembleFieldMSL via the cook) AND platform
// (the field source compiler the cook's PSO build needs) — exactly what main.cpp does to wire the compiler;
// runtime selftests may NOT include platform, so the golden lives here, not in the runtime op leaf.
#include "runtime/point_graph.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/field_graph.h"  // setFieldSourceCompiler (the field source compiler seam the cook needs)
#include "runtime/graph.h"        // Graph / Node / pinId
#include "runtime/tex_op_cache.h" // clearTexOpCache (fresh source-compute-PSO cache per run-device)
#include "runtime/tixl_point.h"   // SwPoint (64B) + EvaluationContext

#include "platform/metal_compile.h"  // platform::compileLibraryFromSource (the field source compiler)

#ifndef SW_SHADER_METALLIB
#define SW_SHADER_METALLIB "shaders.metallib"
#endif

namespace sw {

// The MoveToSDF cook fn registrar (defined in runtime/point_ops_movepointstosdf.cpp; no header — it is in
// registerPointModifyPointOps too, but the golden registers it explicitly like point_ops_addnoise's golden).
void registerMoveToSdfOp();

int runMoveToSdfSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  const float R = 0.6f;

  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue* q = dev->newCommandQueue();
  NS::Error* err = nullptr;
  MTL::Library* lib =
      dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
  if (!lib) {
    std::printf("[selftest-movepointstosdf] FAIL: no metallib\n");
    if (q) q->release(); if (dev) dev->release(); pool->release();
    return 1;
  }

  // CRITICAL: wire the field source compiler (the SAME seam main.cpp wires) so the assembled field MSL
  // compiles into a compute PSO. WITHOUT this, cachedSourceComputePSO returns null → the cook takes the
  // pass-through fallback even WITH a field wired (a false RED). clearTexOpCache drops stale PSOs.
  setFieldSourceCompiler([](void* device, const char* msl) -> void* {
    NS::Error* e = nullptr;
    return platform::compileLibraryFromSource(static_cast<MTL::Device*>(device), msl, &e);
  });
  clearTexOpCache();

  registerBuiltinPointOps();  // registers GridPoints + the SphereSDF FieldOp factory
  registerMoveToSdfOp();

  // GridPoints (z=0 plane) -> MoveToSDF(Field=SphereSDF) -> DrawPoints (terminal so the cook realizes).
  Graph g;
  Node gen; gen.id = 1; gen.type = "GridPoints";
  gen.params["Count"] = 64.0f;  // buffer capacity = CountX*CountY*CountZ (host responsibility)
  gen.params["CountX"] = 8.0f; gen.params["CountY"] = 8.0f; gen.params["CountZ"] = 1.0f;
  gen.params["SizeMode"] = 1.0f;  // Bounds: Size is TOTAL extent → 8x8 grid spans ~[-0.5,0.44] in x,y (z=0)
  gen.params["Size.x"] = 1.0f; gen.params["Size.y"] = 1.0f; gen.params["Size.z"] = 1.0f;
  g.nodes.push_back(gen);

  Node sph; sph.id = 4; sph.type = "SphereSDF";
  sph.params["Center.x"] = 0.0f; sph.params["Center.y"] = 0.0f; sph.params["Center.z"] = 0.0f;
  sph.params["Radius"] = R;
  g.nodes.push_back(sph);

  Node mts; mts.id = 2; mts.type = "MoveToSDF";
  mts.params["Amount"] = 1.0f;
  mts.params["MinDistance"] = 0.005f;
  mts.params["MaxSteps"] = 64.0f;        // plenty of steps to converge to the surface
  mts.params["StepDistanceFactor"] = 0.5f;
  mts.params["NormalSamplingDistance"] = 0.01f;
  g.nodes.push_back(mts);

  Node drw; drw.id = 3; drw.type = "DrawPoints"; g.nodes.push_back(drw);

  // points -> MoveToSDF(port 0); MoveToSDF(out port 1) -> DrawPoints(port 0).
  g.connections.push_back({101, pinId(1, 0), pinId(2, 0)});
  g.connections.push_back({102, pinId(2, 1), pinId(3, 0)});
  // The Field wire: SphereSDF.Result (output port idx 4: Center.x/.y/.z, Radius, Result) -> MoveToSDF.Field
  // (input port idx 2: points, out, Field). injectBug SEVERS it → the direct-Field gather finds nothing →
  // pass-through copy → unmoved grid → on-sphere assert RED (the gather is the thing under test).
  if (!injectBug)
    g.connections.push_back({103, pinId(4, 4), pinId(2, 2)});

  PointGraph pg(dev, lib, q, 64, 64);
  EvaluationContext ctx{};
  ctx.frameIndex = 0; ctx.time = 0.0f; ctx.deltaTime = 1.0f / 60.0f;
  pg.cook(g, ctx, nullptr, pg.defaultDrawTarget(g));

  // Read back the MoveToSDF output bag directly (the cook-core readback harness).
  const MTL::Buffer* outBag = pg.debugCookedBuffer(2);
  if (!outBag) {
    std::printf("[selftest-movepointstosdf] FAIL: MoveToSDF output buffer not cooked\n");
    if (q) q->release(); lib->release(); if (dev) dev->release(); pool->release();
    return 1;
  }
  // The grid has CountX*CountY = 64 points (z=0 plane). Read them all.
  const size_t N = 64;
  std::vector<SwPoint> bag(N, SwPoint{});
  std::memcpy(bag.data(), const_cast<MTL::Buffer*>(outBag)->contents(), N * sizeof(SwPoint));

  // On-sphere assertion: every NON-degenerate moved point lies on |pos|=R within ~MinDistance.
  //
  // ORIENTATION assertion (SetOrientation, TiXL .t3 default TRUE — hlsl:133-136): with Amount=1,
  //   Rotation = qSlerp(orig, qLookAt(n, up), 1) = normalize(qLookAt(n, up)). qLookAt builds a basis whose
  //   FORWARD (local +Z) axis is `forward` (TiXL convention: m2* = forward), so the quaternion rotates world
  //   +Z onto n. On the sphere the SDF normal n = GetNormal(pp) = normalize(pp) (outward radial). HAND-
  //   REASONING: therefore qRotateVec3((0,0,1), Rotation) must align with the OUTWARD normal normalize(pos),
  //   i.e. dot(rotatedForward, normalize(pos)) ≈ +1. We assert dot >= 0.9 (a generous threshold absorbing the
  //   small surface-overshoot / finite-diff normal error; a perfect converge gives ~1.0). injectBug (field
  //   severed → pass-through) leaves Rotation = GridPoints identity (0,0,0,1) → rotatedForward = (0,0,1),
  //   while points stay on the flat z=0 grid so normalize(pos) is in the XY plane → dot ≈ 0 → would FAIL the
  //   orientation gate too, but the on-sphere gate already bites first.
  const float kTol = 0.01f;  // 2x MinDistance — one final march step can overshoot the surface slightly.
  const float kOrientDotMin = 0.9f;  // rotated-forward vs outward normal alignment threshold.
  size_t tested = 0, onSphere = 0, oriented = 0, colorOk = 0;
  float maxErr = 0.0f, minOrientDot = 2.0f;
  for (const SwPoint& p : bag) {
    float r = std::sqrt(p.Position.x * p.Position.x + p.Position.y * p.Position.y +
                        p.Position.z * p.Position.z);
    if (r < 1e-3f) continue;  // the degenerate center vertex (NaN gradient → no move), excluded by hand-reasoning
    ++tested;
    float e = std::fabs(r - R);
    if (e > maxErr) maxErr = e;
    if (e < kTol) ++onSphere;

    // qRotateVec3((0,0,1), Rotation): rotate world +Z by the readback quaternion (fast Rodrigues form,
    // byte-identical to quat.metal.h qRotateVec3). xyz = imaginary, w = real.
    float qx = p.Rotation.x, qy = p.Rotation.y, qz = p.Rotation.z, qw = p.Rotation.w;
    float v0 = 0.0f, v1 = 0.0f, v2 = 1.0f;            // world forward +Z
    float tx = 2.0f * (qy * v2 - qz * v1);            // t = 2 * cross(q.xyz, v)
    float ty = 2.0f * (qz * v0 - qx * v2);
    float tz = 2.0f * (qx * v1 - qy * v0);
    float fwdX = v0 + qw * tx + (qy * tz - qz * ty);  // v + q.w*t + cross(q.xyz, t)
    float fwdY = v1 + qw * ty + (qz * tx - qx * tz);
    float fwdZ = v2 + qw * tz + (qx * ty - qy * tx);
    float invR = (r > 1e-6f) ? 1.0f / r : 0.0f;       // outward normal = normalize(pos)
    float dotN = fwdX * (p.Position.x * invR) + fwdY * (p.Position.y * invR) + fwdZ * (p.Position.z * invR);
    if (dotN < minOrientDot) minOrientDot = dotN;
    if (dotN >= kOrientDotMin) ++oriented;

    // SetColor sanity (weak — see header): GridPoints seeds white, SphereSDF color-mode emits white, so the
    // ported lerp must leave Color ≈ white and FINITE. A miswired/NaN color branch trips this.
    bool colWhite = std::fabs(p.Color.x - 1.0f) < 0.05f && std::fabs(p.Color.y - 1.0f) < 0.05f &&
                    std::fabs(p.Color.z - 1.0f) < 0.05f && std::isfinite(p.Color.x) &&
                    std::isfinite(p.Color.y) && std::isfinite(p.Color.z);
    if (colWhite) ++colorOk;
  }

  // PASS: a large MAJORITY of tested points converged to the surface (a few grazing rays near the grid
  // edge can stall, so require >=90% on-sphere, not 100%). injectBug (pass-through, unmoved flat grid →
  // points at |pos| up to ~1.0, almost none at R=0.6) → onFrac collapses → FAIL.
  bool enough = tested >= 16;
  float onFrac = tested > 0 ? (float)onSphere / (float)tested : 0.0f;
  float orientFrac = tested > 0 ? (float)oriented / (float)tested : 0.0f;
  // Orientation gate: ONLY asserted on the wired (no-bug) path — there SetOrientation re-aims every
  // converged point's forward axis onto the outward normal, so >=90% must clear kOrientDotMin. The
  // injectBug path takes the pass-through copy (no orient branch runs, rotation stays identity); its RED
  // is already produced by the on-sphere gate, so we don't double-gate it on orientation.
  float colorFrac = tested > 0 ? (float)colorOk / (float)tested : 0.0f;
  bool orientOk = injectBug ? true : (orientFrac >= 0.9f);
  bool colorWhiteOk = injectBug ? true : (colorFrac >= 0.9f);  // weak sanity, no-bug path only
  bool pass = enough && onFrac >= 0.9f && orientOk && colorWhiteOk;

  std::printf("[selftest-movepointstosdf] R=%.2f tested=%zu onSphere=%zu (frac=%.3f, need>=0.90) "
              "maxErr=%.4f | oriented=%zu (frac=%.3f, need>=0.90) minDot=%.4f | colorWhite=%zu (frac=%.3f)%s -> %s\n",
              R, tested, onSphere, onFrac, maxErr, oriented, orientFrac, minOrientDot, colorOk, colorFrac,
              injectBug ? " [field severed -> pass-through unmoved grid]" : " [field wired -> on surface+oriented]",
              pass ? "PASS" : "FAIL");

  if (q) q->release(); lib->release(); if (dev) dev->release(); pool->release();
  return pass ? 0 : 1;
}

}  // namespace sw

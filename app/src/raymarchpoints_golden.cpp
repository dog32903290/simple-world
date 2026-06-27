// raymarchpoints_golden — --selftest-raymarchpoints. The SDF point-modify + count-multiply seam golden for
// the TWO-MODE op RaymarchPoints. HARNESS-FIRST CPU-readback through the REAL cook:
//
//   LinePoints (4 points near origin, identity rotation -> forward axis (0,0,-1))
//     -> RaymarchPoints(Field = SphereSDF Center=(0,0,+2) Radius=R, Mode, MaxReflectionCount=0)
//     -> readback the op's output bag via PointGraph::debugCookedBuffer(rmpNodeId).
//
// HAND-REASONING (geometry shared by both modes). LinePoints emits identity rotation, so each source point's
// forward axis n = qRotateVec3((0,0,-1), identity) = (0,0,-1). The march steps pp -= n*d*StepFactor = +Z*d,
// so the ray advances toward +Z. Put the sphere at +Z (Center=(0,0,+2)) so the -Z-facing ray pierces it.
// SphereSDF emits f.w = length(p - Center) - R. The raymarch (MovePointsForwardToSDF.hlsl:104-143) steps
// until |d| < MinDistance, i.e. until |pp - Center| ~= R: a surface hit.
//
// COUNT (count-multiply seam, mode-INDEPENDENT): PointCountPerLine = MaxSteps+1; PointCountPerLineReflections
//   = (MaxSteps+1)*(clamp(MaxReflectionCount,0,10)+1). With MaxSteps=80, MaxReflectionCount=0 ->
//   perLineRefl = 81*1 = 81; output count = 4*81 = 324 (NOT inherited 4). Static-stash countTransform sizes
//   the output buffer with a one-frame lag on a fresh build, so we cook TWICE and read the second cook.
//
// MODE 0 Raymarch (hlsl:98-154): the SOURCE point is kept at line[0]; the FIRST surface hit is written at
//   line index 1 (outBase + reflectionIndex(0) + 1). So OUTPUT[i*perLineRefl + 1] must lie ON the sphere:
//     abs(length(hit - Center) - R) < kTol.
// MODE 1 KeepSteps (hlsl:155-207): every march step is written at outBase + 0*perLine + stepIndex; on the
//   hit iteration the point is written (hlsl:183) THEN the loop breaks, so the LAST finite (non-NaN-Scale)
//   point in line segment 0 is the surface point. We scan the segment for the last finite point and assert
//   it is on the sphere. (NaN-Scale separators fill the tail; we skip them.)
//
// injectBug LEG (proves the direct-Field GATHER is load-bearing): SEVER the SphereSDF->op Field wire. The
// cook's one-hop direct-Field gather then finds no wired Field -> inputFieldTree null -> the op takes the
// pass-through copy -> no surface point is written -> the on-sphere assertion FAILS (RED) for the active mode.
//
// ZONE: shell tier (app/src/ root, like sdfreflectionlinepoints_golden.cpp). Crosses runtime (PointGraph cook,
// the op + field NodeSpecs, assembleFieldMSL via the cook) AND platform (the field source compiler the cook's
// PSO build needs) — exactly what main.cpp wires; runtime selftests may NOT include platform, so it lives here.
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
#include "runtime/tex_op_cache.h" // clearTexOpCache
#include "runtime/tixl_point.h"   // SwPoint (64B) + EvaluationContext

#include "platform/metal_compile.h"  // platform::compileLibraryFromSource (the field source compiler)

#ifndef SW_SHADER_METALLIB
#define SW_SHADER_METALLIB "shaders.metallib"
#endif

namespace sw {

void registerRaymarchPointsOp();

namespace {

// Cook a RaymarchPoints graph for a given Mode and return the readback bag (sized to EXPECT) + cookedCount.
// Returns false on any cook/buffer failure. injectBug severs the Field wire.
bool cookRaymarchMode(MTL::Device* dev, MTL::Library* lib, MTL::CommandQueue* q, int mode, bool injectBug,
                      uint32_t SRC, uint32_t PER_LINE_REFL, std::vector<SwPoint>& outBag,
                      uint32_t& cookedCount, float CZ, float R) {
  const uint32_t EXPECT = SRC * PER_LINE_REFL;

  Graph g;
  Node gen; gen.id = 1; gen.type = "LinePoints";
  gen.params["Count"] = (float)SRC;
  gen.params["Length"] = 0.3f;
  gen.params["Pivot"] = 0.5f;        // center the line on the origin
  gen.params["Center.x"] = 0.0f; gen.params["Center.y"] = 0.0f; gen.params["Center.z"] = 0.0f;
  gen.params["Direction.x"] = 1.0f; gen.params["Direction.y"] = 0.0f; gen.params["Direction.z"] = 0.0f;
  g.nodes.push_back(gen);

  Node sph; sph.id = 4; sph.type = "SphereSDF";
  sph.params["Center.x"] = 0.0f; sph.params["Center.y"] = 0.0f; sph.params["Center.z"] = CZ;
  sph.params["Radius"] = R;
  g.nodes.push_back(sph);

  Node rmp; rmp.id = 2; rmp.type = "RaymarchPoints";
  rmp.params["Mode"] = (float)mode;
  // MaxReflectionCount=2: Raymarch mode writes the first hit at line[1] (reflectionIndex 0) and the separator
  // fill starts at reflectionIndex>=2, so line[1] SURVIVES (with refl=1 the separator fill at index 1 would
  // overwrite it — faithful TiXL aliasing). KeepSteps marches regardless.
  rmp.params["MaxReflectionCount"] = 2.0f;
  rmp.params["MaxSteps"] = 80.0f;              // plenty of steps to converge
  rmp.params["MinDistance"] = 0.005f;
  rmp.params["StepDistanceFactor"] = 1.0f;
  rmp.params["NormalSamplingDistance"] = 0.01f;
  rmp.params["MaxDistance"] = 100.0f;
  g.nodes.push_back(rmp);

  Node drw; drw.id = 3; drw.type = "DrawPoints"; g.nodes.push_back(drw);

  g.connections.push_back({101, pinId(1, 0), pinId(2, 0)});   // points -> op(port 0)
  g.connections.push_back({102, pinId(2, 1), pinId(3, 0)});   // op(out port 1) -> DrawPoints
  if (!injectBug)
    g.connections.push_back({103, pinId(4, 4), pinId(2, 2)}); // SphereSDF.Result -> op.Field (severed by bug)

  PointGraph pg(dev, lib, q, 64, 256);
  EvaluationContext ctx{};
  ctx.frameIndex = 0; ctx.time = 0.0f; ctx.deltaTime = 1.0f / 60.0f;
  // Count-multiply static-stash sizing has a one-frame lag: cook TWICE so the buffer is correctly sized.
  pg.cook(g, ctx, nullptr, pg.defaultDrawTarget(g));
  pg.cook(g, ctx, nullptr, pg.defaultDrawTarget(g));

  const MTL::Buffer* bag = pg.debugCookedBuffer(2);
  if (!bag) return false;
  cookedCount = (uint32_t)(const_cast<MTL::Buffer*>(bag)->length() / sizeof(SwPoint));
  outBag.assign(EXPECT, SwPoint{});
  std::memcpy(outBag.data(), const_cast<MTL::Buffer*>(bag)->contents(), EXPECT * sizeof(SwPoint));
  return true;
}

}  // namespace

int runRaymarchPointsSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  const float R = 0.5f;
  const float CZ = 2.0f;               // sphere center z (along the +Z march direction)
  const uint32_t SRC = 4;              // source line points
  const int MAX_STEPS = 80;
  const int MAX_REFL = 2;
  const uint32_t PER_LINE_REFL = (uint32_t)(MAX_STEPS + 1) * (uint32_t)(MAX_REFL + 1);  // 81*3 = 243
  const uint32_t EXPECT = SRC * PER_LINE_REFL;                                          // 4 * 243 = 972
  const float kTol = 0.03f;            // ~6x MinDistance: a final march step can overshoot slightly.

  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue* q = dev->newCommandQueue();
  NS::Error* err = nullptr;
  MTL::Library* lib =
      dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
  if (!lib) {
    std::printf("[selftest-raymarchpoints] FAIL: no metallib\n");
    if (q) q->release(); if (dev) dev->release(); pool->release();
    return 1;
  }

  setFieldSourceCompiler([](void* device, const char* msl) -> void* {
    NS::Error* e = nullptr;
    return platform::compileLibraryFromSource(static_cast<MTL::Device*>(device), msl, &e);
  });
  clearTexOpCache();

  registerBuiltinPointOps();  // registers LinePoints + the SphereSDF FieldOp factory
  registerRaymarchPointsOp();

  bool allPass = true;

  // ===== MODE 0 Raymarch: line[1] of each source line is the first surface hit. =====
  {
    std::vector<SwPoint> bag; uint32_t cookedCount = 0;
    bool ok = cookRaymarchMode(dev, lib, q, /*mode=*/0, injectBug, SRC, PER_LINE_REFL, bag, cookedCount, CZ, R);
    bool countOk = ok && (cookedCount == EXPECT);
    size_t onSphere = 0, sourceKept = 0;
    float maxErr = 0.0f;
    if (ok) {
      for (uint32_t i = 0; i < SRC; ++i) {
        const SwPoint& h = bag[i * PER_LINE_REFL + 1u];  // line[1] = first surface hit (survives with MaxRefl>=2)
        float dx = h.Position.x, dy = h.Position.y, dz = h.Position.z - CZ;
        float dist = std::sqrt(dx * dx + dy * dy + dz * dz);
        float e = std::fabs(dist - R);
        if (e > maxErr) maxErr = e;
        if (e < kTol) ++onSphere;
        const SwPoint& s = bag[i * PER_LINE_REFL + 0u];  // line[0] = kept source point
        if (std::isfinite(s.Position.x) && std::isfinite(s.Position.z) &&
            std::fabs(s.Position.z) < 0.01f && std::fabs(s.Position.x) <= 0.2f)
          ++sourceKept;
      }
    }
    bool onSphereOk = ok && (onSphere == SRC);
    bool sourceKeptOk = injectBug ? true : (ok && sourceKept == SRC);
    bool pass = countOk && onSphereOk && sourceKeptOk;
    allPass = allPass && pass;
    std::printf("[selftest-raymarchpoints] MODE0 Raymarch  cookedCount=%u (need %u) | onSphere=%zu/%u "
                "(maxErr=%.4f tol=%.3f) | sourceKept=%zu/%u%s -> %s\n",
                cookedCount, EXPECT, onSphere, SRC, maxErr, kTol, sourceKept, SRC,
                injectBug ? " [field severed -> pass-through, no hit at line[1]]" : "",
                pass ? "PASS" : "FAIL");
  }

  // ===== MODE 1 KeepSteps: the march PATH is traced — every step becomes an output point. =====
  // HAND-REASONING + FAITHFUL ARTIFACT: in KeepSteps the hit iteration writes the step (hlsl:183) THEN breaks,
  // and the separator-fill loop (hlsl:201) restarts at the SAME stepIndex -> it OVERWRITES the surface step
  // with a NaN-Scale separator. So the SURVIVING finite points are the PRE-hit march steps, advancing along
  // +Z from the source toward the sphere. A ray that converges in one step (x=±0.05) leaves only the source;
  // a ray that needs >1 step (x=±0.15) leaves intermediate points whose deepest reaches the surface band
  // (z ~ CZ-R). So we assert the DEEPEST finite +Z point over the bag reaches the first-touch surface plane
  // z = CZ-R (the longest-marching ray gets there), and bites when the field is severed (pass-through -> NO
  // point advances past the source plane z~0). This tests the path-trace WITHOUT over-claiming the overwrite-
  // erased exact surface landing.
  {
    std::vector<SwPoint> bag; uint32_t cookedCount = 0;
    bool ok = cookRaymarchMode(dev, lib, q, /*mode=*/1, injectBug, SRC, PER_LINE_REFL, bag, cookedCount, CZ, R);
    bool countOk = ok && (cookedCount == EXPECT);
    float maxFiniteZ = -1e9f;   // deepest +Z finite point over the whole bag
    if (ok) {
      for (uint32_t k = 0; k < EXPECT; ++k) {
        const SwPoint& pt = bag[k];
        if (std::isfinite(pt.Scale.x) && std::isfinite(pt.Position.z) && pt.Position.z > maxFiniteZ)
          maxFiniteZ = pt.Position.z;
      }
    }
    // The +Z ray first touches the sphere at its near pole z = CZ - R (x=0 line) / slightly nearer for off-axis
    // lines; the deepest pre-hit march step the longest ray leaves sits in [CZ-R-2*kTol, CZ-R+kTol].
    const float surfaceZ = CZ - R;  // 1.5
    bool advancedToSurface = ok && std::fabs(maxFiniteZ - surfaceZ) < 3.0f * kTol;  // marched up to the surface band
    bool noMarchOnSever    = injectBug ? (ok && maxFiniteZ < 0.5f) : true;          // severed -> stays at source
    bool pass = countOk && (injectBug ? noMarchOnSever : advancedToSurface);
    allPass = allPass && pass;
    std::printf("[selftest-raymarchpoints] MODE1 KeepSteps cookedCount=%u (need %u) | deepestFiniteZ=%.4f "
                "(surfaceZ=%.3f tol=%.3f)%s -> %s\n",
                cookedCount, EXPECT, maxFiniteZ, surfaceZ, 3.0f * kTol,
                injectBug ? " [field severed -> pass-through, no march -> deepestZ~source]" : "",
                pass ? "PASS" : "FAIL");
  }

  if (q) q->release(); lib->release(); if (dev) dev->release(); pool->release();
  return allPass ? 0 : 1;
}

}  // namespace sw

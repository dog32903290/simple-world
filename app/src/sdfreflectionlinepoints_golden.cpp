// sdfreflectionlinepoints_golden — --selftest-sdfreflectionlinepoints. The SDF point-modify + count-multiply
// seam golden (op SdfReflectionLinePoints). HARNESS-FIRST CPU-readback through the REAL cook:
//
//   LinePoints (4 points near origin, identity rotation -> forward axis (0,0,-1))
//     -> SdfReflectionLinePoints(Field = SphereSDF Center=(0,0,-2) Radius=R, MaxReflectionCount=2)
//     -> readback the op's output bag via PointGraph::debugCookedBuffer(reflNodeId).
//
// HAND-REASONING. LinePoints emits identity rotation (linepoints.metal:78), so each source point's forward
// axis n = qRotateVec3((0,0,-1), identity) = (0,0,-1): the ray marches along -Z. SphereSDF emits
// f.w = length(p - Center) - R. With Center=(0,0,-2) and a source point at (x,0,0) (|x| < R so the -Z ray
// pierces the sphere), the raymarch (SdfReflectionLinePoints.hlsl:117-153) steps pp -= n*d*StepDistanceFactor
// until |d| < MinDistance, i.e. until |pp - Center| ~= R. On that hit the kernel WRITES the surface point at
// ResultPoints[outIndex] (hlsl:142) — which, with the source point kept at line index 0, is the line's
// index 1. So for each source line, OUTPUT[i*pointsPerLine + 1] must lie ON the sphere:
//   abs(length(hit - Center) - R) < ~kTol.   (pointsPerLine = clamp(MaxReflectionCount,0,10)+3 = 5.)
// This is the load-bearing proof that BOTH seams fire: the direct-Field gather reached the cook (the ray
// has an SDF to hit) AND the count-multiply layout placed the source at line[0] and the first hit at line[1].
//
// COUNT assertion (count-multiply seam): output count == sourceCount * pointsPerLine == 4 * 5 == 20 (NOT
// inherited 4). The static-stash countTransform sizes the output buffer; like SubdivideLinePoints this has a
// one-frame sizing lag on a fresh build, so we cook TWICE and read the second cook's buffer.
//
// injectBug LEG (proves the direct-Field GATHER is load-bearing): SEVER the SphereSDF->op Field wire. The
// cook's one-hop direct-Field gather then finds no wired Field -> inputFieldTree null -> the op takes the
// pass-through copy -> no surface hit is written at line index 1 (the slot is a copied source point or a
// zeroed trailing slot, neither on the sphere) -> the on-sphere assertion FAILS (RED). The only difference
// from the no-bug graph is whether the field reaches the cook, so the bite lands on the gather.
//
// ZONE: shell tier (app/src/ root, like movepointstosdf_golden.cpp). Crosses runtime (PointGraph cook, the
// op + field NodeSpecs, assembleFieldMSL via the cook) AND platform (the field source compiler the cook's
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

void registerSdfReflectionLinePointsOp();

int runSdfReflectionLinePointsSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  const float R = 0.5f;
  // March direction = -n where n = qRotateVec3((0,0,-1), identity) = (0,0,-1), so the ray advances toward
  // +Z (pp -= n*d steps +Z when d>0). Put the sphere at +Z so the ray pierces it.
  const float CZ = 2.0f;              // sphere center z (along the +Z march direction)
  const uint32_t SRC = 4;             // source line points
  const int MAX_REFL = 2;             // pointsPerLine = clamp(2,0,10)+3 = 5
  const uint32_t PER_LINE = (uint32_t)MAX_REFL + 3u;
  const uint32_t EXPECT = SRC * PER_LINE;  // 4 * 5 = 20

  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue* q = dev->newCommandQueue();
  NS::Error* err = nullptr;
  MTL::Library* lib =
      dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
  if (!lib) {
    std::printf("[selftest-sdfreflectionlinepoints] FAIL: no metallib\n");
    if (q) q->release(); if (dev) dev->release(); pool->release();
    return 1;
  }

  // Wire the field source compiler (the SAME seam main.cpp wires) so the assembled field MSL compiles into
  // a compute PSO. WITHOUT this, cachedSourceComputePSO returns null -> pass-through even with a field wired.
  setFieldSourceCompiler([](void* device, const char* msl) -> void* {
    NS::Error* e = nullptr;
    return platform::compileLibraryFromSource(static_cast<MTL::Device*>(device), msl, &e);
  });
  clearTexOpCache();

  registerBuiltinPointOps();  // registers LinePoints + the SphereSDF FieldOp factory
  registerSdfReflectionLinePointsOp();

  // LinePoints (4 pts near origin, Direction=+X, Length 0.3, Pivot 0.5 -> x in [-0.15,0.15], all |x|<R)
  //   -> SdfReflectionLinePoints(Field=SphereSDF) -> DrawPoints (terminal so the cook realizes).
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

  Node srl; srl.id = 2; srl.type = "SdfReflectionLinePoints";
  srl.params["MaxReflectionCount"] = (float)MAX_REFL;
  srl.params["MaxSteps"] = 80.0f;            // plenty of steps to converge
  srl.params["MinDistance"] = 0.005f;
  srl.params["StepDistanceFactor"] = 1.0f;
  srl.params["NormalSamplingDistance"] = 0.01f;
  srl.params["MaxDistance"] = 100.0f;
  g.nodes.push_back(srl);

  Node drw; drw.id = 3; drw.type = "DrawPoints"; g.nodes.push_back(drw);

  // points -> op(port 0); op(out port 1) -> DrawPoints(port 0).
  g.connections.push_back({101, pinId(1, 0), pinId(2, 0)});
  g.connections.push_back({102, pinId(2, 1), pinId(3, 0)});
  // The Field wire: SphereSDF.Result (output port idx 4) -> op.Field (input port idx 2). injectBug SEVERS it.
  if (!injectBug)
    g.connections.push_back({103, pinId(4, 4), pinId(2, 2)});

  PointGraph pg(dev, lib, q, 64, 256);
  EvaluationContext ctx{};
  ctx.frameIndex = 0; ctx.time = 0.0f; ctx.deltaTime = 1.0f / 60.0f;
  // Count-multiply static-stash sizing has a one-frame lag: cook TWICE so the buffer is correctly sized.
  pg.cook(g, ctx, nullptr, pg.defaultDrawTarget(g));
  pg.cook(g, ctx, nullptr, pg.defaultDrawTarget(g));

  const MTL::Buffer* outBag = pg.debugCookedBuffer(2);
  if (!outBag) {
    std::printf("[selftest-sdfreflectionlinepoints] FAIL: op output buffer not cooked\n");
    if (q) q->release(); lib->release(); if (dev) dev->release(); pool->release();
    return 1;
  }
  std::vector<SwPoint> bag(EXPECT, SwPoint{});
  std::memcpy(bag.data(), const_cast<MTL::Buffer*>(outBag)->contents(), EXPECT * sizeof(SwPoint));

  // COUNT assertion (count-multiply seam): the cooked output bag is sized sourceCount*pointsPerLine.
  // ensureOut allocates exactly `count` SwPoints, so the buffer's byte length / 64 == EXPECT (20, NOT 4).
  uint32_t cookedCount = (uint32_t)(const_cast<MTL::Buffer*>(outBag)->length() / sizeof(SwPoint));
  bool countOk = (cookedCount == EXPECT);

  // ON-SPHERE assertion: line index 1 of each source line is the first surface hit (|hit-Center| ~= R).
  // injectBug -> pass-through (no hit written at line[1]) -> fails.
  const float kTol = 0.02f;  // ~4x MinDistance: one final march step can overshoot the surface slightly.
  size_t tested = 0, onSphere = 0;
  float maxErr = 0.0f;
  for (uint32_t i = 0; i < SRC; ++i) {
    uint32_t idx = i * PER_LINE + 1u;  // line[1] = the first surface hit
    if (idx >= bag.size()) break;
    const SwPoint& h = bag[idx];
    float dx = h.Position.x - 0.0f, dy = h.Position.y - 0.0f, dz = h.Position.z - CZ;
    float dist = std::sqrt(dx * dx + dy * dy + dz * dz);
    ++tested;
    float e = std::fabs(dist - R);
    if (e > maxErr) maxErr = e;
    if (e < kTol) ++onSphere;
  }

  // Also verify the line[0] entries are the kept SOURCE points (near the origin plane, finite, non-NaN).
  // On the no-bug path this confirms the per-line layout (source at line[0], hit at line[1]).
  size_t sourceKept = 0;
  for (uint32_t i = 0; i < SRC; ++i) {
    const SwPoint& s = bag[i * PER_LINE + 0u];
    if (std::isfinite(s.Position.x) && std::isfinite(s.Position.z) &&
        std::fabs(s.Position.z) < 0.01f && std::fabs(s.Position.x) <= 0.2f)
      ++sourceKept;
  }

  bool onSphereOk = (tested == SRC) && (onSphere == SRC);
  bool sourceKeptOk = injectBug ? true : (sourceKept == SRC);  // only assert layout on the wired path
  bool pass = countOk && onSphereOk && sourceKeptOk;

  std::printf("[selftest-sdfreflectionlinepoints] R=%.2f perLine=%u cookedCount=%u (need %u) | onSphere=%zu/%zu "
              "(maxErr=%.4f, tol=%.3f) | sourceKept=%zu/%u%s -> %s\n",
              R, PER_LINE, cookedCount, EXPECT, onSphere, tested, maxErr, kTol, sourceKept, SRC,
              injectBug ? " [field severed -> pass-through, no surface hit at line[1]]"
                        : " [field wired -> reflection line, hit on surface]",
              pass ? "PASS" : "FAIL");

  if (q) q->release(); lib->release(); if (dev) dev->release(); pool->release();
  return pass ? 0 : 1;
}

}  // namespace sw

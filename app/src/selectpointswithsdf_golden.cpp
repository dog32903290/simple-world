// selectpointswithsdf_golden — --selftest-selectpointswithsdf. Direct-Field gather LEAF golden
// (SelectPointsWithSDF, cloned from movepointstosdf_golden.cpp). HARNESS-FIRST CPU-readback through the
// REAL cook:
//
//   GridPoints (z=0 plane) -> SelectPointsWithSDF(Field = SphereSDF Center=0 Radius=R, all .t3 defaults)
//     -> readback the output bag; assert each FX1 == the hand-computed selection scalar.
//
// HAND-COMPUTATION at the .t3 defaults (Strength=1, StrengthFactor=None, WriteTo=F1, Mode=Override,
//   Mapping=Centered, Range=1, Offset(Center)=0, GainAndBias=(0.5,0.5), Scatter=0, ClampNegative=true,
//   DiscardNonSelected=false). Walk SelectPointsWithField.hlsl with these constants:
//   - Scatter=0  -> f0 = GetDistance(pos) = length(pos) - R  (SphereSDF distance branch, w=0).
//   - Mapping=Centered (hlsl:111-113), Range=1, Center=0:  f = (f0 + 0.5)/1 - 0 = f0 + 0.5.
//   - GainAndBias=(0.5,0.5) is the IDENTITY of ApplyGainAndBias for 0<value<1 (GetBias(0.5,x)=x and
//     GetSchlickBias(0.5,x)=x; verified by hand), so f = 1 - (f0 + 0.5) = 0.5 - f0.
//   - WriteTo=F1 -> org = p.FX1 (=0 from GridPoints). Mode=Override -> f unchanged.
//   - DiscardNonSelected=false -> NO NaN discard (count preserved). strength = Strength*1 = 1.
//   - result = lerp(org, f*abs(strength+1), strength) = lerp(0, f*2, 1) = 2f = 2(0.5 - f0) = 1 - 2*f0.
//   - ClampNegative=true -> result = max(0, 1 - 2*f0). WriteTo=F1 -> p.FX1 = result.
//   ===> FX1 == max(0, 1 - 2*(length(pos) - R)).  ON the surface (f0=0) FX1=1; at f0=0.5 FX1=0; outside
//        (f0>=0.5) FX1=0; inside (f0<0) FX1>1. This is the precise per-point anchor.
//
// injectBug LEG (proves the direct-Field GATHER is load-bearing): SEVER the SphereSDF->SelectPointsWithSDF
//   Field wire. The cook's one-hop direct-Field gather then finds no wired Field -> inputFieldTree null ->
//   the cook takes the pass-through copy -> FX1 stays the GridPoints seed (0) for EVERY point -> the
//   per-point FX1 anchor (which is >0 for many points near/inside the sphere) FAILS (RED). The only
//   difference is whether the field reaches the cook, so the bite lands precisely on the gather.
//
// ZONE: shell tier (app/src/ root, like movepointstosdf_golden.cpp). Crosses runtime AND platform (the
// field source compiler the cook's PSO build needs).
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

void registerSelectPointsWithSdfOp();  // runtime/point_ops_selectpointswithsdf.cpp (no header)

int runSelectPointsWithSdfSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  const float R = 0.6f;

  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue* q = dev->newCommandQueue();
  NS::Error* err = nullptr;
  MTL::Library* lib =
      dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
  if (!lib) {
    std::printf("[selftest-selectpointswithsdf] FAIL: no metallib\n");
    if (q) q->release(); if (dev) dev->release(); pool->release();
    return 1;
  }

  setFieldSourceCompiler([](void* device, const char* msl) -> void* {
    NS::Error* e = nullptr;
    return platform::compileLibraryFromSource(static_cast<MTL::Device*>(device), msl, &e);
  });
  clearTexOpCache();

  registerBuiltinPointOps();  // registers GridPoints + the SphereSDF FieldOp factory
  registerSelectPointsWithSdfOp();

  // GridPoints (z=0 plane) -> SelectPointsWithSDF(Field=SphereSDF) -> DrawPoints (terminal).
  Graph g;
  Node gen; gen.id = 1; gen.type = "GridPoints";
  gen.params["Count"] = 64.0f;
  gen.params["CountX"] = 8.0f; gen.params["CountY"] = 8.0f; gen.params["CountZ"] = 1.0f;
  gen.params["SizeMode"] = 1.0f;  // Bounds: Size is TOTAL extent → grid spans ~[-0.5,0.44] in x,y (z=0)
  gen.params["Scale"] = 1.0f;  // unit multiplier: Size below is the literal grid extent (param-completion fan-out added the Scale knob, .t3 default 0.1)
  gen.params["Size.x"] = 2.0f; gen.params["Size.y"] = 2.0f; gen.params["Size.z"] = 1.0f;
  g.nodes.push_back(gen);

  Node sph; sph.id = 4; sph.type = "SphereSDF";
  sph.params["Center.x"] = 0.0f; sph.params["Center.y"] = 0.0f; sph.params["Center.z"] = 0.0f;
  sph.params["Radius"] = R;
  g.nodes.push_back(sph);

  Node sel; sel.id = 2; sel.type = "SelectPointsWithSDF";
  // All .t3 defaults explicit (so the hand-computation holds even if a default ever drifts).
  sel.params["Strength"] = 1.0f;
  sel.params["StrengthFactor"] = 0.0f;
  sel.params["WriteTo"] = 1.0f;
  sel.params["Mode"] = 0.0f;
  sel.params["Mapping"] = 0.0f;
  sel.params["Range"] = 1.0f;
  sel.params["Offset"] = 0.0f;
  sel.params["GainAndBias.x"] = 0.5f; sel.params["GainAndBias.y"] = 0.5f;
  sel.params["Scatter"] = 0.0f;
  sel.params["ClampNegative"] = 1.0f;
  sel.params["DiscardNonSelected"] = 0.0f;
  g.nodes.push_back(sel);

  Node drw; drw.id = 3; drw.type = "DrawPoints"; g.nodes.push_back(drw);

  // GridPoints(out 1) -> SelectPointsWithSDF(in 0); SelectPointsWithSDF(out 1) -> DrawPoints(in 0).
  g.connections.push_back({101, pinId(1, 1), pinId(2, 0)});
  g.connections.push_back({102, pinId(2, 1), pinId(3, 0)});
  // The Field wire: SphereSDF.Result (output port idx 4) -> SelectPointsWithSDF.Field (input port idx 2).
  if (!injectBug)
    g.connections.push_back({103, pinId(4, 4), pinId(2, 2)});

  PointGraph pg(dev, lib, q, 64, 64);
  EvaluationContext ctx{};
  ctx.frameIndex = 0; ctx.time = 0.0f; ctx.deltaTime = 1.0f / 60.0f;
  pg.cook(g, ctx, nullptr, pg.defaultDrawTarget(g));

  const MTL::Buffer* outBag = pg.debugCookedBuffer(2);
  if (!outBag) {
    std::printf("[selftest-selectpointswithsdf] FAIL: SelectPointsWithSDF output buffer not cooked\n");
    if (q) q->release(); lib->release(); if (dev) dev->release(); pool->release();
    return 1;
  }
  const size_t N = 64;
  std::vector<SwPoint> bag(N, SwPoint{});
  std::memcpy(bag.data(), const_cast<MTL::Buffer*>(outBag)->contents(), N * sizeof(SwPoint));

  // Per-point anchor: FX1 == max(0, 1 - 2*(length(pos) - R)). The injectBug path leaves FX1 == 0 for every
  // point (pass-through of the GridPoints seed). We also track how many points have a NON-zero expected
  // FX1 (near/inside the sphere) — those are exactly the points whose RED proves the gather.
  const float kTol = 0.01f;
  size_t tested = 0, matched = 0, nonzeroExpected = 0;
  float maxErr = 0.0f;
  for (const SwPoint& p : bag) {
    float r = std::sqrt(p.Position.x * p.Position.x + p.Position.y * p.Position.y +
                        p.Position.z * p.Position.z);
    float f0 = r - R;
    float expected = std::fmax(0.0f, 1.0f - 2.0f * f0);
    if (expected > 0.01f) ++nonzeroExpected;
    if (!std::isfinite(p.FX1)) continue;
    ++tested;
    float e = std::fabs(p.FX1 - expected);
    if (e > maxErr) maxErr = e;
    if (e < kTol) ++matched;
  }

  bool enough = tested >= 32;
  // The grid spans ~[-1,1] in x,y so MANY points fall inside/near the R=0.6 sphere -> nonzeroExpected high.
  bool haveSignal = nonzeroExpected >= 8;
  float matchFrac = tested > 0 ? (float)matched / (float)tested : 0.0f;
  // No-bug: every FX1 matches the anchor (>=99%). injectBug: FX1==0 everywhere, so the points with a
  // nonzero expected value MISMATCH -> matchFrac drops below 1 -> FAIL. haveSignal guards a degenerate grid.
  bool pass = enough && haveSignal && matchFrac >= 0.99f;

  std::printf("[selftest-selectpointswithsdf] R=%.2f tested=%zu matched=%zu (frac=%.3f, maxErr=%.4f) "
              "nonzeroExpected=%zu%s -> %s\n",
              R, tested, matched, matchFrac, maxErr, nonzeroExpected,
              injectBug ? " [field severed -> pass-through FX1=0]" : " [field wired -> FX1=max(0,1-2*dist)]",
              pass ? "PASS" : "FAIL");

  if (q) q->release(); lib->release(); if (dev) dev->release(); pool->release();
  return pass ? 0 : 1;
}

}  // namespace sw

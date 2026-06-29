// pointcolorwithfield_golden — --selftest-pointcolorwithfield. Direct-Field gather LEAF golden
// (PointColorWithField, cloned from movepointstosdf_golden.cpp). HARNESS-FIRST CPU-readback through the
// REAL cook:
//
//   GridPoints -> SetPointAttributes(SetColor=1, Color=(0.2,0.4,0.8,1))
//     -> PointColorWithField(Field = SphereSDF Center=0 Radius=R, Strength=1) -> readback the output bag.
//
// HAND-COMPUTATION. SphereSDF emits, in the COLOR branch (p.w >= 0.5):
//   f.xyz = (p.w < 0.5 ? p.xyz : float3(1.0))  (field_ops_spheresdf.cpp:58)  -> float3(1.0) at w=1,
//   f.w   = length(p.xyz - Center) - Radius     (.cpp:56), and the all-ones seed leaves f.w finite.
// ColorPointsWithField.hlsl:75-76:  field = GetField(float4(pos,1));  p.Color = lerp(p.Color, field, strength).
// With Strength=1, StrengthFactor=None -> strength=1 -> p.Color = field = (1,1,1, f.w). The .xyz are the
// LOAD-BEARING tooth: every point's Color.rgb must become white(1,1,1), OVERWRITING the seeded (0.2,0.4,0.8).
//
// injectBug LEG (proves the direct-Field GATHER is load-bearing): SEVER the SphereSDF->PointColorWithField
//   Field wire. The cook's one-hop direct-Field gather then finds no wired Field -> inputFieldTree stays
//   null -> PointColorWithField takes the pass-through copy -> Color stays the SEEDED (0.2,0.4,0.8) ->
//   the "became white" assertion FAILS (RED). The only difference is whether the field reaches the cook, so
//   the bite lands precisely on the gather.
//
// ZONE: shell tier (app/src/ root, like movepointstosdf_golden.cpp). Crosses runtime (PointGraph cook, the
// field/point NodeSpecs, assembleFieldMSL via the cook) AND platform (the field source compiler the cook's
// PSO build needs); runtime selftests may NOT include platform, so the golden lives here.
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

void registerPointColorWithFieldOp();  // runtime/point_ops_pointcolorwithfield.cpp (no header)

int runPointColorWithFieldSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  const float R = 0.6f;
  // Seeded (non-white) input Color so the lerp-toward-white is OBSERVABLE.
  const float SR = 0.2f, SG = 0.4f, SB = 0.8f;

  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue* q = dev->newCommandQueue();
  NS::Error* err = nullptr;
  MTL::Library* lib =
      dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
  if (!lib) {
    std::printf("[selftest-pointcolorwithfield] FAIL: no metallib\n");
    if (q) q->release(); if (dev) dev->release(); pool->release();
    return 1;
  }

  // Wire the field source compiler (same seam main.cpp wires) so the assembled field MSL compiles into a
  // compute PSO. WITHOUT this, cachedSourceComputePSO returns null → the cook takes the pass-through
  // fallback even WITH a field wired (a false RED). clearTexOpCache drops stale PSOs.
  setFieldSourceCompiler([](void* device, const char* msl) -> void* {
    NS::Error* e = nullptr;
    return platform::compileLibraryFromSource(static_cast<MTL::Device*>(device), msl, &e);
  });
  clearTexOpCache();

  registerBuiltinPointOps();  // registers GridPoints + SetPointAttributes + the SphereSDF FieldOp factory
  registerPointColorWithFieldOp();

  // GridPoints -> SetPointAttributes(Color seed) -> PointColorWithField(Field=SphereSDF) -> DrawPoints.
  Graph g;
  Node gen; gen.id = 1; gen.type = "GridPoints";
  gen.params["Count"] = 64.0f;
  gen.params["CountX"] = 8.0f; gen.params["CountY"] = 8.0f; gen.params["CountZ"] = 1.0f;
  gen.params["SizeMode"] = 1.0f;
  gen.params["Scale"] = 1.0f;  // unit multiplier: Size below is the literal grid extent (param-completion fan-out added the Scale knob, .t3 default 0.1)
  gen.params["Size.x"] = 1.0f; gen.params["Size.y"] = 1.0f; gen.params["Size.z"] = 1.0f;
  g.nodes.push_back(gen);

  // Seed a known non-white Color on every point (SetColor=1 → absolute set).
  Node setc; setc.id = 5; setc.type = "SetPointAttributes";
  setc.params["Amount"] = 1.0f;
  setc.params["SetColor"] = 1.0f;
  setc.params["Color.x"] = SR; setc.params["Color.y"] = SG; setc.params["Color.z"] = SB;
  setc.params["Color.w"] = 1.0f;
  g.nodes.push_back(setc);

  Node sph; sph.id = 4; sph.type = "SphereSDF";
  sph.params["Center.x"] = 0.0f; sph.params["Center.y"] = 0.0f; sph.params["Center.z"] = 0.0f;
  sph.params["Radius"] = R;
  g.nodes.push_back(sph);

  Node pcwf; pcwf.id = 2; pcwf.type = "PointColorWithField";
  pcwf.params["Strength"] = 1.0f;
  pcwf.params["StrengthFactor"] = 0.0f;  // None → ×1
  g.nodes.push_back(pcwf);

  Node drw; drw.id = 3; drw.type = "DrawPoints"; g.nodes.push_back(drw);

  // GridPoints(out 1) -> SetPointAttributes(in 0); SetPointAttributes(out 1) -> PointColorWithField(in 0);
  // PointColorWithField(out 1) -> DrawPoints(in 0).
  g.connections.push_back({101, pinId(1, 1), pinId(5, 0)});
  g.connections.push_back({102, pinId(5, 1), pinId(2, 0)});
  g.connections.push_back({103, pinId(2, 1), pinId(3, 0)});
  // The Field wire: SphereSDF.Result (output port idx 4) -> PointColorWithField.Field (input port idx 2).
  // injectBug SEVERS it → the direct-Field gather finds nothing → pass-through copy → Color stays seeded.
  if (!injectBug)
    g.connections.push_back({104, pinId(4, 4), pinId(2, 2)});

  PointGraph pg(dev, lib, q, 64, 64);
  EvaluationContext ctx{};
  ctx.frameIndex = 0; ctx.time = 0.0f; ctx.deltaTime = 1.0f / 60.0f;
  pg.cook(g, ctx, nullptr, pg.defaultDrawTarget(g));

  const MTL::Buffer* outBag = pg.debugCookedBuffer(2);
  if (!outBag) {
    std::printf("[selftest-pointcolorwithfield] FAIL: PointColorWithField output buffer not cooked\n");
    if (q) q->release(); lib->release(); if (dev) dev->release(); pool->release();
    return 1;
  }
  const size_t N = 64;
  std::vector<SwPoint> bag(N, SwPoint{});
  std::memcpy(bag.data(), const_cast<MTL::Buffer*>(outBag)->contents(), N * sizeof(SwPoint));

  // No-bug: every point's Color.rgb == white(1,1,1) (the field color overwrote the seed at strength=1).
  // injectBug: every point's Color.rgb == the SEEDED (SR,SG,SB) (pass-through left it untouched).
  size_t tested = 0, becameWhite = 0, stayedSeed = 0;
  float maxWhiteErr = 0.0f, maxSeedErr = 0.0f;
  for (const SwPoint& p : bag) {
    if (!std::isfinite(p.Color.x) || !std::isfinite(p.Color.y) || !std::isfinite(p.Color.z)) continue;
    ++tested;
    float we = std::fmax(std::fmax(std::fabs(p.Color.x - 1.0f), std::fabs(p.Color.y - 1.0f)),
                         std::fabs(p.Color.z - 1.0f));
    if (we > maxWhiteErr) maxWhiteErr = we;
    if (we < 0.02f) ++becameWhite;
    float se = std::fmax(std::fmax(std::fabs(p.Color.x - SR), std::fabs(p.Color.y - SG)),
                         std::fabs(p.Color.z - SB));
    if (se > maxSeedErr) maxSeedErr = se;
    if (se < 0.02f) ++stayedSeed;
  }

  bool enough = tested >= 32;
  float whiteFrac = tested > 0 ? (float)becameWhite / (float)tested : 0.0f;
  float seedFrac  = tested > 0 ? (float)stayedSeed  / (float)tested : 0.0f;
  // SINGLE invariant (so the -bug leg BITES, per the --bite harness): with the field wired the cook recolors
  // EVERY point to white. injectBug severs the Field wire → the pass-through leaves the SEEDED color → the
  // "became white" gate FAILS (RED). We assert it UNCONDITIONALLY (no injectBug branch) so the tooth bites.
  bool pass = enough && whiteFrac >= 0.99f;

  std::printf("[selftest-pointcolorwithfield] R=%.2f tested=%zu becameWhite=%zu (frac=%.3f, maxErr=%.4f) "
              "stayedSeed=%zu (frac=%.3f, maxErr=%.4f)%s -> %s\n",
              R, tested, becameWhite, whiteFrac, maxWhiteErr, stayedSeed, seedFrac, maxSeedErr,
              injectBug ? " [field severed -> pass-through seed color]" : " [field wired -> recolored white]",
              pass ? "PASS" : "FAIL");

  if (q) q->release(); lib->release(); if (dev) dev->release(); pool->release();
  return pass ? 0 : 1;
}

}  // namespace sw

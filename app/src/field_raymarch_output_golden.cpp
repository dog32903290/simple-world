// field_raymarch_output_golden — --selftest-raymarchfield-output. The PRODUCTION-PATH golden that proves
// "the UI actually draws a raymarched field": a SphereSDF → RaymarchField terminal is cooked through the
// CANONICAL resident cook (libFromGraph → buildEvalGraph → pg.cookResident), then pg.target() (== the
// displayTex the OutputWindow / eye / present path show) is read back and the raymarch SILHOUETTE is
// pinned (center-screen glow differs from the corner glow by a clear margin) plus 4-FOLD SYMMETRY.
//
// WHY THIS LEG (vs field_raymarch_golden, which calls renderField3d DIRECTLY): this one walks the FULL
// production cook — the Field-input gather (graph "Field" wire → FieldNode tree → tc.inputFieldTree), the
// tex terminal dispatch (cookResidentTexNode → displayTex), and the RGBA32Float→RGBA8 tonemap copy. It
// proves the SDF island is wired to the OUTPUT, not just that the render kernel works in isolation. The
// silhouette assertion reads the SAME RGBA8 texture target() returns (pg.target() == displayTex).
//
// injectBug = raymarchFieldInjectBug() true → the RaymarchField cook short-circuits to a black clear (no
// raymarch) → the silhouette vanishes → center≈corner → the margin tooth goes RED. Teeth on the real
// cook path, not by inverting a faithful pass.
//
// CAMERA: the cook uses the DEFAULT camera at the output aspect (no Camera connection, v1) — the SAME
// parity target field_raymarch_golden pins. A 256² square output → aspect 1 → center ray straight at the
// origin → SphereSDF (R=0.5 at origin) projects a disc around screen center.
//
// ZONE: shell tier (app/src/ root, like field_raymarch_golden.cpp) — crosses runtime (PointGraph,
// graph_bridge, field_render's compiler seam) AND platform (compileLibraryFromSource via the seam).
#include "runtime/field_render.h"  // raymarchFieldInjectBug, runRaymarchFieldOutputSelfTest (decl)

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/eval_context.h"         // EvaluationContext
#include "runtime/field_graph.h"          // setFieldSourceCompiler
#include "runtime/graph.h"                // Graph / Node / Connection / pinId / findSpec
#include "runtime/graph_bridge.h"         // libFromGraph
#include "runtime/point_graph.h"          // PointGraph::cookResident + registerBuiltinPointOps
#include "runtime/resident_eval_graph.h"  // buildEvalGraph (production path)
#include "runtime/tex_op_cache.h"         // clearTexOpCache

#include "platform/metal_compile.h"  // platform::compileLibraryFromSource (field source compiler seam)

#ifndef SW_SHADER_METALLIB
#define SW_SHADER_METALLIB "shaders.metallib"
#endif

namespace sw {

int runRaymarchFieldOutputSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  const uint32_t W = 256, H = 256;

  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  if (!dev) {
    std::printf("[selftest-raymarchfield-output] FAIL: no Metal device\n");
    pool->release();
    return 1;
  }
  MTL::CommandQueue* q = dev->newCommandQueue();
  NS::Error* err = nullptr;
  MTL::Library* lib =
      dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
  if (!lib) {
    std::printf("[selftest-raymarchfield-output] FAIL: no metallib\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }
  registerBuiltinPointOps();
  clearTexOpCache();
  // Field source compiler seam (renderField3d compiles the assembled MSL through this). Same wire as
  // main.cpp:270 / field_raymarch_golden.cpp.
  setFieldSourceCompiler([](void* device, const char* msl) -> void* {
    NS::Error* e = nullptr;
    return platform::compileLibraryFromSource(static_cast<MTL::Device*>(device), msl, &e);
  });

  // Graph: SphereSDF (node 1, .t3 defaults: Center=0, Radius=0.5) → RaymarchField (node 2, terminal).
  Graph g;
  Node sphere; sphere.id = 1; sphere.type = "SphereSDF";  // defaults
  g.nodes.push_back(sphere);
  Node rm; rm.id = 2; rm.type = "RaymarchField";  // march defaults (.t3) via the spec
  g.nodes.push_back(rm);

  // Wire SphereSDF.Result (Field out) → RaymarchField.SDFField (Field in).
  auto findPin = [&](const char* type, bool wantInput, const char* dataType) -> int {
    const NodeSpec* s = findSpec(type);
    for (size_t i = 0; i < s->ports.size(); ++i)
      if (s->ports[i].isInput == wantInput && s->ports[i].dataType == dataType) return (int)i;
    return -1;
  };
  int sphereOut = findPin("SphereSDF", false, "Field");
  int rmFieldIn = findPin("RaymarchField", true, "Field");
  g.connections.push_back({100, pinId(1, sphereOut), pinId(2, rmFieldIn)});

  PointGraph pg(dev, lib, q, W, H);
  SymbolLibrary slib = libFromGraph(g);
  ResidentEvalGraph rg = buildEvalGraph(slib, slib.rootId);
  EvaluationContext ctx{}; ctx.frameIndex = 0; ctx.time = 0.0f; ctx.deltaTime = 1.0f / 60.0f;

  raymarchFieldInjectBug() = injectBug;
  pg.cookResident(rg, ctx, /*reg=*/nullptr, /*targetPath=*/"2");  // RaymarchField terminal
  raymarchFieldInjectBug() = false;

  MTL::Texture* tex = pg.target();  // == displayTex (the RGBA8 the OutputWindow / eye show)
  if (!tex || (uint32_t)tex->width() != W || (uint32_t)tex->height() != H) {
    std::printf("[selftest-raymarchfield-output] FAIL: target() null / wrong size (%s)\n",
                tex ? "size" : "null");
    q->release(); lib->release(); dev->release(); pool->release();
    return 1;
  }

  std::vector<uint8_t> px((size_t)W * H * 4, 0);
  tex->getBytes(px.data(), W * 4, MTL::Region::Make2D(0, 0, W, H), 0);
  auto glowAt = [&](uint32_t x, uint32_t y) { return (float)px[((size_t)y * W + x) * 4] / 255.0f; };

  const uint32_t cx = W / 2, cy = H / 2;
  float center = glowAt(cx, cy);
  float corners = 0.25f * (glowAt(2, 2) + glowAt(W - 3, 2) + glowAt(2, H - 3) + glowAt(W - 3, H - 3));
  float margin = std::fabs(center - corners);

  int rc = 0;

  // (a) SILHOUETTE: center glow must differ from corner glow by a clear margin (the sphere is drawn).
  {
    const float kMargin = 0.02f;
    bool ok = margin > kMargin;
    if (!ok) rc = 1;
    std::printf("[selftest-raymarchfield-output] (a) silhouette center=%.4f corner=%.4f margin=%.4f "
                "(need>%.2f) %s\n", center, corners, margin, kMargin, ok ? "OK" : "RED");
  }

  // (b) 4-FOLD SYMMETRY: an off-center pixel inside the projected disc matches its 3 mirror images
  //     (no unproject x/y skew). Skipped under injectBug (a black clear makes everything trivially equal).
  if (!injectBug) {
    uint32_t ox = cx + 32, oy = cy + 32;
    float a = glowAt(ox, oy);
    float b = glowAt(W - 1 - ox, oy);
    float c = glowAt(ox, H - 1 - oy);
    float d = glowAt(W - 1 - ox, H - 1 - oy);
    float spread = std::max({a, b, c, d}) - std::min({a, b, c, d});
    // RGBA8 quantization (1/255 ≈ 0.004) — allow a slightly looser bound than the float golden's 0.01.
    bool ok = spread < 0.02f;
    if (!ok) rc = 1;
    std::printf("[selftest-raymarchfield-output] (b) 4-fold symmetry glow=(%.4f,%.4f,%.4f,%.4f) "
                "spread=%.4f (need<0.02) %s\n", a, b, c, d, spread, ok ? "OK" : "RED");
  }

  // (c) RANGE: center glow finite in [0,1] (RGBA8 → always in range; pins it is not all-zero on a hit).
  {
    bool ok = std::isfinite(center) && center >= 0.0f && center <= 1.0f;
    if (!ok) rc = 1;
    std::printf("[selftest-raymarchfield-output] (c) center glow=%.4f in [0,1] %s\n", center,
                ok ? "OK" : "RED");
  }

  q->release(); lib->release(); dev->release(); pool->release();

  if (injectBug) {
    if (rc == 0) {
      std::printf("[selftest-raymarchfield-output] FAIL: injectBug (black clear) tripped no tooth "
                  "(silhouette survived a skipped raymarch)\n");
      return 1;
    }
    std::printf("[selftest-raymarchfield-output] injectBug correctly RED\n");
    return 1;
  }
  if (rc == 0) std::printf("[selftest-raymarchfield-output] PASS\n");
  return rc;
}

}  // namespace sw

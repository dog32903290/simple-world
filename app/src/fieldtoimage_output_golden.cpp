// fieldtoimage_output_golden — --selftest-fieldtoimage-output. The PRODUCTION-PATH golden that proves
// "the UI actually draws a FieldToImage slice": a SphereSDF -> FieldToImage terminal is cooked through
// the CANONICAL resident cook (libFromGraph -> buildEvalGraph -> pg.cookResident), then pg.target() (==
// the displayTex the OutputWindow / eye / present path show) is read back. Mirrors
// field_raymarch_output_golden.cpp's shape for the RaymarchField terminal.
//
// TEETH: with the Gradient input UNWIRED (falls back to FieldToImage.t3's own black(0)->white(1) default)
// and every placement param at its .t3 default (Mode=MapDistanceToColor, Center=0, Scale=1, Rotate=0,
// SliceDepth=0, Range=(0,1), GainAndBias=(0.5,0.5)):
//   (a) CENTER pixel: inside the R=0.5 sphere -> d saturates to 0 -> the gradient's BLACK endpoint.
//   (b) BAND pixel (px=112 on the center row, field-space p.x≈0.758): d≈0.258 (unsaturated) -> a
//       MID-gradient GRAY, strictly brighter than the center (proves the distance is actually threaded
//       through Range/GainAndBias into the Gradient sample, not a flat clear).
//
// injectBug = fieldToImageInjectBug() true -> the cook short-circuits to an opaque BLACK clear (no field
// render) -> center and band both read black -> the margin tooth collapses -> RED.
//
// ZONE: shell tier (app/src/ root, like field_raymarch_output_golden.cpp) — crosses runtime (PointGraph,
// graph_bridge, field_render's compiler seam) AND platform (compileLibraryFromSource via the seam).
#include "runtime/field_render.h"  // fieldToImageInjectBug, runFieldToImageOutputSelfTest (decl)

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

int runFieldToImageOutputSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  const uint32_t W = 128, H = 128;

  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  if (!dev) {
    std::printf("[selftest-fieldtoimage-output] FAIL: no Metal device\n");
    pool->release();
    return 1;
  }
  MTL::CommandQueue* q = dev->newCommandQueue();
  NS::Error* err = nullptr;
  MTL::Library* lib =
      dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
  if (!lib) {
    std::printf("[selftest-fieldtoimage-output] FAIL: no metallib\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }
  registerBuiltinPointOps();
  clearTexOpCache();
  setFieldSourceCompiler([](void* device, const char* msl) -> void* {
    NS::Error* e = nullptr;
    return platform::compileLibraryFromSource(static_cast<MTL::Device*>(device), msl, &e);
  });

  // Graph: SphereSDF (node 1, .t3 defaults: Center=0, Radius=0.5) -> FieldToImage (node 2, terminal).
  // Gradient input left UNWIRED (the fallback-gradient tooth).
  Graph g;
  Node sphere; sphere.id = 1; sphere.type = "SphereSDF";
  g.nodes.push_back(sphere);
  Node fti; fti.id = 2; fti.type = "FieldToImage";  // every param at .t3 defaults via the spec
  g.nodes.push_back(fti);

  auto findPin = [&](const char* type, bool wantInput, const char* dataType) -> int {
    const NodeSpec* s = findSpec(type);
    for (size_t i = 0; i < s->ports.size(); ++i)
      if (s->ports[i].isInput == wantInput && s->ports[i].dataType == dataType) return (int)i;
    return -1;
  };
  int sphereOut = findPin("SphereSDF", false, "Field");
  int ftiFieldIn = findPin("FieldToImage", true, "Field");
  g.connections.push_back({100, pinId(1, sphereOut), pinId(2, ftiFieldIn)});

  PointGraph pg(dev, lib, q, W, H);
  SymbolLibrary slib = libFromGraph(g);
  ResidentEvalGraph rg = buildEvalGraph(slib, slib.rootId);
  EvaluationContext ctx{}; ctx.frameIndex = 0; ctx.time = 0.0f; ctx.deltaTime = 1.0f / 60.0f;

  fieldToImageInjectBug() = injectBug;
  pg.cookResident(rg, ctx, /*reg=*/nullptr, /*targetPath=*/"2");  // FieldToImage terminal
  fieldToImageInjectBug() = false;

  MTL::Texture* tex = pg.target();  // == displayTex (the RGBA8 the OutputWindow / eye show)
  if (!tex || (uint32_t)tex->width() != W || (uint32_t)tex->height() != H) {
    std::printf("[selftest-fieldtoimage-output] FAIL: target() null / wrong size (%s)\n",
                tex ? "size" : "null");
    q->release(); lib->release(); dev->release(); pool->release();
    return 1;
  }

  std::vector<uint8_t> px((size_t)W * H * 4, 0);
  tex->getBytes(px.data(), W * 4, MTL::Region::Make2D(0, 0, W, H), 0);
  auto rAt = [&](uint32_t x, uint32_t y) { return (float)px[((size_t)y * W + x) * 4] / 255.0f; };

  const uint32_t cy = (H - 1) / 2;      // center row (field-space p.y ≈ 0)
  const uint32_t cx = (W - 1) / 2;      // center col (field-space p.x ≈ 0, INSIDE the sphere)
  const uint32_t bandX = 112;           // field-space p.x ≈ 0.758, d ≈ 0.258 (mid-gradient band)

  float center = rAt(cx, cy);
  float band = rAt(bandX, cy);

  int rc = 0;

  // (a) CENTER ≈ BLACK (the gradient's t=0 endpoint; d saturates deep negative inside the sphere).
  {
    const float kTol = 3.0f / 255.0f + 0.01f;
    bool ok = center <= kTol;
    if (!ok) rc = 1;
    std::printf("[selftest-fieldtoimage-output] (a) center R=%.4f want≈0 (black endpoint) %s\n", center,
                ok ? "OK" : "RED");
  }

  // (b) BAND strictly brighter than CENTER by a clear margin (the mid-gradient gray vs the black
  //     endpoint) — proves the distance actually threads through into the Gradient sample, not a flat
  //     clear. NOT skipped under injectBug: a black clear collapses band==center -> margin≈0 -> RED
  //     (the load-bearing tooth for this golden, same discipline as field_raymarch_golden's silhouette
  //     margin — the ASSERTION runs in both modes; injectBug makes it fail naturally).
  {
    const float kMargin = 0.15f;  // t≈0.258 -> band R≈0.258, well clear of center≈0
    bool ok = (band - center) > kMargin;
    if (!ok) rc = 1;
    std::printf("[selftest-fieldtoimage-output] (b) band=%.4f center=%.4f margin=%.4f (need>%.2f) %s\n",
                band, center, band - center, kMargin, ok ? "OK" : "RED");
  }

  q->release(); lib->release(); dev->release(); pool->release();

  if (injectBug) {
    if (rc == 0) {
      std::printf("[selftest-fieldtoimage-output] FAIL: injectBug (black clear) tripped no tooth "
                  "(band survived a skipped render)\n");
      return 0;  // dead tooth -> exit 0 so --bite NO-BITE list catches it
    }
    std::printf("[selftest-fieldtoimage-output] injectBug correctly RED\n");
    return 1;
  }
  if (rc == 0) std::printf("[selftest-fieldtoimage-output] PASS\n");
  return rc;
}

}  // namespace sw

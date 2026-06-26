// connect_cooks_golden — --selftest-connect-cooks. The LOAD-BEARING end-to-end proof that a wire
// created by the agent's `connect` HAND VERB actually drives the PRODUCTION cook to a visible image.
//
// It builds SphereSDF + RaymarchField as two children of a compound (NO connection), then wires
// SphereSDF.Result(Field) -> RaymarchField.SDFField(Field) ONLY through the verb path
// (hand::feedLine "connect ..." -> g_connectHook -> ui::connectByVerb -> applyConnection -> the lib),
// rebuilds the resident eval graph from the now-wired lib, cooks pg.cookResident, and reads back
// pg.target() (== the displayTex the OutputWindow / eye show). The sphere SILHOUETTE + 4-fold
// symmetry are pinned — the SAME assertions field_raymarch_output_golden uses, but reached via the
// VERB, not a hand-built connection. This is the camera-free, panel-free visual proof the raymarch
// output deferred (no screen coords, no panel occlusion).
//
// injectBug = SKIP the connect verb. With no Field wire, the RaymarchField cook falls into
// clearTarget() (black) -> the silhouette vanishes -> center≈corner -> RED. Teeth on the real cook
// path, proving the verb is what carries the field to the output.
//
// ZONE: shell tier (app/src/ root, like field_raymarch_output_golden.cpp) — crosses runtime
// (PointGraph, resident_eval_graph, field_render) AND platform (compileLibraryFromSource seam) AND
// ui/app (the verb hook it drives). Restores the live doc on exit.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "app/command.h"                  // g_commands
#include "app/document.h"                 // doc::g_lib / g_compositionPath
#include "runtime/compound_graph.h"       // Symbol / SymbolChild
#include "runtime/eval_context.h"         // EvaluationContext
#include "runtime/field_graph.h"          // setFieldSourceCompiler
#include "runtime/field_render.h"         // raymarchFieldInjectBug
#include "runtime/graph.h"                // findSpec (atomic symbol defs)
#include "runtime/graph_bridge.h"         // atomicSymbolFromSpec (register op defs into the lib)
#include "runtime/point_graph.h"          // PointGraph::cookResident + registerBuiltinPointOps
#include "runtime/resident_eval_graph.h"  // buildEvalGraph (production path)
#include "runtime/tex_op_cache.h"         // clearTexOpCache
#include "ui/connection_ops.h"            // mountConnectionVerbs (install the verb hooks)
#include "verify/hand/hand.h"             // feedLine / clearPending

#include "platform/metal_compile.h"  // platform::compileLibraryFromSource (field source compiler seam)

#ifndef SW_SHADER_METALLIB
#define SW_SHADER_METALLIB "shaders.metallib"
#endif

namespace sw {

int runConnectCooksSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  const uint32_t W = 256, H = 256;

  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  if (!dev) {
    std::printf("[selftest-connect-cooks] FAIL: no Metal device\n");
    pool->release();
    return 1;
  }
  MTL::CommandQueue* q = dev->newCommandQueue();
  NS::Error* err = nullptr;
  MTL::Library* lib =
      dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
  if (!lib) {
    std::printf("[selftest-connect-cooks] FAIL: no metallib\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }
  registerBuiltinPointOps();
  clearTexOpCache();
  setFieldSourceCompiler([](void* device, const char* msl) -> void* {
    NS::Error* e = nullptr;
    return platform::compileLibraryFromSource(static_cast<MTL::Device*>(device), msl, &e);
  });

  // --- swap in a compound with the two children UNWIRED; restore the live doc on exit ---
  SymbolLibrary saved = doc::g_lib();
  std::vector<int> savedPath = doc::g_compositionPath;

  SymbolLibrary slib;
  // The atomic op DEFINITIONS the children reference — buildEvalGraph resolves a child's symbolId
  // against lib.symbols (its ports/Field defs live there), so these MUST exist (libFromGraph registers
  // them the same way; we hand-build the lib here to drive the verb, so we register them ourselves).
  slib.symbols["SphereSDF"]     = atomicSymbolFromSpec(*findSpec("SphereSDF"));
  slib.symbols["RaymarchField"] = atomicSymbolFromSpec(*findSpec("RaymarchField"));
  Symbol comp; comp.id = "comp"; comp.name = "comp";
  { SymbolChild s; s.id = 1; s.symbolId = "SphereSDF";     comp.children.push_back(s); }   // Field src
  { SymbolChild r; r.id = 2; r.symbolId = "RaymarchField"; comp.children.push_back(r); }   // tex terminal
  comp.nextChildId = 3;
  slib.symbols[comp.id] = comp;
  slib.rootId = "comp";

  doc::g_lib() = slib;
  doc::g_compositionPath.clear();   // root scope -> currentSymbol() == "comp"
  g_commands.clear();
  ui::mountConnectionVerbs();
  hand::clearPending();

  // *** THE VERB PATH ***: wire SphereSDF(1).Result(Field) -> RaymarchField(2).SDFField(Field) via the
  // hand `connect` directive. injectBug skips this line -> no Field wire -> black clear -> RED.
  if (!injectBug) hand::feedLine("connect 1 Result 2 SDFField");

  // Rebuild the resident eval graph from the (now-wired) lib + cook the production path.
  Symbol* curComp = doc::g_lib().find("comp");
  ResidentEvalGraph rg = buildEvalGraph(doc::g_lib(), doc::g_lib().rootId);
  EvaluationContext ctx{}; ctx.frameIndex = 0; ctx.time = 0.0f; ctx.deltaTime = 1.0f / 60.0f;

  PointGraph pg(dev, lib, q, W, H);
  pg.cookResident(rg, ctx, /*reg=*/nullptr, /*targetPath=*/"2");  // RaymarchField terminal (child id 2)

  MTL::Texture* tex = pg.target();  // == displayTex (the RGBA8 the OutputWindow / eye show)
  bool wiredOk = curComp && (curComp->connections.size() == (injectBug ? 0u : 1u));

  std::vector<uint8_t> px((size_t)W * H * 4, 0);
  int rc = 0;
  float center = 0.0f, corners = 0.0f, margin = 0.0f;
  if (tex && (uint32_t)tex->width() == W && (uint32_t)tex->height() == H) {
    tex->getBytes(px.data(), W * 4, MTL::Region::Make2D(0, 0, W, H), 0);
    auto glowAt = [&](uint32_t x, uint32_t y) { return (float)px[((size_t)y * W + x) * 4] / 255.0f; };
    const uint32_t cx = W / 2, cy = H / 2;
    center = glowAt(cx, cy);
    corners = 0.25f * (glowAt(2, 2) + glowAt(W - 3, 2) + glowAt(2, H - 3) + glowAt(W - 3, H - 3));
    margin = std::fabs(center - corners);

    // (a) the connect VERB created exactly the one expected wire (verb-path SSOT, not a manual wire).
    {
      bool ok = wiredOk;
      if (!ok) rc = 1;
      std::printf("[selftest-connect-cooks] (a) connect verb wired field=%d (need %d) %s\n",
                  curComp ? (int)curComp->connections.size() : -1, injectBug ? 0 : 1,
                  ok ? "OK" : "RED");
    }
    // (b) SILHOUETTE: center glow differs from corner glow by a clear margin (the sphere is drawn,
    //     because the verb carried the field to the terminal). Black clear under bug -> margin ~0 -> RED.
    {
      const float kMargin = 0.02f;
      bool ok = margin > kMargin;
      if (!ok) rc = 1;
      std::printf("[selftest-connect-cooks] (b) silhouette center=%.4f corner=%.4f margin=%.4f "
                  "(need>%.2f) %s\n", center, corners, margin, kMargin, ok ? "OK" : "RED");
    }
    // (c) 4-FOLD SYMMETRY (skip under bug: a black clear is trivially symmetric).
    if (!injectBug) {
      uint32_t ox = cx + 32, oy = cy + 32;
      float a = glowAt(ox, oy);
      float b = glowAt(W - 1 - ox, oy);
      float c = glowAt(ox, H - 1 - oy);
      float d = glowAt(W - 1 - ox, H - 1 - oy);
      float spread = std::max({a, b, c, d}) - std::min({a, b, c, d});
      bool ok = spread < 0.02f;
      if (!ok) rc = 1;
      std::printf("[selftest-connect-cooks] (c) 4-fold symmetry glow=(%.4f,%.4f,%.4f,%.4f) "
                  "spread=%.4f (need<0.02) %s\n", a, b, c, d, spread, ok ? "OK" : "RED");
    }
  } else {
    std::printf("[selftest-connect-cooks] FAIL: target() null / wrong size\n");
    rc = 1;
  }

  // --- restore the live document + GPU teardown ---
  doc::g_lib() = saved;
  doc::g_compositionPath = savedPath;
  g_commands.clear();
  hand::clearPending();
  q->release(); lib->release(); dev->release(); pool->release();

  if (injectBug) {
    if (rc == 0) {
      std::printf("[selftest-connect-cooks] FAIL: injectBug (no connect) tripped no tooth "
                  "(silhouette survived a skipped wire)\n");
      return 1;
    }
    std::printf("[selftest-connect-cooks] injectBug correctly RED\n");
    return 1;
  }
  if (rc == 0) std::printf("[selftest-connect-cooks] PASS\n");
  return rc;
}

}  // namespace sw

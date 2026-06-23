// SetRequestedResolution command op + the S1 output-resolution-seam golden — TiXL
// Operators/Lib/render/shading/SetRequestedResolution.cs (the EXPLICIT RequestedResolution override).
//
// BACKWARD-TRACE (SetRequestedResolution.cs:18-28):
//   var prev = context.RequestedResolution;                       // save
//   context.RequestedResolution = new Int2(w, h).Multiply(f)      // set (Width/Height or current × Multiply)
//                                  .Clamp(1, 16384);
//   Command.GetValue(context);                                    // cook child subtree under the new size
//   context.RequestedResolution = prev;                           // restore
// ★INTEGRATION (the S1 seam) — SW resolves RequestedResolution on a CPU cook-state slot
// (PointGraph::Impl::requestedResolution, NOT the 16-byte GPU EvaluationContext — fork-S1-cpu-resstack),
// and the SUBTREE is cooked by the DRIVER (cookCommand), not by this op. So the push/pop lives in the
// cook driver (point_graph.cpp / point_graph_resident.cpp cookCommand): it SETS requestedResolution =
// resolveSetRequestedResolution(params, current) before recursing into this node's Command input, and
// RESTORES it after. A WindowFollow RenderTarget (or a camera) INSIDE that subtree then sizes itself to
// the pushed value (RenderTarget.cs:53-56 adopt; EvaluationContext.cs:78,94 camera aspect). This op cook
// itself only FORWARDS the cooked subtree's items — exactly like Camera forwards its stamped items.
//
// FORKS (named): fork-S1-int2-as-renderresolution (RenderResolution{uint32_t} vs TiXL Int2 — identical
// behaviour). Width/Height==0 = "use the current RequestedResolution" (a bare Multiply scales the ambient
// size), faithful to TiXL's Int2(0,0) meaning "unset → keep context" combined with the Multiply factor.
#include "runtime/point_ops.h"

#include "runtime/point_graph.h"     // CmdCookCtx, registerCmdOp, RenderResolution, resolve* decls
#include "runtime/render_command.h"  // RenderCommand
#include "runtime/selftest_registry.h"  // REGISTER_SELFTESTS (leaf-local selftest registration)

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/graph.h"       // Graph / Node / pinId
#include "runtime/tixl_point.h"  // EvaluationContext / SwPoint

#ifndef SW_SHADER_METALLIB
#define SW_SHADER_METALLIB "shaders.metallib"
#endif

namespace sw {

namespace {
float paramOr(const std::map<std::string, float>& m, const char* id, float def) {
  auto it = m.find(id);
  return it != m.end() ? it->second : def;
}
uint32_t clampDim(float v) {  // TiXL Int2.Clamp(1, 16384) — also Metal's max 2D texture dim on Apple Si.
  float c = std::min(std::max(v, 1.0f), 16384.0f);
  return (uint32_t)std::lround(c);
}
}  // namespace

// The resolution this op PUSHES around its Command subtree (shared by the flat + resident cook drivers).
// w = (Width>0 ? Width : current.w) * Multiply; h likewise; both clamped [1,16384]. Width/Height==0 means
// "scale the ambient RequestedResolution" (a bare Multiply). NOT a method on a ctx — a pure function so
// the driver can call it BEFORE the op runs (the subtree is cooked in the driver).
RenderResolution resolveSetRequestedResolution(const std::map<std::string, float>& params,
                                               RenderResolution current) {
  float w = paramOr(params, "Width", 0.0f);
  float h = paramOr(params, "Height", 0.0f);
  float mul = paramOr(params, "Multiply", 1.0f);
  float baseW = (w > 0.0f) ? w : (float)current.w;
  float baseH = (h > 0.0f) ? h : (float)current.h;
  return RenderResolution{clampDim(baseW * mul), clampDim(baseH * mul)};
}

// SetRequestedResolution cook: Command subtree in → Command out. The driver already PUSHED the resolution
// around the subtree cook, so this op only forwards the cooked items (the RequestedResolution effect is
// purely on the children, already realized by the time we get here). Unwired Command → empty chain.
RenderCommand cookSetRequestedResolution(CmdCookCtx& c) {
  RenderCommand rc;
  if (c.inputCommand) rc.items = c.inputCommand->items;  // forward the subtree (cooked under the push)
  return rc;
}

void registerSetRequestedResolutionOp() {
  registerCmdOp("SetRequestedResolution", cookSetRequestedResolution);
}

// ───────────────────────────────── GOLDEN ─────────────────────────────────
// S1 output-resolution seam (the harness-first HARD GATE). Topology (window 800×600, deliberately ≠ the
// pushed size):
//   RadialPoints → DrawPoints(Command) → RenderTarget_inner(WindowFollow=adopt, Texture2D out)
//     → DrawScreenQuad(samples inner tex → Command) → SetRequestedResolution(320×200)
//       → RenderTarget_outer(Command in → Texture2D, the TERMINAL)
// When the terminal gathers its Command input it recurses into SetRequestedResolution, whose driver-side
// PUSH sets requestedResolution=320×200 BEFORE cooking its Command subtree (DrawScreenQuad). DrawScreenQuad
// pulls its Texture2D input → RenderTarget_inner cooks UNDER the push → its WindowFollow adopts 320×200
// (NOT the 800×600 window). So:
//   ASSERT: debugCookedTexture(inner) is 320×200 (inherited the pushed resolution, not the window) and
//           non-black at a known interior pixel (the RadialPoints ring actually rendered into it).
//   ★injectBug: SKIP SetRequestedResolution — DrawScreenQuad feeds the (WindowFollow) outer terminal
//     directly, so the inner cooks only under the outer's window push → inner sizes 800×600, width()!=320
//     → the tooth BITES (RED). Refuter run as the SINGLE token --selftest-requestedresolution-bug.
int runRequestedResolutionSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  const uint32_t N = 128, PW = 320, PH = 200, WINW = 800, WINH = 600;

  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue* q = dev->newCommandQueue();
  NS::Error* err = nullptr;
  MTL::Library* lib =
      dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
  if (!lib) {
    std::printf("[selftest-requestedresolution] FAIL: no metallib\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }
  registerBuiltinPointOps();  // RadialPoints + DrawPoints + DrawScreenQuad + RenderTarget + SetRequestedResolution

  PointGraph pg(dev, lib, q, WINW, WINH);  // window 800×600 — deliberately ≠ the pushed 320×200
  Graph g;
  // RadialPoints (id 1) → DrawPoints (id 2)
  Node gen; gen.id = 1; gen.type = "RadialPoints";
  gen.params["Count"] = (float)N; gen.params["Radius"] = 2.0f; g.nodes.push_back(gen);
  Node drw; drw.id = 2; drw.type = "DrawPoints"; g.nodes.push_back(drw);
  // RenderTarget_inner (id 3): WindowFollow (Resolution=0) → ADOPTS the active requestedResolution. It is
  // INSIDE SetRequestedResolution's subtree (faithful) → adopts 320×200, NOT the window.
  Node rti; rti.id = 3; rti.type = "RenderTarget"; rti.params["Resolution"] = 0.0f; g.nodes.push_back(rti);
  // DrawScreenQuad (id 4): samples the inner texture, emits a Command (the Texture2D→Command bridge so the
  // inner RenderTarget can sit INSIDE SetRequestedResolution's Command subtree).
  Node dsq; dsq.id = 4; dsq.type = "DrawScreenQuad"; g.nodes.push_back(dsq);
  // SetRequestedResolution (id 5): pushes 320×200 around its Command subtree (which contains the inner).
  Node srr; srr.id = 5; srr.type = "SetRequestedResolution";
  srr.params["Width"] = (float)PW; srr.params["Height"] = (float)PH; srr.params["Multiply"] = 1.0f;
  g.nodes.push_back(srr);
  // RenderTarget_outer (id 6, the TERMINAL): WindowFollow → its OWN texture follows the 800×600 window.
  // The inner (read via debugCookedTexture(3)) is the assertion target, NOT this.
  Node rto; rto.id = 6; rto.type = "RenderTarget"; rto.params["Resolution"] = 0.0f; g.nodes.push_back(rto);

  g.connections.push_back({101, pinId(1, 0), pinId(2, 0)});  // RadialPoints.points → DrawPoints.points
  g.connections.push_back({102, pinId(2, 1), pinId(3, 0)});  // DrawPoints.out → RenderTarget_inner.command
  g.connections.push_back({103, pinId(3, 1), pinId(4, 0)});  // RenderTarget_inner.tex → DrawScreenQuad.Texture
  if (!injectBug) {
    // inner is INSIDE SetRequestedResolution's subtree → when SetRequestedResolution(5) cooks DrawScreenQuad
    // (→ inner) under its 320×200 PUSH, the inner's WindowFollow adopts 320×200.
    g.connections.push_back({104, pinId(4, 1), pinId(5, 0)});  // DrawScreenQuad.out → SetRequestedResolution.command
    g.connections.push_back({105, pinId(5, 1), pinId(6, 0)});  // SetRequestedResolution.out → RenderTarget_outer.command
  } else {
    // ★injectBug: SKIP SetRequestedResolution — DrawScreenQuad feeds the outer terminal DIRECTLY. The inner
    // then cooks only under the outer's WindowFollow push (800×600) → inner adopts 800×600, NOT 320×200.
    g.connections.push_back({104, pinId(4, 1), pinId(6, 0)});  // DrawScreenQuad.out → RenderTarget_outer.command
  }

  EvaluationContext ctx{};
  ctx.frameIndex = 0; ctx.time = 0.0f; ctx.deltaTime = 1.0f / 60.0f;
  pg.cook(g, ctx, nullptr, /*targetNodeId=*/6);  // terminal = RenderTarget_outer; we assert on inner (id 3)

  MTL::Texture* inner = pg.debugCookedTexture(3);
  uint32_t iw = inner ? (uint32_t)inner->width() : 0;
  uint32_t ih = inner ? (uint32_t)inner->height() : 0;
  bool sized = inner && iw == PW && ih == PH;

  int nonBlack = 0;
  if (sized) {
    std::vector<uint8_t> px((size_t)iw * ih * 4, 0);
    inner->getBytes(px.data(), iw * 4, MTL::Region::Make2D(0, 0, iw, ih), 0);
    for (size_t i = 0; i < (size_t)iw * ih; ++i)
      if (px[i * 4] > 30 || px[i * 4 + 1] > 30 || px[i * 4 + 2] > 30) ++nonBlack;
  }

  bool pass = sized && nonBlack > 50;
  std::printf("[selftest-requestedresolution] inner=%ux%u(want %ux%u; window=%ux%u) nonBlack=%d(need>50) "
              "-> %s\n", iw, ih, PW, PH, WINW, WINH, nonBlack, pass ? "PASS" : "FAIL");

  lib->release(); q->release(); dev->release(); pool->release();

  if (injectBug) {
    if (pass) {
      std::printf("[selftest-requestedresolution] FAIL: injectBug still passed (inner sized 320×200 with "
                  "NO push — the seam is not actually pushing)\n");
      return 1;
    }
    std::printf("[selftest-requestedresolution] injectBug correctly RED (no SetRequestedResolution push → "
                "inner RenderTarget sized from the %ux%u window, not 320×200)\n", WINW, WINH);
    return 1;
  }
  return pass ? 0 : 1;
}

REGISTER_SELFTESTS(/*orderBase=*/310, {"requestedresolution", runRequestedResolutionSelfTest});

}  // namespace sw

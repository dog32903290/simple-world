// Output-window VIEW BACKGROUND COLOR golden (TiXL OutputWindow._backgroundColor →
// EvaluationContext.BackgroundColor, consumed in CommandOutputUi.Recompute:63-67 as the base
// ClearRenderTargetView before the Command chain runs).
//
// ★SEAM under test: setCommandViewBackground (cmd_view_background.h ambient) → the terminal Command
// executor (cookRenderTarget, point_ops_rendertarget.cpp) uses it as the BASE clear color INSTEAD of its
// hardcoded black. Same shape as the C1 active-camera scope: UI sets the ambient, the executor reads it.
// The Output window owns this (session-only view state, never serialized) and engages it for a
// Command-type view; a Texture2D view never engages it (the picker is Command-only, UI side).
//
// PARITY (CommandOutputUi.Recompute):
//   var colorRgba = context.BackgroundColor;                          // the window's _backgroundColor
//   deviceContext.ClearRenderTargetView(_msaaColorBufferRtv, colorRgba);  // BASE clear (BEFORE the chain)
//   slot.Update(context);                                            // then the Command chain draws over it
// So an EMPTY corner (no point rasterized there) reads back == BackgroundColor. That is what this
// golden asserts: a corner pixel of a bare DrawPoints(Command) terminal == the engaged background.
//
// Topology (IDENTICAL across both legs — differs ONLY in the override, so non-tautological):
//   RadialPoints(id1, centered ring) → DrawPoints(id2, Command) [TERMINAL, NO RenderTarget op]
//   The terminal Command realizes through execIntoTarget("RenderTarget",...) into pg.target().
//   Corner pixel (0,0) is OUTSIDE the centered ring → it carries ONLY the clear color.
//   bg = (0.2, 0.4, 0.6, 1.0) → RGBA8 (51,102,153,255).
//   ★injectBug: SKIP the setter → the executor clears to its default opaque BLACK → corner (0,0,0)
//     → the bg assertion BITES (RED). Proves the seam under test is the OVERRIDE, not the default.
#include "runtime/point_ops.h"

#include "runtime/cmd_view_background.h"  // setCommandViewBackground / clearCommandViewBackground (the seam)
#include "runtime/point_graph.h"        // PointGraph, RenderResolution
#include "runtime/render_command.h"     // RenderCommand
#include "runtime/selftest_registry.h"  // REGISTER_SELFTESTS

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/graph.h"       // Graph / Node / pinId
#include "runtime/tixl_point.h"  // EvaluationContext

namespace sw {

int runOutputBackgroundColorSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  const uint32_t N = 256, W = 256, H = 256;
  // Target bg: a non-grey, all-channels-distinct color so a per-channel readback proves all four
  // components route (not a coincidental equal-channel match). RGBA8: (51,102,153,255).
  const float BG_R = 0.2f, BG_G = 0.4f, BG_B = 0.6f, BG_A = 1.0f;
  const int WANT_R = 51, WANT_G = 102, WANT_B = 153, WANT_A = 255;  // round(c*255)
  const int TOL = 1;  // ±1/255 (spec tolerance)

  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue* q = dev->newCommandQueue();
  NS::Error* err = nullptr;
  MTL::Library* lib =
      dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
  if (!lib) {
    std::printf("[selftest-output-bgcolor] FAIL: no metallib\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }
  registerBuiltinPointOps();  // RadialPoints + DrawPoints + RenderTarget executor

  PointGraph pg(dev, lib, q, W, H);
  Graph g;
  // RadialPoints (id 1): a small centered ring → leaves the image corners empty (only the clear shows).
  Node gen; gen.id = 1; gen.type = "RadialPoints";
  gen.params["Count"] = (float)N; gen.params["Radius"] = 0.5f; g.nodes.push_back(gen);
  // DrawPoints (id 2): the Command TERMINAL. No RenderTarget op → realizes via execIntoTarget into target().
  Node drw; drw.id = 2; drw.type = "DrawPoints"; g.nodes.push_back(drw);
  g.connections.push_back({101, pinId(1, 0), pinId(2, 0)});  // RadialPoints.points → DrawPoints.points

  EvaluationContext ctx{};
  ctx.frameIndex = 0; ctx.time = 0.0f; ctx.deltaTime = 1.0f / 60.0f;

  // Engage the view background override (the seam). ★injectBug: SKIP it → executor stays black → RED.
  clearCommandViewBackground();  // start from the executor default (hygiene; ambient is process-global)
  if (!injectBug) setCommandViewBackground(BG_R, BG_G, BG_B, BG_A);
  pg.cook(g, ctx, nullptr, /*targetNodeId=*/2);

  MTL::Texture* tex = pg.target();
  int cr = -1, cg = -1, cb = -1, ca = -1;
  if (tex && tex->width() == W && tex->height() == H) {
    std::vector<uint8_t> px((size_t)W * H * 4, 0);
    tex->getBytes(px.data(), W * 4, MTL::Region::Make2D(0, 0, W, H), 0);
    // Corner pixel (0,0): outside the centered ring → the clear color, nothing drawn over it.
    cr = px[0]; cg = px[1]; cb = px[2]; ca = px[3];
  }

  auto near = [&](int got, int want) { return got >= 0 && std::abs(got - want) <= TOL; };
  const bool cornerIsBg = near(cr, WANT_R) && near(cg, WANT_G) && near(cb, WANT_B) && near(ca, WANT_A);
  const bool pass = cornerIsBg;

  std::printf("[selftest-output-bgcolor] corner=(%d,%d,%d,%d) want=(%d,%d,%d,%d)±%d -> %s\n",
              cr, cg, cb, ca, WANT_R, WANT_G, WANT_B, WANT_A, TOL, pass ? "PASS" : "FAIL");

  clearCommandViewBackground();  // leave no leaked ambient (it is process-global)
  lib->release(); q->release(); dev->release(); pool->release();

  if (injectBug) {
    if (pass) {
      std::printf("[selftest-output-bgcolor] FAIL: injectBug still passed (no override set, yet the "
                  "corner matched the background — the seam is not actually driving the clear color)\n");
      return 1;
    }
    std::printf("[selftest-output-bgcolor] injectBug correctly RED (no override → executor clears to "
                "default black → corner != background)\n");
    return 1;  // -bug ALWAYS exits non-zero (the tooth bit) — the bite scanner asserts this
  }
  return pass ? 0 : 1;
}

REGISTER_SELFTESTS(/*orderBase=*/313, {"output-bgcolor", runOutputBackgroundColorSelfTest});

}  // namespace sw

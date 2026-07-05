// point_ops_setrequestedresolutioncmd_golden — --selftest-setrequestedresolutioncmd.
//
// Proves the flow-rail SetRequestedResolutionCmd pushes the SAME RequestedResolution scope as
// SetRequestedResolution BUT with the per-axis StretchResolution factor (SetRequestedResolutionCmd.cs:24-28).
// Non-uniform stretch is the tooth: a plain single-Multiply push would size the inner texture SQUARE-scaled;
// only the correct per-axis stretch yields the asymmetric target, so a dropped/uniform stretch (the -bug)
// BITES. Split out of point_ops_setrequestedresolution.cpp to keep that TU under the 400-line ratchet.
//
// Topology (window 800×600, ≠ the pushed size — same shape as the S1 SetRequestedResolution golden):
//   RadialPoints → DrawPoints(Command) → RenderTarget_inner(WindowFollow, adopts requestedResolution)
//     → DrawScreenQuad(samples inner → Command) → SetRequestedResolutionCmd(Res=200×100, Scale=2, Stretch=(3,1))
//       → RenderTarget_outer(TERMINAL)
// Closed-form (cs:24-28, all four >0 → adopt Resolution): inner = clamp(200*2*3), clamp(100*2*1) = 1200×200.
//   ★injectBug: SKIP the SetRequestedResolutionCmd push (DrawScreenQuad → outer directly) → inner cooks
//     under the 800×600 window → width()!=1200 → the tooth BITES (RED).
#include <cstdint>
#include <cstdio>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/graph.h"               // Graph / Node / pinId
#include "runtime/point_graph.h"         // PointGraph / registerBuiltinPointOps
#include "runtime/selftest_registry.h"   // REGISTER_SELFTESTS
#include "runtime/tixl_point.h"          // EvaluationContext

#ifndef SW_SHADER_METALLIB
#define SW_SHADER_METALLIB "shaders.metallib"
#endif

namespace sw {

int runSetRequestedResolutionCmdSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  const uint32_t N = 128, WINW = 800, WINH = 600;
  // Resolution 200×100, ScaleResolution 2, StretchResolution (3,1): cs:27-28 → 200*2*3 × 100*2*1 = 1200×200.
  const uint32_t RW = 200, RH = 100;
  const float SCALE = 2.0f, STRETCH_X = 3.0f, STRETCH_Y = 1.0f;
  const uint32_t PW = (uint32_t)(RW * SCALE * STRETCH_X);  // 1200
  const uint32_t PH = (uint32_t)(RH * SCALE * STRETCH_Y);  // 200

  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue* q = dev->newCommandQueue();
  NS::Error* err = nullptr;
  MTL::Library* lib =
      dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
  if (!lib) {
    std::printf("[selftest-setrequestedresolutioncmd] FAIL: no metallib\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }
  registerBuiltinPointOps();  // RadialPoints + DrawPoints + DrawScreenQuad + RenderTarget + SetRequestedResolutionCmd

  PointGraph pg(dev, lib, q, WINW, WINH);  // window 800×600 — deliberately ≠ the pushed 1200×200
  Graph g;
  Node gen; gen.id = 1; gen.type = "RadialPoints";
  gen.params["Count"] = (float)N; gen.params["Radius"] = 2.0f; g.nodes.push_back(gen);
  Node drw; drw.id = 2; drw.type = "DrawPoints"; g.nodes.push_back(drw);
  Node rti; rti.id = 3; rti.type = "RenderTarget"; rti.params["Resolution"] = 0.0f; g.nodes.push_back(rti);
  Node dsq; dsq.id = 4; dsq.type = "DrawScreenQuad"; g.nodes.push_back(dsq);
  Node srr; srr.id = 5; srr.type = "SetRequestedResolutionCmd";
  srr.params["Width"] = (float)RW; srr.params["Height"] = (float)RH;
  srr.params["ScaleResolution"] = SCALE;
  srr.params["StretchResolution.x"] = STRETCH_X; srr.params["StretchResolution.y"] = STRETCH_Y;
  g.nodes.push_back(srr);
  Node rto; rto.id = 6; rto.type = "RenderTarget"; rto.params["Resolution"] = 0.0f; g.nodes.push_back(rto);

  g.connections.push_back({101, pinId(1, 0), pinId(2, 0)});
  g.connections.push_back({102, pinId(2, 1), pinId(3, 0)});
  g.connections.push_back({103, pinId(3, 1), pinId(4, 0)});
  if (!injectBug) {
    g.connections.push_back({104, pinId(4, 1), pinId(5, 0)});  // DrawScreenQuad → SetRequestedResolutionCmd.Texture
    g.connections.push_back({105, pinId(5, 1), pinId(6, 0)});  // SetRequestedResolutionCmd.out → outer
  } else {
    g.connections.push_back({104, pinId(4, 1), pinId(6, 0)});  // ★bug: skip the push → inner sizes from window
  }

  EvaluationContext ctx{};
  ctx.frameIndex = 0; ctx.time = 0.0f; ctx.deltaTime = 1.0f / 60.0f;
  pg.cook(g, ctx, nullptr, /*targetNodeId=*/6);

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
  std::printf("[selftest-setrequestedresolutioncmd] inner=%ux%u(want %ux%u; window=%ux%u; Res=%ux%u Scale=%.0f "
              "Stretch=(%.0f,%.0f)) nonBlack=%d(need>50) -> %s\n", iw, ih, PW, PH, WINW, WINH, RW, RH,
              (double)SCALE, (double)STRETCH_X, (double)STRETCH_Y, nonBlack, pass ? "PASS" : "FAIL");

  lib->release(); q->release(); dev->release(); pool->release();

  if (injectBug) {
    if (pass) {
      std::printf("[selftest-setrequestedresolutioncmd] FAIL: injectBug still passed (inner sized %ux%u with "
                  "NO push — the seam is not actually pushing the stretched resolution)\n", PW, PH);
      return 1;
    }
    std::printf("[selftest-setrequestedresolutioncmd] injectBug correctly RED (no push → inner sized from the "
                "%ux%u window, not the stretched %ux%u)\n", WINW, WINH, PW, PH);
    return 1;
  }
  return pass ? 0 : 1;
}

REGISTER_SELFTESTS(/*orderBase=*/312,
                   {"setrequestedresolutioncmd", runSetRequestedResolutionCmdSelfTest});

}  // namespace sw

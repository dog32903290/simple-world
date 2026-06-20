// DrawLinesBuildup command op (Points → Command) — TiXL Operators/Lib/point/draw/DrawLinesBuildup.cs
// (compound → DrawLinesBuildup.hlsl). A polyline like DrawLines (sequential adjacency
// Points[i]→Points[i+1], screen-space-thickened band) with a progressive BUILDUP REVEAL driven by
// each point's W (FX1):
//
//   TiXL psMain: OffsetU = TransitionProgress - 0.01;  u = wAtPoint - OffsetU;
//     f1 = saturate((u + VisibleRange) * 100);  f2 = 1 - saturate(u * 100);  alpha *= f1 * f2;
//   wAtPoint = the point's parametric W (FX1).
//
// A piece of the line is VISIBLE when wAtPoint ∈ [OffsetU - VisibleRange, OffsetU]: a window of
// width VisibleRange whose leading edge tracks TransitionProgress. As TransitionProgress sweeps
// 0→1 the window slides along the polyline → the line "builds up". (.t3 routing trace verified: the
// FloatsToBuffer fills cbuffer Params(b0) in order Color/LineWidth/ShrinkWithDistance/OffsetU/
// VisibleRange — NO mid-graph math except the Add(-0.01) on TransitionProgress → OffsetU and the
// Multiply(10.8)-free LineWidth. No Cut55 mis-route.)
//
// It rides DrawKind::LinesBuildup — its OWN shader (draw_lines_buildup.metal) + PSO + params — so
// DrawLines / DrawClosedLines (DrawKind::Lines) are byte-identical.
//
// Params mirror DrawLinesBuildup.t3 defaults: Color (Vec4 white) + LineWidth (0.02) +
// TransitionProgress (0.5) + VisibleRange (0.5). FORKS (named, inherited from DrawLines' baked-ortho
// class — sw has NO camera): TiXL's camera/Transforms, ShrinkWithDistance, Fog, the Texture_ sample
// (default white-pixel = no-op tint), the neighbor-normal miter join, BlendMode, ZTest/ZWrite are
// dropped; the BlendColors×ForegroundColor theme coupling on Color is dropped (Color used verbatim,
// same fork class as DrawLines). Texture_ port deferred (asset-bind seam not built) → omitted.
#include "runtime/point_ops.h"

#include "runtime/point_graph.h"     // CmdCookCtx, registerCmdOp, cookParam/cookVecN
#include "runtime/render_command.h"  // RenderCommand / RenderDrawItem / DrawKind

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/compound_graph.h"       // SymbolLibrary / Symbol / SymbolChild (resident golden)
#include "runtime/graph.h"                // Graph/Node (flat selftest)
#include "runtime/resident_eval_graph.h"  // buildEvalGraph / kSymbolBoundary (resident golden)
#include "runtime/tixl_point.h"           // SwPoint + EvaluationContext

#ifndef SW_SHADER_METALLIB
#define SW_SHADER_METALLIB "shaders.metallib"
#endif

namespace sw {

RenderCommand cookDrawLinesBuildup(CmdCookCtx& c) {
  RenderCommand rc;
  if (!c.points || c.count < 2) return rc;  // need ≥2 points for one segment
  RenderDrawItem it{c.points, c.count, 3.5f};
  it.kind = DrawKind::LinesBuildup;
  float white[4] = {1.0f, 1.0f, 1.0f, 1.0f};
  cookVecN(c, "Color", white, 4, it.color);
  it.lineWidth = cookParam(c, "LineWidth", 0.02f);                       // TiXL LineWidth (.t3 0.02)
  it.transitionProgress = cookParam(c, "TransitionProgress", 0.5f);      // TiXL TransitionProgress (.t3 0.5)
  it.visibleRange = cookParam(c, "VisibleRange", 0.5f);                  // TiXL VisibleRange (.t3 0.5)
  rc.items.push_back(it);
  return rc;
}

void registerDrawLinesBuildupOp() { registerCmdOp("DrawLinesBuildup", cookDrawLinesBuildup); }

namespace {

// CPU-fill a Points generator stub for the resident golden: a horizontal row of points across the
// view with W (FX1) ramping 0→1 left→right (the parametric reveal coord). With TransitionProgress in
// the middle + a narrow VisibleRange the MIDDLE of the line reveals while the RIGHT end (W ahead of
// the window) stays dark. injectBug(B) is driven by a TransitionProgress=5 override on the resident
// graph (window past the whole line → nothing reveals) — the gen itself is unchanged.
void rampWLineGen(PointCookCtx& c) {
  if (!c.output || c.count == 0) return;
  SwPoint* dst = (SwPoint*)c.output->contents();
  const float xL = -2.0f, xR = 2.0f;
  for (uint32_t i = 0; i < c.count; ++i) {
    dst[i] = SwPoint{};
    dst[i].Color = {1, 1, 1, 1};
    dst[i].Scale = {1, 1, 1};
    float t = (c.count > 1) ? (float)i / (float)(c.count - 1) : 0.0f;  // 0→1 along the line
    dst[i].Position = {xL + (xR - xL) * t, 0.0f, 0.0f};
    dst[i].FX1 = t;  // W = parametric position → the reveal coord
  }
}

Symbol atomicOp(const char* id, std::vector<SlotDef> ins, std::vector<SlotDef> outs) {
  Symbol s; s.id = id; s.name = id; s.atomic = true;
  s.inputDefs = std::move(ins); s.outputDefs = std::move(outs);
  return s;
}

}  // namespace

// ─────────────────────────────────────────────────────────────────────────────────────────────────
// Golden. TWO legs, both required (R-2 鐵律). The DISTINGUISHING behavior vs DrawLines is the
// W-reveal: with TransitionProgress=0.5, VisibleRange=0.3 the visible window is wAtPoint ∈
// [0.19, 0.49] → the line is lit ONLY where W is in that window. A row of points with W ramping
// 0→1 left→right is therefore lit in the MIDDLE-LEFT and DARK on the far RIGHT (W>0.49, ahead of
// the window) — that asymmetry is the buildup. A plain DrawLines would light the WHOLE row.
//   FLAT  : hand-build a RenderCommand (LinesBuildup) over a CPU W-ramp row, run the executor,
//           read back. Assert the in-window band is lit AND the far-right (ahead-of-window) is dark.
//           injectBug(A) sets TransitionProgress=5 → window past all W → the in-window band goes dark → FAIL.
//   RESIDENT (production path): Gen(W-ramp)→DrawLinesBuildup→RenderTarget via pg.cookResident, read
//           back pg.target(). injectBug(B) overrides TransitionProgress=5 (window past the line) →
//           the in-window band goes dark → FAIL.
// ─────────────────────────────────────────────────────────────────────────────────────────────────
int runDrawLinesBuildupSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  const uint32_t W = 256, H = 256;

  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue* q = dev->newCommandQueue();
  NS::Error* err = nullptr;
  MTL::Library* lib =
      dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
  if (!lib) {
    printf("[selftest-drawlinesbuildup] FAIL: no metallib\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }
  registerDrawLinesBuildupOp();
  registerRenderTargetOp();

  const float viewExtent = 3.5f;
  // World x in [-2,2] → NDC [-0.571,0.571] → px [55,201] (center 128). W ramps 0→1 along that.
  // Window wAtPoint ∈ [0.19,0.49] → x where t∈[0.19,0.49] → world x∈[-1.24,-0.04] → px∈[78,127].
  // Far right (t≈1, W≈1, ahead of window) → x≈2 → px≈201 → must be DARK.
  auto worldXToPx = [&](float wx) { return (int)std::lround((wx / viewExtent) * 0.5f * (float)W + 0.5f * (float)W); };
  const int midY = (int)H / 2;
  const int xInWindow = worldXToPx(-0.6f);  // t≈0.35, W≈0.35 ∈ [0.19,0.49] → reveal ≈ 1
  const int xFarRight = worldXToPx(1.9f);   // t≈0.975, W≈0.975 ahead of window → reveal 0

  // ── FLAT leg ───────────────────────────────────────────────────────────────────────────────────
  const uint32_t N = 32;
  MTL::Buffer* pts = dev->newBuffer((NS::UInteger)N * sizeof(SwPoint), MTL::ResourceStorageModeShared);
  SwPoint* d = (SwPoint*)pts->contents();
  const float xL = -2.0f, xR = 2.0f;
  for (uint32_t i = 0; i < N; ++i) {
    d[i] = SwPoint{}; d[i].Color = {1, 1, 1, 1}; d[i].Scale = {1, 1, 1};
    float t = (float)i / (float)(N - 1);
    d[i].Position = {xL + (xR - xL) * t, 0.0f, 0.0f};
    d[i].FX1 = t;
  }

  MTL::TextureDescriptor* td =
      MTL::TextureDescriptor::texture2DDescriptor(MTL::PixelFormatRGBA8Unorm, W, H, false);
  td->setUsage(MTL::TextureUsageRenderTarget | MTL::TextureUsageShaderRead);
  td->setStorageMode(MTL::StorageModeShared);
  MTL::Texture* tex = dev->newTexture(td);

  RenderCommand rc;
  RenderDrawItem it{pts, N, viewExtent};
  it.kind = DrawKind::LinesBuildup;
  it.color[0] = it.color[1] = it.color[2] = it.color[3] = 1.0f;
  it.lineWidth = 0.4f;                              // wide band so the row crosses several pixel rows
  it.transitionProgress = injectBug ? 5.0f : 0.5f;  // injectBug(A): window past all W → whole line dark
  it.visibleRange = 0.3f;
  rc.items.push_back(it);

  TexCookCtx c;
  c.dev = dev; c.lib = lib; c.queue = q;
  c.nodeId = 1; c.command = &rc; c.output = tex;
  cookRenderTarget(c);

  std::vector<uint8_t> px((size_t)W * H * 4, 0);
  tex->getBytes(px.data(), W * 4, MTL::Region::Make2D(0, 0, W, H), 0);
  auto lit = [&](int x, int y) {
    if (x < 0 || y < 0 || x >= (int)W || y >= (int)H) return false;
    size_t i = ((size_t)y * W + x) * 4;
    return px[i] > 30 || px[i + 1] > 30 || px[i + 2] > 30;
  };
  auto bandLit = [&](int x, int y, int rad) {
    for (int dx = -rad; dx <= rad; ++dx)
      for (int dy = -rad; dy <= rad; ++dy)
        if (lit(x + dx, y + dy)) return true;
    return false;
  };
  bool inWindowLit = bandLit(xInWindow, midY, 4);   // W in [0.19,0.49] → revealed → lit (when not -bug)
  bool farRightLit = bandLit(xFarRight, midY, 4);   // W ahead of window → NOT revealed → dark
  // PASS(flat) asserts the TRUE buildup behavior: the in-window band is lit AND the far-right
  // (ahead-of-window) is dark. injectBug(A) TransitionProgress=5 → window past all W → in-window
  // band goes dark → inWindowLit=false → FAIL. (The far-right-dark half also distinguishes from a
  // plain DrawLines, which would light the whole row.)
  bool flatPass = inWindowLit && !farRightLit;

  pts->release(); tex->release();

  // ── RESIDENT (production) leg ────────────────────────────────────────────────────────────────────
  registerPointOp("RadialPoints", rampWLineGen);  // a real registered gen, CPU-fills the W-ramp row

  SymbolLibrary slib;
  slib.symbols["RadialPoints"] =
      atomicOp("RadialPoints", {{"Count", "Count", "Float", (float)N}},
               {{"points", "points", "Points", 0.0f}});
  slib.symbols["DrawLinesBuildup"] =
      atomicOp("DrawLinesBuildup",
               {{"points", "points", "Points", 0.0f},
                {"Color.x", "Color", "Float", 1.0f}, {"Color.y", "Color.y", "Float", 1.0f},
                {"Color.z", "Color.z", "Float", 1.0f}, {"Color.w", "Color.w", "Float", 1.0f},
                {"LineWidth", "LineWidth", "Float", 0.02f},
                {"TransitionProgress", "TransitionProgress", "Float", 0.5f},
                {"VisibleRange", "VisibleRange", "Float", 0.5f}},
               {{"out", "out", "Command", 0.0f}});
  slib.symbols["RenderTarget"] = atomicOp(
      "RenderTarget",
      {{"command", "command", "Command", 0.0f}, {"Resolution", "Resolution", "Float", 0.0f},
       {"CustomW", "CustomW", "Float", 512.0f}, {"CustomH", "CustomH", "Float", 512.0f},
       {"ClearColor.x", "ClearColor", "Float", 0.0f}, {"ClearColor.y", "ClearColor.y", "Float", 0.0f},
       {"ClearColor.z", "ClearColor.z", "Float", 0.0f}, {"ClearColor.w", "ClearColor.w", "Float", 1.0f}},
      {{"out", "out", "Texture2D", 0.0f}});
  Symbol root; root.id = "Root"; root.name = "Root"; root.atomic = false;
  root.outputDefs = {{"out", "out", "Texture2D", 0.0f}};
  SymbolChild g; g.id = 1; g.symbolId = "RadialPoints"; g.overrides["Count"] = (float)N;
  SymbolChild db; db.id = 2; db.symbolId = "DrawLinesBuildup";
  db.overrides["LineWidth"] = 0.4f;
  db.overrides["TransitionProgress"] = injectBug ? 5.0f : 0.5f;  // injectBug(B): window past the line
  db.overrides["VisibleRange"] = 0.3f;
  SymbolChild r; r.id = 3; r.symbolId = "RenderTarget"; r.overrides["Resolution"] = 0.0f;  // WindowFollow
  root.children = {g, db, r};
  root.connections = {
      {1, "points", 2, "points"},
      {2, "out", 3, "command"},
      {3, "out", kSymbolBoundary, "out"},
  };
  slib.symbols["Root"] = root; slib.rootId = "Root";
  ResidentEvalGraph rg = buildEvalGraph(slib, "Root");

  EvaluationContext rctx{};
  rctx.frameIndex = 0; rctx.time = 0.0f; rctx.deltaTime = 1.0f / 60.0f;

  PointGraph pg(dev, lib, q, W, H);
  pg.cookResident(rg, rctx, /*reg=*/nullptr, /*targetPath=*/"3");
  MTL::Texture* rtex = pg.target();

  bool resInWindowLit = false, resFarRightLit = false, resHasPixels = false;
  if (rtex) {
    uint32_t rw = (uint32_t)rtex->width(), rh = (uint32_t)rtex->height();
    std::vector<uint8_t> rpx((size_t)rw * rh * 4, 0);
    rtex->getBytes(rpx.data(), rw * 4, MTL::Region::Make2D(0, 0, rw, rh), 0);
    auto rlit = [&](int x, int y) {
      if (x < 0 || y < 0 || x >= (int)rw || y >= (int)rh) return false;
      size_t i = ((size_t)y * rw + x) * 4;
      return rpx[i] > 30 || rpx[i + 1] > 30 || rpx[i + 2] > 30;
    };
    auto rBand = [&](int x, int y, int rad) {
      for (int dx = -rad; dx <= rad; ++dx)
        for (int dy = -rad; dy <= rad; ++dy)
          if (rlit(x + dx, y + dy)) return true;
      return false;
    };
    // The resident target tracks the window size (WindowFollow → 256×256 here, same map as flat).
    int rcy = (int)rh / 2;
    auto rWorldXToPx = [&](float wx) {
      return (int)std::lround((wx / viewExtent) * 0.5f * (float)rw + 0.5f * (float)rw);
    };
    resInWindowLit = rBand(rWorldXToPx(-0.6f), rcy, 4);
    resFarRightLit = rBand(rWorldXToPx(1.9f), rcy, 4);
    for (size_t i = 0; i < (size_t)rw * rh && !resHasPixels; ++i)
      if (rpx[i * 4] > 30 || rpx[i * 4 + 1] > 30 || rpx[i * 4 + 2] > 30) resHasPixels = true;
  }
  // PASS(resident): non-empty target + the in-window band lit + the far-right (ahead-of-window) dark,
  // on the production path. injectBug(B) TransitionProgress=5 → in-window band dark → resInWindowLit
  // false → FAIL.
  bool resPass = resHasPixels && resInWindowLit && !resFarRightLit;

  bool pass = flatPass && resPass;
  printf("[selftest-drawlinesbuildup] FLAT: inWindow=%d farRight=%d(tp=%.1f) -> %s | "
         "RESIDENT: hasPixels=%d inWindow=%d farRight=%d -> %s | %s\n",
         inWindowLit, farRightLit, it.transitionProgress, flatPass ? "ok" : "BAD",
         resHasPixels, resInWindowLit, resFarRightLit, resPass ? "ok" : "BAD",
         pass ? "PASS" : "FAIL");

  lib->release(); q->release(); dev->release(); pool->release();
  return pass ? 0 : 1;
}

}  // namespace sw

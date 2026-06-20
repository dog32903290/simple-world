// DrawClosedLines command op (Points → Command) — TiXL Operators/Lib/point/draw/DrawClosedLines.cs
// (compound → DrawLinesAlt.hlsl). The CLOSED-LOOP sibling of DrawLines: it connects the point bag
// into a polyline AND wraps the last point back to the first (Points[last]→Points[0]), so an open
// row of points becomes a closed polygon. TiXL's GetWrappedIndex (DrawLinesAlt.hlsl:71-83) does the
// wrap modulo `shapePts`; PointsPerShape>0 splits the bag into multiple closed shapes of that many
// points each (.t3 default 0 = ONE shape over the whole bag).
//
// It rides the SAME DrawKind::Lines path as DrawLines — same screen-space-band shader (draw_lines.metal),
// same executor case — differing ONLY in (a) item.lineClosed=true (the shader's wrap branch) and (b)
// item.pointsPerShape (TiXL PointsPerShape). The executor draws `count` segments (one per point) instead
// of `count-1` so the closing wrap segment exists. DrawLines (lineClosed=false) is byte-identical.
//
// Params mirror DrawClosedLines.t3 defaults: Color (Vec4, white) + LineWidth (0.02) + PointsPerShape (0).
// FORKS (named, inherited from DrawLines' band model — draw_lines.metal): TiXL's camera/Transforms
// (ObjectToClipSpace), ShrinkWithDistance, Fog, texture/UV, miter-join, BlendMode, ZTest/ZWrite,
// LineOffset, WidthFactor, TransitionProgress, FadeOutTooLong are dropped — sw has no camera system
// (same fork class as DrawPoints' baked ortho), so the line is a flat untextured constant-width band.
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

RenderCommand cookDrawClosedLines(CmdCookCtx& c) {
  RenderCommand rc;
  if (!c.points || c.count < 2) return rc;  // need ≥2 points for one segment
  RenderDrawItem it{c.points, c.count, 3.5f};
  it.kind = DrawKind::Lines;            // shares the Lines PSO + shader; closed flag selects the wrap
  it.lineClosed = true;                 // ← the only topology difference vs DrawLines
  float white[4] = {1.0f, 1.0f, 1.0f, 1.0f};
  cookVecN(c, "Color", white, 4, it.color);
  it.lineWidth = cookParam(c, "LineWidth", 0.02f);
  it.pointsPerShape = (uint32_t)std::lround(cookParam(c, "PointsPerShape", 0.0f));  // TiXL default 0
  rc.items.push_back(it);
  return rc;
}

void registerDrawClosedLinesOp() { registerCmdOp("DrawClosedLines", cookDrawClosedLines); }

namespace {

// CPU-fill a Points generator stub for the resident golden: writes a UNIT SQUARE (4 corners, CCW)
// into the driver-allocated output buffer. A closed loop over these 4 points draws all FOUR edges
// (incl. the right→top→...→back-to-first wrap); an open DrawLines would leave the closing edge dark.
bool g_genBugDropWrapCorner = false;  // injectBug(B): collapse the bag so the wrap edge can't form
void closedSquareGen(PointCookCtx& c) {
  if (!c.output || c.count == 0) return;
  SwPoint* dst = (SwPoint*)c.output->contents();
  // Square corners at ±s in object space (viewExtent 3.5 → well inside NDC). Order so consecutive
  // points are adjacent edges and the wrap (p3→p0) is the LEFT edge.
  const float s = 1.2f;
  const float cx[4] = {-s,  s,  s, -s};  // p0 BL, p1 BR, p2 TR, p3 TL (p3→p0 wrap = LEFT edge)
  const float cy[4] = {-s, -s,  s,  s};
  for (uint32_t i = 0; i < c.count; ++i) {
    dst[i] = SwPoint{};
    dst[i].Color = {1, 1, 1, 1};
    dst[i].Scale = {1, 1, 1};
    uint32_t k = i % 4;
    dst[i].Position = {cx[k], cy[k], 0.0f};
  }
  if (g_genBugDropWrapCorner) {
    // Bug(B): NaN p0 (bottom-left). The wrap segment p3→p0 (the LEFT edge — only ever drawn by the
    // CLOSED loop) touches p0, so it collapses offscreen (draw_lines.metal isnan guard) → the LEFT
    // edge goes dark → the wrap-edge probe fails. The RIGHT edge (p1→p2) has no NaN endpoint, so it
    // stays lit → it is the NaN-independent control. (Open DrawLines never draws p3→p0 at all, so
    // this same probe distinguishes closed-vs-open in the FLAT leg's lineClosed tooth.)
    if (c.count >= 4) dst[0].Position.x = NAN;
  }
}

Symbol atomicOp(const char* id, std::vector<SlotDef> ins, std::vector<SlotDef> outs) {
  Symbol s; s.id = id; s.name = id; s.atomic = true;
  s.inputDefs = std::move(ins); s.outputDefs = std::move(outs);
  return s;
}

}  // namespace

// ─────────────────────────────────────────────────────────────────────────────────────────────────
// Golden. TWO legs, both required (R-2 鐵律):
//   FLAT  : hand-build a RenderCommand (closed=true) over a CPU square buffer, run the RenderTarget
//           executor directly, read back. Asserts the CLOSING (wrap) edge is lit — the thing that
//           distinguishes DrawClosedLines from DrawLines. injectBug(A) clears lineClosed → open
//           polyline → the closing edge goes dark → FAIL.
//   RESIDENT (production path): build a real ResidentEvalGraph Gen(square)→DrawClosedLines→RenderTarget,
//           cook via pg.cookResident, read back pg.target(). Proves the cmd op + executor + shader
//           draw a closed loop on the SAME path the live app uses (frame_cook → cookResident).
//           injectBug(B) NaNs the wrap corner → the closing edge collapses → FAIL.
// ─────────────────────────────────────────────────────────────────────────────────────────────────
int runDrawClosedLinesSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  const uint32_t W = 256, H = 256;

  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue* q = dev->newCommandQueue();
  NS::Error* err = nullptr;
  MTL::Library* lib =
      dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
  if (!lib) {
    printf("[selftest-drawclosedlines] FAIL: no metallib\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }
  registerDrawClosedLinesOp();
  registerRenderTargetOp();

  // The square corners in NDC (viewExtent 3.5): ±1.2/3.5 ≈ ±0.343 → pixel ±0.343*128 ≈ ±44 from
  // center (128,128) → x/y ∈ {84, 172}. The LEFT edge (x≈84, y∈[84,172]) is the wrap (p3→p0) edge.
  const float s = 1.2f, viewExtent = 3.5f;
  auto ndcToPx = [&](float world) { return (int)std::lround((world / viewExtent) * 0.5f * (float)W + 0.5f * (float)W); };
  const int xL = ndcToPx(-s), xR = ndcToPx(s);
  const int yB = ndcToPx(-s), yT = ndcToPx(s);  // NB: +Y world → larger NDC y; pixel y is top-down in
  // getBytes (row 0 = top). We probe a band, so exact orientation doesn't matter — both the closing
  // (left) edge column and a known-open-too (bottom/top) edge row are inside the same square outline.

  // ── FLAT leg ───────────────────────────────────────────────────────────────────────────────────
  const uint32_t N = 4;
  MTL::Buffer* pts = dev->newBuffer((NS::UInteger)N * sizeof(SwPoint), MTL::ResourceStorageModeShared);
  SwPoint* d = (SwPoint*)pts->contents();
  const float fcx[4] = {-s,  s,  s, -s};  // BL, BR, TR, TL — same order as the resident gen
  const float fcy[4] = {-s, -s,  s,  s};
  for (uint32_t i = 0; i < N; ++i) { d[i] = SwPoint{}; d[i].Color = {1, 1, 1, 1}; d[i].Scale = {1, 1, 1};
    d[i].Position = {fcx[i], fcy[i], 0.0f}; }

  MTL::TextureDescriptor* td =
      MTL::TextureDescriptor::texture2DDescriptor(MTL::PixelFormatRGBA8Unorm, W, H, false);
  td->setUsage(MTL::TextureUsageRenderTarget | MTL::TextureUsageShaderRead);
  td->setStorageMode(MTL::StorageModeShared);
  MTL::Texture* tex = dev->newTexture(td);

  RenderCommand rc;
  RenderDrawItem it{pts, N, viewExtent};
  it.kind = DrawKind::Lines;
  it.color[0] = it.color[1] = it.color[2] = it.color[3] = 1.0f;
  it.lineWidth = 0.4f;                       // wide band so the edge crosses several pixels
  it.lineClosed = injectBug ? false : true;  // injectBug(A): open polyline → wrap edge dark
  it.pointsPerShape = 0;                     // single shape over all 4 points
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
  // Square outline edges at pixel cols/rows {xL,xR}×{yB,yT} (corners at 84/172, center 128). The
  // CLOSING edge is the LEFT column x≈xL (the wrap p3→p0 — drawn ONLY by the closed loop). The RIGHT
  // column x≈xR (p1→p2) is drawn in both open & closed AND has no NaN endpoint under bug(B) → the
  // NaN-independent control. Probe an edge MIDPOINT (between corners, where only that one edge runs).
  int midY = (yB + yT) / 2;
  auto edgeLit = [&](int x, int y, int rad) {
    for (int dx = -rad; dx <= rad; ++dx)
      for (int dy = -rad; dy <= rad; ++dy)
        if (lit(x + dx, y + dy)) return true;
    return false;
  };
  bool closingEdgeLit = edgeLit(xL, midY, 4);   // LEFT edge = the wrap (only closed draws it)
  bool controlEdgeLit = edgeLit(xR, midY, 4);   // RIGHT edge = always drawn + NaN-free (control)
  // PASS(flat) asserts the TRUE behavior unconditionally: both the control edge AND the closing
  // (wrap) edge must be lit. injectBug(A) drops lineClosed → open polyline → no p3→p0 → the LEFT
  // edge goes dark → this assertion FAILS (the tooth bites; the -bug run must exit non-zero).
  bool flatPass = controlEdgeLit && closingEdgeLit;

  pts->release(); tex->release();

  // ── RESIDENT (production) leg ────────────────────────────────────────────────────────────────────
  g_genBugDropWrapCorner = injectBug;
  registerPointOp("RadialPoints", closedSquareGen);  // a real registered gen, CPU-fills the square

  SymbolLibrary slib;
  slib.symbols["RadialPoints"] =
      atomicOp("RadialPoints", {{"Count", "Count", "Float", 4.0f}},
               {{"points", "points", "Points", 0.0f}});
  slib.symbols["DrawClosedLines"] =
      atomicOp("DrawClosedLines",
               {{"points", "points", "Points", 0.0f},
                {"Color.x", "Color", "Float", 1.0f}, {"Color.y", "Color.y", "Float", 1.0f},
                {"Color.z", "Color.z", "Float", 1.0f}, {"Color.w", "Color.w", "Float", 1.0f},
                {"LineWidth", "LineWidth", "Float", 0.02f},
                {"PointsPerShape", "PointsPerShape", "Float", 0.0f}},
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
  SymbolChild g; g.id = 1; g.symbolId = "RadialPoints"; g.overrides["Count"] = 4.0f;
  SymbolChild dc; dc.id = 2; dc.symbolId = "DrawClosedLines"; dc.overrides["LineWidth"] = 0.4f;
  SymbolChild r; r.id = 3; r.symbolId = "RenderTarget"; r.overrides["Resolution"] = 0.0f;  // WindowFollow
  root.children = {g, dc, r};
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

  bool resClosingLit = false, resControlLit = false, resHasPixels = false;
  if (rtex) {
    uint32_t rw = (uint32_t)rtex->width(), rh = (uint32_t)rtex->height();
    std::vector<uint8_t> rpx((size_t)rw * rh * 4, 0);
    rtex->getBytes(rpx.data(), rw * 4, MTL::Region::Make2D(0, 0, rw, rh), 0);
    auto rlit = [&](int x, int y) {
      if (x < 0 || y < 0 || x >= (int)rw || y >= (int)rh) return false;
      size_t i = ((size_t)y * rw + x) * 4;
      return rpx[i] > 30 || rpx[i + 1] > 30 || rpx[i + 2] > 30;
    };
    auto rEdge = [&](int x, int y, int rad) {
      for (int dx = -rad; dx <= rad; ++dx)
        for (int dy = -rad; dy <= rad; ++dy)
          if (rlit(x + dx, y + dy)) return true;
      return false;
    };
    resClosingLit = rEdge(xL, midY, 4);   // LEFT (wrap p3→p0) — dark under bug(B) p0-NaN
    resControlLit = rEdge(xR, midY, 4);   // RIGHT (p1→p2) — NaN-free control
    for (size_t i = 0; i < (size_t)rw * rh && !resHasPixels; ++i)
      if (rpx[i * 4] > 30 || rpx[i * 4 + 1] > 30 || rpx[i * 4 + 2] > 30) resHasPixels = true;
  }
  // PASS(resident) asserts the TRUE behavior unconditionally: non-empty target + control edge lit +
  // closing (wrap) edge lit. injectBug(B) NaNs p0 → the wrap segment p3→p0 collapses → the LEFT edge
  // goes dark → this assertion FAILS (the production-path tooth bites; -bug exits non-zero).
  bool resPass = resHasPixels && resControlLit && resClosingLit;

  bool pass = flatPass && resPass;
  printf("[selftest-drawclosedlines] FLAT: controlEdge=%d closingEdge=%d(closed=%d) -> %s | "
         "RESIDENT: hasPixels=%d controlEdge=%d closingEdge=%d -> %s | %s\n",
         controlEdgeLit, closingEdgeLit, injectBug ? 0 : 1, flatPass ? "ok" : "BAD",
         resHasPixels, resControlLit, resClosingLit, resPass ? "ok" : "BAD",
         pass ? "PASS" : "FAIL");

  g_genBugDropWrapCorner = false;
  lib->release(); q->release(); dev->release(); pool->release();
  return pass ? 0 : 1;
}

}  // namespace sw

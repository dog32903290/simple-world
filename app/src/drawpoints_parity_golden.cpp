// drawpoints_parity_golden — --selftest-drawpoints-parity. PARITY golden for DrawPoints (v1), the
// ONE 修偏差 (deviation-fix) case in the draw family: 柏為 found "DrawPoints 跟 TiXL 不一樣".
//
// ── THE DEVIATION (scout-confirmed) ──────────────────────────────────────────────────────────────────
// TiXL DrawPoints (DrawPoints.cs + DrawPoints.t3 + DrawPoints.hlsl) draws each Point as a 6-vert
// screen-facing QUAD SPRITE whose size = PointSize (.t3 default 0.1) × (ScaleFactor==F1 ? W : 1),
// tinted by Color (.t3 default white) × Point.Color. DrawPoints2 is the SAME shader with a Radius knob.
// sw's pre-fix cookDrawPoints (point_ops.cpp) emitted DrawKind::Points — a degenerate hardcoded 4px
// point (draw_points.metal: o.point_size = 4.0f, color = pt.Color, NO PointSize, NO Color uniform).
// So sw's DrawPoints (a) did not respond to PointSize and (b) did not tint by Color → a definition-level
// deviation from TiXL. The fix routes cookDrawPoints through DrawKind::Points2 (the faithful quad-sprite
// shader already ported in draw_points2.metal) wired to the real PointSize/Color/ScaleFactor params.
//
// TiXL GROUND-TRUTH ANCHORS (鐵律2): DrawPoints.t3 DefaultValue — PointSize=0.1, ScaleFactor=0(None),
// BlendMode=0(Normal), Color=(1,1,1,1). DrawPoints.hlsl: quadPos.xy*0.10*(PointSize*sizeFxFactor*Scale),
// sizeFxFactor = ScaleFX==1(F1) ? FX1(W) : 1. The v1 PointSize feeds the shader PointSize DIRECTLY (no
// ×10.8 — that ×10.8 is DrawPoints2's Radius→Multiply chain, a DrawPoints2-only fork).
//
// ── RED-FIRST TEETH (鐵律1) ──────────────────────────────────────────────────────────────────────────
//  TOOTH A (PointSize-response): cook a single center Point through the PRODUCTION resident path
//    Gen(point)→DrawPoints→RenderTarget twice — once PointSize=0.4 (big), once PointSize=0.05 (small) —
//    count the lit texels in a center window. Assert areaLit(0.4) ≫ areaLit(0.05).
//    Against the pre-fix 4px dead point BOTH cooks draw the same 4px dot → areaBig ≈ areaSmall → RED.
//    Post-fix the sprite tracks PointSize → areaBig ≫ areaSmall → GREEN.
//  TOOTH B (Color-tint): cook the center Point with Color=(1,0,0) → assert center R bright, G/B dark.
//    Pre-fix draw_points.metal returns pt.Color (white point) ignoring the Color uniform → G/B bright → RED.
//    Post-fix DrawKind::Points2 returns P.color*pt.Color = red → GREEN.
//  injectBug (post-fix regression latch): forces the cook to emit the OLD DrawKind::Points 4px dead point
//    (drawPointsBugForceV1ForTest) → PointSize stops responding AND Color stops tinting → BOTH teeth RED.
//
// ── pixel-deferred-windows (鐵律5 + cross-GPU honesty) ───────────────────────────────────────────────
// This is a TIGHTNESS / PROPERTY probe (area-ordering + channel-dominance), NOT a byte-exact pixel
// compare: TiXL renders the .hlsl on DX11, sw on Metal — pixel-exact cross-GPU equality is a separate
// deferred lane (pixel-deferred-windows). The scene params are pinned explicitly (鐵律5): single Point
// at world origin, viewExtent 3.5, 256² target, PointSize 0.4 vs 0.05, Color (1,0,0)/(1,1,1).
#include "runtime/point_ops.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/compound_graph.h"       // SymbolLibrary / Symbol / SymbolChild
#include "runtime/point_graph.h"          // PointGraph, registerPointOp/registerCmdOp
#include "runtime/render_command.h"       // drawPointsBugForceV1ForTest
#include "runtime/resident_eval_graph.h"  // buildEvalGraph / kSymbolBoundary
#include "runtime/tixl_point.h"           // SwPoint + EvaluationContext

#ifndef SW_SHADER_METALLIB
#define SW_SHADER_METALLIB "shaders.metallib"
#endif

namespace sw {

namespace {

// CPU-fill a single-Point gen at world origin with a known color (set per cook below). DrawPoints
// expands it into a sprite whose AREA tracks PointSize × (ScaleFactor==F1 ? W : 1).
float g_genColorR = 1.0f, g_genColorG = 1.0f, g_genColorB = 1.0f;
void centerPointGen(PointCookCtx& c) {
  if (!c.output || c.count == 0) return;
  SwPoint* dst = (SwPoint*)c.output->contents();
  for (uint32_t i = 0; i < c.count; ++i) {
    dst[i] = SwPoint{};
    dst[i].Color = {g_genColorR, g_genColorG, g_genColorB, 1.0f};
    dst[i].Scale = {1, 1, 1};
    dst[i].FX1 = 1.0f;                      // W = 1
    dst[i].Position = {0.0f, 0.0f, 0.0f};   // center
  }
}

Symbol atomicOp(const char* id, std::vector<SlotDef> ins, std::vector<SlotDef> outs) {
  Symbol s; s.id = id; s.name = id; s.atomic = true;
  s.inputDefs = std::move(ins); s.outputDefs = std::move(outs);
  return s;
}

// Build the production resident rig Gen(point)→DrawPoints→RenderTarget and cook it, returning the
// readback target. pointSize / color route through DrawPoints' REAL params (the cook-through path
// that exercises cookDrawPoints — 鐵律4). The Gen's per-point Color is white so the tint isolates to
// DrawPoints' Color uniform.
struct CookResult { std::vector<uint8_t> px; uint32_t w = 0, h = 0; };
CookResult cookDrawPointsRig(MTL::Device* dev, MTL::Library* lib, MTL::CommandQueue* q,
                             uint32_t W, uint32_t H, float pointSize, float cr, float cg, float cb) {
  g_genColorR = 1.0f; g_genColorG = 1.0f; g_genColorB = 1.0f;  // white point → tint comes from DrawPoints.Color
  registerPointOp("RadialPoints", centerPointGen);

  SymbolLibrary slib;
  slib.symbols["RadialPoints"] =
      atomicOp("RadialPoints", {{"Count", "Count", "Float", 1.0f}},
               {{"points", "points", "Points", 0.0f}});
  // DrawPoints params mirror the post-fix NodeSpec: Color(Vec4) + PointSize + ScaleFactor + BlendMode.
  slib.symbols["DrawPoints"] =
      atomicOp("DrawPoints",
               {{"points", "points", "Points", 0.0f},
                {"PointSize", "PointSize", "Float", 0.1f},
                {"Color.x", "Color", "Float", 1.0f}, {"Color.y", "Color.y", "Float", 1.0f},
                {"Color.z", "Color.z", "Float", 1.0f}, {"Color.w", "Color.w", "Float", 1.0f},
                {"ScaleFactor", "ScaleFactor", "Float", 0.0f},
                {"BlendMode", "BlendMode", "Float", 0.0f}},
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
  SymbolChild g; g.id = 1; g.symbolId = "RadialPoints"; g.overrides["Count"] = 1.0f;
  SymbolChild dp; dp.id = 2; dp.symbolId = "DrawPoints";
  dp.overrides["PointSize"] = pointSize;
  dp.overrides["Color"] = cr; dp.overrides["Color.y"] = cg; dp.overrides["Color.z"] = cb;
  dp.overrides["Color.w"] = 1.0f;
  SymbolChild r; r.id = 3; r.symbolId = "RenderTarget"; r.overrides["Resolution"] = 0.0f;
  root.children = {g, dp, r};
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

  CookResult out;
  if (rtex) {
    out.w = (uint32_t)rtex->width(); out.h = (uint32_t)rtex->height();
    out.px.assign((size_t)out.w * out.h * 4, 0);
    rtex->getBytes(out.px.data(), out.w * 4, MTL::Region::Make2D(0, 0, out.w, out.h), 0);
  }
  return out;
}

// Count lit texels (any channel >30) in a ±half window around the target center.
int areaLit(const CookResult& r, int half) {
  if (r.px.empty()) return 0;
  int cx = (int)r.w / 2, cy = (int)r.h / 2, n = 0;
  for (int y = cy - half; y <= cy + half; ++y)
    for (int x = cx - half; x <= cx + half; ++x) {
      if (x < 0 || y < 0 || x >= (int)r.w || y >= (int)r.h) continue;
      size_t i = ((size_t)y * r.w + x) * 4;
      if (r.px[i] > 30 || r.px[i + 1] > 30 || r.px[i + 2] > 30) ++n;
    }
  return n;
}

// Read the center texel's R/G/B (for the tint tooth).
void centerRGB(const CookResult& r, int& cr, int& cg, int& cb) {
  cr = cg = cb = 0;
  if (r.px.empty()) return;
  size_t i = (((size_t)(r.h / 2) * r.w + (r.w / 2)) * 4);
  cr = r.px[i]; cg = r.px[i + 1]; cb = r.px[i + 2];
}

}  // namespace

int runDrawPointsParitySelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  const uint32_t W = 256, H = 256;

  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue* q = dev->newCommandQueue();
  NS::Error* err = nullptr;
  MTL::Library* lib =
      dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
  if (!lib) {
    printf("[selftest-drawpoints-parity] FAIL: no metallib\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }
  registerCmdOp("DrawPoints", cookDrawPoints);  // Points → Command (the op under test)
  registerRenderTargetOp();

  // injectBug: force the cook to emit the OLD DrawKind::Points 4px dead point → the deviation returns.
  drawPointsBugForceV1ForTest() = injectBug;

  // ── TOOTH A: PointSize-response (big vs small sprite area), white tint ────────────────────────────
  // Sprite NDC half-extent = 0.10 * PointSize / viewExtent (draw_points2.metal). The faithful sprite at
  // the .t3 default PointSize=0.1 is genuinely tiny (~1px), so the tooth uses two CLEARLY-separated sizes:
  // PointSize=2.0 (~15px sprite, areaLit~200) vs 0.2 (~3px, areaLit~few). The ABSOLUTE size tracks TiXL's
  // formula; the tooth asserts the ORDERING (big ≫ small), which a fixed 4px dead point can NEVER produce.
  CookResult big   = cookDrawPointsRig(dev, lib, q, W, H, /*PointSize=*/2.0f, 1, 1, 1);
  CookResult small = cookDrawPointsRig(dev, lib, q, W, H, /*PointSize=*/0.2f, 1, 1, 1);
  int areaBig = areaLit(big, 32), areaSmall = areaLit(small, 32);
  // The big sprite must cover MUCH more area than the small one. A 4px dead point (pre-fix / -bug)
  // ignores PointSize → both == 16 → the ordering collapses → RED.
  bool toothA = areaBig > areaSmall + 40 && areaBig > 60;

  // ── TOOTH B: Color tint (red point), PointSize 2.0 so the center is solidly filled ────────────────
  CookResult red = cookDrawPointsRig(dev, lib, q, W, H, /*PointSize=*/2.0f, 1, 0, 0);
  int rr, rg, rb; centerRGB(red, rr, rg, rb);
  // Red tint → center R bright, G/B dark. Pre-fix returns the white pt.Color (Color uniform ignored)
  // → G/B bright → RED. (Threshold: R clearly dominant, G/B near black.)
  bool toothB = rr > 180 && rg < 60 && rb < 60;

  bool pass = toothA && toothB;
  printf("[selftest-drawpoints-parity] TOOTH-A PointSize areaBig=%d areaSmall=%d -> %s | "
         "TOOTH-B tint centerRGB=(%d,%d,%d) -> %s | %s%s\n",
         areaBig, areaSmall, toothA ? "ok" : "BAD",
         rr, rg, rb, toothB ? "ok" : "BAD",
         pass ? "PASS" : "FAIL", injectBug ? " [injectBug]" : "");

  drawPointsBugForceV1ForTest() = false;
  lib->release(); q->release(); dev->release(); pool->release();
  return pass ? 0 : 1;
}

}  // namespace sw

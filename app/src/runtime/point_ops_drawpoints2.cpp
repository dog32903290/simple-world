// DrawPoints2 command op (Points → Command) — TiXL Operators/Lib/point/draw/DrawPoints2.cs
// (compound → DrawPoints.hlsl). The Radius-driven sibling of DrawPoints: it draws the point bag as
// screen-facing quad sprites whose size is the Radius parameter (TiXL routes Radius → Multiply ×10.8
// → the shader's PointSize) instead of v1's PointSize, and (UseWForSize default true) scales each
// sprite by the point's W (FX1, the shader's ScaleFX==1 path). Color tints (Color * Point.Color).
//
// It rides DrawKind::Points2 — its OWN shader (draw_points2.metal) + PSO + params struct — so the v1
// DrawPoints path (DrawKind::Points, bare-float viewExtent binding) is byte-identical. The host
// pre-multiplies Radius*10.8 into `size`; the shader applies the 0.10 quad-unit + the W-scale,
// matching DrawPoints.hlsl `quadPos.xy * 0.10 * (PointSize * sizeFxFactor)`.
//
// Params mirror DrawPoints2.t3 defaults: Color (Vec4 white) + Radius (0.01) + UseWForSize (true).
// FORKS (named, inherited from DrawPoints' baked-ortho class — sw has NO camera): TiXL's
// camera/Transforms (ObjectToClipSpace), Fog, FadeNearest (camera-Z fade), the optional Texture_
// sprite atlas (default white-dot round mask → here a flat SQUARE), BlendMode, ZTest/ZWrite are
// dropped. Texture_ port deferred: it needs the asset-bind/Texture2D-into-points seam (not built)
// → omitted (DrawPoints2 with no texture is the white-square sprite). Named fork: NO round-dot mask.
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

// TiXL DrawPoints2.t3: Radius → Multiply B=10.8 → the shader PointSize. We apply the ×10.8 here so
// the item's `size` is the shader PointSize verbatim (the executor binds it as DrawPoint2Params.pointSize).
static constexpr float kDrawPoints2RadiusToSize = 10.8f;

RenderCommand cookDrawPoints2(CmdCookCtx& c) {
  RenderCommand rc;
  if (!c.points || c.count == 0) return rc;
  RenderDrawItem it{c.points, c.count, 3.5f};
  it.kind = DrawKind::Points2;
  float white[4] = {1.0f, 1.0f, 1.0f, 1.0f};
  cookVecN(c, "Color", white, 4, it.color);
  float radius = cookParam(c, "Radius", 0.01f);          // TiXL Radius (.t3 default 0.01)
  it.size = radius * kDrawPoints2RadiusToSize;            // → shader PointSize (TiXL Multiply ×10.8)
  it.useWForSize = cookParam(c, "UseWForSize", 1.0f) >= 0.5f;  // TiXL UseWForSize (.t3 default true)
  rc.items.push_back(it);
  return rc;
}

void registerDrawPoints2Op() { registerCmdOp("DrawPoints2", cookDrawPoints2); }

namespace {

// CPU-fill a Points generator stub for the resident golden: a SINGLE point at world origin with a
// known W (FX1). DrawPoints2 expands it into a sprite whose AREA is Radius*10.8-sized × W. The bug
// path NaNs the point (collapse offscreen → no sprite).
bool g_genBugDropPoint = false;  // injectBug(B): NaN the point so the sprite collapses
void singlePointGen(PointCookCtx& c) {
  if (!c.output || c.count == 0) return;
  SwPoint* dst = (SwPoint*)c.output->contents();
  for (uint32_t i = 0; i < c.count; ++i) {
    dst[i] = SwPoint{};
    dst[i].Color = {1, 1, 1, 1};
    dst[i].Scale = {1, 1, 1};
    dst[i].FX1 = 1.0f;                 // W = 1 (UseWForSize scales by this → factor 1)
    dst[i].Position = {0.0f, 0.0f, 0.0f};  // center
  }
  if (g_genBugDropPoint && c.count >= 1) dst[0].Position.x = NAN;  // collapse the only point
}

Symbol atomicOp(const char* id, std::vector<SlotDef> ins, std::vector<SlotDef> outs) {
  Symbol s; s.id = id; s.name = id; s.atomic = true;
  s.inputDefs = std::move(ins); s.outputDefs = std::move(outs);
  return s;
}

}  // namespace

// ─────────────────────────────────────────────────────────────────────────────────────────────────
// Golden. TWO legs, both required (R-2 鐵律):
//   FLAT  : hand-build a RenderCommand (DrawKind::Points2) over a CPU single-point buffer at center,
//           run the RenderTarget executor directly, read back. Asserts the sprite QUAD covers an AREA
//           (a Radius-sized sprite, several texels) AROUND the center — the DrawPoints2 size knob.
//           injectBug(A) sets Radius=0 → zero-area quad → no covered texels → FAIL.
//   RESIDENT (production path): build a real ResidentEvalGraph Gen(point)→DrawPoints2→RenderTarget,
//           cook via pg.cookResident, read back pg.target(). Proves the cmd op + executor + shader
//           draw a sprite on the SAME path the live app uses (frame_cook → cookResident).
//           injectBug(B) NaNs the point → the sprite collapses → FAIL.
// ─────────────────────────────────────────────────────────────────────────────────────────────────
int runDrawPoints2SelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  const uint32_t W = 256, H = 256;

  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue* q = dev->newCommandQueue();
  NS::Error* err = nullptr;
  MTL::Library* lib =
      dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
  if (!lib) {
    printf("[selftest-drawpoints2] FAIL: no metallib\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }
  registerDrawPoints2Op();
  registerRenderTargetOp();

  const float viewExtent = 3.5f;
  const uint32_t cx = W / 2, cy = H / 2;  // a single point at world origin maps to the texture center

  // ── FLAT leg ───────────────────────────────────────────────────────────────────────────────────
  const uint32_t N = 1;
  MTL::Buffer* pts = dev->newBuffer((NS::UInteger)N * sizeof(SwPoint), MTL::ResourceStorageModeShared);
  SwPoint* d = (SwPoint*)pts->contents();
  d[0] = SwPoint{}; d[0].Color = {1, 1, 1, 1}; d[0].Scale = {1, 1, 1}; d[0].FX1 = 1.0f;
  d[0].Position = {0.0f, 0.0f, 0.0f};

  MTL::TextureDescriptor* td =
      MTL::TextureDescriptor::texture2DDescriptor(MTL::PixelFormatRGBA8Unorm, W, H, false);
  td->setUsage(MTL::TextureUsageRenderTarget | MTL::TextureUsageShaderRead);
  td->setStorageMode(MTL::StorageModeShared);
  MTL::Texture* tex = dev->newTexture(td);

  RenderCommand rc;
  RenderDrawItem it{pts, N, viewExtent};
  it.kind = DrawKind::Points2;
  it.color[0] = it.color[1] = it.color[2] = it.color[3] = 1.0f;
  // size = the shader PointSize. injectBug(A): Radius 0 → size 0 → zero-area quad → no covered texels.
  it.size = injectBug ? 0.0f : (0.20f * kDrawPoints2RadiusToSize);  // a Radius giving a clearly-visible sprite
  it.useWForSize = true;
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
  // Count lit texels in a window around the center: a real Radius sprite covers AREA (>1 texel).
  int areaLit = 0;
  for (int y = (int)cy - 24; y <= (int)cy + 24; ++y)
    for (int x = (int)cx - 24; x <= (int)cx + 24; ++x)
      if (lit(x, y)) ++areaLit;
  // PASS(flat) asserts the sprite covers a real area. injectBug(A) Radius=0 → zero-area → areaLit≈0 → FAIL.
  bool flatPass = areaLit > 8;

  pts->release(); tex->release();

  // ── RESIDENT (production) leg ────────────────────────────────────────────────────────────────────
  g_genBugDropPoint = injectBug;
  registerPointOp("RadialPoints", singlePointGen);  // a real registered gen, CPU-fills one center point

  SymbolLibrary slib;
  slib.symbols["RadialPoints"] =
      atomicOp("RadialPoints", {{"Count", "Count", "Float", 1.0f}},
               {{"points", "points", "Points", 0.0f}});
  slib.symbols["DrawPoints2"] =
      atomicOp("DrawPoints2",
               {{"points", "points", "Points", 0.0f},
                {"Color.x", "Color", "Float", 1.0f}, {"Color.y", "Color.y", "Float", 1.0f},
                {"Color.z", "Color.z", "Float", 1.0f}, {"Color.w", "Color.w", "Float", 1.0f},
                {"Radius", "Radius", "Float", 0.01f},
                {"UseWForSize", "UseWForSize", "Float", 1.0f}},
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
  SymbolChild dp; dp.id = 2; dp.symbolId = "DrawPoints2"; dp.overrides["Radius"] = 0.20f;  // visible sprite
  SymbolChild r; r.id = 3; r.symbolId = "RenderTarget"; r.overrides["Resolution"] = 0.0f;  // WindowFollow
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

  int resAreaLit = 0;
  bool resHasPixels = false;
  if (rtex) {
    uint32_t rw = (uint32_t)rtex->width(), rh = (uint32_t)rtex->height();
    std::vector<uint8_t> rpx((size_t)rw * rh * 4, 0);
    rtex->getBytes(rpx.data(), rw * 4, MTL::Region::Make2D(0, 0, rw, rh), 0);
    auto rlit = [&](int x, int y) {
      if (x < 0 || y < 0 || x >= (int)rw || y >= (int)rh) return false;
      size_t i = ((size_t)y * rw + x) * 4;
      return rpx[i] > 30 || rpx[i + 1] > 30 || rpx[i + 2] > 30;
    };
    int rcx = (int)rw / 2, rcy = (int)rh / 2;
    for (int y = rcy - 24; y <= rcy + 24; ++y)
      for (int x = rcx - 24; x <= rcx + 24; ++x)
        if (rlit(x, y)) ++resAreaLit;
    for (size_t i = 0; i < (size_t)rw * rh && !resHasPixels; ++i)
      if (rpx[i * 4] > 30 || rpx[i * 4 + 1] > 30 || rpx[i * 4 + 2] > 30) resHasPixels = true;
  }
  // PASS(resident) asserts a real sprite on the production path. injectBug(B) NaNs the point → no
  // sprite → resAreaLit≈0 → FAIL.
  bool resPass = resHasPixels && resAreaLit > 8;

  bool pass = flatPass && resPass;
  printf("[selftest-drawpoints2] FLAT: areaLit=%d(need>8, size=%.2f) -> %s | "
         "RESIDENT: hasPixels=%d areaLit=%d -> %s | %s\n",
         areaLit, it.size, flatPass ? "ok" : "BAD",
         resHasPixels, resAreaLit, resPass ? "ok" : "BAD",
         pass ? "PASS" : "FAIL");

  g_genBugDropPoint = false;
  lib->release(); q->release(); dev->release(); pool->release();
  return pass ? 0 : 1;
}

}  // namespace sw

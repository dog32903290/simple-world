// DrawLines command op (Points → Command) — TiXL Operators/Lib/point/draw/DrawLines.cs.
// Like DrawPoints it emits a 1-item RenderCommand describing how to draw the upstream bag; no
// render pass here (the executor cookRenderTarget rasterizes the chain). The item's DrawKind=Lines
// tells the executor to connect Points[i]→Points[i+1] into screen-space quads (draw_lines.metal).
//
// Params mirror DrawLines.t3 defaults: Color (Vec4, white) + LineWidth (0.02). FORKS (named):
// TiXL's camera/texture/UV/ShrinkWithDistance/Fog/Blend/ZTest plumbing is dropped — we have no
// camera system (same fork class as DrawPoints' baked ortho); the line is a flat untextured band.
#include "runtime/point_ops.h"

#include "runtime/point_graph.h"     // CmdCookCtx, registerCmdOp, cookParam/cookVecN
#include "runtime/render_command.h"  // RenderCommand / RenderDrawItem / DrawKind

#include <cstdio>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/graph.h"      // Graph/Node (selftest)
#include "runtime/tixl_point.h"  // SwPoint (selftest)

#ifndef SW_SHADER_METALLIB
#define SW_SHADER_METALLIB "shaders.metallib"
#endif

namespace sw {

RenderCommand cookDrawLines(CmdCookCtx& c) {
  RenderCommand rc;
  if (!c.points || c.count < 2) return rc;  // need ≥2 points for one segment
  RenderDrawItem it{c.points, c.count, 3.5f};
  it.kind = DrawKind::Lines;
  float white[4] = {1.0f, 1.0f, 1.0f, 1.0f};
  cookVecN(c, "Color", white, 4, it.color);
  it.lineWidth = cookParam(c, "LineWidth", 0.02f);
  rc.items.push_back(it);
  return rc;
}

void registerDrawLinesOp() { registerCmdOp("DrawLines", cookDrawLines); }

// Golden: CPU-fill a horizontal row of points across the view, run DrawLines → RenderTarget, read
// back. ASSERT pixels are lit BETWEEN the two endpoints (segment body, not just at the points), AND
// that a W=NaN break point splits the polyline (the gap column past the break is dark). injectBug
// sets LineWidth=0 → zero-area quads → nothing between the points → FAIL.
int runDrawLinesSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  const uint32_t W = 256, H = 256;

  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue* q = dev->newCommandQueue();
  NS::Error* err = nullptr;
  MTL::Library* lib =
      dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
  if (!lib) {
    printf("[selftest-drawlines] FAIL: no metallib\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }
  registerDrawLinesOp();
  registerRenderTargetOp();

  // Two segments: A=(-2,0) — B=(0,0) — [BREAK at B] — C=(0,0)? Instead build 4 points:
  //   p0=(-2,0)  p1=(-0.5,0)   gap test point p2=(+0.5,0) [W=NaN -> break before it]  p3=(+2,0)
  // With FX1(p2)=NaN, the segment p1→p2 AND p2→p3 collapse → the right half (x>0) stays dark,
  // proving the separator. The left segment p0→p1 (x in [-2,-0.5]) stays lit.
  const uint32_t N = 4;
  MTL::Buffer* pts = dev->newBuffer((NS::UInteger)N * sizeof(SwPoint), MTL::ResourceStorageModeShared);
  SwPoint* d = (SwPoint*)pts->contents();
  for (uint32_t i = 0; i < N; ++i) { d[i] = SwPoint{}; d[i].Color = {1, 1, 1, 1}; d[i].Scale = {1, 1, 1}; }
  d[0].Position = {-2.0f, 0.0f, 0.0f};
  d[1].Position = {-0.5f, 0.0f, 0.0f};
  d[2].Position = { 0.5f, 0.0f, 0.0f}; d[2].FX1 = 0.0f / 0.0f;  // W=NaN break marker
  d[3].Position = { 2.0f, 0.0f, 0.0f};

  MTL::TextureDescriptor* td =
      MTL::TextureDescriptor::texture2DDescriptor(MTL::PixelFormatRGBA8Unorm, W, H, false);
  td->setUsage(MTL::TextureUsageRenderTarget | MTL::TextureUsageShaderRead);
  td->setStorageMode(MTL::StorageModeShared);
  MTL::Texture* tex = dev->newTexture(td);

  RenderCommand rc;
  RenderDrawItem it{pts, N, 3.5f};
  it.kind = DrawKind::Lines;
  it.color[0] = it.color[1] = it.color[2] = it.color[3] = 1.0f;
  it.lineWidth = injectBug ? 0.0f : 0.4f;  // wide so the band crosses several pixel rows
  rc.items.push_back(it);

  TexCookCtx c;
  c.dev = dev; c.lib = lib; c.queue = q;
  c.nodeId = 1; c.command = &rc; c.output = tex;
  cookRenderTarget(c);

  std::vector<uint8_t> px((size_t)W * H * 4, 0);
  tex->getBytes(px.data(), W * 4, MTL::Region::Make2D(0, 0, W, H), 0);
  auto lit = [&](uint32_t x, uint32_t y) {
    size_t i = ((size_t)y * W + x) * 4;
    return px[i] > 30 || px[i + 1] > 30 || px[i + 2] > 30;
  };
  // Sample the mid row band. Left half (x in [-2,-0.5] → px ~[16,96]) must be lit (segment body);
  // right half (x>0 → px>128) must be dark (broken segments collapsed).
  uint32_t midY = H / 2;
  int leftLit = 0, rightLit = 0;
  for (uint32_t x = 24; x < 88; ++x)
    for (uint32_t y = midY - 8; y <= midY + 8; ++y)
      if (lit(x, y)) { ++leftLit; break; }
  for (uint32_t x = 160; x < 240; ++x)
    for (uint32_t y = midY - 8; y <= midY + 8; ++y)
      if (lit(x, y)) { ++rightLit; break; }

  bool pass = leftLit > 30 && rightLit < 4;
  printf("[selftest-drawlines] leftLit=%d(need>30, segment body) rightLit=%d(need<4, break) -> %s\n",
         leftLit, rightLit, pass ? "PASS" : "FAIL");

  pts->release(); tex->release();
  lib->release(); q->release(); dev->release(); pool->release();
  return pass ? 0 : 1;
}

}  // namespace sw

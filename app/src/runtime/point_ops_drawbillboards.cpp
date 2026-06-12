// DrawBillboards command op (Points → Command) — TiXL Operators/Lib/point/draw/DrawBillboards.cs.
// Emits a 1-item RenderCommand (DrawKind=Billboards); the executor cookRenderTarget expands each
// Point into a 6-vert screen-facing quad sprite (draw_billboards.metal).
//
// Params mirror DrawBillboards.t3 defaults: Scale (1.0) + Color (Vec4, white). FORKS (named):
// TiXL's camera/atlas/sprite-texture/random-scatter/rotation/curves plumbing is dropped — a flat
// untextured screen-facing quad (same camera-less fork as DrawPoints). Per-point Scale.xy stretch
// is kept (TiXL UsePointScale default true).
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

RenderCommand cookDrawBillboards(CmdCookCtx& c) {
  RenderCommand rc;
  if (!c.points || c.count == 0) return rc;
  RenderDrawItem it{c.points, c.count, 3.5f};
  it.kind = DrawKind::Billboards;
  float white[4] = {1.0f, 1.0f, 1.0f, 1.0f};
  cookVecN(c, "Color", white, 4, it.color);
  it.size = cookParam(c, "Scale", 1.0f);
  rc.items.push_back(it);
  return rc;
}

void registerDrawBillboardsOp() { registerCmdOp("DrawBillboards", cookDrawBillboards); }

// Golden: a SINGLE point at the origin, run DrawBillboards → RenderTarget, read back. ASSERT the
// quad covers an AREA (> a single pixel — a point would light ~1px; a billboard fills a region).
// injectBug sets Scale=0 → zero-area quad → ~no lit pixels → FAIL.
int runDrawBillboardsSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  const uint32_t W = 256, H = 256;

  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue* q = dev->newCommandQueue();
  NS::Error* err = nullptr;
  MTL::Library* lib =
      dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
  if (!lib) {
    printf("[selftest-drawbillboards] FAIL: no metallib\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }
  registerDrawBillboardsOp();
  registerRenderTargetOp();

  const uint32_t N = 1;
  MTL::Buffer* pts = dev->newBuffer((NS::UInteger)N * sizeof(SwPoint), MTL::ResourceStorageModeShared);
  SwPoint* d = (SwPoint*)pts->contents();
  d[0] = SwPoint{};
  d[0].Position = {0.0f, 0.0f, 0.0f};
  d[0].Color = {1, 1, 1, 1};
  d[0].Scale = {1, 1, 1};

  MTL::TextureDescriptor* td =
      MTL::TextureDescriptor::texture2DDescriptor(MTL::PixelFormatRGBA8Unorm, W, H, false);
  td->setUsage(MTL::TextureUsageRenderTarget | MTL::TextureUsageShaderRead);
  td->setStorageMode(MTL::StorageModeShared);
  MTL::Texture* tex = dev->newTexture(td);

  RenderCommand rc;
  RenderDrawItem it{pts, N, 3.5f};
  it.kind = DrawKind::Billboards;
  it.color[0] = it.color[1] = it.color[2] = it.color[3] = 1.0f;
  // Big sprite so the quad covers many pixels (0.010 unit × size / viewExtent). Scale 150 →
  // half ~= 150*0.010/3.5 ≈ 0.43 NDC ≈ 110px half-extent.
  it.size = injectBug ? 0.0f : 150.0f;
  rc.items.push_back(it);

  TexCookCtx c;
  c.dev = dev; c.lib = lib; c.queue = q;
  c.nodeId = 1; c.command = &rc; c.output = tex;
  cookRenderTarget(c);

  std::vector<uint8_t> px((size_t)W * H * 4, 0);
  tex->getBytes(px.data(), W * 4, MTL::Region::Make2D(0, 0, W, H), 0);
  int nonBlack = 0;
  for (size_t i = 0; i < (size_t)W * H; ++i)
    if (px[i * 4] > 30 || px[i * 4 + 1] > 30 || px[i * 4 + 2] > 30) ++nonBlack;

  // A point would light ~1-4 px; a billboard quad fills a region. > 100 px = real area.
  bool pass = nonBlack > 100;
  printf("[selftest-drawbillboards] nonBlack=%d(need>100, quad area) -> %s\n",
         nonBlack, pass ? "PASS" : "FAIL");

  pts->release(); tex->release();
  lib->release(); q->release(); dev->release(); pool->release();
  return pass ? 0 : 1;
}

}  // namespace sw

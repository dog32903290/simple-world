// VoronoiCells image-filter texture op (lane image_filter).
// TiXL authority: external/tixl/Operators/Lib/image/fx/stylize/VoronoiCells.cs (ports) +
// VoronoiCells.t3 (defaults) + Assets/shaders/img/fx/VoronoiCells.hlsl (kernel).
//
// Single-pass port: cookVoronoiCells reads c.inputTexture (upstream Texture2D via the gather
// direct-through, used as the feature-point + cell-colour field), runs one fullscreen pass of
// voronoicells_vs/_fs, writes c.output. No upstream texture wired: clear output to black.
//
// The op binds TWO constant buffers: b0 = VoronoiCellsParams (EdgeColor/Background/Scale/
// LineWidth/Phase), and a second Resolution cbuffer (TargetWidth/TargetHeight, the HLSL's b2)
// filled from c.output->width()/height() for aspect correction — same pattern as ChromaticAbberation.
//
// Self-contained leaf: cookVoronoiCells + registerVoronoiCellsOp() + runVoronoiCellsSelfTest.
// Shares the D2-2 PSO+scratch cache seam (tex_op_cache.h).
#include "runtime/point_ops.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/eval_context.h"
#include "runtime/point_graph.h"          // TexCookCtx, cookParam, registerTexOp
#include "runtime/tex_op_cache.h"         // cachedTexPSO (D2-2 PSO reuse)
#include "runtime/voronoicells_params.h"  // VoronoiCellsParams/Resolution, VORONOI_Params/Resolution

#ifndef SW_SHADER_METALLIB
#define SW_SHADER_METALLIB "shaders.metallib"
#endif

namespace sw {
namespace {

// VoronoiCells texture op: single pass. Reads c.inputTexture, writes c.output.
// No upstream texture wired: clear output to black.
void cookVoronoiCells(TexCookCtx& c) {
  if (!c.lib || !c.output) return;
  MTL::PixelFormat fmt = c.output->pixelFormat();

  if (!c.inputTexture) {
    MTL::RenderPassDescriptor* pass = MTL::RenderPassDescriptor::renderPassDescriptor();
    auto* ca = pass->colorAttachments()->object(0);
    ca->setTexture(c.output);
    ca->setLoadAction(MTL::LoadActionClear);
    ca->setClearColor(MTL::ClearColor::Make(0.0, 0.0, 0.0, 1.0));
    ca->setStoreAction(MTL::StoreActionStore);
    MTL::CommandBuffer* cmd = c.queue->commandBuffer();
    cmd->renderCommandEncoder(pass)->endEncoding();
    cmd->commit();
    cmd->waitUntilCompleted();
    return;
  }

  MTL::RenderPipelineState* rps =
      cachedTexPSO(c.dev, c.lib, "voronoicells_vs", "voronoicells_fs", fmt);  // D2-2 reuse
  if (!rps) return;

  MTL::SamplerDescriptor* sd = MTL::SamplerDescriptor::alloc()->init();
  sd->setMinFilter(MTL::SamplerMinMagFilterLinear);
  sd->setMagFilter(MTL::SamplerMinMagFilterLinear);
  sd->setSAddressMode(MTL::SamplerAddressModeClampToEdge);  // fork: fixed clamp (TiXL Wrap=Clamp)
  sd->setTAddressMode(MTL::SamplerAddressModeClampToEdge);
  MTL::SamplerState* samp = c.dev->newSamplerState(sd);
  sd->release();

  // TiXL VoronoiCells.cs defaults from VoronoiCells.t3:
  // EdgeColor (0,0,0,1), Background (1,1,1,1), Scale 10.0, EdgeWidth 0.68, Phase 0.0.
  VoronoiCellsParams p{};
  p.EdgeColorR = cookParam(c, "EdgeColor.r", 0.0f);
  p.EdgeColorG = cookParam(c, "EdgeColor.g", 0.0f);
  p.EdgeColorB = cookParam(c, "EdgeColor.b", 0.0f);
  p.EdgeColorA = cookParam(c, "EdgeColor.a", 1.0f);
  p.BackgroundR = cookParam(c, "Background.r", 1.0f);
  p.BackgroundG = cookParam(c, "Background.g", 1.0f);
  p.BackgroundB = cookParam(c, "Background.b", 1.0f);
  p.BackgroundA = cookParam(c, "Background.a", 1.0f);
  p.Scale     = cookParam(c, "Scale", 10.0f);
  p.LineWidth = cookParam(c, "EdgeWidth", 0.68f);  // HLSL cbuffer field LineWidth = .cs EdgeWidth
  p.Phase     = cookParam(c, "Phase", 0.0f);

  VoronoiCellsResolution res{};
  res.TargetWidth  = (float)c.output->width();
  res.TargetHeight = (float)c.output->height();

  MTL::RenderPassDescriptor* pass = MTL::RenderPassDescriptor::renderPassDescriptor();
  auto* ca = pass->colorAttachments()->object(0);
  ca->setTexture(c.output);
  ca->setLoadAction(MTL::LoadActionClear);
  ca->setClearColor(MTL::ClearColor::Make(0.0, 0.0, 0.0, 1.0));
  ca->setStoreAction(MTL::StoreActionStore);
  MTL::CommandBuffer* cmd = c.queue->commandBuffer();
  MTL::RenderCommandEncoder* enc = cmd->renderCommandEncoder(pass);
  enc->setRenderPipelineState(rps);
  enc->setFragmentTexture(const_cast<MTL::Texture*>(c.inputTexture), 0);
  enc->setFragmentSamplerState(samp, 0);
  enc->setFragmentBytes(&p,   sizeof(VoronoiCellsParams),     VORONOI_Params);
  enc->setFragmentBytes(&res, sizeof(VoronoiCellsResolution), VORONOI_Resolution);
  enc->drawPrimitives(MTL::PrimitiveTypeTriangle, NS::UInteger(0), NS::UInteger(3));
  enc->endEncoding();
  cmd->commit();
  cmd->waitUntilCompleted();

  samp->release();  // rps is cache-owned (tex_op_cache), not released here
}

}  // namespace

void registerVoronoiCellsOp() { registerTexOp("VoronoiCells", cookVoronoiCells); }

// --- VoronoiCells MATH golden ---------------------------------------------------------------
// Source: a smooth 2-axis gradient (R = x/W, G = y/H) so each cell's feature point (read from the
// input .xy at the cell index) varies spatially -> distinct, non-degenerate cells.
// Run with EdgeColor=(1,0,0,1) RED, Background=(1,1,1,1) WHITE, Phase=0, EdgeWidth=0.68.
// The cell INTERIORS become Background*cellColor (bright, R≈G≈B), the cell BORDERS become RED
// (EdgeColor: R high, G/B low). We scan the whole frame and count "red edge" pixels
// (R>150 && G<80 && B<80). A proper Voronoi mosaic at Scale=24 produces MANY such border pixels.
// Assert: redEdgeCount > 200 (mosaic has visible cell borders).
// injectBug EdgeWidth=0: the smoothstep edge band collapses (c.x - EdgeWidth*0.1 + 0.1 stays
// above the 0.07 upper threshold), so the EdgeColor red borders are not drawn -> redEdgeCount ~ 0
// -> assertion FAILS (teeth).
int runVoronoiCellsSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  const uint32_t W = 128, H = 128;

  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  clearTexOpCache();
  MTL::CommandQueue* q = dev->newCommandQueue();
  NS::Error* err = nullptr;
  MTL::Library* lib =
      dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
  if (!lib) {
    printf("[selftest-voronoicells] FAIL: no metallib\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }

  MTL::TextureDescriptor* td =
      MTL::TextureDescriptor::texture2DDescriptor(MTL::PixelFormatRGBA8Unorm, W, H, false);
  td->setUsage(MTL::TextureUsageRenderTarget | MTL::TextureUsageShaderRead);
  td->setStorageMode(MTL::StorageModeShared);
  MTL::Texture* src = dev->newTexture(td);
  MTL::Texture* dst = dev->newTexture(td);

  // Source: a smooth gradient R=x/W, G=y/H, B=0.5 — varied per-cell feature points.
  std::vector<uint8_t> in((size_t)W * H * 4, 0);
  for (uint32_t y = 0; y < H; ++y)
    for (uint32_t x = 0; x < W; ++x) {
      size_t i = ((size_t)y * W + x) * 4;
      in[i]     = (uint8_t)(255.0f * x / (float)W);
      in[i + 1] = (uint8_t)(255.0f * y / (float)H);
      in[i + 2] = 128;
      in[i + 3] = 255;
    }
  src->replaceRegion(MTL::Region::Make2D(0, 0, W, H), 0, in.data(), W * 4);

  std::map<std::string, float> params;
  params["EdgeColor.r"] = 1.0f; params["EdgeColor.g"] = 0.0f;
  params["EdgeColor.b"] = 0.0f; params["EdgeColor.a"] = 1.0f;
  params["Background.r"] = 1.0f; params["Background.g"] = 1.0f;
  params["Background.b"] = 1.0f; params["Background.a"] = 1.0f;
  params["Scale"] = 24.0f;
  // injectBug EdgeWidth=0: the smoothstep edge band collapses — the border-distance test never
  // crosses into the EdgeColor region (c.x - 0*0.1 + 0.1 stays > 0.07 almost everywhere), so the
  // red cell borders disappear. EdgeWidth is the real .cs/.t3 knob that controls edge thickness,
  // so this is a faithful logic flip (TiXL default 0.68 draws borders; 0 erases them).
  params["EdgeWidth"] = injectBug ? 0.0f : 0.68f;
  params["Phase"] = 0.0f;

  TexCookCtx c;
  c.dev = dev; c.lib = lib; c.queue = q;
  c.nodeId = 1; c.inputTexture = src; c.output = dst; c.params = &params;
  cookVoronoiCells(c);

  std::vector<uint8_t> out((size_t)W * H * 4, 0);
  dst->getBytes(out.data(), W * 4, MTL::Region::Make2D(0, 0, W, H), 0);

  // Count red-edge pixels across the whole frame (EdgeColor leaks through at cell borders).
  int redEdgeCount = 0;
  for (size_t i = 0; i < (size_t)W * H; ++i) {
    int R = out[i * 4], G = out[i * 4 + 1], B = out[i * 4 + 2];
    if (R > 150 && G < 80 && B < 80) redEdgeCount++;
  }

  bool hasBorders = redEdgeCount > 200;  // injectBug Scale=1 -> ~0 borders -> fails
  bool pass = hasBorders;
  printf("[selftest-voronoicells] redEdgeCount=%d (need>200) -> %s\n",
         redEdgeCount, pass ? "PASS" : "FAIL");

  src->release(); dst->release();
  lib->release(); q->release(); dev->release(); pool->release();
  return pass ? 0 : 1;
}

}  // namespace sw

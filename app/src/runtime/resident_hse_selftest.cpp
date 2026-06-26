// Headless RED->GREEN proof that the HSE leaf works in the RESIDENT (production) cook path — not only
// in the flat --selftest-hse. Same structure as resident_combinematerialchannels_selftest.cpp:
//   (1) cookResident must cook + bind BOTH fixed textures (inputTextures[0]=Image, [1]=FxTexture),
//   (2) run HueShift's HSB hue shift: Image=RED + FxTexture.g=1/3 -> hue rotates RED to GREEN.
// injectBug: OMIT the FxTexture wire -> the slot is unwired -> the cook binds a 1x1 BLACK dummy ->
//   fx.g = 0 -> hueShift = Hue(0) -> output stays RED ([fork-hse-unwired-fxtexture]). So the B/2nd
//   input is proven: with the wire the output is GREEN, without it RED.
#include "runtime/point_graph.h"

#include <cmath>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/compound_graph.h"
#include "runtime/image_filter_op_registry.h"
#include "runtime/resident_eval_graph.h"
#include "runtime/tixl_point.h"

#ifndef SW_SHADER_METALLIB
#define SW_SHADER_METALLIB "shaders.metallib"
#endif

namespace sw {
namespace {

constexpr uint32_t kW = 32, kH = 32;

// RenderTarget cook OVERRIDE: paint a solid picked by ClearColor.x (0 -> RED Image, 1 -> FxTexture g=1/3).
void texSource(TexCookCtx& c) {
  if (!c.output) return;
  const uint32_t w = (uint32_t)c.output->width(), h = (uint32_t)c.output->height();
  const int which = (int)std::lround(cookParam(c, "ClearColor.x", 0.0f));
  uint8_t r = 255, g = 0, b = 0;        // which==0: pure RED Image
  if (which == 1) { r = 0; g = 85; b = 0; }  // which==1: FxTexture, g = 85/255 ~= 1/3
  std::vector<uint8_t> px((size_t)w * h * 4, 0);
  for (size_t i = 0; i < (size_t)w * h; ++i) {
    px[i * 4 + 0] = r; px[i * 4 + 1] = g; px[i * 4 + 2] = b; px[i * 4 + 3] = 255;
  }
  c.output->replaceRegion(MTL::Region::Make2D(0, 0, w, h), 0, px.data(), w * 4);
}

Symbol atomicOp(const char* id, std::vector<SlotDef> ins, std::vector<SlotDef> outs) {
  Symbol s; s.id = id; s.name = id; s.atomic = true;
  s.inputDefs = std::move(ins); s.outputDefs = std::move(outs);
  return s;
}

bool cookResidentCenter(MTL::Device* dev, MTL::CommandQueue* q, MTL::Library* mlib, bool wireFx,
                        uint8_t out[4], uint32_t& ow, uint32_t& oh) {
  registerTexOp("RenderTarget", texSource);

  SymbolLibrary lib;
  lib.symbols["RenderTarget"] = atomicOp(
      "RenderTarget",
      {{"command", "command", "Command", 0.0f}, {"Resolution", "Resolution", "Float", 0.0f},
       {"CustomW", "CustomW", "Float", 512.0f}, {"CustomH", "CustomH", "Float", 512.0f},
       {"ClearColor.x", "ClearColor.x", "Float", 0.0f}},
      {{"out", "out", "Texture2D", 0.0f}});
  lib.symbols["HSE"] = atomicOp(
      "HSE",
      {{"Image", "Image", "Texture2D", 0.0f},
       {"FxTexture", "FxTexture", "Texture2D", 0.0f},
       {"Hue", "Hue", "Float", 0.0f},
       {"Saturation", "Saturation", "Float", 1.0f},
       {"Exposure", "Exposure", "Float", 1.0f},
       {"Resolution", "Resolution", "Float", 0.0f}},
      {{"out", "out", "Texture2D", 0.0f}});
  Symbol root; root.id = "Root"; root.name = "Root"; root.atomic = false;
  root.outputDefs = {{"out", "out", "Texture2D", 0.0f}};
  SymbolChild c1; c1.id = 1; c1.symbolId = "RenderTarget";  // Image (ClearColor.x=0 default -> RED)
  SymbolChild c2; c2.id = 2; c2.symbolId = "RenderTarget"; c2.overrides["ClearColor.x"] = 1.0f;  // FxTexture g=1/3
  SymbolChild c3; c3.id = 3; c3.symbolId = "HSE";
  c3.overrides["Resolution"] = 0.0f;  // WindowFollow -> matches the PointGraph kW x kH
  c3.overrides["Hue"] = 0.0f; c3.overrides["Saturation"] = 1.0f; c3.overrides["Exposure"] = 1.0f;
  root.children = {c1, c2, c3};
  root.connections = {{1, "out", 3, "Image"}, {3, "out", kSymbolBoundary, "out"}};
  if (wireFx) root.connections.push_back({2, "out", 3, "FxTexture"});
  lib.symbols["Root"] = root; lib.rootId = "Root";
  ResidentEvalGraph rg = buildEvalGraph(lib, "Root");

  EvaluationContext ctx{};
  ctx.frameIndex = 0; ctx.time = 0.0f; ctx.deltaTime = 1.0f / 60.0f;

  PointGraph pg(dev, mlib, q, kW, kH);
  pg.cookResident(rg, ctx, /*reg=*/nullptr, /*targetPath=*/"3");

  MTL::Texture* tex = pg.target();
  ow = tex ? (uint32_t)tex->width() : 0;
  oh = tex ? (uint32_t)tex->height() : 0;
  if (!tex || ow != kW || oh != kH) return false;
  std::vector<uint8_t> px((size_t)ow * oh * 4, 0);
  tex->getBytes(px.data(), ow * 4, MTL::Region::Make2D(0, 0, ow, oh), 0);
  const uint32_t cx = kW / 2, cy = kH / 2;
  size_t i = ((size_t)cy * kW + cx) * 4;
  out[0] = px[i]; out[1] = px[i + 1]; out[2] = px[i + 2]; out[3] = px[i + 3];
  return true;
}

}  // namespace

int runResidentHseSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();

  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue* q = dev->newCommandQueue();
  NS::Error* err = nullptr;
  MTL::Library* mlib =
      dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
  if (!mlib) {
    std::printf("[selftest-hseresident] FAIL: no metallib\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }

  uint8_t got[4] = {0, 0, 0, 0};
  uint32_t ow = 0, oh = 0;
  bool gotOk = cookResidentCenter(dev, q, mlib, /*wireFx=*/!injectBug, got, ow, oh);
  bool dimsOk = gotOk && ow == kW && oh == kH;

  // With the FxTexture wire: Image RED + fx.g=1/3 -> GREEN. injectBug drops the wire -> RED.
  bool isGreen = dimsOk && got[1] > 200 && got[0] < 55 && got[2] < 55 && got[3] > 250;
  bool pass = isGreen;
  std::printf("[selftest-hseresident] out=%ux%u(want %ux%u,dimsOk=%d) got=(%u,%u,%u,%u) "
              "expect GREEN(G>200,R<55,B<55,A>250) isGreen=%d injectBug=%d -> %s\n",
              ow, oh, kW, kH, dimsOk ? 1 : 0, got[0], got[1], got[2], got[3], isGreen ? 1 : 0,
              injectBug ? 1 : 0, pass ? "PASS" : "FAIL");

  mlib->release(); q->release(); dev->release(); pool->release();
  return pass ? 0 : 1;
}

}  // namespace sw

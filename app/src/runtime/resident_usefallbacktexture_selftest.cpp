// Headless RED->GREEN proof that UseFallbackTexture's two FIXED Texture2D ports thread through the
// RESIDENT (production) cook path. The live app drives cookResident (frame_cook.cpp); UseFallbackTexture
// must, in resident:
//   (1) route TextureA's wire into inputTextures[0] and Fallback's wire into inputTextures[1] (the FIXED
//       Texture2D else-branch of cookTexNode — each port one slot, in port-declaration order),
//   (2) forward `TextureA ?? Fallback` (TiXL null-coalesce: TextureA if its wire is present/non-null,
//       else Fallback).
//
// THE TOOTH (null-coalesce + port->slot order): TextureA = RT#1 (green), Fallback = RT#2 (blue), both
// wired. With TextureA present, the op forwards TextureA -> GREEN. If the routing swapped the two ports
// (Fallback into slot 0) or ignored TextureA, the output would be blue -> the green pin misses.
//
// injectBug: OMIT the TextureA wire (RT#1 -> TextureA). Now TextureA (slot 0) is null -> `?? Fallback`
// forwards Fallback (RT#2, blue) -> the green pin misses -> RED. This drives the exact null-coalesce
// through the resident path (an unwired FIXED port lands as a null slot).
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
constexpr uint8_t kAr = 20, kAg = 200, kAb = 20;   // TextureA (RT#1, green) <- selected when present
constexpr uint8_t kFr = 30, kFg = 60,  kFb = 240;  // Fallback (RT#2, blue)  <- injectBug fall-through
constexpr uint8_t kExpR = kAr, kExpG = kAg, kExpB = kAb, kExpA = 255;

// RenderTarget cook OVERRIDE (one painter, two patterns): ClearColor.x 0 -> TextureA color, 1 -> Fallback.
void texSource(TexCookCtx& c) {
  if (!c.output) return;
  const uint32_t w = (uint32_t)c.output->width(), h = (uint32_t)c.output->height();
  const int which = (int)std::lround(cookParam(c, "ClearColor.x", 0.0f));
  uint8_t r = kAr, g = kAg, b = kAb;
  if (which == 1) { r = kFr; g = kFg; b = kFb; }
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

// Cook RT#1 (TextureA, green) + RT#2 (Fallback, blue) -> UseFallbackTexture#3 through cookResident; read
// back the center pixel. wireTextureA=false -> OMIT the TextureA wire (injectBug -> select Fallback).
bool cookResidentCenter(MTL::Device* dev, MTL::CommandQueue* q, MTL::Library* mlib, bool wireTextureA,
                        uint8_t out[4], uint32_t& ow, uint32_t& oh) {
  registerTexOp("RenderTarget", texSource);

  SymbolLibrary lib;
  lib.symbols["RenderTarget"] = atomicOp(
      "RenderTarget",
      {{"command", "command", "Command", 0.0f}, {"Resolution", "Resolution", "Float", 0.0f},
       {"CustomW", "CustomW", "Float", 512.0f}, {"CustomH", "CustomH", "Float", 512.0f},
       {"ClearColor.x", "ClearColor.x", "Float", 0.0f}},
      {{"out", "out", "Texture2D", 0.0f}});
  // UseFallbackTexture: TWO FIXED Texture2D ports — TextureA (slot 0), Fallback (slot 1). Port order
  // here MUST match the registrar (TextureA first) so resident routing lands TextureA in slot 0.
  lib.symbols["UseFallbackTexture"] = atomicOp(
      "UseFallbackTexture",
      {{"TextureA", "TextureA", "Texture2D", 0.0f},
       {"Fallback", "Fallback", "Texture2D", 0.0f},
       {"Resolution", "Resolution", "Float", 0.0f}},
      {{"out", "out", "Texture2D", 0.0f}});
  Symbol root; root.id = "Root"; root.name = "Root"; root.atomic = false;
  root.outputDefs = {{"out", "out", "Texture2D", 0.0f}};
  SymbolChild c1; c1.id = 1; c1.symbolId = "RenderTarget";  // TextureA source (green)
  SymbolChild c2; c2.id = 2; c2.symbolId = "RenderTarget"; c2.overrides["ClearColor.x"] = 1.0f;  // Fallback (blue)
  SymbolChild c3; c3.id = 3; c3.symbolId = "UseFallbackTexture";
  c3.overrides["Resolution"] = 0.0f;   // WindowFollow
  root.children = {c1, c2, c3};
  // Fallback always wired; TextureA wired unless injectBug. (Distinct ports -> distinct slots; no
  // multiInput gather needed here, just the FIXED port->slot routing.)
  root.connections = {{2, "out", 3, "Fallback"},
                      {3, "out", kSymbolBoundary, "out"}};
  if (wireTextureA) root.connections.push_back({1, "out", 3, "TextureA"});
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

int runResidentUseFallbackTextureSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();

  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue* q = dev->newCommandQueue();
  NS::Error* err = nullptr;
  MTL::Library* mlib =
      dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
  if (!mlib) {
    std::printf("[selftest-usefallbacktextureresident] FAIL: no metallib\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }

  const int kTol = 2;
  uint8_t got[4] = {0, 0, 0, 0};
  uint32_t ow = 0, oh = 0;
  // injectBug -> drop the TextureA wire; TextureA null -> `?? Fallback` -> blue -> green pin misses.
  bool gotOk = cookResidentCenter(dev, q, mlib, /*wireTextureA=*/!injectBug, got, ow, oh);
  bool dimsOk = gotOk && ow == kW && oh == kH;

  // Non-degenerate: TextureA (green) differs from Fallback (blue, the injectBug fall-through).
  bool nonDegenerate = (kExpR != kFr) || (kExpG != kFg) || (kExpB != kFb);

  int dR = std::abs((int)got[0] - (int)kExpR);
  int dG = std::abs((int)got[1] - (int)kExpG);
  int dB = std::abs((int)got[2] - (int)kExpB);
  int dA = std::abs((int)got[3] - (int)kExpA);
  bool match = dimsOk && dR <= kTol && dG <= kTol && dB <= kTol && dA <= kTol;

  bool pass = dimsOk && nonDegenerate && match;
  std::printf("[selftest-usefallbacktextureresident] out=%ux%u(want %ux%u,dimsOk=%d) "
              "want=(%u,%u,%u,%u) got=(%u,%u,%u,%u) d=(%d,%d,%d,%d) match(<=%d)=%d nonDeg=%d "
              "injectBug=%d -> %s\n",
              ow, oh, kW, kH, dimsOk ? 1 : 0, kExpR, kExpG, kExpB, kExpA, got[0], got[1], got[2],
              got[3], dR, dG, dB, dA, kTol, match ? 1 : 0, nonDegenerate ? 1 : 0, injectBug ? 1 : 0,
              pass ? "PASS" : "FAIL");

  mlib->release(); q->release(); dev->release(); pool->release();
  return pass ? 0 : 1;
}

}  // namespace sw

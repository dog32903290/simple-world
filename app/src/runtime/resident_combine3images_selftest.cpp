// Headless RED->GREEN proof that the Combine3Images leaf works in the RESIDENT (production) cook path
// — not only in the flat --selftest-combine3images. The live app drives cookResident (frame_cook.cpp),
// so Combine3Images (an image filter with THREE graph-wired Texture2D inputs — the first 3-input
// consumer of the multi-image seam) must, in resident:
//   (1) have ALL THREE upstream textures cooked + bound: cookTexNode recurses each Texture2D input port
//       in spec order into inputTextures[0]=ImageA / [1]=ImageB / [2]=ImageC,
//   (2) channel-pack out.R<-ImageA.r, out.G<-ImageB.g, out.B<-ImageC.b, out.A<-1.
// Cut 52 lesson: a flat-only golden can let live garbage through; the resident hook must be DRIVEN
// (cookResident -> cookTexNode -> leaf -> displayTex -> target()), not merely code-mirrored.
//
// Controlled inputs (same as the flat golden): three FLAT solids with distinct channel values painted by
// three RenderTarget source ops (one painter, three patterns picked by ClearColor.x). Combine3Images#4
// reads ImageA<-RT#1, ImageB<-RT#2, ImageC<-RT#3. Identity tints, default selects, AlphaMode=SetToOne ->
// the output is a single solid (kA_r, kB_g, kC_b, 255), pinned at the center.
//
// injectBug: a THIRD-INPUT wiring perturbation — OMIT the ImageC wire (RT#3 -> Combine3Images#4.ImageC).
// With ImageC unwired the leaf's fork samples ImageA, so out.B reads ImageA.b instead of ImageC.b -> the
// B pin diverges by >>tol -> RED. This exercises the THREE-image gather (the third Texture2D port really
// must be threaded).
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
// Distinct solid channel values per image (identical to the flat golden's kA/kB/kC).
constexpr uint8_t kA_r = 200, kA_g = 10,  kA_b = 10;
constexpr uint8_t kB_r = 10,  kB_g = 180, kB_b = 10;
constexpr uint8_t kC_r = 10,  kC_g = 10,  kC_b = 150;
// Expected packed output: R<-A.r, G<-B.g, B<-C.b, A<-1.
constexpr uint8_t kExpR = kA_r, kExpG = kB_g, kExpB = kC_b, kExpA = 255;

// RenderTarget cook OVERRIDE (one painter, three patterns). findSpec only resolves REGISTERED node
// types, so the three sources must be a real registered type ("RenderTarget"). We override its cook with
// a single painter that picks the solid from the node's ClearColor.x param (a real RenderTarget port):
// ClearColor.x==0 -> ImageA solid, ==1 -> ImageB solid, ==2 -> ImageC solid.
void texSource(TexCookCtx& c) {
  if (!c.output) return;
  const uint32_t w = (uint32_t)c.output->width(), h = (uint32_t)c.output->height();
  const int which = (int)std::lround(cookParam(c, "ClearColor.x", 0.0f));
  uint8_t r = kA_r, g = kA_g, b = kA_b;
  if (which == 1) { r = kB_r; g = kB_g; b = kB_b; }
  else if (which == 2) { r = kC_r; g = kC_g; b = kC_b; }
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

// Cook RenderTarget#1(A) + #2(B) + #3(C) -> Combine3Images#4 through cookResident; read back the center
// pixel. wireImageC=false -> OMIT the ImageC connection (injectBug: the 3-image gather loses its 3rd
// input).
bool cookResidentCenter(MTL::Device* dev, MTL::CommandQueue* q, MTL::Library* mlib, bool wireImageC,
                        uint8_t out[4], uint32_t& ow, uint32_t& oh) {
  registerTexOp("RenderTarget", texSource);  // override RenderTarget's cook with the three-pattern painter

  SymbolLibrary lib;
  lib.symbols["RenderTarget"] = atomicOp(
      "RenderTarget",
      {{"command", "command", "Command", 0.0f}, {"Resolution", "Resolution", "Float", 0.0f},
       {"CustomW", "CustomW", "Float", 512.0f}, {"CustomH", "CustomH", "Float", 512.0f},
       {"ClearColor.x", "ClearColor.x", "Float", 0.0f}},
      {{"out", "out", "Texture2D", 0.0f}});
  lib.symbols["Combine3Images"] = atomicOp(
      "Combine3Images",
      {{"ImageA", "ImageA", "Texture2D", 0.0f},
       {"ImageB", "ImageB", "Texture2D", 0.0f},
       {"ImageC", "ImageC", "Texture2D", 0.0f},
       {"ColorA.x", "ColorA.x", "Float", 1.0f}, {"ColorA.y", "ColorA.y", "Float", 1.0f},
       {"ColorA.z", "ColorA.z", "Float", 1.0f}, {"ColorA.w", "ColorA.w", "Float", 1.0f},
       {"ColorB.x", "ColorB.x", "Float", 1.0f}, {"ColorB.y", "ColorB.y", "Float", 1.0f},
       {"ColorB.z", "ColorB.z", "Float", 1.0f}, {"ColorB.w", "ColorB.w", "Float", 1.0f},
       {"ColorC.x", "ColorC.x", "Float", 1.0f}, {"ColorC.y", "ColorC.y", "Float", 1.0f},
       {"ColorC.z", "ColorC.z", "Float", 1.0f}, {"ColorC.w", "ColorC.w", "Float", 1.0f},
       {"SelectChannel_R", "SelectChannel_R", "Float", 0.0f},
       {"SelectChannel_G", "SelectChannel_G", "Float", 6.0f},
       {"SelectChannel_B", "SelectChannel_B", "Float", 12.0f},
       {"SelectAlphaChannel", "SelectAlphaChannel", "Float", 4.0f},
       {"Resolution", "Resolution", "Float", 0.0f}},
      {{"out", "out", "Texture2D", 0.0f}});
  Symbol root; root.id = "Root"; root.name = "Root"; root.atomic = false;
  root.outputDefs = {{"out", "out", "Texture2D", 0.0f}};
  SymbolChild c1; c1.id = 1; c1.symbolId = "RenderTarget";  // ImageA solid (ClearColor.x=0 default)
  SymbolChild c2; c2.id = 2; c2.symbolId = "RenderTarget"; c2.overrides["ClearColor.x"] = 1.0f;  // ImageB
  SymbolChild c3; c3.id = 3; c3.symbolId = "RenderTarget"; c3.overrides["ClearColor.x"] = 2.0f;  // ImageC
  SymbolChild c4; c4.id = 4; c4.symbolId = "Combine3Images";
  // Identity tints + default selects + SetToOne alpha (atomicOp defaults already carry them; explicit for
  // clarity / robustness against default drift).
  c4.overrides["ColorA.x"] = 1.0f; c4.overrides["ColorA.y"] = 1.0f; c4.overrides["ColorA.z"] = 1.0f; c4.overrides["ColorA.w"] = 1.0f;
  c4.overrides["ColorB.x"] = 1.0f; c4.overrides["ColorB.y"] = 1.0f; c4.overrides["ColorB.z"] = 1.0f; c4.overrides["ColorB.w"] = 1.0f;
  c4.overrides["ColorC.x"] = 1.0f; c4.overrides["ColorC.y"] = 1.0f; c4.overrides["ColorC.z"] = 1.0f; c4.overrides["ColorC.w"] = 1.0f;
  c4.overrides["SelectChannel_R"] = 0.0f;
  c4.overrides["SelectChannel_G"] = 6.0f;
  c4.overrides["SelectChannel_B"] = 12.0f;
  c4.overrides["SelectAlphaChannel"] = 4.0f;
  c4.overrides["Resolution"] = 0.0f;
  root.children = {c1, c2, c3, c4};
  root.connections = {{1, "out", 4, "ImageA"}, {2, "out", 4, "ImageB"},
                      {4, "out", kSymbolBoundary, "out"}};
  if (wireImageC) root.connections.push_back({3, "out", 4, "ImageC"});
  lib.symbols["Root"] = root; lib.rootId = "Root";
  ResidentEvalGraph rg = buildEvalGraph(lib, "Root");

  EvaluationContext ctx{};
  ctx.frameIndex = 0; ctx.time = 0.0f; ctx.deltaTime = 1.0f / 60.0f;

  PointGraph pg(dev, mlib, q, kW, kH);
  pg.cookResident(rg, ctx, /*reg=*/nullptr, /*targetPath=*/"4");

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

int runResidentCombine3ImagesSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();

  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue* q = dev->newCommandQueue();
  NS::Error* err = nullptr;
  MTL::Library* mlib =
      dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
  if (!mlib) {
    std::printf("[selftest-combine3imagesresident] FAIL: no metallib\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }

  const int kTol = 2;
  uint8_t got[4] = {0, 0, 0, 0};
  uint32_t ow = 0, oh = 0;
  // injectBug -> drop the ImageC wire (the 3-image gather loses its 3rd input -> B reads ImageA.b).
  bool gotOk = cookResidentCenter(dev, q, mlib, /*wireImageC=*/!injectBug, got, ow, oh);
  bool dimsOk = gotOk && ow == kW && oh == kH;

  bool nonDegenerate = (kExpR != kExpG) && (kExpG != kExpB) && (kExpR != kExpB);

  int dR = std::abs((int)got[0] - (int)kExpR);
  int dG = std::abs((int)got[1] - (int)kExpG);
  int dB = std::abs((int)got[2] - (int)kExpB);
  int dA = std::abs((int)got[3] - (int)kExpA);
  bool match = dimsOk && dR <= kTol && dG <= kTol && dB <= kTol && dA <= kTol;

  bool pass = dimsOk && nonDegenerate && match;
  std::printf("[selftest-combine3imagesresident] out=%ux%u(want %ux%u,dimsOk=%d) "
              "want=(%u,%u,%u,%u) got=(%u,%u,%u,%u) d=(%d,%d,%d,%d) match(<=%d)=%d nonDeg=%d "
              "injectBug=%d -> %s\n",
              ow, oh, kW, kH, dimsOk ? 1 : 0, kExpR, kExpG, kExpB, kExpA, got[0], got[1], got[2],
              got[3], dR, dG, dB, dA, kTol, match ? 1 : 0, nonDegenerate ? 1 : 0, injectBug ? 1 : 0,
              pass ? "PASS" : "FAIL");

  mlib->release(); q->release(); dev->release(); pool->release();
  return pass ? 0 : 1;
}

}  // namespace sw

// Headless RED->GREEN proof that the CombineMaterialChannels leaf works in the RESIDENT (production)
// cook path — not only in the flat --selftest-combinematerialchannels. Same structure as
// resident_combinematerialchannels2_selftest.cpp (the sibling); a PBR-flavored controlled input set
// keeps it an independent witness.
//   (1) cookResident must cook + bind the THREE fixed material textures (inputTextures[0/1/2]) +
//       rasterize the RemapRoughness LUT (t3),
//   (2) channel-pack out.R<-Roughness.r (remapped through the IDENTITY default curve = passthrough),
//       out.G<-Metallic.g, out.B<-Occlusion.r, out.A<-1.
//   ★PRODUCTION CURVE NOTE: there is NO Curve PRODUCER op yet, so the resident path uses the embedded
//    DEFAULT identity RemapRoughness curve ([fork-cmc-embedded-default-curve]) — the remap is a
//    passthrough, so out.R == Roughness.r. (The flat --selftest injects a non-identity curve to prove
//    the remap math; this resident test proves the default-curve production path + the 3-texture gather.)
// injectBug: OMIT the Occlusion wire -> IsOcclusionConnected=0 -> out.B = 1.0 (255) instead of 160 ->
//    B pin diverges >>tol -> RED (exercises the 3rd Texture2D port + its connected-flag in the resident
//    gather).
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
constexpr uint8_t kR_r = 120, kR_g = 10,  kR_b = 10;   // Roughness map
constexpr uint8_t kM_r = 10,  kM_g = 200, kM_b = 10;   // Metallic  map
constexpr uint8_t kO_r = 160, kO_g = 10,  kO_b = 10;   // Occlusion map
// Identity remap (production default curve) -> out.R == Roughness.r.
constexpr uint8_t kExpR = kR_r, kExpG = kM_g, kExpB = kO_r, kExpA = 255;

// RenderTarget cook OVERRIDE (one painter, three solids picked by ClearColor.x: 0->Rough, 1->Metal, 2->Occl).
void texSource(TexCookCtx& c) {
  if (!c.output) return;
  const uint32_t w = (uint32_t)c.output->width(), h = (uint32_t)c.output->height();
  const int which = (int)std::lround(cookParam(c, "ClearColor.x", 0.0f));
  uint8_t r = kR_r, g = kR_g, b = kR_b;
  if (which == 1) { r = kM_r; g = kM_g; b = kM_b; }
  else if (which == 2) { r = kO_r; g = kO_g; b = kO_b; }
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

bool cookResidentCenter(MTL::Device* dev, MTL::CommandQueue* q, MTL::Library* mlib, bool wireOcclusion,
                        uint8_t out[4], uint32_t& ow, uint32_t& oh) {
  registerTexOp("RenderTarget", texSource);

  SymbolLibrary lib;
  lib.symbols["RenderTarget"] = atomicOp(
      "RenderTarget",
      {{"command", "command", "Command", 0.0f}, {"Resolution", "Resolution", "Float", 0.0f},
       {"CustomW", "CustomW", "Float", 512.0f}, {"CustomH", "CustomH", "Float", 512.0f},
       {"ClearColor.x", "ClearColor.x", "Float", 0.0f}},
      {{"out", "out", "Texture2D", 0.0f}});
  lib.symbols["CombineMaterialChannels"] = atomicOp(
      "CombineMaterialChannels",
      {{"Roughness", "Roughness", "Texture2D", 0.0f},
       {"Metallic", "Metallic", "Texture2D", 0.0f},
       {"Occlusion", "Occlusion", "Texture2D", 0.0f},
       {"RemapRoughness", "RemapRoughness", "Curve", 0.0f},
       {"Resolution", "Resolution", "Float", 0.0f}},
      {{"out", "out", "Texture2D", 0.0f}});
  Symbol root; root.id = "Root"; root.name = "Root"; root.atomic = false;
  root.outputDefs = {{"out", "out", "Texture2D", 0.0f}};
  SymbolChild c1; c1.id = 1; c1.symbolId = "RenderTarget";  // Roughness (ClearColor.x=0 default)
  SymbolChild c2; c2.id = 2; c2.symbolId = "RenderTarget"; c2.overrides["ClearColor.x"] = 1.0f;  // Metallic
  SymbolChild c3; c3.id = 3; c3.symbolId = "RenderTarget"; c3.overrides["ClearColor.x"] = 2.0f;  // Occlusion
  SymbolChild c4; c4.id = 4; c4.symbolId = "CombineMaterialChannels";
  c4.overrides["Resolution"] = 0.0f;
  root.children = {c1, c2, c3, c4};
  root.connections = {{1, "out", 4, "Roughness"}, {2, "out", 4, "Metallic"},
                      {4, "out", kSymbolBoundary, "out"}};
  if (wireOcclusion) root.connections.push_back({3, "out", 4, "Occlusion"});
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

int runResidentCombineMaterialChannelsSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();

  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue* q = dev->newCommandQueue();
  NS::Error* err = nullptr;
  MTL::Library* mlib =
      dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
  if (!mlib) {
    std::printf("[selftest-combinematerialchannelsresident] FAIL: no metallib\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }

  const int kTol = 2;
  uint8_t got[4] = {0, 0, 0, 0};
  uint32_t ow = 0, oh = 0;
  bool gotOk = cookResidentCenter(dev, q, mlib, /*wireOcclusion=*/!injectBug, got, ow, oh);
  bool dimsOk = gotOk && ow == kW && oh == kH;

  bool nonDegenerate = (kExpR != kExpG) && (kExpG != kExpB) && (kExpR != kExpB);
  int dR = std::abs((int)got[0] - (int)kExpR);
  int dG = std::abs((int)got[1] - (int)kExpG);
  int dB = std::abs((int)got[2] - (int)kExpB);
  int dA = std::abs((int)got[3] - (int)kExpA);
  bool match = dimsOk && dR <= kTol && dG <= kTol && dB <= kTol && dA <= kTol;

  bool pass = dimsOk && nonDegenerate && match;
  std::printf("[selftest-combinematerialchannelsresident] out=%ux%u(want %ux%u,dimsOk=%d) "
              "want=(%u,%u,%u,%u) got=(%u,%u,%u,%u) d=(%d,%d,%d,%d) match(<=%d)=%d nonDeg=%d "
              "injectBug=%d -> %s\n",
              ow, oh, kW, kH, dimsOk ? 1 : 0, kExpR, kExpG, kExpB, kExpA, got[0], got[1], got[2],
              got[3], dR, dG, dB, dA, kTol, match ? 1 : 0, nonDegenerate ? 1 : 0, injectBug ? 1 : 0,
              pass ? "PASS" : "FAIL");

  mlib->release(); q->release(); dev->release(); pool->release();
  return pass ? 0 : 1;
}

}  // namespace sw

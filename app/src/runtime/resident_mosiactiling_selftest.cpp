// Headless RED->GREEN proof that the MosiacTiling leaf works in the RESIDENT (production) cook path —
// not only in the flat --selftest-mosiactiling. Same structure as resident_combinematerialchannels_-
// selftest.cpp (the sibling 2/3-input resident pattern): RenderTarget producers feed the two fixed
// Texture2D inputs (Image @ port0, FxImage @ port1) of the MosiacTiling node, then cookResident drives
// the production tex-cook gather + the OWN pixel shader.
//   (1) cookResident must cook + bind BOTH fixed textures (inputTextures[0]=Image, [1]=FxImage),
//   (2) run the quadtree mosaic where the FxImage (2nd input) DRIVES the subdivision depth.
// The DETERMINISTIC config (hand-derived via the shader sim, same as the flat golden):
//   Image   = FLAT solid (value irrelevant at the assert pixel — gap=0 there),
//   FxImage = DIAGONAL gradient g=(u+v)/2 (its differing diagonal corners drive ONE subdivision),
//   Size=0.5 MaxSubdivisions=2 SubdivisionThreshold=0.3 Padding=0.75 Feather=0.25 GapColor=(0,0,1,1).
//   At texel (8,8) of a 32x32 output the cell subdivides once → at the finer cell center the gap
//   factor is 0 → out = pure GapColor = (0,0,255,255) (blue).
// injectBug: OMIT the FxImage wire → the gather binds null for port1 → the cook's unwired-FxImage fork
//   substitutes the FLAT Image → corner distance 0 → NO subdivision → out ≈ Image (~122,146,182) →
//   diverges >>tol → RED (exercises the 2nd Texture2D port in the RESIDENT gather).
#include "runtime/point_graph.h"

#include <algorithm>
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
constexpr uint32_t kPX = 8, kPY = 8;  // assert texel (uv center 0.265625,0.265625)
constexpr uint8_t kImg_r = 128, kImg_g = 153, kImg_b = 179;  // flat Image (value-independent at assert px)
constexpr uint8_t kExpR = 0, kExpG = 0, kExpB = 255, kExpA = 255;  // pure GapColor (blue) at texel(8,8)

// RenderTarget cook OVERRIDE: ClearColor.x selects which source to paint —
//   0 -> flat Image solid; 1 -> diagonal gradient FxImage g=(u+v)/2.
void texSource(TexCookCtx& c) {
  if (!c.output) return;
  const uint32_t w = (uint32_t)c.output->width(), h = (uint32_t)c.output->height();
  const int which = (int)std::lround(cookParam(c, "ClearColor.x", 0.0f));
  std::vector<uint8_t> px((size_t)w * h * 4, 0);
  if (which == 1) {
    for (uint32_t y = 0; y < h; ++y) {
      for (uint32_t x = 0; x < w; ++x) {
        const float u = ((float)x + 0.5f) / (float)w;
        const float v = ((float)y + 0.5f) / (float)h;
        const float g = (u + v) * 0.5f;
        const uint8_t gb = (uint8_t)std::lround(std::clamp(g, 0.0f, 1.0f) * 255.0f);
        size_t i = ((size_t)y * w + x) * 4;
        px[i + 0] = gb; px[i + 1] = gb; px[i + 2] = gb; px[i + 3] = 255;
      }
    }
  } else {
    for (size_t i = 0; i < (size_t)w * h; ++i) {
      px[i * 4 + 0] = kImg_r; px[i * 4 + 1] = kImg_g; px[i * 4 + 2] = kImg_b; px[i * 4 + 3] = 255;
    }
  }
  c.output->replaceRegion(MTL::Region::Make2D(0, 0, w, h), 0, px.data(), w * 4);
}

Symbol atomicOp(const char* id, std::vector<SlotDef> ins, std::vector<SlotDef> outs) {
  Symbol s; s.id = id; s.name = id; s.atomic = true;
  s.inputDefs = std::move(ins); s.outputDefs = std::move(outs);
  return s;
}

bool cookResidentCenter(MTL::Device* dev, MTL::CommandQueue* q, MTL::Library* mlib, bool wireFxImage,
                        uint8_t out[4], uint32_t& ow, uint32_t& oh) {
  registerTexOp("RenderTarget", texSource);

  SymbolLibrary lib;
  lib.symbols["RenderTarget"] = atomicOp(
      "RenderTarget",
      {{"command", "command", "Command", 0.0f}, {"Resolution", "Resolution", "Float", 0.0f},
       {"CustomW", "CustomW", "Float", 512.0f}, {"CustomH", "CustomH", "Float", 512.0f},
       {"ClearColor.x", "ClearColor.x", "Float", 0.0f}},
      {{"out", "out", "Texture2D", 0.0f}});
  lib.symbols["MosiacTiling"] = atomicOp(
      "MosiacTiling",
      {{"Image", "Image", "Texture2D", 0.0f},
       {"FxImage", "FxImage", "Texture2D", 0.0f},
       {"Center.x", "Center.x", "Float", 0.0f}, {"Center.y", "Center.y", "Float", 0.0f},
       {"Stretch.x", "Stretch.x", "Float", 1.0f}, {"Stretch.y", "Stretch.y", "Float", 1.0f},
       {"Size", "Size", "Float", 0.2f},
       {"SubdivisionThreshold", "SubdivisionThreshold", "Float", 0.0f},
       {"MaxSubdivisions", "MaxSubdivisions", "Float", 4.0f},
       {"Randomize", "Randomize", "Float", 0.0f},
       {"Padding", "Padding", "Float", 0.0f},
       {"Feather", "Feather", "Float", 0.0f},
       {"GapColor.r", "GapColor.r", "Float", 0.0f}, {"GapColor.g", "GapColor.g", "Float", 0.0f},
       {"GapColor.b", "GapColor.b", "Float", 0.0f}, {"GapColor.a", "GapColor.a", "Float", 1.0f},
       {"MixOriginal", "MixOriginal", "Float", 1.0f},
       {"Resolution", "Resolution", "Float", 0.0f}},
      {{"out", "out", "Texture2D", 0.0f}});

  Symbol root; root.id = "Root"; root.name = "Root"; root.atomic = false;
  root.outputDefs = {{"out", "out", "Texture2D", 0.0f}};
  SymbolChild c1; c1.id = 1; c1.symbolId = "RenderTarget";  // Image (ClearColor.x=0 default)
  SymbolChild c2; c2.id = 2; c2.symbolId = "RenderTarget"; c2.overrides["ClearColor.x"] = 1.0f;  // FxImage gradient
  SymbolChild c3; c3.id = 3; c3.symbolId = "MosiacTiling";
  // The deterministic FX-driven-subdivision config (matches the flat golden):
  c3.overrides["Size"] = 0.5f;
  c3.overrides["SubdivisionThreshold"] = 0.3f;
  c3.overrides["MaxSubdivisions"] = 2.0f;
  c3.overrides["Padding"] = 0.75f;
  c3.overrides["Feather"] = 0.25f;
  c3.overrides["GapColor.b"] = 1.0f;   // GapColor = (0,0,1,1) blue
  c3.overrides["GapColor.a"] = 1.0f;
  c3.overrides["MixOriginal"] = 1.0f;
  c3.overrides["Resolution"] = 0.0f;
  root.children = {c1, c2, c3};
  root.connections = {{1, "out", 3, "Image"}, {3, "out", kSymbolBoundary, "out"}};
  if (wireFxImage) root.connections.push_back({2, "out", 3, "FxImage"});
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
  size_t i = ((size_t)kPY * kW + kPX) * 4;
  out[0] = px[i]; out[1] = px[i + 1]; out[2] = px[i + 2]; out[3] = px[i + 3];
  return true;
}

}  // namespace

int runResidentMosiacTilingSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();

  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue* q = dev->newCommandQueue();
  NS::Error* err = nullptr;
  MTL::Library* mlib =
      dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
  if (!mlib) {
    std::printf("[selftest-mosiactilingresident] FAIL: no metallib\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }

  const int kTol = 2;
  uint8_t got[4] = {0, 0, 0, 0};
  uint32_t ow = 0, oh = 0;
  bool gotOk = cookResidentCenter(dev, q, mlib, /*wireFxImage=*/!injectBug, got, ow, oh);
  bool dimsOk = gotOk && ow == kW && oh == kH;

  int dR = std::abs((int)got[0] - (int)kExpR);
  int dG = std::abs((int)got[1] - (int)kExpG);
  int dB = std::abs((int)got[2] - (int)kExpB);
  int dA = std::abs((int)got[3] - (int)kExpA);
  bool match = dimsOk && dR <= kTol && dG <= kTol && dB <= kTol && dA <= kTol;

  bool pass = dimsOk && match;
  std::printf("[selftest-mosiactilingresident] out=%ux%u(want %ux%u,dimsOk=%d) "
              "want=(%u,%u,%u,%u) got=(%u,%u,%u,%u) d=(%d,%d,%d,%d) match(<=%d)=%d "
              "injectBug=%d -> %s\n",
              ow, oh, kW, kH, dimsOk ? 1 : 0, kExpR, kExpG, kExpB, kExpA, got[0], got[1], got[2],
              got[3], dR, dG, dB, dA, kTol, match ? 1 : 0, injectBug ? 1 : 0,
              pass ? "PASS" : "FAIL");

  mlib->release(); q->release(); dev->release(); pool->release();
  return pass ? 0 : 1;
}

}  // namespace sw

// Headless RED->GREEN proof that the DistortAndShade leaf works in the RESIDENT (production) cook path
// — not only in the flat --selftest-distortandshade. The live app drives cookResident (frame_cook.cpp),
// so DistortAndShade (an image filter with TWO graph-wired Texture2D inputs — the multi-image seam) must,
// in resident:
//   (1) have BOTH upstream textures cooked + bound: cookTexNode recurses each Texture2D input port in
//       spec order into inputTextures[0]=ImageA / inputTextures[1]=ImageB,
//   (2) sample ImageA at the ImageB-driven uv2 and produce the hand-derived ramp output.
// Cut 52 lesson: a flat-only golden can let live garbage through; the resident hook must be DRIVEN
// (cookResident -> cookTexNode -> leaf -> displayTex -> target()), not merely code-mirrored. Cut 58
// lesson: the multi-image seam is the SAME path Displace uses; this is its second resident consumer.
//
// Controlled inputs (same as the flat golden): ImageA = a horizontal luminance RAMP, ImageB = a UNIFORM
// constant (kB=128 -> 0.502). Two RenderTarget source ops paint them; DistortAndShade#3 reads ImageA <-
// RT#1, ImageB <- RT#2. Shading=0, Center=(0,0.5), Displacement=1.0 -> output = ramp sampled at uv2.x =
// uv.x*(1+B*D). We pin the center row to the hand-derived ramp values (identical to the flat golden).
//
// injectBug: a SECOND-INPUT wiring perturbation — OMIT the ImageB wire (RT#2 -> DistortAndShade#3.ImageB).
// With ImageB unwired the leaf's fork samples ImageA (the ramp) as displaceAmount -> a different,
// nonlinear magnification -> the center-row pins diverge by >>2 -> RED. This exercises the multi-image
// gather itself (the second Texture2D port really must be threaded).
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

constexpr uint32_t kW = 64, kH = 64;
constexpr uint8_t kB = 128;  // ImageB uniform = 0.50196

// HARDCODED hand-derived pins (identical to the flat golden's kDasPins — same kernel, same inputs,
// same Center/Displacement/Shading). center row y=H/2.
struct DasPin { uint32_t x; uint8_t v; };
constexpr DasPin kPins[] = {
    {8, 50}, {16, 98}, {24, 147}, {40, 244},
};

// RenderTarget cook OVERRIDE (one painter, two patterns). findSpec only resolves REGISTERED node
// types, so the two sources must be a real registered type ("RenderTarget"). We override its cook with
// a single painter that picks the pattern from the node's ClearColor.x param (a real RenderTarget port):
// ClearColor.x==0 -> horizontal luminance RAMP (ImageA), ClearColor.x==1 -> uniform kB const (ImageB).
void texSource(TexCookCtx& c) {
  if (!c.output) return;
  const uint32_t w = (uint32_t)c.output->width(), h = (uint32_t)c.output->height();
  const bool wantConst = cookParam(c, "ClearColor.x", 0.0f) > 0.5f;
  std::vector<uint8_t> px((size_t)w * h * 4, 0);
  if (wantConst) {
    for (size_t i = 0; i < (size_t)w * h; ++i) {
      px[i * 4 + 0] = kB; px[i * 4 + 1] = kB; px[i * 4 + 2] = kB; px[i * 4 + 3] = 255;
    }
  } else {
    for (uint32_t y = 0; y < h; ++y)
      for (uint32_t x = 0; x < w; ++x) {
        size_t i = ((size_t)y * w + x) * 4;
        uint8_t v = (uint8_t)std::lround((double)x * 255.0 / (double)(w - 1));
        px[i] = v; px[i + 1] = v; px[i + 2] = v; px[i + 3] = 255;
      }
  }
  c.output->replaceRegion(MTL::Region::Make2D(0, 0, w, h), 0, px.data(), w * 4);
}

Symbol atomicOp(const char* id, std::vector<SlotDef> ins, std::vector<SlotDef> outs) {
  Symbol s; s.id = id; s.name = id; s.atomic = true;
  s.inputDefs = std::move(ins); s.outputDefs = std::move(outs);
  return s;
}

// Cook RenderTarget#1(ramp) + RenderTarget#2(const) -> DistortAndShade#3 through cookResident; read
// back the center row. wireImageB=false -> OMIT the ImageB connection (injectBug: the multi-image
// gather loses its 2nd input).
bool cookResidentRow(MTL::Device* dev, MTL::CommandQueue* q, MTL::Library* mlib, bool wireImageB,
                     std::vector<uint8_t>& out, uint32_t& ow, uint32_t& oh) {
  registerTexOp("RenderTarget", texSource);  // override RenderTarget's cook with the two-pattern painter

  SymbolLibrary lib;
  lib.symbols["RenderTarget"] = atomicOp(
      "RenderTarget",
      {{"command", "command", "Command", 0.0f}, {"Resolution", "Resolution", "Float", 0.0f},
       {"CustomW", "CustomW", "Float", 512.0f}, {"CustomH", "CustomH", "Float", 512.0f},
       {"ClearColor.x", "ClearColor.x", "Float", 0.0f}},
      {{"out", "out", "Texture2D", 0.0f}});
  lib.symbols["DistortAndShade"] = atomicOp(
      "DistortAndShade",
      {{"ImageA", "ImageA", "Texture2D", 0.0f},
       {"ImageB", "ImageB", "Texture2D", 0.0f},
       {"Displacement", "Displacement", "Float", 0.5f},
       {"Shading", "Shading", "Float", 0.0f},
       {"Center.x", "Center.x", "Float", 0.5f},
       {"Center.y", "Center.y", "Float", 0.5f},
       {"Resolution", "Resolution", "Float", 0.0f}},
      {{"out", "out", "Texture2D", 0.0f}});
  Symbol root; root.id = "Root"; root.name = "Root"; root.atomic = false;
  root.outputDefs = {{"out", "out", "Texture2D", 0.0f}};
  SymbolChild c1; c1.id = 1; c1.symbolId = "RenderTarget";  // ImageA = ramp (ClearColor.x=0 default)
  SymbolChild c2; c2.id = 2; c2.symbolId = "RenderTarget";  // ImageB = const 0.502
  c2.overrides["ClearColor.x"] = 1.0f;                       // pick the uniform-const pattern
  SymbolChild c3; c3.id = 3; c3.symbolId = "DistortAndShade";
  c3.overrides["Displacement"] = 1.0f;
  c3.overrides["Shading"] = 0.0f;
  c3.overrides["Center.x"] = 0.0f;   // displacement radiates from the left edge
  c3.overrides["Center.y"] = 0.5f;
  c3.overrides["Resolution"] = 0.0f;
  root.children = {c1, c2, c3};
  root.connections = {{1, "out", 3, "ImageA"}, {3, "out", kSymbolBoundary, "out"}};
  if (wireImageB) root.connections.push_back({2, "out", 3, "ImageB"});
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
  out.assign((size_t)ow * oh * 4, 0);
  tex->getBytes(out.data(), ow * 4, MTL::Region::Make2D(0, 0, ow, oh), 0);
  return true;
}

}  // namespace

int runResidentDistortAndShadeSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();

  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue* q = dev->newCommandQueue();
  NS::Error* err = nullptr;
  MTL::Library* mlib =
      dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
  if (!mlib) {
    std::printf("[selftest-distortandshaderesident] FAIL: no metallib\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }

  const uint32_t Y = kH / 2;
  const int kTol = 2;

  // injectBug -> drop the ImageB wire (the multi-image gather loses its 2nd input -> ramp self-displace).
  std::vector<uint8_t> got;
  uint32_t ow = 0, oh = 0;
  bool gotOk = cookResidentRow(dev, q, mlib, /*wireImageB=*/!injectBug, got, ow, oh);
  bool dimsOk = gotOk && ow == kW && oh == kH;

  auto lum = [&](const std::vector<uint8_t>& v, uint32_t x) {
    return (int)v[((size_t)Y * kW + x) * 4];
  };

  bool reshapes = false;
  for (size_t i = 1; i < sizeof(kPins) / sizeof(kPins[0]); ++i)
    if (std::abs((int)kPins[i].v - (int)kPins[0].v) > 3) reshapes = true;

  bool matchPins = dimsOk;
  int maxDelta = 0;
  if (dimsOk)
    for (const DasPin& p : kPins) {
      int d = std::abs(lum(got, p.x) - (int)p.v);
      maxDelta = std::max(maxDelta, d);
      if (d > kTol) matchPins = false;
    }

  bool pass = dimsOk && reshapes && matchPins;
  std::printf("[selftest-distortandshaderesident] out=%ux%u(want %ux%u,dimsOk=%d) reshapes=%d "
              "pins maxDelta=%d match(<=%d)=%d injectBug=%d -> %s\n",
              ow, oh, kW, kH, dimsOk ? 1 : 0, reshapes ? 1 : 0, maxDelta, kTol, matchPins ? 1 : 0,
              injectBug ? 1 : 0, pass ? "PASS" : "FAIL");
  if (dimsOk)
    for (const DasPin& p : kPins)
      std::printf("  pin px(%u,%u) want=%d got=%d\n", p.x, Y, p.v, lum(got, p.x));

  mlib->release(); q->release(); dev->release(); pool->release();
  return pass ? 0 : 1;
}

}  // namespace sw

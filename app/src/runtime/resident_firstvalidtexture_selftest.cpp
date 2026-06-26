// Headless RED->GREEN proof that FirstValidTexture's VARIABLE-N MultiInputSlot<Texture2D> gather works
// on the RESIDENT (production) cook path — reusing the proven seam (PickTexture, commit 0fd14a4). The
// live app drives cookResident (frame_cook.cpp); FirstValidTexture must, in resident:
//   (1) gather its `Input` port's wires into CONSECUTIVE inputTextures[0..N) (primary wire +
//       ri->extraConns) — the seam under test,
//   (2) forward the FIRST NON-NULL gathered texture (TiXL: first connections[i].GetValue() != null).
// All RenderTarget sources cook to NON-null textures, so "first non-null" == the FIRST WIRED input in
// wire-declaration order. The tooth: which color comes out depends on WHICH wire is first in the gather.
//
// THE TOOTH (gather order + first-non-null): TWO RenderTarget sources paint two DISTINCT solids and wire
// into the ONE `Input` multiInput port. Wire order = input[0] (RT#1 green), input[1] (RT#2 blue).
// FirstValidTexture returns the first non-null = input[0] = GREEN. If the gather dropped the primary
// wire and only saw the extraConn, input[0] would be blue -> the green pin misses.
//
// injectBug: OMIT the FIRST wire (RT#1 -> Input). Now only RT#2 (blue) gathers as input[0] -> first
// non-null = blue -> the green pin misses -> RED. (This is the wire-level analogue of the leaf's
// g_firstValidNullFirst: dropping the first valid input shifts the selection to the next one.)
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
constexpr uint8_t k0r = 20,  k0g = 200, k0b = 20;   // input[0] (RT#1, green) <- first non-null -> selected
constexpr uint8_t k1r = 30,  k1g = 60,  k1b = 240;  // input[1] (RT#2, blue)  <- injectBug fall-through
constexpr uint8_t kExpR = k0r, kExpG = k0g, kExpB = k0b, kExpA = 255;

// RenderTarget cook OVERRIDE (one painter, two patterns) — same trick as resident_picktexture: the
// sources are real registered "RenderTarget" nodes; their cook picks the solid from ClearColor.x.
void texSource(TexCookCtx& c) {
  if (!c.output) return;
  const uint32_t w = (uint32_t)c.output->width(), h = (uint32_t)c.output->height();
  const int which = (int)std::lround(cookParam(c, "ClearColor.x", 0.0f));
  uint8_t r = k0r, g = k0g, b = k0b;
  if (which == 1) { r = k1r; g = k1g; b = k1b; }
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

// Cook RT#1 + RT#2 -> FirstValidTexture#3 (both wired into the ONE multiInput Input port) through
// cookResident; read back the center pixel. wireFirst=false -> OMIT the 1st wire (injectBug).
bool cookResidentCenter(MTL::Device* dev, MTL::CommandQueue* q, MTL::Library* mlib, bool wireFirst,
                        uint8_t out[4], uint32_t& ow, uint32_t& oh) {
  registerTexOp("RenderTarget", texSource);  // override RenderTarget's cook with the two-pattern painter

  SymbolLibrary lib;
  lib.symbols["RenderTarget"] = atomicOp(
      "RenderTarget",
      {{"command", "command", "Command", 0.0f}, {"Resolution", "Resolution", "Float", 0.0f},
       {"CustomW", "CustomW", "Float", 512.0f}, {"CustomH", "CustomH", "Float", 512.0f},
       {"ClearColor.x", "ClearColor.x", "Float", 0.0f}},
      {{"out", "out", "Texture2D", 0.0f}});
  // FirstValidTexture: ONE Texture2D input slot "Input"; the multiInput flag is read from the REGISTERED
  // NodeSpec (registrar in point_ops_firstvalidtexture.cpp set it), NOT from this SlotDef.
  lib.symbols["FirstValidTexture"] = atomicOp(
      "FirstValidTexture",
      {{"Input", "Input", "Texture2D", 0.0f},
       {"Resolution", "Resolution", "Float", 0.0f}},
      {{"out", "out", "Texture2D", 0.0f}});
  Symbol root; root.id = "Root"; root.name = "Root"; root.atomic = false;
  root.outputDefs = {{"out", "out", "Texture2D", 0.0f}};
  SymbolChild c1; c1.id = 1; c1.symbolId = "RenderTarget";  // input[0] (ClearColor.x=0 -> green)
  SymbolChild c2; c2.id = 2; c2.symbolId = "RenderTarget"; c2.overrides["ClearColor.x"] = 1.0f;  // input[1] blue
  SymbolChild c3; c3.id = 3; c3.symbolId = "FirstValidTexture";
  c3.overrides["Resolution"] = 0.0f;   // WindowFollow
  root.children = {c1, c2, c3};
  // RT#2 wires into Input as the FIRST listed connection so that, with the primary wire (RT#1) present,
  // wire order = [RT#1, RT#2] (primary first). The resident flatten appends the 2nd+ wires to
  // ri->extraConns BECAUSE FirstValidTexture's Input port is multiInput.
  root.connections = {{2, "out", 3, "Input"},
                      {3, "out", kSymbolBoundary, "out"}};
  if (wireFirst) root.connections.insert(root.connections.begin(), {1, "out", 3, "Input"});
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

int runResidentFirstValidTextureSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();

  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue* q = dev->newCommandQueue();
  NS::Error* err = nullptr;
  MTL::Library* mlib =
      dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
  if (!mlib) {
    std::printf("[selftest-firstvalidtextureresident] FAIL: no metallib\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }

  const int kTol = 2;
  uint8_t got[4] = {0, 0, 0, 0};
  uint32_t ow = 0, oh = 0;
  // injectBug -> drop the 1st wire (RT#1); now first non-null = RT#2 (blue) -> green pin misses.
  bool gotOk = cookResidentCenter(dev, q, mlib, /*wireFirst=*/!injectBug, got, ow, oh);
  bool dimsOk = gotOk && ow == kW && oh == kH;

  // Non-degenerate: the SELECTED (input[0], green) solid differs from input[1] (blue, injectBug).
  bool nonDegenerate = (kExpR != k1r) || (kExpG != k1g) || (kExpB != k1b);

  int dR = std::abs((int)got[0] - (int)kExpR);
  int dG = std::abs((int)got[1] - (int)kExpG);
  int dB = std::abs((int)got[2] - (int)kExpB);
  int dA = std::abs((int)got[3] - (int)kExpA);
  bool match = dimsOk && dR <= kTol && dG <= kTol && dB <= kTol && dA <= kTol;

  bool pass = dimsOk && nonDegenerate && match;
  std::printf("[selftest-firstvalidtextureresident] out=%ux%u(want %ux%u,dimsOk=%d) "
              "want=(%u,%u,%u,%u) got=(%u,%u,%u,%u) d=(%d,%d,%d,%d) match(<=%d)=%d nonDeg=%d "
              "injectBug=%d -> %s\n",
              ow, oh, kW, kH, dimsOk ? 1 : 0, kExpR, kExpG, kExpB, kExpA, got[0], got[1], got[2],
              got[3], dR, dG, dB, dA, kTol, match ? 1 : 0, nonDegenerate ? 1 : 0, injectBug ? 1 : 0,
              pass ? "PASS" : "FAIL");

  mlib->release(); q->release(); dev->release(); pool->release();
  return pass ? 0 : 1;
}

}  // namespace sw

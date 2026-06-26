// Headless RED->GREEN proof that the VARIABLE-N MultiInputSlot<Texture2D> gather works on the RESIDENT
// (production) cook path — not only in the flat --selftest-picktexture. The live app drives cookResident
// (frame_cook.cpp), so PickTexture (the FIRST op with a genuinely variable-N MultiInput Texture2D port)
// must, in resident:
//   (1) gather ALL THREE wires of its single `Input` port into CONSECUTIVE inputTextures[0..2]:
//       cookTexNode's MultiInput Texture2D branch walks the primary wire (ri->srcNodePath) THEN
//       ri->extraConns (the 2nd+ wires) — this is the seam under test,
//   (2) forward inputTextures[Index mod N]; with Index=2, N=3 -> inputTextures[2].
// Cut 52 lesson: a flat-only golden can let live garbage through; the resident hook must be DRIVEN
// (cookResident -> cookTexNode MultiInput gather -> leaf -> displayTex -> target()), not code-mirrored.
//
// THE TOOTH (variable-N gather): three RenderTarget sources paint three DISTINCT solids and all wire into
// the ONE `Input` multiInput port (wire order = input[0],[1],[2]). PickTexture#4.Index=2 selects the 3rd.
// If the gather only collected the primary wire (or primary+1, i.e. dropped extraConns), inputTextureCount
// would be 1 or 2, and Index(2) mod count would select a DIFFERENT solid -> the pin misses. So a GREEN
// result proves all 3 wires (primary + 2 extraConns) reached consecutive inputTextures[] slots.
//
// injectBug: OMIT the 3rd wire (RT#3 -> PickTexture#4.Input). Now only 2 wires gather (N=2), Index(2) mod
// 2 = 0 -> selects input[0] (RT#1's solid) instead of input[2] (RT#3's) -> RED.
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
// Three distinct solids (one per source); Index=2 selects the THIRD.
constexpr uint8_t k0r = 200, k0g = 20,  k0b = 20;   // input[0] (RT#1)
constexpr uint8_t k1r = 20,  k1g = 200, k1b = 20;   // input[1] (RT#2)
constexpr uint8_t k2r = 30,  k2g = 60,  k2b = 240;  // input[2] (RT#3) <- selected
constexpr uint8_t kExpR = k2r, kExpG = k2g, kExpB = k2b, kExpA = 255;

// RenderTarget cook OVERRIDE (one painter, three patterns) — same trick as resident_combine3images: the
// three sources are real registered "RenderTarget" nodes; their cook is overridden with a painter that
// picks the solid from ClearColor.x (0 -> input[0], 1 -> input[1], 2 -> input[2]).
void texSource(TexCookCtx& c) {
  if (!c.output) return;
  const uint32_t w = (uint32_t)c.output->width(), h = (uint32_t)c.output->height();
  const int which = (int)std::lround(cookParam(c, "ClearColor.x", 0.0f));
  uint8_t r = k0r, g = k0g, b = k0b;
  if (which == 1) { r = k1r; g = k1g; b = k1b; }
  else if (which == 2) { r = k2r; g = k2g; b = k2b; }
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

// Cook RT#1 + RT#2 + RT#3 -> PickTexture#4 (all three wired into the ONE multiInput Input port) through
// cookResident; read back the center pixel. wireThird=false -> OMIT the 3rd wire (injectBug).
bool cookResidentCenter(MTL::Device* dev, MTL::CommandQueue* q, MTL::Library* mlib, bool wireThird,
                        uint8_t out[4], uint32_t& ow, uint32_t& oh) {
  registerTexOp("RenderTarget", texSource);  // override RenderTarget's cook with the three-pattern painter

  SymbolLibrary lib;
  lib.symbols["RenderTarget"] = atomicOp(
      "RenderTarget",
      {{"command", "command", "Command", 0.0f}, {"Resolution", "Resolution", "Float", 0.0f},
       {"CustomW", "CustomW", "Float", 512.0f}, {"CustomH", "CustomH", "Float", 512.0f},
       {"ClearColor.x", "ClearColor.x", "Float", 0.0f}},
      {{"out", "out", "Texture2D", 0.0f}});
  // PickTexture: ONE Texture2D input slot "Input" (the multiInput flag is read from the REGISTERED
  // NodeSpec via findSpec, NOT from this SlotDef — the registrar in point_ops_picktexture.cpp set it).
  lib.symbols["PickTexture"] = atomicOp(
      "PickTexture",
      {{"Input", "Input", "Texture2D", 0.0f},
       {"Index", "Index", "Float", 0.0f},
       {"Resolution", "Resolution", "Float", 0.0f}},
      {{"out", "out", "Texture2D", 0.0f}});
  Symbol root; root.id = "Root"; root.name = "Root"; root.atomic = false;
  root.outputDefs = {{"out", "out", "Texture2D", 0.0f}};
  SymbolChild c1; c1.id = 1; c1.symbolId = "RenderTarget";  // input[0] (ClearColor.x=0 default)
  SymbolChild c2; c2.id = 2; c2.symbolId = "RenderTarget"; c2.overrides["ClearColor.x"] = 1.0f;  // input[1]
  SymbolChild c3; c3.id = 3; c3.symbolId = "RenderTarget"; c3.overrides["ClearColor.x"] = 2.0f;  // input[2]
  SymbolChild c4; c4.id = 4; c4.symbolId = "PickTexture";
  c4.overrides["Index"] = 2.0f;        // select the 3rd gathered input
  c4.overrides["Resolution"] = 0.0f;   // WindowFollow
  root.children = {c1, c2, c3, c4};
  // ALL THREE sources wire into the SAME `Input` port (wire order = primary, extraConn, extraConn). The
  // resident flatten appends the 2nd+ wires to ri->extraConns BECAUSE the registered PickTexture spec's
  // Input port is multiInput — so cookTexNode's MultiInput branch gathers them into inputTextures[0..2].
  root.connections = {{1, "out", 4, "Input"},
                      {2, "out", 4, "Input"},
                      {4, "out", kSymbolBoundary, "out"}};
  if (wireThird) root.connections.push_back({3, "out", 4, "Input"});
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

int runResidentPickTextureSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();

  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue* q = dev->newCommandQueue();
  NS::Error* err = nullptr;
  MTL::Library* mlib =
      dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
  if (!mlib) {
    std::printf("[selftest-picktextureresident] FAIL: no metallib\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }

  const int kTol = 2;
  uint8_t got[4] = {0, 0, 0, 0};
  uint32_t ow = 0, oh = 0;
  // injectBug -> drop the 3rd wire (gather loses its 3rd input -> Index(2) mod 2 = 0 -> input[0]).
  bool gotOk = cookResidentCenter(dev, q, mlib, /*wireThird=*/!injectBug, got, ow, oh);
  bool dimsOk = gotOk && ow == kW && oh == kH;

  // Non-degenerate: the SELECTED (3rd) solid differs from input[0] (what injectBug falls back to).
  bool nonDegenerate = (kExpR != k0r) || (kExpG != k0g) || (kExpB != k0b);

  int dR = std::abs((int)got[0] - (int)kExpR);
  int dG = std::abs((int)got[1] - (int)kExpG);
  int dB = std::abs((int)got[2] - (int)kExpB);
  int dA = std::abs((int)got[3] - (int)kExpA);
  bool match = dimsOk && dR <= kTol && dG <= kTol && dB <= kTol && dA <= kTol;

  bool pass = dimsOk && nonDegenerate && match;
  std::printf("[selftest-picktextureresident] out=%ux%u(want %ux%u,dimsOk=%d) "
              "want=(%u,%u,%u,%u) got=(%u,%u,%u,%u) d=(%d,%d,%d,%d) match(<=%d)=%d nonDeg=%d "
              "injectBug=%d -> %s\n",
              ow, oh, kW, kH, dimsOk ? 1 : 0, kExpR, kExpG, kExpB, kExpA, got[0], got[1], got[2],
              got[3], dR, dG, dB, dA, kTol, match ? 1 : 0, nonDegenerate ? 1 : 0, injectBug ? 1 : 0,
              pass ? "PASS" : "FAIL");

  mlib->release(); q->release(); dev->release(); pool->release();
  return pass ? 0 : 1;
}

}  // namespace sw

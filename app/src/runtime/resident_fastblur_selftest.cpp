// Headless RED->GREEN proof that the FastBlur MULTI-PASS COMPUTE leaf works in the RESIDENT
// (production) cook path — not only in the flat --selftest-fastblur. The live app drives cookResident
// (frame_cook.cpp), so FastBlur (a multi-pass ImageFilterComputeOp) must, in resident:
//   (1) get MTL::TextureUsageShaderWrite on its output (imageFilterComputeTypes()/needsWrite) so the
//       final-up RWTexture2D write lands — proven by full coverage + the soft-edge / energy probes,
//   (2) issue its N down + N up dispatches over the per-level scratch textures (the multi-pass
//       cachedScratchTex shaderWrite seam) inside the leaf, with the upstream texture fed in via the
//       resident gather — proven by the blurred output differing from the hard-edged input.
// Cut 52/53 lesson: a flat-only golden can let live garbage through; the resident hook must be DRIVEN
// (cookResident -> cookTexNode -> leaf multi-pass -> displayTex -> target()), not merely code-mirrored.
//
// Topology: RenderTarget (a REAL registered tex op, its cook fn overridden here to paint a white
// square on black into its own output — the parity-selftest pattern, so findSpec resolves it) ->
// FastBlur terminal. We cook through cookResident with FastBlur as targetPath, read back pg.target()
// (= displayTex = FastBlur's full-res output) and assert energy-conservation + edge-softening.
//
// injectBug: paint a SOLID field (no edge) instead of a square. Then there is no hard boundary for the
// blur to soften and no black region for energy to bleed into -> the spread-outside / edge-softened
// probes can't fire -> RED. A real pattern perturbation that proves the readback checks cooked pixels.
#include "runtime/point_graph.h"

#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/compound_graph.h"       // SymbolLibrary / Symbol / SymbolChild / SymbolConnection
#include "runtime/resident_eval_graph.h"  // buildEvalGraph / kSymbolBoundary
#include "runtime/tixl_point.h"           // EvaluationContext

#ifndef SW_SHADER_METALLIB
#define SW_SHADER_METALLIB "shaders.metallib"
#endif

namespace sw {
namespace {

// Window / source geometry. 100x100 (non-8-divisible) -> the RenderTarget source. A white square
// centered, so the blur has a hard edge to soften and black margins for energy to bleed into.
constexpr uint32_t kW = 100, kH = 100;
constexpr uint32_t kBlk = 20;
constexpr uint32_t kSX0 = (kW - kBlk) / 2;  // 40
constexpr uint32_t kSY0 = (kH - kBlk) / 2;  // 40
constexpr uint32_t kSX1 = kSX0 + kBlk;      // 60 (exclusive)
constexpr uint32_t kSY1 = kSY0 + kBlk;      // 60

bool g_bugSolidField = false;  // injectBug: paint a solid field (no edge) -> probes can't fire
bool g_toothHalfPlane = false;  // exact-pixel tooth: paint a vertical half-plane (x>=w/2 white)

// RenderTarget cook override: paint the white square (or, under bug, a solid field) into the node's
// own output (allocated by the cook at the Resolution-pin size). FastBlur reads this as its input.
void texSource(TexCookCtx& c) {
  if (!c.output) return;
  const uint32_t w = (uint32_t)c.output->width();
  const uint32_t h = (uint32_t)c.output->height();
  std::vector<uint8_t> px((size_t)w * h * 4, 0);
  for (uint32_t y = 0; y < h; ++y) {
    for (uint32_t x = 0; x < w; ++x) {
      size_t i = ((size_t)y * w + x) * 4;
      bool on;
      if (g_toothHalfPlane)
        on = (x >= w / 2);  // vertical half-plane (constant in Y) for the exact-pixel tooth
      else if (g_bugSolidField)
        on = true;
      else
        on = (x >= kSX0 && x < kSX1 && y >= kSY0 && y < kSY1);
      uint8_t v = on ? 255 : 0;
      px[i + 0] = v; px[i + 1] = v; px[i + 2] = v; px[i + 3] = 255;
    }
  }
  c.output->replaceRegion(MTL::Region::Make2D(0, 0, w, h), 0, px.data(), w * 4);
}

Symbol atomicOp(const char* id, std::vector<SlotDef> ins, std::vector<SlotDef> outs) {
  Symbol s; s.id = id; s.name = id; s.atomic = true;
  s.inputDefs = std::move(ins); s.outputDefs = std::move(outs);
  return s;
}

// --- EXACT-PIXEL tooth (resident path, closed-form MaxLevels=1) ----------------------------------
// Same reasoning as the flat tooth (point_ops_fastblur.cpp): the energy/spread/edge/coverage probes
// are invariant under WRONG KERNEL WEIGHTS, so this pins specific output texels to hand-computed
// correct-weight values. Driven through the SAME resident path (cookResident -> cookTexNode -> leaf
// multi-pass -> displayTex), at a small 16x16 window so the 1-level closed form is exact:
//   input = vertical half-plane (x>=8 white), constant in Y; MaxLevels=1 => down-once -> up-once.
//   DOWN (box*0.25): level0 (8 wide) ideal [0,0,0,63.75,191.25,255,255,255] -> RGBA8 [..,64,191,..].
//   UP (final, tight weights normalized /20; Y collapses 9 taps to {0.2,0.6,0.2} over {p-1,p,p+1},
//       p=gx/2-0.25): row 8 -> x5=29 x6=61 x7=102 x8=153 x9=194 (matches the unmodified GPU output).
// Bites both refuter perturbations: down 0.25->0.30 pushes out[6] 61->73; up tightDiag 1.0->4.0
// re-weights to {0.3125,0.375,0.3125} pushing out[5] 29->36, out[6] 61->68 — beyond +/-2.
bool residentFastBlurExactPixelTooth(MTL::Device* dev, MTL::CommandQueue* q, MTL::Library* mlib) {
  const uint32_t TW = 16, TH = 16;
  g_toothHalfPlane = true;

  SymbolLibrary lib;
  lib.symbols["RenderTarget"] = atomicOp(
      "RenderTarget",
      {{"command", "command", "Command", 0.0f}, {"Resolution", "Resolution", "Float", 0.0f},
       {"CustomW", "CustomW", "Float", 512.0f}, {"CustomH", "CustomH", "Float", 512.0f}},
      {{"out", "out", "Texture2D", 0.0f}});
  lib.symbols["FastBlur"] = atomicOp(
      "FastBlur",
      {{"Image", "Image", "Texture2D", 0.0f},
       {"MaxLevels", "MaxLevels", "Float", 0.0f},
       {"Resolution", "Resolution", "Float", 0.0f},
       {"CustomW", "CustomW", "Float", 512.0f}, {"CustomH", "CustomH", "Float", 512.0f}},
      {{"out", "out", "Texture2D", 0.0f}});
  Symbol root; root.id = "Root"; root.name = "Root"; root.atomic = false;
  root.outputDefs = {{"out", "out", "Texture2D", 0.0f}};
  SymbolChild c1; c1.id = 1; c1.symbolId = "RenderTarget";  // WindowFollow -> 16x16 source
  SymbolChild c2; c2.id = 2; c2.symbolId = "FastBlur";
  c2.overrides["MaxLevels"] = 1.0f;   // forced 1-level => closed-form down-once -> up-once
  c2.overrides["Resolution"] = 0.0f;  // WindowFollow -> 16x16 output
  root.children = {c1, c2};
  root.connections = {{1, "out", 2, "Image"}, {2, "out", kSymbolBoundary, "out"}};
  lib.symbols["Root"] = root; lib.rootId = "Root";
  ResidentEvalGraph rg = buildEvalGraph(lib, "Root");

  EvaluationContext ctx{};
  ctx.frameIndex = 0; ctx.time = 0.0f; ctx.deltaTime = 1.0f / 60.0f;

  PointGraph pg(dev, mlib, q, TW, TH);
  pg.cookResident(rg, ctx, /*reg=*/nullptr, /*targetPath=*/"2");

  MTL::Texture* out = pg.target();
  bool ok = out && (uint32_t)out->width() == TW && (uint32_t)out->height() == TH;
  std::vector<uint8_t> buf;
  if (ok) {
    buf.assign((size_t)TW * TH * 4, 0);
    out->getBytes(buf.data(), TW * 4, MTL::Region::Make2D(0, 0, TW, TH), 0);
  }
  auto R = [&](uint32_t x, uint32_t y) { return (int)buf[((size_t)y * TW + x) * 4]; };

  struct Pin { uint32_t x; int want; };
  const Pin pins[] = {{5, 29}, {6, 61}, {7, 102}, {8, 153}, {9, 194}};
  const int kTol = 2;
  for (const Pin& p : pins) {
    int got = ok ? R(p.x, 8) : -999;
    bool hit = ok && std::abs(got - p.want) <= kTol;
    if (!hit) ok = false;
    std::printf("[selftest-fastblurresident:tooth] px(%u,8)=%d want %d(+/-%d) %s\n", p.x, got,
                p.want, kTol, hit ? "ok" : "MISS");
  }

  g_toothHalfPlane = false;
  return ok;
}

}  // namespace

int runResidentFastBlurSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  g_bugSolidField = injectBug;

  // Override RenderTarget's cook fn with the pattern painter (a REAL registered op so findSpec
  // resolves it). FastBlur is already self-registered (ImageFilterComputeOp).
  registerTexOp("RenderTarget", texSource);

  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue* q = dev->newCommandQueue();
  NS::Error* err = nullptr;
  MTL::Library* mlib =
      dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
  if (!mlib) {
    std::printf("[selftest-fastblurresident] FAIL: no metallib\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }

  // Resident lib: RenderTarget#1 (Texture2D out) -> FastBlur#2.Image; FastBlur#2.out -> Root out.
  SymbolLibrary lib;
  lib.symbols["RenderTarget"] = atomicOp(
      "RenderTarget",
      {{"command", "command", "Command", 0.0f}, {"Resolution", "Resolution", "Float", 0.0f},
       {"CustomW", "CustomW", "Float", 512.0f}, {"CustomH", "CustomH", "Float", 512.0f}},
      {{"out", "out", "Texture2D", 0.0f}});
  lib.symbols["FastBlur"] = atomicOp(
      "FastBlur",
      {{"Image", "Image", "Texture2D", 0.0f},
       {"MaxLevels", "MaxLevels", "Float", 0.0f},
       {"Resolution", "Resolution", "Float", 0.0f},
       {"CustomW", "CustomW", "Float", 512.0f}, {"CustomH", "CustomH", "Float", 512.0f}},
      {{"out", "out", "Texture2D", 0.0f}});
  Symbol root; root.id = "Root"; root.name = "Root"; root.atomic = false;
  root.outputDefs = {{"out", "out", "Texture2D", 0.0f}};
  SymbolChild c1; c1.id = 1; c1.symbolId = "RenderTarget";  // WindowFollow -> 100x100 source
  SymbolChild c2; c2.id = 2; c2.symbolId = "FastBlur";
  c2.overrides["MaxLevels"] = 4.0f;   // fixed 4-level pyramid -> deterministic spread
  c2.overrides["Resolution"] = 0.0f;  // WindowFollow -> 100x100 output
  root.children = {c1, c2};
  root.connections = {
      {1, "out", 2, "Image"},
      {2, "out", kSymbolBoundary, "out"},
  };
  lib.symbols["Root"] = root; lib.rootId = "Root";
  ResidentEvalGraph rg = buildEvalGraph(lib, "Root");

  EvaluationContext ctx{};
  ctx.frameIndex = 0; ctx.time = 0.0f; ctx.deltaTime = 1.0f / 60.0f;

  PointGraph pg(dev, mlib, q, kW, kH);
  pg.cookResident(rg, ctx, /*reg=*/nullptr, /*targetPath=*/"2");

  MTL::Texture* out = pg.target();  // = displayTex = FastBlur's full-res output
  uint32_t ow = out ? (uint32_t)out->width() : 0;
  uint32_t oh = out ? (uint32_t)out->height() : 0;
  bool dimsOk = (ow == kW && oh == kH);

  std::vector<uint8_t> buf;
  if (dimsOk) {
    buf.assign((size_t)ow * oh * 4, 0);
    out->getBytes(buf.data(), ow * 4, MTL::Region::Make2D(0, 0, ow, oh), 0);
  }
  auto lum = [&](uint32_t x, uint32_t y) {
    size_t i = ((size_t)y * ow + x) * 4;
    return (int)buf[i] + buf[i + 1] + buf[i + 2];
  };

  // (1) SPREAD OUTSIDE: 5px beyond the (original) square edge is lit by bled energy. Under the bug
  // (solid field) that location was ALREADY solid white, so "lit" is trivially true there — the bug
  // is caught by (2)/(3) below, not this one. We still assert it for the GREEN path.
  int spreadLum = dimsOk ? lum(kSX1 + 5, (kSY0 + kSY1) / 2) : 0;
  bool spread = spreadLum > 10;

  // (2) EDGE SOFTENED: just inside the original right edge is dimmer than solid 765 (blur pulled
  // energy across the boundary). Under the bug (solid field, no edge) this pixel stays ~765 -> FAIL.
  int edgeLum = dimsOk ? lum(kSX1 - 2, (kSY0 + kSY1) / 2) : 0;
  bool edgeSoftened = dimsOk && edgeLum < 700;

  // (3) ENERGY band: total within [0.4x,1.8x] of the SQUARE input energy (BLK*BLK*765=306000). The
  // bug paints a SOLID field (100*100*765 = 7,650,000) -> total far above the band -> RED. This is
  // the load-bearing tooth: it proves the blur ran on the intended (square) input through resident.
  long total = 0;
  if (dimsOk)
    for (size_t i = 0; i < (size_t)ow * oh; ++i)
      total += (long)buf[i * 4 + 0] + buf[i * 4 + 1] + buf[i * 4 + 2];
  const long inTotal = (long)kBlk * kBlk * 765;  // 306000 (the SQUARE energy)
  bool energyOk = dimsOk && total > inTotal * 4 / 10 && total < inTotal * 18 / 10;

  // (4) COVERAGE: every output pixel opaque (alpha 255 from the final-up write). Without ShaderWrite
  // the kernel write is dropped -> not opaque.
  int unwritten = 0;
  if (dimsOk)
    for (size_t i = 0; i < (size_t)ow * oh; ++i)
      if (buf[i * 4 + 3] != 255) ++unwritten;
  bool covered = dimsOk && (unwritten == 0);

  // EXACT-PIXEL tooth: closed-form 1-level case (16x16 half-plane) through the SAME resident path,
  // pinning correct-weight pixels (catches a mis-WEIGHTED tap the band-checks above let through).
  // Reset g_bugSolidField first so the tooth cooks the intended half-plane regardless of injectBug
  // (it tests kernel WEIGHTS, orthogonal to the injectBug pattern perturbation; under injectBug the
  // probes above already RED'd pass to false, so this AND'd term cannot turn a -bug run green).
  g_bugSolidField = false;
  bool exactPix = residentFastBlurExactPixelTooth(dev, q, mlib);

  bool pass = dimsOk && spread && edgeSoftened && energyOk && covered && exactPix;
  std::printf("[selftest-fastblurresident] out=%ux%u(want %ux%u,dimsOk=%d) spreadOut=%d edge=%d"
              "(soft<700:%d) total=%ld(in=%ld,band:%d) unwritten=%d covered=%d exactPix=%d -> %s\n",
              ow, oh, kW, kH, dimsOk ? 1 : 0, spreadLum, edgeLum, edgeSoftened ? 1 : 0, total,
              inTotal, energyOk ? 1 : 0, unwritten, covered ? 1 : 0, exactPix ? 1 : 0,
              pass ? "PASS" : "FAIL");

  g_bugSolidField = false;
  mlib->release(); q->release(); dev->release(); pool->release();
  return pass ? 0 : 1;
}

}  // namespace sw

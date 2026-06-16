// Headless RED->GREEN proof that the RgbTV leaf works in the RESIDENT (production) cook path — not
// only in the flat --selftest-rgbtv. The live app drives cookResident (frame_cook.cpp), so RgbTV (an
// ImageFilterComputeOp that ALSO declares an asset texture @t1 + consumes the input-mip seam) must, in
// resident:
//   (1) get MTL::TextureUsageShaderWrite on its output (needsWrite) so the RWTexture2D write lands,
//   (2) generate input mips + bind input(mipped)@0 / noise-asset@1 / output@2 inside the leaf,
//   (3) have the ASSET-TEXTURE SEAM driven: cookTexNode resolves the registered key, calls the
//       registered decoder, caches the texture, and hands it via TexCookCtx::assetTexture.
// Cut 52 lesson: a flat-only golden can let live garbage through; the resident hook must be DRIVEN
// (cookResident -> cookTexNode -> leaf -> displayTex -> target()), not merely code-mirrored.
//
// The golden value is taken in a NOISE-INDEPENDENT region: GlitchAmount=0 collapses BOTH noise
// sources (asset noiseColor -> constant 0.03, procedural noiseDelta -> 0), so the output is a
// deterministic function of the (uniform-gray) input + the RGB-stripe/vignette math (same region the
// flat golden pins). We assert the resident output matches the flat-cooked GREEN reference exactly,
// and that the asset seam fired (a synthetic decoder shim records its invocation).
//
// injectBug: drop the RGB-stripe pattern (PatternAmount override 0) -> the stripe never blends in ->
// the center-row pixels collapse toward the plain image -> they diverge from GREEN -> RED. A real
// wiring perturbation, proving the readback checks cooked pixels.
#include "runtime/point_graph.h"

#include <cmath>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/compound_graph.h"
#include "runtime/image_filter_op_registry.h"  // setAssetTextureDecoder / clearImageFilterAssetCache
#include "runtime/resident_eval_graph.h"
#include "runtime/tixl_point.h"

#ifndef SW_SHADER_METALLIB
#define SW_SHADER_METALLIB "shaders.metallib"
#endif

namespace sw {
namespace {

constexpr uint32_t kW = 64, kH = 64;
constexpr uint8_t kGray = 128;
constexpr uint8_t kNoise = 204;  // synthetic noise texel = 0.8 -> abs(0.8-0.5)=0.3 (live noiseColor)

// HARDCODED correct-wiring pins (identical to the flat golden's kRgbTvPins — same kernel, same input,
// same GlitchAmount=0 region). Baked so a global param-routing error reddens the resident tooth too.
struct RgbTvPin { uint32_t x; uint8_t r, g, b; };
constexpr RgbTvPin kPins[] = {
    {30, 56, 67, 56}, {31, 67, 56, 56}, {32, 56, 56, 65}, {33, 56, 65, 56}, {34, 65, 57, 57},
};

// GlitchAmount=1.0 NOISE-PATH pins (identical to the flat golden's kRgbTvGlitchPins — same kernel,
// same uniform-gray input, same uniform noise=0.8 from the synthetic decoder, same GlitchAmount=1.0).
// Proves the resident path exercises the noise/distortion path, not just the noise-free stripe math.
constexpr RgbTvPin kGlitchPins[] = {
    {30, 48, 57, 48}, {31, 57, 47, 47}, {32, 47, 47, 56}, {33, 48, 56, 48}, {34, 57, 48, 48},
};

// Synthetic asset decoder: fabricates a known 8x8 noise texture (uniform kNoise=0.8) so the asset seam
// is exercised WITHOUT pulling ImageIO into runtime AND the GlitchAmount>0 noise path is deterministic.
// Records that it was invoked (proves cookTexNode drove the seam).
int g_decoderCalls = 0;
MTL::Texture* g_synthDev_lastTex = nullptr;
MTL::Texture* syntheticDecoder(MTL::Device* dev, const char* /*absPath*/, bool /*mipped*/) {
  ++g_decoderCalls;
  const uint32_t N = 8;
  MTL::TextureDescriptor* td =
      MTL::TextureDescriptor::texture2DDescriptor(MTL::PixelFormatRGBA8Unorm, N, N, false);
  td->setUsage(MTL::TextureUsageShaderRead);
  td->setStorageMode(MTL::StorageModeShared);
  MTL::Texture* t = dev->newTexture(td);
  std::vector<uint8_t> px((size_t)N * N * 4, kNoise);
  for (size_t i = 0; i < (size_t)N * N; ++i) px[i * 4 + 3] = 255;
  t->replaceRegion(MTL::Region::Make2D(0, 0, N, N), 0, px.data(), N * 4);
  return t;  // ownership transfers to cachedAssetTexture (released by clearImageFilterAssetCache)
}

bool g_bugDropPattern = false;

// RenderTarget cook override: paint a uniform mid-gray field into the node's own output. RgbTV reads
// this as its input; uniform input makes every mip LOD = gray so the blur/glow collapse to constant.
void texSource(TexCookCtx& c) {
  if (!c.output) return;
  const uint32_t w = (uint32_t)c.output->width();
  const uint32_t h = (uint32_t)c.output->height();
  std::vector<uint8_t> px((size_t)w * h * 4, 0);
  for (size_t i = 0; i < (size_t)w * h; ++i) {
    px[i * 4 + 0] = kGray; px[i * 4 + 1] = kGray; px[i * 4 + 2] = kGray; px[i * 4 + 3] = 255;
  }
  c.output->replaceRegion(MTL::Region::Make2D(0, 0, w, h), 0, px.data(), w * 4);
}

Symbol atomicOp(const char* id, std::vector<SlotDef> ins, std::vector<SlotDef> outs) {
  Symbol s; s.id = id; s.name = id; s.atomic = true;
  s.inputDefs = std::move(ins); s.outputDefs = std::move(outs);
  return s;
}

// Cook RenderTarget#1 -> RgbTV#2 through cookResident; read back the RgbTV output (= target()).
// glitchAmount selects the golden case (0 = noise-free stripe; 1 = noise path live). bugKind drives
// the injectBug perturbation: 0=none, 1=drop RGB stripe (PatternAmount=0, caseA), 2=kill noise shade
// (ShadeDistortion=0, caseB noise-path perturbation).
bool cookResidentRow(MTL::Device* dev, MTL::CommandQueue* q, MTL::Library* mlib, float glitchAmount,
                     int bugKind, std::vector<uint8_t>& out, uint32_t& ow, uint32_t& oh) {
  registerTexOp("RenderTarget", texSource);

  SymbolLibrary lib;
  lib.symbols["RenderTarget"] = atomicOp(
      "RenderTarget",
      {{"command", "command", "Command", 0.0f}, {"Resolution", "Resolution", "Float", 0.0f},
       {"CustomW", "CustomW", "Float", 512.0f}, {"CustomH", "CustomH", "Float", 512.0f}},
      {{"out", "out", "Texture2D", 0.0f}});
  lib.symbols["RgbTV"] = atomicOp(
      "RgbTV",
      {{"Image", "Image", "Texture2D", 0.0f},
       {"GlitchAmount", "GlitchAmount", "Float", 1.0f},
       {"PatternAmount", "PatternAmount", "Float", 0.2f},
       {"ShadeDistortion", "ShadeDistortion", "Float", 2.0f},
       {"Resolution", "Resolution", "Float", 0.0f},
       {"CustomW", "CustomW", "Float", 512.0f}, {"CustomH", "CustomH", "Float", 512.0f}},
      {{"out", "out", "Texture2D", 0.0f}});
  Symbol root; root.id = "Root"; root.name = "Root"; root.atomic = false;
  root.outputDefs = {{"out", "out", "Texture2D", 0.0f}};
  SymbolChild c1; c1.id = 1; c1.symbolId = "RenderTarget";  // WindowFollow -> kW x kH source
  SymbolChild c2; c2.id = 2; c2.symbolId = "RgbTV";
  c2.overrides["GlitchAmount"] = glitchAmount;  // 0 -> noise-free golden; 1 -> noise path live
  c2.overrides["PatternAmount"] = (bugKind == 1) ? 0.0f : 0.2f;     // bug 1: drop the RGB stripe
  c2.overrides["ShadeDistortion"] = (bugKind == 2) ? 0.0f : 2.0f;   // bug 2: kill the noise shade
  c2.overrides["Resolution"] = 0.0f;    // WindowFollow -> kW x kH output
  root.children = {c1, c2};
  root.connections = {{1, "out", 2, "Image"}, {2, "out", kSymbolBoundary, "out"}};
  lib.symbols["Root"] = root; lib.rootId = "Root";
  ResidentEvalGraph rg = buildEvalGraph(lib, "Root");

  EvaluationContext ctx{};
  ctx.frameIndex = 0; ctx.time = 0.0f; ctx.deltaTime = 1.0f / 60.0f;

  PointGraph pg(dev, mlib, q, kW, kH);
  pg.cookResident(rg, ctx, /*reg=*/nullptr, /*targetPath=*/"2");

  MTL::Texture* tex = pg.target();
  ow = tex ? (uint32_t)tex->width() : 0;
  oh = tex ? (uint32_t)tex->height() : 0;
  if (!tex || ow != kW || oh != kH) return false;
  out.assign((size_t)ow * oh * 4, 0);
  tex->getBytes(out.data(), ow * 4, MTL::Region::Make2D(0, 0, ow, oh), 0);
  return true;
}

}  // namespace

int runResidentRgbTvSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();

  // Register the synthetic asset decoder so the asset-texture seam (cookTexNode -> cachedAssetTexture)
  // fires in this isolated run. clearImageFilterAssetCache() at the end drops the fabricated texture.
  g_decoderCalls = 0;
  setAssetTextureDecoder(&syntheticDecoder);
  clearImageFilterAssetCache();

  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue* q = dev->newCommandQueue();
  NS::Error* err = nullptr;
  MTL::Library* mlib =
      dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
  if (!mlib) {
    std::printf("[selftest-rgbtvresident] FAIL: no metallib\n");
    setAssetTextureDecoder(nullptr);
    q->release(); dev->release(); pool->release();
    return 1;
  }

  const uint32_t Y = kH / 2;
  const int kTol = 2;

  // ===== CASE A: GlitchAmount=0 through the RESIDENT path (noise-free stripe). injectBug=bug 1. =====
  std::vector<uint8_t> got;
  uint32_t ow = 0, oh = 0;
  bool gotOk = cookResidentRow(dev, q, mlib, /*glitchAmount=*/0.0f, /*bugKind=*/injectBug ? 1 : 0, got,
                               ow, oh);
  bool dimsOk = gotOk;

  // ===== CASE B: GlitchAmount=1.0 through the RESIDENT path (noise path live, uniform noise=0.8 from
  // the synthetic decoder). injectBug=bug 2 (kill ShadeDistortion -> the noise shade vanishes). =====
  std::vector<uint8_t> gotB;
  uint32_t owB = 0, ohB = 0;
  bool gotOkB = cookResidentRow(dev, q, mlib, /*glitchAmount=*/1.0f, /*bugKind=*/injectBug ? 2 : 0, gotB,
                                owB, ohB);
  bool dimsOkB = gotOkB && owB == kW && ohB == kH;

  // The asset seam must have fired (decoder invoked via cookTexNode). >=1 proves cookTexNode drove it.
  bool assetSeamFired = (g_decoderCalls >= 1);

  auto px = [&](const std::vector<uint8_t>& v, uint32_t x, int ch) {
    return (int)v[((size_t)Y * kW + x) * 4 + ch];
  };

  // INVARIANT: the HARDCODED pins genuinely reshape the input gray (the stripe is real).
  bool reshapes = false;
  for (const RgbTvPin& p : kPins)
    if (std::abs((int)p.r - kGray) > 3 || std::abs((int)p.g - kGray) > 3 ||
        std::abs((int)p.b - kGray) > 3)
      reshapes = true;

  // EXACT-PIXEL match vs the HARDCODED correct-wiring pins through the RESIDENT path — both cases.
  bool matchA = dimsOk;
  int maxDeltaA = 0;
  if (dimsOk)
    for (const RgbTvPin& p : kPins) {
      int want[4] = {p.r, p.g, p.b, 255};
      for (int ch = 0; ch < 4; ++ch) {
        int d = std::abs(px(got, p.x, ch) - want[ch]);
        maxDeltaA = std::max(maxDeltaA, d);
        if (d > kTol) matchA = false;
      }
    }

  bool matchB = dimsOkB;
  int maxDeltaB = 0;
  if (dimsOkB)
    for (const RgbTvPin& p : kGlitchPins) {
      int want[4] = {p.r, p.g, p.b, 255};
      for (int ch = 0; ch < 4; ++ch) {
        int d = std::abs(px(gotB, p.x, ch) - want[ch]);
        maxDeltaB = std::max(maxDeltaB, d);
        if (d > kTol) matchB = false;
      }
    }

  // CROSS-CHECK: the noise case must genuinely differ from the noise-free case (noise path exercised).
  bool casesDiffer = false;
  if (dimsOk && dimsOkB)
    for (const RgbTvPin& p : kGlitchPins)
      for (int ch = 0; ch < 3; ++ch)
        if (std::abs(px(got, p.x, ch) - px(gotB, p.x, ch)) > kTol) casesDiffer = true;

  // Tooth: dims + asset seam fired + pins reshape + BOTH resident rows match their baked pins + the
  // noise case differs from the noise-free case. no-bug -> PASS. injectBug -> a row misses -> FAIL.
  bool pass = dimsOk && dimsOkB && assetSeamFired && reshapes && matchA && matchB && casesDiffer;

  std::printf("[selftest-rgbtvresident] out=%ux%u(want %ux%u,dimsOk=%d/%d) assetSeamFired=%d(calls=%d) "
              "reshapes=%d caseA(GA=0) maxDelta=%d match=%d caseB(GA=1,noise) maxDelta=%d match=%d "
              "casesDiffer=%d injectBug=%d -> %s\n",
              ow, oh, kW, kH, dimsOk ? 1 : 0, dimsOkB ? 1 : 0, assetSeamFired ? 1 : 0, g_decoderCalls,
              reshapes ? 1 : 0, maxDeltaA, matchA ? 1 : 0, maxDeltaB, matchB ? 1 : 0,
              casesDiffer ? 1 : 0, injectBug ? 1 : 0, pass ? "PASS" : "FAIL");
  if (dimsOk) {
    std::printf("  -- caseA (GlitchAmount=0) --\n");
    for (const RgbTvPin& p : kPins)
      std::printf("  pin px(%u,%u) want=(%d,%d,%d) got=(%d,%d,%d)\n", p.x, Y, p.r, p.g, p.b,
                  px(got, p.x, 0), px(got, p.x, 1), px(got, p.x, 2));
  }
  if (dimsOkB) {
    std::printf("  -- caseB (GlitchAmount=1, uniform noise=0.8) --\n");
    for (const RgbTvPin& p : kGlitchPins)
      std::printf("  pin px(%u,%u) want=(%d,%d,%d) got=(%d,%d,%d)\n", p.x, Y, p.r, p.g, p.b,
                  px(gotB, p.x, 0), px(gotB, p.x, 1), px(gotB, p.x, 2));
  }

  clearImageFilterAssetCache();
  setAssetTextureDecoder(nullptr);
  mlib->release(); q->release(); dev->release(); pool->release();
  return pass ? 0 : 1;
}

}  // namespace sw

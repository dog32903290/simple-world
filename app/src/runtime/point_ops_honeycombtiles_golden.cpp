// HoneyCombTiles EXACT-PIXEL GOLDEN (split from point_ops_honeycombtiles.cpp for the ≤400-line
// ratchet). --selftest-honeycombtiles. Three deterministic legs (no fwidth / half-decay pixels):
//
//  CASE A — BACKGROUND==FILL PLATEAU (position-independent exact pixel).  ImageA = uniform BLACK,
//    MixOriginal=0, and Fill := Background (both set to the .t3 Background (1,0.99999,0.99999,0.804)).
//    -> col = lerp(Background, Fill, c) = Background for ANY c (Fill==Background -> lerp is constant),
//       so the hex/LUT math (c) cannot perturb the result. MixOriginal=0 -> col unchanged.
//    -> output = Background = (1,0.99999,0.99999,0.804) for EVERY pixel.
//       RGBA8: R=255, G=round(0.99999*255)=255, B=255, A=round(0.804*255)=205.
//    BITES: t0 bind, the final lerp endpoints (Fill/Background routing), the MixOriginal=0 leg, the
//    whole cook+bind path. A swapped Fill/Background source, a broken lerp, or a wrong default would
//    move the plateau. (Deliberately LUT-independent: the LUT tooth is CASE C.)
//
//  CASE B — MIXORIGINAL=1 PASSTHROUGH (exact per-pixel).  ImageA = horizontal luminance ramp,
//    MixOriginal=1. -> col = lerp(col, ImageA.Sample(uv), 1) = ImageA.Sample(uv). At a texel center
//    uv=(x+0.5)/W lands exactly on texel x (Linear+Repeat, no neighbor blend) -> output = ImageA[x].
//    Pins 4 center-row columns against round(x*255/(W-1)). BITES: t0 sampling at the unmodified uv +
//    the MixOriginal=1 leg. Independent of the hex/LUT math (fully overwritten by MixOriginal=1).
//
//  CASE C — LUT/HEX DIFFERENTIAL TOOTH (the curve-bake bite + the `-bug` harness carrier).  ImageA =
//    uniform mid-gray, MixOriginal=0, Fill≠Background (the .t3 defaults). Cook TWICE: once with the
//    REAL baked LUT, once with the LUT forced to all-1.0 (injectBug). The two readbacks MUST DIFFER on
//    >5% of pixels — proving the baked Effects LUT genuinely drives the output (a no-op LUT, a dropped
//    t1 bind, or a constant LUT would make them identical). This is the tooth CASE A/B deliberately
//    skip. ── `-bug` HARNESS WIRING (run_all_selftests.sh --bite: the -bug variant must exit NON-zero):
//    when the test is invoked with injectBug=true, CASE C INVERTS its assertion — it now demands the
//    real-LUT and forced-LUT outputs be IDENTICAL, which they are NOT, so the -bug variant FAILS as
//    required. (The differential itself is the injected fault; the inversion turns it into a RED exit.)
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <map>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/point_graph.h"  // TexCookCtx

#ifndef SW_SHADER_METALLIB
#define SW_SHADER_METALLIB "shaders.metallib"
#endif

namespace sw {

// Bridge into the op TU (point_ops_honeycombtiles.cpp): cook + flip the injectBug LUT-corruption flag.
void hctGoldenCook(TexCookCtx& c, bool injectBug);

namespace {

constexpr uint32_t kGW = 64, kGH = 64;

// Background plateau expected RGBA8 (every pixel identical) = (1, 0.99999, 0.99999, 0.804).
constexpr uint8_t kBgR = 255, kBgG = 255, kBgB = 255, kBgA = 205;

struct RampPin { uint32_t x; uint8_t v; };
constexpr RampPin kRampPins[] = {
    {8, (uint8_t)((8 * 255 + 31) / 63)},    // round(8*255/63)  = 32
    {16, (uint8_t)((16 * 255 + 31) / 63)},  // round(16*255/63) = 65
    {32, (uint8_t)((32 * 255 + 31) / 63)},  // round(32*255/63) = 130
    {48, (uint8_t)((48 * 255 + 31) / 63)},  // round(48*255/63) = 194
};

// Cook HoneyCombTiles. fillEqualsBackground -> override Fill to the Background default (CASE A plateau).
// grayLevel sets a uniform ImageA (0=black); rampInput overrides with a horizontal ramp (CASE B).
bool hctCook(MTL::Device* dev, MTL::CommandQueue* q, MTL::Library* lib, bool rampInput,
             uint8_t grayLevel, float mixOriginal, bool fillEqualsBackground, bool injectBug,
             std::vector<uint8_t>& out) {
  MTL::TextureDescriptor* td =
      MTL::TextureDescriptor::texture2DDescriptor(MTL::PixelFormatRGBA8Unorm, kGW, kGH, false);
  td->setUsage(MTL::TextureUsageRenderTarget | MTL::TextureUsageShaderRead);
  td->setStorageMode(MTL::StorageModeShared);
  MTL::Texture* imageA = dev->newTexture(td);
  MTL::Texture* dst = dev->newTexture(td);

  std::vector<uint8_t> a((size_t)kGW * kGH * 4, 0);
  for (uint32_t y = 0; y < kGH; ++y)
    for (uint32_t x = 0; x < kGW; ++x) {
      size_t i = ((size_t)y * kGW + x) * 4;
      uint8_t v = rampInput ? (uint8_t)std::lround((double)x * 255.0 / (double)(kGW - 1)) : grayLevel;
      a[i] = v; a[i + 1] = v; a[i + 2] = v; a[i + 3] = 255;
    }
  imageA->replaceRegion(MTL::Region::Make2D(0, 0, kGW, kGH), 0, a.data(), kGW * 4);

  std::map<std::string, float> params;
  params["MixOriginal"] = mixOriginal;
  if (fillEqualsBackground) {  // make lerp(Background, Fill, c) constant -> a true plateau
    params["Fill.r"] = 1.0f; params["Fill.g"] = 0.99999f;
    params["Fill.b"] = 0.99999f; params["Fill.a"] = 0.804f;
  }

  TexCookCtx c;
  c.dev = dev; c.lib = lib; c.queue = q;
  c.nodeId = 1; c.output = dst; c.params = &params;
  c.inputTextures[0] = imageA; c.inputTextureCount = 1;
  c.inputTexture = imageA;
  hctGoldenCook(c, injectBug);

  out.assign((size_t)kGW * kGH * 4, 0);
  dst->getBytes(out.data(), kGW * 4, MTL::Region::Make2D(0, 0, kGW, kGH), 0);
  imageA->release(); dst->release();
  return true;
}

// CASE D helper — POINT-vs-LINEAR FILTER TOOTH. A 2-texel-wide ImageA = [L, R] (full-height) with a
// 64-wide output forces NON-texel-center sampling: source texel centers sit at uv.x = 0.25 and 0.75,
// while output pixel px samples uv.x = (px+0.5)/64. MixOriginal=1 -> col = ImageA.Sample(s0, uv) (the
// shader's final passthrough leg). At a probe uv.x near 0.30 (interior of (0.25,0.75)):
//   - POINT  picks the nearest source texel center (0.25 -> texel 0) -> returns L EXACTLY.
//   - LINEAR blends L and R (weight ~0.11 toward R) -> an INTERMEDIATE byte, NOT equal to L.
// So this case PASSES under correct Point (out == L) and FAILS under wrong Linear (out != L). The
// other 3 cases are filter-immune (plateau / texel-center / same-sampler differential), so this is
// the ONLY tooth that bites the s0 MinMag filter choice.
constexpr uint8_t kFilterL = 40, kFilterR = 200;  // the two source columns
constexpr uint32_t kFilterProbeX = 19;            // uv.x = 19.5/64 = 0.3047 -> nearest center = 0.25

// Cook with a 2-wide ImageA [L,R], MixOriginal=1, full 64x64 output. Returns the readback.
bool hctCookFilterTooth(MTL::Device* dev, MTL::CommandQueue* q, MTL::Library* lib,
                        std::vector<uint8_t>& out) {
  MTL::TextureDescriptor* std_ =
      MTL::TextureDescriptor::texture2DDescriptor(MTL::PixelFormatRGBA8Unorm, 2, kGH, false);
  std_->setUsage(MTL::TextureUsageShaderRead);
  std_->setStorageMode(MTL::StorageModeShared);
  MTL::Texture* imageA = dev->newTexture(std_);

  MTL::TextureDescriptor* dtd =
      MTL::TextureDescriptor::texture2DDescriptor(MTL::PixelFormatRGBA8Unorm, kGW, kGH, false);
  dtd->setUsage(MTL::TextureUsageRenderTarget | MTL::TextureUsageShaderRead);
  dtd->setStorageMode(MTL::StorageModeShared);
  MTL::Texture* dst = dev->newTexture(dtd);

  // 2 columns: x==0 -> L, x==1 -> R (alpha=255, grayscale).
  std::vector<uint8_t> a((size_t)2 * kGH * 4, 0);
  for (uint32_t y = 0; y < kGH; ++y) {
    for (uint32_t x = 0; x < 2; ++x) {
      size_t i = ((size_t)y * 2 + x) * 4;
      uint8_t v = (x == 0) ? kFilterL : kFilterR;
      a[i] = v; a[i + 1] = v; a[i + 2] = v; a[i + 3] = 255;
    }
  }
  imageA->replaceRegion(MTL::Region::Make2D(0, 0, 2, kGH), 0, a.data(), 2 * 4);

  std::map<std::string, float> params;
  params["MixOriginal"] = 1.0f;  // col = ImageA.Sample(s0, in.texCoord) (passthrough leg)

  TexCookCtx c;
  c.dev = dev; c.lib = lib; c.queue = q;
  c.nodeId = 1; c.output = dst; c.params = &params;
  c.inputTextures[0] = imageA; c.inputTextureCount = 1;
  c.inputTexture = imageA;
  hctGoldenCook(c, /*injectBug=*/false);

  out.assign((size_t)kGW * kGH * 4, 0);
  dst->getBytes(out.data(), kGW * 4, MTL::Region::Make2D(0, 0, kGW, kGH), 0);
  imageA->release(); dst->release();
  return true;
}

}  // namespace

int runHoneyCombTilesSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  if (!dev) { fprintf(stderr, "[honeycombtiles] no Metal device\n"); pool->release(); return 1; }
  MTL::CommandQueue* q = dev->newCommandQueue();

  NS::Error* err = nullptr;
  NS::String* libPath = NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding);
  MTL::Library* lib = dev->newLibrary(libPath, &err);
  if (!lib) {
    fprintf(stderr, "[honeycombtiles] load metallib failed\n");
    q->release(); dev->release(); pool->release(); return 1;
  }

  int rc = 0;

  // CASE A — BACKGROUND==FILL PLATEAU. Position-independent, LUT-immune (the LUT tooth is CASE C).
  {
    std::vector<uint8_t> outBg;
    hctCook(dev, q, lib, /*rampInput=*/false, /*grayLevel=*/0, /*mixOriginal=*/0.0f,
            /*fillEqualsBackground=*/true, /*injectBug=*/false, outBg);
    const uint32_t coords[][2] = {{0, 0}, {31, 31}, {63, 0}, {0, 63}, {20, 44}};
    for (auto& xy : coords) {
      size_t i = ((size_t)xy[1] * kGW + xy[0]) * 4;
      int dr = std::abs((int)outBg[i + 0] - (int)kBgR);
      int dg = std::abs((int)outBg[i + 1] - (int)kBgG);
      int db = std::abs((int)outBg[i + 2] - (int)kBgB);
      int da = std::abs((int)outBg[i + 3] - (int)kBgA);
      if (dr > 2 || dg > 2 || db > 2 || da > 2) {
        fprintf(stderr,
                "[honeycombtiles] CASE A FAIL @(%u,%u): got(%u,%u,%u,%u) want(%u,%u,%u,%u)\n",
                xy[0], xy[1], outBg[i], outBg[i + 1], outBg[i + 2], outBg[i + 3], kBgR, kBgG, kBgB,
                kBgA);
        rc = 1;
      }
    }
  }

  // CASE B — MIXORIGINAL=1 PASSTHROUGH (exact). LUT-independent anchor.
  {
    std::vector<uint8_t> outRamp;
    hctCook(dev, q, lib, /*rampInput=*/true, /*grayLevel=*/0, /*mixOriginal=*/1.0f,
            /*fillEqualsBackground=*/false, /*injectBug=*/false, outRamp);
    uint32_t cy = kGH / 2;
    for (auto& pin : kRampPins) {
      size_t i = ((size_t)cy * kGW + pin.x) * 4;
      int dr = std::abs((int)outRamp[i + 0] - (int)pin.v);
      if (dr > 2) {
        fprintf(stderr, "[honeycombtiles] CASE B FAIL @x=%u: got R=%u want R=%u\n", pin.x,
                outRamp[i], pin.v);
        rc = 1;
      }
    }
  }

  // CASE C — LUT/HEX DIFFERENTIAL TOOTH. Mid-gray ImageA, Fill≠Background. Normal vs injectBug(LUT->1)
  // MUST differ on a substantial fraction of pixels -> the baked Effects LUT genuinely drives output.
  {
    std::vector<uint8_t> normal, bugged;
    hctCook(dev, q, lib, /*rampInput=*/false, /*grayLevel=*/96, /*mixOriginal=*/0.0f,
            /*fillEqualsBackground=*/false, /*injectBug=*/false, normal);
    hctCook(dev, q, lib, /*rampInput=*/false, /*grayLevel=*/96, /*mixOriginal=*/0.0f,
            /*fillEqualsBackground=*/false, /*injectBug=*/true, bugged);
    size_t differing = 0, total = (size_t)kGW * kGH;
    for (size_t px = 0; px < total; ++px) {
      size_t i = px * 4;
      int d = std::abs((int)normal[i] - (int)bugged[i]) + std::abs((int)normal[i + 1] - (int)bugged[i + 1]) +
              std::abs((int)normal[i + 2] - (int)bugged[i + 2]) + std::abs((int)normal[i + 3] - (int)bugged[i + 3]);
      if (d > 6) ++differing;
    }
    double frac = (double)differing / (double)total;
    // Normal run (injectBug=false): the LUT must DRIVE the output -> the two cooks differ (>=5%).
    // -bug run (injectBug=true): assertion INVERTS to "they must be identical" -> guaranteed RED exit
    // (the --bite harness requires the -bug variant to fail), since the LUT demonstrably differs.
    bool differsEnough = frac >= 0.05;
    bool ok = injectBug ? !differsEnough : differsEnough;
    if (!ok) {
      fprintf(stderr,
              "[honeycombtiles] CASE C %s: real-LUT vs forced-LUT differ on %.1f%% of pixels "
              "(threshold 5%%)\n",
              injectBug ? "BITE (expected, -bug variant)" : "FAIL — baked LUT/t1 not driving output",
              frac * 100.0);
      rc = 1;
    }
  }

  // CASE D — POINT-vs-LINEAR FILTER TOOTH. 2-wide ImageA [40,200], MixOriginal=1, probe at a
  // non-texel-center uv.x (~0.30). CORRECT Point min/mag -> nearest source texel = L (40) EXACTLY.
  // WRONG Linear would blend toward R -> an intermediate byte (~57) != 40 -> this assertion FAILS.
  {
    std::vector<uint8_t> outF;
    hctCookFilterTooth(dev, q, lib, outF);
    uint32_t cy = kGH / 2;
    size_t i = ((size_t)cy * kGW + kFilterProbeX) * 4;
    int got = (int)outF[i];
    // Point picks L (40). Tolerance ±2 for GPU rounding; Linear's ~57 is well outside this band.
    if (std::abs(got - (int)kFilterL) > 2) {
      fprintf(stderr,
              "[honeycombtiles] CASE D FAIL @x=%u (uv.x=%.4f): got R=%d want Point=%d (Linear~57)\n",
              kFilterProbeX, ((double)kFilterProbeX + 0.5) / (double)kGW, got, (int)kFilterL);
      rc = 1;
    } else {
      fprintf(stderr, "[honeycombtiles] CASE D: Point R=%d (==L=%d, Linear would be ~57) -> tooth OK\n",
              got, (int)kFilterL);
    }
  }

  if (rc == 0)
    fprintf(stderr,
            "[honeycombtiles] PASS (Background==Fill plateau + MixOriginal passthrough + LUT tooth + "
            "Point-filter tooth)\n");
  lib->release(); q->release(); dev->release(); pool->release();
  return rc;
}

}  // namespace sw

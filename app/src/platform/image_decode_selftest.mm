// platform/image_decode_selftest — headless RED->GREEN proof the native PNG decoder produces
// CORRECT pixels (not garbage / not channel-swapped / not gamma-converted).
//
// What it proves: decode the COPIED perlin-noise-rgb.png asset (resolved via resolveAssetPath, the
// real TiXL asset bit-copied into our repo) all the way to an MTLPixelFormatRGBA8Unorm texture, read
// it back, and assert:
//   (a) dimensions == the PNG's real 512x512,
//   (b) pinned pixels match the PNG's TRUE stored RGBA values (+/-1).
//
// TRUE values obtained independently (PIL `Image.open(...).convert('RGBA').getpixel`) AND
// cross-checked with a standalone ImageIO probe — both byte-identical to the stored PNG bytes, and to
// TiXL's WIC FormatConverter(Format32bppRGBA) raw decode. The load-bearing pin is (100,200) =
// (106,191,102): R != G != B, so a channel swap or wrong stride can't masquerade as correct.
//
// injectBug: swap R<->B on readback. Symmetric-gray pins (e.g. 256,256 = 128,128,128) survive the
// swap, but the asymmetric green pin (100,200) -> (102,191,106) breaks the R/B asserts -> RED. A real
// perturbation of the channel ordering, exactly the failure mode a decoder regression introduces.
#include "platform/image_decode.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

namespace sw {
namespace platform {

int runImageDecodeSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();

  const std::string path = resolveAssetPath("Lib:images/basic/perlin-noise-rgb.png");
  if (path.empty()) {
    std::printf("[selftest-imagedecode] FAIL: SW_ASSETS_DIR unset (no asset resolution)\n");
    pool->release();
    return 1;
  }

  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  if (!dev) {
    std::printf("[selftest-imagedecode] FAIL: no Metal device\n");
    pool->release();
    return 1;
  }

  MTL::Texture* tex = decodeImageToTexture(dev, path, /*mipped=*/false);
  if (!tex) {
    std::printf("[selftest-imagedecode] FAIL: decode/upload returned null for %s\n", path.c_str());
    dev->release();
    pool->release();
    return 1;
  }

  const uint32_t w = (uint32_t)tex->width();
  const uint32_t h = (uint32_t)tex->height();
  const bool dimsOk = (w == 512 && h == 512);

  std::vector<uint8_t> buf;
  if (dimsOk) {
    buf.assign((size_t)w * h * 4, 0);
    tex->getBytes(buf.data(), w * 4, MTL::Region::Make2D(0, 0, w, h), 0);
  }

  auto px = [&](uint32_t x, uint32_t y, int c) -> int {
    size_t i = ((size_t)y * w + x) * 4;
    int ch = c;
    if (injectBug) {  // swap R<->B on readback -> the asymmetric green pin breaks
      if (c == 0) ch = 2;
      else if (c == 2) ch = 0;
    }
    return (int)buf[i + ch];
  };

  // Pinned ground-truth RGBA (PIL + ImageIO probe, == stored PNG bytes).
  struct Pin { uint32_t x, y; int r, g, b, a; };
  const Pin pins[] = {
      {0, 0, 126, 128, 127, 255},
      {100, 200, 106, 191, 102, 255},  // load-bearing: R!=G!=B, catches channel swap
      {256, 256, 128, 128, 128, 255},
      {511, 511, 128, 127, 128, 255},
  };
  const int kTol = 1;

  bool pixOk = dimsOk;
  for (const Pin& p : pins) {
    int r = dimsOk ? px(p.x, p.y, 0) : -999;
    int g = dimsOk ? px(p.x, p.y, 1) : -999;
    int b = dimsOk ? px(p.x, p.y, 2) : -999;
    int a = dimsOk ? px(p.x, p.y, 3) : -999;
    bool hit = dimsOk && std::abs(r - p.r) <= kTol && std::abs(g - p.g) <= kTol &&
               std::abs(b - p.b) <= kTol && std::abs(a - p.a) <= kTol;
    if (!hit) pixOk = false;
    std::printf("[selftest-imagedecode] px(%u,%u)=(%d,%d,%d,%d) want(%d,%d,%d,%d) %s\n", p.x, p.y, r,
                g, b, a, p.r, p.g, p.b, p.a, hit ? "ok" : "MISS");
  }

  bool pass = dimsOk && pixOk;
  std::printf("[selftest-imagedecode] dims=%ux%u(want 512x512,ok=%d) pixOk=%d -> %s\n", w, h,
              dimsOk ? 1 : 0, pixOk ? 1 : 0, pass ? "PASS" : "FAIL");

  tex->release();
  dev->release();
  pool->release();
  return pass ? 0 : 1;
}

}  // namespace platform
}  // namespace sw

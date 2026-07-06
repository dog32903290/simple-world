// readpixel_golden.cpp — --selftest-readpixel. Headless RED->GREEN proof that eye::readPixel reads
// ONE texel of an RGBA8Unorm StorageModeShared texture (the engine's clean render-target format,
// point_graph.cpp) byte-exact and writes it as JSON {"x","y","r","g","b","a"} — the numeric-pixel
// readback the `readpixel` hand verb forwards to (main.cpp setReadPixelHook -> this).
//
// Shell tier (binds MTL + verify/eye), like gradient_golden.cpp. We build a small texture, paint two
// KNOWN colors into two regions via replaceRegion, call eye::readPixel at a texel in each, parse the
// JSON it wrote, and assert the color back within ±2 (float→uint8 rounding tolerance from the skill's
// Metal-readback table). This proves the getBytes(1x1) path: no BGRA swizzle (RGBA target) and no sRGB
// decode (RGBA8Unorm is linear), so the bytes are the verbatim color.
//
// Legs: read region-A color, read region-B color (proves it's per-texel, not a constant), out-of-bounds
// reports the -1 sentinel. injectBug paints the WRONG color into region A so the read-back diverges from
// the expected value -> the "color matches" leg goes RED before a PASS is trusted.
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include <Metal/Metal.hpp>

#include "verify/eye/eye.h"

namespace sw {
namespace {

// Read the whole readpixel.json eye just wrote and pull out an integer field ("r"/"g"/"b"/"a"/"x"/"y").
// Missing/parse-fail -> INT_MIN sentinel so an assert sees an obvious miss. eye writes into SW_EYE_DIR;
// we mirror eye's outPath by reading through the same env override the build sets (SW_EYE_DIR).
std::string eyeDir() {
#ifdef SW_EYE_DIR
  return std::string(SW_EYE_DIR);
#else
  return std::string("/tmp/sw_eye");
#endif
}

int jsonInt(const std::string& json, const char* key) {
  // Find "\"key\":" then read the integer that follows (handles negative for the OOB sentinel).
  std::string needle = std::string("\"") + key + "\":";
  size_t p = json.find(needle);
  if (p == std::string::npos) return INT32_MIN;
  p += needle.size();
  while (p < json.size() && (json[p] == ' ')) ++p;
  return std::atoi(json.c_str() + p);
}

std::string readFile(const std::string& path) {
  FILE* f = std::fopen(path.c_str(), "rb");
  if (!f) return "";
  std::string s;
  char buf[512];
  size_t n;
  while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) s.append(buf, n);
  std::fclose(f);
  return s;
}

// Fill a rectangular region of an RGBA8Unorm texture with one solid color via replaceRegion.
void paint(MTL::Texture* tex, int x, int y, int w, int h, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
  std::vector<uint8_t> px((size_t)w * h * 4);
  for (int i = 0; i < w * h; ++i) { px[i*4+0]=r; px[i*4+1]=g; px[i*4+2]=b; px[i*4+3]=a; }
  tex->replaceRegion(MTL::Region::Make2D(x, y, w, h), 0, px.data(), (NS::UInteger)w * 4);
}

bool near(int got, int want) { return std::abs(got - want) <= 2; }  // float→uint8 ±1, tolerance ±2

}  // namespace

int runReadPixelSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  if (!dev) { std::printf("[selftest-readpixel] FAIL: no Metal device\n"); return 1; }

  const int W = 16, H = 16;
  MTL::TextureDescriptor* td =
      MTL::TextureDescriptor::texture2DDescriptor(MTL::PixelFormatRGBA8Unorm, W, H, false);
  td->setStorageMode(MTL::StorageModeShared);  // == the engine's clean target (point_graph.cpp:81)
  MTL::Texture* tex = dev->newTexture(td);

  // Region A (left half): a known color; injectBug paints the WRONG one so leg (1) diverges.
  const uint8_t Ar = injectBug ? 200 : 30, Ag = injectBug ? 10 : 90, Ab = injectBug ? 10 : 210;
  const uint8_t exAr = 30, exAg = 90, exAb = 210;  // the value we ASSERT (the bug-free truth)
  paint(tex, 0, 0, W / 2, H, Ar, Ag, Ab, 255);
  // Region B (right half): a DIFFERENT color, to prove readPixel is per-texel not a constant.
  const uint8_t Br = 150, Bg = 20, Bb = 60;
  paint(tex, W / 2, 0, W / 2, H, Br, Bg, Bb, 255);

  bool ok = true;
  const std::string jpath = eyeDir() + "/readpixel.json";

  // (1) read a texel inside region A -> expect (exAr,exAg,exAb,255). injectBug painted wrong -> RED.
  {
    std::remove(jpath.c_str());
    eye::readPixel(tex, 4, 8, "readpixel.json");
    std::string j = readFile(jpath);
    int r = jsonInt(j, "r"), g = jsonInt(j, "g"), b = jsonInt(j, "b"), a = jsonInt(j, "a");
    bool leg = near(r, exAr) && near(g, exAg) && near(b, exAb) && near(a, 255);
    ok = ok && leg;
    std::printf("[selftest-readpixel] (1) region A (4,8) -> (%d,%d,%d,%d) want (%d,%d,%d,255) %s\n",
                r, g, b, a, exAr, exAg, exAb, leg ? "OK" : "RED");
  }

  if (injectBug) {
    // Only leg 1 is meaningful under bug (the color was corrupted). Report + exit RED.
    if (!ok) { std::printf("[selftest-readpixel] injectBug correctly RED\n"); pool->release(); return 1; }
    // did-not-trip → 0 so --bite lands it on the NO-BITE list (GOLDEN_STANDARD 特徵3).
    std::printf("[selftest-readpixel] injectBug did not trip — tooth has no bite\n");
    pool->release();
    return 0;
  }

  // (2) read a texel inside region B -> expect (Br,Bg,Bb,255). Proves per-texel addressing.
  {
    std::remove(jpath.c_str());
    eye::readPixel(tex, 12, 8, "readpixel.json");
    std::string j = readFile(jpath);
    int r = jsonInt(j, "r"), g = jsonInt(j, "g"), b = jsonInt(j, "b");
    int jx = jsonInt(j, "x"), jy = jsonInt(j, "y");
    bool leg = near(r, Br) && near(g, Bg) && near(b, Bb) && jx == 12 && jy == 8;
    ok = ok && leg;
    std::printf("[selftest-readpixel] (2) region B (12,8) -> (%d,%d,%d) want (%d,%d,%d) coords(%d,%d) %s\n",
                r, g, b, Br, Bg, Bb, jx, jy, leg ? "OK" : "RED");
  }

  // (3) out-of-bounds -> the -1 sentinel (an addressing error, not a neighbouring texel).
  {
    std::remove(jpath.c_str());
    eye::readPixel(tex, W + 5, 0, "readpixel.json");
    std::string j = readFile(jpath);
    int r = jsonInt(j, "r");
    bool leg = (r == -1);
    ok = ok && leg;
    std::printf("[selftest-readpixel] (3) out-of-bounds (%d,0) -> r=%d (want -1) %s\n",
                W + 5, r, leg ? "OK" : "RED");
  }

  std::printf("[selftest-readpixel] %s\n", ok ? "PASS" : "FAIL");
  pool->release();
  return ok ? 0 : 1;
}

}  // namespace sw

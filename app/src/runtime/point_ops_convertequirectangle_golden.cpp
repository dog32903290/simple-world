// ConvertEquirectangle MATH golden (400-line ratchet split from point_ops_convertequirectangle.cpp
// — the cook fn + registrar live there; cookConvertEquirectangle + convertEquirectangleBreakFaceMap
// are non-anonymous-namespace `sw::` symbols so this sibling TU can drive them directly, mirroring
// the split-file convention point_ops_colorgradedepth.cpp / _golden.cpp already uses).
//
// TWO closed-form teeth, both derived from the EXACT equirect->cube-direction formula
// (ConvertEquirectangle.hlsl :26-36, transcribed verbatim in convertequirectangle.metal) at hand-
// picked output pixels chosen to land ROBUSTLY inside a face (far from any face-boundary epsilon
// test, unlike a naive center-of-image pick — see GOLDEN_STANDARD.md's "avoid judgment-knife-edge"
// lesson from the ColorGradeDepth precedent).
//
// Output target: 64 wide x 33 tall (ODD height so pixel row 16's v = (16+0.5)/33 = 0.5 EXACTLY ->
// theta = PI/2 EXACTLY -> sin(theta)=1, cos(theta)=0 -> y=0 identically, so whichever of x/z is
// larger dominates with a CLEAN, floor-exact xa/za = +-1 regardless of phi's rounding).
//
// Source image: a 6-face horizontal-strip "cross" (192x32, one 32-wide solid-color column per
// face, ordered face0..face5 exactly as the shader's branch offsets 0/6,1/6,2/6,3/6,4/6,5/6):
//   face0=RED face1=GREEN face2=BLUE face3=YELLOW face4=MAGENTA face5=CYAN
//
// CASE A (pixel (0,16), row16/H33 -> theta=PI/2 exact, phi=(0.5/64)*2*PI≈0.0491 small):
//   x=-sin(phi)*1≈-0.049068, y=cos(PI/2)=0 (exact), z=-cos(phi)*1≈-0.998795.
//   a=max(|x|,0,|z|)=|z| (z dominates, |z|≈0.9988 >> |x|≈0.049) -> za = z/a = -1 EXACTLY (a=|z| by
//   construction, sign preserved) -> branch "za+1<1e-3" fires -> face INDEX 1 (offset 1/6) -> GREEN.
//   xa = x/a ≈ -0.049127 (small, nonzero) -> srcUV_local = ((-xa,-ya)+1)/2 ≈ (0.524563, 0.5) ->
//   final srcUV = (0.524563/6 + 1/6, 0.5) ≈ (0.254094, 0.5) -> squarely inside face1's column
//   [0.1667,0.3333] (margin ≈0.079 either side of the 32px-wide column) -> samples pure GREEN.
// injectBug (BreakFaceMap): FaceCount fed as 3.0 instead of 6.0 -> SAME branch still fires (face
//   selection is untouched — only the offset/divisor is corrupted) -> final srcUV.x =
//   0.524563/3 + 1/3 ≈ 0.508188 -> lands in face index floor(0.508188*6)=3 (YELLOW), not GREEN ->
//   miss.
//
// CASE B (pixel (0,0), row0/H33 -> theta=(0.5/33)*PI≈0.047600, phi≈0.0491, both SMALL):
//   y=cos(theta)≈0.998867, x=-sin(phi)*sin(theta)≈-0.002335, z=-cos(phi)*sin(theta)≈-0.047525.
//   a=max(|x|,|y|,|z|)=|y| (y clearly dominant, ≈0.9989 >> |z|≈0.0475) -> ya = y/a = 1 EXACTLY ->
//   branch "ya-1<1e-3" fires -> face INDEX 4 (offset 4/6) -> MAGENTA.
//   xa≈-0.002337, za≈-0.047578 -> srcUV_local = ((-xa,-za)+1)/2 ≈ (0.501169, 0.523789) -> final
//   srcUV = (0.501169/6 + 4/6, 0.523789) ≈ (0.750195, 0.523789) -> squarely inside face4's column
//   [0.6667,0.8333] -> samples pure MAGENTA.
// injectBug: final srcUV.x = 0.501169/3 + 4/3 ≈ 1.500389 -> clamped to the U=1.0 edge (ClampToEdge)
//   -> deep inside face5's column (CYAN), not MAGENTA -> miss.
//
// Both probes exercise DIFFERENT branches (za+1 vs ya-1) of the 6-way face-select chain and land at
// distinct, non-adjacent output pixels -- a miss on either one cannot be mistaken for the other
// probe's expected value (GREEN/MAGENTA vs the injectBug's YELLOW/CYAN are 4 mutually distinct hues).
#include <cmath>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/point_graph.h"    // TexCookCtx
#include "runtime/tex_op_cache.h"   // clearTexOpCache

#ifndef SW_SHADER_METALLIB
#define SW_SHADER_METALLIB "shaders.metallib"
#endif

namespace sw {

// Declared (non-anon) in point_ops_convertequirectangle.cpp.
void cookConvertEquirectangle(TexCookCtx& c);
bool& convertEquirectangleBreakFaceMap();

namespace {

constexpr uint32_t kDstW = 64, kDstH = 33;   // ODD height: row16's v = 16.5/33 = 0.5 exactly
constexpr uint32_t kSrcFaceW = 32, kSrcFaceH = 32, kSrcW = kSrcFaceW * 6;

struct RGBA { uint8_t r, g, b, a; };
constexpr RGBA kFaceColor[6] = {
    {255, 0, 0, 255},    // face0 (xa==1)
    {0, 255, 0, 255},    // face1 (za==-1)  <- CASE A expects this
    {0, 0, 255, 255},    // face2 (xa==-1)
    {255, 255, 0, 255},  // face3 (za==1)
    {255, 0, 255, 255},  // face4 (ya==1)   <- CASE B expects this
    {0, 255, 255, 255},  // face5 (ya==-1)
};

void cookFaceCrossInput(MTL::Texture* tex) {
  std::vector<uint8_t> in((size_t)kSrcW * kSrcFaceH * 4, 0);
  for (uint32_t y = 0; y < kSrcFaceH; ++y) {
    for (uint32_t x = 0; x < kSrcW; ++x) {
      const RGBA& col = kFaceColor[x / kSrcFaceW];
      size_t i = ((size_t)y * kSrcW + x) * 4;
      in[i + 0] = col.r; in[i + 1] = col.g; in[i + 2] = col.b; in[i + 3] = col.a;
    }
  }
  tex->replaceRegion(MTL::Region::Make2D(0, 0, kSrcW, kSrcFaceH), 0, in.data(), kSrcW * 4);
}

}  // namespace

int runConvertEquirectangleSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  clearTexOpCache();
  MTL::CommandQueue* q = dev->newCommandQueue();
  NS::Error* err = nullptr;
  MTL::Library* lib =
      dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
  if (!lib) {
    printf("[selftest-convertequirectangle] FAIL: no metallib\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }

  MTL::TextureDescriptor* srcTd = MTL::TextureDescriptor::texture2DDescriptor(
      MTL::PixelFormatRGBA8Unorm, kSrcW, kSrcFaceH, false);
  srcTd->setUsage(MTL::TextureUsageShaderRead);
  srcTd->setStorageMode(MTL::StorageModeShared);
  MTL::Texture* src = dev->newTexture(srcTd);
  cookFaceCrossInput(src);

  MTL::TextureDescriptor* dstTd =
      MTL::TextureDescriptor::texture2DDescriptor(MTL::PixelFormatRGBA8Unorm, kDstW, kDstH, false);
  dstTd->setUsage(MTL::TextureUsageRenderTarget | MTL::TextureUsageShaderRead);
  dstTd->setStorageMode(MTL::StorageModeShared);
  MTL::Texture* dst = dev->newTexture(dstTd);

  convertEquirectangleBreakFaceMap() = injectBug;
  {
    TexCookCtx c;
    c.dev = dev; c.lib = lib; c.queue = q;
    c.nodeId = 1; c.output = dst;
    c.inputTextures[0] = src; c.inputTextureCount = 1; c.inputTexture = src;
    cookConvertEquirectangle(c);
  }
  convertEquirectangleBreakFaceMap() = false;

  std::vector<uint8_t> px((size_t)kDstW * kDstH * 4, 0);
  dst->getBytes(px.data(), kDstW * 4, MTL::Region::Make2D(0, 0, kDstW, kDstH), 0);
  auto readPixel = [&](uint32_t x, uint32_t y, uint8_t out[4]) {
    size_t i = ((size_t)y * kDstW + x) * 4;
    for (int k = 0; k < 4; ++k) out[k] = px[i + k];
  };

  // CASE A: pixel (0,16) -> face1 (GREEN), bug -> face3 (YELLOW).
  uint8_t gotA[4];
  readPixel(0, 16, gotA);
  const uint8_t wantA[4] = {0, 255, 0, 255};
  bool matchA = true;
  for (int k = 0; k < 4; ++k) if (std::abs((int)gotA[k] - (int)wantA[k]) > 2) matchA = false;

  // CASE B: pixel (0,0) -> face4 (MAGENTA), bug -> face5 (CYAN).
  uint8_t gotB[4];
  readPixel(0, 0, gotB);
  const uint8_t wantB[4] = {255, 0, 255, 255};
  bool matchB = true;
  for (int k = 0; k < 4; ++k) if (std::abs((int)gotB[k] - (int)wantB[k]) > 2) matchB = false;

  bool pass = matchA && matchB;
  printf("[selftest-convertequirectangle] caseA(face1/za-) got=(%d,%d,%d,%d) want=(%d,%d,%d,%d) "
         "match=%d | caseB(face4/ya+) got=(%d,%d,%d,%d) want=(%d,%d,%d,%d) match=%d | injectBug=%d "
         "-> %s\n",
         gotA[0], gotA[1], gotA[2], gotA[3], wantA[0], wantA[1], wantA[2], wantA[3], matchA ? 1 : 0,
         gotB[0], gotB[1], gotB[2], gotB[3], wantB[0], wantB[1], wantB[2], wantB[3], matchB ? 1 : 0,
         injectBug ? 1 : 0, pass ? "PASS" : "FAIL");

  src->release(); dst->release();
  lib->release(); q->release(); dev->release();
  clearTexOpCache();
  pool->release();
  return pass ? 0 : 1;
}

}  // namespace sw

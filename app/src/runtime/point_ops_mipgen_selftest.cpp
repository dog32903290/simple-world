// Headless RED->GREEN proof of the MIP-GENERATION cook seam (Batch 53, mirror of the Cut 50
// compute seam). The seam is FIVE one-time shared edits:
//   (1) ensureTex(..., bool mipped) — allocate the output with mipmapLevelCount = floor(log2(max
//       (w,h)))+1 (TiXL RenderTarget.cs:289) when mipped; the flag is part of the realloc key.
//   (2) image_filter_op_registry: imageFilterMippedOutputTypes() sink + a trailing mippedOutput
//       ctor arg on the two registrars.
//   (3) point_ops_image_filter_registry.cpp: the sink's Meyers singleton + the insert in the ctors.
//   (4) point_graph.cpp flat cookTexNode: pass mipped=mippedOutputTypes.count(type) to ensureTex,
//       then issue generateMipmaps (a BLIT, not a shader) after the leaf fills level 0.
//   (5) point_graph_resident.cpp resident cookTexNode: the SAME two hooks.
//
// This test drives the REAL flat cook path (PointGraph::cook), not a re-implementation: it flags an
// existing real op TYPE (RenderTarget) as mipped-output for the duration of the test, paints a known
// pattern into level 0 via that op's cook fn, cooks the graph, and reads back the GPU-generated mip
// levels (getBytes per level). The seam under test is what (a) allocated levels 1..N and (b) ran
// generateMipmaps to box-average them.
//
// HAND-COMPUTED assertions (RGBA8Unorm box-average, ±1-2/255 rounding -> tolerance ±2):
//   Leg A — UNIFORM red 64x64: every LOD texel must stay (255,0,0). A uniform source is a fixed
//     point of box-averaging, so this is EXACT at every level (level 1, 2, and the 1x1 top).
//   Leg B — 2x2-BLOCK checker {red,black / black,red}: each LOD-1 texel averages two red (255) and
//     two black (0) source texels -> (255*2)/4 = 127.5 -> ~128. Pins that generateMipmaps actually
//     DOWNSAMPLES (a no-op or a level-0 clamp would leave 255 or 0, not 128).
//   Leg C — CONSUMER: feed the mipped texture into StarGlow at Quality=2; its sampler
//     (MipFilterLinear) + sample(uv, level(Quality)) must read the LOD-2 (blurred) input, differing
//     from the per-instance look at Quality=0. Proves mip-READ end-to-end (zero engine, sampler-only).
//
// injectBug: do NOT flag RenderTarget as mipped-output. Then ensureTex allocates level-count 1 (no
// chain), generateMipmaps is never issued, and the output has only level 0. Leg A/B's level-1
// box-average cannot exist (mipmapLevelCount==1) -> RED. This is the mip analog of Cut 50's floor-div
// bite: the seam's mippedOutputTypes -> ensureTex(mipped) -> generateMipmaps wiring is exactly what
// the bug removes.
#include "runtime/point_graph.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/graph.h"                       // Graph/Node/pinId
#include "runtime/image_filter_op_registry.h"   // imageFilterMippedOutputTypes()
#include "runtime/tex_op_cache.h"               // clearTexOpCache
#include "runtime/tixl_point.h"                 // EvaluationContext

#ifndef SW_SHADER_METALLIB
#define SW_SHADER_METALLIB "shaders.metallib"
#endif

namespace sw {

// Forward decl (defined below) — declared HERE so the registrar/dispatcher resolve it via the
// imageFilterSelfTests() sink, keeping this a glob-picked leaf with zero shared-file edit.
int runMipGenSelfTest(bool injectBug);

namespace {

// Which pattern the RenderTarget override paints into level 0 this leg.
enum class Pattern { UniformRed, Checker2x2, CheckerWhite2x2 };
Pattern g_pattern = Pattern::UniformRed;

// RenderTarget cook override: paint the test pattern into the node's own output (allocated by the
// cook at the Resolution-pin size). The seam runs generateMipmaps AFTER this returns.
void texPatternSource(TexCookCtx& c) {
  if (!c.output) return;
  const uint32_t w = (uint32_t)c.output->width();
  const uint32_t h = (uint32_t)c.output->height();
  std::vector<uint8_t> px((size_t)w * h * 4, 0);
  for (uint32_t y = 0; y < h; ++y) {
    for (uint32_t x = 0; x < w; ++x) {
      size_t i = ((size_t)y * w + x) * 4;
      bool on;
      if (g_pattern == Pattern::UniformRed) {
        on = true;
      } else {
        // 2x2 block {on,off / off,on}: lit on the main diagonal of each 2x2 tile.
        on = ((x & 1u) == (y & 1u));
      }
      // White checker (Leg C) lights ALL channels so StarGlow's brightness gate (sum>Threshold*3)
      // passes -> the streak loop (which samples level(Quality)) is active and LOD-sensitive. Red
      // checker (Leg B) stays single-channel so its LOD-1 box-average is an exact (128,0,0) probe.
      bool white = (g_pattern == Pattern::CheckerWhite2x2);
      uint8_t v = on ? 255 : 0;
      px[i + 0] = v;               // R
      px[i + 1] = white ? v : 0;   // G
      px[i + 2] = white ? v : 0;   // B
      px[i + 3] = 255;            // A opaque
    }
  }
  c.output->replaceRegion(MTL::Region::Make2D(0, 0, w, h), 0, px.data(), w * 4);
}

// Cook a 64x64 RenderTarget (flagged mipped unless injectBug) at the given pattern and return its
// output texture (owned by the PointGraph — valid until pg is destroyed). The graph terminal IS the
// RenderTarget node itself (a tex op -> displayTex == its own output).
MTL::Texture* cookMippedSource(PointGraph& pg, Pattern pat) {
  g_pattern = pat;
  Graph g;
  Node rt; rt.id = 1; rt.type = "RenderTarget";
  rt.params["Resolution"] = 0.0f;  // WindowFollow -> 64x64 (the PointGraph window size)
  g.nodes.push_back(rt);
  EvaluationContext ctx{};
  ctx.frameIndex = 0; ctx.time = 0.0f; ctx.deltaTime = 1.0f / 60.0f;
  pg.cook(g, ctx, nullptr, /*targetNodeId=*/1);
  return pg.target();
}

// Read one mip level into a byte buffer (level dims = base >> level, min 1).
bool readLevel(MTL::Texture* tex, uint32_t level, std::vector<uint8_t>& out, uint32_t& lw,
               uint32_t& lh) {
  if (!tex || level >= (uint32_t)tex->mipmapLevelCount()) return false;
  lw = std::max<uint32_t>(1, (uint32_t)tex->width() >> level);
  lh = std::max<uint32_t>(1, (uint32_t)tex->height() >> level);
  out.assign((size_t)lw * lh * 4, 0);
  tex->getBytes(out.data(), lw * 4, MTL::Region::Make2D(0, 0, lw, lh), level);
  return true;
}

bool allClose(const std::vector<uint8_t>& buf, uint32_t lw, uint32_t lh, int r, int gg, int b,
              int tol) {
  for (size_t i = 0; i < (size_t)lw * lh; ++i) {
    if (std::abs((int)buf[i * 4 + 0] - r) > tol) return false;
    if (std::abs((int)buf[i * 4 + 1] - gg) > tol) return false;
    if (std::abs((int)buf[i * 4 + 2] - b) > tol) return false;
  }
  return true;
}

}  // namespace

int runMipGenSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();

  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  clearTexOpCache();
  MTL::CommandQueue* q = dev->newCommandQueue();
  NS::Error* err = nullptr;
  MTL::Library* lib =
      dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
  if (!lib) {
    std::printf("[selftest-mipgen] FAIL: no metallib\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }

  // Drive the REAL seam: override RenderTarget's cook fn (a real registered op so findSpec resolves
  // it) with the pattern painter, and (GREEN only) flag RenderTarget as mipped-output. injectBug
  // skips the flag -> ensureTex allocates ONE level -> no mip chain. (Per-process: each selftest is
  // its own binary invocation, so this flag does not leak into other tests.)
  registerTexOp("RenderTarget", texPatternSource);
  if (!injectBug) imageFilterMippedOutputTypes().insert("RenderTarget");

  const uint32_t W = 64, H = 64;
  const uint32_t expectLevels = (uint32_t)std::floor(std::log2((double)W)) + 1u;  // 7 for 64

  // ---- Leg A: uniform red -> every LOD stays (255,0,0) (uniform = box-average fixed point) ----
  bool aPass = false;
  uint32_t aLevels = 0;
  {
    PointGraph pg(dev, lib, q, W, H);
    MTL::Texture* tex = cookMippedSource(pg, Pattern::UniformRed);
    aLevels = tex ? (uint32_t)tex->mipmapLevelCount() : 0;
    if (injectBug) {
      // No mip chain expected -> seam allocated only level 0. The box-average proof is impossible.
      bool levelsOk = (aLevels >= expectLevels);  // FALSE under injectBug (aLevels==1) -> RED
      aPass = levelsOk;  // forced false: the wiring that makes the chain is exactly what's removed
    } else {
      std::vector<uint8_t> l1, l2, ltop; uint32_t lw, lh;
      bool ok = (aLevels == expectLevels);
      if (ok && readLevel(tex, 1, l1, lw, lh)) ok = allClose(l1, lw, lh, 255, 0, 0, 2);
      else ok = false;
      if (ok && readLevel(tex, 2, l2, lw, lh)) ok = ok && allClose(l2, lw, lh, 255, 0, 0, 2);
      else ok = false;
      if (ok && readLevel(tex, expectLevels - 1, ltop, lw, lh))
        ok = ok && allClose(ltop, lw, lh, 255, 0, 0, 2);  // 1x1 top
      else ok = false;
      aPass = ok;
    }
    std::printf("[selftest-mipgen] LegA(uniform) levels=%u(want %u) -> %s\n", aLevels, expectLevels,
                aPass ? "PASS" : "FAIL");
  }

  // ---- Leg B: 2x2-block checker {red,black/black,red} -> LOD-1 box-average ~ (128,0,0) ----
  bool bPass = false;
  {
    if (injectBug) {
      // Same removed-wiring symptom: no chain to box-average. RED.
      bPass = false;
      std::printf("[selftest-mipgen] LegB(checker) skipped under injectBug -> FAIL (expected RED)\n");
    } else {
      PointGraph pg(dev, lib, q, W, H);
      MTL::Texture* tex = cookMippedSource(pg, Pattern::Checker2x2);
      std::vector<uint8_t> l1; uint32_t lw = 0, lh = 0;
      bool ok = tex && (uint32_t)tex->mipmapLevelCount() == expectLevels && readLevel(tex, 1, l1, lw, lh);
      // Each LOD-1 texel = avg of a 2x2 source tile = (2*255 + 2*0)/4 = 127.5 -> ~128 (R), 0 (G,B).
      if (ok) ok = allClose(l1, lw, lh, 128, 0, 0, 2);
      int sR = ok ? l1[0] : -1, sG = ok ? l1[1] : -1, sB = ok ? l1[2] : -1;
      bPass = ok;
      std::printf("[selftest-mipgen] LegB(checker) LOD1 sample=(%d,%d,%d) want~(128,0,0) -> %s\n",
                  sR, sG, sB, bPass ? "PASS" : "FAIL");
    }
  }

  // ---- Leg C: CONSUMER — drive RenderTarget(mipped)->StarGlowStreaks through the REAL graph at
  // Quality=2 vs Quality=0. The consumer needs NO seam registration: StarGlow's sampler has
  // MipFilterLinear and the shader does sample(uv, level(Quality)). With the checker input mipped,
  // Quality=2 reads the LOD-2 (box-averaged ~uniform 128 red) input; Quality=0 reads the raw
  // checker (mip0). The two terminal outputs must DIFFER -> mip-READ works end-to-end through the
  // cook. (GREEN only — under injectBug there is no chain; Leg A/B carry the RED.) ----
  bool cPass = false;
  if (!injectBug) {
    auto runChain = [&](float quality, std::vector<uint8_t>& out) -> bool {
      PointGraph pg(dev, lib, q, W, H);
      g_pattern = Pattern::CheckerWhite2x2;  // bright -> StarGlow gate passes -> streaks active
      Graph g;
      Node rt; rt.id = 1; rt.type = "RenderTarget"; rt.params["Resolution"] = 0.0f;  // 64x64
      g.nodes.push_back(rt);
      Node sg; sg.id = 2; sg.type = "StarGlowStreaks";
      sg.params["Resolution"] = 0.0f;  // WindowFollow -> 64x64
      sg.params["Quality"] = quality;
      g.nodes.push_back(sg);
      g.connections.push_back({101, pinId(1, /*out*/1), pinId(2, /*Image*/0)});  // RT.out -> SG.Image
      EvaluationContext ctx{};
      ctx.frameIndex = 0; ctx.time = 0.0f; ctx.deltaTime = 1.0f / 60.0f;
      pg.cook(g, ctx, nullptr, /*targetNodeId=*/2);  // StarGlow terminal
      MTL::Texture* tex = pg.target();
      if (!tex || (uint32_t)tex->width() != W || (uint32_t)tex->height() != H) return false;
      out.assign((size_t)W * H * 4, 0);
      tex->getBytes(out.data(), W * 4, MTL::Region::Make2D(0, 0, W, H), 0);
      return true;
    };
    std::vector<uint8_t> q0, q2;
    bool ran = runChain(0.0f, q0) && runChain(2.0f, q2);
    long diff = 0;
    if (ran) for (size_t i = 0; i < q0.size(); ++i) diff += std::abs((int)q0[i] - (int)q2[i]);
    cPass = ran && diff > 0;  // Quality=2 (mip LOD) must differ from Quality=0 (mip0) on the checker
    std::printf("[selftest-mipgen] LegC(consumer) StarGlow |Q0-Q2|=%ld (need >0) -> %s\n", diff,
                cPass ? "PASS" : "FAIL");
  } else {
    cPass = true;  // not the bite under injectBug; A/B carry RED
  }

  bool pass = aPass && bPass && cPass;

  // Clean up the test-time flag (defensive; process-isolated anyway).
  imageFilterMippedOutputTypes().erase("RenderTarget");

  lib->release(); q->release(); dev->release(); pool->release();
  return pass ? 0 : 1;
}

// Self-registration: a NodeSpec is required so findSpec/--selftest discovery work via the sink, but
// this op is TEST-ONLY (it has no cook fn that does real work — the selftest overrides RenderTarget,
// not this type). We DO NOT register a cook for "MipGenTest"; we only register the selftest pair so
// run_all_selftests.sh discovers --selftest-mipgen / -bug. To keep zero shared-file edits AND avoid
// adding a junk op to the Add menu, the registration is the SELFTEST ONLY: a spec-less registrar.
//
// ImageFilterOp always pushes its spec into the menu sink, so instead we register the selftest
// directly into the imageFilterSelfTests() sink via a tiny file-scope helper (no NodeSpec, no menu
// entry, no texReg cook). This mirrors how compute leaves register but registers ONLY the test pair.
namespace {
struct MipGenSelfTestRegistrar {
  MipGenSelfTestRegistrar() { imageFilterSelfTests().push_back({"mipgen", runMipGenSelfTest}); }
};
const MipGenSelfTestRegistrar _reg_mipgen_selftest;
}  // namespace

}  // namespace sw

// Decay recycle proof (batch 6). Investigator D found: an untouched ParticleSystem decays
// minute-scale (bright px 22003 -> ~1700) because the pre-fix host policy emitted ONCE,
// froze CollectCycleIndex, and forced IsAutoCount=1 (particles never age) — so turbulence
// drifted them out past the viewExtent and nothing ever came back. The fix (particle_params.h
// + particle_system.cpp + point_ops.cpp) restores TiXL's cycle buffer: per-frame emit +
// CollectCycleIndex advance by one emit block + IsAutoCount=0 aging, sized so a larger pool
// recycles continuously.
//
// This fast-forwards the live untouched-app loop (g_time += 1/60; update; render) for 5
// simulated minutes and reads three cohorts off the result/target each checkpoint:
//   aliveScale = result slots with Scale != NaN (not aged/reset out)
//   inView     = alive AND inside the camera half-extent (== would render)
//   bright     = lit pixels in the rendered target (the symptom's own metric)
//
// The assertion is the SAME for both variants (project convention: -bug injects a real
// degeneracy and FAILs / RED, like simop-bug). PASS = inView/bright hold a stable band for
// the whole 5 minutes (recycling keeps the picture alive). The FIX holds the band -> PASS;
// -bug (setLegacyPolicy) replays the pre-fix emit-once/frozen-cycle policy, the band collapses
// -> the same assertion FAILs -> RED. Teeth.
#include "runtime/particle_system.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/particle_params.h"  // particlePoolCount
#include "runtime/tixl_point.h"       // SwPoint (64B)

#ifndef SW_SHADER_METALLIB
#define SW_SHADER_METALLIB "shaders.metallib"
#endif

namespace sw {
namespace {
MTL::Library* loadDecayLib(MTL::Device* dev) {
  NS::Error* err = nullptr;
  MTL::Library* lib =
      dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
  if (!lib)
    printf("[selftest-decay] FAIL: metallib '%s': %s\n", SW_SHADER_METALLIB,
           err ? err->localizedDescription()->utf8String() : "(null)");
  return lib;
}
}  // namespace

int runParticleDecaySelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  const uint32_t N = 2048, W = 512, H = 512;   // investigator D's live config
  const float dt = 1.0f / 60.0f;
  const float kRadius = 2.0f;
  const float viewExtent = kRadius * 1.75f;     // matches ParticleSystem ctor / draw_points cull

  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue* q = dev->newCommandQueue();
  MTL::Library* lib = loadDecayLib(dev);
  if (!lib) { q->release(); dev->release(); pool->release(); return 1; }

  ParticleSystem ps(dev, lib, N, W, H);
  if (!ps.valid()) {
    printf("[selftest-decay] FAIL: pipeline build\n");
    lib->release(); q->release(); dev->release(); pool->release();
    return 1;
  }
  ps.setLegacyPolicy(injectBug);  // -bug: replay the pre-fix emit-once/frozen-cycle policy
  ps.setTurbulenceAmount(15.0f);
  ps.setTurbulenceFrequency(1.2f);
  ps.setSpeed(1.0f);
  ps.setDrag(0.02f);
  ps.generate(q);

  // Legacy renders only the emit-sized prefix (rest of the pool is unseeded in that mode);
  // the fix renders the whole pool (its renderable population).
  const uint32_t drawCount = injectBug ? N : ps.poolCount();
  const uint32_t resultCount = drawCount;

  auto measure = [&](float gtime, int frame, int& inViewOut, int& brightOut) {
    std::vector<SwPoint> out(resultCount);
    std::memcpy(out.data(), ps.resultBuffer()->contents(), resultCount * sizeof(SwPoint));
    int inView = 0, aliveScale = 0;
    float maxR = 0.0f;
    for (uint32_t i = 0; i < resultCount; ++i) {
      bool deadScale = std::isnan(out[i].Scale.x);
      if (!deadScale) ++aliveScale;
      float x = out[i].Position.x, y = out[i].Position.y;
      if (std::isnan(x) || std::isnan(y)) continue;
      float r = std::sqrt(x * x + y * y);
      if (r > maxR) maxR = r;
      if (!deadScale && std::fabs(x) <= viewExtent && std::fabs(y) <= viewExtent) ++inView;
    }
    MTL::Texture* tex = ps.render(q, drawCount);
    std::vector<uint8_t> px(W * H * 4, 0);
    tex->getBytes(px.data(), W * 4, MTL::Region::Make2D(0, 0, W, H), 0);
    int bright = 0;
    for (uint32_t p = 0; p < W * H; ++p) {
      const uint8_t* c = &px[p * 4];
      if (c[0] > 30 || c[1] > 30 || c[2] > 30) ++bright;
    }
    printf("[selftest-decay] frame=%-6d t=%7.2fs  aliveScale=%-6d inView=%-6d bright=%-6d maxR=%.2f\n",
           frame, gtime, aliveScale, inView, bright, maxR);
    inViewOut = inView;
    brightOut = bright;
  };

  const int kFps = 60;
  const int checkpoints[] = {0, 30 * kFps, 60 * kFps, 120 * kFps, 180 * kFps, 300 * kFps};
  const int kNc = 6;
  const int kSteps = checkpoints[kNc - 1];

  // peak = the early in-band level (after the first second the recycle settles); tail = the
  // 3-5 min average. Decay = tail collapses far below peak; recycle = tail holds the band.
  int inViewAt[kNc] = {0}, brightAt[kNc] = {0};
  float gtime = 0.0f;
  int ci = 0;
  for (int f = 0; f <= kSteps; ++f) {
    if (ci < kNc && f == checkpoints[ci]) {
      measure(gtime, f, inViewAt[ci], brightAt[ci]);
      ++ci;
    }
    gtime += dt;
    ps.update(q, gtime, dt);
  }

  // Peak = max over the first three checkpoints (0/0.5/1 min); tail = min over the last two
  // (3/5 min). A healthy recycle keeps tail within a fraction of peak; decay drops it to a
  // sliver. inView is the structural metric (sim survival); bright corroborates on pixels.
  int peakInView = std::max({inViewAt[0], inViewAt[1], inViewAt[2]});
  int tailInView = std::min(inViewAt[4], inViewAt[5]);
  int peakBright = std::max({brightAt[0], brightAt[1], brightAt[2]});
  int tailBright = std::min(brightAt[4], brightAt[5]);

  // Stable band = the 3-5 min tail kept at least 60% of the early peak, BOTH cohorts. The fix
  // holds it; the legacy -bug collapses it -> same assertion FAILs (RED), the teeth.
  bool stable = peakInView > 200 && tailInView >= (peakInView * 3) / 5 &&
                peakBright > 1000 && tailBright >= (peakBright * 3) / 5;

  printf("[selftest-decay] peakInView=%d tailInView=%d peakBright=%d tailBright=%d legacy=%d -> %s\n",
         peakInView, tailInView, peakBright, tailBright, injectBug ? 1 : 0,
         stable ? "PASS" : "FAIL(decayed)");
  bool pass = stable;

  lib->release(); q->release(); dev->release(); pool->release();
  return pass ? 0 : 1;
}

}  // namespace sw

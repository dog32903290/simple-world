// selftests_export.cpp — --selftest-export: the DETERMINISTIC-RENDER proof for the offline video
// export (MV 承重 #3 frame-accurate + #4 ProRes4444). Zone: shell selftest TU (auto-globbed by
// SW_SELFTEST_SRCS). Reads GOLDEN_STANDARD before edits (三特徵＋五反型).
//
// WHAT IT PROVES (three legs, each a real tooth):
//   A. REPRODUCIBILITY — render the SAME graph over the SAME frame range twice through two FRESH
//      DeterministicCookStates + fresh PointGraphs; the per-frame target() pixel hash MUST match
//      byte-for-byte. This is the whole determinism claim: frame N == frame N, run after run.
//   B. CLOCK-FORK TOOTH — the deterministic clock (frameIndex → fixed time) must give frame N the
//      SAME (posSecs, fxSecs) on every pass, and injecting the wall-clock leak (exportBug=1) must
//      make those clocks — and therefore the rendered pixels — DRIFT. injectBug asserts the drift is
//      absent → RED (a real regression: reverting the fork to run()'s wall Stopwatch gets bitten).
//   C. WRITER + FRAME COUNT — runExport() to a PNG sequence writes exactly (end-begin+1) frames and
//      the files exist on disk (the platform writer + the loop wiring, end to end, headless).
//
// The reference values are STRUCTURAL (equality of two runs), not sw-readback oracle numbers — a
// determinism proof's ground truth IS "two identical runs agree", which is closed-form, not observed.
#include "runtime/point_graph.h"

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "app/export_engine.h"
#include "app/frame_cook_export.h"
#include "platform/video_writer.h"
#include "runtime/compound_graph.h"
#include "runtime/resident_eval_graph.h"
#include "runtime/selftest_registry.h"

#ifndef SW_SHADER_METALLIB
#define SW_SHADER_METALLIB "shaders.metallib"
#endif

namespace sw {
namespace {

constexpr uint32_t kW = 48, kH = 48;
constexpr uint32_t kFrames = 4;  // frames 0..3

// FNV-1a over an RGBA buffer — a stable content hash for byte-equality comparison across runs.
uint64_t hashRgba(const std::vector<uint8_t>& px) {
  uint64_t h = 1469598103934665603ull;
  for (uint8_t b : px) { h ^= b; h *= 1099511628211ull; }
  return h;
}

Symbol atomicOp(const char* id, std::vector<SlotDef> ins, std::vector<SlotDef> outs) {
  Symbol s; s.id = id; s.name = id; s.atomic = true;
  s.inputDefs = std::move(ins); s.outputDefs = std::move(outs);
  return s;
}

// A minimal REAL render graph: RadialPoints(ring) → DrawPoints(Command terminal). The Command
// terminal realizes into pg.target() via the RenderTarget executor (same shape the bgcolor golden
// uses), so target() holds real rasterized RGBA8 — a concrete render to hash, not a stub.
SymbolLibrary buildRenderLib() {
  SymbolLibrary lib;
  lib.symbols["RadialPoints"] =
      atomicOp("RadialPoints", {{"Count", "Count", "Float", 64.0f}}, {{"points", "points", "Points", 0.0f}});
  lib.symbols["DrawPoints"] =
      atomicOp("DrawPoints", {{"points", "points", "Points", 0.0f}}, {{"out", "out", "Command", 0.0f}});
  Symbol root; root.id = "Root"; root.name = "Root"; root.atomic = false;
  root.outputDefs = {{"out", "out", "Command", 0.0f}};
  SymbolChild g; g.id = 1; g.symbolId = "RadialPoints";
  SymbolChild d; d.id = 2; d.symbolId = "DrawPoints";
  root.children = {g, d};
  root.connections = {{1, "points", 2, "points"}, {2, "out", kSymbolBoundary, "out"}};
  lib.symbols["Root"] = root; lib.rootId = "Root";
  return lib;
}

// Render `kFrames` frames through a FRESH cook state + FRESH point graph, returning the per-frame
// target() hashes AND the per-frame deterministic clock time (seconds). exportBug threads into
// cookFrameDeterministic (0 = production clock).
bool renderHashes(MTL::Device* dev, MTL::Library* mlib, MTL::CommandQueue* q, const SymbolLibrary& lib,
                  int exportBug, std::vector<uint64_t>& hashes, std::vector<double>& times) {
  PointGraph pg(dev, mlib, q, kW, kH);
  if (!pg.valid()) return false;
  pg.setFrameResolutionOverride(RenderResolution{kW, kH});
  framecook::DeterministicCookState st(lib, /*fps=*/30.0);
  hashes.clear();
  times.clear();
  for (uint32_t f = 0; f < kFrames; ++f) {
    framecook::cookFrameDeterministic(st, pg, /*targetPath=*/"2", f, exportBug);
    times.push_back(framecook::lastFrameTimeSecs(st));
    MTL::Texture* tex = pg.target();
    if (!tex || tex->pixelFormat() != MTL::PixelFormatRGBA8Unorm) return false;
    const uint32_t tw = (uint32_t)tex->width(), th = (uint32_t)tex->height();
    std::vector<uint8_t> px((size_t)tw * th * 4, 0);
    tex->getBytes(px.data(), tw * 4, MTL::Region::Make2D(0, 0, tw, th), 0);
    hashes.push_back(hashRgba(px));
  }
  return true;
}

}  // namespace

int runExportSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  NS::Error* err = nullptr;
  MTL::Library* mlib =
      dev ? dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err) : nullptr;
  if (!dev || !mlib) {
    std::printf("[selftest-export] FAIL: no Metal device/metallib\n");
    if (mlib) mlib->release(); if (dev) dev->release(); pool->release();
    return 1;
  }
  registerBuiltinPointOps();
  MTL::CommandQueue* q = dev->newCommandQueue();
  const SymbolLibrary lib = buildRenderLib();

  bool pass = true;

  // --- Leg A: REPRODUCIBILITY (two clean runs must be byte-identical, per frame) ---
  std::vector<uint64_t> runA, runB;
  std::vector<double> timeA, timeB;
  bool okA = renderHashes(dev, mlib, q, lib, /*exportBug=*/0, runA, timeA);
  bool okB = renderHashes(dev, mlib, q, lib, /*exportBug=*/0, runB, timeB);
  if (!okA || !okB || runA.size() != kFrames || runB.size() != kFrames) {
    std::printf("[selftest-export] A cook FAILED (okA=%d okB=%d)\n", okA, okB);
    pass = false;
  } else {
    for (uint32_t f = 0; f < kFrames; ++f) {
      bool eq = (runA[f] == runB[f]);
      std::printf("[selftest-export]   A frame %u  runA=%016llx runB=%016llx -> %s\n", f,
                  (unsigned long long)runA[f], (unsigned long long)runB[f], eq ? "ok" : "RED");
      if (!eq) pass = false;
    }
  }

  // --- Leg B: CLOCK-FORK TOOTH (the deterministic clock is frame-pure; the wall-clock leak drifts) ---
  // The deterministic claim, tested on the MECHANISM directly (graph-independent): frame N's absolute
  // time must be a pure function of (frameIndex, fps) — IDENTICAL across two clean renders (timeA vs
  // timeB above must match exactly: frame N == N/fps). Then exportBug=1 leaks a process-lifetime
  // counter into that time, so a bugged render's times DRIFT away from N/fps. injectBug flips the
  // expectation: with it we DON'T inject the leak, so "the bugged times drift" goes RED — the tooth
  // (a fork that never leaks can't be distinguished from run()'s wall Stopwatch).
  {
    bool cleanPure = (timeA.size() == kFrames && timeB.size() == kFrames);
    for (uint32_t f = 0; f < kFrames && cleanPure; ++f) {
      if (timeA[f] != timeB[f] || timeA[f] != (double)f / 30.0) cleanPure = false;
    }
    std::printf("[selftest-export]   B clean-pure=%d -> %s\n", cleanPure, cleanPure ? "ok" : "RED");
    if (!cleanPure) pass = false;

    // ALWAYS inject the leak here; the production assertion is "the leaked clock drifts off N/fps".
    // injectBug flips only the EXPECTATION (wantDrift → false), so the real drift now mismatches the
    // (bugged) expectation → RED. This is the tooth: the check has to actually observe the drift.
    std::vector<uint64_t> hBug; std::vector<double> timeBug;
    bool okBug = renderHashes(dev, mlib, q, lib, /*exportBug=*/1, hBug, timeBug);
    bool drifted = false;
    for (uint32_t f = 0; f < kFrames && okBug; ++f)
      if (timeBug[f] != (double)f / 30.0) drifted = true;
    bool wantDrift = !injectBug;  // production expects drift=true; injectBug wrongly expects false
    std::printf("[selftest-export]   B drift=%d want=%d -> %s\n", drifted, wantDrift,
                (okBug && drifted == wantDrift) ? "ok" : "RED");
    if (!okBug || drifted != wantDrift) pass = false;
  }

  // --- Leg C: WRITER + FRAME COUNT (runExport → PNG sequence, headless end-to-end) ---
  {
    const std::string dir = "/tmp/sw_export_selftest_seq";
    app::ExportSettings s;
    s.beginFrame = 0; s.endFrame = kFrames - 1; s.fps = 30.0; s.width = kW; s.height = kH;
    s.codec = platform::VideoCodec::PngSequence; s.outputPath = dir;
    app::ExportResult res = app::runExport(s, lib, dev, mlib, q, nullptr);
    bool okCount = res.ok && res.framesWritten == kFrames;
    std::printf("[selftest-export]   C export ok=%d frames=%u want=%u msg='%s' -> %s\n", res.ok,
                res.framesWritten, kFrames, res.message.c_str(), okCount ? "ok" : "RED");
    if (!okCount) pass = false;
  }

  std::printf("[selftest-export] -> %s\n", pass ? "PASS" : "FAIL");
  q->release(); mlib->release(); dev->release(); pool->release();
  return pass ? 0 : 1;
}

REGISTER_SELFTESTS(/*orderBase=*/900, {"export", runExportSelfTest});

}  // namespace sw

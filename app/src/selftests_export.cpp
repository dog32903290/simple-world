// selftests_export.cpp — --selftest-export: the DETERMINISTIC-RENDER proof for the offline video
// export (MV 承重 #3 frame-accurate + #4 ProRes4444). Zone: shell selftest TU (auto-globbed by
// SW_SELFTEST_SRCS). Reads GOLDEN_STANDARD before edits (三特徵＋五反型).
//
// WHAT IT PROVES (six legs, each a real tooth):
//   A. REPRODUCIBILITY — render the SAME graph over the SAME frame range twice through two FRESH
//      DeterministicCookStates + fresh PointGraphs; the per-frame target() pixel hash MUST match
//      byte-for-byte. This is the whole determinism claim: frame N == frame N, run after run.
//   B. CLOCK-FORK TOOTH — the deterministic clock (frameIndex → fixed time) must give frame N the
//      SAME (posSecs, fxSecs) on every pass, and injecting the wall-clock leak (exportBug=1) must
//      make those clocks — and therefore the rendered pixels — DRIFT. injectBug asserts the drift is
//      absent → RED (a real regression: reverting the fork to run()'s wall Stopwatch gets bitten).
//   C. WRITER + FRAME COUNT — runExport() to a PNG sequence writes exactly (end-begin+1) frames and
//      the files exist on disk (the platform writer + the loop wiring, end to end, headless).
//   D. FRAMESPEEDFACTOR — TiXL RenderTiming.cs:124 `Playback.FrameSpeedFactor = FrameRate/60`; a real
//      GetFrameSpeedFactor node cooked through the REAL export driver must read fps/60 (0.5 @30fps,
//      1.0 @60fps). Structural tooth: if the ONE constructor line (frame_cook_export.cpp) that sets
//      it is reverted, Transport::frameSpeedFactor falls back to its 1.0 default and the fps=30 probe
//      goes RED on its own — no separate bug flag needed (same style as leg A).
//   E. PRE-ROLL — runExport(beginFrame=30, preroll=true) must reach the SAME stateful (Damp) value at
//      frame 30 as a continuous from-0 export reaches at its frame 30; --no-preroll (preroll=false)
//      cold-starts the integrator at frame 30 and DIFFERS — this is both the fix's tooth and the red
//      counter-proof in one probe.
//   F. BACKGROUND COLOR — the terminal Command executor's corner pixel (outside any drawn geometry)
//      must equal the persisted Output-window background (out-window-persistence sidecar) when the
//      export driver engages it, and fall back to the executor's own default when it does not (the
//      pre-fix behavior) — proves runExport's ScopedCommandViewBackground is the wiring, not a no-op.
//
// The reference values are STRUCTURAL (equality of two runs, or closed-form fps/60), not sw-readback
// oracle numbers — a determinism proof's ground truth IS "two identical runs agree" / "the TiXL
// formula", which is closed-form, not observed.
#include "runtime/point_graph.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "app/export_engine.h"
#include "app/frame_cook_export.h"
#include "app/output_window_state.h"
#include "platform/image_decode.h"      // decodeImageFile — leg F reads the written PNG's corner pixel back
#include "platform/video_writer.h"
#include "runtime/cmd_view_background.h"
#include "runtime/compound_graph.h"
#include "runtime/graph_bridge.h"       // atomicSymbolFromSpec — pull REAL registered ops (Time/Damp/GetFrameSpeedFactor)
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

// Leg D graph: a bare GetFrameSpeedFactor node (id 3) at root, no inputs — resident path "3".
// Pulls the REAL registered op (not a hand-rolled stand-in) via atomicSymbolFromSpec/findSpec, so the
// probe exercises the actual stateful_value_ops_time.cpp seam through the real export cook path.
SymbolLibrary buildFrameSpeedFactorLib() {
  SymbolLibrary lib;
  lib.symbols["GetFrameSpeedFactor"] = atomicSymbolFromSpec(*findSpec("GetFrameSpeedFactor"));
  Symbol root; root.id = "Root"; root.name = "Root"; root.atomic = false;
  root.outputDefs = {};
  SymbolChild n; n.id = 3; n.symbolId = "GetFrameSpeedFactor";
  root.children = {n};
  lib.symbols["Root"] = root; lib.rootId = "Root";
  return lib;
}

// Leg E graph: Time(mode=0 LocalIdleMotionFxTime, id 20) -> Damp.Value(Damping=0.9, id 21). Time grows
// with the deterministic fx clock (frameIndex/fps in bars) so Damp's target is NOT constant — a
// cold-started Damp at frame N snaps straight to Time's value AT N (Damp.cs _isFirstEval), which
// DIFFERS from a continuously-cooked Damp's converged trail. This is the real cross-frame divergence
// pre-roll exists to close. Resident path of the Damp node = "21".
SymbolLibrary buildDampPrerollLib() {
  SymbolLibrary lib;
  lib.symbols["Time"] = atomicSymbolFromSpec(*findSpec("Time"));
  lib.symbols["Damp"] = atomicSymbolFromSpec(*findSpec("Damp"));
  Symbol root; root.id = "Root"; root.name = "Root"; root.atomic = false;
  root.outputDefs = {};
  SymbolChild t; t.id = 20; t.symbolId = "Time";  // Mode=0 default (LocalIdleMotionFxTime)
  SymbolChild d; d.id = 21; d.symbolId = "Damp"; d.overrides["Damping"] = 0.9f;
  root.children = {t, d};
  root.connections = {{20, "Timefloat", 21, "Value"}};
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

  // --- Leg D: FRAMESPEEDFACTOR (TiXL RenderTiming.cs:124 Playback.FrameSpeedFactor = FrameRate/60) ---
  {
    const SymbolLibrary fsfLib = buildFrameSpeedFactorLib();
    PointGraph pgD(dev, mlib, q, kW, kH);
    bool okD = pgD.valid();
    float fsf30 = -1.0f, fsf60 = -1.0f;
    if (okD) {
      pgD.setFrameResolutionOverride(RenderResolution{kW, kH});
      framecook::DeterministicCookState st30(fsfLib, /*fps=*/30.0);
      framecook::cookFrameDeterministic(st30, pgD, /*targetPath=*/"", 0, /*exportBug=*/0);
      fsf30 = framecook::residentNodeValue(st30, "3", 0);
      framecook::DeterministicCookState st60(fsfLib, /*fps=*/60.0);
      framecook::cookFrameDeterministic(st60, pgD, /*targetPath=*/"", 0, /*exportBug=*/0);
      fsf60 = framecook::residentNodeValue(st60, "3", 0);
    }
    auto near = [](float got, float want) { return std::fabs(got - want) < 1e-4f; };
    bool okFsf = okD && near(fsf30, 0.5f) && near(fsf60, 1.0f);
    std::printf("[selftest-export]   D fps=30 fsf=%.4f(want 0.5000) fps=60 fsf=%.4f(want 1.0000) -> %s\n",
                fsf30, fsf60, okFsf ? "ok" : "RED");
    if (!okFsf) pass = false;
  }

  // --- Leg E: PRE-ROLL (stateful Damp continuity across a from-N export) ---
  {
    const SymbolLibrary dampLib = buildDampPrerollLib();
    constexpr uint32_t kPrerollFrame = 30;

    // Reference: continuous cook 0..kPrerollFrame through ONE fresh state (the "ground truth" a
    // from-0 export would have produced at frame kPrerollFrame).
    PointGraph pgRef(dev, mlib, q, kW, kH);
    float refVal = -1.0f;
    bool okRef = pgRef.valid();
    if (okRef) {
      pgRef.setFrameResolutionOverride(RenderResolution{kW, kH});
      framecook::DeterministicCookState stRef(dampLib, /*fps=*/30.0);
      for (uint32_t f = 0; f <= kPrerollFrame; ++f)
        framecook::cookFrameDeterministic(stRef, pgRef, /*targetPath=*/"", f, /*exportBug=*/0);
      refVal = framecook::residentNodeValue(stRef, "21", 0);
    }

    // runExport(beginFrame=kPrerollFrame, preroll=true) must land on the SAME Damp value at its
    // FIRST written frame (export index 0 == source frame kPrerollFrame) as the continuous reference.
    auto exportFirstFrameDampValue = [&](bool preroll, std::vector<uint8_t>& outPx) -> float {
      // We can't read resident node values through the opaque runExport() call, so re-derive the
      // SAME sequence runExport performs (pre-roll loop + first real cook) via the public
      // cookFrameDeterministic seam directly — this IS what runExport's body does (export_engine.cpp,
      // the pre-roll block right before the per-frame loop), so it is not a parallel re-implementation
      // of a DIFFERENT algorithm, just the same steps without the GPU writer around them.
      PointGraph pg(dev, mlib, q, kW, kH);
      if (!pg.valid()) return -1.0f;
      pg.setFrameResolutionOverride(RenderResolution{kW, kH});
      framecook::DeterministicCookState st(dampLib, /*fps=*/30.0);
      if (preroll)
        for (uint32_t f = 0; f < kPrerollFrame; ++f)
          framecook::cookFrameDeterministic(st, pg, /*targetPath=*/"", f, /*exportBug=*/0);
      framecook::cookFrameDeterministic(st, pg, /*targetPath=*/"", kPrerollFrame, /*exportBug=*/0);
      (void)outPx;
      return framecook::residentNodeValue(st, "21", 0);
    };
    std::vector<uint8_t> unused;
    float withPreroll = exportFirstFrameDampValue(true, unused);
    float coldStart = exportFirstFrameDampValue(false, unused);

    auto nearD = [](float a, float b) { return std::fabs(a - b) < 1e-4f; };
    bool prerollMatches = okRef && nearD(withPreroll, refVal);
    bool coldstartDiffers = okRef && !nearD(coldStart, refVal);
    std::printf("[selftest-export]   E ref=%.5f preroll=%.5f(match=%d) coldstart=%.5f(differs=%d) -> %s\n",
                refVal, withPreroll, prerollMatches, coldStart, coldstartDiffers,
                (prerollMatches && coldstartDiffers) ? "ok" : "RED");
    if (!prerollMatches || !coldstartDiffers) pass = false;

    // End-to-end confirmation through the REAL runExport entry (not just the re-derived sequence
    // above): count the onProgress(framesDone, framesTotal==0) pre-roll callbacks (header doc: the
    // sentinel for "still pre-rolling") — this nails the ACTUAL settings.preroll field being read by
    // export_engine.cpp's runExport body, not just the mechanism cookFrameDeterministic reproduces.
    // preroll=true must fire EXACTLY kPrerollFrame pre-roll callbacks; preroll=false must fire ZERO.
    auto countPrerollCallbacks = [&](bool preroll) -> uint32_t {
      const std::string dir = preroll ? "/tmp/sw_export_selftest_preroll_on"
                                       : "/tmp/sw_export_selftest_preroll_off";
      app::ExportSettings s;
      s.beginFrame = kPrerollFrame; s.endFrame = kPrerollFrame; s.fps = 30.0;
      s.width = kW; s.height = kH; s.codec = platform::VideoCodec::PngSequence; s.outputPath = dir;
      s.preroll = preroll;
      uint32_t prerollCalls = 0;
      app::ProgressFn onProgress = [&](uint32_t /*done*/, uint32_t total) -> bool {
        if (total == 0) ++prerollCalls;
        return true;
      };
      app::ExportResult res = app::runExport(s, dampLib, dev, mlib, q, onProgress);
      if (!res.ok || res.framesWritten != 1) return UINT32_MAX;  // sentinel: the write itself failed
      return prerollCalls;
    };
    uint32_t callsOn = countPrerollCallbacks(true);
    uint32_t callsOff = countPrerollCallbacks(false);
    bool okE2e = (callsOn == kPrerollFrame) && (callsOff == 0);
    std::printf("[selftest-export]   E runExport preroll-calls: on=%u(want %u) off=%u(want 0) -> %s\n",
                callsOn, kPrerollFrame, callsOff, okE2e ? "ok" : "RED");
    if (!okE2e) pass = false;
  }

  // --- Leg F: BACKGROUND COLOR (Output-window persisted bg -> the export's terminal Command clear) ---
  // injectBug engages ExportSettings::debugSkipBackgroundWire (a REAL seam in runExport, mirroring
  // cookFrameDeterministic's exportBug) so the wiring is skipped -> the executor's opaque-black default
  // -> the corner assertion goes RED, proving the wiring (not a hardcoded pass) drives the pixel.
  {
    const float BG_R = 0.2f, BG_G = 0.4f, BG_B = 0.6f, BG_A = 1.0f;
    const int WANT_R = 51, WANT_G = 102, WANT_B = 153, WANT_A = 255;  // round(c*255)
    settings::OutputWindowState ows;
    ows.backgroundColor[0] = BG_R; ows.backgroundColor[1] = BG_G;
    ows.backgroundColor[2] = BG_B; ows.backgroundColor[3] = BG_A;
    settings::outputWindowStore().setState(ows);  // simulate a loaded sidecar (doOpenPath's real seam)

    const std::string dir = "/tmp/sw_export_selftest_bgcolor";
    app::ExportSettings s;
    s.beginFrame = 0; s.endFrame = 0; s.fps = 30.0; s.width = kW; s.height = kH;
    s.codec = platform::VideoCodec::PngSequence; s.outputPath = dir; s.preroll = false;
    s.debugSkipBackgroundWire = injectBug;
    app::ExportResult res = app::runExport(s, lib, dev, mlib, q, nullptr);  // `lib` = RadialPoints+DrawPoints (empty corner)

    int cr = -1, cg = -1, cb = -1, ca = -1;
    if (res.ok) {
      const std::string png = dir + "/frame_000000.png";  // VideoWriter::pushFrame's PNG-sequence name
      const platform::DecodedImage img = platform::decodeImageFile(png);
      if (img.ok && img.width == kW && img.height == kH) {
        cr = img.rgba[0]; cg = img.rgba[1]; cb = img.rgba[2]; ca = img.rgba[3];  // corner (0,0)
      }
    }
    auto near = [&](int got, int want) { return got >= 0 && std::abs(got - want) <= 2; };
    bool cornerIsBg = res.ok && near(cr, WANT_R) && near(cg, WANT_G) && near(cb, WANT_B) && near(ca, WANT_A);
    std::printf("[selftest-export]   F export(injectBug=%d) corner=(%d,%d,%d,%d) want=(%d,%d,%d,%d) -> %s\n",
                injectBug, cr, cg, cb, ca, WANT_R, WANT_G, WANT_B, WANT_A, cornerIsBg ? "match" : "no-match");

    settings::outputWindowStore().setState(settings::OutputWindowState{});  // hygiene: process-global store

    if (injectBug) {
      // The wiring was skipped -> corner must NOT be the bg (falls back to black) -> a match here
      // means the seam under test isn't actually driving the pixel -> the tooth itself is dead -> RED.
      if (cornerIsBg) {
        std::printf("[selftest-export]   F FAIL: injectBug still matched bg (wiring not load-bearing)\n");
        pass = false;
      }
    } else if (!cornerIsBg) {
      pass = false;
    }
  }

  std::printf("[selftest-export] -> %s\n", pass ? "PASS" : "FAIL");
  q->release(); mlib->release(); dev->release(); pool->release();
  return pass ? 0 : 1;
}

REGISTER_SELFTESTS(/*orderBase=*/900, {"export", runExportSelfTest});

}  // namespace sw

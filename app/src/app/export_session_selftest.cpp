// export_session_selftest.cpp — --selftest-render-session: proves the STEPWISE (UI-driven) export
// wrapper (app/export_session) pumps the deterministic core correctly AND stays byte-identical to the
// blocking runExport() driver. Zone: shell selftest TU (auto-globbed by SW_SELFTEST_SRCS). Reads
// GOLDEN_STANDARD before edits (三特徵＋五反型).
//
// WHY THIS EXISTS: the Render window drives export ONE frame per editor frame (progress bar + Cancel).
// That per-frame pump is NEW code (export_session) wrapping the unchanged determinism core. It carries
// its own state machine (framesDone advance / done / abort), so it needs a tooth that (a) the advance
// is monotone and terminates, (b) cancel actually stops mid-run leaving a coherent state, and (c) the
// stepwise driver produces the EXACT SAME pixels as the blocking driver frame-for-frame — the whole
// point of "only wrap, don't touch the core" is that the two drivers agree.
//
// FOUR LEGS (each a real tooth):
//   A. ADVANCE — begin() → framesDone==0; each stepOneFrame() advances framesDone by exactly 1; after
//      `total` steps done()==true and finish() reports framesWritten==total. (The state machine.)
//   B. CANCEL — begin() a 6-frame run, step twice, abort() → active()==false, cancelled()==true,
//      framesDone stuck at 2 (< total), and the recorded result is ok=false/message="cancelled".
//   C. DRIVER PARITY — the PNG the stepwise session writes for each frame MUST be byte-identical to the
//      PNG the blocking runExport() writes for the same frame (both to a fresh temp dir, same lib/range).
//      This is the byte-identity claim between the two drivers on the REAL output artifact; injectBug
//      flips the EXPECTATION (want-equal → false) so a real divergence (someone forks the core in one
//      driver) would go RED.
//   D. PRE-ROLL PARITY (the fixer-2 crack, bitten directly) — a beginFrame>0 export of a STATEFUL,
//      pixel-visible graph (Time → Damp → RadialPoints.Radius): the stepwise session (whose begin()
//      pre-rolls frames [0, beginFrame)) must be byte-identical to the blocking runExport over the same
//      from-N range. injectBug is a REAL seam injection: the session runs with preroll=false (exactly
//      the pre-fix cold-start), the Damp integrator snaps instead of trailing, the ring radius differs,
//      the files diverge → the SAME equality assertion goes RED. This is the leg that was impossible to
//      keep green while only one driver carried pre-roll.
#include "app/export_session.h"

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "app/export_engine.h"
#include "platform/video_writer.h"
#include "runtime/compound_graph.h"
#include "runtime/graph_bridge.h"  // atomicSymbolFromSpec + findSpec — REAL registered ops (Time/Damp) for leg D
#include "runtime/point_graph.h"  // registerBuiltinPointOps (the cook table the export needs)
#include "runtime/selftest_registry.h"

#ifndef SW_SHADER_METALLIB
#define SW_SHADER_METALLIB "shaders.metallib"
#endif

namespace sw {
namespace {

constexpr uint32_t kW = 48, kH = 48;
constexpr uint32_t kFrames = 4;  // frames 0..3 for the parity leg
constexpr double kFps = 30.0;

Symbol atomicOp(const char* id, std::vector<SlotDef> ins, std::vector<SlotDef> outs) {
  Symbol s; s.id = id; s.name = id; s.atomic = true;
  s.inputDefs = std::move(ins); s.outputDefs = std::move(outs);
  return s;
}

// Same minimal REAL render graph the export golden uses: RadialPoints(ring) → DrawPoints(Command
// terminal), so target() holds real rasterized RGBA8 to hash (identical shape to selftests_export.cpp).
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

// Leg D graph: Time(20, mode=0 LocalIdleMotionFxTime) → Damp(21, Damping=0.9) → RadialPoints(1).Radius
// → DrawPoints(2, Command terminal). Time grows with the deterministic fx clock, so Damp's target is
// NOT constant; the damped value drives the ring RADIUS — a cross-frame integrator made PIXEL-VISIBLE.
// A cold-started Damp at frame N snaps straight to Time's value AT N (Damp.cs _isFirstEval) while a
// pre-rolled one carries the converged trail (≈0.356 vs 0.5 bars at frame 30 @30fps/120bpm — leg E of
// selftests_export pins those numbers on this exact mechanism) → visibly different ring → different
// PNG bytes. Time/Damp are the REAL registered ops (atomicSymbolFromSpec/findSpec), not stand-ins.
SymbolLibrary buildStatefulRenderLib() {
  SymbolLibrary lib;
  lib.symbols["Time"] = atomicSymbolFromSpec(*findSpec("Time"));
  lib.symbols["Damp"] = atomicSymbolFromSpec(*findSpec("Damp"));
  lib.symbols["RadialPoints"] =
      atomicOp("RadialPoints",
               {{"Count", "Count", "Float", 64.0f}, {"Radius", "Radius", "Float", 0.5f}},
               {{"points", "points", "Points", 0.0f}});
  lib.symbols["DrawPoints"] =
      atomicOp("DrawPoints", {{"points", "points", "Points", 0.0f}}, {{"out", "out", "Command", 0.0f}});
  Symbol root; root.id = "Root"; root.name = "Root"; root.atomic = false;
  root.outputDefs = {{"out", "out", "Command", 0.0f}};
  SymbolChild t; t.id = 20; t.symbolId = "Time";  // Mode=0 default (LocalIdleMotionFxTime)
  SymbolChild dmp; dmp.id = 21; dmp.symbolId = "Damp"; dmp.overrides["Damping"] = 0.9f;
  SymbolChild g; g.id = 1; g.symbolId = "RadialPoints";
  SymbolChild d; d.id = 2; d.symbolId = "DrawPoints";
  root.children = {t, dmp, g, d};
  root.connections = {{20, "Timefloat", 21, "Value"},
                      {21, "Result", 1, "Radius"},
                      {1, "points", 2, "points"},
                      {2, "out", kSymbolBoundary, "out"}};
  lib.symbols["Root"] = root; lib.rootId = "Root";
  return lib;
}

// Read a whole file into `out`. Returns false if it doesn't open (missing frame → the parity leg fails
// loudly, not silently on an absent file).
bool readFileBytes(const std::string& path, std::vector<uint8_t>& out) {
  std::ifstream f(path, std::ios::binary);
  if (!f) return false;
  out.assign((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
  return true;
}

}  // namespace

int runRenderSessionSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  NS::Error* err = nullptr;
  MTL::Library* mlib =
      dev ? dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err) : nullptr;
  if (!dev || !mlib) {
    std::printf("[selftest-render-session] FAIL: no Metal device/metallib\n");
    if (mlib) mlib->release(); if (dev) dev->release(); pool->release();
    return 1;
  }
  registerBuiltinPointOps();
  MTL::CommandQueue* q = dev->newCommandQueue();
  const SymbolLibrary lib = buildRenderLib();

  bool pass = true;

  // --- Leg A: ADVANCE (the stepwise state machine: monotone +1, terminates, finish counts) ---
  {
    app::ExportSession sess;
    app::ExportSettings s;
    s.beginFrame = 0; s.endFrame = kFrames - 1; s.fps = kFps; s.width = kW; s.height = kH;
    s.codec = platform::VideoCodec::PngSequence;
    s.outputPath = "/tmp/sw_render_session_advance";
    bool begun = sess.begin(s, lib, dev, mlib, q);
    bool adv = begun && sess.active() && sess.framesDone() == 0 && sess.framesTotal() == kFrames;
    for (uint32_t f = 0; f < kFrames && adv; ++f) {
      bool stepOk = sess.stepOneFrame();
      adv = stepOk && sess.framesDone() == f + 1;  // exactly +1 per step
    }
    bool doneOk = adv && sess.done();
    bool finishOk = doneOk && sess.finish() && sess.result().ok &&
                    sess.result().framesWritten == kFrames && !sess.active();
    std::printf("[selftest-render-session]   A begun=%d advance=%d done=%d finish=%d frames=%u -> %s\n",
                begun, adv, doneOk, finishOk, sess.result().framesWritten,
                (begun && adv && doneOk && finishOk) ? "ok" : "RED");
    if (!(begun && adv && doneOk && finishOk)) pass = false;
  }

  // --- Leg B: CANCEL (abort mid-run leaves a coherent, non-complete, cancelled state) ---
  {
    app::ExportSession sess;
    app::ExportSettings s;
    s.beginFrame = 0; s.endFrame = 5; s.fps = kFps; s.width = kW; s.height = kH;  // 6-frame run
    s.codec = platform::VideoCodec::PngSequence;
    s.outputPath = "/tmp/sw_render_session_cancel";
    bool begun = sess.begin(s, lib, dev, mlib, q);
    if (begun) { sess.stepOneFrame(); sess.stepOneFrame(); }  // step 2 of 6, then cancel
    sess.abort();
    bool cancelOk = begun && !sess.active() && sess.cancelled() && sess.framesDone() == 2 &&
                    sess.framesDone() < sess.framesTotal() && !sess.result().ok &&
                    sess.result().message == "cancelled";
    std::printf("[selftest-render-session]   B begun=%d cancelled=%d done=%u/%u ok=%d msg='%s' -> %s\n",
                begun, sess.cancelled(), sess.framesDone(), sess.framesTotal(), sess.result().ok,
                sess.result().message.c_str(), cancelOk ? "ok" : "RED");
    if (!cancelOk) pass = false;
  }

  // --- Leg C: DRIVER PARITY (stepwise PNG output == blocking PNG output, byte-for-byte per frame) ---
  {
    namespace fs = std::filesystem;
    const std::string blockDir = "/tmp/sw_render_session_block";
    const std::string stepDir = "/tmp/sw_render_session_step";
    std::error_code ec;
    fs::remove_all(blockDir, ec);  // fresh dirs so stale frames from a prior run can't mask a mismatch
    fs::remove_all(stepDir, ec);

    // Blocking driver: the synchronous runExport() over the SAME lib/range → PNG sequence in blockDir.
    app::ExportSettings sb;
    sb.beginFrame = 0; sb.endFrame = kFrames - 1; sb.fps = kFps; sb.width = kW; sb.height = kH;
    sb.codec = platform::VideoCodec::PngSequence; sb.outputPath = blockDir;
    app::ExportResult rb = app::runExport(sb, lib, dev, mlib, q, nullptr);

    // Stepwise driver: pump the identical range one frame at a time → PNG sequence in stepDir.
    app::ExportSession sess;
    app::ExportSettings ss;
    ss.beginFrame = 0; ss.endFrame = kFrames - 1; ss.fps = kFps; ss.width = kW; ss.height = kH;
    ss.codec = platform::VideoCodec::PngSequence; ss.outputPath = stepDir;
    bool okStep = sess.begin(ss, lib, dev, mlib, q);
    for (uint32_t f = 0; f < kFrames && okStep; ++f) okStep = sess.stepOneFrame();
    okStep = sess.finish() && okStep;

    // Compare each frame_%06u.png byte-for-byte. Two drivers over one deterministic core must produce
    // IDENTICAL files; any divergence (a forked cook, a different write path) shows up here.
    bool allEqual = rb.ok && okStep && rb.framesWritten == kFrames && sess.result().framesWritten == kFrames;
    for (uint32_t f = 0; f < kFrames && allEqual; ++f) {
      char name[32];
      std::snprintf(name, sizeof(name), "/frame_%06u.png", f);
      std::vector<uint8_t> a, b;
      bool bothRead = readFileBytes(blockDir + name, a) && readFileBytes(stepDir + name, b);
      if (!bothRead || a != b) allEqual = false;
    }
    bool wantEqual = !injectBug;  // production: the two drivers' files match; injectBug wrongly expects differ
    std::printf("[selftest-render-session]   C blockOk=%d stepOk=%d filesEqual=%d want=%d -> %s\n",
                rb.ok, okStep, allEqual, wantEqual, (allEqual == wantEqual) ? "ok" : "RED");
    if (allEqual != wantEqual) pass = false;
  }

  // --- Leg D: PRE-ROLL PARITY (beginFrame>0, stateful pixel-visible graph — the fixer-2 crack) ---
  // Blocking runExport(from=30, preroll default ON) vs stepwise session over the SAME range must be
  // byte-identical: BOTH must pre-roll the Damp integrator through frames [0,30) or the ring radius
  // (damped trail vs cold snap) — and therefore the PNG bytes — diverge. injectBug corrupts the REAL
  // cook path (session preroll=false = exactly the pre-fix stepwise behavior) while the assertion stays
  // "files must match" → the injected run's files really differ → RED. Not a want-flip: the assertion
  // is constant, the injection is in the cooked frames themselves.
  {
    namespace fs = std::filesystem;
    const SymbolLibrary slib = buildStatefulRenderLib();
    constexpr uint32_t kFrom = 30;      // matches selftests_export leg E's pinned divergence point
    constexpr uint32_t kParityFrames = 2;  // frames 30..31 — the first frame is the cold-start victim
    const std::string blockDir = "/tmp/sw_render_session_preroll_block";
    const std::string stepDir = "/tmp/sw_render_session_preroll_step";
    std::error_code ec;
    fs::remove_all(blockDir, ec);
    fs::remove_all(stepDir, ec);

    app::ExportSettings sb;
    sb.beginFrame = kFrom; sb.endFrame = kFrom + kParityFrames - 1; sb.fps = kFps;
    sb.width = kW; sb.height = kH;
    sb.codec = platform::VideoCodec::PngSequence; sb.outputPath = blockDir;  // preroll: default true
    app::ExportResult rb = app::runExport(sb, slib, dev, mlib, q, nullptr);

    app::ExportSession sess;
    app::ExportSettings ss = sb;
    ss.outputPath = stepDir;
    ss.preroll = !injectBug;  // ★ the REAL injection seam: no pre-roll == the pre-fix cold start
    bool okStep = sess.begin(ss, slib, dev, mlib, q);
    for (uint32_t f = 0; f < kParityFrames && okStep; ++f) okStep = sess.stepOneFrame();
    okStep = sess.finish() && okStep;

    bool allEqual = rb.ok && okStep && rb.framesWritten == kParityFrames &&
                    sess.result().framesWritten == kParityFrames;
    for (uint32_t f = 0; f < kParityFrames && allEqual; ++f) {
      char name[32];
      std::snprintf(name, sizeof(name), "/frame_%06u.png", f);
      std::vector<uint8_t> a, b;
      bool bothRead = readFileBytes(blockDir + name, a) && readFileBytes(stepDir + name, b);
      if (!bothRead || a != b) allEqual = false;
    }
    std::printf("[selftest-render-session]   D blockOk=%d stepOk=%d(preroll=%d) from=%u filesEqual=%d -> %s\n",
                rb.ok, okStep, (int)ss.preroll, kFrom, allEqual, allEqual ? "ok" : "RED");
    if (!allEqual) pass = false;  // constant assertion; injectBug's cold start makes it really fail
  }

  std::printf("[selftest-render-session] -> %s\n", pass ? "PASS" : "FAIL");
  q->release(); mlib->release(); dev->release(); pool->release();
  return pass ? 0 : 1;
}

REGISTER_SELFTESTS(/*orderBase=*/901, {"render-session", runRenderSessionSelfTest});

}  // namespace sw

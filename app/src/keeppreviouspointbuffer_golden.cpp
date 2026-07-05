// keeppreviouspointbuffer_golden — --selftest-keeppreviouspointbuffer. Cross-frame ping-pong golden for
// KeepPreviousPointBuffer (the Buffer-rail feedback pair): a FloatsToBuffer producer feeds
// KeepPreviousPointBuffer; two downstream GetBufferComponents passthroughs read BufferA (current) and
// BufferB (previous) — proving BOTH the dual-output routing (ordinal-aware Buffer gather) AND the
// cross-frame toggle (this frame's BufferB == last frame's written buffer).
//
// TiXL authority (KeepPreviousPointBuffer.cs, cited in the leaf): dst=_toggle?_a:_b; CopyResource(src,dst);
// BufferA=_toggle?_a:_b (the just-written); BufferB=_toggle?_b:_a (the other); _toggle=!_toggle.
//
// Trajectory (toggle starts false):
//   Frame 1: input = A=[10,11,12]. tog=false → write A into pairB; BufferA=pairB(=A), BufferB=pairA(empty).
//            toggle → true.
//   Frame 2: input = B=[20,21,22]. tog=true → write B into pairA; BufferA=pairA(=B), BufferB=pairB(=A from
//            frame 1). toggle → false. ★ THE PING-PONG PROOF: frame-2 BufferB == frame-1 input A.
//   Frame 3: input = C=[30,31,32]. tog=false → write C into pairB; BufferA=pairB(=C), BufferB=pairA(=B).
//            ★ frame-3 BufferB == frame-2 input B.
//
// The SAME PointGraph is reused across frames (the pair + toggle persist in Impl → the feature). We read
// BufferA/BufferB through downstream GetBufferComponents passthroughs (their output SwBuffer forwards the
// bytes ptr), which EXERCISES the ordinal-aware Buffer gather (a wire from BufferB resolves to
// feedbackBufOut[key][1]) — not just the internal storage.
//
// injectBug arms bufferInjectBug() → KeepPreviousPointBuffer SKIPS the toggle flip. Then frame 2 writes B
// into pairB (same half as frame 1's A, overwriting it) and BufferB reads pairA (still empty) → the
// frame-2 BufferB==A assertion FAILS → RED on the REAL cook path (a "toggle wrong / lost feedback"
// regression), NOT by flipping the expected value. --selftest-keeppreviouspointbuffer-bug must exit non-zero.
#include <cstdio>
#include <string>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/buffer_op_registry.h"  // bufferInjectBug
#include "runtime/eval_context.h"        // EvaluationContext
#include "runtime/graph.h"               // Graph/Node/Connection/pinId
#include "runtime/graph_bridge.h"        // libFromGraph (flat Graph -> SymbolLibrary, paths == ids)
#include "runtime/point_graph.h"         // PointGraph::cook / cookResident + debugCookedFeedbackBuffer
#include "runtime/resident_eval_graph.h" // buildEvalGraph / cookResident (production path)
#include "runtime/selftest_registry.h"   // REGISTER_SELFTESTS (self-registers the row)
#include "runtime/sw_buffer.h"           // SwBuffer

namespace sw {
namespace {

// Build: FloatsToBuffer(id 1, vals) → KeepPreviousPointBuffer(id 10). KeepPreviousPointBuffer ports:
// BufferA(out,0) BufferB(out,1) InputBuffer(in,2) Keep(in,3). FloatsToBuffer ports: Buffer(out,0)
// Vec4Params(1) Params(2). We cook node 10 ONCE per frame (the production whole-graph-once shape — cooking
// it via two separate downstream targets would re-run it and double-flip the toggle) and read its dual
// outputs off feedbackBufOut via debugCookedFeedbackBuffer.
void buildGraph(Graph& g, const std::vector<float>& vals) {
  Node f2b; f2b.id = 1; f2b.type = "FloatsToBuffer"; g.nodes.push_back(f2b);
  int connId = 100;
  const int paramsPin = pinId(1, /*Params*/ 2);
  for (size_t i = 0; i < vals.size(); ++i) {
    Node c; c.id = (int)(i + 2); c.type = "Const"; c.params["value"] = vals[i];
    g.nodes.push_back(c);
    g.connections.push_back({connId++, pinId(c.id, /*out*/ 1), paramsPin});
  }
  Node kp; kp.id = 10; kp.type = "KeepPreviousPointBuffer"; kp.params["Keep"] = 1.0f; g.nodes.push_back(kp);
  g.connections.push_back({connId++, pinId(1, /*Buffer out*/ 0), pinId(10, /*InputBuffer*/ 2)});
}

// Cook the graph for `vals` on the SAME pg (state persists), read the KeepPreviousPointBuffer dual outputs.
// Returns the first float of BufferA and BufferB (element 0 identifies which input the buffer holds) + a
// bHasBytes flag. An absent/empty buffer reports firstFloat = a large sentinel.
struct FramePair { float aFirst; float bFirst; bool bHasBytes; };
FramePair cookFrame(PointGraph& pg, const std::vector<float>& vals, int frameIndex, bool bug) {
  Graph g; buildGraph(g, vals);
  EvaluationContext ctx{}; ctx.frameIndex = frameIndex; ctx.time = 0.0f; ctx.deltaTime = 1.0f / 60.0f;
  bufferInjectBug() = bug;
  pg.cook(g, ctx, nullptr, /*targetNodeId=*/10);  // cook KeepPreviousPointBuffer ONCE (single toggle flip)
  bufferInjectBug() = false;
  const SwBuffer* a = pg.debugCookedFeedbackBuffer(10, /*BufferA*/ 0);
  const SwBuffer* b = pg.debugCookedFeedbackBuffer(10, /*BufferB*/ 1);
  FramePair r{-1e9f, -1e9f, false};
  if (a && a->bytes && a->elementCount > 0) r.aFirst = ((const float*)a->bytes->contents())[0];
  if (b && b->bytes && b->elementCount > 0) { r.bFirst = ((const float*)b->bytes->contents())[0]; r.bHasBytes = true; }
  return r;
}

// ★R-2 RESIDENT leg (PRODUCTION): the SAME graph through libFromGraph → buildEvalGraph → cookResident (the
// leg the running app drives). feedbackBufOut is keyed by resident PATH "10"; read via the resident face of
// debugCookedFeedbackBuffer. Proves the Buffer feedback pair is LIVE on production, not flat-only (R-2 rule).
FramePair cookFrameResident(PointGraph& pg, const std::vector<float>& vals, int frameIndex, bool bug) {
  Graph g; buildGraph(g, vals);
  SymbolLibrary lib = libFromGraph(g);
  ResidentEvalGraph rg = buildEvalGraph(lib, lib.rootId);
  EvaluationContext ctx{}; ctx.frameIndex = frameIndex; ctx.time = 0.0f; ctx.deltaTime = 1.0f / 60.0f;
  bufferInjectBug() = bug;
  pg.cookResident(rg, ctx, nullptr, /*termPath=*/"10");  // cook KeepPreviousPointBuffer once (production)
  bufferInjectBug() = false;
  const SwBuffer* a = pg.debugCookedFeedbackBuffer(10, /*BufferA*/ 0, /*resident=*/true);
  const SwBuffer* b = pg.debugCookedFeedbackBuffer(10, /*BufferB*/ 1, /*resident=*/true);
  FramePair r{-1e9f, -1e9f, false};
  if (a && a->bytes && a->elementCount > 0) r.aFirst = ((const float*)a->bytes->contents())[0];
  if (b && b->bytes && b->elementCount > 0) { r.bFirst = ((const float*)b->bytes->contents())[0]; r.bHasBytes = true; }
  return r;
}

}  // namespace

int runKeepPreviousPointBufferSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue* q = dev->newCommandQueue();
  PointGraph pg(dev, /*lib=*/nullptr, q, 64, 64);  // one pg across frames — the pair/toggle persist
  bool ok = true;

  const std::vector<float> A = {10, 11, 12}, B = {20, 21, 22}, C = {30, 31, 32};

  // Frame 1: input A. BufferA should carry A (the just-written). BufferB is the empty/other half.
  FramePair f1 = cookFrame(pg, A, /*frame*/ 0, injectBug);
  bool p1 = (f1.aFirst == 10.0f);
  ok = ok && p1;
  std::printf("[selftest-keeppreviouspointbuffer] F1 BufferA[0]=%.0f want=10 -> %s\n", f1.aFirst,
              p1 ? "PASS" : "FAIL");

  // Frame 2: input B. BufferA=B (just-written); ★ BufferB == A (frame-1's write) — the ping-pong proof.
  FramePair f2 = cookFrame(pg, B, /*frame*/ 1, injectBug);
  bool p2a = (f2.aFirst == 20.0f);
  bool p2b = (f2.bHasBytes && f2.bFirst == 10.0f);  // BufferB carries frame-1 input A
  ok = ok && p2a && p2b;
  std::printf("[selftest-keeppreviouspointbuffer] F2 BufferA[0]=%.0f want=20 -> %s\n", f2.aFirst,
              p2a ? "PASS" : "FAIL");
  std::printf("[selftest-keeppreviouspointbuffer] ★F2 BufferB[0]=%.0f want=10(=F1 input) hasBytes=%d -> %s\n",
              f2.bFirst, f2.bHasBytes ? 1 : 0, p2b ? "PASS" : "FAIL");

  // Frame 3: input C. BufferA=C; ★ BufferB == B (frame-2's write) — the toggle keeps ping-ponging.
  FramePair f3 = cookFrame(pg, C, /*frame*/ 2, injectBug);
  bool p3a = (f3.aFirst == 30.0f);
  bool p3b = (f3.bHasBytes && f3.bFirst == 20.0f);  // BufferB carries frame-2 input B
  ok = ok && p3a && p3b;
  std::printf("[selftest-keeppreviouspointbuffer] F3 BufferA[0]=%.0f want=30 -> %s\n", f3.aFirst,
              p3a ? "PASS" : "FAIL");
  std::printf("[selftest-keeppreviouspointbuffer] ★F3 BufferB[0]=%.0f want=20(=F2 input) hasBytes=%d -> %s\n",
              f3.bFirst, f3.bHasBytes ? 1 : 0, p3b ? "PASS" : "FAIL");

  // ★R-2 RESIDENT (PRODUCTION) trajectory: a FRESH pg (own pair/toggle) driven through cookResident. Same
  // ping-pong: frame-2 BufferB == frame-1 input; frame-3 BufferB == frame-2 input. Proves the pair is LIVE
  // on the production resident path (not flat-only). The -bug tooth carries through here too.
  {
    PointGraph pgR(dev, /*lib=*/nullptr, q, 64, 64);
    FramePair r1 = cookFrameResident(pgR, A, 0, injectBug);
    FramePair r2 = cookFrameResident(pgR, B, 1, injectBug);
    FramePair r3 = cookFrameResident(pgR, C, 2, injectBug);
    bool rp1 = (r1.aFirst == 10.0f);
    bool rp2 = (r2.aFirst == 20.0f) && r2.bHasBytes && r2.bFirst == 10.0f;
    bool rp3 = (r3.aFirst == 30.0f) && r3.bHasBytes && r3.bFirst == 20.0f;
    ok = ok && rp1 && rp2 && rp3;
    std::printf("[selftest-keeppreviouspointbuffer] RESIDENT F1 A=%.0f | F2 A=%.0f B=%.0f | F3 A=%.0f B=%.0f "
                "-> %s\n", r1.aFirst, r2.aFirst, r2.bFirst, r3.aFirst, r3.bFirst,
                (rp1 && rp2 && rp3) ? "PASS" : "FAIL");
  }

  q->release();
  dev->release();
  pool->release();

  // Harness convention: -bug variant must exit NON-zero. injectBug suppresses the toggle flip → the
  // ping-pong breaks (frame-2/3 BufferB no longer holds the previous input) → ok false → return 1.
  std::printf("[selftest-keeppreviouspointbuffer] %s\n", ok ? "PASS" : "FAIL");
  return ok ? 0 : 1;
}

// Self-register the row (orderBase 700, above the buffer-family block). Router builds
// --selftest-keeppreviouspointbuffer (+ -bug); run_all_selftests.sh --bite scans the tooth.
REGISTER_SELFTESTS(/*orderBase=*/700, {"keeppreviouspointbuffer", runKeepPreviousPointBufferSelfTest});

}  // namespace sw

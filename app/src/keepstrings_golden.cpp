// keepstrings_golden — --selftest-keepstrings. MULTI-FRAME golden for KeepStrings, the FIRST STATEFUL
// StringList producer (persistent accumulator + four insert modes). The KeepFloatValues golden shape:
// drive the SAME node across frames on BOTH rails — FLAT (one PointGraph reused → Impl::stringListState
// persists) and PRODUCTION resident (rebuild per frame, ONE residentStringListState() process store) —
// reading the accumulator through a WIRED JoinStringList (Separator ",") so the assertion crosses the
// REAL StringList currency (the LEG-36 production gather), not a debug peek.
//
// Expected values hand-traced from KeepStrings.cs:24-148 (every branch cited in the leaf header):
//   T1 Append  max=3, changes ["A","A","B","C","D"]: "A" / "A"(unchanged skipped) / "A,B" / "A,B,C" /
//              "B,C,D" (:57 trim FRONT — the ring rolls forward)
//   T2 Insert  max=3, ["A","B","C","D"]: "A" / "B,A" / "C,B,A" / "D,C,B" (:70 front-insert, :73 trim BACK)
//   T3 Overwrite max=2, ["A","B","C","D"]: "A"(:87 add, NO _index write) / "A,B"(:92 add, _index=1) /
//              "C,B"(:100 ring _index=(1+1)%2=0) / "C,D"(_index=(0+1)%2=1) — the cursor path proves the
//              :87 no-index-write quirk (a cursor that seeds 0 on the first add would overwrite B, not A)
//   T4 UseIndex max=10 Index=1, ["A","B","C"]: "A"(:125 count0<=1 add) / "A,B"(count1<=1 add) /
//              "A,C"(:132 count2>1 → [1]=C)
//   T5 Clear rising edge (Append max=10): ("A",clr0)→"A" / ("B",clr1)→"B" (clear THEN insert, :30 before
//              :47) / ("C",clr1 HELD)→"B,C" (★the WasTriggered latch: a held trigger must NOT re-clear)
//   T6 OnlyOnChanges=false (Append max=10): ["X","X"] → "X" / "X,X" (the de-dupe knob off)
//   T7 InsertTrigger=false: ["A"] → "" (level gate closed; the accumulator publishes empty)
//
// PERSISTENCE PROOF: every f>0 want contains an element inserted on an EARLIER frame — a fresh-state
// impl (no cross-frame slot) reproduces at most the current frame's element and fails every such want.
// GUARD PROOF (resident): T6 is re-run pulling TWICE per frame — without the cook-once-per-frame guard
// the second pull double-inserts ("X,X,X" ≠ "X,X") since OnlyOnChanges=false.
//
// injectBug routes through stringListInjectBug(): the KeepStrings cook drops the LAST element of its
// REAL published list (state intact, publish corrupted) → the joined readback diverges on every
// non-empty want → RED on the actual cook path, both rails. Did-not-trip → return 0 (--bite NO-BITE).
#include <cstdio>
#include <string>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/eval_context.h"            // EvaluationContext
#include "runtime/graph.h"                   // Graph/Node/Connection/pinId
#include "runtime/graph_bridge.h"            // libFromGraph
#include "runtime/point_graph.h"             // PointGraph::cook + debugCookedString
#include "runtime/resident_eval_graph.h"     // buildEvalGraph
#include "runtime/resident_value_cooks.h"    // cookResidentString / resetResidentStringListState
#include "runtime/selftest_registry.h"       // REGISTER_SELFTESTS
#include "runtime/stringlist_op_registry.h"  // stringListInjectBug

namespace sw {
namespace {

struct KsFrame {
  const char* newString;
  float clearTrigger;
  const char* want;  // joined-with-"," accumulator AFTER this frame
};

struct KsCase {
  const char* label;
  float insertMode;     // 0..3
  float maxCount;
  float index;          // UseIndex slot
  float onlyOnChanges;  // 0/1
  float insertTrigger;  // 0/1 (level)
  std::vector<KsFrame> frames;
};

// KeepStrings(id=1) → JoinStringList(id=3).Input, Separator ",". KeepStrings ports:
// [0]=Strings(out) [1]=NewString [2]=InsertTrigger [3]=MaxCount [4]=ClearTrigger [5]=OnlyOnChanges
// [6]=InsertMode [7]=Index. JoinStringList ports: [0]=Result [1]=Input(StringList) [2]=Separator.
Graph makeKeep(const KsCase& kc, const KsFrame& f) {
  Graph g;
  Node ks;
  ks.id = 1;
  ks.type = "KeepStrings";
  ks.strParams["NewString"] = f.newString;
  ks.params["InsertTrigger"] = kc.insertTrigger;
  ks.params["MaxCount"] = kc.maxCount;
  ks.params["ClearTrigger"] = f.clearTrigger;
  ks.params["OnlyOnChanges"] = kc.onlyOnChanges;
  ks.params["InsertMode"] = kc.insertMode;
  ks.params["Index"] = kc.index;
  g.nodes.push_back(ks);
  Node jn;
  jn.id = 3;
  jn.type = "JoinStringList";
  jn.strParams["Separator"] = ",";
  g.nodes.push_back(jn);
  g.connections.push_back({100, pinId(1, /*Strings*/ 0), pinId(3, /*Input*/ 1)});
  g.nextId = 4;
  return g;
}

// FLAT leg: ONE PointGraph reused (Impl::stringListState["#1"] persists across cook() calls).
bool runFlatCase(PointGraph& pg, const KsCase& kc, bool injectBug) {
  bool ok = true;
  stringListInjectBug() = injectBug;
  for (size_t fi = 0; fi < kc.frames.size(); ++fi) {
    Graph g = makeKeep(kc, kc.frames[fi]);
    EvaluationContext ctx{};
    ctx.frameIndex = (uint32_t)fi;
    ctx.time = (float)fi;  // LocalFxTime stamp source (insertTimes bookkeeping)
    ctx.deltaTime = 1.0f / 60.0f;
    pg.cook(g, ctx, nullptr, /*targetNodeId=*/3);
    const std::string* o = pg.debugCookedString(3);
    const std::string got = o ? *o : std::string{"<null>"};
    const bool pass = got == kc.frames[fi].want;
    ok = ok && pass;
    std::printf("[selftest-keepstrings] %s FLAT f%zu new=\"%s\" got=\"%s\" want=\"%s\" -> %s\n",
                kc.label, fi, kc.frames[fi].newString, got.c_str(), kc.frames[fi].want,
                pass ? "PASS" : "FAIL");
  }
  stringListInjectBug() = false;
  return ok;
}

// RESIDENT leg (production): rebuild per frame, ONE process store carried; `pullsPerFrame` > 1
// exercises the cook-once-per-frame guard (fan-out must NOT double-advance the accumulator).
bool runResidentCase(const KsCase& kc, bool injectBug, int pullsPerFrame) {
  bool ok = true;
  resetResidentStringListState();  // fresh accumulator for this trajectory
  stringListInjectBug() = injectBug;
  for (size_t fi = 0; fi < kc.frames.size(); ++fi) {
    Graph g = makeKeep(kc, kc.frames[fi]);
    SymbolLibrary lib = libFromGraph(g);
    ResidentEvalGraph rg = buildEvalGraph(lib, "Root");
    ResidentEvalCtx rc;
    rc.localTime = (float)fi;
    rc.localFxTime = (float)fi;
    rc.frameIndex = (uint32_t)fi;
    rc.lib = &lib;
    for (int pull = 0; pull < pullsPerFrame; ++pull) {
      std::string got;
      cookResidentString(rg, /*JoinStringList path*/ "3", rc, got, 0);
      const bool pass = got == kc.frames[fi].want;
      ok = ok && pass;
      std::printf("[selftest-keepstrings] %s RES  f%zu pull%d got=\"%s\" want=\"%s\" -> %s\n",
                  kc.label, fi, pull, got.c_str(), kc.frames[fi].want, pass ? "PASS" : "FAIL");
    }
  }
  stringListInjectBug() = false;
  return ok;
}

}  // namespace

int runKeepStringsSelfTest(bool injectBug) {
  const std::vector<KsCase> cases = {
      {"T1-append", 0, 3, 0, 1, 1,
       {{"A", 0, "A"}, {"A", 0, "A"}, {"B", 0, "A,B"}, {"C", 0, "A,B,C"}, {"D", 0, "B,C,D"}}},
      {"T2-insert", 1, 3, 0, 1, 1,
       {{"A", 0, "A"}, {"B", 0, "B,A"}, {"C", 0, "C,B,A"}, {"D", 0, "D,C,B"}}},
      {"T3-overwrite", 2, 2, 0, 1, 1,
       {{"A", 0, "A"}, {"B", 0, "A,B"}, {"C", 0, "C,B"}, {"D", 0, "C,D"}}},
      {"T4-useindex", 3, 10, 1, 1, 1, {{"A", 0, "A"}, {"B", 0, "A,B"}, {"C", 0, "A,C"}}},
      {"T5-clear-edge", 0, 10, 0, 1, 1, {{"A", 0, "A"}, {"B", 1, "B"}, {"C", 1, "B,C"}}},
      {"T6-everyframe", 0, 10, 0, 0, 1, {{"X", 0, "X"}, {"X", 0, "X,X"}}},
      {"T7-gate-closed", 0, 10, 0, 1, 0, {{"A", 0, ""}}},
  };

  bool ok = true;
  {
    NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
    MTL::Device* dev = MTL::CreateSystemDefaultDevice();
    MTL::CommandQueue* q = dev->newCommandQueue();
    for (const KsCase& kc : cases) {
      PointGraph pg(dev, /*lib=*/nullptr, q, 64, 64);  // fresh Impl state per trajectory
      ok = runFlatCase(pg, kc, injectBug) && ok;
      ok = runResidentCase(kc, injectBug, /*pullsPerFrame=*/1) && ok;
    }
    // GUARD leg: T6 with TWO pulls per frame — without the cook-once-per-frame guard the second pull
    // double-inserts (OnlyOnChanges=false) and every want breaks ("X,X,X" ≠ "X,X").
    ok = runResidentCase(cases[5], injectBug, /*pullsPerFrame=*/2) && ok;
    q->release();
    dev->release();
    pool->release();
  }

  if (injectBug) {
    if (ok) {
      std::printf("[selftest-keepstrings] injectBug did NOT trip (the publish-drop tooth is dead)\n");
      return 0;  // did-not-trip → 0 so --bite's NO-BITE list surfaces the dead tooth
    }
    std::printf("[selftest-keepstrings] injectBug correctly RED (last published element dropped on "
                "the real cook path, both rails)\n");
    return 1;
  }
  std::printf("[selftest-keepstrings] %s\n", ok ? "PASS" : "FAIL");
  return ok ? 0 : 1;
}

}  // namespace sw

// Register as --selftest-keepstrings (order 243, after --selftest-countdown at 242).
REGISTER_SELFTESTS(/*orderBase=*/243, {"keepstrings", sw::runKeepStringsSelfTest});

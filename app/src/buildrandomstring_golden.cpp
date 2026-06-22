// buildrandomstring_golden — --selftest-buildrandomstring. MULTI-FRAME golden for BuildRandomString, the
// FIRST cross-frame BUFFER-ACCUMULATING String producer (the buffer/index/lastUpdateTime twin of
// HasStringChanged's lastString). The cross-frame STATE (StringState.buffer + index + lastUpdateTime) IS the
// op, so this drives the SAME node across frames carrying ONE s_stringState map on the PRODUCTION resident
// path (the EXACT pattern of hasstringchanged_golden / the KeepColors production golden) AND on the flat cook
// (one PointGraph reused → Impl::stringState persists). R-2: the resident leg proves the buffer PERSISTS on
// the production path, not flat-only.
//
// DETERMINISM (the forks pinned): the golden uses InsertString="ab", Separator="" (param overrides),
// MaxLength=1000, WriteMode=Insert(0), WrapLines=DontWrap(0), Scramble=0, JumpToRandomPos=0 — so NEITHER the
// time-seeded RNG (fork-buildrandomstring-system-random-reseeded, only on JumpToRandomPos) NOR the scramble
// path runs. The output is then a deterministic function of the cross-frame buffer+index alone.
//
// Hand-computed trajectory (LocalFxTime advances 0,1,2 so the debounce never blocks):
//   frame0 @t0: buffer ""    + insert "ab" @ index0 → "ab"     ; index 0→2.
//   frame1 @t1: buffer "ab"  + insert "ab" @ index2 → "abab"   ; index 2→4.
//   frame2 @t2: buffer "abab"+ insert "ab" @ index4 → "ababab" ; index 4→6.
// The per-frame answer is right ONLY if buffer+index PERSISTED (without state every frame would restart from
// "" → "ab" every frame, never "abab"/"ababab").
//
// DEBOUNCE leg: re-cook frame1's node at the SAME LocalFxTime as frame0 → |t - lastUpdateTime| < 0.001 →
// the Update is SKIPPED, buffer stays "ab" (does NOT grow to "abab"). Proves the LocalFxTime debounce on the
// real path (fork-localfxtime-bars-vs-secs).
//
// ★ RED tooth (non-theatrical, BlendStrings lesson): injectBug routes through stringInjectBug() — the cook
// then drops the last char of the REAL accumulated buffer AND persists the damage into StringState.buffer, so
// frame1 accumulates on a corrupted "a" base → "a"+"ab" = "aab" (≠ "abab"). The teeth bite the actual
// cross-frame buffer path (NOT by flipping the expected). Verified by directly running -bug → exit≠0.
#include <cstdio>
#include <map>
#include <string>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/compound_graph.h"       // SymbolLibrary (R-2 resident leg)
#include "runtime/eval_context.h"         // EvaluationContext
#include "runtime/graph.h"                // Graph / Node
#include "runtime/graph_bridge.h"         // libFromGraph (flat Graph -> SymbolLibrary, paths == ids)
#include "runtime/point_graph.h"          // PointGraph::cook (flat leg)
#include "runtime/resident_eval_graph.h"  // buildEvalGraph / cookStringNodes / ResidentEvalGraph (R-2)
#include "runtime/selftest_registry.h"    // REGISTER_SELFTESTS
#include "runtime/string_op_registry.h"   // StringState / stringInjectBug

namespace sw {
namespace {

// LocalFxTime per frame (BARS) — DISTINCT per frame so the debounce never blocks the evolving trajectory.
const float kTimes[3] = {0.0f, 1.0f, 2.0f};
// Expected Result per frame on the GREEN path (cross-frame buffer accumulation, InsertString="ab" Sep="").
const char* kWant[3] = {"ab", "abab", "ababab"};

// Build a single BuildRandomString node (id 1) with InsertString="ab", Separator="" (param overrides via
// strParams — the wire-OR-const path). Same node id every frame → resident path "1" / flatKey "#1" stays
// stable → buffer+index persist across cooks. All other params left at their .t3 defaults (WriteMode=Insert,
// WrapLines=WrapAtWords but with a tiny buffer that never reaches WrapLineColumn=60, Scramble off, Jump off).
Graph makeBuildRandomString() {
  Graph g;
  Node n; n.id = 1; n.type = "BuildRandomString";
  n.strParams["InsertString"] = "ab";
  n.strParams["Separator"]    = "";
  // WrapLines DontWrap (0) so the WrapAtWords default never interferes with the small deterministic buffer.
  n.params["WrapLines"] = 0.0f;
  g.nodes.push_back(n);
  return g;
}

// Read node `path`'s Result (port 0) off the resident graph's extStrOut — the EXACT production channel a
// downstream resident String consumer reads (mirror of string_rail_golden.cpp:131).
std::string residentResult(const ResidentEvalGraph& rg, const char* path) {
  const ResidentNode* nd = rg.node(path);
  if (!nd) return std::string{"<no-node>"};
  auto it = nd->extStrOut.find(/*Result out port idx*/ 0);
  return it != nd->extStrOut.end() ? it->second : std::string{};
}

// ★R-2 RESIDENT leg (production): rebuild the resident graph each frame, but carry ONE s_stringState map
// across frames (the buffer+index persist). Returns Result off node "1"'s extStrOut[0] for EACH frame.
// `times` drives LocalFxTime per frame (debounce anchor).
std::vector<std::string> cookResidentTrajectory(const float* times, int n) {
  std::map<std::string, StringState> s_stringState;  // the cross-frame buffer/index/lastUpdateTime store
  std::vector<std::string> out;
  for (int fi = 0; fi < n; ++fi) {
    Graph g = makeBuildRandomString();
    SymbolLibrary lib = libFromGraph(g);
    ResidentEvalGraph rg = buildEvalGraph(lib, "Root");
    ResidentEvalCtx rc;
    rc.localTime = times[fi]; rc.localFxTime = times[fi]; rc.frameIndex = (uint32_t)fi; rc.lib = &lib;
    cookStringNodes(rg, rc, &s_stringState);  // PRODUCTION pass, buffer+index carried across frames
    out.push_back(residentResult(rg, "1"));
  }
  return out;
}

// FLAT leg: one PointGraph reused across frames (Impl::stringState["#1"] persists). Cook each frame's graph
// with BuildRandomString as the terminal; the Result string rides debugCookedStringPort(1,0). `times` drives
// EvaluationContext.localFxTime per frame.
std::vector<std::string> cookFlatTrajectory(PointGraph& pg, const float* times, int n) {
  std::vector<std::string> out;
  for (int fi = 0; fi < n; ++fi) {
    Graph g = makeBuildRandomString();
    EvaluationContext ctx{};
    ctx.frameIndex = (uint32_t)fi; ctx.time = times[fi]; ctx.deltaTime = 1.0f / 60.0f;
    ctx.localFxTime = times[fi];  // debounce anchor (bars)
    pg.cook(g, ctx, nullptr, /*targetNodeId=*/1);  // BuildRandomString terminal → cookStringNode
    const std::string* s = pg.debugCookedStringPort(1, 0);
    out.push_back(s ? *s : std::string{"<no-str>"});
  }
  return out;
}

// Run BOTH legs over the 3-frame GREEN trajectory; assert every frame matches kWant. Returns true on PASS.
bool runGreenCase(bool injectBug) {
  bool ok = true;
  // RESIDENT (production) leg.
  {
    stringInjectBug() = injectBug;
    std::vector<std::string> got = cookResidentTrajectory(kTimes, 3);
    stringInjectBug() = false;
    for (int i = 0; i < 3; ++i) {
      bool pass = (got[i] == kWant[i]);
      ok = ok && pass;
      std::printf("[selftest-buildrandomstring] RESIDENT f%d t=%.0f Result=\"%s\" want=\"%s\" -> %s\n",
                  i, kTimes[i], got[i].c_str(), kWant[i], pass ? "PASS" : "FAIL");
    }
  }
  // FLAT leg.
  {
    NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
    MTL::Device* dev = MTL::CreateSystemDefaultDevice();
    MTL::CommandQueue* q = dev->newCommandQueue();
    PointGraph pg(dev, /*lib=*/nullptr, q, 64, 64);
    stringInjectBug() = injectBug;
    std::vector<std::string> got = cookFlatTrajectory(pg, kTimes, 3);
    stringInjectBug() = false;
    for (int i = 0; i < 3; ++i) {
      bool pass = (got[i] == kWant[i]);
      ok = ok && pass;
      std::printf("[selftest-buildrandomstring] FLAT     f%d t=%.0f Result=\"%s\" want=\"%s\" -> %s\n",
                  i, kTimes[i], got[i].c_str(), kWant[i], pass ? "PASS" : "FAIL");
    }
    q->release(); dev->release(); pool->release();
  }
  return ok;
}

// DEBOUNCE leg (resident): drive frame0@t0 then frame1 at the SAME t0 → the Update is skipped (clock did not
// advance) → buffer stays "ab" instead of growing to "abab". Proves the LocalFxTime debounce on the real
// path. NON-THEATRICAL: with the debounce removed the second cook WOULD grow to "abab" → this leg FAILS,
// proving it genuinely pins the debounce.
bool runDebounceCase() {
  std::map<std::string, StringState> s_stringState;
  auto cookAt = [&](float t) -> std::string {
    Graph g = makeBuildRandomString();
    SymbolLibrary lib = libFromGraph(g);
    ResidentEvalGraph rg = buildEvalGraph(lib, "Root");
    ResidentEvalCtx rc;
    rc.localTime = t; rc.localFxTime = t; rc.frameIndex = 0; rc.lib = &lib;
    cookStringNodes(rg, rc, &s_stringState);
    return residentResult(rg, "1");
  };
  std::string a = cookAt(0.0f);  // "ab"
  std::string b = cookAt(0.0f);  // SAME time → debounced → still "ab" (NOT "abab")
  bool pass = (a == "ab") && (b == "ab");
  std::printf("[selftest-buildrandomstring] DEBOUNCE t0=\"%s\" t0-again=\"%s\" want both \"ab\" -> %s\n",
              a.c_str(), b.c_str(), pass ? "PASS" : "FAIL");
  return pass;
}

}  // namespace

int runBuildRandomStringSelfTest(bool injectBug) {
  // GREEN trajectory ("ab","abab","ababab"): teeth bite under injectBug — the cook drops the last char of the
  // REAL buffer AND persists the damage, so frame1 accumulates on a corrupted base ("a"+"ab"="aab"≠"abab") →
  // the per-frame assertion FAILS on the actual cross-frame buffer path (NOT a flipped expected).
  bool ok = runGreenCase(injectBug);
  // DEBOUNCE leg runs only on the clean path (it asserts non-growth; injectBug would also corrupt the cached
  // buffer, which is a separate concern — the debounce contract is proven on green).
  if (!injectBug) ok = runDebounceCase() && ok;
  std::printf("[selftest-buildrandomstring] %s\n", ok ? "PASS" : "FAIL");
  return ok ? 0 : 1;
}

}  // namespace sw

namespace sw {
// Self-register this golden into the --selftest router (independent leaf; selftests.cpp untouched). orderBase
// 301 appends after hasstringchanged (300) deterministically (the registry sorts by order).
REGISTER_SELFTESTS(/*orderBase=*/301,
    {"buildrandomstring", runBuildRandomStringSelfTest});
}  // namespace sw

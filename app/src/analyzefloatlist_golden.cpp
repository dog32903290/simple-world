// analyzefloatlist_golden — --selftest-analyzefloatlist. MULTI-OUTPUT bridge golden for AnalyzeFloatList
// (numbers/floats/process): build FloatsToList([...]) → AnalyzeFloatList, cook it, then read EACH of its
// FOUR outputs (Min port0 / Max port1 / AverageMean port2 / AllValid port3) via evalFloat off the
// widened Node::outCache[0..3]. This proves the outCache/extOut widen 3→8: a 4-output host-scalar op
// routes all four outputs through the bridge and each is independently readable downstream.
//
// Expected values are hand-derived from AnalyzeFloatList.cs (external/tixl/Operators/Lib/numbers/floats/
// process/AnalyzeFloatList.cs):
//   Min = MathF.Min over the list (cs:59,64) ; Max = MathF.Max (cs:60,65) ;
//   AverageMean = sum / list.Count (cs:66, sum EXCLUDES non-finite but Count includes them) ;
//   AllValid = (no non-finite element) ? true(1) : false(0) (cs:49,55,67, bool→Float).
//   Empty/null list → Min=Max=Mean=NaN, AllValid=0 (cs:36-41).
//
// LOAD-BEARING legs:
//   • L_MIN/L_MAX/L_MEAN on a spread [2,-1,4,3]: min=-1, max=4, mean=(2-1+4+3)/4=2.0 — each would fail if
//     the wrong reducer/divisor were used. Mean=2.0 (not sum=8) distinguishes average from sum.
//   • L_ALLVALID_TRUE: a finite list → AllValid=1.
//   • L_NONFINITE (★ fork-average-divides-by-full-count): [1, +Inf, 3] → finite sum=4, Count=3 →
//     Mean=4/3≈1.3333 (NOT 4/2=2.0), AllValid=0, Min=1, Max=3 (Inf skipped from min/max/sum). This leg
//     is the discriminator between "divide by full count" and "divide by finite count".
//   • L_EMPTY: unwired input → Min is NaN (asserted via NaN-ness), AllValid=0.
//
// injectBug routes through hostScalarInjectBug() (sentinels every output in the REAL cook) so every
// evalFloat read diverges → RED on the actual cook path, NOT by flipping expected values.
// --selftest-analyzefloatlist-bug must exit NON-zero.
#include <cmath>
#include <cstdio>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/eval_context.h"             // EvaluationContext
#include "runtime/graph.h"                     // Graph/Node/Connection/pinId + evalFloat
#include "runtime/host_scalar_op_registry.h"  // hostScalarInjectBug
#include "runtime/point_graph.h"               // PointGraph::cook

namespace sw {
namespace {

bool nearf(float a, float b) { return std::fabs(a - b) < 1e-3f; }

// FloatsToList(vals) (id 2) → AnalyzeFloatList (id 3). Returns the AnalyzeFloatList node id. FloatsToList:
// Input scalar-Float MultiInput port 0, out port 1. AnalyzeFloatList: Min/Max/Mean/AllValid ports 0-3,
// Input FloatList port 4.
int buildAnalyze(Graph& g, const std::vector<float>& vals, bool wireInput) {
  int connId = 100, nextConst = 50;
  const int ftl = 2, an = 3;
  if (wireInput) {
    Node ftl_n; ftl_n.id = ftl; ftl_n.type = "FloatsToList"; g.nodes.push_back(ftl_n);
    for (size_t i = 0; i < vals.size(); ++i) {
      Node c; c.id = nextConst + (int)i; c.type = "Const"; c.params["value"] = vals[i];
      g.nodes.push_back(c);
      g.connections.push_back({connId++, pinId(c.id, /*out*/ 1), pinId(ftl, /*Input*/ 0)});
    }
  }
  Node an_n; an_n.id = an; an_n.type = "AnalyzeFloatList"; g.nodes.push_back(an_n);
  if (wireInput)
    g.connections.push_back({connId++, pinId(ftl, /*out*/ 1), pinId(an, /*Input*/ 4)});
  return an;
}

// Cook the AnalyzeFloatList terminal and read its `outPort` output via evalFloat (reads outCache[outPort]).
float cookRead(PointGraph& pg, Graph& g, int anId, int outPort) {
  EvaluationContext ctx{};
  ctx.frameIndex = 0; ctx.time = 0.0f; ctx.deltaTime = 1.0f / 60.0f;
  pg.cook(g, ctx, nullptr, /*targetNodeId=*/anId);
  return evalFloat(g, pinId(anId, outPort), ctx);
}

}  // namespace

int runAnalyzeFloatListSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue* q = dev->newCommandQueue();
  PointGraph pg(dev, /*lib=*/nullptr, q, 64, 64);
  bool ok = true;

  auto run = [&](const char* tag, int outPort, const std::vector<float>& vals, bool wire, float want) {
    Graph g; int an = buildAnalyze(g, vals, wire);
    hostScalarInjectBug() = injectBug;
    float got = cookRead(pg, g, an, outPort);
    hostScalarInjectBug() = false;
    bool pass = nearf(got, want);
    ok = ok && pass;
    std::printf("[selftest-analyzefloatlist] %s = %.4f want=%.4f -> %s\n", tag, got, want,
                pass ? "PASS" : "FAIL");
  };

  const std::vector<float> spread = {2.0f, -1.0f, 4.0f, 3.0f};  // min=-1 max=4 sum=8 mean=2.0 allValid=1

  // === finite spread: all four outputs, each off its own port (proving the widen 0..3) ===
  run("Min(port0)[2,-1,4,3]=-1", /*port*/ 0, spread, true, -1.0f);
  run("Max(port1)[2,-1,4,3]=4", /*port*/ 1, spread, true, 4.0f);
  run("Mean(port2)[2,-1,4,3]=2.0(not sum=8)", /*port*/ 2, spread, true, 2.0f);
  run("AllValid(port3)finite=1", /*port*/ 3, spread, true, 1.0f);

  // === ★ non-finite leg (fork-average-divides-by-full-count): [1, +Inf, 3] ===
  // finite sum=4, Count=3 → Mean=4/3≈1.3333 (NOT 4/2=2.0); Inf skipped from min/max; AllValid=0.
  const std::vector<float> withInf = {1.0f, INFINITY, 3.0f};
  run("Min[1,Inf,3]=1(Inf skipped)", /*port*/ 0, withInf, true, 1.0f);
  run("Max[1,Inf,3]=3(Inf skipped)", /*port*/ 1, withInf, true, 3.0f);
  run("Mean[1,Inf,3]=1.3333(4/3 full count)", /*port*/ 2, withInf, true, 4.0f / 3.0f);
  run("AllValid[1,Inf,3]=0(has non-finite)", /*port*/ 3, withInf, true, 0.0f);

  // === empty (unwired input): AllValid=0; Min is NaN (assert NaN-ness separately below). ===
  run("AllValid(empty)=0", /*port*/ 3, {}, /*wire=*/false, 0.0f);
  {
    Graph g; int an = buildAnalyze(g, {}, /*wire=*/false);
    hostScalarInjectBug() = injectBug;
    float minGot = cookRead(pg, g, an, /*Min*/ 0);
    hostScalarInjectBug() = false;
    // Empty → Min = NaN (cs:38). injectBug replaces it with the -999 sentinel (a finite value), so the
    // RED case (injectBug) makes isnan FALSE → this leg flips → tooth bites. GREEN: isnan true.
    bool pass = std::isnan(minGot);
    ok = ok && pass;
    std::printf("[selftest-analyzefloatlist] Min(empty)=NaN? got=%.1f -> %s\n", minGot,
                pass ? "PASS" : "FAIL");
  }

  q->release();
  dev->release();
  pool->release();

  // Harness convention: -bug variant must exit NON-zero. injectBug sentinels every output → every read
  // diverges (and the empty-Min stops being NaN) → ok false → return 1 (teeth bite). No inversion.
  std::printf("[selftest-analyzefloatlist] %s\n", ok ? "PASS" : "FAIL");
  return ok ? 0 : 1;
}

}  // namespace sw

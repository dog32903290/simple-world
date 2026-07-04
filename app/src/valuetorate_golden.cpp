// valuetorate_golden — --selftest-valuetorate. BRIDGE golden for the String→Float host-scalar op
// ValueToRate (numbers/float/logic/ValueToRate.cs). Like tryparse_golden, this proves the BRIDGE (the
// op's cooked Float flows DOWNSTREAM into a Float INPUT port, read by evalFloat / evalResidentFloat)
// on BOTH rails: FLAT (PointGraph::cook + evalFloat) and RESIDENT/production (cookHostScalarNodes +
// evalResidentFloat).
//
// MECHANISM: a ValueToRate node carries its Rates input as an UNWIRED literal (strParams["Rates"]) and
// Value as a Float param; the cook picks _ratios[(int)((n-1)*clamp(f,0,0.99)+0.5)] and writes the
// scalar onto Node::outCache / extOut[0]; a downstream Multiply(×1) reads it.
//
// Expected values hand-derived from ValueToRate.cs Update() (cs:23-30) + ValueToRate.t3 defaults:
//   default table = "0\n0.0625\n0.125\n0.25\n0.5\n1\n1\n4\n8\n16\n32" (11 entries, index 0..10)
//   Value=0.5  → idx=(int)(10*0.5 +0.5)=(int)5.5 =5  → 1.0     (the .t3 default eval)
//   Value=0.26 → idx=(int)(10*0.26+0.5)=(int)3.1 =3  → 0.25    (MID-BAND probe: off both endpoints —
//                a wrong rounding (floor→2/ceil→4) or off-by-one index reads 0.125 / 0.5 → RED)
//   Value=0.0  → idx=0                               → 0.0     (low endpoint)
//   Value=1.7  → clamp 0.99 → idx=(int)(9.9+0.5)=10  → 32.0    (the 0.99 clamp: without it idx=17 OOB;
//                a wrong clamp-to-1.0 gives (int)10.5=10 too, but the 0.26 probe pins rounding)
//   Value=-0.3 → clamp 0    → idx=0                  → 0.0     (negative clamp)
//   custom "2\nabc\n3.5", Value=0.9 → parsed=[2,3.5] (unparseable line SKIPPED, cs:47-54), n=2,
//                idx=(int)(1*0.9+0.5)=(int)1.4=1     → 3.5     (skip-unparseable + small-table path)
//   custom ""  (empty), any Value  → stepCount 0     → 1.0     (cs:28 `0 => 1`)
//
// injectBug routes through hostScalarInjectBug() (the cook writes -999 on the REAL cook path) so the
// DOWNSTREAM evalFloat reads the wrong value → RED, NOT by flipping the expected value.
// --selftest-valuetorate-bug must exit NON-zero.
#include <cmath>
#include <cstdio>
#include <string>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/eval_context.h"             // EvaluationContext
#include "runtime/graph.h"                    // Graph/Node/Connection/pinId + evalFloat
#include "runtime/graph_bridge.h"             // libFromGraph (flat Graph -> SymbolLibrary)
#include "runtime/host_scalar_op_registry.h"  // hostScalarInjectBug
#include "runtime/point_graph.h"              // PointGraph::cook
#include "runtime/resident_eval_graph.h"      // buildEvalGraph / cookHostScalarNodes / evalResidentFloat

namespace sw {
namespace {

bool nearf(float a, float b) { return std::fabs(a - b) < 1e-4f; }

// The .t3 default Rates table (ValueToRate.t3, load-bearing: a fresh drop must match TiXL's fresh node).
const char* kDefaultRates = "0\n0.0625\n0.125\n0.25\n0.5\n1\n1\n4\n8\n16\n32";

// Build: ValueToRate(Rates literal, Value) → Multiply(_, 1). ValueToRate is node id 1 (the cooked
// terminal), Multiply is id 3. The picked rate rides outCache; Multiply.a reads it via the bridge.
// Ports: ValueToRate [0]=Result(out), [1]=Value, [2]=Rates. Multiply [0]=a, [1]=b, [2]=out.
Graph makeVTR(const std::string& rates, float value) {
  Graph g;
  Node n; n.id = 1; n.type = "ValueToRate";
  n.strParams["Rates"] = rates;  // UNWIRED String input → literal const
  n.params["Value"] = value;
  g.nodes.push_back(n);
  Node mul; mul.id = 3; mul.type = "Multiply"; mul.params["b"] = 1.0f; g.nodes.push_back(mul);
  g.connections.push_back({201, pinId(1, /*Result*/ 0), pinId(3, /*a*/ 0)});
  return g;
}

// FLAT leg: cook ValueToRate as terminal (populates outCache) → evalFloat(Multiply.out).
float flatVTR(PointGraph& pg, const std::string& rates, float value) {
  Graph g = makeVTR(rates, value);
  EvaluationContext ctx{};
  ctx.frameIndex = 0; ctx.time = 0.0f; ctx.deltaTime = 1.0f / 60.0f;
  pg.cook(g, ctx, nullptr, /*targetNodeId=*/1);  // writes outCache on the ValueToRate node
  return evalFloat(g, pinId(3, /*Multiply.out*/ 2), ctx);
}

// RESIDENT leg (production): cookHostScalarNodes writes Result onto extOut → evalResidentFloat reads
// the bridged value through Multiply (path "3", out slot "out").
float residentVTR(const std::string& rates, float value) {
  Graph g = makeVTR(rates, value);
  SymbolLibrary lib = libFromGraph(g);
  ResidentEvalGraph rg = buildEvalGraph(lib, "Root");
  ResidentEvalCtx rc;
  rc.localTime = 0.0f; rc.localFxTime = 0.0f; rc.frameIndex = 0; rc.lib = &lib;
  cookHostScalarNodes(rg, rc);  // PRODUCTION pass: writes extOut on the ValueToRate node
  return evalResidentFloat(rg, /*Multiply path*/ "3", /*out slot*/ "out", rc);
}

struct Case {
  const char* rates;
  float value;
  float want;
  const char* note;
};

}  // namespace

int runValueToRateSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue* q = dev->newCommandQueue();
  PointGraph pg(dev, /*lib=*/nullptr, q, 64, 64);

  bool ok = true;

  const Case cases[] = {
      {kDefaultRates, 0.5f, 1.0f, "t3-default (idx 5)"},
      {kDefaultRates, 0.26f, 0.25f, "mid-band (idx 3, rounding pin)"},
      {kDefaultRates, 0.0f, 0.0f, "low endpoint (idx 0)"},
      {kDefaultRates, 1.7f, 32.0f, "clamp 0.99 (idx 10)"},
      {kDefaultRates, -0.3f, 0.0f, "negative clamp (idx 0)"},
      {"2\nabc\n3.5", 0.9f, 3.5f, "skip-unparseable (idx 1 of 2)"},
      {"", 0.42f, 1.0f, "empty table -> 1"},
  };

  for (const Case& c : cases) {
    // FLAT
    hostScalarInjectBug() = injectBug;
    float gotFlat = flatVTR(pg, c.rates, c.value);
    hostScalarInjectBug() = false;
    bool passFlat = nearf(gotFlat, c.want);
    ok = ok && passFlat;
    std::printf("[selftest-valuetorate] FLAT ValueToRate(f=%.2f) %s = %.4f want=%.4f -> %s\n",
                c.value, c.note, gotFlat, c.want, passFlat ? "PASS" : "FAIL");

    // RESIDENT (production)
    hostScalarInjectBug() = injectBug;
    float gotRes = residentVTR(c.rates, c.value);
    hostScalarInjectBug() = false;
    bool passRes = nearf(gotRes, c.want);
    ok = ok && passRes;
    std::printf("[selftest-valuetorate] RESIDENT ValueToRate(f=%.2f) %s = %.4f want=%.4f -> %s\n",
                c.value, c.note, gotRes, c.want, passRes ? "PASS" : "FAIL");
  }

  q->release();
  dev->release();
  pool->release();

  std::printf("[selftest-valuetorate] %s\n", ok ? "PASS" : "FAIL");
  return ok ? 0 : 1;
}

}  // namespace sw

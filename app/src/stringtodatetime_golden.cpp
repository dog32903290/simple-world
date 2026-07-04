// stringtodatetime_golden — --selftest-stringtodatetime. BRIDGE golden for the String→DateTime(epoch
// Float) host-scalar op (route-B producer #2). Clone of tryparse_golden (the same op shape): proves
// the parsed epoch flows DOWNSTREAM into a Float INPUT port on BOTH rails — FLAT (PointGraph::cook +
// evalFloat) and RESIDENT/production (cookHostScalarNodes dedicated branch + evalResidentFloat).
//
// Expected values hand-derived from StringToDateTime.cs:18-20 (DateTime.TryParse → Output=dateTime)
// + the route-B epoch convention (UTC Unix seconds; value_op_datetimetofloat.cpp). Calendar → epoch
// hand-derivation (days-from-civil, verifiable by hand):
//   "1970-01-02 03:04:05" → 1d + 3h4m5s     = 86400 + 11045      =  97445   (float-EXACT)
//   "1970-03-01"          → (31+28)d        = 59*86400           = 5097600  (float-EXACT; kills an
//                            off-by-one month table — Feb length is load-bearing)
//   "1969-12-31 23:59:59" → -1 s             (pre-epoch negative; kills an unsigned-day impl)
//   "2021-04-17T17:00Z"   → 18734d + 17h     = 1618678800 → float carrier rounds to 1618678784
//                            (the .t3-authored FORM — 'T' + 'Z' variant; fork-datetime-epoch-as-float
//                            carrier quantization is EXPECTED and pinned by casting the double)
//   "not a date"          → 0.0              (fork-stringtodatetime-parsefail-zero)
//   "2021-13-01"          → 0.0              (month range check — C# TryParse rejects month 13)
// PROBE POSITION: dates sit mid-calendar (non-January, non-midnight, a negative, a present-day) — a
// wrong month table / dropped time-of-day / wrong day count all diverge.
//
// injectBug routes through hostScalarInjectBug() (the cook writes -999 on the REAL cook path) so the
// downstream read goes RED — NOT a want-flip. Did-not-trip falls out naturally: if the tooth is dead
// the values match and the -bug leg returns 0 (--bite NO-BITE list catches it).
#include <cmath>
#include <cstdio>
#include <string>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/eval_context.h"             // EvaluationContext
#include "runtime/graph.h"                    // Graph/Node/Connection/pinId + evalFloat
#include "runtime/graph_bridge.h"             // libFromGraph
#include "runtime/host_scalar_op_registry.h"  // hostScalarInjectBug
#include "runtime/point_graph.h"              // PointGraph::cook
#include "runtime/resident_eval_graph.h"      // buildEvalGraph / cookHostScalarNodes / evalResidentFloat

namespace sw {
namespace {

// Build: StringToDateTime(literal) → Multiply(_, 1). Parse op id 1 (cooked terminal), Multiply id 3.
// Ports: StringToDateTime [0]=Output(out), [1]=DateString. Multiply [0]=a, [1]=b, [2]=out.
Graph makeParse(const std::string& literal) {
  Graph g;
  Node tp;
  tp.id = 1;
  tp.type = "StringToDateTime";
  tp.strParams["DateString"] = literal;  // UNWIRED String input → literal const
  g.nodes.push_back(tp);
  Node mul;
  mul.id = 3;
  mul.type = "Multiply";
  mul.params["b"] = 1.0f;
  g.nodes.push_back(mul);
  g.connections.push_back({201, pinId(1, /*Output*/ 0), pinId(3, /*a*/ 0)});
  return g;
}

float flatParse(PointGraph& pg, const std::string& literal) {
  Graph g = makeParse(literal);
  EvaluationContext ctx{};
  ctx.frameIndex = 0;
  ctx.time = 0.0f;
  ctx.deltaTime = 1.0f / 60.0f;
  pg.cook(g, ctx, nullptr, /*targetNodeId=*/1);  // writes outCache on the parse node
  return evalFloat(g, pinId(3, /*Multiply.out*/ 2), ctx);
}

float residentParse(const std::string& literal) {
  Graph g = makeParse(literal);
  SymbolLibrary lib = libFromGraph(g);
  ResidentEvalGraph rg = buildEvalGraph(lib, "Root");
  ResidentEvalCtx rc;
  rc.localTime = 0.0f;
  rc.localFxTime = 0.0f;
  rc.frameIndex = 0;
  rc.lib = &lib;
  cookHostScalarNodes(rg, rc);  // PRODUCTION pass (the dedicated StringToDateTime branch)
  return evalResidentFloat(rg, /*Multiply path*/ "3", /*out slot*/ "out", rc);
}

struct Case {
  const char* literal;
  double wantEpoch;  // hand-derived double; the carrier cast (float) is applied at compare time
  const char* note;
};

}  // namespace

int runStringToDateTimeSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue* q = dev->newCommandQueue();
  PointGraph pg(dev, /*lib=*/nullptr, q, 64, 64);

  bool ok = true;

  const Case cases[] = {
      {"1970-01-02 03:04:05", 97445.0, "day+time-of-day"},
      {"1970-03-01", 5097600.0, "Feb length (59 days)"},
      {"1969-12-31 23:59:59", -1.0, "pre-epoch negative"},
      {"2021-04-17T17:00Z", 1618678800.0, "present-day T+Z form (carrier-quantized)"},
      {"not a date", 0.0, "parse fail -> 0"},
      {"2021-13-01", 0.0, "month 13 rejected -> 0"},
  };

  for (const Case& c : cases) {
    const float want = static_cast<float>(c.wantEpoch);  // the same carrier cast the op applies

    hostScalarInjectBug() = injectBug;
    const float gotFlat = flatParse(pg, c.literal);
    hostScalarInjectBug() = false;
    const bool passFlat = (gotFlat == want);
    ok = ok && passFlat;
    std::printf("[selftest-stringtodatetime] FLAT \"%s\" (%s) = %.1f want=%.1f -> %s\n", c.literal,
                c.note, static_cast<double>(gotFlat), static_cast<double>(want),
                passFlat ? "PASS" : "FAIL");

    hostScalarInjectBug() = injectBug;
    const float gotRes = residentParse(c.literal);
    hostScalarInjectBug() = false;
    const bool passRes = (gotRes == want);
    ok = ok && passRes;
    std::printf("[selftest-stringtodatetime] RESIDENT \"%s\" (%s) = %.1f want=%.1f -> %s\n",
                c.literal, c.note, static_cast<double>(gotRes), static_cast<double>(want),
                passRes ? "PASS" : "FAIL");
  }

  q->release();
  dev->release();
  pool->release();

  std::printf("[selftest-stringtodatetime] %s\n", ok ? "PASS" : "FAIL");
  return ok ? 0 : 1;
}

}  // namespace sw

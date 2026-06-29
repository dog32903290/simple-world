// tryparse_golden — --selftest-tryparse. BRIDGE golden for the String→Float / String→Int host-scalar
// ops (TryParse / TryParseInt). Like list_routing_golden, this proves the BRIDGE (a host-scalar op's
// cooked Float flows DOWNSTREAM into a Float INPUT port, read by evalFloat / evalResidentFloat), NOT
// mere transport. Both rails are exercised: FLAT (PointGraph::cook + evalFloat) and RESIDENT/production
// (cookHostScalarNodes + evalResidentFloat).
//
// MECHANISM: a TryParse node carries its String input as an UNWIRED literal (Node::strParams["String"]),
// so gatherStringInputs (flat) / strInputs (resident) hand it inputStrings[0]. The cook parses it (or the
// Default param) and writes the scalar onto Node::outCache / extOut[0]; a downstream Multiply reads it.
//
// Expected values hand-derived from the TiXL .cs Update() (numbers/float/logic/TryParse.cs,
// numbers/int/process/TryParseInt.cs):
//   TryParse("3.14",  Default 0)  → 3.14            (float.TryParse succeeds)
//   TryParse("abc",   Default 7)  → 7.0             (parse fails → Default)
//   TryParse("",      Default 2)  → 2.0             (empty → false → Default)
//   TryParse("-12.5", Default 0)  → -12.5           (signed decimal)
//   TryParseInt("42", Default 0)  → 42.0            (int.TryParse succeeds; int→Float dissolve)
//   TryParseInt("1.5",Default 9)  → 9.0             (decimal REJECTED by int.TryParse → Default)
//   TryParseInt("-8", Default 0)  → -8.0            (signed integer)
//
// injectBug routes through hostScalarInjectBug() (the cook writes -999) so the DOWNSTREAM evalFloat reads
// the wrong value → RED on the actual cook path, NOT by flipping the expected value. --selftest-tryparse-bug
// must exit NON-zero.
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/eval_context.h"             // EvaluationContext
#include "runtime/graph.h"                     // Graph/Node/Connection/pinId + evalFloat
#include "runtime/graph_bridge.h"             // libFromGraph (flat Graph -> SymbolLibrary, paths == node ids)
#include "runtime/host_scalar_op_registry.h"  // hostScalarInjectBug
#include "runtime/point_graph.h"               // PointGraph::cook
#include "runtime/resident_eval_graph.h"       // buildEvalGraph / cookHostScalarNodes / evalResidentFloat

namespace sw {
namespace {

bool nearf(float a, float b) { return std::fabs(a - b) < 1e-4f; }

// Build: TryParse|TryParseInt(literal, Default) → Multiply(_, 1). The parse op is node id 1 (the cooked
// terminal), Multiply is id 3. The parsed scalar rides outCache; Multiply.a reads it via the bridge.
// Multiply by 1.0 so the downstream-read value EQUALS the parsed scalar (the bridge identity).
// Ports: TryParse [0]=Result(out), [1]=String, [2]=Default. Multiply [0]=a, [1]=b, [2]=out.
Graph makeParse(const char* type, const std::string& literal, float def) {
  Graph g;
  Node tp; tp.id = 1; tp.type = type;
  tp.strParams["String"] = literal;  // UNWIRED String input → literal const
  tp.params["Default"] = def;
  g.nodes.push_back(tp);
  Node mul; mul.id = 3; mul.type = "Multiply"; mul.params["b"] = 1.0f; g.nodes.push_back(mul);
  // TryParse.Result (port 0) → Multiply.a (port 0). The bridged wire.
  g.connections.push_back({201, pinId(1, /*Result*/ 0), pinId(3, /*a*/ 0)});
  return g;
}

// FLAT leg: cook the parse op as terminal (populates outCache) → evalFloat(Multiply.out).
float flatParse(PointGraph& pg, const char* type, const std::string& literal, float def) {
  Graph g = makeParse(type, literal, def);
  EvaluationContext ctx{};
  ctx.frameIndex = 0; ctx.time = 0.0f; ctx.deltaTime = 1.0f / 60.0f;
  pg.cook(g, ctx, nullptr, /*targetNodeId=*/1);  // writes outCache on the parse node
  return evalFloat(g, pinId(3, /*Multiply.out*/ 2), ctx);
}

// RESIDENT leg (production): cookHostScalarNodes writes Result onto extOut → evalResidentFloat(Multiply.out)
// reads the bridged parsed value. Multiply path "3", out slot "out".
float residentParse(const char* type, const std::string& literal, float def) {
  Graph g = makeParse(type, literal, def);
  SymbolLibrary lib = libFromGraph(g);
  ResidentEvalGraph rg = buildEvalGraph(lib, "Root");
  ResidentEvalCtx rc;
  rc.localTime = 0.0f; rc.localFxTime = 0.0f; rc.frameIndex = 0; rc.lib = &lib;
  cookHostScalarNodes(rg, rc);  // PRODUCTION pass: writes extOut on the parse node
  return evalResidentFloat(rg, /*Multiply path*/ "3", /*out slot*/ "out", rc);
}

struct Case {
  const char* type;
  const char* literal;
  float def;
  float want;
  const char* note;
};

}  // namespace

int runTryParseSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue* q = dev->newCommandQueue();
  PointGraph pg(dev, /*lib=*/nullptr, q, 64, 64);

  bool ok = true;

  const Case cases[] = {
      {"TryParse", "3.14", 0.0f, 3.14f, "decimal"},
      {"TryParse", "abc", 7.0f, 7.0f, "fail->Default"},
      {"TryParse", "", 2.0f, 2.0f, "empty->Default"},
      {"TryParse", "-12.5", 0.0f, -12.5f, "signed decimal"},
      {"TryParseInt", "42", 0.0f, 42.0f, "int"},
      {"TryParseInt", "1.5", 9.0f, 9.0f, "decimal rejected->Default"},
      {"TryParseInt", "-8", 0.0f, -8.0f, "signed int"},
  };

  for (const Case& c : cases) {
    // FLAT
    hostScalarInjectBug() = injectBug;
    float gotFlat = flatParse(pg, c.type, c.literal, c.def);
    hostScalarInjectBug() = false;
    bool passFlat = nearf(gotFlat, c.want);
    ok = ok && passFlat;
    std::printf("[selftest-tryparse] FLAT %s(\"%s\",def=%.2f) %s = %.3f want=%.3f -> %s\n", c.type,
                c.literal, c.def, c.note, gotFlat, c.want, passFlat ? "PASS" : "FAIL");

    // RESIDENT (production)
    hostScalarInjectBug() = injectBug;
    float gotRes = residentParse(c.type, c.literal, c.def);
    hostScalarInjectBug() = false;
    bool passRes = nearf(gotRes, c.want);
    ok = ok && passRes;
    std::printf("[selftest-tryparse] RESIDENT %s(\"%s\",def=%.2f) %s = %.3f want=%.3f -> %s\n", c.type,
                c.literal, c.def, c.note, gotRes, c.want, passRes ? "PASS" : "FAIL");
  }

  q->release();
  dev->release();
  pool->release();

  std::printf("[selftest-tryparse] %s\n", ok ? "PASS" : "FAIL");
  return ok ? 0 : 1;
}

}  // namespace sw

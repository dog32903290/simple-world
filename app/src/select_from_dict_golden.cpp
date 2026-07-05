// select_from_dict_golden — --selftest-selectfromdict. THE dict-currency bridge golden: a Dict<float>
// produced by BuildFloatDict flows across a "Dict" wire into a Select*FromDict consumer, whose looked-up
// scalar/vector is read DOWNSTREAM by evalFloat (the pure value recursion) — proving the BRIDGE, not just
// transport. Mirror of list_routing_golden (the FloatList→Float bridge) widened to the Dict rail.
//
// MECHANISM: the flat cook driver's host-scalar branch (point_graph_hostscalar_cook.cpp) gathers the Dict
// input (cookFlatDict cooks the upstream BuildFloatDict), runs the Select op, writes the component(s) into
// Node::outCache[0..2]; evalFloat's generalised host-scalar escape hatch (graph.cpp:186-190) reads them.
// The resident twin: cookHostScalarNodes gathers the Dict via cookResidentDict, writes extOut[0..2],
// evalResidentFloat reads them (the PRODUCTION leg — the refuter's must-have).
//
// EXPECTED VALUES — hand-derived from the TiXL .cs (impl-independent, GOLDEN_STANDARD three-features):
//   • SelectFloatFromDict.cs:22-23  — TryGetValue(select) → value; miss → 0 (Result default 0f, line 8).
//   • SelectBoolFromFloatDict.cs:22-23 — hit → (v > 0.5f); miss → false (default, line 9). bool→Float 0/1.
//   • SelectVec2FromDict.cs:24-30 + FindKeyForY (cs:64-85) — X=dict[selectX]; Y=dict[ key ORDINALLY-NEXT
//     after selectX in OrderBy(x=>x) ]. Both must exist, else (0,0) (default, line 8).
//   • SelectVec3FromDict.cs:33-57 + StringUtils.TryUpdateVectorKeysRelatedToX (cs:203-256) — ordinal-sort
//     keys, from selectX collect keys with CountDifferentChars(key,selectX)<=1 until 3; TryGetValue all 3.
//
// The probes use a NON-TRIVIAL dict (multi-key, a missing-key fallback, ordinal-order-dependent vector
// keys) so the op BODY is exercised (P2 guard: swap the lookup/sort for a wrong one → the golden reddens).
//
// TEETH (two real cook seams, NOT want-flip):
//   • dictInjectBug() — corrupts the PRODUCED dict (drops the last entry in BuildFloatDict) → the consumer
//     sees a missing key → the downstream read diverges. Bites the PRODUCE path.
//   • hostScalarInjectBug() — corrupts the CONSUMED scalar (Select writes -999) → downstream reads -999.
//     Bites the CONSUME path.
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/dict_op_registry.h"         // dictInjectBug
#include "runtime/eval_context.h"             // EvaluationContext
#include "runtime/graph.h"                    // Graph/Node/Connection/pinId + evalFloat
#include "runtime/graph_bridge.h"            // libFromGraph (flat Graph -> SymbolLibrary, paths == node ids)
#include "runtime/host_scalar_op_registry.h"  // hostScalarInjectBug
#include "runtime/point_graph.h"              // PointGraph::cook
#include "runtime/resident_eval_graph.h"     // buildEvalGraph / cookHostScalarNodes / evalResidentFloat

namespace sw {
namespace {

bool nearf(float a, float b) { return std::fabs(a - b) < 1e-5f; }

// Build BuildFloatDict(keysCsv, values) → <selectNode>(dict). selectNode id = 1 (the cooked terminal);
// BuildFloatDict id = 2; one Const per value (ids 10+). `selectType` = the Select op; `selectKeyId` = its
// String key port id ("Select" or "SelectX"); `selectKey` = the stored key. Returns the graph.
Graph makeSelectFromDict(const std::string& selectType, const std::string& selectKeyId,
                         const std::string& selectKey, const std::string& keysCsv,
                         const std::vector<float>& values, int dictPortIndex) {
  Graph g;
  Node sel; sel.id = 1; sel.type = selectType; sel.strParams[selectKeyId] = selectKey;
  g.nodes.push_back(sel);
  Node bd; bd.id = 2; bd.type = "BuildFloatDict"; bd.strParams["Keys"] = keysCsv;
  g.nodes.push_back(bd);

  // BuildFloatDict.Values (Float MultiInput, port index 1) ← one Const per value (wire-declaration order).
  const int valPin = pinId(2, /*Values*/ 1);
  int connId = 100;
  for (size_t i = 0; i < values.size(); ++i) {
    Node c; c.id = (int)(i + 10); c.type = "Const"; c.params["value"] = values[i];
    g.nodes.push_back(c);
    g.connections.push_back({connId++, pinId(c.id, /*out*/ 1), valPin});
  }
  // BuildFloatDict.out (last port) → Select.DictionaryInput (port `dictPortIndex`). BuildFloatDict ports:
  // [0]=Keys,[1]=Values,[2]=out → out is port index 2.
  g.connections.push_back({200, pinId(2, /*out*/ 2), pinId(1, dictPortIndex)});
  return g;
}

// FLAT leg: cook the Select node as terminal (populates its outCache), then evalFloat the Select output
// pin (component `comp`) — the bridge (outCache → value pull). Select output ports are FIRST (index=comp).
float flatSelect(PointGraph& pg, Graph& g, int comp) {
  EvaluationContext ctx{};
  ctx.frameIndex = 0; ctx.time = 0.0f; ctx.deltaTime = 1.0f / 60.0f;
  pg.cook(g, ctx, nullptr, /*targetNodeId=*/1);
  return evalFloat(g, pinId(1, comp), ctx);
}

// RESIDENT leg (production): mirror the flat Graph into a SymbolLibrary (paths == node ids), build the
// resident graph, run cookHostScalarNodes (the per-frame production pass), evalResidentFloat the Select
// node's output slot `outSlot`. The EXACT production path.
float residentSelect(Graph& g, const std::string& outSlot) {
  SymbolLibrary lib = libFromGraph(g);
  ResidentEvalGraph rg = buildEvalGraph(lib, "Root");
  ResidentEvalCtx rc;
  rc.localTime = 0.0f; rc.localFxTime = 0.0f; rc.frameIndex = 0; rc.lib = &lib;
  cookHostScalarNodes(rg, rc);
  return evalResidentFloat(rg, /*Select path*/ "1", outSlot, rc);
}

}  // namespace

int runSelectFromDictSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue* q = dev->newCommandQueue();
  PointGraph pg(dev, /*lib=*/nullptr, q, 64, 64);
  bool ok = true;

  // The shared NON-TRIVIAL dict. Keys are DELIBERATELY NOT in ordinal order in the CSV (posX after gain)
  // so the Vec2/Vec3 consumers' internal ordinal SORT is exercised (P2): insertion order != sorted order.
  //   entries (insertion order): gain=0.8, posX=1.0, posY=2.0, posZ=3.0, w=9.0
  //   ordinal-sorted keys:       gain, posX, posY, posZ, w
  // NOTE the trailing "w" (sorts AFTER posZ): TiXL's TryUpdateVectorKeysRelatedToX (StringUtils.cs:231,242)
  // returns `count < 0`, which requires the loop to visit a key AFTER the triple to decrement count from 0
  // to -1 (else break). Without a key after posZ the op returns FALSE even for a valid triple — a real TiXL
  // edge, faithfully reproduced. "w" (CountDifferentChars("w","posX")=MAX>1) trips the else-break → true.
  const std::string keys = "gain,posX,posY,posZ,w";
  const std::vector<float> vals = {0.8f, 1.0f, 2.0f, 3.0f, 9.0f};

  // TEETH POLARITY (GOLDEN_STANDARD three-features #3): every assert compares against the FIXED TiXL-derived
  // value (NEVER injectBug-flipped — that would be P3 want-flip). injectBug corrupts the REAL cook (Select
  // writes -999 / BuildFloatDict drops an entry), so under -bug `got` diverges from the fixed want → the
  // leg FAILS → ok=false → the harness's -bug variant exits non-zero. The hostScalar teeth ride the CONSUME
  // legs; the dict-produce teeth ride the dedicated PRODUCE leg (LEG 2b).

  // ── LEG 1 — SelectFloatFromDict (the headline): key "posX" → 1.0 (SelectFloatFromDict.cs:23). ──
  {
    hostScalarInjectBug() = injectBug; dictInjectBug() = false;
    Graph g = makeSelectFromDict("SelectFloatFromDict", "Select", "posX", keys, vals, /*Dict port*/ 1);
    float got = flatSelect(pg, g, /*Result*/ 0);
    hostScalarInjectBug() = false;
    bool pass = nearf(got, 1.0f);  // FIXED want (P3-safe); -bug → -999 ≠ 1 → RED
    ok = ok && pass;
    std::printf("[selftest-selectfromdict] FLAT SelectFloat(posX)=%.3f want=1.000 -> %s\n",
                got, pass ? "PASS" : "FAIL");
  }
  // Missing-key fallback → 0 (SelectFloatFromDict.cs:22-23 TryGetValue miss keeps default 0). No teeth here
  // (a bug that returned -999 for a missing key would already be caught by LEG 1); pure boundary, non-bug.
  if (!injectBug) {
    Graph g = makeSelectFromDict("SelectFloatFromDict", "Select", "nope", keys, vals, 1);
    float got = flatSelect(pg, g, 0);
    bool pass = nearf(got, 0.0f);
    ok = ok && pass;
    std::printf("[selftest-selectfromdict] FLAT SelectFloat(missing)=%.3f want=0.000 -> %s\n",
                got, pass ? "PASS" : "FAIL");
  }
  // RESIDENT leg (production): key "posY" → 2.0. hostScalar teeth carry through the production pass.
  {
    hostScalarInjectBug() = injectBug; dictInjectBug() = false;
    Graph g = makeSelectFromDict("SelectFloatFromDict", "Select", "posY", keys, vals, 1);
    float got = residentSelect(g, "Result");
    hostScalarInjectBug() = false;
    bool pass = nearf(got, 2.0f);  // FIXED want; -bug → -999 → RED
    ok = ok && pass;
    std::printf("[selftest-selectfromdict] RESIDENT SelectFloat(posY)=%.3f want=2.000 -> %s\n",
                got, pass ? "PASS" : "FAIL");
  }

  // ── LEG 2 — SelectBoolFromFloatDict: gain=0.8 > 0.5 → true → 1.0. hostScalar teeth. ──
  {
    hostScalarInjectBug() = injectBug; dictInjectBug() = false;
    Graph g = makeSelectFromDict("SelectBoolFromFloatDict", "Select", "gain", keys, vals, 1);
    float got = flatSelect(pg, g, 0);
    hostScalarInjectBug() = false;
    bool pass = nearf(got, 1.0f);  // 0.8 > 0.5 (SelectBoolFromFloatDict.cs:23); -bug → -999 → RED
    ok = ok && pass;
    std::printf("[selftest-selectfromdict] FLAT SelectBool(gain=0.8)=%.3f want=1.000 -> %s\n",
                got, pass ? "PASS" : "FAIL");
  }
  // Below-threshold BOUNDARY: value <= 0.5 → false → 0.0 (dedicated dict {lo=0.2}). Pure boundary, non-bug.
  if (!injectBug) {
    Graph g = makeSelectFromDict("SelectBoolFromFloatDict", "Select", "lo", "lo", {0.2f}, 1);
    float got = flatSelect(pg, g, 0);
    bool pass = nearf(got, 0.0f);  // 0.2 > 0.5 is false
    ok = ok && pass;
    std::printf("[selftest-selectfromdict] FLAT SelectBool(lo=0.2)=%.3f want=0.000 -> %s\n",
                got, pass ? "PASS" : "FAIL");
  }
  // ── LEG 2b — DICT-PRODUCE teeth (the other real seam): dictInjectBug drops the LAST produced entry. Dict
  //    {a=0.9} select "a": 0.9>0.5=true → 1.0. Under -bug BuildFloatDict drops "a" → consumer misses → false
  //    → 0.0 ≠ 1.0 → RED. Bites the PRODUCE path (not the consume path). FIXED want 1.0 (P3-safe). ──
  {
    dictInjectBug() = injectBug; hostScalarInjectBug() = false;
    Graph g = makeSelectFromDict("SelectBoolFromFloatDict", "Select", "a", "a", {0.9f}, 1);
    float got = flatSelect(pg, g, 0);
    dictInjectBug() = false;
    bool pass = nearf(got, 1.0f);  // FIXED want; -bug drops the produced entry → miss → 0.0 → RED
    ok = ok && pass;
    std::printf("[selftest-selectfromdict] FLAT SelectBool(a=0.9, produce-teeth)=%.3f want=1.000 -> %s\n",
                got, pass ? "PASS" : "FAIL");
  }

  // ── LEG 3 — SelectVec2FromDict: SelectX="posX". Sorted keys gain,posX,posY,posZ,w. FindKeyForY finds the
  //    key AFTER posX = posY (SelectVec2FromDict.cs:64-85) → Result=(posX,posY)=(1.0,2.0). hostScalar teeth. ──
  {
    hostScalarInjectBug() = injectBug; dictInjectBug() = false;
    Graph g = makeSelectFromDict("SelectVec2FromDict", "SelectX", "posX", keys, vals, /*Dict port*/ 2);
    float rx = flatSelect(pg, g, /*Result.x*/ 0);
    Graph g2 = makeSelectFromDict("SelectVec2FromDict", "SelectX", "posX", keys, vals, 2);
    float ry = flatSelect(pg, g2, /*Result.y*/ 1);
    hostScalarInjectBug() = false;
    bool pass = nearf(rx, 1.0f) && nearf(ry, 2.0f);  // FIXED want; -bug → (-999,-999) → RED
    ok = ok && pass;
    std::printf("[selftest-selectfromdict] FLAT SelectVec2(posX)=(%.3f,%.3f) want=(1.000,2.000) -> %s\n",
                rx, ry, pass ? "PASS" : "FAIL");
  }
  // RESIDENT Vec2 (production): Result.y = 2.0. hostScalar teeth carry through.
  {
    hostScalarInjectBug() = injectBug; dictInjectBug() = false;
    Graph g = makeSelectFromDict("SelectVec2FromDict", "SelectX", "posX", keys, vals, 2);
    float ry = residentSelect(g, "Result.y");
    hostScalarInjectBug() = false;
    bool pass = nearf(ry, 2.0f);  // FIXED want; -bug → -999 → RED
    ok = ok && pass;
    std::printf("[selftest-selectfromdict] RESIDENT SelectVec2(posX).y=%.3f want=2.000 -> %s\n",
                ry, pass ? "PASS" : "FAIL");
  }

  // ── LEG 4 — SelectVec3FromDict: SelectX="posX". TryUpdateVectorKeysRelatedToX ordinal-sorts, from posX
  //    collects posX,posY,posZ (each CountDifferentChars(.,posX)<=1, "w" trips the break) → (1.0,2.0,3.0).
  //    "gain" precedes posX in sort so is skipped; if the sort/adjacency logic is wrong the triple breaks
  //    (P2 — swap ordinal for a wrong order and the golden reddens). hostScalar teeth. ──
  {
    hostScalarInjectBug() = injectBug; dictInjectBug() = false;
    Graph gx = makeSelectFromDict("SelectVec3FromDict", "SelectX", "posX", keys, vals, /*Dict port*/ 3);
    float rx = flatSelect(pg, gx, 0);
    Graph gy = makeSelectFromDict("SelectVec3FromDict", "SelectX", "posX", keys, vals, 3);
    float ry = flatSelect(pg, gy, 1);
    Graph gz = makeSelectFromDict("SelectVec3FromDict", "SelectX", "posX", keys, vals, 3);
    float rz = flatSelect(pg, gz, 2);
    hostScalarInjectBug() = false;
    bool pass = nearf(rx, 1.0f) && nearf(ry, 2.0f) && nearf(rz, 3.0f);  // FIXED want; -bug → (-999…) → RED
    ok = ok && pass;
    std::printf("[selftest-selectfromdict] FLAT SelectVec3(posX)=(%.3f,%.3f,%.3f) want=(1.000,2.000,3.000) -> %s\n",
                rx, ry, rz, pass ? "PASS" : "FAIL");
  }
  // RESIDENT Vec3 (production): Result.z = 3.0. hostScalar teeth carry through.
  {
    hostScalarInjectBug() = injectBug; dictInjectBug() = false;
    Graph g = makeSelectFromDict("SelectVec3FromDict", "SelectX", "posX", keys, vals, 3);
    float rz = residentSelect(g, "Result.z");
    hostScalarInjectBug() = false;
    bool pass = nearf(rz, 3.0f);  // FIXED want; -bug → -999 → RED
    ok = ok && pass;
    std::printf("[selftest-selectfromdict] RESIDENT SelectVec3(posX).z=%.3f want=3.000 -> %s\n",
                rz, pass ? "PASS" : "FAIL");
  }

  q->release();
  dev->release();
  pool->release();

  // Harness convention (run_all_selftests.sh --bite): the -bug variant MUST exit non-zero. injectBug
  // corrupts the REAL cooked value (consume seam) / produced dict (produce seam) → downstream diverges.
  std::printf("[selftest-selectfromdict] %s\n", ok ? "PASS" : "FAIL");
  return ok ? 0 : 1;
}

}  // namespace sw

// matrixctxvar_golden — --selftest-matrixctxvar. R-2 PRODUCTION golden for the MATRIX context-var seam
// (sub-seam D): SetMatrixVar + GetMatrixVar on the TYPED matrixVars channel, driven through the resident
// path (libFromGraph → buildEvalGraph → cook_host_values' cookMatrixCtxVarNodes). The matrix twin of
// stringctxvar_golden (String channel) — but on the MATRIX currency: a 4-row Vector4[] rides the
// extColorOut vec4 channel (the SAME matrix-as-4-vec4 rail TransformMatrix uses), NOT extStrOut.
//
// TiXL authority: external/tixl/Operators/Lib/flow/context/GetMatrixVar.cs
// TiXL authority: external/tixl/Operators/Lib/flow/context/SetMatrixVar.cs
//
// Legs (each an EXACT closed-form matrix, no behaviour-band):
//   A roundtrip — a ColorsToList producing a KNOWN 4-row matrix M feeds SetMatrixVar("m").Value; a
//                 GetMatrixVar("m") reads it back → Result == M, byte-identical (GetMatrixVar.cs:31
//                 Result = vector4Array; SetMatrixVar.cs:47 ObjectVariables[name]=newValue). PROBE is
//                 a NON-identity matrix (rows carry distinct non-0/non-1 values) → a swizzle/row-order
//                 bug in the channel round-trip diverges. injectBug (=1) severs SetMatrixVar's map write
//                 → GetMatrixVar reads the identity fallback → RED vs M.
//   A echo      — SetMatrixVar.Output echoes the written M (NAMED FORK — the Command has no value; the
//                 echo is the golden probe + makes SetMatrixVar a matrix producer on extColorOut).
//   B miss      — GetMatrixVar("unset") on a name never written → the 4-row identity (GetMatrixVar.cs:42
//                 _identity). Independent of injectBug ("unset" is never a Set target).
//   C ordering  — a GetMatrixVar declared BEFORE its SetMatrixVar writer in g.nodes STILL reads the set
//                 value (writer-first PASS 1 runs all Sets before PASS 2's Gets). Proves the 2-pass split.
//
// injectBug routes through setMatrixCtxVarBug() so the RED case corrupts the REAL cook path:
//   mode 1 (A/C teeth) = SetMatrixVar severs the matrixVars write → the reader falls to identity.
#include <array>
#include <cmath>
#include <cstdio>
#include <vector>

#include <simd/simd.h>

#include "runtime/compound_graph.h"       // SymbolLibrary
#include "runtime/graph.h"                 // Graph / Node / Connection / pinId
#include "runtime/graph_bridge.h"          // libFromGraph (flat Graph → SymbolLibrary, paths == ids)
#include "runtime/resident_eval_graph.h"   // buildEvalGraph / initResidentCache / cookMatrixCtxVarNodes
#include "runtime/stateful_value_ops.h"    // ContextVarMap (matrixVars channel)

namespace sw {
namespace {

int g_fail = 0;
void expect(const char* what, bool ok) {
  if (!ok) { ++g_fail; std::printf("  [matrixctxvar] FAIL %s\n", what); }
  else std::printf("  [matrixctxvar] ok   %s\n", what);
}

bool near4(simd::float4 a, simd::float4 b) {
  const float e = 1e-5f;
  return std::fabs(a.x - b.x) < e && std::fabs(a.y - b.y) < e && std::fabs(a.z - b.z) < e &&
         std::fabs(a.w - b.w) < e;
}

// The KNOWN non-identity probe matrix M (4 rows, each with distinct non-0/non-1 values so a row-order or
// x/y/z/w swizzle bug in the channel round-trip diverges). Row-major.
const std::array<simd::float4, 4>& probeMatrix() {
  static const std::array<simd::float4, 4> M = {
      simd::make_float4(2.0f, 0.5f, 1.0f, 3.0f),
      simd::make_float4(0.25f, 4.0f, 6.0f, 0.5f),
      simd::make_float4(7.0f, 8.0f, 9.0f, 1.5f),
      simd::make_float4(0.1f, 0.2f, 0.3f, 1.0f)};
  return M;
}

const std::array<simd::float4, 4>& identityMatrix() {
  static const std::array<simd::float4, 4> I = {
      simd::make_float4(1, 0, 0, 0), simd::make_float4(0, 1, 0, 0), simd::make_float4(0, 0, 1, 0),
      simd::make_float4(0, 0, 0, 1)};
  return I;
}

Node makeNode(int id, const char* type) { Node n; n.id = id; n.type = type; return n; }

// Wire a ColorsToList producer (node id `ctlId`) that outputs the 4 rows of `M` in wire order (one Const
// per (row,channel), fed into ColorsToList's 4 PARALLEL Float MultiInput component ports Colors.x/.y/.z/.w
// — the SAME driver colorlist_golden uses). Const nodes start at `nextNode` (bumped), connections at
// `connId` (bumped). ColorsToList ports: [0]=Colors.x,[1]=.y,[2]=.z,[3]=.w,[4]=out(ColorList). Const out=1.
void addColorsToList(Graph& g, int ctlId, const std::array<simd::float4, 4>& M, int& nextNode, int& connId) {
  g.nodes.push_back(makeNode(ctlId, "ColorsToList"));
  const int chanPin[4] = {pinId(ctlId, 0), pinId(ctlId, 1), pinId(ctlId, 2), pinId(ctlId, 3)};
  for (int r = 0; r < 4; ++r) {
    const float comp[4] = {M[r].x, M[r].y, M[r].z, M[r].w};
    for (int k = 0; k < 4; ++k) {
      Node c = makeNode(nextNode++, "Const");
      c.params["value"] = comp[k];
      g.nodes.push_back(c);
      g.connections.push_back({connId++, pinId(c.id, /*out*/ 1), chanPin[k]});
    }
  }
}

ResidentEvalGraph buildResident(const Graph& g, SymbolLibrary& outLib) {
  outLib = libFromGraph(g);
  ResidentEvalGraph rg = buildEvalGraph(outLib, "Root");
  initResidentCache(rg);
  return rg;
}

// Drive ONE production frame of the matrix ctx-var pass: clear matrixVars (the pass-0 Reset analog), then
// cookMatrixCtxVarNodes (the writer-first 2-pass: all SetMatrixVar, then all GetMatrixVar). One map,
// cleared each frame (the production contract).
void cookFrame(SymbolLibrary& lib, ResidentEvalGraph& g, ContextVarMap& vars, uint32_t frame) {
  vars.matrixVars.clear();  // = frame_cook pass-0 Reset for the matrix channel
  ResidentEvalCtx rc;
  rc.localTime = 0.0f; rc.localFxTime = 0.0f; rc.frameIndex = frame; rc.lib = &lib;
  cookMatrixCtxVarNodes(g, rc, &vars);
}

// Read a matrix op's cooked 4 rows off extColorOut[Result/Output port idx 0]. Empty on a structural miss.
std::vector<simd::float4> extMat(const ResidentEvalGraph& g, const char* path) {
  const ResidentNode* n = g.node(path);
  if (!n) return {};
  auto it = n->extColorOut.find(0);
  return it != n->extColorOut.end() ? it->second : std::vector<simd::float4>{};
}

bool matEq(const std::vector<simd::float4>& got, const std::array<simd::float4, 4>& want) {
  if (got.size() != 4) return false;
  for (int i = 0; i < 4; ++i)
    if (!near4(got[i], want[i])) return false;
  return true;
}

}  // namespace

int runMatrixCtxVarSelfTest(bool injectBug) {
  g_fail = 0;
  std::printf("[selftest] matrixctxvar (MATRIX ctx-var seam sub-seam D: typed matrixVars + writer-first 2-pass)\n");
  const std::array<simd::float4, 4>& M = probeMatrix();
  const std::array<simd::float4, 4>& I = identityMatrix();

  // ===== A roundtrip + A echo + B miss (one graph). =========================================
  // 1=SetMatrixVar("m") ← Value from ColorsToList(id 10, rows=M); 2=GetMatrixVar("m"); 3=GetMatrixVar("unset").
  {
    Graph g;
    Node sm = makeNode(1, "SetMatrixVar");
    sm.strParams["VariableName"] = "m";
    g.nodes.push_back(sm);
    Node gm = makeNode(2, "GetMatrixVar"); gm.strParams["VariableName"] = "m"; g.nodes.push_back(gm);
    Node gu = makeNode(3, "GetMatrixVar"); gu.strParams["VariableName"] = "unset"; g.nodes.push_back(gu);

    int nextNode = 100, connId = 1000;
    addColorsToList(g, /*ctlId=*/10, M, nextNode, connId);
    // Wire ColorsToList(10).out (port idx 4) → SetMatrixVar(1).Value (port idx 2).
    g.connections.push_back({connId++, pinId(10, /*out*/ 4), pinId(1, /*Value*/ 2)});
    g.nextId = nextNode;

    if (injectBug) setMatrixCtxVarBug(1);  // TOOTH: SetMatrixVar severs the matrixVars write
    SymbolLibrary lib; ResidentEvalGraph rg = buildResident(g, lib);
    ContextVarMap vars;
    cookFrame(lib, rg, vars, 0);
    setMatrixCtxVarBug(0);

    // A: GetMatrixVar("m") == M (writer→matrixVars→reader→extColorOut). injectBug severs the write →
    //    GetMatrixVar falls to identity → ≠ M → FAIL.
    expect("A roundtrip: GetMatrixVar(\"m\")==M (SetMatrixVar→matrixVars→GetMatrixVar→extColorOut)",
           matEq(extMat(rg, "2"), M));
    // A echo: SetMatrixVar.Output echoes the written M (NAMED FORK — golden probe). This ECHOES the
    //    gathered Value even when the map write is severed, so it stays == M under injectBug (proves the
    //    echo is the Value, not the map read).
    expect("A echo: SetMatrixVar.Output echoes M", matEq(extMat(rg, "1"), M));
    // B: unset name → identity fallback (GetMatrixVar.cs:42). Independent of injectBug.
    expect("B miss: GetMatrixVar(\"unset\")==identity (TryGetValue miss → _identity)",
           matEq(extMat(rg, "3"), I));
  }

  // ===== C ordering: GetMatrixVar declared BEFORE its SetMatrixVar writer. =====================
  // 1=GetMatrixVar("m"); 2=SetMatrixVar("m") ← Value from ColorsToList(11, rows=M). The Get is FIRST in
  // g.nodes; the writer-first 2-pass still runs the Set (PASS 1) before the Get (PASS 2) → the Get reads M.
  // injectBug severs the Set write → the Get falls to identity → ≠ M → FAIL.
  {
    Graph g;
    Node gm = makeNode(1, "GetMatrixVar"); gm.strParams["VariableName"] = "m"; g.nodes.push_back(gm);
    Node sm = makeNode(2, "SetMatrixVar"); sm.strParams["VariableName"] = "m"; g.nodes.push_back(sm);
    int nextNode = 100, connId = 1000;
    addColorsToList(g, /*ctlId=*/11, M, nextNode, connId);
    g.connections.push_back({connId++, pinId(11, /*out*/ 4), pinId(2, /*Value*/ 2)});
    g.nextId = nextNode;

    if (injectBug) setMatrixCtxVarBug(1);
    SymbolLibrary lib; ResidentEvalGraph rg = buildResident(g, lib);
    ContextVarMap vars;
    cookFrame(lib, rg, vars, 0);
    setMatrixCtxVarBug(0);

    // Premise: the Get is declared before the Set in g.nodes (out-of-order).
    expect("C premise: GetMatrixVar(idx 0) < SetMatrixVar(idx 1) in g.nodes (out-of-order declaration)",
           rg.nodes.size() >= 2 && rg.nodes[0].opType == "GetMatrixVar" && rg.nodes[1].opType == "SetMatrixVar");
    // C: the out-of-order Get STILL reads M (writer-first PASS 1 before reader PASS 2).
    expect("C ordering: out-of-order GetMatrixVar(\"m\")==M (writer-first 2-pass load-bearing)",
           matEq(extMat(rg, "1"), M));
  }

  std::printf("[selftest] matrixctxvar %s (%d fail)\n", g_fail == 0 ? "PASS" : "FAIL", g_fail);
  // Harness (run_all_selftests.sh --bite): the -bug variant must exit NON-zero. injectBug severs the REAL
  // SetMatrixVar write → A + C readers fall to identity → g_fail > 0 → return 1 (the tooth bites). If the
  // injection DID NOT trip (g_fail==0 under injectBug), return 0 so --bite's NO-BITE list catches it
  // (GOLDEN_STANDARD: did-not-trip → return 0, never a false green exit).
  return g_fail == 0 ? 0 : 1;
}

}  // namespace sw

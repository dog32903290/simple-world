// runtime/resident_matrix_ctxvar_cook — the MATRIX-channel context-var seam (sub-seam D): SetMatrixVar +
// GetMatrixVar. The matrix twin of the vec3/string ctx-var ops. Both ride the MATRIX output rail
// (extColorOut, 4×float4 = TiXL Slot<Vector4[]>, the SAME channel resident_matrix_output_cook uses for
// TransformMatrix) and the TYPED matrixVars channel on the host ContextVarMap (NOT TiXL's boxed
// ObjectVariables dict — the SAME typed-channel fork sw took for vec3/string).
//
// TiXL authority: external/tixl/Operators/Lib/flow/context/GetMatrixVar.cs
// TiXL authority: external/tixl/Operators/Lib/flow/context/SetMatrixVar.cs
//
// TiXL ground truth (read-only external/tixl):
//   GetMatrixVar.cs:25-44 — reads context.ObjectVariables[VariableName]; on a hit that `is Vector4[]`
//     → Result = that array; on a miss OR a wrong-type value → Result = _identity (the 4-row identity,
//     GetMatrixVar.cs:91-97). DROP ICustomDropdownHolder + IStatusProvider (editor-only telemetry).
//   SetMatrixVar.cs:15-49 — empty name → no-op (cs:22-25, string.IsNullOrEmpty). The no-SubGraph branch
//     (cs:45-48): `if(!clearAfterExecution) context.ObjectVariables[name] = newValue`.
//   .t3 DEFAULT AUDIT (the .t3 DefaultValue OVERRIDES the C# ctor — the load-bearing default):
//     GetMatrixVar.t3: VariableName default "m".  SetMatrixVar.t3: VariableName default "m",
//     ClearAfterExecution default false, SubGraph null, Value null. → NodeSpec strDef for VariableName = "m".
//
// NAMED FORK (fork-ctxvar-matrix-typed-channel): TiXL stores/reads the Vector4[] in the SHARED
//   context.ObjectVariables boxed-object dict (with an `is Vector4[]` cast). sw stores it on a TYPED
//   std::array<float,16> channel (matrixVars), the SAME choice it made for vec3/string over the boxed
//   dict — can't collide with a float/int/vec3/string of the same name; the round-trip value is
//   byte-identical (16 floats = 4 rows × 4). Recorded next to fork-ctxvar-vec3-typed-channel.
//
// NAMED FORK (fork-setmatrixvar-subgraph-command-rail): SetMatrixVar's SubGraph(Command) push/restore
//   scope (SetMatrixVar.cs:27-44, the `if (SubGraph.HasInputConnections)` branch) + ClearAfterExecution
//   are the SAME Command-rail scoping as SetFloatVar/SetStringVar — NOT ported here (sw's two-rail model
//   can't put a matrix echo output AND a Command SubGraph output on one node-spec; precedent:
//   string_ops_stringctxvar.cpp SetStringVar, point_ops_setvarcmd.cpp SetFloatVarCmd). THIS leaf is the
//   no-SubGraph, no-clear branch only (cs:45-48 — the flat map write). A future "SetMatrixVarCmd" Command
//   type carries SubGraph + ClearAfterExecution. Behaviour-faithful, spelling-forked. Fork count = 2.
//
// NAMED FORK (fork-setmatrixvar-echo-output): TiXL's SetMatrixVar.Output is a Slot<Command> (no value-rail
//   analog). sw gives it a ColorList (4-row matrix) echo Output (the written matrix) — the SAME
//   echo-as-golden-probe fork SetStringVar/SetFloatVar took. The real product is the map mutation; the
//   echo is the readable golden probe (and makes SetMatrixVar a matrix producer on extColorOut).
//
// PLACEMENT: runtime leaf (pure CPU; depends only on resident_eval_graph.h + graph.h + stateful_value_ops.h
//   for ContextVarMap). Called from app/cook_host_values.cpp once per frame (writer-first), same slot
//   family as cookMatrixOutputNodes / cookColorListNodes.
#include "runtime/resident_eval_graph.h"

#include <array>
#include <map>
#include <string>
#include <vector>

#include <simd/simd.h>

#include "runtime/graph.h"               // NodeSpec / PortSpec / findSpec
#include "runtime/stateful_value_ops.h"  // ContextVarMap (matrixVars channel)

namespace sw {

namespace {

// The 4-row identity matrix (GetMatrixVar.cs:91-97 _identity). Rows: (1,0,0,0)(0,1,0,0)(0,0,1,0)(0,0,0,1).
std::array<simd::float4, 4> identityRows() {
  return {simd::make_float4(1, 0, 0, 0), simd::make_float4(0, 1, 0, 0), simd::make_float4(0, 0, 1, 0),
          simd::make_float4(0, 0, 0, 1)};
}

// Pack 4 float4 rows -> flat 16 (row-major) for the typed matrixVars channel.
std::array<float, 16> pack(const std::array<simd::float4, 4>& r) {
  std::array<float, 16> m{};
  for (int i = 0; i < 4; ++i) {
    m[i * 4 + 0] = r[i].x; m[i * 4 + 1] = r[i].y; m[i * 4 + 2] = r[i].z; m[i * 4 + 3] = r[i].w;
  }
  return m;
}

// Unpack flat 16 -> 4 float4 rows.
std::array<simd::float4, 4> unpack(const std::array<float, 16>& m) {
  std::array<simd::float4, 4> r{};
  for (int i = 0; i < 4; ++i)
    r[i] = simd::make_float4(m[i * 4 + 0], m[i * 4 + 1], m[i * 4 + 2], m[i * 4 + 3]);
  return r;
}

// The resolved String VariableName of a resident node (strInputs["VariableName"]; empty if absent). Same
// spine cookStatefulValueNodes uses for the float/vec3 ctx-var ops (frame_cook.cpp:239-241).
const std::string& varNameOf(const ResidentNode& rn) {
  static const std::string kEmpty;
  auto it = rn.strInputs.find("VariableName");
  return it != rn.strInputs.end() ? it->second : kEmpty;
}

// The port index of the node's MAIN ColorList output (Result / Output). -1 if none.
int mainColorOutPort(const NodeSpec* s) {
  for (size_t i = 0; i < s->ports.size(); ++i)
    if (!s->ports[i].isInput && s->ports[i].dataType == "ColorList") return (int)i;
  return -1;
}

// injectBug teeth (sub-seam D): corrupt the REAL cook path so a fixed expected bites.
//   1 = SetMatrixVar SEVERS the map write (matrixVars never populated → GetMatrixVar reads identity).
//   2 = GetMatrixVar corrupts the read (drops the last row's W → 0 → the golden's W=1 assert goes RED).
int g_matrixCtxVarBug = 0;

}  // namespace

// PUBLIC teeth hook (--selftest-matrixctxvar). Sticky module switch; the golden restores 0 after each bug run.
void setMatrixCtxVarBug(int mode) { g_matrixCtxVarBug = mode; }

// The MATRIX context-var cook pass. WRITER-FIRST (mirror of the string ctx-var 2-pass): every SetMatrixVar
// writes matrixVars BEFORE any GetMatrixVar reads it, deterministically every frame (sw iterates g.nodes in
// build order, not dataflow → ordering imposed explicitly). `vars` = the per-frame host ContextVarMap
// (frame_cook's s_ctxVars; matrixVars already cleared this frame in the pass-0 Reset). nullptr = a caller
// with no ctx-var graph → the pass is a no-op (matrixVars unreachable).
void cookMatrixCtxVarNodes(ResidentEvalGraph& g, const ResidentEvalCtx& ctx, ContextVarMap* vars) {
  if (!vars) return;

  // PASS 1 — WRITERS (SetMatrixVar): gather the ColorList Value input (4 rows) through the resident graph,
  // write matrixVars[name] = those 16 floats, and echo the 4 rows onto extColorOut (the golden probe).
  for (ResidentNode& rn : g.nodes) {
    if (rn.opType != "SetMatrixVar") continue;
    const NodeSpec* s = findSpec(rn.opType);
    if (!s) continue;

    // Gather the "Value" ColorList input (its upstream producer's 4 rows). Empty/unwired → identity
    // (a bare SetMatrixVar with no Value wire writes the identity matrix — faithful to a null Value).
    std::array<simd::float4, 4> rows = identityRows();
    const ResidentInput* ri = rn.input("Value");
    if (ri && ri->driver == ResidentInput::Driver::Connection) {
      std::vector<simd::float4> up;
      if (cookResidentColorList(g, ri->srcNodePath, ctx, up, 0, /*state=*/nullptr) && up.size() >= 4) {
        for (int i = 0; i < 4; ++i) rows[i] = up[i];
      }
    }

    const std::string& name = varNameOf(rn);
    // echo (fork-setmatrixvar-echo-output — the Command has no value; this is the golden probe)
    int outIdx = mainColorOutPort(s);
    if (outIdx >= 0) rn.extColorOut[outIdx] = {rows[0], rows[1], rows[2], rows[3]};

    if (name.empty()) continue;                 // string.IsNullOrEmpty(name) → no-op (SetMatrixVar.cs:22-25)
    if (g_matrixCtxVarBug == 1) continue;        // TOOTH 1: sever the map write (map stays empty)
    vars->matrixVars[name] = pack(rows);         // no-SubGraph branch: context.ObjectVariables[name]=Vector4[]
  }

  // PASS 2 — READERS (GetMatrixVar): read matrixVars[name] (else identity — the miss/wrong-type fallback,
  // GetMatrixVar.cs:42/43), emit the 4 rows onto extColorOut.
  for (ResidentNode& rn : g.nodes) {
    if (rn.opType != "GetMatrixVar") continue;
    const NodeSpec* s = findSpec(rn.opType);
    if (!s) continue;
    int outIdx = mainColorOutPort(s);
    if (outIdx < 0) continue;

    std::array<simd::float4, 4> rows = identityRows();  // GetMatrixVar.cs:42 _identity on a miss
    const std::string& name = varNameOf(rn);
    if (!name.empty()) {
      auto it = vars->matrixVars.find(name);
      if (it != vars->matrixVars.end()) rows = unpack(it->second);  // TryGetValue hit (cs:27-31)
    }
    if (g_matrixCtxVarBug == 2) rows[3].w = 0.0f;  // TOOTH 2: corrupt the read (drop the identity/last-row W)
    rn.extColorOut[outIdx] = {rows[0], rows[1], rows[2], rows[3]};
  }
}

}  // namespace sw

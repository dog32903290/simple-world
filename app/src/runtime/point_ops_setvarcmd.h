// runtime/point_ops_setvarcmd — S3a context-var bridge: the Command-rail SetFloatVarCmd / SetIntVarCmd
// push/restore helper, shared by the flat (point_graph.cpp) and resident (point_graph_resident.cpp) cook
// drivers so the SubGraph-scoped var write is mirrored on BOTH legs (the S2b/S2c blood lesson — one
// forgotten leg = a prod-only black-hole; production runs the RESIDENT leg).
//
// Declared in its OWN tiny header (NOT the at-cap point_ops.h, which is on the line-count ratchet) so the
// two cook drivers can call the push/restore without pulling the god-header. The bodies live in
// point_ops_setvarcmd.cpp (it includes stateful_value_ops.h for the complete ContextVarMap type).
//
// Faithful to TiXL flow/context/SetFloatVar.cs:26-45 + SetIntVar.cs:38-64 (the SubGraph branch):
//   hadPrev = TryGetValue(name, out prev);  vars[name] = newValue;  <cook SubGraph>;
//   if (hadPrev) vars[name] = prev;  else if (!clearAfterExecution) vars.Remove(name);
// The driver cooks the SubGraph between cmdVarPush and cmdVarRestore (exactly like S1's requestedResolution
// save/mutate/cook/restore), so this header carries only the save/mutate (push) and restore halves.
#pragma once
#include <map>
#include <string>

namespace sw {

struct ContextVarMap;  // stateful_value_ops.h (complete type only in the .cpp)

// True iff opType is a Command-rail context-var WRITER (SetFloatVarCmd / SetIntVarCmd). The driver's
// Command branch consults this to decide whether to push a scoped var around the node's SubGraph.
bool isCmdContextVarWriter(const std::string& opType);

// The saved state cmdVarPush returns, fed back into cmdVarRestore (the TiXL hadPrev/previous capture).
struct CmdVarScope {
  bool active = false;       // false => not a writer / no map => restore is a no-op
  bool isInt = false;        // which dict (SetIntVar -> intVars, else floatVars)
  std::string name;          // the resolved VariableName (empty name => no-op, TiXL string.IsNullOrEmpty)
  bool hadPrev = false;      // TiXL: context.*Variables.TryGetValue(name, out previous)
  float prevF = 0.0f;        // previous float value (when hadPrev && !isInt)
  long  prevI = 0;           // previous int value (when hadPrev && isInt)
  bool clearAfter = false;   // TiXL ClearAfterExecution (keep the var set after the subtree if no prev)
};

// PUSH (TiXL SetFloatVar.cs:28-29): capture hadPrev/previous, then write the new value into the matching
// dict. `params` = the node's RESOLVED Float params (FloatValue/Value + ClearAfterExecution); `varName` =
// the resolved String VariableName (flat Node::strParams / resident ResidentNode::strInputs). No-op (returns
// an inactive scope) when opType is not a writer, vars is null, or varName is empty. Call BEFORE cooking the
// SubGraph.
CmdVarScope cmdVarPush(const std::string& opType, const std::map<std::string, float>& params,
                       const std::string& varName, ContextVarMap* vars);

// RESTORE (TiXL SetFloatVar.cs:33-40): if hadPrev restore the previous value; else if !clearAfterExecution
// Remove(name). No-op on an inactive scope. Call AFTER cooking the SubGraph.
void cmdVarRestore(const CmdVarScope& scope, ContextVarMap* vars);

// ─────────────────── S3b: value↔command LIVE-READ scope (the seam that closes S3a's hollow) ──────────────────
// THE LIVE AMBIENT context-var map a value-rail Get*Var reads while it is cooked UNDERNEATH a Command-rail
// SetVarCmd scope — = TiXL's EvaluationContext.Float/IntVariables seen by a SubGraph child's GetFloatVar.Update.
//
// Why a thread-local and not a threaded param: the value-rail readers are evalFloat (graph.cpp, takes only the
// 16-byte GPU EvaluationContext — cannot grow; 87 call sites) and evalResidentFloat. The var is "live" ONLY for
// the duration of the SubGraph cook — exactly the ambient-context lifetime TiXL gives it — so a scope guard set
// around the driver's SubGraph cook (where cmdVarPush/cmdVarRestore already wrap) is the faithful shape. Mirrors
// the existing process-scoped cook-driver flags (setVarBugSkipWrite / executeCollectFirstOnlyForTest). nullptr
// outside any scope → Get*Var falls back to its normal value-rail value (faithful: behaviour unchanged off-scope).
//
// RAII guard: construct around the SubGraph cook AFTER cmdVarPush, destroy AFTER cmdVarRestore. Nests correctly
// (saves+restores the prior live map) so an inner SetVarCmd scope layers on the outer one, like TiXL's stacked
// pushes on the one ambient dictionary. A null `vars` (inactive scope / non-writer) leaves the prior live map.
struct LiveCtxVarScope {
  explicit LiveCtxVarScope(ContextVarMap* vars);
  ~LiveCtxVarScope();
  LiveCtxVarScope(const LiveCtxVarScope&) = delete;
  LiveCtxVarScope& operator=(const LiveCtxVarScope&) = delete;
 private:
  ContextVarMap* prev_;
  bool engaged_;
};

// The live ambient map, or nullptr when no SetVarCmd scope is active. Consulted by the value-rail Get*Var
// readers (evalFloat / evalResidentFloat) to re-resolve a named var LIVE instead of returning the frozen
// pre-cook value. Read-only for the readers; the writers go through LiveCtxVarScope.
ContextVarMap* liveCtxVars();

// Resolve ONE value-rail Get*Var node against the live ambient map, the TiXL Get{Float,Int}Var.Update way:
// TryGetValue(name) hit → the scoped value; miss → `fallback`. Returns `fallback` (unchanged) when no live
// scope is active, the op is not a Get*Var, or the name is unset. `opType` selects floatVars vs intVars +
// the GetIntVar truncate-toward-zero (TiXL Slot<int>). This is the SINGLE codepath both eval readers call so
// the flat and resident live-reads can never fork. found (optional) reports whether a live hit replaced fallback.
float liveGetVar(const std::string& opType, const std::string& varName, float fallback, bool* found = nullptr);

// True iff opType is a value-rail context-var READER (GetFloatVar / GetIntVar). The eval readers gate the
// live-read on this so every non-Get*Var node stays byte-identical (the live scope never perturbs them).
bool isValueRailContextVarReader(const std::string& opType);

// S3a -bug DRIVER flag (mirror of executeCollectFirstOnlyForTest): when true, BOTH cook legs SKIP the
// Command-rail var WRITE (cmdVarPush) — so a probe Command op in the SubGraph reads the unset map and
// falls back, biting the golden RED on both flat and resident. OFF in production. Reference returned so
// the golden flips it around a cook then resets it (process hygiene).
bool& setVarBugSkipWrite();

// Command-op registrar (Command/SubGraph in -> Command out, forwards the cooked subtree items, like
// SetRequestedResolution). The push/restore is in the driver; these ops only forward.
void registerSetVarCmdOps();

// --selftest-setvar-scope (the S3a HARD-GATE golden; both flat + resident legs). injectBug skips the
// Command-rail write so a probe Command op inside the SubGraph reads the unset map -> RED on BOTH legs.
int runSetVarScopeSelfTest(bool injectBug);

}  // namespace sw

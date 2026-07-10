// runtime/render_command_flow — the FLOW-CONTROL declarations for the RenderCommand collectors (Execute /
// Switch / Loop / ExecRepeatedly) + their test-only -bug DRIVER flags. Peeled out of render_command.h for
// the line-count ratchet (that header is the DrawKind/RenderDrawItem rendering surface + the Seam 2
// render-state stamp; these are a separate concern — HOW a MultiInput Command collector concatenates/
// selects/repeats its wired sources, not WHAT gets drawn). Both cook drivers (flat point_graph_command_cook.cpp
// + resident point_graph_resident_command_cook.cpp) call these SAME functions so the collector math can NEVER
// fork between legs (the S2c/S3a blood lesson). Zone: runtime leaf (no upward deps).
#pragma once

#include <functional>
#include <string>

namespace sw {

struct RenderCommand;    // render_command.h
struct ContextVarMap;    // stateful_value_ops.h (complete type only where the map is mutated)

// S2a test-only DRIVER flag (the MultiInput Command collector tooth): when true, cookCommand's
// MultiInput Command branch COLLAPSES to the first wire (the `break` bug) instead of concatenating all
// N wired Command chains in wire order. So --selftest-execute's -bug leg drops every layer past the
// first → the composited chain loses items → the golden goes RED. OFF in production (zero behavior
// change — the loop concatenates every wire). This is a CPU DRIVER flag, NOT a shader bug-branch (no
// test seam in any .metal — constitution rule); parallel to meshDepthDisableForTest (render_command.h).
// Defined in point_ops_execute.cpp; read by the flat (point_graph.cpp) + resident (point_graph_resident.cpp)
// collectors. Single-input Command ports (Camera/SetRequestedResolution) are unaffected (they already
// take only the first wire; the flag is a no-op for them).
bool& executeCollectFirstOnlyForTest();

// S3b Switch (TiXL flow/Switch.cs): the Command-collector SUB-SELECT. Unlike Execute (concat ALL wires),
// Switch cooks ONLY the index-th wired Command (wrap, negative-safe), -2 = all, -1/empty = none. The
// SELECTION is a cook-core hook in the SAME MultiInput Command collector branch Execute/SetVarCmd live in:
// the driver, on a Switch node, reads the Index param + counts the N wired Commands and concatenates only
// the selected one. switchSelectIndex() is the SINGLE source of truth both the flat (point_graph.cpp) and
// resident (point_graph_resident.cpp) collectors call, so the wrap/negative/empty math can NEVER diverge
// (the §3 off-by-one trap: resident wires = primary + extraConns). Defined in point_ops_switch.cpp.
constexpr int kSwitchSelectAll = -2;   // cook every wire (TiXL Switch.cs index==-2)
constexpr int kSwitchSelectNone = -1;  // cook no wire (TiXL Switch.cs index==-1 OR count==0)
// rawIndex = the (truncated-to-int) Switch.Index param; count = number of wired Command inputs gathered.
// → kSwitchSelectAll / kSwitchSelectNone, or the wrapped index in [0, count) (the single wire to cook).
int switchSelectIndex(int rawIndex, int count);
// Test-only DRIVER flag (the Switch sub-select tooth): true → the collector IGNORES the selection and cooks
// ALL wires (== Execute), so --selftest-switch's -bug leg draws the wrong branch → center-pixel RED. OFF in
// production (zero behaviour change). A CPU DRIVER flag, NOT a shader bug-branch (constitution rule); read by
// both collectors, parallel to executeCollectFirstOnlyForTest(). Defined in point_ops_switch.cpp.
bool& switchIgnoreIndexForTest();
// SwitchParticleForce (force-rail Switch twin): declarations + full rationale in point_ops.h /
// point_ops_switchforce.cpp (kept out of this header too).

// ─────────────────────────────── S3c Loop (TiXL flow/Loop.cs) ───────────────────────────────
// The re-cook keystone: cook the wired SubGraph `Count` times; iteration i writes index→BOTH Float+Int dicts
// and progress→Float into the live ContextVarMap FIRST, then re-cooks the subtree (so a value-rail Get*Var
// inside it reads i/progress live), concatenating each iteration's items. Faithful to Loop.cs:23-40: index=i,
// progress=(Count==1?0:i/(float)(Count-1)), and does NOT restore index/progress after (Loop.cs:21 TODO leaks
// them — match it). loopRunIterations() is the SINGLE mechanism both the flat + resident collectors call (the
// var-write + live-scope + re-cook + concat can NEVER fork between legs — the S2c/S3a blood lesson). The leg
// supplies `cookOneIteration` (fresh-cooks the subtree, returns its items); the helper owns var/scope/concat.
// `vars` null → no var write (benign no-op-var loop); `count<=0` → empty. A LiveCtxVarScope per iteration
// makes the nodeParams memo re-resolve Float params FRESH so GetFloatVar(index) differs per iteration.
void loopRunIterations(int count, const std::string& indexVar, const std::string& progressVar,
                       ContextVarMap* vars, RenderCommand& out,
                       const std::function<RenderCommand()>& cookOneIteration);

// S3c -bug DRIVER flags (mirror of switchIgnoreIndexForTest / executeCollectFirstOnlyForTest), read by
// loopRunIterations on BOTH legs. OFF in production (zero behaviour change).
//   (a) loopBugCookOnceForTest: drop the for-loop → cook the subtree ONCE (one iteration's items) → the
//       chain has 1 item instead of Count → RED.
//   (b) loopBugReuseFirstForTest: cook ONCE then replicate that first iteration's items Count times WITHOUT
//       re-cook / without a fresh var write → every item carries iteration-0's index → all identical → RED.
// Defined in point_ops_loop.cpp.
bool& loopBugCookOnceForTest();
bool& loopBugReuseFirstForTest();

// ─────────────────── S3c ExecRepeatedly: re-cook the MultiInput wires `count` times ───────────────────
// The Loop SIBLING with no var injection (TiXL ExecRepeatedly.cs:34-53): cooks the COLLECTED Command wires
// (MultiInput, wire order) `repeatCount` times, concatenating every repetition. No index/progress var (the
// subtree re-executes for its side-effects). repeatCount clamped [0,100] (ExecRepeatedly.cs:24; driver clamps).
// execRepeatedlyRunRepetitions() = the SINGLE re-cook mechanism BOTH legs call so it can NEVER fork (S2c/S3a
// lesson). The leg supplies `cookAllWiresOnce` (fresh-cook every wired source, wire order). count<=0 → empty.
void execRepeatedlyRunRepetitions(int count, RenderCommand& out,
                                  const std::function<RenderCommand()>& cookAllWiresOnce);

// S3c ExecRepeatedly -bug DRIVER flag (mirror of loopBugCookOnceForTest), read by
// execRepeatedlyRunRepetitions on BOTH legs. OFF in production. When true: cook the wires ONCE (drop the
// repeat loop) → the chain has 1×wires items instead of count×wires → the item-count assertion goes RED.
// Defined in point_ops_execrepeatedly.cpp.
bool& execRepeatedlyBugRunOnceForTest();

}  // namespace sw

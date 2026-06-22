// runtime/resident_eval_graph — the RESIDENT (build-once, frame-stable) evaluation
// graph: the flattened form of a nested SymbolLibrary. = TiXL's "resolve boundaries at
// wire-time, transparent at eval-time" + Slot's structural role. A ResidentNode is one
// inlined Child; its inputs each carry a `driver` (= TiXL Slot's update action). Driver
// AUTHORITY for Automation lives in the definition-layer Animator (contract C3/P2) — the
// resident input's driver is a PROJECTION resolved at build/patch time, never the store.
//
// This module is the batch-1 engine. It is pure CPU (no Metal) and a runtime leaf:
// depends only on compound_graph.h (nested model) + graph.h (NodeSpec/findSpec). It is
// proven headless (--selftest-residenteval) and NOT yet wired to production cook — that
// is a named follow-on slice (see docs/superpowers/plans/2026-06-10-resident-eval-graph-batch1.md).
#pragma once
#include <cstdint>
#include <map>
#include <string>
#include <unordered_set>
#include <vector>

#include <simd/simd.h>  // simd::float4 (ResidentNode::extColorOut — the vec4-list resident channel)

#include "runtime/compound_graph.h"  // SymbolLibrary / Symbol / SymbolChild / SymbolConnection

namespace sw {

// Two-clock eval context (bars units, 拍板 P3). localTime = the playhead (scrub/pause
// freezes it) — automation samples THIS (time-lane S3). localFxTime = the wall clock
// (runs while paused) — stateful sims sample THIS. Defined now so automation/sim plug in
// without reshaping (contract 2.5b). This is the CPU eval-time context — NOT the 16-byte
// GPU `EvaluationContext` (eval_context.h); reconciling the GPU constant buffer happens at
// the production-swap slice. evalResidentFloat builds a transient GPU ctx to call evaluate().
struct ResidentEvalCtx {
  float localTime = 0.0f;    // playhead, bars
  float localFxTime = 0.0f;  // wall clock, bars
  uint32_t frameIndex = 0;
  // S3: the library whose definition-layer Animators back the Automation drivers. Sampling an
  // Automation input resolves (animSymbolId, curveRef) -> Curve* THROUGH this, then samples @
  // localTime (the playhead). nullptr (default) = no automation resolvable -> Automation drivers
  // fall back to the projected constant (the pre-S3 flat behavior — paths never diverge).
  const SymbolLibrary* lib = nullptr;
};

// How one resident input is driven (= TiXL Slot's UpdateAction). The PROJECTION of the
// definition-layer authority, resolved at build time. Automation carries a ref into the
// def-layer Animator keyed by (symbolId,childId,inputId) — sampled in S3; stub here.
struct ResidentInput {
  enum class Driver { Constant, Connection, Automation };
  std::string slotId;                 // the op's input slot id (= PortSpec.id / SlotDef.id)
  Driver driver = Driver::Constant;
  float constant = 0.0f;              // Driver::Constant: the projected value
  std::string srcNodePath;            // Driver::Connection: upstream resident node path
  std::string srcSlotId;              // Driver::Connection: upstream output slot id
  std::string curveRef;               // Driver::Automation: def-layer animator key (S3 samples it)
  std::string animSymbolId;           // Driver::Automation: which Symbol's Animator owns curveRef
  // MultiInput (批次25 seam): EXTRA Connection sources beyond the primary (srcNodePath/srcSlotId),
  // for a port flagged PortSpec.multiInput. Empty for every single-cardinality input (the primary
  // fields hold the one driver, exactly as before). Order = wire declaration order in the Symbol.
  std::vector<std::pair<std::string, std::string>> extraConns;  // (srcNodePath, srcSlotId) each
};

// --- batch 1b: version-chasing dirty + per-output-slot cache (承重決策 6/7, TiXL DirtyFlag) ---
// One output slot's incremental-eval state, living ON the resident node (= TiXL Slot — cache
// has a frame-stable home, 拍板「節點 = slot」; not a parallel structure). C5: per-output-slot
// granularity. initially dirty: valueVersion(0) != sourceVersion(1) so the first pull computes
// (DirtyFlag.cs:62). dirty == valueVersion != sourceVersion (version-chasing, NOT content hash).
struct ResidentOutputCache {
  uint64_t baseVersion = 1;    // THIS slot's own accumulated version (LIVE 每幀 / edit-time push
                               //   ++ this — monotonic, never overwritten, = TiXL SourceVersion++)
  uint64_t sourceVersion = 1;  // effective version, recomputed each pull = baseVersion + 上游和
                               //   (derived adopts the upstream sum ON TOP of its own baseVersion)
  uint64_t valueVersion = 0;   // = sourceVersion at last recompute; dirty when they differ
  float cachedFloat = 0.0f;    // last computed value (returned while not dirty — 算一次存著)
  bool isLiveSource = false;   // op 宣告恆髒 (Time…) ∨ Automation-driven ∨ per-output triggerOverride=
                               //   Always (S2); bumped every frame (決策 7 / 🪤#1)
  // S2 per-output isDisabled (= TiXL Slot.SetDisabled, Slot.cs:43-67): the value FREEZES at its last
  // result. In this version-chasing cache that means "stop chasing": pullResidentFloat sees this flag,
  // skips the version compare + recompute, and returns cachedFloat verbatim — so an upstream change can
  // never thaw it. NOT a default, NOT a node skip. Cleared (false) restores normal version-chasing.
  bool isDisabled = false;
  // editor-only: the frameIndex (ResidentEvalCtx::frameIndex) of the last REAL recompute on this slot
  // (= TiXL DirtyFlag._lastUpdateTick, DirtyFlag.cs:48 "editor-specific"). Written by pullResidentFloat
  // ONLY when valueVersion advances (a true recompute). 0 = never recomputed (treat as just-updated,
  // no fade). UI reads this to compute idle fade: node's max(lastUpdatePass across outputs) -> framesSince
  // -> RemapAndClamp(0,60,1.0,0.6) (TiXL DrawNode.cs:49-50). Does NOT affect cook semantics.
  uint32_t lastUpdatePass = 0;
};

// One inlined operator instance. `path` is the path-qualified id (join of the child-id
// chain, e.g. "5/2/1") — unique AND frame-stable, so cache (slice 4) and the per-node
// buffer map (slice 2) key off it. opType = the atomic symbol id = the operator type.
struct ResidentNode {
  std::string path;
  std::string opType;
  std::vector<ResidentInput> inputs;
  // String sub-seam (context-var YELLOW): resolved String input params, keyed by slot id (e.g.
  // "VariableName" -> "x"). Projected at flatten time from the SymbolChild's strOverrides else the
  // referenced symbol's String inputDef.strDef. EMPTY for every node with no String port (universal
  // → zero footprint). String ports carry NO driver/wire (the value rail is float-only): a String
  // param is a pure resolved constant, so it lives here, NOT in `inputs`. cookStatefulValueNodes
  // reads strInputs["VariableName"] to drive the context-var ops. The ONE non-float resident input
  // channel; the Float-only resolvers (resolveResidentFloatInputs / evalResidentFloat) never see it.
  std::map<std::string, std::string> strInputs;  // String slot id -> resolved text
  std::map<std::string, ResidentOutputCache> outCache;  // outSlotId -> cache (1b; per-output)
  // Externally-cooked outputs (mirror of flat Node::outCache): stateful value ops with no pure
  // evaluate() — AudioReaction — are cooked by the app's per-frame cooker, which writes the
  // results here; evalResidentFloat returns extOut[output port index] for such nodes.
  // Width 8 (was 3): some stateful value ops emit >3 outputs (HasVec3Changed=7, PeakLevel=4).
  float extOut[8] = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
  // Externally-cooked COLORLIST outputs (vec4-list cook flow = TiXL Slot<List<Vector4>>). The vec4
  // PARALLEL of extOut[]: cookColorListNodes (the per-frame production pass, frame_cook.cpp) writes a
  // colorlist op's (ColorsToList) result HERE, keyed by output port index. A SEPARATE channel — a host
  // color list is NOT packed into the float extOut slots (that would shred a float4 across 4 scalar
  // slots and break the per-output-slot scalar cache). Empty for every non-colorlist node (zero
  // footprint). Borrowed-readable by a downstream colorlist consumer + the resident colorlist golden.
  std::map<int, std::vector<simd::float4>> extColorOut;  // output port index -> cooked host color list
  // Externally-cooked STRING outputs (host std::string cook flow = TiXL Slot<string>). The string twin
  // of extColorOut: cookStringNodes (the per-frame production pass, frame_cook.cpp) writes a string op's
  // (FloatToString / CombineStrings / …) result HERE, keyed by output port index. A SEPARATE channel —
  // a host string is NOT a float, so it cannot ride extOut[] (and a String wire carries no GPU buffer).
  // Empty for every non-string node (zero footprint). Borrowed-readable by a downstream resident String
  // consumer (StringLength's resident leg gathers through it) + the resident string-rail golden (LEG 20).
  std::map<int, std::string> extStrOut;  // output port index -> cooked host string
  // Externally-cooked STRINGLIST outputs (host std::vector<std::string> cook flow = TiXL Slot<List<string>>).
  // Sub-seam A — the string-rail twin of extColorOut over std::string: a StringList producer (SplitString)
  // hands its result HERE, keyed by output port index. A SEPARATE channel — a host string LIST is neither a
  // float (extOut[]) nor a single string (extStrOut), and a StringList wire carries no GPU buffer. Empty for
  // every non-stringlist node (zero footprint). Borrowed-readable by a downstream resident StringList
  // consumer (JoinStringList's resident leg gathers through cookResidentStringList) + the golden (LEG 36).
  std::map<int, std::vector<std::string>> extStrListOut;  // output port index -> cooked host string list
  // S2 bypass (= TiXL Slot.ByPassUpdate, Slot.cs:176-179): when bypassed AND bypassable, this node's
  // MAIN output (bypassOutSlot) returns its MAIN input's (bypassInSlot) upstream value instead of
  // cooking. Set at build time (mirrors how a wire feeding a slot is resolved at build, not eval).
  // bypassed==false (default) => the op cooks normally. Empty slot ids when not bypassable.
  bool bypassed = false;
  std::string bypassInSlot;   // MAIN input slot id (Inputs[0]) the output passes through to
  std::string bypassOutSlot;  // MAIN output slot id (Outputs[0]) that becomes a passthrough
  // S2 per-output state projected from the SymbolChild at build time (sparse: only non-default
  // outputs present). initResidentNodeCache reads these onto each output's cache: a triggerAlways
  // output is a LIVE source (bumped每幀); a disabled output freezes (pull returns cachedFloat). Keyed
  // by output slot id (= NodeSpec output port id). Empty = every output default.
  std::map<std::string, bool> disabledOut;          // outSlotId -> true (frozen)
  std::map<std::string, bool> triggerAlwaysOut;     // outSlotId -> true (DirtyFlagTrigger.Always)
  const ResidentInput* input(const std::string& slotId) const;
};

// The flattened, walkable graph. `outputs` maps the root Symbol's outputDef id -> the
// resident (path, slotId) that produces it (boundary sentinel resolved away).
struct ResidentEvalGraph {
  std::vector<ResidentNode> nodes;
  std::map<std::string, int> byPath;        // path -> index into nodes
  std::map<std::string, std::pair<std::string, std::string>> outputs;  // rootOutputDefId -> (path, slotId)
  const ResidentNode* node(const std::string& path) const;
};

// S3: sample an Automation-driven input through the definition-layer Animator. Resolves
// (ri.animSymbolId, ri.curveRef) -> Curve* via ctx.lib, then samples @ ctx.localTime (the playhead,
// 拍板 取樣時鐘 = localTime). Falls back to ri.constant when ctx.lib is null or the curveRef is
// unresolvable (= the pre-S3 flat fallback — an Automation binding with no live curve reads the
// stored constant, so the resident and flat paths never diverge). The ONE automation sampling
// codepath the three eval sites (eval/resolve/pull) share.
float sampleAutomation(const ResidentEvalCtx& ctx, const ResidentInput& ri);

// Flatten a nested SymbolLibrary rooted at rootId into a resident eval graph. Inlines every
// compound child recursively (path prefix grows), resolves boundary-crossing wires via the
// kSymbolBoundary sentinel, and guards self-nesting/cycles (a symbol id repeating on the
// current path, or depth overflow) by skipping the offending child (TiXL Core does NOT guard
// this — we add the guard, contract S14, because the resident era has no per-frame rebuild
// to bail us out). Returns an empty graph if rootId is missing.
ResidentEvalGraph buildEvalGraph(const SymbolLibrary& lib, const std::string& rootId);

// Pull the float value produced by (nodePath, outSlotId), resolving each input's driver and
// recursing Connection drivers. Reuses the SAME NodeSpec::evaluate fns as flat evalFloat
// (builds a transient 16-byte EvaluationContext: time = ctx.localFxTime for now). depth>64
// breaks cycles. Automation driver returns 0 (S3 stub).
float evalResidentFloat(const ResidentEvalGraph& g, const std::string& nodePath,
                        const std::string& outSlotId, const ResidentEvalCtx& ctx, int depth = 0);
// Resolve EVERY Float input port of one resident node through its driver (Constant value /
// Connection -> evalResidentFloat / Automation -> S3 stub), keyed by port id. The resident
// cook driver hands this map to ops (PointCookCtx::params — the slice-2b seam, mirror of
// flat resolveNodeParams). Ports with no resident input fall back to the spec default.
std::map<std::string, float> resolveResidentFloatInputs(const ResidentEvalGraph& g,
                                                        const ResidentNode& n,
                                                        const ResidentEvalCtx& ctx);

// The RESIDENT value-currency cook prototypes (cookResidentFloatList / cookHostScalarNodes /
// cookResidentColorList / cookColorListNodes / cookResidentStringList / cookResidentString /
// cookStringNodes) were EXTRACTED into resident_value_cooks.h to keep this header at-or-below its
// line-count cap (ARCHITECTURE.md rule 4 ratchet). Included at the tail (below, after the type
// definitions it forward-declares) so every existing caller that includes resident_eval_graph.h still
// sees them unchanged.

// --- batch 1b cache API (resident_eval_cache.cpp) ---
// Populate each node's per-output cache (one entry per NodeSpec output port) and mark live
// sources (ops that declare an always-dirty output, e.g. Time). Call once after buildEvalGraph.
void initResidentCache(ResidentEvalGraph& g);
// Same, for ONE node (used by the patch layer when it creates nodes incrementally).
void initResidentNodeCache(ResidentNode& n);
// Carry frozen (isDisabled) output values across a projection REBUILD — TiXL has no rebuild
// artifact (its slots persist, Value and all), so "frozen at the last result" must survive
// ours; without this the GUI disable-toggle snaps the value to 0 next frame (refuter-S2 P1×P7).
void transplantDisabledCaches(const ResidentEvalGraph& oldG, ResidentEvalGraph& newG);
// Bump every live source's sourceVersion (= TiXL DirtyFlagTrigger.Always 每幀). 🪤#1 (決策 7):
// the per-frame correctness invariant — call at the START of every frame, before pulling.
// Miss one live source -> downstream reads stale cache (卡舊畫面). Wired into the -bug teeth.
void bumpLiveSources(ResidentEvalGraph& g);
// Pull a float through the version-chasing cache (eager post-order, one pass — 決策 6): recurse
// Connection inputs first (always walks the cone — cheap), a DERIVED slot adopts the SUM of its
// upstream sourceVersions (multi-input combine: any input changes -> dirty), recompute+cache only
// when dirty (valueVersion != sourceVersion), else return cachedFloat WITHOUT recomputing (省重算).
// Mutates g — the cache is resident state. depth>64 breaks cycles. Automation driver -> 0 (S3 stub).
float pullResidentFloat(ResidentEvalGraph& g, const std::string& nodePath,
                        const std::string& outSlotId, const ResidentEvalCtx& ctx, int depth = 0);

// Headless RED->GREEN proof: builds a nested library (compound w/ reuse) + the equivalent
// flat library, asserts resident eval matches AND matches the hand-computed value, asserts
// reuse isolation, driver resolve, and the two-clock shape. injectBug pollutes a definition
// so reuse leaks -> the equivalence assertion FAILS (teeth).
int runResidentEvalSelfTest(bool injectBug);
// MultiInput seam proof (批次25): N wires into one multiInput slot reduce via Sum. See the .cpp.
int runMultiInputSelfTest(bool injectBug);

// Headless RED->GREEN proof of the 1b version-chasing cache: a STATIC subgraph recomputes once
// then returns cache (mutating an upstream constant WITHOUT an edit-time bump stays stale —
// proving the short-circuit), an edit-time bump propagates (sum) and forces recompute, and a
// LIVE source (Time) recomputes every frame. injectBug skips bumpLiveSources -> the LIVE node
// stays frozen on frame 1 (卡舊) -> the per-frame assertion FAILS (teeth, spec 🪤#1).
int runResidentCacheSelfTest(bool injectBug);

// --- batch 14 IF: live-source downstream closure (production idle-fade stamp) ---
// Compute the transitive closure of nodes reachable from any live-source (isLiveSource=true)
// node by following Connection edges forward (source → consumer). Returns the set of node
// paths that must have their lastUpdatePass stamped every frame — covers live sources
// themselves AND all downstream stateless nodes that recompute every frame via evalResidentFloat
// (= TiXL's every-dirty-slot SetUpdated in Slot.cs:160-168). Nodes OUTSIDE the closure are
// static (their lastUpdatePass stays at build time → fade after 60 frames, fork-I0 named).
// Requires initResidentCache to have been called on `g` (reads outCache.isLiveSource).
std::unordered_set<std::string> computeLiveDownstreamClosure(const ResidentEvalGraph& g);

// Headless RED->GREEN proof of the idle fade signal (批次14 lane IF):
// P1 pullResidentFloat writes cache.lastUpdatePass = ctx.frameIndex on each true recompute;
// P2 a cache-hit (short-circuit) does NOT update lastUpdatePass (idle node detected);
// P3 cook output (cachedFloat) is bit-identical whether or not idle tracking is present (iron line);
// P4 (in ui/node_style) nodeBgColorIdle(1.0)=active=nodeBgColor; nodeBgColorIdle(0.6)!=active.
// P5 downstream stamp: Time→Multiply graph, after stampLiveLastUpdatePass with the closure,
//    Multiply's framesSince == 0 (fix for downstream always-dark bug). injectBug stamps only
//    source nodes (pre-fix behaviour) -> Multiply stays dark -> FAILS.
// injectBug zeroes lastUpdatePass after the active cook -> the active-vs-idle distinction FAILS.
int runIdleFadeSelfTest(bool injectBug);

// --- batch 1b / slice-3 first cut: incremental patch (resident_eval_patch.cpp) ---
// Edit the resident graph IN PLACE, preserving cache on untouched nodes (增量, 不每幀重建 —
// the structural half of "resident"). Cache versions follow TiXL's edit rules exactly so that a
// patched graph evaluates identically to one rebuilt with the edit baked in (patch == rebuild).
//
// B3 (批次9): each returns whether the edit LANDED (path resolves to a resident node AND the slot
// exists on it). false = NOT PATCHABLE at the resident level — the caller must rebuild. The named
// unrepresentable case: a BYPASSED COMPOUND child has zero resident footprint (修C rewires it
// away), so an edit addressed at its path can only be realized by rebuild; the old silent no-op
// broke patch == rebuild one-sided (refuter-E1 B3, --selftest-residentpatch leg 3).
//
// S1 value edit (Slot.cs / InputSlot.cs:57-63, ChangeInputValueCommand.cs:122): change a Constant
// input's value, then edit-time push by bumping THIS node's output sourceVersions. Downstream goes
// dirty via the pull-time upstream-sum; untouched siblings keep their cache.
bool patchSetConstant(ResidentEvalGraph& g, const std::string& path, const std::string& slotId,
                      float value);
// S11① add connection (Slot.cs:198-205): rewire a Constant input to a Connection, then force the
// dst's outputs to first-pull-recompute by setting valueVersion to the never-matching sentinel
// (= TiXL ValueVersion=-1). NOT a sourceVersion bump — that would corrupt the derived multi-input
// sum arithmetic (spec 健檢二補 ②). Untouched nodes keep their cache.
bool patchAddConnection(ResidentEvalGraph& g, const std::string& dstPath, const std::string& dstSlot,
                        const std::string& srcPath, const std::string& srcSlot);
// S11② remove connection (Slot.cs:233-245): restore the pre-connection driver — the input falls back
// to Constant with the KEPT value (in.constant survives under a Connection, = TiXL
// _actionBeforeAddingConnecting / SymbolChild.Input.Value persisting while wired) — then
// ForceInvalidate. A found-but-not-wired slot returns true (a representable no-op == rebuild no-op).
// 🪤 version monotonicity (generalizes refuter D1/A4): the dropped upstream's
// contribution is ABSORBED into baseVersion before the force-bump, so this slot's sourceVersion
// NEVER DECREASES across the edit — otherwise the derived upstream-sum arithmetic has false-clean
// collisions (e.g. dropped contribution exactly 1 cancels the ++).
bool patchRemoveConnection(ResidentEvalGraph& g, const std::string& dstPath, const std::string& dstSlot);

// --- slice 3 rest: DEFINITION-level patches (resident_eval_patch_lib.cpp) ---
// TiXL edits live on the Symbol (definition) and broadcast to every instance (Symbol.cs:222-330,
// _childrenCreatedFromMe). These mutate the LIBRARY first (definition = authority, contract C3),
// then incrementally patch the resident projection so that patch == rebuild (the golden).
// Value-level edits are O(1)-ish surgery; STRUCTURAL edits (add/remove child, IO change) re-derive
// the wiring through the ONE canonical codepath (buildEvalGraph) and MIGRATE caches: nodes whose
// path+opType+inputs (incl. Connection resolvability) are unchanged keep their cache wholesale;
// changed nodes are forced with baseVersion >= old sourceVersion + 1 (monotonic — no false-clean).
// Edits are rare; O(graph) at edit time matches TiXL's own broadcast-reconnect cost. Returns false
// if the edit's preconditions fail (missing symbol/slot/child, duplicate id, ...).

// S11⑤ change a definition input's default (Symbol.cs:375-386 + Symbol.Child.cs:677-698): updates
// the SlotDef, then bumps ONLY resident instances still using the default — children with an
// override on that slot are untouched and keep their cache (the IsDefault filter). Wired inputs
// refresh their KEPT fallback without a bump (TiXL reads defaults live at eval; a later disconnect
// must restore the NEW default — refuter A-2). Compound symbols route through rebuild+migration
// (the filter emerges from rule 1/2).
bool patchLibSetDefault(SymbolLibrary& lib, ResidentEvalGraph& g, const std::string& symbolId,
                        const std::string& slotId, float newDef);
// S11③ add child (Symbol.Instantiation.cs:14-39): appends the child to the parent definition and
// instantiates one resident node per resident instance of the parent (reuse: N parents -> N new
// nodes). Atomic child symbols only for now (compound add = recursive inline, named-deferred).
// A pre-existing DANGLING reference to the new path becomes resolvable -> its consumers are forced
// (resolvability change counts as an input change; the dangling fixed-1 contribution would
// otherwise alias the fresh node's sourceVersion 1 -> false-clean).
bool patchLibAddChild(SymbolLibrary& lib, ResidentEvalGraph& g, const std::string& parentSymbolId,
                      int childId, const std::string& childSymbolId);
// S11④ remove child (Symbol.cs:311-330): removes the parent definition's connections touching the
// child (牽涉連線清除) then the child itself; every resident instance of the subtree disappears.
// Same-scope sibling consumers (their wire was just erased) fall back to Constant effectiveInput;
// cross-boundary consumers (their wire lives in ANOTHER symbol, now unresolvable) go dangling and
// evaluate the upstream as 0 — both exactly what a rebuild yields, both force-invalidated.
bool patchLibRemoveChild(SymbolLibrary& lib, ResidentEvalGraph& g, const std::string& parentSymbolId,
                         int childId);
// S11⑥ IO change / S13 收屍 (Symbol.TypeUpdating.cs:99-132,213-261): removes an input def from a
// symbol, scrubs orphaned wires (inside: boundary->consumer; outside: parent wires INTO that slot
// of every child referencing the symbol) and drops now-obsolete child overrides on that slot.
// Inner consumers fall back to their own effectiveInput.
bool patchLibRemoveInputDef(SymbolLibrary& lib, ResidentEvalGraph& g, const std::string& symbolId,
                            const std::string& slotId);
// Symmetric output-def removal (scrubs the symbol's own SINK boundary wire + every parent's wire OUT
// of that output slot; outputs carry no overrides). Same rebuild-on-edit contract as the input case.
bool patchLibRemoveOutputDef(SymbolLibrary& lib, ResidentEvalGraph& g, const std::string& symbolId,
                             const std::string& slotId);

// Headless RED->GREEN proof of the slice-3 first cut: after patchSetConstant / patchAddConnection,
// the graph evaluates identically to one rebuilt with the edit baked in (patch == rebuild), AND
// untouched nodes keep their cache (a sibling whose constant was mutated out-of-band is NOT
// recomputed — only the edited cone is). injectBug skips the edit-time invalidation -> the patched
// value stays stale (卡舊) -> the assertion FAILS (teeth).
int runResidentPatchSelfTest(bool injectBug);

// Headless RED->GREEN proof of slice-3 rest (the remaining S11 edits; with the existing
// set-const/add-connection goldens this completes the six-edit patch == rebuild sweep):
// remove-connection restore + force, change-default with the IsDefault filter (overriding child
// keeps cache — probed out-of-band), add-child broadcast across reuse + dangling-ref resolution
// forcing, remove-child (same-scope restore AND cross-boundary dangling, cache preserved on
// untouched branches), remove-input-def fallback. injectBug applies the default edit without the
// edit-time bump -> stale (卡舊) -> FAILS (teeth).
int runResidentLibPatchSelfTest(bool injectBug);

}  // namespace sw

// The host-side value-currency cook prototypes (FloatList / ColorList / String / StringList). Included
// AFTER the ResidentEvalGraph / ResidentEvalCtx definitions above (the prototypes take them by
// reference), so resident_eval_graph.h stays the single include hub — existing callers are unchanged.
#include "runtime/resident_value_cooks.h"

// runtime/compound_graph — the NESTED graph model (TiXL compound). This is the
// graph-model layer ONLY: pure CPU data, no Metal, no upward deps (ARCHITECTURE.md
// runtime leaf). It coexists with the flat `Graph` (graph.h) — the flat graph is
// what cook()/evalFloat consume TODAY. The batch-1 flattener (resident_eval_graph.*)
// inlines a nested SymbolLibrary into a RESIDENT eval graph (build-once, frame-stable,
// edit-time patched) — NOT a per-frame throwaway flat graph (that route was 作廢, see
// compound contract §2.2: cache must hang on a node with stable cross-frame identity).
// This header adds only the nested data types; the resident engine is a separate module.
//
// Faithful to TiXL (Core/Operator/Symbol.cs, Symbol.Child.cs, Symbol.Connection):
//   Symbol          = a definition (id + input/output defs + child instances + connections)
//   SymbolChild     = an instance of a Symbol inside another Symbol's subgraph
//   SymbolConnection= a 4-tuple wire; childId == kSymbolBoundary(0) is the SENTINEL for
//                     "the parent Symbol's own external slot" (= TiXL's Guid.Empty).
// reuse = many SymbolChildren referencing the same symbolId (edit the def -> all change).
// There are NO Input/Output proxy nodes — the compound's external ports ARE its
// input/output defs, and boundary-crossing wires use the sentinel. This is TiXL's trick.
#pragma once
#include <map>
#include <string>
#include <vector>

#include "runtime/curve_animator.h"  // Animator (S3 definition-layer automation authority)

namespace sw {

// A Symbol's external input/output slot (= TiXL InputDefinition / OutputDefinition).
struct SlotDef {
  std::string id;        // stable slot identity (used by connections + overrides)
  std::string name;      // display label
  std::string dataType;  // "Points" / "Command" / "Texture2D" / "Float" / ...
  float def = 0.0f;      // default value for an input slot (outputs ignore it)
  // Canvas position of this slot's BOUNDARY node when viewing inside the symbol (= TiXL
  // IInputUi/IOutputUi.PosOnCanvas, persisted there in .t3ui — our v2 is single-file so it
  // lives inline, same precedent as SymbolChild.x/y). Movable on canvas, serialized in v2.
  float x = 0.0f, y = 0.0f;
};

// Sentinel child id meaning "the parent Symbol's own external port" (= TiXL Guid.Empty).
// Real child ids are >= 1 (like node ids), so 0 is a natural, collision-free sentinel.
constexpr int kSymbolBoundary = 0;

// One wire inside a Symbol's subgraph (= TiXL Connection 4-tuple). A side whose child id
// is kSymbolBoundary refers to one of the parent Symbol's own SlotDefs (the boundary cross),
// resolved by slotId against inputDefs (source side) or outputDefs (target side).
struct SymbolConnection {
  int srcChild = kSymbolBoundary;
  std::string srcSlot;  // srcChild==0 -> parent inputDef id; else child's output slot id
  int dstChild = kSymbolBoundary;
  std::string dstSlot;  // dstChild==0 -> parent outputDef id; else child's input slot id
};

// An instance of a Symbol inside a parent Symbol's subgraph (= TiXL Symbol.Child).
// `overrides` holds ONLY non-default input values for THIS instance — everything else
// falls back to the referenced Symbol's inputDef.def, so the definition is never polluted.
struct SymbolChild {
  int id = 0;                              // instance id within the parent subgraph (>= 1)
  std::string symbolId;                    // which Symbol definition this instantiates
  std::map<std::string, float> overrides;  // slotId -> overridden value (non-default only)
  float x = 0.0f, y = 0.0f;                // canvas position (UI; moves to a sidecar later)
  // Custom instance name (= TiXL Symbol.Child.Name). EMPTY = fall back to the referenced Symbol's
  // name (TiXL ReadableName); non-empty overrides ONLY this instance's title, never the definition.
  // May hold CJK. LAST member on purpose: existing positional aggregate inits ({id, symbolId,
  // overrides, x, y}) stay valid (default-empty name) — no churn across the selftest call sites.
  std::string name;
};

// The display title of an instance = its custom name, or the referenced Symbol's name if blank
// (= TiXL Symbol.Child.ReadableName). `defName` is the resolved definition title to fall back to.
std::string childReadableName(const SymbolChild& c, const std::string& defName);

// A Symbol definition (= TiXL Symbol). An ATOMIC symbol has no subgraph — it is a leaf
// operator whose cook comes from the op registry (its id doubles as the operator type, e.g.
// "RadialPoints"). A COMPOUND symbol's subgraph = children + connections. External ports =
// inputDefs / outputDefs for both kinds.
struct Symbol {
  std::string id;    // stable identity (atomic: the operator type string; compound: generated)
  std::string name;  // display title
  bool atomic = false;
  std::vector<SlotDef> inputDefs;
  std::vector<SlotDef> outputDefs;
  std::vector<SymbolChild> children;        // compound subgraph instances (empty if atomic)
  std::vector<SymbolConnection> connections;  // compound subgraph wires (empty if atomic)
  // MONOTONIC child-id floor (serialized in v2). TiXL never reuses a child id (GUIDs); our
  // int-id fork must emulate that or a new child resurrects a deleted child's per-path
  // runtime state (GPU sim buffers, AudioReaction hit counts — refuter N2 #2). Bumped by
  // AddChildCommand (never decremented, undo included); nextFreeChildId() reads it.
  int nextChildId = 1;
  // S3: the definition-layer Animator — per-(childId,inputId) automation curves owned by THIS
  // symbol's subgraph (= TiXL Symbol.Animator, Symbol.cs). Editing a curve here broadcasts to every
  // instance (reuse). The resident projection's Automation drivers resolve curveRefs INTO this.
  Animator animator;
};

// Project-level playback/audio settings (= TiXL CompositionSettings / Playback.Settings —
// Playback.cs:32). The HOME of BPM + soundtrack persistence (契約3 健檢修正 — these live on the
// composition, not a separate file). Saved in the v2 .swproj "composition" segment; defaults are
// the new-document values, so an OLD file with no segment loads cleanly at 120 BPM (S15).
struct CompositionSettings {
  double bpm = 120.0;            // = Playback.Bpm. Drives the Transport's bars<->secs.
  std::string soundtrackPath;    // optional audio file backing the composition ("" = none).
  double soundtrackVolume = 1.0; // linear gain for the soundtrack (placeholder until S-audio lands).
};

// A project = a library of Symbol definitions + which one is the root (= TiXL SymbolPackage +
// root composition). reuse is expressed here: multiple children across symbols share a symbolId.
struct SymbolLibrary {
  std::map<std::string, Symbol> symbols;  // id -> definition
  std::string rootId;
  CompositionSettings composition;        // BPM/soundtrack home (S5; saved in the v2 file).

  const Symbol* find(const std::string& id) const;
  Symbol* find(const std::string& id);  // mutable — the editing model (批次 3 lib-native doc)
};

// Child lookup within one Symbol's subgraph (mirror of Graph::node(id)). nullptr if absent.
SymbolChild* childById(Symbol& s, int id);
const SymbolChild* childById(const Symbol& s, int id);

// Next free instance id in this subgraph: max(monotonic floor, max existing id + 1) — never
// reuses a freed id (>= 1; kSymbolBoundary==0 stays reserved). See Symbol::nextChildId.
int nextFreeChildId(const Symbol& s);

// The wire feeding (dstChild, dstSlot), or nullptr. Inputs are single-cardinality (multi-
// input order is a pinned-but-unused contract today), so at most one — the SSOT for
// "is this input already wired?" in the lib world (mirror of Graph::connectionToInput).
const SymbolConnection* connectionToInput(const Symbol& s, int dstChild,
                                          const std::string& dstSlot);

// The RESIDENT path that PRODUCES child `childId`'s primary output, viewed from the symbol
// subgraph at `prefixPath` ("" = root scope, else "1/4/"-style with trailing slash).
// An ATOMIC child is its own producer (prefix + id). A COMPOUND child inlines away — its
// resident path doesn't exist — so "view it" = follow its FIRST outputDef's boundary wire
// inward, recursively (TiXL: viewing an op shows its output slot; for a composition that
// slot is fed by an inner producer). Returns "" when unresolvable (no output def, unwired,
// input-passthrough, dangling, or depth/cycle overflow) — callers fall back to a terminal.
std::string viewProducerPath(const SymbolLibrary& lib, const std::string& prefixPath,
                             int childId);

// --- resolve helpers (behavior the flattener in batch 1 builds on) ---

// The effective value of a child's input slot: the instance override if present, else the
// referenced Symbol's inputDef default. The definition is never mutated — reuse stays isolated.
// Returns `fallback` if neither the override nor a matching inputDef exists.
float effectiveInput(const SymbolLibrary& lib, const SymbolChild& child,
                     const std::string& slotId, float fallback = 0.0f);

// Would instantiating Symbol `symbolId` inside Symbol `parentId` close a containment cycle?
// True iff symbolId == parentId (direct self-nest) OR parentId is reachable from symbolId's
// subtree (children->symbolId, transitively). The SSOT cycle gate for adding a compound from
// the Add Node menu (toolbar pre-filters with it) and AddChildCommand's defensive early-out.
// Atomic symbols have no children, so adding an atomic never cycles (returns false). Total even
// over an already-cyclic lib (crafted file) — visited-set bounded. = TiXL's "prevent graph
// cycles" intent (SymbolFilter.cs:118), made transitive (FORK noted in the .cpp).
bool addChildWouldCycle(const SymbolLibrary& lib, const std::string& parentId,
                        const std::string& symbolId);

// Sentinel predicates: is this wire's endpoint the parent Symbol's own external port?
inline bool sourceIsSymbolInput(const SymbolConnection& c) { return c.srcChild == kSymbolBoundary; }
inline bool targetIsSymbolOutput(const SymbolConnection& c) { return c.dstChild == kSymbolBoundary; }

// --- S13 收屍: remove a Symbol's external input/output definition (the ONE lib-surgery codepath) ---
// Removing a def is a CONTRACT change on the Symbol that broadcasts to every instance everywhere in
// the lib (= TiXL Symbol.TypeUpdating.cs:99-132 inner boundary wires + Symbol.Child.cs:217-223
// per-instance override scrub + Symbol.Child.cs:424-451 parent-side connection scrub). Both the
// resident patch (patchLibRemoveInputDef) AND the command layer (DeleteInputDefCommand) call this so
// the surgery never forks. `removed` (optional out) captures EXACTLY what was deleted, with original
// indices, so an undoable command can restore byte-identically; pass nullptr when you only need the
// forward edit (the resident path rebuilds, it doesn't undo).

// One parent symbol's connection that pointed INTO the deleted slot of one instance (host symbol id +
// the wire + its original index in that host's connection list, for order-preserving restore).
struct RemovedHostWire {
  std::string hostSymbolId;
  size_t index = 0;
  SymbolConnection wire;
};
// One instance's override on the deleted slot that was scrubbed (host symbol id + child id + value).
struct RemovedOverride {
  std::string hostSymbolId;
  int childId = 0;
  float value = 0.0f;
};
// The full undo snapshot of one def removal (def + its index + every wire/override the scrub touched).
struct RemovedSlotDef {
  SlotDef def;                              // the removed definition itself
  size_t index = 0;                         // its original position in inputDefs/outputDefs
  bool isInput = true;                      // which list it came from
  std::vector<std::pair<size_t, SymbolConnection>> innerWires;  // edited symbol's boundary wires (idx+wire)
  std::vector<RemovedHostWire> hostWires;   // parent symbols' wires into/out of that slot, every instance
  std::vector<RemovedOverride> overrides;   // scrubbed instance overrides (inputs only)
};

// Remove input def `slotId` from Symbol `symbolId`: erase the def, scrub the symbol's own boundary
// wires whose SOURCE is that input, scrub every parent symbol's wires INTO (childOfSymbol, slotId)
// preserving multi-input order, and drop now-obsolete instance overrides on that slot. Returns false
// (lib untouched) if the symbol or def is absent. Fills `removed` when non-null.
bool removeInputDefFromLib(SymbolLibrary& lib, const std::string& symbolId, const std::string& slotId,
                           RemovedSlotDef* removed = nullptr);
// Remove output def `slotId`: erase the def, scrub the symbol's own boundary wires whose TARGET is
// that output, scrub every parent symbol's wires OUT of (childOfSymbol, slotId). Outputs carry no
// instance overrides. Same return/`removed` contract as removeInputDefFromLib.
bool removeOutputDefFromLib(SymbolLibrary& lib, const std::string& symbolId, const std::string& slotId,
                            RemovedSlotDef* removed = nullptr);
// Re-insert a def removed by the two functions above, restoring the def, its boundary wires, parent
// wires, and overrides at their original indices (= the command-layer undo, order-faithful). No-op if
// the symbol is gone. The lib edit is the authority; callers bump the projection revision separately.
void restoreSlotDefToLib(SymbolLibrary& lib, const std::string& symbolId, const RemovedSlotDef& removed);

// Headless RED->GREEN proof of the nested model: builds a small library with an atomic + a
// compound symbol (reuse: two children of the same symbol), and asserts reuse isolation,
// override vs default, and the boundary sentinel. injectBug pollutes a definition so the
// reuse-isolation assertion FAILS (teeth).
int runCompoundModelSelfTest(bool injectBug);

// Headless RED->GREEN proof of the AddChild cycle gate (addChildWouldCycle): builds an A⊃B⊃C
// containment chain + an atomic leaf and asserts every self-nest (direct + transitive) is refused
// while legal reuse / atomic adds are allowed. injectBug blinds the predicate -> refuse legs FAIL.
int runCycleGuardSelfTest(bool injectBug);

}  // namespace sw

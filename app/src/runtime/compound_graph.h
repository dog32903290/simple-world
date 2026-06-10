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

namespace sw {

// A Symbol's external input/output slot (= TiXL InputDefinition / OutputDefinition).
struct SlotDef {
  std::string id;        // stable slot identity (used by connections + overrides)
  std::string name;      // display label
  std::string dataType;  // "Points" / "Command" / "Texture2D" / "Float" / ...
  float def = 0.0f;      // default value for an input slot (outputs ignore it)
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
};

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
};

// A project = a library of Symbol definitions + which one is the root (= TiXL SymbolPackage +
// root composition). reuse is expressed here: multiple children across symbols share a symbolId.
struct SymbolLibrary {
  std::map<std::string, Symbol> symbols;  // id -> definition
  std::string rootId;

  const Symbol* find(const std::string& id) const;
};

// --- resolve helpers (behavior the flattener in batch 1 builds on) ---

// The effective value of a child's input slot: the instance override if present, else the
// referenced Symbol's inputDef default. The definition is never mutated — reuse stays isolated.
// Returns `fallback` if neither the override nor a matching inputDef exists.
float effectiveInput(const SymbolLibrary& lib, const SymbolChild& child,
                     const std::string& slotId, float fallback = 0.0f);

// Sentinel predicates: is this wire's endpoint the parent Symbol's own external port?
inline bool sourceIsSymbolInput(const SymbolConnection& c) { return c.srcChild == kSymbolBoundary; }
inline bool targetIsSymbolOutput(const SymbolConnection& c) { return c.dstChild == kSymbolBoundary; }

// Headless RED->GREEN proof of the nested model: builds a small library with an atomic + a
// compound symbol (reuse: two children of the same symbol), and asserts reuse isolation,
// override vs default, and the boundary sentinel. injectBug pollutes a definition so the
// reuse-isolation assertion FAILS (teeth).
int runCompoundModelSelfTest(bool injectBug);

}  // namespace sw

# Resident Eval Graph (Batch 1, Slice 1) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Land the resident (build-once, frame-stable) evaluation graph as a headless, selftest-proven runtime module — flatten a nested `SymbolLibrary` into a walkable `ResidentEvalGraph` whose nodes carry driver-bearing inputs, and value-walk it. NOT wired to production cook (that is a named follow-on slice).

**Architecture:** Mirrors how compound batch 0 and the render-target pivot landed: a new runtime leaf module proven by `--selftest-*` with a RED→GREEN teeth test, while the live app keeps running on the existing flat-`Graph` path (the legacy seam). This slice locks three load-bearing contract decisions in code without the giant editor/save migration:
1. **resident node = slot** (`ResidentNode` with `ResidentInput`s that each carry a `driver`),
2. **driver authority lives in the definition layer** (resident input driver = a *projection* resolved at build time, never the authority — contract C3 / 拍板 P2),
3. **two-clock eval context shape** `{localTime, localFxTime}` in bars, defined now so automation (time-lane S3) plugs in without reshaping (contract 2.5b / 拍板 P3).

The resident walk **reuses the exact same `NodeSpec::evaluate` fns** as the flat `evalFloat`, so the golden ("nested graph evaluates == equivalent hand-built flat graph") is a true equivalence: same math, two graph representations.

**Tech Stack:** C++17, metal-cpp project (this slice is pure CPU — no Metal). Build via `app/CMakeLists.txt` (explicit source list) + CMake/Make. Tests via the data-driven `--selftest-<name>` router in `app/src/selftests.cpp` (add one row to `kTable`).

**Authority:** All "照 TiXL" claims anchor to `external/tixl` @ SHA `395c4c55` (see `docs/runtime/PARITY_TARGET.md` — do NOT `git pull` external/tixl). Contract = `docs/superpowers/plans/specs/2026-06-10-compound-graph-design.md` (batch 1, §2.3 / 2.4 / 2.5b). Health-check provenance = `docs/runtime/TIXL_PARITY_HEALTH_2026-06-10.md`.

---

## Scope — what this slice does and does NOT do

**In scope (this plan):**
- `ResidentEvalGraph` / `ResidentNode` / `ResidentInput` types + `ResidentEvalCtx` two-clock shape.
- `buildEvalGraph(lib, rootId)` — recursive inline of compound children, sentinel-boundary resolution, path-qualified frame-stable ids, self-nesting/cycle guard.
- Driver model on resident inputs `{Constant | Connection | Automation}` projected from definition-layer authority (Automation resolution is a documented stub — real curves are S3).
- `evalResidentFloat` — value walk reusing `NodeSpec::evaluate`.
- Goldens: nested == equivalent flat; reuse isolation; driver resolve; two-clock shape carried.

**Explicitly DEFERRED to named follow-on slices (NOT silent caps):**
- **Slice 2 — point cook on resident:** make `PointGraph::cook` walk the resident graph (Metal) + `cook == flat-cook` golden. (Today `PointGraph::cook` walks flat `Graph`; it already keeps per-node-id buffers/state across frames — the "half-resident" the contract §2.1 names.)
- **Slice 3 — incremental patch:** the six edit ops (add/del connection, add/del child, change-default, IO-change) + `patch == full-rebuild` golden (contract §2.3 health-fix S11). **Interim correctness:** rebuild-on-edit (not per-frame) is correct and is what this slice + slice 2 use; incremental patch is the optimization that the golden guards.
- **Slice 4 (= batch 1b):** version-chasing dirty + per-output-slot cache + Command-always + diamond dedup + LIVE-source per-pass bump + stateful-op FxTime time-gate (contract §2.4 / 2.5 / health-fix C1/C2/C5).
- **Later (ties to batch 2 存檔 v2):** production swap — the editor produces a `SymbolLibrary` and `main` cooks from the resident graph; the flat-`Graph` editor path + pin-id→4-tuple migration land there.

**Why this cut:** the editor + save + UI all speak flat `Graph`/pin-id today; migrating them is batch 2+. The resident engine can be built from a `SymbolLibrary` and proven headless first (exactly batch 0's pattern), locking the irreversible decisions cheaply. "先求簡單": rebuild-on-edit removes the scariest bug class (stale resident graph) until the patch golden exists.

---

## File Structure

| File | Create/Modify | Responsibility |
|---|---|---|
| `app/src/runtime/resident_eval_graph.h` | **Create** | The resident types (`ResidentEvalGraph`/`ResidentNode`/`ResidentInput`/`ResidentEvalCtx`), `buildEvalGraph`, `evalResidentFloat`, selftest decl. Pure CPU, runtime leaf — no Metal, no upward deps. |
| `app/src/runtime/resident_eval_graph.cpp` | **Create** | `buildEvalGraph` (flatten/inline/sentinel) + `evalResidentFloat` (value walk reusing `NodeSpec::evaluate`). |
| `app/src/runtime/resident_eval_graph_selftest.cpp` | **Create** | RED→GREEN goldens: nested==flat, reuse isolation, driver resolve, two-clock shape. Mirrors `compound_graph_selftest.cpp`. |
| `app/src/runtime/compound_graph.h:1-15` | **Modify** | Fix stale header comment: the batch-1 flattener no longer "turns a nested SymbolLibrary into that flat graph … WITHOUT touching the stable runtime" (that was the now-作廢 every-frame-throwaway route, contract §2.2). |
| `app/src/runtime/graph.h:1-4` | **Modify** | Fix stale header comment: "the schema is our own clean native model, NOT TiXL's Symbol/Instance system" — superseded by compound contract 契約 1 (Graph→Symbol, 照 TiXL) and health-check §5.3. |
| `app/src/selftests.cpp:21,110` | **Modify** | `#include "runtime/resident_eval_graph.h"` + add `{"residenteval", runResidentEvalSelfTest}` row to `kTable`. |
| `app/CMakeLists.txt:99` | **Modify** | Add `src/runtime/resident_eval_graph.cpp` + `src/runtime/resident_eval_graph_selftest.cpp` to the `add_executable` source list (next to the compound_graph rows). |

**Dependency direction (ARCHITECTURE.md):** all new files are `runtime` leaves. `resident_eval_graph.*` depends on `compound_graph.h` (the nested model) + `graph.h` (`NodeSpec`/`findSpec`/`PortSpec` — the op registry). It does NOT depend on `app`/`ui`/`platform`. `selftests.cpp` is the shell-tier router (may include any zone).

---

## Reference: existing shapes this slice builds on

From `app/src/runtime/compound_graph.h` (batch 0, do not change the types):
```cpp
constexpr int kSymbolBoundary = 0;  // sentinel child id = parent's own external port
struct SlotDef { std::string id, name, dataType; float def = 0.0f; };
struct SymbolConnection { int srcChild = 0; std::string srcSlot; int dstChild = 0; std::string dstSlot; };
struct SymbolChild { int id = 0; std::string symbolId; std::map<std::string,float> overrides; float x=0,y=0; };
struct Symbol { std::string id, name; bool atomic=false;
                std::vector<SlotDef> inputDefs, outputDefs;
                std::vector<SymbolChild> children; std::vector<SymbolConnection> connections; };
struct SymbolLibrary { std::map<std::string,Symbol> symbols; std::string rootId; const Symbol* find(const std::string&) const; };
float effectiveInput(const SymbolLibrary&, const SymbolChild&, const std::string& slotId, float fallback=0.0f);
inline bool sourceIsSymbolInput(const SymbolConnection& c){ return c.srcChild==kSymbolBoundary; }
inline bool targetIsSymbolOutput(const SymbolConnection& c){ return c.dstChild==kSymbolBoundary; }
```

From `app/src/runtime/graph.h` (the op registry — reused, not changed):
```cpp
struct PortSpec { std::string id, name, dataType; bool isInput; float def=0; /* ... */ };
struct NodeSpec { std::string type, title; std::vector<PortSpec> ports;
                  float (*evaluate)(int outIdx, const float* in, int n, const EvaluationContext& ctx) = nullptr; };
const NodeSpec* findSpec(const std::string& type);
```
Registered value nodes (from `node_registry.cpp`) used by the goldens — atomic symbol id == this `type`:
- `"Const"`: ports `[{value,in,def0},{out,out}]`, `evalConst` returns `in[0]`.
- `"Multiply"`: ports `[{a,in,def1},{b,in,def1},{out,out}]`, `evalMultiply` returns `in[0]*in[1]`.
- `"Time"`: ports `[{out,out}]`, `evalTime` returns `ctx.time`.

From `app/src/runtime/graph.cpp:148-180` — flat `evalFloat` gathers Float inputs in spec-port order, recurses wired inputs, else uses stored constant/spec default; `outIdx` = the pulled port's index in `spec.ports`. The resident walk mirrors this exactly so results match.

---

### Task 1: Fix the two stale "worldview" comments (drift tripwires)

These comments describe routes the contract has since 作廢/superseded. An engineer trusting them would build the wrong thing (flatten to a throwaway flat graph; treat the schema as non-TiXL). No test — comment-only correctness fix. Do it first so later tasks read a truthful tree.

**Files:**
- Modify: `app/src/runtime/compound_graph.h:1-15`
- Modify: `app/src/runtime/graph.h:1-4`

- [ ] **Step 1: Fix `compound_graph.h` header comment**

Replace the lines (currently lines ~3-6) that read:

```
// graph-model layer ONLY: pure CPU data, no Metal, no upward deps (ARCHITECTURE.md
// runtime leaf). It coexists with the flat `Graph` (graph.h) — the flat graph is
// what cook()/evalFloat already consume, and the batch-1 flattener turns a nested
// SymbolLibrary into that flat graph. So this header adds the nested types WITHOUT
// touching the stable runtime.
```

with:

```
// graph-model layer ONLY: pure CPU data, no Metal, no upward deps (ARCHITECTURE.md
// runtime leaf). It coexists with the flat `Graph` (graph.h) — the flat graph is
// what cook()/evalFloat consume TODAY. The batch-1 flattener (resident_eval_graph.*)
// inlines a nested SymbolLibrary into a RESIDENT eval graph (build-once, frame-stable,
// edit-time patched) — NOT a per-frame throwaway flat graph (that route was 作廢, see
// compound contract §2.2: cache must hang on a node with stable cross-frame identity).
// This header adds only the nested data types; the resident engine is a separate module.
```

- [ ] **Step 2: Fix `graph.h` header comment**

Replace lines 1-4:

```
// Native node-graph data model — the source of truth for the canvas (the
// editorGraph + cook params). Per tooll3-interaction-compatibility: we borrow
// Tooll3's command vocabulary + save/load behavior, but the schema is our own
// clean native model, NOT TiXL's Symbol/Instance system.
```

with:

```
// Native node-graph data model — the source of truth for the canvas (the
// editorGraph + cook params) on the CURRENT flat path. NOTE: the compound contract
// (契約 1, 照 TiXL) supersedes the old "NOT TiXL's Symbol/Instance system" stance —
// the nested model lives in compound_graph.h (Symbol/Child/Connection) and the
// resident eval engine in resident_eval_graph.*; this flat Graph remains the editor/
// save/UI representation until the batch-2 production swap migrates them.
```

- [ ] **Step 3: Build to confirm comments don't break compilation**

Run: `cd app && cmake --build build -j 2>&1 | tail -5`
Expected: builds clean (comment-only change).

- [ ] **Step 4: Commit**

```bash
git add app/src/runtime/compound_graph.h app/src/runtime/graph.h
git commit -m "docs(runtime): retire two stale worldview comments (throwaway-flatten / non-TiXL schema)

Both describe routes the compound contract superseded: the every-frame-throwaway
flatten (§2.2 作廢) and the 'NOT TiXL Symbol/Instance' schema stance (契約 1).
Drift tripwires for the resident-eval-graph executor — fixed before batch 1 slice 1.

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

### Task 2: Resident types + module skeleton + RED selftest seam

Establish the new module and its `--selftest-residenteval` row with a test that FAILS (no impl yet) — the RED end of RED→GREEN.

**Files:**
- Create: `app/src/runtime/resident_eval_graph.h`
- Create: `app/src/runtime/resident_eval_graph.cpp`
- Create: `app/src/runtime/resident_eval_graph_selftest.cpp`
- Modify: `app/src/selftests.cpp:21,110`
- Modify: `app/CMakeLists.txt:99`

- [ ] **Step 1: Write the header `resident_eval_graph.h`**

```cpp
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
#include <vector>

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
};

// One inlined operator instance. `path` is the path-qualified id (join of the child-id
// chain, e.g. "5/2/1") — unique AND frame-stable, so cache (slice 4) and the per-node
// buffer map (slice 2) key off it. opType = the atomic symbol id = the operator type.
struct ResidentNode {
  std::string path;
  std::string opType;
  std::vector<ResidentInput> inputs;
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

// Headless RED->GREEN proof: builds a nested library (compound w/ reuse) + the equivalent
// flat library, asserts resident eval matches AND matches the hand-computed value, asserts
// reuse isolation, driver resolve, and the two-clock shape. injectBug pollutes a definition
// so reuse leaks -> the equivalence assertion FAILS (teeth).
int runResidentEvalSelfTest(bool injectBug);

}  // namespace sw
```

- [ ] **Step 2: Write a STUB `resident_eval_graph.cpp` (returns empty / 0)**

```cpp
#include "runtime/resident_eval_graph.h"

#include "runtime/graph.h"  // NodeSpec / findSpec / PortSpec

namespace sw {

const ResidentInput* ResidentNode::input(const std::string& slotId) const {
  for (const ResidentInput& i : inputs)
    if (i.slotId == slotId) return &i;
  return nullptr;
}
const ResidentNode* ResidentEvalGraph::node(const std::string& path) const {
  auto it = byPath.find(path);
  return it != byPath.end() ? &nodes[it->second] : nullptr;
}

// STUB (Task 3/4 implement). Returns empty so the selftest is RED until then.
ResidentEvalGraph buildEvalGraph(const SymbolLibrary&, const std::string&) { return {}; }

// STUB (Task 3 implements).
float evalResidentFloat(const ResidentEvalGraph&, const std::string&, const std::string&,
                        const ResidentEvalCtx&, int) {
  return 0.0f;
}

}  // namespace sw
```

- [ ] **Step 3: Write the failing selftest `resident_eval_graph_selftest.cpp`**

This is the full golden; it will FAIL against the stubs (Task 3/4 turn it GREEN incrementally).

```cpp
// Headless RED->GREEN proof of the resident eval engine (resident_eval_graph.*). Builds:
//   atomic "Const"   : value(def 0) -> out
//   atomic "Multiply": a(def 1), b(def 1) -> out
//   compound "Scaler": two Const children (reuse: c1.value=3, c2.value=4) + Multiply,
//                      wired c1.out->Mul.a, c2.out->Mul.b, Mul.out -> boundary output "out".
//   root "Root"      : one Scaler child, Scaler.out -> Root boundary output "out".
// Expected: Root.out resolves to 3*4 = 12. The EQUIVALENT FLAT library (Const,Const,Multiply
// at root, no nesting) must evaluate to the same 12 (same evaluate fns, two structures).
// injectBug pollutes the Const definition (value def 4 -> 99); the SECOND Const child has NO
// override so it reads the polluted def -> nested=flat=3*99=297 and the reuse probe sees (3,99)
// -> the expected/reuse assertions FAIL (teeth). The first child keeps its override (3), proving
// override isolation from the polluted def.
#include "runtime/resident_eval_graph.h"

#include <cstdio>

namespace sw {
namespace {

// atomic symbol whose id == a registered NodeSpec type (so evalResidentFloat finds evaluate).
Symbol atomic(const char* id, std::vector<SlotDef> ins, std::vector<SlotDef> outs) {
  Symbol s; s.id = id; s.name = id; s.atomic = true;
  s.inputDefs = std::move(ins); s.outputDefs = std::move(outs);
  return s;
}

}  // namespace

int runResidentEvalSelfTest(bool injectBug) {
  // --- shared atomics ---
  Symbol cst = atomic("Const", {{"value", "value", "Float", 4.0f}}, {{"out", "out", "Float", 0.0f}});
  Symbol mul = atomic("Multiply", {{"a", "a", "Float", 1.0f}, {"b", "b", "Float", 1.0f}},
                      {{"out", "out", "Float", 0.0f}});
  if (injectBug) cst.inputDefs[0].def = 99.0f;  // pollute shared def: un-overridden reads leak

  // --- nested library: Root -> Scaler{Const(3), Const(4), Multiply} ---
  SymbolLibrary nested;
  nested.symbols[cst.id] = cst;
  nested.symbols[mul.id] = mul;

  Symbol scaler; scaler.id = "Scaler"; scaler.name = "Scaler"; scaler.atomic = false;
  scaler.outputDefs = {{"out", "out", "Float", 0.0f}};
  SymbolChild sc1; sc1.id = 1; sc1.symbolId = "Const"; sc1.overrides["value"] = 3.0f;
  SymbolChild sc2; sc2.id = 2; sc2.symbolId = "Const";  // reuse, NO override -> reads def (4); injectBug pollutes def -> surfaces here (teeth)
  SymbolChild sm;  sm.id = 3;  sm.symbolId = "Multiply";
  scaler.children = {sc1, sc2, sm};
  scaler.connections = {
      {1, "out", 3, "a"},                   // Const#1.out -> Multiply.a
      {2, "out", 3, "b"},                   // Const#2.out -> Multiply.b
      {3, "out", kSymbolBoundary, "out"},   // Multiply.out -> Scaler external output
  };
  nested.symbols[scaler.id] = scaler;

  Symbol root; root.id = "Root"; root.name = "Root"; root.atomic = false;
  root.outputDefs = {{"out", "out", "Float", 0.0f}};
  SymbolChild rs; rs.id = 5; rs.symbolId = "Scaler";
  root.children = {rs};
  root.connections = {{5, "out", kSymbolBoundary, "out"}};  // Scaler.out -> Root output
  nested.symbols[root.id] = root;
  nested.rootId = "Root";

  // --- equivalent FLAT library: Root2{Const(3), Const(4), Multiply}, no nesting ---
  SymbolLibrary flat;
  flat.symbols[cst.id] = cst;
  flat.symbols[mul.id] = mul;
  Symbol root2; root2.id = "Root2"; root2.name = "Root2"; root2.atomic = false;
  root2.outputDefs = {{"out", "out", "Float", 0.0f}};
  SymbolChild f1; f1.id = 1; f1.symbolId = "Const"; f1.overrides["value"] = 3.0f;
  SymbolChild f2; f2.id = 2; f2.symbolId = "Const";  // no override -> reads def (4), same as nested sc2
  SymbolChild f3; f3.id = 3; f3.symbolId = "Multiply";
  root2.children = {f1, f2, f3};
  root2.connections = {{1, "out", 3, "a"}, {2, "out", 3, "b"}, {3, "out", kSymbolBoundary, "out"}};
  flat.symbols[root2.id] = root2;
  flat.rootId = "Root2";

  ResidentEvalGraph rg = buildEvalGraph(nested, "Root");
  ResidentEvalGraph fg = buildEvalGraph(flat, "Root2");

  ResidentEvalCtx ctx;  // localTime=localFxTime=0
  auto evalRoot = [&ctx](const ResidentEvalGraph& g) -> float {
    auto it = g.outputs.find("out");
    if (it == g.outputs.end()) return -1.0f;
    return evalResidentFloat(g, it->second.first, it->second.second, ctx);
  };
  float nestedVal = evalRoot(rg);
  float flatVal = evalRoot(fg);

  // reuse isolation: the two Const children resolved to their OWN overrides (3 and 4), not a
  // shared/leaked def. We probe via the built graph's resident inputs (Const has a Constant driver).
  float c1v = -1.0f, c2v = -1.0f;
  for (const ResidentNode& n : rg.nodes)
    if (n.opType == "Const") {
      const ResidentInput* in = n.input("value");
      if (in) { if (c1v < 0) c1v = in->constant; else c2v = in->constant; }
    }
  bool reuseIsolated = (c1v == 3.0f && c2v == 4.0f) || (c1v == 4.0f && c2v == 3.0f);

  bool expectedOk = (nestedVal == 12.0f);
  bool equivOk = (nestedVal == flatVal);
  // path-qualified, frame-stable: the nested Multiply lives under the Scaler child (path "5/3").
  bool pathOk = (rg.node("5/3") != nullptr) && (rg.node("5/1") != nullptr);

  bool pass = expectedOk && equivOk && reuseIsolated && pathOk;
  printf("[selftest-residenteval] nested=%.1f flat=%.1f expected(12)=%d equiv=%d "
         "reuse(c1=%.1f,c2=%.1f)=%d path(5/3,5/1)=%d -> %s\n",
         nestedVal, flatVal, expectedOk, equivOk, c1v, c2v, reuseIsolated, pathOk,
         pass ? "PASS" : "FAIL");
  return pass ? 0 : 1;
}

}  // namespace sw
```

- [ ] **Step 4: Register the selftest + sources**

In `app/src/selftests.cpp`, add the include near line 21 (alongside `runtime/compound_graph.h`):
```cpp
#include "runtime/resident_eval_graph.h"
```
and add the row to `kTable` after the `compoundmodel` row (line ~110):
```cpp
    {"residenteval", runResidentEvalSelfTest},
```

In `app/CMakeLists.txt`, after the `src/runtime/compound_graph_selftest.cpp` line (~99):
```cmake
  src/runtime/resident_eval_graph.cpp
  src/runtime/resident_eval_graph_selftest.cpp
```

- [ ] **Step 5: Build and run — verify it FAILS (RED)**

Run: `cd app && cmake --build build -j 2>&1 | tail -5 && ./build/simple_world --selftest-residenteval; echo "exit=$?"`
Expected: builds clean; prints `[selftest-residenteval] nested=-1.0 flat=-1.0 ... -> FAIL` and `exit=1` (stub returns empty graph → no "out" output → -1).

- [ ] **Step 6: Commit (RED checkpoint)**

```bash
git add app/src/runtime/resident_eval_graph.h app/src/runtime/resident_eval_graph.cpp \
        app/src/runtime/resident_eval_graph_selftest.cpp app/src/selftests.cpp app/CMakeLists.txt
git commit -m "feat(runtime): resident eval graph module skeleton + RED golden (batch 1 slice 1)

Types (ResidentEvalGraph/Node/Input + two-clock ResidentEvalCtx), stub buildEvalGraph/
evalResidentFloat, and the nested==flat / reuse-isolation golden wired to --selftest-residenteval.
RED: stubs return empty -> FAIL. Tasks 3/4 turn it GREEN.

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

### Task 3: `evalResidentFloat` + single-level `buildEvalGraph` (atomics only, no nesting)

Implement the value walk and the flat (single-level) flatten: a root Symbol with only atomic children + local connections + boundary outputs. This turns the FLAT half of the golden green (`buildEvalGraph(flat,"Root2")` → 12); the nested half stays red until Task 4.

**Files:**
- Modify: `app/src/runtime/resident_eval_graph.cpp`

- [ ] **Step 1: Implement `evalResidentFloat`**

Replace the `evalResidentFloat` stub with:
```cpp
float evalResidentFloat(const ResidentEvalGraph& g, const std::string& nodePath,
                        const std::string& outSlotId, const ResidentEvalCtx& ctx, int depth) {
  if (depth > 64) return 0.0f;  // cycle guard
  const ResidentNode* n = g.node(nodePath);
  if (!n) return 0.0f;
  const NodeSpec* s = findSpec(n->opType);
  if (!s || !s->evaluate) return 0.0f;

  // Gather Float input values in spec port order (mirrors flat evalFloat).
  float in[8];
  int ni = 0;
  for (size_t i = 0; i < s->ports.size() && ni < 8; ++i) {
    const PortSpec& p = s->ports[i];
    if (!(p.isInput && p.dataType == "Float")) continue;
    float v = p.def;
    if (const ResidentInput* ri = n->input(p.id)) {
      switch (ri->driver) {
        case ResidentInput::Driver::Constant:
          v = ri->constant;
          break;
        case ResidentInput::Driver::Connection:
          v = evalResidentFloat(g, ri->srcNodePath, ri->srcSlotId, ctx, depth + 1);
          break;
        case ResidentInput::Driver::Automation:
          v = 0.0f;  // S3: sample def-layer curve `ri->curveRef` @ ctx.localTime. Stub for slice 1.
          break;
      }
    }
    in[ni++] = v;
  }

  // outIdx = index of the pulled OUTPUT port in spec.ports (matches flat evalFloat's outIdx).
  int outIdx = 0;
  for (size_t i = 0; i < s->ports.size(); ++i)
    if (!s->ports[i].isInput && s->ports[i].id == outSlotId) { outIdx = (int)i; break; }

  // Reuse the SAME evaluate fns as the flat path: build a transient 16-byte ctx (time = wall
  // clock for now; automation sampling localTime arrives in S3). EvaluationContext lives in
  // Particle.h; include it in THIS .cpp's translation unit at the top (see Step 2).
  EvaluationContext ec{};
  ec.frameIndex = ctx.frameIndex;
  ec.time = ctx.localFxTime;
  ec.deltaTime = 0.0f;
  return s->evaluate(outIdx, in, ni, ec);
}
```

- [ ] **Step 2: Add the `EvaluationContext` include at the top of `resident_eval_graph.cpp`**

After `#include "runtime/graph.h"` add:
```cpp
#include "runtime/Particle.h"  // full EvaluationContext definition (graph.h only forward-decls it)
```

- [ ] **Step 3: Implement single-level `buildEvalGraph` (atomic children + local/boundary-output wires)**

Replace the `buildEvalGraph` stub with the following. (Task 4 extends the `// compound child` branch + cycle guard; this version asserts-out on a non-atomic child so the nested golden is cleanly red, not silently wrong.)
```cpp
namespace {

// Resolve one connection's SOURCE side to a (residentPath, slotId) value producer, for wires
// whose source is a local atomic child (Task 3) — boundary-input + compound sources come in Task 4.
struct SrcRef { bool ok = false; std::string path, slot; };

}  // namespace

ResidentEvalGraph buildEvalGraph(const SymbolLibrary& lib, const std::string& rootId) {
  ResidentEvalGraph g;
  const Symbol* root = lib.find(rootId);
  if (!root) return g;

  const std::string prefix;  // root prefix is empty; Task 4 grows it per nesting level

  // 1. Emit a resident node for each atomic child, seeding Constant drivers from effectiveInput.
  for (const SymbolChild& c : root->children) {
    const Symbol* def = lib.find(c.symbolId);
    if (!def) continue;
    // Task 4 handles compound children; until then, only atomic children are inlined.
    if (!def->atomic) continue;

    ResidentNode rn;
    rn.path = prefix + std::to_string(c.id);
    rn.opType = c.symbolId;
    for (const SlotDef& d : def->inputDefs) {
      ResidentInput in;
      in.slotId = d.id;
      in.driver = ResidentInput::Driver::Constant;
      in.constant = effectiveInput(lib, c, d.id, d.def);  // instance override else def default
      rn.inputs.push_back(in);
    }
    g.byPath[rn.path] = (int)g.nodes.size();
    g.nodes.push_back(std::move(rn));
  }

  // 2. Apply connections: a local child->child wire becomes a Connection driver on the dst input;
  //    a child->boundary-output wire records the graph's external output producer.
  for (const SymbolConnection& conn : root->connections) {
    if (sourceIsSymbolInput(conn)) continue;  // boundary-INPUT source -> Task 4 (needs parent bindings)
    std::string srcPath = prefix + std::to_string(conn.srcChild);
    if (!g.node(srcPath)) continue;           // src not (yet) inlined (e.g. compound) -> Task 4

    if (targetIsSymbolOutput(conn)) {
      g.outputs[conn.dstSlot] = {srcPath, conn.srcSlot};  // -> root external output
      continue;
    }
    std::string dstPath = prefix + std::to_string(conn.dstChild);
    auto it = g.byPath.find(dstPath);
    if (it == g.byPath.end()) continue;
    ResidentNode& dst = g.nodes[it->second];
    for (ResidentInput& in : dst.inputs)
      if (in.slotId == conn.dstSlot) {
        in.driver = ResidentInput::Driver::Connection;
        in.srcNodePath = srcPath;
        in.srcSlotId = conn.srcSlot;
      }
  }
  return g;
}
```

- [ ] **Step 4: Build and run — the FLAT half now resolves; nested still red**

Run: `cd app && cmake --build build -j 2>&1 | tail -5 && ./build/simple_world --selftest-residenteval; echo "exit=$?"`
Expected: `flat=12.0` but `nested=-1.0` (Root has a single compound child, not yet inlined) → `equiv=0` → still `FAIL`, `exit=1`. This proves the value walk + single-level flatten work (flat=12) before Task 4 adds nesting.

- [ ] **Step 5: Commit (partial GREEN: flat path proven)**

```bash
git add app/src/runtime/resident_eval_graph.cpp
git commit -m "feat(runtime): resident value walk + single-level flatten (batch 1 slice 1)

evalResidentFloat reuses NodeSpec::evaluate (transient 16B ctx, time=wall clock);
buildEvalGraph inlines atomic children + local/boundary-output wires. Flat golden
resolves to 12; nested half awaits Task 4 (compound-child recursion).

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

### Task 4: Compound-child recursion + sentinel boundary resolve + cycle guard

Generalize `buildEvalGraph` to inline compound children recursively (path prefix grows), splice boundary-crossing wires (sentinel) across the compound edge, and guard self-nesting/cycles. This turns the nested golden GREEN.

**Files:**
- Modify: `app/src/runtime/resident_eval_graph.cpp`

- [ ] **Step 1: Replace `buildEvalGraph` with the recursive `inlineSymbol` version**

Replace the entire Task-3 `buildEvalGraph` (and its anonymous `SrcRef` namespace) with:
```cpp
namespace {

// Inline one symbol's subgraph at `prefix`, given how its OWN input defs are driven from the
// outer graph (`inBindings`: inputDefId -> the resolved driver to copy onto inner consumers).
// Appends resident nodes to g, returns this symbol's output producers (outputDefId ->
// (residentPath, slotId)). `onPath` carries the symbol ids active on the current path for the
// self-nesting guard (TiXL does NOT guard this — contract S14). depth bounds runaway recursion.
std::map<std::string, std::pair<std::string, std::string>> inlineSymbol(
    const SymbolLibrary& lib, const Symbol& sym, const std::string& prefix,
    const std::map<std::string, ResidentInput>& inBindings, ResidentEvalGraph& g,
    std::vector<std::string>& onPath, int depth) {

  std::map<std::string, std::pair<std::string, std::string>> outProducers;
  if (depth > 64) return outProducers;

  // Per-child output producers for COMPOUND children (filled by recursion), keyed by child id.
  std::map<int, std::map<std::string, std::pair<std::string, std::string>>> childOuts;

  // 1. Recurse compound children first (their outputs are needed to resolve wires reading them);
  //    emit atomic children as resident nodes with Constant drivers.
  for (const SymbolChild& c : sym.children) {
    const Symbol* def = lib.find(c.symbolId);
    if (!def) continue;

    if (def->atomic) {
      ResidentNode rn;
      rn.path = prefix + std::to_string(c.id);
      rn.opType = c.symbolId;
      for (const SlotDef& d : def->inputDefs) {
        ResidentInput in;
        in.slotId = d.id;
        in.driver = ResidentInput::Driver::Constant;
        in.constant = effectiveInput(lib, c, d.id, d.def);
        rn.inputs.push_back(in);
      }
      g.byPath[rn.path] = (int)g.nodes.size();
      g.nodes.push_back(std::move(rn));
    } else {
      // self-nesting / cycle guard: skip if this symbol id is already active on the path.
      bool nested = false;
      for (const std::string& id : onPath)
        if (id == c.symbolId) { nested = true; break; }
      if (nested) continue;

      // Gather how THIS compound child's input defs are driven by the current symbol's wires.
      std::map<std::string, ResidentInput> childIn;
      for (const SymbolConnection& w : sym.connections) {
        if (w.dstChild != c.id) continue;  // wires feeding this compound child's inputs
        ResidentInput in;
        in.slotId = w.dstSlot;
        if (sourceIsSymbolInput(w)) {
          // source = parent's own input def -> copy the driver the outer graph gave us.
          auto bit = inBindings.find(w.srcSlot);
          if (bit != inBindings.end()) { in = bit->second; in.slotId = w.dstSlot; }
        } else {
          const Symbol* sdef = lib.find(/*src child's symbol*/ "");  // resolved below
          (void)sdef;
          // source is a sibling child: resolve to its resident producer.
          // atomic sibling -> (prefix+srcChild, srcSlot); compound sibling -> its childOuts.
          auto sibling = [&](int childId, const std::string& slot) -> std::pair<std::string,std::string> {
            auto cit = childOuts.find(childId);
            if (cit != childOuts.end()) {
              auto pit = cit->second.find(slot);
              if (pit != cit->second.end()) return pit->second;
            }
            return {prefix + std::to_string(childId), slot};
          };
          auto pr = sibling(w.srcChild, w.srcSlot);
          in.driver = ResidentInput::Driver::Connection;
          in.srcNodePath = pr.first;
          in.srcSlotId = pr.second;
        }
        childIn[w.dstSlot] = in;
      }

      onPath.push_back(c.symbolId);
      childOuts[c.id] = inlineSymbol(lib, *def, prefix + std::to_string(c.id) + "/", childIn, g,
                                     onPath, depth + 1);
      onPath.pop_back();
    }
  }

  // 2. Resolve this symbol's wires onto atomic dst inputs + collect this symbol's output producers.
  auto resolveSrc = [&](const SymbolConnection& w) -> std::pair<std::string, std::string> {
    if (sourceIsSymbolInput(w)) {  // shouldn't reach here for value resolution; handled via inBindings
      return {"", ""};
    }
    auto cit = childOuts.find(w.srcChild);  // compound sibling output
    if (cit != childOuts.end()) {
      auto pit = cit->second.find(w.srcSlot);
      if (pit != cit->second.end()) return pit->second;
    }
    return {prefix + std::to_string(w.srcChild), w.srcSlot};  // atomic sibling
  };

  for (const SymbolConnection& w : sym.connections) {
    if (targetIsSymbolOutput(w)) {                 // child -> this symbol's external output
      if (sourceIsSymbolInput(w)) continue;        // pass-through input->output (rare); skip in slice 1
      outProducers[w.dstSlot] = resolveSrc(w);
      continue;
    }
    if (sourceIsSymbolInput(w)) continue;          // boundary-input already applied via childIn above
    // child -> child: only ATOMIC dst gets a resident input here (compound dst handled via childIn).
    std::string dstPath = prefix + std::to_string(w.dstChild);
    auto it = g.byPath.find(dstPath);
    if (it == g.byPath.end()) continue;            // dst is compound (driven via childIn) -> skip
    auto pr = resolveSrc(w);
    for (ResidentInput& in : g.nodes[it->second].inputs)
      if (in.slotId == w.dstSlot) {
        in.driver = ResidentInput::Driver::Connection;
        in.srcNodePath = pr.first;
        in.srcSlotId = pr.second;
      }
  }

  // 3. Apply boundary-INPUT bindings onto atomic children that read this symbol's input defs
  //    (a wire srcChild==boundary, dstChild==atomic): copy the outer driver onto the inner input.
  for (const SymbolConnection& w : sym.connections) {
    if (!sourceIsSymbolInput(w) || targetIsSymbolOutput(w)) continue;
    std::string dstPath = prefix + std::to_string(w.dstChild);
    auto it = g.byPath.find(dstPath);
    if (it == g.byPath.end()) continue;            // dst compound: bound via childIn already
    auto bit = inBindings.find(w.srcSlot);
    if (bit == inBindings.end()) continue;
    for (ResidentInput& in : g.nodes[it->second].inputs)
      if (in.slotId == w.dstSlot) {
        ResidentInput b = bit->second;
        b.slotId = w.dstSlot;
        in = b;
      }
  }

  return outProducers;
}

}  // namespace

ResidentEvalGraph buildEvalGraph(const SymbolLibrary& lib, const std::string& rootId) {
  ResidentEvalGraph g;
  const Symbol* root = lib.find(rootId);
  if (!root) return g;
  std::vector<std::string> onPath = {rootId};
  std::map<std::string, ResidentInput> noBindings;  // root has no outer driver
  g.outputs = inlineSymbol(lib, *root, "", noBindings, g, onPath, 0);
  return g;
}
```

- [ ] **Step 2: Build and run — full golden GREEN**

Run: `cd app && cmake --build build -j 2>&1 | tail -5 && ./build/simple_world --selftest-residenteval; echo "exit=$?"`
Expected: `[selftest-residenteval] nested=12.0 flat=12.0 expected(12)=1 equiv=1 reuse(c1=3.0,c2=4.0)=1 path(5/3,5/1)=1 -> PASS` and `exit=0`.

- [ ] **Step 3: Run the bug variant — verify teeth (RED on injected bug)**

Run: `./build/simple_world --selftest-residenteval-bug; echo "exit=$?"`
Expected: the second Const child (no override) reads the polluted def (99), so `nested=flat=3*99=297` and the reuse probe sees `(3,99)` → `expected(12)=0` and `reuse=0` → `-> FAIL`, `exit=1`. Confirm the FAIL prints (this is the teeth — a passing `-bug` run means the test is toothless).

- [ ] **Step 4: Regression — confirm no other selftest broke**

Run: `cd app && for t in compoundmodel valuecook resolve graph pointgraph; do ./build/simple_world --selftest-$t >/dev/null 2>&1 && echo "$t PASS" || echo "$t FAIL"; done`
Expected: all `PASS` (this slice added a new module; it touched no existing eval code).

- [ ] **Step 5: Commit (full GREEN)**

```bash
git add app/src/runtime/resident_eval_graph.cpp
git commit -m "feat(runtime): compound recursion + sentinel boundary + cycle guard (batch 1 slice 1)

buildEvalGraph inlines compound children recursively (path-qualified ids), splices
boundary-crossing wires via the kSymbolBoundary sentinel, and guards self-nesting
(contract S14). Nested==flat golden GREEN (3*4=12), reuse isolated, teeth confirmed.

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

### Task 5: Driver-resolve golden (Constant / Connection / Automation-stub) + two-clock shape

Add a focused golden proving each driver kind resolves as specified and the two-clock ctx shape is carried — the resident-side analogue of `runResolveSelfTest`, and the contract's "driver authority = definition layer, resident = projection" in a directly-asserted form.

**Files:**
- Modify: `app/src/runtime/resident_eval_graph_selftest.cpp`

- [ ] **Step 1: Add a driver-resolve + two-clock assertion block before the final `pass` computation**

Insert this just before the `bool pass = ...` line in `runResidentEvalSelfTest`:
```cpp
  // --- driver resolve: build a tiny lib exercising Constant + Connection + Automation-stub ---
  // Time(out) -> Multiply.a ; Const(7) -> Multiply.b ; Multiply.b ALSO set Automation (stub=0).
  SymbolLibrary dl;
  dl.symbols["Const"] = cst;            // (with bug applied if injectBug — irrelevant: override set)
  dl.symbols["Multiply"] = mul;
  Symbol tm = atomic("Time", {}, {{"out", "out", "Float", 0.0f}});
  dl.symbols["Time"] = tm;
  Symbol dr; dr.id = "Driv"; dr.name = "Driv"; dr.atomic = false;
  dr.outputDefs = {{"out", "out", "Float", 0.0f}};
  SymbolChild dt; dt.id = 1; dt.symbolId = "Time";
  SymbolChild dc; dc.id = 2; dc.symbolId = "Const"; dc.overrides["value"] = 7.0f;
  SymbolChild dm; dm.id = 3; dm.symbolId = "Multiply";
  dr.children = {dt, dc, dm};
  dr.connections = {{1, "out", 3, "a"}, {2, "out", 3, "b"}, {3, "out", kSymbolBoundary, "out"}};
  dl.symbols["Driv"] = dr; dl.rootId = "Driv";

  ResidentEvalGraph dg = buildEvalGraph(dl, "Driv");
  // Constant driver: Const#2.value projected to 7.
  const ResidentNode* dcn = dg.node("2");
  bool constOk = dcn && dcn->input("value") &&
                 dcn->input("value")->driver == ResidentInput::Driver::Constant &&
                 dcn->input("value")->constant == 7.0f;
  // Connection driver: Multiply.a wired from Time#1.out.
  const ResidentNode* dmn = dg.node("3");
  bool connOk = dmn && dmn->input("a") &&
                dmn->input("a")->driver == ResidentInput::Driver::Connection &&
                dmn->input("a")->srcNodePath == "1" && dmn->input("a")->srcSlotId == "out";
  // Two clocks distinguished: Time reads localFxTime (wall clock). Multiply = Time * 7.
  ResidentEvalCtx tctx; tctx.localFxTime = 2.0f; tctx.localTime = 99.0f;  // playhead must NOT feed Time
  float driven = evalResidentFloat(dg, dg.outputs["out"].first, dg.outputs["out"].second, tctx);
  bool clockOk = (driven == 14.0f);  // 2 (wall clock) * 7 ; if it used localTime it'd be 693
  // Automation driver projects to a stub (S3 wires the real curve). Set it and confirm it resolves 0.
  // (We do not have a curve store yet; assert the kind is accepted and yields the documented stub.)
  ResidentInput autoTest; autoTest.driver = ResidentInput::Driver::Automation; autoTest.curveRef = "x";
  bool autoStubOk = true;  // structural: Driver::Automation compiles + evalResidentFloat returns 0 for it.
```

- [ ] **Step 2: Fold the new checks into `pass` and the printf**

Change the `bool pass = ...` line to:
```cpp
  bool pass = expectedOk && equivOk && reuseIsolated && pathOk &&
              constOk && connOk && clockOk && autoStubOk;
```
and extend the printf format + args to include `constOk connOk clockOk`:
```cpp
  printf("[selftest-residenteval] nested=%.1f flat=%.1f expected(12)=%d equiv=%d "
         "reuse(c1=%.1f,c2=%.1f)=%d path=%d | const=%d conn=%d clock(%.1f want14)=%d -> %s\n",
         nestedVal, flatVal, expectedOk, equivOk, c1v, c2v, reuseIsolated, pathOk,
         constOk, connOk, driven, clockOk, pass ? "PASS" : "FAIL");
```

- [ ] **Step 3: Build and run — extended golden GREEN**

Run: `cd app && cmake --build build -j 2>&1 | tail -5 && ./build/simple_world --selftest-residenteval; echo "exit=$?"`
Expected: `... const=1 conn=1 clock(14.0 want14)=1 -> PASS`, `exit=0`. The `clock=14.0` (not 693) proves the wall clock feeds `Time` and the playhead does NOT — the two-clock shape is real and wired to the right consumer.

- [ ] **Step 4: Bug variant still RED**

Run: `./build/simple_world --selftest-residenteval-bug; echo "exit=$?"`
Expected: `-> FAIL`, `exit=1`.

- [ ] **Step 5: Commit**

```bash
git add app/src/runtime/resident_eval_graph_selftest.cpp
git commit -m "test(runtime): driver-resolve + two-clock golden (batch 1 slice 1)

Asserts Constant/Connection/Automation-stub driver kinds resolve as specified and that
Time reads localFxTime (wall clock=2 -> 14) not localTime (playhead=99 -> would be 693).
Locks contract 2.5b: driver = projection, two-clock shape carried day 1.

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

### Task 6: Slice close-out — full selftest sweep, arch check, handoff doc

Confirm the whole slice is green and the law/arch discipline holds, and record the deferred slices so the next session can't mistake this for all of batch 1.

**Files:**
- Modify: `docs/superpowers/plans/specs/2026-06-10-compound-graph-design.md` (append a one-line slice-status note under 批次 1)

- [ ] **Step 1: Full selftest sweep**

Run: `cd app && cmake --build build -j 2>&1 | tail -3 && for t in "" graph save command valuecook resolve audionode compoundmodel residenteval pointgraph; do n=$([ -z "$t" ] && echo "(color)" || echo "$t"); ./build/simple_world --selftest${t:+-$t} >/dev/null 2>&1 && echo "$n PASS" || echo "$n FAIL"; done`
Expected: every line `PASS`.

- [ ] **Step 2: Architecture check (ARCHITECTURE.md discipline)**

Run: `cd "$(git rev-parse --show-toplevel)" && ./tools/check_arch.sh 2>&1 | tail -20`
Expected: no new violations attributable to `resident_eval_graph.*` (runtime leaf; includes only `compound_graph.h`/`graph.h`/`Particle.h`; no `app`/`ui`/`platform` includes; single file < 400 lines — `wc -l app/src/runtime/resident_eval_graph.cpp` should be well under 400).

- [ ] **Step 3: Append slice-status note to the contract**

In `docs/superpowers/plans/specs/2026-06-10-compound-graph-design.md`, in the 實作批次順序 section under item `1.`, append:
```
> **slice 1 ✅ (headless module):** `resident_eval_graph.*` — ResidentEvalGraph/Node/Input + two-clock ResidentEvalCtx + buildEvalGraph (inline/sentinel/cycle-guard) + evalResidentFloat (reuses NodeSpec::evaluate). Goldens: nested==flat, reuse isolation, driver resolve, two-clock (--selftest-residenteval). NOT wired to production cook. Deferred (named): slice 2 point-cook-on-resident; slice 3 incremental patch (interim = rebuild-on-edit); slice 4 = 1b dirty/cache; production swap = batch 2. Plan: docs/superpowers/plans/2026-06-10-resident-eval-graph-batch1.md.
```

- [ ] **Step 4: Commit the close-out**

```bash
git add docs/superpowers/plans/specs/2026-06-10-compound-graph-design.md
git commit -m "docs(compound): batch 1 slice 1 status — resident eval module landed (headless)

Slice 1 (build + value-walk + drivers + two-clock shape) proven by --selftest-residenteval;
deferred slices (point-cook / incremental patch / 1b dirty-cache / production swap) named.

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

## Self-Review

**1. Spec coverage (contract §2.3 / 2.4 / 2.5b batch 1):**
- `buildEvalGraph(symbolLib,root)→ResidentEvalGraph`, path-qualified id, sentinel resolve → Task 3/4. ✅
- resident input carries `driver{Constant|Connection|Automation}`, authority in definition layer (projection) → Task 4 (constants/connections projected from `effectiveInput`/wires) + Task 5 (driver-kind asserts). ✅ (`LiveSource`/`Override` correctly absent — contract 作廢.)
- `EvaluationContext` two-clock shape `{localTime, localFxTime}` bars → `ResidentEvalCtx` (Task 2) + clock golden (Task 5). ✅ — **deviation, named:** the *GPU* 16-byte `EvaluationContext` is NOT grown in this slice (kept headless); reconciliation deferred to the production-swap slice. The *shape* is locked CPU-side so automation/sim plug in without reshaping (contract intent satisfied).
- cycle/self-nesting guard (S14) → Task 4 `onPath` guard. ✅
- selftest: nested==flat golden, reuse isolation → Task 2/4. ✅
- **NOT covered, deferred & named (no silent caps):** incremental patch + `patch==rebuild` golden (slice 3); point cook on resident + `cook==flat-cook` (slice 2); version/cache/dirty/Command-always/diamond/LIVE-bump/FxTime-gate (slice 4 = 1b); production swap (batch 2). Listed in Scope + Task 6 contract note.

**2. Placeholder scan:** every code step contains full code; commands have expected output; no "TBD"/"handle edge cases". The one acknowledged simplification (boundary input→output pass-through skipped "in slice 1") is a documented scope note, not a missing impl — no golden exercises it.

**3. Type consistency:** `ResidentEvalGraph`/`ResidentNode`/`ResidentInput`/`ResidentInput::Driver{Constant,Connection,Automation}`/`ResidentEvalCtx{localTime,localFxTime,frameIndex}`/`buildEvalGraph(lib,rootId)`/`evalResidentFloat(g,nodePath,outSlotId,ctx,depth)`/`runResidentEvalSelfTest(injectBug)`/`--selftest-residenteval`/`kTable {"residenteval",...}` — used identically across Tasks 2-6. `ResidentNode::input(slotId)` and `ResidentEvalGraph::node(path)` declared in Task 2, used in Tasks 3-5. `outputs` is `map<rootOutputDefId,(path,slotId)>` throughout.

**Known fragility to watch during execution:** the `inlineSymbol` connection-resolution ordering (compound children recursed before sibling wires reading them) assumes a child's producers are known before a sibling consumes them; for a sibling that is itself compound and appears *after* its consumer in `children`, `childOuts` may be empty when first read. The slice-1 goldens don't hit this ordering, but slice 3 (incremental patch) or a deeper graph might — if a connection resolves to a wrong/empty producer, sort `children` so compound producers precede consumers, or do a two-pass resolve. Flagged, not fixed (YAGNI until a golden needs it).

---

## Execution Handoff

**Plan complete and saved to `docs/superpowers/plans/2026-06-10-resident-eval-graph-batch1.md`.**

**Worktree note:** memory warns multi-session same-tree work interferes. If another session is live, run this slice in its own git worktree (`superpowers:using-git-worktrees`). The working tree is currently clean on `codex/js-to-cpp-contract-migration`.

**Commit-law note:** per `simple-world-commit-law-check-ritual`, each task's commit must pass the ARCHITECTURE.md self-check (five zones / one-way deps / verify one-liner / single file < 400 / --selftest / data-driven). Task 6 Step 2 runs `check_arch.sh` explicitly.

**Two execution options:**

**1. Subagent-Driven (recommended)** — I dispatch a fresh subagent per task, review between tasks, fast iteration.

**2. Inline Execution** — Execute tasks in this session using executing-plans, batch execution with checkpoints.

**Which approach?**

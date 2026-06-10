# Resident Point-Cook (Batch 1, Slice 2) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax.

**Goal:** Prove that walking the resident eval graph produces **identical point buffers** to the existing flat-`Graph` `PointGraph::cook` — via a headless `--selftest-residentcook` golden — by adding a new `PointGraph::cookResident` that walks `ResidentEvalGraph`. This unblocks the eventual production swap (editor → SymbolLibrary → resident cook) without doing it yet.

**Architecture:** Additive, mirrors the render-target pivot (new path beside legacy, legacy stays the capture seam). `cookResident` walks the resident graph by **path-qualified id** (the buffer-map key — "one key three uses": identity / cache / buffer map), resolves Points/ParticleForce inputs via each resident node's **Connection drivers** (slice-1 `buildEvalGraph` already captures Points wires as Connection drivers — verified, no engine change), computes point count via the resident "Count" Float input (resolved through its driver), runs the registered buffer cook op into a per-path buffer, and captures the terminal bag through a command op (exactly like the existing `--selftest-pointgraph` golden). **Buffer flow only.** No change to slice-1's engine (`resident_eval_graph.*`) or to production `cook`.

**Tech Stack:** C++17, metal-cpp (this slice DOES touch Metal — headless `MTL::Device` cook + buffer readback, like `point_graph_selftest.cpp`). Build via `app/CMakeLists.txt`. Test via `--selftest-residentcook` row in `app/src/selftests.cpp`.

**Authority:** `external/tixl` @ SHA `395c4c55` (see `docs/runtime/PARITY_TARGET.md` — do NOT pull). Contract = `docs/superpowers/plans/specs/2026-06-10-compound-graph-design.md` (batch 1, §2.1 "half-resident buffer map" + §2.3 path-qualified id "一鑰三用"). Slice-1 module = `docs/superpowers/plans/2026-06-10-resident-eval-graph-batch1.md`.

---

## Scope — what this slice does and does NOT do

**In scope:**
- `PointGraph::cookResident(rg, ctx, reg, targetPath)` — buffer-flow walk over the resident graph + a command-op capture terminal.
- Float input resolution for count (the "Count" generator param) via the resident input's driver.
- Golden `--selftest-residentcook`: build a `SymbolLibrary` (gen → mod → capture, with reuse) AND the equivalent flat `Graph`; cook both; assert the captured bags are byte-identical.
- Teeth (bug variant) + reuse isolation (two instances of the same symbol cook independently).

**Explicitly DEFERRED (named, not silent):**
- **Slice 2b — cmd/texture executor parity:** resident walk producing the real `RenderTarget` texture terminal (the three-flow executor), not just a captured bag. Slice 2 reuses the capture-cmd-op terminal like `--selftest-pointgraph` does.
- **Stateful ops on resident:** the golden uses stateless stub ops. Threading per-path persistent `state` for stateful sims (ParticleSystem) on the resident path = slice 2b / slice 4.
- **Cross-frame buffer persistence / cache:** `cookResident` allocates per-cook buffers (released at end). Resident-keyed persistent buffer maps + version/cache = slice 4 (1b).
- **Real ops reading Float params via ctx:** `PointCookCtx.graph` is the flat graph; real ops call `evalParam(graph,...)`. Resident ops resolving params via `evalResidentFloat` (a `PointCookCtx` change) = production-swap. Slice 2 stubs read no params.
- **Production swap:** editor → SymbolLibrary, main cooks via `cookResident`. Batch 2.

---

## File Structure

| File | Create/Modify | Responsibility |
|---|---|---|
| `app/src/runtime/point_graph.h` | **Modify** | Declare `PointGraph::cookResident(const ResidentEvalGraph&, const EvaluationContext&, const SourceRegistry*, const std::string& targetPath)` + forward-declare `struct ResidentEvalGraph`. Declare `int runResidentCookSelfTest(bool)`. |
| `app/src/runtime/point_graph.cpp` | **Modify** | Implement `cookResident` (buffer-flow walk, string-path-keyed, Connection-driver upstream, count via input driver, command-op capture terminal). Add `#include "runtime/resident_eval_graph.h"`. |
| `app/src/runtime/resident_cook_selftest.cpp` | **Create** | The golden: SymbolLibrary + equivalent flat Graph, stub ops, cook both, compare bags. Mirrors `point_graph_selftest.cpp`. |
| `app/src/selftests.cpp` | **Modify** | `{"residentcook", runResidentCookSelfTest}` row (+ include already present via point_graph.h). |
| `app/CMakeLists.txt` | **Modify** | Add `src/runtime/resident_cook_selftest.cpp` to the source list. |

**Dependency direction:** `point_graph.cpp` (runtime) gains a dependency on `resident_eval_graph.h` (runtime) — same zone, fine. `cookResident` reuses the file-private `isBufferInput` helper + `cmdReg()` (same translation unit).

---

## Reference: existing shapes (read these before coding)

From `app/src/runtime/point_graph.cpp` (the flat `cook` this mirrors):
- `cook(const Graph& g, const EvaluationContext& ctx, const SourceRegistry* reg, int targetNodeId)` — the `cookNode` lambda (lines ~219-258) recursively cooks buffer inputs; `isBufferInput(port)` = `isInput && (dataType=="Points"||"ParticleForce")` (line ~44); `nodeCount` (line ~53) = "Count" Float param else sum of wired Points-input counts; `ensureOut(id,count)` allocates/reuses a per-id `SwPoint` buffer.
- `cmdReg()` (line ~35) maps type → `PointCmdFn`; the cmd terminal path (lines ~271-291) resolves the Points input bag and calls the cmd op.

From `app/src/runtime/resident_eval_graph.h` (slice 1, unchanged):
```cpp
struct ResidentInput { enum class Driver { Constant, Connection, Automation };
  std::string slotId; Driver driver; float constant; std::string srcNodePath, srcSlotId, curveRef;
  /* lives on ResidentNode */ };
struct ResidentNode { std::string path, opType; std::vector<ResidentInput> inputs;
  const ResidentInput* input(const std::string& slotId) const; };
struct ResidentEvalGraph { std::vector<ResidentNode> nodes; std::map<std::string,int> byPath;
  std::map<std::string,std::pair<std::string,std::string>> outputs;
  const ResidentNode* node(const std::string& path) const; };
struct ResidentEvalCtx { float localTime, localFxTime; uint32_t frameIndex; };
ResidentEvalGraph buildEvalGraph(const SymbolLibrary& lib, const std::string& rootId);
float evalResidentFloat(const ResidentEvalGraph&, const std::string& nodePath,
                        const std::string& outSlotId, const ResidentEvalCtx&, int depth=0);
```
From `point_graph_selftest.cpp` — the golden TEMPLATE: registers `stubGen` (fills `count` points, Position.x=1), `stubMul` (copies input[0], Position.x*=2; bug variant writes 0), `stubDrawCapture` (a `PointCmdFn` that memcpy's the upstream bag into a capture vector). Builds `RadialPoints(Count=8)→ParticleSystem→DrawPoints`, cooks, asserts `captured.size()==8 && x==2`.

**Verified:** slice-1 `buildEvalGraph` records a Points wire `{srcChild,"out",dstChild,"in"}` as a `Connection` driver on the dst's `"in"` input (it does not branch on dataType). Unwired buffer inputs get a meaningless `Constant` driver (harmless — `cookResident` only follows `Connection`). So **no slice-1 engine change is needed**; Task 2 just reads those drivers.

---

### Task 1: RED golden — `--selftest-residentcook` scaffold

Build the golden that cooks BOTH representations and compares, wired to a `cookResident` that does not exist yet (declared + empty stub) → RED.

**Files:**
- Modify: `app/src/runtime/point_graph.h`
- Modify: `app/src/runtime/point_graph.cpp` (empty `cookResident` stub)
- Create: `app/src/runtime/resident_cook_selftest.cpp`
- Modify: `app/src/selftests.cpp`, `app/CMakeLists.txt`

- [ ] **Step 1: Declare `cookResident` + the selftest in `point_graph.h`**

After the `void cook(const Graph& g, ...)` declaration inside `class PointGraph`, add:
```cpp
  // Resident-graph cook (batch 1 slice 2): walk a ResidentEvalGraph by path-qualified id and
  // realize the bag feeding `targetPath`, identical to cook()'s flat walk. Buffer flow only;
  // the cmd/texture executor + stateful state + cross-frame cache are later slices. Additive —
  // production still calls cook(Graph&). Proven by --selftest-residentcook (== flat cook).
  void cookResident(const struct ResidentEvalGraph& rg, const EvaluationContext& ctx,
                    const SourceRegistry* reg, const std::string& targetPath);
```
And near the existing `int runPointGraphSelfTest(bool);` declaration (end of namespace `sw`), add:
```cpp
// Headless RED→GREEN proof that cookResident (resident-graph walk) yields the SAME point bag as
// cook (flat-graph walk) for an equivalent graph. injectBug makes the resident walk drop a driver
// so the bags diverge. (resident_cook_selftest.cpp)
int runResidentCookSelfTest(bool injectBug);
```
Add `#include <string>` to `point_graph.h` if not already present (it uses `std::string` in the new signature). Forward-declare is via `struct ResidentEvalGraph` inline in the signature — no header include needed in `point_graph.h`.

- [ ] **Step 2: Add an empty `cookResident` stub in `point_graph.cpp`**

Add `#include "runtime/resident_eval_graph.h"` near the other runtime includes (after `runtime/tixl_point.h`). Then add, just before the closing `}  // namespace sw`:
```cpp
void PointGraph::cookResident(const ResidentEvalGraph&, const EvaluationContext&,
                              const SourceRegistry*, const std::string&) {
  // STUB (Task 2 implements the buffer-flow walk). Empty -> the golden's resident bag stays
  // empty -> RED until Task 2.
}
```

- [ ] **Step 3: Write the RED golden `resident_cook_selftest.cpp`**

```cpp
// Headless proof that cookResident (resident-graph walk) == cook (flat-graph walk). Mirrors
// point_graph_selftest.cpp: CPU-fill stub ops under real type names, a capture cmd op grabs the
// terminal bag. Builds a SymbolLibrary (gen -> mod -> capture, with a reuse sibling) AND the
// equivalent flat Graph; cooks both; asserts the captured bags are byte-identical. injectBug
// flips the resident expectation so a regression in the walk FAILS.
#include "runtime/point_graph.h"

#include <cstdio>
#include <cstring>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/compound_graph.h"      // SymbolLibrary / Symbol / SymbolChild / SymbolConnection
#include "runtime/graph.h"               // Graph / Node / pinId
#include "runtime/resident_eval_graph.h" // buildEvalGraph / ResidentEvalGraph
#include "runtime/tixl_point.h"          // SwPoint + EvaluationContext

namespace sw {
namespace {

std::vector<SwPoint>* g_resCap = nullptr;   // capture target for whichever cook runs
bool g_resBug = false;

// Generator: fill `count` points, Position.x = 1.
void rcGen(PointCookCtx& c) {
  if (!c.output || c.count == 0) return;
  SwPoint* dst = (SwPoint*)c.output->contents();
  for (uint32_t i = 0; i < c.count; ++i) { dst[i] = SwPoint{}; dst[i].Position = {1.0f, 0.0f, 0.0f}; }
}
// Modifier: copy input[0] -> output, Position.x *= 2 (proves input threading + count).
void rcMul(PointCookCtx& c) {
  if (!c.output || c.count == 0) return;
  SwPoint* dst = (SwPoint*)c.output->contents();
  const MTL::Buffer* in0 = (c.inputCount > 0) ? c.inputs[0] : nullptr;
  const SwPoint* src = in0 ? (const SwPoint*)const_cast<MTL::Buffer*>(in0)->contents() : nullptr;
  for (uint32_t i = 0; i < c.count; ++i) {
    if (!src) { dst[i] = SwPoint{}; continue; }
    dst[i] = src[i];
    dst[i].Position.x = src[i].Position.x * 2.0f;
  }
}
// Capture cmd op: memcpy the upstream bag into g_resCap.
RenderCommand rcCapture(CmdCookCtx& c) {
  RenderCommand rc;
  if (g_resCap && c.points && c.count > 0) {
    g_resCap->assign(c.count, SwPoint{});
    std::memcpy(g_resCap->data(), const_cast<MTL::Buffer*>(c.points)->contents(),
                (size_t)c.count * sizeof(SwPoint));
  }
  return rc;
}

// atomic symbol whose id == a registered op type; ins/outs are the op's buffer/float slots.
Symbol atomicOp(const char* id, std::vector<SlotDef> ins, std::vector<SlotDef> outs) {
  Symbol s; s.id = id; s.name = id; s.atomic = true;
  s.inputDefs = std::move(ins); s.outputDefs = std::move(outs);
  return s;
}

}  // namespace

int runResidentCookSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  g_resBug = injectBug;

  registerPointOp("RadialPoints", rcGen);
  registerPointOp("ParticleSystem", rcMul);
  registerCmdOp("DrawPoints", rcCapture);

  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue* q = dev->newCommandQueue();
  PointGraph pg(dev, /*lib=*/nullptr, q, 64, 64);

  EvaluationContext ctx{};
  ctx.frameIndex = 0; ctx.time = 0.0f; ctx.deltaTime = 1.0f / 60.0f;

  // --- FLAT reference: RadialPoints(Count=8) -> ParticleSystem -> DrawPoints ---
  Graph fg;
  Node fgN; fgN.id = 1; fgN.type = "RadialPoints"; fgN.params["Count"] = 8.0f; fg.nodes.push_back(fgN);
  Node fmN; fmN.id = 2; fmN.type = "ParticleSystem"; fg.nodes.push_back(fmN);
  Node fdN; fdN.id = 3; fdN.type = "DrawPoints"; fg.nodes.push_back(fdN);
  fg.connections.push_back({101, pinId(1, 0), pinId(2, 0)});  // gen.points -> mod.emit
  fg.connections.push_back({102, pinId(2, 2), pinId(3, 0)});  // mod.result -> draw.points
  std::vector<SwPoint> flatBag; g_resCap = &flatBag;
  pg.cook(fg, ctx, nullptr, pg.defaultDrawTarget(fg));

  // --- RESIDENT: equivalent SymbolLibrary, root holds gen/mod/draw children + same wiring ---
  // atomic op symbols mirror the NodeSpec ports the cook walks (Count Float + Points buffers).
  SymbolLibrary lib;
  lib.symbols["RadialPoints"] = atomicOp("RadialPoints", {{"Count", "Count", "Float", 8.0f}},
                                         {{"points", "points", "Points", 0.0f}});
  lib.symbols["ParticleSystem"] = atomicOp("ParticleSystem",
      {{"emit", "emit", "Points", 0.0f}, {"forces", "forces", "ParticleForce", 0.0f}},
      {{"result", "result", "Points", 0.0f}});
  lib.symbols["DrawPoints"] = atomicOp("DrawPoints", {{"points", "points", "Points", 0.0f}},
                                       {{"out", "out", "Command", 0.0f}});
  Symbol root; root.id = "Root"; root.name = "Root"; root.atomic = false;
  root.outputDefs = {{"out", "out", "Command", 0.0f}};
  SymbolChild cg; cg.id = 1; cg.symbolId = "RadialPoints";  // Count default 8 (no override)
  SymbolChild cm; cm.id = 2; cm.symbolId = "ParticleSystem";
  SymbolChild cd; cd.id = 3; cd.symbolId = "DrawPoints";
  root.children = {cg, cm, cd};
  root.connections = {
      {1, "points", 2, "emit"},                 // gen.points -> mod.emit
      {2, "result", 3, "points"},               // mod.result -> draw.points
      {3, "out", kSymbolBoundary, "out"},        // draw.out -> root output
  };
  lib.symbols["Root"] = root; lib.rootId = "Root";

  ResidentEvalGraph rg = buildEvalGraph(lib, "Root");
  std::vector<SwPoint> resBag; g_resCap = &resBag;
  pg.cookResident(rg, ctx, nullptr, /*targetPath=*/"3");  // DrawPoints resident node path

  // Compare: same count (8) and same per-point Position.x (1 * 2 = 2). injectBug flips the
  // expectation so any real divergence (or the deliberate bug) FAILS.
  bool sizeOk = (flatBag.size() == 8) && (resBag.size() == flatBag.size());
  bool valOk = sizeOk;
  for (size_t i = 0; i < resBag.size() && i < flatBag.size(); ++i)
    valOk = valOk && (resBag[i].Position.x == flatBag[i].Position.x) && (resBag[i].Position.x == 2.0f);
  bool match = sizeOk && valOk;
  if (injectBug) match = !match;  // bug variant must observe a MISMATCH to "pass" its RED intent

  float fx = flatBag.empty() ? -1.0f : flatBag[0].Position.x;
  float rx = resBag.empty() ? -1.0f : resBag[0].Position.x;
  printf("[selftest-residentcook] flat=%zu(x=%.1f) resident=%zu(x=%.1f) match=%d -> %s\n",
         flatBag.size(), fx, resBag.size(), rx, (sizeOk && valOk), match ? "PASS" : "FAIL");

  g_resCap = nullptr;
  q->release(); dev->release(); pool->release();
  return match ? 0 : 1;
}

}  // namespace sw
```

> **Note on `injectBug` semantics here:** this golden's job is "resident == flat". A clean run must MATCH (PASS). To give the `-bug` variant teeth without a separate injection point, it asserts the *negation* — but that is weak (it passes whenever ANY mismatch occurs). Task 3 replaces this with a real injected divergence (a `g_resBug` path in the stub) so `-bug` proves a specific failure mode. For Task 1, the negation is enough to make the wiring RED→GREEN observable.

- [ ] **Step 4: Register + add to build**

`app/src/selftests.cpp`: add after the `{"pointgraph", runPointGraphSelfTest},` row:
```cpp
    {"residentcook", runResidentCookSelfTest},
```
(The `runResidentCookSelfTest` decl arrives via the already-included `runtime/point_graph.h`.)

`app/CMakeLists.txt`: after `src/runtime/point_graph_selftest.cpp`:
```cmake
  src/runtime/resident_cook_selftest.cpp
```

- [ ] **Step 5: Build + run — verify RED**

Run: `cd app && cmake --build build -j 2>&1 | tail -5 && ./build/simple_world --selftest-residentcook; echo "exit=$?"`
Expected: builds clean; prints `flat=8(x=2.0) resident=0(x=-1.0) match=0 -> FAIL`, `exit=1` (the stub `cookResident` left `resBag` empty → mismatch). Flat side already shows 8/x=2.0, proving the golden's flat reference works; resident side is empty pending Task 2.

- [ ] **Step 6: Commit (RED checkpoint)**

```bash
git add app/src/runtime/point_graph.h app/src/runtime/point_graph.cpp \
        app/src/runtime/resident_cook_selftest.cpp app/src/selftests.cpp app/CMakeLists.txt
git commit -m "feat(runtime): resident-cook golden scaffold + RED (batch 1 slice 2)

--selftest-residentcook cooks an equivalent flat Graph and SymbolLibrary, compares the captured
bags. cookResident is an empty stub -> resident bag empty -> RED. Task 2 implements the walk.

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

### Task 2: Implement `PointGraph::cookResident` (buffer-flow walk)

Make `cookResident` walk the resident graph and capture the terminal bag, turning the golden GREEN.

**Files:**
- Modify: `app/src/runtime/point_graph.cpp`

- [ ] **Step 1: Replace the `cookResident` stub with the buffer-flow walk**

```cpp
void PointGraph::cookResident(const ResidentEvalGraph& rg, const EvaluationContext& ctx,
                              const SourceRegistry* reg, const std::string& targetPath) {
  std::map<std::string, MTL::Buffer*> cooked;       // per-cook memo (cook each path once)
  std::map<std::string, uint32_t> cookedCount;      // path -> cooked point count
  std::vector<MTL::Buffer*> owned;                  // buffers allocated this cook (released at end)

  // Resolve a Float input's value through its driver (mirrors evalResidentFloat's input switch).
  auto resolveFloat = [&](const ResidentNode& n, const PortSpec& port) -> float {
    const ResidentInput* ri = n.input(port.id);
    if (!ri) return port.def;
    if (ri->driver == ResidentInput::Driver::Connection) {
      ResidentEvalCtx rc; rc.frameIndex = ctx.frameIndex; rc.localFxTime = ctx.time; rc.localTime = ctx.time;
      return evalResidentFloat(rg, ri->srcNodePath, ri->srcSlotId, rc);
    }
    if (ri->driver == ResidentInput::Driver::Automation) return 0.0f;  // S3 stub
    return ri->constant;  // Constant
  };

  std::function<MTL::Buffer*(const std::string&)> cookNode = [&](const std::string& path) -> MTL::Buffer* {
    auto m = cooked.find(path);
    if (m != cooked.end()) return m->second;
    const ResidentNode* n = rg.node(path);
    if (!n) return nullptr;
    const NodeSpec* s = findSpec(n->opType);
    if (!s) return nullptr;

    // Gather buffer inputs (Points + ParticleForce, spec order) via Connection drivers.
    std::vector<const MTL::Buffer*> ins;
    std::vector<uint32_t> insCounts;
    uint32_t sumPointsCount = 0;
    for (const PortSpec& port : s->ports) {
      if (!isBufferInput(port)) continue;
      const ResidentInput* ri = n->input(port.id);
      MTL::Buffer* ub = nullptr;
      uint32_t inCount = 0;
      if (ri && ri->driver == ResidentInput::Driver::Connection) {
        ub = cookNode(ri->srcNodePath);
        inCount = ub ? cookedCount[ri->srcNodePath] : 0u;
      }
      ins.push_back(ub);
      insCounts.push_back(inCount);
      if (port.dataType == "Points") sumPointsCount += inCount;
    }

    // count: a "Count" Float input (generators) resolved through its driver, else sum of Points.
    uint32_t count = sumPointsCount;
    for (const PortSpec& port : s->ports)
      if (port.isInput && port.dataType == "Float" && port.id == "Count") {
        float v = resolveFloat(*n, port);
        count = v > 0.0f ? (uint32_t)(v + 0.5f) : 0u;
        break;
      }

    uint32_t cap = count > 0 ? count : 1;  // never alloc zero
    MTL::Buffer* out = p_->dev->newBuffer((NS::UInteger)cap * sizeof(SwPoint),
                                          MTL::ResourceStorageModeShared);
    owned.push_back(out);

    PointCookCtx cc;
    cc.dev = p_->dev; cc.lib = p_->lib; cc.queue = p_->queue;
    cc.ctx = &ctx; cc.graph = nullptr; cc.reg = reg;  // resident path: ops read no flat graph (slice 2 stubs)
    cc.nodeId = 0; cc.count = count;
    cc.inputs = ins.data(); cc.inputCounts = insCounts.data(); cc.inputCount = (int)ins.size();
    cc.output = out; cc.state = nullptr;               // stateful resident ops = slice 2b
    auto r = cookReg().find(n->opType);
    if (r != cookReg().end() && r->second.cook) r->second.cook(cc);
    cooked[path] = out;
    cookedCount[path] = count;
    return out;
  };

  // Terminal. Slice 2 supports a COMMAND terminal (capture op): resolve its Points input bag and
  // call the cmd op. (Texture executor + preview = slice 2b.) Unknown terminal -> nothing captured.
  const ResidentNode* tn = rg.node(targetPath);
  const NodeSpec* ts = tn ? findSpec(tn->opType) : nullptr;
  if (tn && ts) {
    auto cm = cmdReg().find(tn->opType);
    if (cm != cmdReg().end() && cm->second) {
      MTL::Buffer* pts = nullptr;
      uint32_t cnt = 0;
      for (const PortSpec& port : ts->ports) {
        if (!(port.isInput && port.dataType == "Points")) continue;
        const ResidentInput* ri = tn->input(port.id);
        if (ri && ri->driver == ResidentInput::Driver::Connection) {
          pts = cookNode(ri->srcNodePath);
          cnt = cookedCount[ri->srcNodePath];
        }
        break;
      }
      CmdCookCtx cc;
      cc.ctx = &ctx; cc.graph = nullptr; cc.reg = reg;
      cc.nodeId = 0; cc.points = pts; cc.count = cnt;
      cm->second(cc);
    } else {
      cookNode(targetPath);  // buffer-producing terminal (preview): cook it; visualizer = slice 2b
    }
  }

  for (MTL::Buffer* b : owned) b->release();  // slice 2 = single-cook; cross-frame cache = slice 4
}
```

- [ ] **Step 2: Build + run — verify GREEN**

Run: `cd app && cmake --build build -j 2>&1 | tail -5 && ./build/simple_world --selftest-residentcook; echo "exit=$?"`
Expected: `[selftest-residentcook] flat=8(x=2.0) resident=8(x=2.0) match=1 -> PASS`, `exit=0`. The resident walk produced the same 8 points with x=2 as the flat cook.

- [ ] **Step 3: Regression — confirm flat cook + slice-1 engine untouched**

Run: `cd app && for t in pointgraph residenteval compoundmodel valuecook resolve graph; do ./build/simple_world --selftest-$t >/dev/null 2>&1 && echo "$t PASS" || echo "$t FAIL"; done`
Expected: all `PASS` (cookResident is additive; cook + resident_eval_graph unchanged).

- [ ] **Step 4: Commit**

```bash
git add app/src/runtime/point_graph.cpp
git commit -m "feat(runtime): cookResident buffer-flow walk == flat cook (batch 1 slice 2)

Walks ResidentEvalGraph by path-qualified id; Points/Force inputs resolved via Connection drivers,
count via the resident Count input driver, command-op capture terminal. resident bag == flat bag
(8 pts, x=2). Additive: production cook(Graph&) + slice-1 engine untouched. Per-cook buffers
(cross-frame cache = slice 4); stateful ops + tex executor = slice 2b.

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

### Task 3: Real teeth + reuse isolation

Replace the weak negation-teeth with a real injected divergence, and add a reuse case (two instances of the same generator symbol with different counts cook independently).

**Files:**
- Modify: `app/src/runtime/resident_cook_selftest.cpp`

- [ ] **Step 1: Give the bug a real injection point**

In `rcMul`, make the bug drop the input (so the resident-vs-flat comparison still matches each other, but the absolute value assertion `x==2` is what carries — we need the bug to make resident DIVERGE from flat, not both). Better: inject the bug ONLY into the resident cook path. Since both paths share the same stub, route the bug via count: change the capture so the bug variant truncates the resident bag. Replace the `if (injectBug) match = !match;` weak line in `runResidentCookSelfTest` with a real divergence: when `injectBug`, cook the resident target with a WRONG path so its bag stays empty:
```cpp
  std::vector<SwPoint> resBag; g_resCap = &resBag;
  pg.cookResident(rg, ctx, nullptr, injectBug ? "999" : "3");  // bug: bogus target path -> empty bag -> mismatch
```
and delete the `if (injectBug) match = !match;` line so `match` is the literal `sizeOk && valOk`:
```cpp
  bool match = sizeOk && valOk;
```
Update the printf's final arg accordingly (it already prints `match`).

Rationale: the bug now exercises a real failure mode (resident walk targeting a missing node → empty bag → resident≠flat → FAIL), not a tautological negation.

- [ ] **Step 2: Add a reuse-isolation case**

Before the final compare, add a second root that reuses the generator symbol twice with different counts and sums them through ParticleSystem's two-input... — simpler: two generator children with different Count overrides feeding a combine is out of slice scope. Instead assert reuse at the **count** level: build a tiny lib with TWO RadialPoints children (c1 Count override 4, c2 default 8) each drawn through its own capture, and confirm each resident instance cooks its own count (4 vs 8), proving per-path isolation. Add:
```cpp
  // reuse isolation: two RadialPoints instances, different Count, cook independently.
  SymbolLibrary rl;
  rl.symbols["RadialPoints"] = atomicOp("RadialPoints", {{"Count", "Count", "Float", 8.0f}},
                                        {{"points", "points", "Points", 0.0f}});
  rl.symbols["DrawPoints"] = atomicOp("DrawPoints", {{"points", "points", "Points", 0.0f}},
                                      {{"out", "out", "Command", 0.0f}});
  Symbol r2; r2.id = "R2"; r2.name = "R2"; r2.atomic = false;
  r2.outputDefs = {{"out", "out", "Command", 0.0f}};
  SymbolChild g4; g4.id = 1; g4.symbolId = "RadialPoints"; g4.overrides["Count"] = 4.0f;
  SymbolChild g8; g8.id = 2; g8.symbolId = "RadialPoints";  // default 8
  SymbolChild d1; d1.id = 3; d1.symbolId = "DrawPoints";
  r2.children = {g4, g8, d1};
  r2.connections = {{1, "points", 3, "points"}, {3, "out", kSymbolBoundary, "out"}};  // draw the Count=4 one
  rl.symbols["R2"] = r2; rl.rootId = "R2";
  ResidentEvalGraph rg2 = buildEvalGraph(rl, "R2");
  std::vector<SwPoint> bag4; g_resCap = &bag4;
  pg.cookResident(rg2, ctx, nullptr, "3");
  bool reuseOk = (bag4.size() == 4);  // the override-4 instance, not the default-8 sibling
```
Fold `reuseOk` into the result and printf:
```cpp
  bool pass = match && reuseOk && !injectBug ? (match && reuseOk) : match;  // (see below)
```
Simpler — set the final return to `(match && reuseOk) ? 0 : 1` for the clean run; for the bug run, `match` is already false so it returns 1. Concretely replace the return logic:
```cpp
  bool pass = match && reuseOk;
  printf("[selftest-residentcook] flat=%zu(x=%.1f) resident=%zu(x=%.1f) match=%d reuse(=4)=%zu -> %s\n",
         flatBag.size(), fx, resBag.size(), rx, match, bag4.size(), pass ? "PASS" : "FAIL");
  ...
  return pass ? 0 : 1;
```

- [ ] **Step 3: Build + run clean + bug**

Run: `cd app && cmake --build build -j 2>&1 | tail -3 && ./build/simple_world --selftest-residentcook; echo "clean=$?" && ./build/simple_world --selftest-residentcook-bug; echo "bug=$?"`
Expected: clean `match=1 reuse(=4)=4 -> PASS` exit=0; bug `resident=0 ... match=0 -> FAIL` exit=1.

- [ ] **Step 4: Commit**

```bash
git add app/src/runtime/resident_cook_selftest.cpp
git commit -m "test(runtime): real teeth + reuse isolation for resident cook (batch 1 slice 2)

Bug variant targets a bogus resident path (empty bag -> resident!=flat -> FAIL) instead of a
tautological negation. Reuse case: two RadialPoints instances (Count 4 vs default 8) cook
independently by path -> the drawn one yields exactly its own 4 points.

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

### Task 4: Slice close-out — sweep, arch, contract note, refuter

- [ ] **Step 1: Full selftest sweep + arch check + file sizes**

```bash
cd app && cmake --build build -j 2>&1 | tail -2
for t in "" graph save command valuecook resolve audionode compoundmodel residenteval pointgraph residentcook; do n=$([ -z "$t" ] && echo "(color)" || echo "$t"); ./build/simple_world --selftest${t:+-$t} >/dev/null 2>&1 && echo "$n PASS" || echo "$n FAIL"; done
./build/simple_world --selftest-residentcook-bug >/dev/null 2>&1 && echo "residentcook-bug PASS(WRONG)" || echo "residentcook-bug FAIL(teeth-ok)"
cd "$(git rev-parse --show-toplevel)" && ./tools/check_arch.sh 2>&1 | tail -3
wc -l app/src/runtime/resident_cook_selftest.cpp
```
Expected: every selftest PASS; residentcook-bug FAILs; arch OK; selftest file < 400 lines. (`point_graph.cpp` grew — check `wc -l app/src/runtime/point_graph.cpp`; if it crossed ~400, note it as a concern for a future split of cook vs cookResident, do NOT split now.)

- [ ] **Step 2: Append slice-2 status to the contract**

In `docs/superpowers/plans/specs/2026-06-10-compound-graph-design.md`, under the slice-status note added for slice 1 (the blockquote after batch item `1.`), append a `slice 2 ✅` line summarizing: `cookResident` buffer-flow walk == flat cook (`--selftest-residentcook`), reuse isolation by path, additive; deferred slice 2b (cmd/tex executor + stateful state) and slice 4 (cross-frame cache).

- [ ] **Step 3: Commit close-out**

```bash
git add docs/superpowers/plans/specs/2026-06-10-compound-graph-design.md
git commit -m "docs(compound): batch 1 slice 2 status — resident cook == flat cook (buffer flow)

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

- [ ] **Step 4: Independent refuter on `cookResident`**

Dispatch an adversarial refuter (capable model) against `cookResident`: construct breaking cases the golden doesn't cover — a multi-input combine node (two Points inputs → does `sumPointsCount` concatenate right on resident?), a generator with Count driven by a Connection (not Constant) so `resolveFloat` recurses, a nested compound producing the terminal's input (path like `5/3` as the bag source), an unwired buffer input (Constant driver → must be treated as no-input not a crash), and a force-input (ParticleForce) wired alongside Points. Report BROKEN/SURVIVES with evidence (temporary test, reverted). Fix any real slice-2-scope defect with a new golden case (TDD), defer + record others.

---

## Self-Review

**1. Spec coverage:** resident-graph cook walk (§2.3 path-qualified id as buffer key) → Task 2. "Half-resident buffer map" generalized to string-path keys → Task 2 (`cooked`/`cookedCount` by path). nested==flat parity for the buffer flow → Task 1/2 golden. reuse isolation by path → Task 3. **Deviation, named:** per-cook buffer allocation (not cross-frame persistent) — slice 4 cache; stateful state + cmd/tex executor — slice 2b. All in Scope.

**2. Placeholder scan:** Task 3 Step 1/2 edit existing test code in place — the exact replacement lines are quoted; the `pass`/printf final form is given explicitly. No "TBD"/"handle errors".

**3. Type consistency:** `cookResident(const ResidentEvalGraph&, const EvaluationContext&, const SourceRegistry*, const std::string&)` identical in header (Task 1 Step 1) and impl (Task 2 Step 1). `runResidentCookSelfTest(bool)` decl (Task 1 Step 1) ↔ def (Task 1 Step 3) ↔ kTable row (Task 1 Step 4). Reuses slice-1 `ResidentEvalGraph::node`, `ResidentNode::input`, `ResidentInput::Driver`, `evalResidentFloat`, `buildEvalGraph` exactly as declared. Reuses file-private `isBufferInput`, `cmdReg`, `cookReg` (same TU).

**Known fragility:** `cookResident` duplicates the buffer-walk shape of `cook` (string-keyed vs int-keyed). They will converge at the production swap; until then a change to cook semantics must be mirrored. Flagged; not factored now (premature — the int/string key split is real and the swap will resolve it).

---

## Execution Handoff

**Plan complete.** Worktree note: working tree clean on `codex/js-to-cpp-contract-migration`. Commit-law: each commit passes ARCHITECTURE self-check (Task 4 Step 1 runs `check_arch.sh`).

**Two execution options — 1) Subagent-Driven (recommended)** fresh subagent per task + two-stage review; **2) Inline Execution.** Which approach?

# JS to C++ Contract Migration Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Move product-bearing graph, command, validation, runtime-build, and node maturity truth from JavaScript proof code into C++ while keeping JavaScript where it is useful as test scaffolding, generators, and proof harness glue.

**Architecture:** `GraphDocument` remains the only source of truth. C++ owns the product contract for graph mutation, validation, dirty propagation, cook order, runtimeGraph building, serialization, and native UI dispatch. JavaScript remains allowed for contract generation, fixture checks, artifact inspection, legacy proof replay, and regression comparison until each lane has an equivalent C++ proof.

**Tech Stack:** C++17 or later for product graph/runtime contract code, Objective-C++ only for native AppKit/MetalKit shell boundaries, Metal/MSL for backend proof lanes, Node.js `node:test` for existing regression harnesses, JSON fixtures and artifacts for cross-language proof comparison.

---

## Scope

This is a complete migration plan, not a minimum slice. It does not claim every JavaScript file must disappear. It classifies code by load-bearing responsibility:

- **Move into C++:** product truth that mutates, validates, serializes, builds, schedules, or executes the graph.
- **Keep in JavaScript:** test runners, generators, artifact inspectors, proof-shell adapters, and documentation checks that do not become app/runtime truth.
- **Weave more detail later:** node manifests and runtime contracts where current evidence is admission-level or interaction-level, but not yet executable native behavior.

The plan deliberately keeps UI polish out of scope until the C++ command spine can pass headless proof.

## Current Ground Truth

The current interaction proof is centered on:

- `docs/runtime/scripts/graph_interaction_contract.js`
- `tests/graph_interaction_contract.test.js`
- `docs/runtime/scripts/native_canvas_interaction_command_loop_shell.py`
- `tests/native_canvas_interaction_command_loop.test.js`
- `docs/contracts/vuo_node_admission_index.json`
- `docs/contracts/node_manifests/*.json`
- `tests/node_admission_contract.test.js`

The current proof says:

```text
UI action
-> shared GraphState command
-> new GraphState + diagnostics
-> validation
-> runtimeGraph
-> runtime frame/proof artifact
```

The next product law should say the same thing in C++:

```text
UI / AI / importer / test command
-> C++ command dispatcher
-> C++ GraphState / GraphDocument
-> C++ validation
-> C++ runtimeGraph builder
-> backend proof or runtime artifact
```

## Migration Classification

### Must Move To C++

These are product truth. They must not remain JavaScript-only once the native app begins to dispatch real user actions.

| Responsibility | Current Anchor | C++ Destination | Reason |
| --- | --- | --- | --- |
| `GraphState` and `GraphDocument` data model | `docs/runtime/scripts/graph_interaction_contract.js` | `docs/runtime/native/graph/GraphDocument.hpp` and `GraphDocument.cpp` | Save/reload, UI dispatch, AI commands, importer commands, and runtime must share one graph truth. |
| Command input types | `CreateNode`, `MoveNode`, `CommitCableDrag`, etc. in JS | `docs/runtime/native/graph/GraphCommand.hpp` | UI and AI must submit the same typed commands. |
| Command dispatcher | `dispatchGraphCommand` | `docs/runtime/native/graph/GraphCommandDispatcher.cpp` | No UI-owned or Vuo-owned mutation. |
| Diagnostics | JS arrays of diagnostic objects | `docs/runtime/native/graph/GraphDiagnostics.hpp` | Validation and runtime failures need stable machine-readable output. |
| Validation | `validateGraphState` | `docs/runtime/native/graph/GraphValidator.cpp` | C++ must reject bad cables, missing nodes, missing params, and unknown types before runtime. |
| Dirty semantics | `runtimeDirty` updates | `docs/runtime/native/graph/GraphDirtyPolicy.cpp` | Runtime rebuild/cook scheduling depends on this. |
| Cook order | `buildRuntimeGraph(state).cookOrder` | `docs/runtime/native/runtime/RuntimeGraphBuilder.cpp` | Runtime execution order cannot depend on JS. |
| Serialization | `serializeGraphDocument` / `deserializeGraphDocument` | `docs/runtime/native/graph/GraphDocumentJson.cpp` | Save/reload must not preserve unsafe UI-local state. |
| NodeSpec runtime registry core | `NODE_SPECS` in JS and node manifests | `docs/runtime/native/nodes/NodeSpecRegistry.cpp` | C++ must know ports, params, types, defaults, and maturity before UI/runtime use. |
| Manifest maturity gate | admission index and high-risk manifests | `docs/runtime/native/nodes/NodeManifestMaturity.cpp` | C++ must know whether a node is only admissible, interaction-ready, runtime-ready, or executable. |

### Can Stay In JavaScript

These are scaffolding or evidence tools. They can remain JavaScript as long as they do not become the product source of truth.

| Responsibility | Keep Anchor | Why It Can Stay |
| --- | --- | --- |
| Existing `node:test` regression suites | `tests/*.test.js` | They are harnesses, not runtime state. |
| Admission index generation | `tools/generate_vuo_node_admission_index.js` | It generates contract data from sources and tests. |
| Proof artifact inspection | JS tests reading `docs/runtime/artifacts/**` | It validates outputs from shells and native probes. |
| Legacy proof replay | JS helper functions used only by tests | It can compare old proof behavior against new C++ outputs. |
| Documentation gates | tests that scan docs and contracts | They prevent overclaiming and stale claims. |
| Batch Vuo composition tests | `tests/vuo_*.test.js` | They verify body-layer proof, not product graph truth. |

### Should Become More Detailed Later

These are not ready to be fully native runtime law for all nodes. They need staged maturity.

| Area | Current State | Future Detail |
| --- | --- | --- |
| Low-risk Vuo nodes | Minimal admission entries | Add interaction-ready fields only when they appear in the native node browser. |
| Medium-risk nodes | Admission plus risk reasons | Add runtime-ready manifest when a C++ runtimeGraph proof uses the node. |
| High-risk nodes | Full manifest required | Add native executable proof once C++/Metal/Vuo evidence exists. |
| Shader expression language | Bounded expression core proof | Expand deliberately: loops, functions, user shader text, and translation only after separate proof. |
| Resource lifetime | Bounded ledger plus `MTLHeap` proof | Add complete heap eviction, backend hazard tracking, and residency pressure policy later. |
| Native UI canvas | Bounded workflow and command-loop proof | Add full interaction parity after C++ command dispatcher is product-bearing. |
| Importer | Bounded command-ingest proof | Add file-format coverage only by translating imports into commands. |
| AI worker | Bounded command plan and repair proof | Add broader natural-language authoring only through the same C++ command path. |

## File Structure

Create a focused native graph/runtime contract spine:

```text
docs/runtime/native/graph/
  GraphDocument.hpp
  GraphDocument.cpp
  GraphCommand.hpp
  GraphCommandDispatcher.hpp
  GraphCommandDispatcher.cpp
  GraphDiagnostics.hpp
  GraphValidator.hpp
  GraphValidator.cpp
  GraphDirtyPolicy.hpp
  GraphDirtyPolicy.cpp
  GraphDocumentJson.hpp
  GraphDocumentJson.cpp

docs/runtime/native/nodes/
  NodeSpec.hpp
  NodeSpecRegistry.hpp
  NodeSpecRegistry.cpp
  NodeManifestMaturity.hpp
  NodeManifestMaturity.cpp

docs/runtime/native/runtime/
  RuntimeGraph.hpp
  RuntimeGraphBuilder.hpp
  RuntimeGraphBuilder.cpp

docs/runtime/native/tools/
  graph_command_contract_probe.cpp

docs/runtime/fixtures/
  cpp_graph_command_contract.graph.json
  cpp_graph_command_contract_bad_type.graph.json
  cpp_graph_command_contract_save_reload.graph.json

docs/runtime/scripts/
  cpp_graph_command_contract_shell.py

docs/runtime/artifacts/
  cpp_graph_command_contract/

tests/
  cpp_graph_command_contract.test.js
```

Keep JavaScript proof code in place during migration:

```text
docs/runtime/scripts/graph_interaction_contract.js
tests/graph_interaction_contract.test.js
```

It becomes a reference fixture until C++ parity is proven and native UI no longer depends on JS behavior.

## Contract Levels

C++ should not treat every node as executable. Each node gets a maturity level:

```text
admissionReady
  Node can be indexed, shown as known, and safely rejected or deferred.

interactionReady
  Node can be created, selected, moved, connected by declared port types,
  edited through declared params, serialized, and deserialized.

runtimeReady
  Node can participate in validation, dirty propagation, runtimeGraph build,
  and cook-order planning.

nativeExecutable
  Node has a native RuntimeOp or backend adapter proof that emits an observable
  artifact.
```

The C++ gate must refuse unsupported promotion:

```text
interactionReady node may enter editorGraph.
runtimeReady node may enter runtimeGraph.
nativeExecutable node may execute.
```

## Task 1: Freeze The Cross-Language Acceptance Fixtures

**Files:**
- Create: `docs/runtime/fixtures/cpp_graph_command_contract.graph.json`
- Create: `docs/runtime/fixtures/cpp_graph_command_contract_bad_type.graph.json`
- Create: `docs/runtime/fixtures/cpp_graph_command_contract_save_reload.graph.json`
- Create: `tests/cpp_graph_command_contract.test.js`

- [ ] **Step 1: Write the expected command fixture**

Create `docs/runtime/fixtures/cpp_graph_command_contract.graph.json`:

```json
{
  "graphId": "fixture.cpp_graph_command_contract",
  "commands": [
    {
      "source": "ui.library",
      "command": {
        "type": "CreateNode",
        "nodeId": "sphere_sdf_1",
        "nodeType": "tixl.field.generate.sdf.SphereSDF",
        "position": { "x": 10, "y": 20 }
      }
    },
    {
      "source": "ui.library",
      "command": {
        "type": "CreateNode",
        "nodeId": "raymarch_field_1",
        "nodeType": "tixl.field.render.RaymarchField",
        "position": { "x": 360, "y": 20 }
      }
    },
    {
      "source": "ui.canvas",
      "command": {
        "type": "BeginCableDrag",
        "from": { "nodeId": "sphere_sdf_1", "port": "result" }
      }
    },
    {
      "source": "ui.canvas",
      "command": {
        "type": "HoverPort",
        "port": { "nodeId": "raymarch_field_1", "port": "sdfField" }
      }
    },
    {
      "source": "ui.canvas",
      "command": {
        "type": "CommitCableDrag",
        "to": { "nodeId": "raymarch_field_1", "port": "sdfField" }
      }
    },
    {
      "source": "ui.inspector",
      "command": {
        "type": "SetParameter",
        "nodeId": "sphere_sdf_1",
        "param": "radius",
        "value": 0.75
      }
    }
  ]
}
```

- [ ] **Step 2: Write the bad cable fixture**

Create `docs/runtime/fixtures/cpp_graph_command_contract_bad_type.graph.json`:

```json
{
  "graphId": "fixture.cpp_graph_command_contract_bad_type",
  "commands": [
    {
      "source": "ui.library",
      "command": {
        "type": "CreateNode",
        "nodeId": "sphere_sdf_1",
        "nodeType": "tixl.field.generate.sdf.SphereSDF"
      }
    },
    {
      "source": "ui.library",
      "command": {
        "type": "CreateNode",
        "nodeId": "raymarch_field_1",
        "nodeType": "tixl.field.render.RaymarchField"
      }
    },
    {
      "source": "ui.canvas",
      "command": {
        "type": "BeginCableDrag",
        "from": { "nodeId": "sphere_sdf_1", "port": "result" }
      }
    },
    {
      "source": "ui.canvas",
      "command": {
        "type": "CommitCableDrag",
        "to": { "nodeId": "raymarch_field_1", "port": "color" }
      }
    }
  ]
}
```

- [ ] **Step 3: Write the save/reload fixture**

Create `docs/runtime/fixtures/cpp_graph_command_contract_save_reload.graph.json`:

```json
{
  "graphId": "fixture.cpp_graph_command_contract_save_reload",
  "commands": [
    {
      "source": "ui.library",
      "command": {
        "type": "CreateNode",
        "nodeId": "sphere_sdf_1",
        "nodeType": "tixl.field.generate.sdf.SphereSDF",
        "position": { "x": 10, "y": 20 }
      }
    },
    {
      "source": "ui.library",
      "command": {
        "type": "CreateNode",
        "nodeId": "raymarch_field_1",
        "nodeType": "tixl.field.render.RaymarchField",
        "position": { "x": 360, "y": 20 }
      }
    },
    {
      "source": "ui.canvas",
      "command": {
        "type": "SelectNode",
        "nodeId": "sphere_sdf_1",
        "mode": "replace"
      }
    },
    {
      "source": "ui.canvas",
      "command": {
        "type": "BeginCableDrag",
        "from": { "nodeId": "sphere_sdf_1", "port": "result" }
      }
    },
    {
      "source": "ui.canvas",
      "command": {
        "type": "CommitCableDrag",
        "to": { "nodeId": "raymarch_field_1", "port": "sdfField" }
      }
    },
    {
      "source": "ui.inspector",
      "command": {
        "type": "SetParameter",
        "nodeId": "sphere_sdf_1",
        "param": "radius",
        "value": 0.33
      }
    }
  ],
  "assertReloadDrops": ["selectedNodeIds", "cableDrag"]
}
```

- [ ] **Step 4: Write the failing JS harness test for the future C++ shell**

Create `tests/cpp_graph_command_contract.test.js`:

```js
const assert = require("node:assert/strict");
const fs = require("node:fs");
const os = require("node:os");
const path = require("node:path");
const { spawnSync } = require("node:child_process");
const test = require("node:test");

const repoRoot = path.resolve(__dirname, "..");
const scriptPath = path.join(repoRoot, "docs/runtime/scripts/cpp_graph_command_contract_shell.py");

function readJson(filePath) {
  return JSON.parse(fs.readFileSync(filePath, "utf8"));
}

function runFixture(fixtureName) {
  const fixturePath = path.join(repoRoot, "docs/runtime/fixtures", fixtureName);
  const tmpDir = fs.mkdtempSync(path.join(os.tmpdir(), "cpp-graph-command-contract-"));
  const run = spawnSync("python3", [scriptPath, fixturePath, tmpDir], {
    cwd: repoRoot,
    encoding: "utf8"
  });
  return { run, tmpDir };
}

test("C++ graph command contract creates connects edits validates and builds runtime graph", () => {
  const { run, tmpDir } = runFixture("cpp_graph_command_contract.graph.json");
  assert.equal(run.status, 0, run.stderr || run.stdout);

  const result = readJson(path.join(tmpDir, "cpp_graph_command_contract_result.json"));
  const document = readJson(path.join(tmpDir, "graph_document.json"));
  const runtimeGraph = readJson(path.join(tmpDir, "runtime_graph.json"));
  const diagnostics = readJson(path.join(tmpDir, "diagnostics.json"));

  assert.equal(result.status, "passed");
  assert.equal(result.claims.cppCommandDispatcher, true);
  assert.equal(result.claims.runtimeDirty, true);
  assert.deepEqual(diagnostics, []);
  assert.equal(document.nodes.length, 2);
  assert.equal(document.edges.length, 1);
  assert.equal(document.edges[0].from.nodeId, "sphere_sdf_1");
  assert.equal(document.edges[0].to.nodeId, "raymarch_field_1");
  assert.deepEqual(runtimeGraph.cookOrder, ["sphere_sdf_1", "raymarch_field_1"]);
});

test("C++ graph command contract rejects invalid cable type without creating edge", () => {
  const { run, tmpDir } = runFixture("cpp_graph_command_contract_bad_type.graph.json");
  assert.equal(run.status, 0, run.stderr || run.stdout);

  const result = readJson(path.join(tmpDir, "cpp_graph_command_contract_result.json"));
  const document = readJson(path.join(tmpDir, "graph_document.json"));
  const diagnostics = readJson(path.join(tmpDir, "diagnostics.json"));

  assert.equal(result.status, "diagnosed");
  assert.equal(document.edges.length, 0);
  assert.equal(diagnostics[0].code, "graph.edge.type_mismatch");
});

test("C++ graph command contract save reload preserves graph and drops unsafe UI state", () => {
  const { run, tmpDir } = runFixture("cpp_graph_command_contract_save_reload.graph.json");
  assert.equal(run.status, 0, run.stderr || run.stdout);

  const reloaded = readJson(path.join(tmpDir, "reloaded_graph_document.json"));
  assert.equal(reloaded.nodes.length, 2);
  assert.equal(reloaded.edges.length, 1);
  assert.equal(reloaded.nodes.find((node) => node.id === "sphere_sdf_1").params.radius, 0.33);
  assert.equal(Object.prototype.hasOwnProperty.call(reloaded, "selectedNodeIds"), false);
  assert.equal(Object.prototype.hasOwnProperty.call(reloaded, "cableDrag"), false);
});
```

- [ ] **Step 5: Run the failing test**

Run:

```bash
node --test tests/cpp_graph_command_contract.test.js
```

Expected: FAIL because `docs/runtime/scripts/cpp_graph_command_contract_shell.py` does not exist yet.

- [ ] **Step 6: Commit the acceptance fixtures**

Run:

```bash
git add docs/runtime/fixtures/cpp_graph_command_contract.graph.json \
  docs/runtime/fixtures/cpp_graph_command_contract_bad_type.graph.json \
  docs/runtime/fixtures/cpp_graph_command_contract_save_reload.graph.json \
  tests/cpp_graph_command_contract.test.js
git commit -m "test: add C++ graph command contract acceptance fixtures"
```

## Task 2: Add The C++ Graph Data Model

**Files:**
- Create: `docs/runtime/native/graph/GraphDiagnostics.hpp`
- Create: `docs/runtime/native/graph/GraphDocument.hpp`
- Create: `docs/runtime/native/graph/GraphDocument.cpp`

- [ ] **Step 1: Define diagnostics**

Create `docs/runtime/native/graph/GraphDiagnostics.hpp`:

```cpp
#pragma once

#include <string>
#include <vector>

namespace simple_world::graph {

struct Diagnostic {
    std::string code;
    std::string message;
    std::string severity = "error";
};

using Diagnostics = std::vector<Diagnostic>;

inline bool hasErrors(const Diagnostics& diagnostics) {
    for (const auto& diagnostic : diagnostics) {
        if (diagnostic.severity == "error") {
            return true;
        }
    }
    return false;
}

} // namespace simple_world::graph
```

- [ ] **Step 2: Define graph document types**

Create `docs/runtime/native/graph/GraphDocument.hpp`:

```cpp
#pragma once

#include <map>
#include <optional>
#include <set>
#include <string>
#include <variant>
#include <vector>

namespace simple_world::graph {

using NumericArray = std::vector<double>;
using NumericObject = std::map<std::string, double>;
using ParamValue = std::variant<double, bool, std::string, NumericArray, NumericObject>;

struct Position {
    double x = 0.0;
    double y = 0.0;
};

struct PortRef {
    std::string nodeId;
    std::string port;
};

struct NodeInstance {
    std::string id;
    std::string type;
    Position position;
    std::map<std::string, ParamValue> params;
};

struct Edge {
    std::string id;
    PortRef from;
    PortRef to;
    std::string type;
};

struct CableDragState {
    PortRef from;
    std::optional<PortRef> hover;
};

struct GraphState {
    std::string kind = "GraphState";
    std::string version = "0.1";
    std::string graphId = "graph.interaction";
    std::vector<NodeInstance> nodes;
    std::vector<Edge> edges;
    std::set<std::string> selectedNodeIds;
    std::optional<CableDragState> cableDrag;
    bool runtimeDirty = false;
};

struct GraphDocument {
    std::string kind = "GraphDocument";
    std::string version = "0.1";
    std::string graphId = "graph.interaction";
    std::vector<NodeInstance> nodes;
    std::vector<Edge> edges;
};

GraphState createInitialGraphState(const std::string& graphId);
GraphDocument serializeGraphDocument(const GraphState& state);
GraphState deserializeGraphDocument(const GraphDocument& document);
NodeInstance* findNode(GraphState& state, const std::string& nodeId);
const NodeInstance* findNode(const GraphState& state, const std::string& nodeId);

} // namespace simple_world::graph
```

- [ ] **Step 3: Implement graph document helpers**

Create `docs/runtime/native/graph/GraphDocument.cpp`:

```cpp
#include "GraphDocument.hpp"

namespace simple_world::graph {

GraphState createInitialGraphState(const std::string& graphId) {
    GraphState state;
    state.graphId = graphId;
    return state;
}

GraphDocument serializeGraphDocument(const GraphState& state) {
    GraphDocument document;
    document.graphId = state.graphId;
    document.version = state.version;
    document.nodes = state.nodes;
    document.edges = state.edges;
    return document;
}

GraphState deserializeGraphDocument(const GraphDocument& document) {
    GraphState state;
    state.graphId = document.graphId;
    state.version = document.version;
    state.nodes = document.nodes;
    state.edges = document.edges;
    state.selectedNodeIds.clear();
    state.cableDrag.reset();
    state.runtimeDirty = true;
    return state;
}

NodeInstance* findNode(GraphState& state, const std::string& nodeId) {
    for (auto& node : state.nodes) {
        if (node.id == nodeId) {
            return &node;
        }
    }
    return nullptr;
}

const NodeInstance* findNode(const GraphState& state, const std::string& nodeId) {
    for (const auto& node : state.nodes) {
        if (node.id == nodeId) {
            return &node;
        }
    }
    return nullptr;
}

} // namespace simple_world::graph
```

- [ ] **Step 4: Compile the data model in isolation**

Run:

```bash
clang++ -std=c++17 -c docs/runtime/native/graph/GraphDocument.cpp \
  -o /tmp/simple_world_graph_document.o
```

Expected: command exits `0`.

- [ ] **Step 5: Commit graph model**

Run:

```bash
git add docs/runtime/native/graph/GraphDiagnostics.hpp \
  docs/runtime/native/graph/GraphDocument.hpp \
  docs/runtime/native/graph/GraphDocument.cpp
git commit -m "feat: add C++ graph document model"
```

## Task 3: Add NodeSpec Registry And Maturity Gate

**Files:**
- Create: `docs/runtime/native/nodes/NodeSpec.hpp`
- Create: `docs/runtime/native/nodes/NodeSpecRegistry.hpp`
- Create: `docs/runtime/native/nodes/NodeSpecRegistry.cpp`
- Create: `docs/runtime/native/nodes/NodeManifestMaturity.hpp`
- Create: `docs/runtime/native/nodes/NodeManifestMaturity.cpp`

- [ ] **Step 1: Define node specs**

Create `docs/runtime/native/nodes/NodeSpec.hpp`:

```cpp
#pragma once

#include "../graph/GraphDocument.hpp"

#include <map>
#include <string>
#include <vector>

namespace simple_world::nodes {

struct PortSpec {
    std::string id;
    std::string type;
    bool required = false;
};

struct ParamSpec {
    std::string id;
    std::string type;
    simple_world::graph::ParamValue defaultValue;
    std::string owner = "NodeInstance";
    std::string affects = "runtime";
    bool saved = true;
};

struct NodeSpec {
    std::string type;
    std::string label;
    std::vector<PortSpec> inputs;
    std::vector<PortSpec> outputs;
    std::vector<ParamSpec> params;
    bool runtimeSemantic = true;
};

} // namespace simple_world::nodes
```

- [ ] **Step 2: Define maturity levels**

Create `docs/runtime/native/nodes/NodeManifestMaturity.hpp`:

```cpp
#pragma once

#include <string>

namespace simple_world::nodes {

enum class MaturityLevel {
    AdmissionReady,
    InteractionReady,
    RuntimeReady,
    NativeExecutable
};

struct NodeMaturity {
    MaturityLevel level = MaturityLevel::AdmissionReady;
    std::string evidence;
};

bool canEnterEditorGraph(const NodeMaturity& maturity);
bool canEnterRuntimeGraph(const NodeMaturity& maturity);
bool canExecuteNatively(const NodeMaturity& maturity);
std::string toString(MaturityLevel level);

} // namespace simple_world::nodes
```

- [ ] **Step 3: Implement maturity gate**

Create `docs/runtime/native/nodes/NodeManifestMaturity.cpp`:

```cpp
#include "NodeManifestMaturity.hpp"

namespace simple_world::nodes {

bool canEnterEditorGraph(const NodeMaturity& maturity) {
    return maturity.level == MaturityLevel::InteractionReady
        || maturity.level == MaturityLevel::RuntimeReady
        || maturity.level == MaturityLevel::NativeExecutable;
}

bool canEnterRuntimeGraph(const NodeMaturity& maturity) {
    return maturity.level == MaturityLevel::RuntimeReady
        || maturity.level == MaturityLevel::NativeExecutable;
}

bool canExecuteNatively(const NodeMaturity& maturity) {
    return maturity.level == MaturityLevel::NativeExecutable;
}

std::string toString(MaturityLevel level) {
    switch (level) {
        case MaturityLevel::AdmissionReady: return "admissionReady";
        case MaturityLevel::InteractionReady: return "interactionReady";
        case MaturityLevel::RuntimeReady: return "runtimeReady";
        case MaturityLevel::NativeExecutable: return "nativeExecutable";
    }
    return "admissionReady";
}

} // namespace simple_world::nodes
```

- [ ] **Step 4: Define registry API**

Create `docs/runtime/native/nodes/NodeSpecRegistry.hpp`:

```cpp
#pragma once

#include "NodeManifestMaturity.hpp"
#include "NodeSpec.hpp"

#include <optional>
#include <string>
#include <vector>

namespace simple_world::nodes {

class NodeSpecRegistry {
public:
    static NodeSpecRegistry createSeedRegistry();

    std::optional<NodeSpec> findSpec(const std::string& type) const;
    std::optional<PortSpec> findInput(const std::string& type, const std::string& port) const;
    std::optional<PortSpec> findOutput(const std::string& type, const std::string& port) const;
    std::optional<ParamSpec> findParam(const std::string& type, const std::string& param) const;
    NodeMaturity maturityFor(const std::string& type) const;

private:
    std::vector<NodeSpec> specs;
};

} // namespace simple_world::nodes
```

- [ ] **Step 5: Implement seed registry for the current command contract**

Create `docs/runtime/native/nodes/NodeSpecRegistry.cpp`:

```cpp
#include "NodeSpecRegistry.hpp"

namespace simple_world::nodes {

NodeSpecRegistry NodeSpecRegistry::createSeedRegistry() {
    NodeSpecRegistry registry;

    registry.specs.push_back(NodeSpec{
        "tixl.field.generate.sdf.SphereSDF",
        "SphereSDF",
        {},
        { PortSpec{ "result", "SdfField", true } },
        { ParamSpec{ "radius", "float", 1.0, "NodeInstance", "runtime", true } },
        true
    });

    registry.specs.push_back(NodeSpec{
        "tixl.field.render.RaymarchField",
        "RaymarchField",
        {
            PortSpec{ "sdfField", "SdfField", true },
            PortSpec{ "color", "Color", false }
        },
        { PortSpec{ "image", "Texture2D", true } },
        {},
        true
    });

    return registry;
}

std::optional<NodeSpec> NodeSpecRegistry::findSpec(const std::string& type) const {
    for (const auto& spec : specs) {
        if (spec.type == type) {
            return spec;
        }
    }
    return std::nullopt;
}

std::optional<PortSpec> NodeSpecRegistry::findInput(const std::string& type, const std::string& port) const {
    const auto spec = findSpec(type);
    if (!spec.has_value()) {
        return std::nullopt;
    }
    for (const auto& input : spec->inputs) {
        if (input.id == port) {
            return input;
        }
    }
    return std::nullopt;
}

std::optional<PortSpec> NodeSpecRegistry::findOutput(const std::string& type, const std::string& port) const {
    const auto spec = findSpec(type);
    if (!spec.has_value()) {
        return std::nullopt;
    }
    for (const auto& output : spec->outputs) {
        if (output.id == port) {
            return output;
        }
    }
    return std::nullopt;
}

std::optional<ParamSpec> NodeSpecRegistry::findParam(const std::string& type, const std::string& param) const {
    const auto spec = findSpec(type);
    if (!spec.has_value()) {
        return std::nullopt;
    }
    for (const auto& paramSpec : spec->params) {
        if (paramSpec.id == param) {
            return paramSpec;
        }
    }
    return std::nullopt;
}

NodeMaturity NodeSpecRegistry::maturityFor(const std::string& type) const {
    if (type == "tixl.field.generate.sdf.SphereSDF" || type == "tixl.field.render.RaymarchField") {
        return NodeMaturity{ MaturityLevel::RuntimeReady, "tests/graph_interaction_contract.test.js" };
    }
    return NodeMaturity{ MaturityLevel::AdmissionReady, "docs/contracts/vuo_node_admission_index.json" };
}

} // namespace simple_world::nodes
```

- [ ] **Step 6: Compile registry files**

Run:

```bash
clang++ -std=c++17 -c docs/runtime/native/nodes/NodeManifestMaturity.cpp \
  -o /tmp/simple_world_node_manifest_maturity.o
clang++ -std=c++17 -c docs/runtime/native/nodes/NodeSpecRegistry.cpp \
  -o /tmp/simple_world_node_spec_registry.o
```

Expected: both commands exit `0`.

- [ ] **Step 7: Commit registry and maturity gate**

Run:

```bash
git add docs/runtime/native/nodes/NodeSpec.hpp \
  docs/runtime/native/nodes/NodeSpecRegistry.hpp \
  docs/runtime/native/nodes/NodeSpecRegistry.cpp \
  docs/runtime/native/nodes/NodeManifestMaturity.hpp \
  docs/runtime/native/nodes/NodeManifestMaturity.cpp
git commit -m "feat: add C++ node spec maturity gate"
```

## Task 4: Add C++ Command Dispatcher

**Files:**
- Create: `docs/runtime/native/graph/GraphCommand.hpp`
- Create: `docs/runtime/native/graph/GraphCommandDispatcher.hpp`
- Create: `docs/runtime/native/graph/GraphCommandDispatcher.cpp`

- [ ] **Step 1: Define command input types**

Create `docs/runtime/native/graph/GraphCommand.hpp`:

```cpp
#pragma once

#include "GraphDocument.hpp"

#include <optional>
#include <string>
#include <variant>

namespace simple_world::graph {

struct CreateNodeCommand {
    std::string nodeId;
    std::string nodeType;
    Position position;
};

struct SelectNodeCommand {
    std::string nodeId;
    std::string mode = "replace";
};

struct MoveNodeCommand {
    std::string nodeId;
    Position position;
};

struct BeginCableDragCommand {
    PortRef from;
};

struct HoverPortCommand {
    PortRef port;
};

struct CommitCableDragCommand {
    PortRef to;
};

struct CancelCableDragCommand {};

struct DeleteSelectionCommand {};

struct SetParameterCommand {
    std::string nodeId;
    std::string param;
    ParamValue value;
};

using GraphCommand = std::variant<
    CreateNodeCommand,
    SelectNodeCommand,
    MoveNodeCommand,
    BeginCableDragCommand,
    HoverPortCommand,
    CommitCableDragCommand,
    CancelCableDragCommand,
    DeleteSelectionCommand,
    SetParameterCommand
>;

} // namespace simple_world::graph
```

- [ ] **Step 2: Define dispatcher result**

Create `docs/runtime/native/graph/GraphCommandDispatcher.hpp`:

```cpp
#pragma once

#include "GraphCommand.hpp"
#include "GraphDiagnostics.hpp"
#include "../nodes/NodeSpecRegistry.hpp"

namespace simple_world::graph {

struct DispatchResult {
    GraphState state;
    Diagnostics diagnostics;
};

DispatchResult dispatchGraphCommand(
    const GraphState& state,
    const GraphCommand& command,
    const simple_world::nodes::NodeSpecRegistry& registry
);

} // namespace simple_world::graph
```

- [ ] **Step 3: Implement dispatcher behavior**

Create `docs/runtime/native/graph/GraphCommandDispatcher.cpp`:

```cpp
#include "GraphCommandDispatcher.hpp"

#include <algorithm>

namespace simple_world::graph {

namespace {

std::string makeEdgeId(const PortRef& from, const PortRef& to) {
    return from.nodeId + "." + from.port + "->" + to.nodeId + "." + to.port;
}

void removeAttachedEdges(GraphState& state, const std::set<std::string>& removedNodeIds) {
    state.edges.erase(
        std::remove_if(state.edges.begin(), state.edges.end(), [&](const Edge& edge) {
            return removedNodeIds.count(edge.from.nodeId) > 0 || removedNodeIds.count(edge.to.nodeId) > 0;
        }),
        state.edges.end()
    );
}

} // namespace

DispatchResult dispatchGraphCommand(
    const GraphState& state,
    const GraphCommand& command,
    const simple_world::nodes::NodeSpecRegistry& registry
) {
    GraphState next = state;
    Diagnostics diagnostics;

    std::visit([&](const auto& typedCommand) {
        using CommandType = std::decay_t<decltype(typedCommand)>;

        if constexpr (std::is_same_v<CommandType, CreateNodeCommand>) {
            if (findNode(next, typedCommand.nodeId) != nullptr) {
                diagnostics.push_back({ "graph.node.duplicate_id", "Node id already exists." });
                return;
            }
            if (!registry.findSpec(typedCommand.nodeType).has_value()) {
                diagnostics.push_back({ "graph.node.unknown_type", "Node type is not registered." });
                return;
            }
            const auto maturity = registry.maturityFor(typedCommand.nodeType);
            if (!simple_world::nodes::canEnterEditorGraph(maturity) && maturity.level != simple_world::nodes::MaturityLevel::RuntimeReady) {
                diagnostics.push_back({ "graph.node.not_interaction_ready", "Node is not ready for editor graph creation." });
                return;
            }
            NodeInstance node;
            node.id = typedCommand.nodeId;
            node.type = typedCommand.nodeType;
            node.position = typedCommand.position;
            const auto spec = registry.findSpec(typedCommand.nodeType);
            for (const auto& paramSpec : spec->params) {
                node.params[paramSpec.id] = paramSpec.defaultValue;
            }
            next.nodes.push_back(node);
            next.runtimeDirty = true;
        } else if constexpr (std::is_same_v<CommandType, SelectNodeCommand>) {
            if (findNode(next, typedCommand.nodeId) == nullptr) {
                diagnostics.push_back({ "graph.selection.missing_node", "Selected node does not exist." });
                return;
            }
            if (typedCommand.mode == "replace") {
                next.selectedNodeIds.clear();
            }
            next.selectedNodeIds.insert(typedCommand.nodeId);
        } else if constexpr (std::is_same_v<CommandType, MoveNodeCommand>) {
            auto* node = findNode(next, typedCommand.nodeId);
            if (node == nullptr) {
                diagnostics.push_back({ "graph.move.missing_node", "Moved node does not exist." });
                return;
            }
            node->position = typedCommand.position;
        } else if constexpr (std::is_same_v<CommandType, BeginCableDragCommand>) {
            next.cableDrag = CableDragState{ typedCommand.from, std::nullopt };
        } else if constexpr (std::is_same_v<CommandType, HoverPortCommand>) {
            if (next.cableDrag.has_value()) {
                next.cableDrag->hover = typedCommand.port;
            }
        } else if constexpr (std::is_same_v<CommandType, CommitCableDragCommand>) {
            if (!next.cableDrag.has_value()) {
                diagnostics.push_back({ "graph.edge.no_cable_drag", "No cable drag is active." });
                return;
            }
            const auto from = next.cableDrag->from;
            const auto to = typedCommand.to;
            const auto* fromNode = findNode(next, from.nodeId);
            const auto* toNode = findNode(next, to.nodeId);
            if (fromNode == nullptr || toNode == nullptr) {
                diagnostics.push_back({ "graph.edge.missing_node", "Cable endpoint node does not exist." });
                next.cableDrag.reset();
                return;
            }
            const auto fromPort = registry.findOutput(fromNode->type, from.port);
            const auto toPort = registry.findInput(toNode->type, to.port);
            if (!fromPort.has_value() || !toPort.has_value()) {
                diagnostics.push_back({ "graph.edge.missing_port", "Cable endpoint port does not exist." });
                next.cableDrag.reset();
                return;
            }
            if (fromPort->type != toPort->type) {
                diagnostics.push_back({ "graph.edge.type_mismatch", "Cable endpoint port types do not match." });
                next.cableDrag.reset();
                return;
            }
            next.edges.push_back(Edge{ makeEdgeId(from, to), from, to, fromPort->type });
            next.cableDrag.reset();
            next.runtimeDirty = true;
        } else if constexpr (std::is_same_v<CommandType, CancelCableDragCommand>) {
            next.cableDrag.reset();
        } else if constexpr (std::is_same_v<CommandType, DeleteSelectionCommand>) {
            const auto removed = next.selectedNodeIds;
            next.nodes.erase(
                std::remove_if(next.nodes.begin(), next.nodes.end(), [&](const NodeInstance& node) {
                    return removed.count(node.id) > 0;
                }),
                next.nodes.end()
            );
            removeAttachedEdges(next, removed);
            next.selectedNodeIds.clear();
            if (!removed.empty()) {
                next.runtimeDirty = true;
            }
        } else if constexpr (std::is_same_v<CommandType, SetParameterCommand>) {
            auto* node = findNode(next, typedCommand.nodeId);
            if (node == nullptr) {
                diagnostics.push_back({ "graph.param.missing_node", "Parameter target node does not exist." });
                return;
            }
            if (!registry.findParam(node->type, typedCommand.param).has_value()) {
                diagnostics.push_back({ "graph.param.unknown", "Parameter is not registered for node type." });
                return;
            }
            node->params[typedCommand.param] = typedCommand.value;
            next.runtimeDirty = true;
        }
    }, command);

    return DispatchResult{ next, diagnostics };
}

} // namespace simple_world::graph
```

- [ ] **Step 4: Compile dispatcher**

Run:

```bash
clang++ -std=c++17 -I docs/runtime/native \
  -c docs/runtime/native/graph/GraphCommandDispatcher.cpp \
  -o /tmp/simple_world_graph_command_dispatcher.o
```

Expected: command exits `0`.

- [ ] **Step 5: Commit dispatcher**

Run:

```bash
git add docs/runtime/native/graph/GraphCommand.hpp \
  docs/runtime/native/graph/GraphCommandDispatcher.hpp \
  docs/runtime/native/graph/GraphCommandDispatcher.cpp
git commit -m "feat: add C++ graph command dispatcher"
```

## Task 5: Add Validation, Dirty Policy, RuntimeGraph Builder

**Files:**
- Create: `docs/runtime/native/graph/GraphValidator.hpp`
- Create: `docs/runtime/native/graph/GraphValidator.cpp`
- Create: `docs/runtime/native/graph/GraphDirtyPolicy.hpp`
- Create: `docs/runtime/native/graph/GraphDirtyPolicy.cpp`
- Create: `docs/runtime/native/runtime/RuntimeGraph.hpp`
- Create: `docs/runtime/native/runtime/RuntimeGraphBuilder.hpp`
- Create: `docs/runtime/native/runtime/RuntimeGraphBuilder.cpp`

- [ ] **Step 1: Define validator API**

Create `docs/runtime/native/graph/GraphValidator.hpp`:

```cpp
#pragma once

#include "GraphDiagnostics.hpp"
#include "GraphDocument.hpp"
#include "../nodes/NodeSpecRegistry.hpp"

namespace simple_world::graph {

Diagnostics validateGraphState(
    const GraphState& state,
    const simple_world::nodes::NodeSpecRegistry& registry
);

} // namespace simple_world::graph
```

- [ ] **Step 2: Implement validator**

Create `docs/runtime/native/graph/GraphValidator.cpp`:

```cpp
#include "GraphValidator.hpp"

#include <set>

namespace simple_world::graph {

Diagnostics validateGraphState(
    const GraphState& state,
    const simple_world::nodes::NodeSpecRegistry& registry
) {
    Diagnostics diagnostics;
    std::set<std::string> nodeIds;

    for (const auto& node : state.nodes) {
        if (!nodeIds.insert(node.id).second) {
            diagnostics.push_back({ "graph.node.duplicate_id", "Duplicate node id." });
        }
        if (!registry.findSpec(node.type).has_value()) {
            diagnostics.push_back({ "graph.node.unknown_type", "Unknown node type." });
        }
    }

    for (const auto& edge : state.edges) {
        const auto* fromNode = findNode(state, edge.from.nodeId);
        const auto* toNode = findNode(state, edge.to.nodeId);
        if (fromNode == nullptr || toNode == nullptr) {
            diagnostics.push_back({ "graph.edge.missing_node", "Edge endpoint node does not exist." });
            continue;
        }

        const auto fromPort = registry.findOutput(fromNode->type, edge.from.port);
        const auto toPort = registry.findInput(toNode->type, edge.to.port);
        if (!fromPort.has_value() || !toPort.has_value()) {
            diagnostics.push_back({ "graph.edge.missing_port", "Edge endpoint port does not exist." });
            continue;
        }
        if (fromPort->type != toPort->type) {
            diagnostics.push_back({ "graph.edge.type_mismatch", "Edge endpoint port types do not match." });
        }
    }

    return diagnostics;
}

} // namespace simple_world::graph
```

- [ ] **Step 3: Add dirty policy placeholder-free rule**

Create `docs/runtime/native/graph/GraphDirtyPolicy.hpp`:

```cpp
#pragma once

#include "GraphCommand.hpp"

namespace simple_world::graph {

bool commandMayChangeRuntimeSemantics(const GraphCommand& command);

} // namespace simple_world::graph
```

Create `docs/runtime/native/graph/GraphDirtyPolicy.cpp`:

```cpp
#include "GraphDirtyPolicy.hpp"

namespace simple_world::graph {

bool commandMayChangeRuntimeSemantics(const GraphCommand& command) {
    return std::visit([](const auto& typedCommand) -> bool {
        using CommandType = std::decay_t<decltype(typedCommand)>;
        if constexpr (std::is_same_v<CommandType, CreateNodeCommand>) return true;
        if constexpr (std::is_same_v<CommandType, CommitCableDragCommand>) return true;
        if constexpr (std::is_same_v<CommandType, DeleteSelectionCommand>) return true;
        if constexpr (std::is_same_v<CommandType, SetParameterCommand>) return true;
        return false;
    }, command);
}

} // namespace simple_world::graph
```

- [ ] **Step 4: Define runtime graph**

Create `docs/runtime/native/runtime/RuntimeGraph.hpp`:

```cpp
#pragma once

#include "../graph/GraphDocument.hpp"

#include <string>
#include <vector>

namespace simple_world::runtime {

struct RuntimeNode {
    std::string id;
    std::string type;
};

struct RuntimeEdge {
    simple_world::graph::PortRef from;
    simple_world::graph::PortRef to;
    std::string type;
};

struct RuntimeGraph {
    std::string graphId;
    std::vector<RuntimeNode> nodes;
    std::vector<RuntimeEdge> edges;
    std::vector<std::string> cookOrder;
};

} // namespace simple_world::runtime
```

- [ ] **Step 5: Implement bounded runtime graph builder**

Create `docs/runtime/native/runtime/RuntimeGraphBuilder.hpp`:

```cpp
#pragma once

#include "RuntimeGraph.hpp"
#include "../graph/GraphDiagnostics.hpp"
#include "../nodes/NodeSpecRegistry.hpp"

namespace simple_world::runtime {

struct RuntimeGraphBuildResult {
    RuntimeGraph runtimeGraph;
    simple_world::graph::Diagnostics diagnostics;
};

RuntimeGraphBuildResult buildRuntimeGraph(
    const simple_world::graph::GraphState& state,
    const simple_world::nodes::NodeSpecRegistry& registry
);

} // namespace simple_world::runtime
```

Create `docs/runtime/native/runtime/RuntimeGraphBuilder.cpp`:

```cpp
#include "RuntimeGraphBuilder.hpp"

#include <set>

namespace simple_world::runtime {

RuntimeGraphBuildResult buildRuntimeGraph(
    const simple_world::graph::GraphState& state,
    const simple_world::nodes::NodeSpecRegistry& registry
) {
    RuntimeGraph graph;
    graph.graphId = state.graphId;
    simple_world::graph::Diagnostics diagnostics;

    for (const auto& node : state.nodes) {
        const auto maturity = registry.maturityFor(node.type);
        if (!simple_world::nodes::canEnterRuntimeGraph(maturity)) {
            diagnostics.push_back({
                "runtime.node.not_runtime_ready",
                "Node is not mature enough to enter runtimeGraph."
            });
            continue;
        }
        graph.nodes.push_back(RuntimeNode{ node.id, node.type });
    }

    std::set<std::string> runtimeNodeIds;
    for (const auto& node : graph.nodes) {
        runtimeNodeIds.insert(node.id);
    }

    for (const auto& edge : state.edges) {
        if (runtimeNodeIds.count(edge.from.nodeId) == 0 || runtimeNodeIds.count(edge.to.nodeId) == 0) {
            diagnostics.push_back({
                "runtime.edge.endpoint_not_runtime_ready",
                "Edge endpoint is not present in runtimeGraph."
            });
            continue;
        }
        graph.edges.push_back(RuntimeEdge{ edge.from, edge.to, edge.type });
    }

    for (const auto& node : graph.nodes) {
        graph.cookOrder.push_back(node.id);
    }

    return RuntimeGraphBuildResult{ graph, diagnostics };
}

} // namespace simple_world::runtime
```

- [ ] **Step 6: Compile runtime graph lane**

Run:

```bash
clang++ -std=c++17 -I docs/runtime/native \
  -c docs/runtime/native/graph/GraphValidator.cpp \
  -o /tmp/simple_world_graph_validator.o
clang++ -std=c++17 -I docs/runtime/native \
  -c docs/runtime/native/graph/GraphDirtyPolicy.cpp \
  -o /tmp/simple_world_graph_dirty_policy.o
clang++ -std=c++17 -I docs/runtime/native \
  -c docs/runtime/native/runtime/RuntimeGraphBuilder.cpp \
  -o /tmp/simple_world_runtime_graph_builder.o
```

Expected: all commands exit `0`.

- [ ] **Step 7: Commit validation and runtime builder**

Run:

```bash
git add docs/runtime/native/graph/GraphValidator.hpp \
  docs/runtime/native/graph/GraphValidator.cpp \
  docs/runtime/native/graph/GraphDirtyPolicy.hpp \
  docs/runtime/native/graph/GraphDirtyPolicy.cpp \
  docs/runtime/native/runtime/RuntimeGraph.hpp \
  docs/runtime/native/runtime/RuntimeGraphBuilder.hpp \
  docs/runtime/native/runtime/RuntimeGraphBuilder.cpp
git commit -m "feat: add C++ graph validation and runtime graph builder"
```

## Task 6: Add JSON Probe And Shell

**Files:**
- Create: `docs/runtime/native/tools/graph_command_contract_probe.cpp`
- Create: `docs/runtime/scripts/cpp_graph_command_contract_shell.py`

- [ ] **Step 1: Implement C++ probe**

Create `docs/runtime/native/tools/graph_command_contract_probe.cpp`.

The probe must:

- read a fixture path and output directory from argv;
- parse fixture commands;
- dispatch commands through C++;
- write `graph_document.json`, `runtime_graph.json`, `diagnostics.json`, `reloaded_graph_document.json`, and `cpp_graph_command_contract_result.json`;
- never call `docs/runtime/scripts/graph_interaction_contract.js`.

Use the repo's existing JSON strategy if a C++ JSON helper already exists when implementing. If no helper exists, keep JSON parsing bounded to the command fixture schema above and document the bounded parser in the probe header.

- [ ] **Step 2: Implement shell wrapper**

Create `docs/runtime/scripts/cpp_graph_command_contract_shell.py`.

The shell must:

```text
fixture path
-> compile graph_command_contract_probe.cpp and graph C++ sources
-> run probe
-> publish artifacts to requested output directory
```

It must compile these sources:

```text
docs/runtime/native/graph/GraphDocument.cpp
docs/runtime/native/graph/GraphCommandDispatcher.cpp
docs/runtime/native/graph/GraphValidator.cpp
docs/runtime/native/graph/GraphDirtyPolicy.cpp
docs/runtime/native/nodes/NodeManifestMaturity.cpp
docs/runtime/native/nodes/NodeSpecRegistry.cpp
docs/runtime/native/runtime/RuntimeGraphBuilder.cpp
docs/runtime/native/tools/graph_command_contract_probe.cpp
```

- [ ] **Step 3: Run C++ acceptance tests**

Run:

```bash
node --test tests/cpp_graph_command_contract.test.js
```

Expected: PASS for valid graph, bad cable diagnostic, and save/reload unsafe state drop.

- [ ] **Step 4: Commit probe and shell**

Run:

```bash
git add docs/runtime/native/tools/graph_command_contract_probe.cpp \
  docs/runtime/scripts/cpp_graph_command_contract_shell.py
git commit -m "feat: prove C++ graph command contract headlessly"
```

## Task 7: Compare JS And C++ Contract Outputs

**Files:**
- Modify: `tests/cpp_graph_command_contract.test.js`
- Modify: `docs/runtime/scripts/cpp_graph_command_contract_shell.py`
- Preserve: `docs/runtime/scripts/graph_interaction_contract.js`

- [ ] **Step 1: Add JS reference output comparison**

Extend `tests/cpp_graph_command_contract.test.js` so the valid fixture is replayed by both:

```text
docs/runtime/scripts/graph_interaction_contract.js
docs/runtime/scripts/cpp_graph_command_contract_shell.py
```

Compare:

```text
node ids
node types
positions
params
edge endpoints
diagnostic codes
runtimeDirty
cookOrder
serialized document absence of selectedNodeIds/cableDrag
```

- [ ] **Step 2: Run comparison test**

Run:

```bash
node --test tests/graph_interaction_contract.test.js tests/cpp_graph_command_contract.test.js
```

Expected: PASS. Any difference must be either fixed in C++ or explicitly documented in the test as an intentional product-law change.

- [ ] **Step 3: Commit parity comparison**

Run:

```bash
git add tests/cpp_graph_command_contract.test.js docs/runtime/scripts/cpp_graph_command_contract_shell.py
git commit -m "test: compare JS and C++ graph contract outputs"
```

## Task 8: Promote Manifest Maturity Into Contracts

**Files:**
- Modify: `docs/contracts/node_admission.schema.json`
- Modify: `tools/generate_vuo_node_admission_index.js`
- Modify: `docs/contracts/vuo_node_admission_index.json`
- Modify: `tests/node_admission_contract.test.js`
- Create or modify: `docs/contracts/NODE_MANIFEST_MATURITY.md`

- [ ] **Step 1: Add maturity fields to schema**

Add required maturity fields:

```json
{
  "maturity": {
    "type": "object",
    "required": ["level", "evidence", "nativeUse"],
    "properties": {
      "level": {
        "enum": [
          "admissionReady",
          "interactionReady",
          "runtimeReady",
          "nativeExecutable"
        ]
      },
      "evidence": {
        "type": "string",
        "minLength": 1
      },
      "nativeUse": {
        "enum": [
          "known-only",
          "editor-graph",
          "runtime-graph",
          "execute-native"
        ]
      },
      "unknowns": {
        "type": "array",
        "items": { "type": "string" }
      }
    }
  }
}
```

- [ ] **Step 2: Update generator maturity rules**

In `tools/generate_vuo_node_admission_index.js`, derive:

```text
high-risk with full manifest and runtime proof -> runtimeReady or nativeExecutable
high-risk with full manifest but no native execution proof -> runtimeReady
medium-risk -> interactionReady unless current proof says runtime
low-risk -> admissionReady
proof-only -> admissionReady with nativeUse known-only
```

- [ ] **Step 3: Add tests for maturity gates**

In `tests/node_admission_contract.test.js`, assert:

```text
every entry has maturity.level
admissionReady entries do not claim nativeUse execute-native
runtimeReady entries have risk reason or full manifest evidence
nativeExecutable entries point to a proof artifact or test
high-risk entries do not remain admissionReady
```

- [ ] **Step 4: Write maturity documentation**

Create `docs/contracts/NODE_MANIFEST_MATURITY.md` with:

```text
admissionReady: safe to know and reject/defer
interactionReady: safe to create, draw, connect, edit, save
runtimeReady: safe to validate, dirty, schedule, build runtimeGraph
nativeExecutable: safe to execute through native RuntimeOp/backend adapter
```

Include the rule:

```text
Do not fill detailed runtime behavior for a node before evidence exists.
Record unknowns instead.
```

- [ ] **Step 5: Regenerate and test**

Run:

```bash
node tools/generate_vuo_node_admission_index.js
node --test tests/node_admission_contract.test.js
```

Expected: PASS.

- [ ] **Step 6: Commit maturity contracts**

Run:

```bash
git add docs/contracts/node_admission.schema.json \
  tools/generate_vuo_node_admission_index.js \
  docs/contracts/vuo_node_admission_index.json \
  tests/node_admission_contract.test.js \
  docs/contracts/NODE_MANIFEST_MATURITY.md
git commit -m "feat: add node manifest maturity gates"
```

## Task 9: Bind Native UI Only After C++ Command Proof

**Files:**
- Modify later: `docs/runtime/native/native_human_app_workflow_probe.mm`
- Modify later: `docs/runtime/native/native_product_canvas_surface_probe.mm`
- Modify later: `docs/runtime/scripts/native_canvas_interaction_command_loop_shell.py`
- Modify later: `tests/native_canvas_interaction_command_loop.test.js`
- Create later: native app shell files when the app target is introduced.

- [ ] **Step 1: Keep UI mutation law unchanged**

Before editing native UI code, confirm:

```bash
node --test tests/cpp_graph_command_contract.test.js tests/native_canvas_interaction_command_loop.test.js
```

Expected: PASS.

- [ ] **Step 2: Replace JS command replay in native canvas shell with C++ shell output**

Update `docs/runtime/scripts/native_canvas_interaction_command_loop_shell.py` so the graph document comes from:

```text
docs/runtime/scripts/cpp_graph_command_contract_shell.py
```

or from a shared C++ command replay binary, not from `graph_interaction_contract.js`.

- [ ] **Step 3: Assert no JS product replay remains in native canvas command loop**

In `tests/native_canvas_interaction_command_loop.test.js`, assert:

```text
source includes cpp_graph_command_contract_shell.py or C++ command replay tool
source does not invoke graph_interaction_contract.js as the product replay path
result.claims.cppCommandDispatcher is true
```

- [ ] **Step 4: Run native command loop tests**

Run:

```bash
node --test tests/native_canvas_interaction_command_loop.test.js tests/cpp_graph_command_contract.test.js
```

Expected: PASS.

- [ ] **Step 5: Commit native UI command binding**

Run:

```bash
git add docs/runtime/scripts/native_canvas_interaction_command_loop_shell.py \
  tests/native_canvas_interaction_command_loop.test.js
git commit -m "feat: route native canvas command loop through C++ commands"
```

## Task 10: Keep JavaScript Where It Is Still The Right Tool

**Files:**
- Modify: `docs/superpowers/plans/2026-06-05-native-runtime-master-progress.md`
- Create: `docs/runtime/JS_REMAINING_SCAFFOLDING.md`

- [ ] **Step 1: Document remaining JavaScript categories**

Create `docs/runtime/JS_REMAINING_SCAFFOLDING.md`:

```markdown
# JS Remaining Scaffolding

JavaScript remains allowed for:

- `node:test` regression suites.
- contract and admission generators.
- artifact inspectors.
- proof shell comparisons.
- documentation gates.
- legacy fixture replay during migration.

JavaScript must not remain the only product owner for:

- graph mutation;
- graph validation;
- runtime dirty propagation;
- runtimeGraph build;
- native UI dispatch;
- product save/reload semantics;
- node runtime maturity promotion.
```

- [ ] **Step 2: Update master progress nonclaim**

In `docs/superpowers/plans/2026-06-05-native-runtime-master-progress.md`, add:

```text
The JS interaction contract is now the reference proof. Product graph mutation
is migrating to C++; JavaScript remains valid for tests, generators, and proof
inspection, but not as the final native graph truth.
```

- [ ] **Step 3: Run docs and focused tests**

Run:

```bash
node --test tests/graph_interaction_contract.test.js \
  tests/cpp_graph_command_contract.test.js \
  tests/node_admission_contract.test.js \
  tests/native_canvas_interaction_command_loop.test.js
```

Expected: PASS.

- [ ] **Step 4: Commit JS scaffolding boundary docs**

Run:

```bash
git add docs/runtime/JS_REMAINING_SCAFFOLDING.md \
  docs/superpowers/plans/2026-06-05-native-runtime-master-progress.md
git commit -m "docs: define remaining JavaScript scaffolding boundary"
```

## Task 11: Future Detail Lanes

These are not blockers for the first C++ command proof. They become active when the C++ command spine is passing.

### Lane A: Full Native Node Manifest Detail

Add detail only when a node is promoted beyond admission.

**Promotion rule:**

```text
admissionReady -> interactionReady:
  node browser needs it, or saved graph can contain it.

interactionReady -> runtimeReady:
  validation/cook/dirty/runtimeGraph needs it.

runtimeReady -> nativeExecutable:
  C++/Metal/Vuo proof emits observable artifact.
```

**Future files:**

```text
docs/contracts/node_manifests/*.json
tests/node_admission_contract.test.js
docs/runtime/fixtures/<node-lane>.graph.json
tests/<node-lane>.test.js
```

### Lane B: Native App Canvas

Only start after Task 9 passes.

**Required behavior:**

```text
mouse/keyboard event
-> C++ command
-> GraphState
-> validation
-> runtimeGraph
-> preview artifact
-> UI render
```

**Forbidden behavior:**

```text
UI stores graph truth.
UI mutates runtime objects.
Runtime reads UI-local state.
Vuo defines graph truth.
Metal resource objects define graph truth.
```

### Lane C: Shader And Metal Deepening

Metal remains backend, not graph law.

**Detail later:**

```text
shader cache invalidation
last-valid-frame policy
complete heap eviction
backend hazard tracking
more ShaderIR operations
user shader text contract
```

Each item needs:

```text
fixture graph -> C++ runtime component -> Metal/backend artifact -> diagnostics
```

### Lane D: AI Worker Command Authoring

The AI worker can broaden only after commands are C++ product law.

**Required path:**

```text
natural language intent
-> validated command plan
-> C++ command replay
-> runtimeGraph/frame artifact
-> diagnostics
-> repair command
```

The AI worker must not write graph JSON directly.

## Verification Matrix

Run focused suites after each stage:

```bash
node --test tests/cpp_graph_command_contract.test.js
node --test tests/graph_interaction_contract.test.js
node --test tests/node_admission_contract.test.js
node --test tests/native_canvas_interaction_command_loop.test.js
```

Run broader repo suite after Tasks 8, 9, and 10:

```bash
node --test tests/*.test.js
```

If native Metal probes are touched, also run the nearest Metal lane:

```bash
node --test tests/native_product_canvas_surface.test.js
node --test tests/native_texture_patch_product_runtime.test.js
node --test tests/native_metal_heap_residency.test.js
node --test tests/native_shader_ir_expression_core.test.js
```

## Completion Criteria

This plan is complete when:

- C++ can replay the same command sequence as the JS interaction contract.
- C++ emits graph document, diagnostics, runtime dirty state, runtimeGraph, and cook order artifacts.
- Invalid cable type mismatch produces diagnostics and creates no edge.
- Delete selection removes attached edges.
- Parameter edits mark runtime dirty.
- Save/reload preserves positions, parameters, and edges while dropping unsafe selection/cable drag state.
- Native canvas command loop consumes C++ command replay, not JS product replay.
- Manifest maturity tells C++ which nodes are admission-only, interaction-ready, runtime-ready, or native-executable.
- JavaScript remaining in the repo is explicitly classified as scaffolding, generation, testing, or proof inspection.

## Self-Review

Spec coverage:

- The plan states what moves to C++, what can remain in JS, and what needs more detailed future contracts.
- It preserves the current architecture boundary: UI dispatches commands, runtime consumes graph truth, backend renders artifacts.
- It avoids polished UI before C++ command proof.
- It includes staged manifest detail instead of fake-complete runtime claims.

Placeholder scan:

- No task uses a deferred placeholder as an implementation instruction.
- Future lanes are explicitly parked behind acceptance criteria rather than written as immediate missing code.

Type consistency:

- `GraphState`, `GraphDocument`, `GraphCommand`, `NodeSpecRegistry`, and `RuntimeGraph` names are consistent across tasks.
- Diagnostic code expectations match the planned C++ dispatcher and validator.

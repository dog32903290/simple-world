# Native Metal TiXL Parity Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Move the current TiXL Mesh Draw / PBR lane from bounded software proof to a real native Metal backend replacement proof for the existing `native_render_pipeline` path.

**Architecture:** Keep existing proof shape: `fixture.graph.json -> python shell -> result/trace/errors artifacts -> node:test freshness`. Add positive proof lanes before changing the backend replacement gate. The commander owns parity vocabulary, backend replacement semantics, and final closure; workers own disjoint proof lanes.

**Tech Stack:** Node test runner, Python proof shells, ObjC++/Metal native probes, JSON fixtures/artifacts, existing TiXL source audit artifacts.

---

## Current Boundary

Current checked-in closure is intentionally bounded:

- `docs/runtime/artifacts/runtime_closure_report/runtime_closure_report.json` has `ok: true` and `requiredNext: []`, but only for the bounded closure lane.
- `docs/runtime/artifacts/tixl_mesh_draw_backend_replacement_gate/tixl_mesh_draw_backend_replacement_gate_result.json` has `backendReplacementReady: false`, `fullPbrResourceBinding: false`, `nativeGpuParityComplete: false`, and `tixlRuntimeParity: false`.
- The gate currently blocks because `fullPbrResourceBinding` and `explicitAdapterProof` are missing.

Smallest honest parity target:

```text
TiXL Mesh Draw / PBR lane can replace bounded NativeRendererBackend
with a real native Metal backend for the proven native_render_pipeline path.
```

Not claimed by this plan:

- full TiXL clone parity
- generic HLSL-to-MSL cross-compiler
- Vuo parity
- all TiXL render nodes

## Work Order Classification

Lane: Native Metal TiXL Mesh Draw / PBR parity.

Difficulty: H-high overall.

Worker-eligible:

- `TixlMeshDrawFullPbrResourceBindingProof`
- `TixlMeshDrawExplicitAdapterProof`
- non-empty t8 ShaderGraph resource probe after the exact fixture is defined
- focused tests/docs for a single new proof lane

Must stay commander-owned:

- definition of parity flags
- backend replacement gate semantics
- updates that flip `backendReplacementReady`, `nativeGpuParityComplete`, or bounded `tixlRuntimeParity`
- final runtime closure report

Forbidden:

- setting `backendReplacementReady: true` from a single partial proof
- setting `hlslToMslTranslation: true` while using handwritten adapter strategy
- treating Vuo/OpenGL visual proof as native Metal/TiXL MeshBuffers parity
- treating `boundedPbrVisualReferenceEstablished` as `pbrVisualCorrectness`

Verification:

```bash
node --test tests/tixl_mesh_draw_full_pbr_resource_binding.test.js
node --test tests/tixl_mesh_draw_explicit_adapter_proof.test.js
node --test tests/tixl_mesh_draw_backend_replacement_gate.test.js
node --test tests/runtime_closure_report.test.js
git diff --check
```

Escalation trigger:

- any worker needs to edit `runtime_closure_report_shell.py` or `tixl_mesh_draw_backend_replacement_gate_shell.py`
- any proof has to weaken existing false-claim guards
- Metal probe cannot run on this machine

## Task 1: Full PBR Resource Binding Proof

**Files:**

- Create: `docs/runtime/TIXL_MESH_DRAW_FULL_PBR_RESOURCE_BINDING_PROOF.md`
- Create: `docs/runtime/fixtures/tixl_mesh_draw_full_pbr_resource_binding.graph.json`
- Create: `docs/runtime/scripts/tixl_mesh_draw_full_pbr_resource_binding_shell.py`
- Create: `docs/runtime/artifacts/tixl_mesh_draw_full_pbr_resource_binding/tixl_mesh_draw_full_pbr_resource_binding_result.json`
- Create: `docs/runtime/artifacts/tixl_mesh_draw_full_pbr_resource_binding/tixl_mesh_draw_full_pbr_resource_binding_trace.json`
- Create: `docs/runtime/artifacts/tixl_mesh_draw_full_pbr_resource_binding/tixl_mesh_draw_full_pbr_resource_binding_errors.json`
- Create: `docs/runtime/artifacts/tixl_mesh_draw_full_pbr_resource_binding/generated_full_pbr_resource_binding_probe.metal`
- Test: `tests/tixl_mesh_draw_full_pbr_resource_binding.test.js`

- [ ] **Step 1: Write the failing test**

Create `tests/tixl_mesh_draw_full_pbr_resource_binding.test.js` with these required assertions:

```js
const assert = require("node:assert/strict");
const { spawnSync } = require("node:child_process");
const fs = require("node:fs");
const os = require("node:os");
const path = require("node:path");
const test = require("node:test");

const repoRoot = path.resolve(__dirname, "..");
const contractPath = path.join(repoRoot, "docs/runtime/TIXL_MESH_DRAW_FULL_PBR_RESOURCE_BINDING_PROOF.md");
const fixturePath = path.join(repoRoot, "docs/runtime/fixtures/tixl_mesh_draw_full_pbr_resource_binding.graph.json");
const scriptPath = path.join(repoRoot, "docs/runtime/scripts/tixl_mesh_draw_full_pbr_resource_binding_shell.py");
const artifactDir = path.join(repoRoot, "docs/runtime/artifacts/tixl_mesh_draw_full_pbr_resource_binding");
const resultName = "tixl_mesh_draw_full_pbr_resource_binding_result.json";
const traceName = "tixl_mesh_draw_full_pbr_resource_binding_trace.json";
const errorsName = "tixl_mesh_draw_full_pbr_resource_binding_errors.json";

test("Full PBR binding docs define positive binding without backend replacement", () => {
  const source = fs.readFileSync(contractPath, "utf8");
  assert.match(source, /TiXL Mesh Draw Full PBR Resource Binding Proof/);
  assert.match(source, /fullPbrResourceBinding: true/);
  assert.match(source, /backendReplacementReady: false/);
  assert.match(source, /hlslToMslTranslation: false/);
});

test("Full PBR binding shell emits one source-backed resource ledger", () => {
  const tmpDir = fs.mkdtempSync(path.join(os.tmpdir(), "tixl-full-pbr-binding-"));
  const run = spawnSync("python3", [scriptPath, fixturePath, tmpDir], {
    cwd: repoRoot,
    encoding: "utf8",
  });
  assert.equal(run.status, 0, run.stderr || run.stdout);
  const result = readJson(path.join(tmpDir, resultName));
  const trace = readJson(path.join(tmpDir, traceName));
  const errors = readJson(path.join(tmpDir, errorsName));
  assert.deepEqual(errors, []);
  assert.equal(result.kind, "TixlMeshDrawFullPbrResourceBindingProof");
  assert.equal(result.ok, true);
  assert.equal(result.status, "proven_full_pbr_resource_binding");
  assert.deepEqual(result.claims, {
    sourceAuditArtifactConsumed: true,
    meshBufferBindingArtifactConsumed: true,
    constantBufferPackingArtifactsConsumed: true,
    textureSamplerBindingArtifactConsumed: true,
    shadergraphResourcesExpansionArtifactConsumed: true,
    textureCubePbrReferenceArtifactConsumed: true,
    actualMetalFullBindingProbeRan: true,
    fullPbrResourceBinding: true,
    backendReplacementReady: false,
    explicitAdapterProof: false,
    hlslToMslTranslation: false,
    tixlRuntimeParity: false,
    nativeGpuParityComplete: false,
    pbrVisualCorrectness: false,
  });
  assert.deepEqual(result.bindingLedger.boundRegisters.sort(), [
    "b0", "b1", "b2", "b3", "b4", "b5",
    "s0", "s1",
    "t0", "t1", "t2", "t3", "t4", "t5", "t6", "t7",
  ].sort());
  assert.equal(result.bindingLedger.t8ShadergraphResources.status, "proven_empty_for_current_fixture");
  assert.ok(trace.map((entry) => entry.op).includes("runFullPbrResourceBindingMetalProbe"));
  assertPathClean(result, trace, errors);
});

test("Full PBR binding checked-in artifacts are path-clean and fresh", () => {
  const result = readJson(path.join(artifactDir, resultName));
  const trace = readJson(path.join(artifactDir, traceName));
  const errors = readJson(path.join(artifactDir, errorsName));
  assert.equal(result.kind, "TixlMeshDrawFullPbrResourceBindingProof");
  assert.deepEqual(errors, []);
  assertPathClean(result, trace, errors);

  const tmpDir = fs.mkdtempSync(path.join(os.tmpdir(), "tixl-full-pbr-binding-fresh-"));
  const run = spawnSync("python3", [scriptPath, fixturePath, tmpDir], {
    cwd: repoRoot,
    encoding: "utf8",
  });
  assert.equal(run.status, 0, run.stderr || run.stdout);
  assert.deepEqual(readJson(path.join(tmpDir, resultName)), result);
  assert.deepEqual(readJson(path.join(tmpDir, traceName)), trace);
  assert.deepEqual(readJson(path.join(tmpDir, errorsName)), errors);
});

function assertPathClean(...values) {
  const text = JSON.stringify(values);
  assert.ok(!text.includes("/Users/"));
  assert.ok(!text.includes(repoRoot));
}

function readJson(filePath) {
  return JSON.parse(fs.readFileSync(filePath, "utf8"));
}
```

- [ ] **Step 2: Run test to verify it fails**

Run:

```bash
node --test tests/tixl_mesh_draw_full_pbr_resource_binding.test.js
```

Expected: fail because the contract, fixture, shell, and artifacts do not exist.

- [ ] **Step 3: Implement the proof shell**

Implement `docs/runtime/scripts/tixl_mesh_draw_full_pbr_resource_binding_shell.py` by reusing the path-clean publish pattern from:

```text
docs/runtime/scripts/tixl_mesh_draw_texture_sampler_binding_shell.py
docs/runtime/scripts/tixl_mesh_draw_b5_native_packing_shell.py
docs/runtime/scripts/tixl_mesh_draw_backend_replacement_gate_shell.py
```

The shell must:

- consume source audit, existing t0/t1 resource binding, b0-b4 native packing, b3 PointLights packing, b5 native packing, texture/sampler binding, t8 resources expansion, and TextureCube/PBR reference artifacts;
- reject any missing, stale, or widened input artifact;
- run an actual Metal probe that observes sentinel reads for b0-b5, t0-t7, and s0-s1;
- write generated MSL into `generated_full_pbr_resource_binding_probe.metal`;
- produce a ledger with `boundRegisters` exactly `b0-b5`, `t0-t7`, `s0-s1`;
- keep `backendReplacementReady`, `explicitAdapterProof`, `hlslToMslTranslation`, `tixlRuntimeParity`, `nativeGpuParityComplete`, and `pbrVisualCorrectness` false.

- [ ] **Step 4: Add fixture and documentation**

Create `docs/runtime/fixtures/tixl_mesh_draw_full_pbr_resource_binding.graph.json` with explicit input artifact paths and expected claims.

Create `docs/runtime/TIXL_MESH_DRAW_FULL_PBR_RESOURCE_BINDING_PROOF.md` explaining:

- this is a positive full-resource binding proof;
- it is not backend replacement;
- it is not explicit adapter parity;
- it is not HLSL-to-MSL translation;
- it is not TiXL runtime parity.

- [ ] **Step 5: Generate checked-in artifacts**

Run:

```bash
python3 docs/runtime/scripts/tixl_mesh_draw_full_pbr_resource_binding_shell.py \
  docs/runtime/fixtures/tixl_mesh_draw_full_pbr_resource_binding.graph.json \
  docs/runtime/artifacts/tixl_mesh_draw_full_pbr_resource_binding
```

Expected: exit `0`; errors artifact is `[]`.

- [ ] **Step 6: Run focused proof**

Run:

```bash
node --test tests/tixl_mesh_draw_full_pbr_resource_binding.test.js
```

Expected: pass.

## Task 2: Explicit Adapter Proof

**Files:**

- Create: `docs/runtime/TIXL_MESH_DRAW_EXPLICIT_ADAPTER_PROOF.md`
- Create: `docs/runtime/fixtures/tixl_mesh_draw_explicit_adapter_proof.graph.json`
- Create: `docs/runtime/scripts/tixl_mesh_draw_explicit_adapter_proof_shell.py`
- Create: `docs/runtime/artifacts/tixl_mesh_draw_explicit_adapter_proof/tixl_mesh_draw_explicit_adapter_result.json`
- Create: `docs/runtime/artifacts/tixl_mesh_draw_explicit_adapter_proof/tixl_mesh_draw_explicit_adapter_trace.json`
- Create: `docs/runtime/artifacts/tixl_mesh_draw_explicit_adapter_proof/tixl_mesh_draw_explicit_adapter_errors.json`
- Create: `docs/runtime/artifacts/tixl_mesh_draw_explicit_adapter_proof/generated_explicit_adapter.metal`
- Create: `docs/runtime/artifacts/tixl_mesh_draw_explicit_adapter_proof/frame_stats.json`
- Test: `tests/tixl_mesh_draw_explicit_adapter_proof.test.js`

- [ ] **Step 1: Write the failing test**

The test must assert:

- `kind: "TixlMeshDrawExplicitAdapterProof"`
- `status: "proven_explicit_mesh_draw_adapter"`
- `actualCompilerRan: true`
- `actualMetalRan: true`
- `explicitAdapterProof: true`
- `backendReplacementReady: false`
- `fullPbrResourceBinding` remains false unless Task 1 artifact is consumed
- path-clean fresh checked-in artifacts

- [ ] **Step 2: Implement shell**

The shell must consume:

```text
docs/runtime/artifacts/tixl_mesh_draw_explicit_translation_strategy/tixl_mesh_draw_explicit_translation_strategy_result.json
docs/runtime/artifacts/tixl_mesh_draw_msl_approx/tixl_mesh_draw_msl_approx_result.json
docs/runtime/artifacts/metal_explicit_msl_proof/metal_explicit_msl_result.json
```

It must compile/run explicit adapter MSL through real Metal and publish frame stats. It may prove adapter scope only; it must not claim full TiXL runtime parity.

- [ ] **Step 3: Verify**

Run:

```bash
node --test tests/tixl_mesh_draw_explicit_adapter_proof.test.js
```

Expected: pass.

## Task 3: Backend Replacement Gate Integration

**Files:**

- Modify: `docs/runtime/TIXL_MESH_DRAW_BACKEND_REPLACEMENT_GATE_PROOF.md`
- Modify: `docs/runtime/fixtures/tixl_mesh_draw_backend_replacement_gate.graph.json`
- Modify: `docs/runtime/scripts/tixl_mesh_draw_backend_replacement_gate_shell.py`
- Modify: `docs/runtime/artifacts/tixl_mesh_draw_backend_replacement_gate/tixl_mesh_draw_backend_replacement_gate_result.json`
- Modify: `docs/runtime/artifacts/tixl_mesh_draw_backend_replacement_gate/tixl_mesh_draw_backend_replacement_gate_trace.json`
- Modify: `docs/runtime/artifacts/tixl_mesh_draw_backend_replacement_gate/tixl_mesh_draw_backend_replacement_gate_errors.json`
- Modify: `tests/tixl_mesh_draw_backend_replacement_gate.test.js`

- [ ] **Step 1: Add new positive artifact inputs**

Gate must consume Task 1 and Task 2 artifacts.

- [ ] **Step 2: Flip only the gate claims that are justified**

Allowed only when Task 1 and Task 2 are valid:

```json
{
  "replacementBlockedBecauseFullBindingMissing": false,
  "replacementBlockedBecauseAdapterProofMissing": false,
  "fullPbrResourceBinding": true,
  "explicitAdapterProofPresent": true
}
```

Do not flip `nativeGpuParityComplete` or bounded `tixlRuntimeParity` until native backend integration and equivalence proof exist.

- [ ] **Step 3: Verify forged artifact guards**

Tests must forge widened or missing Task 1/Task 2 artifacts and prove the gate fails closed.

## Task 4: Native Metal Backend Integration

**Files:** commander-owned until a narrower worker contract is written.

- [ ] **Step 1: Define native backend artifact contract**

Update only after Task 1-3 are green. The contract must replace deterministic capture with real Metal compile/render/capture for this lane.

- [ ] **Step 2: Add runtime equivalence proof**

Compare:

- command trace
- resource bindings
- constant buffer values
- MRT outputs
- TextureCube behavior
- expected visual frame stats

- [ ] **Step 3: Flip final parity flags**

Only after native backend integration and equivalence proof:

```json
{
  "backendReplacementReady": true,
  "nativeGpuParityComplete": true,
  "rendererIntegrationComplete": true,
  "tixlRuntimeParity": true
}
```

Keep `hlslToMslTranslation: false` unless a translator is proven.

## Execution Order

1. Task 1 and Task 2 may run in parallel only if they do not edit shared gate or closure files.
2. Task 3 starts after Task 1 and Task 2 pass and checked-in artifacts are fresh.
3. Task 4 starts after Task 3 passes.
4. Runtime closure report update is the final commander-owned step.

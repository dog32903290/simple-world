const assert = require("node:assert/strict");
const { spawnSync } = require("node:child_process");
const fs = require("node:fs");
const os = require("node:os");
const path = require("node:path");
const test = require("node:test");

const repoRoot = path.resolve(__dirname, "..");
const contractPath = path.join(repoRoot, "docs/runtime/TIXL_MESH_DRAW_EXPLICIT_ADAPTER_PROOF.md");
const fixturePath = path.join(repoRoot, "docs/runtime/fixtures/tixl_mesh_draw_explicit_adapter_proof.graph.json");
const scriptPath = path.join(repoRoot, "docs/runtime/scripts/tixl_mesh_draw_explicit_adapter_proof_shell.py");
const artifactDir = path.join(repoRoot, "docs/runtime/artifacts/tixl_mesh_draw_explicit_adapter_proof");
const resultName = "tixl_mesh_draw_explicit_adapter_result.json";
const traceName = "tixl_mesh_draw_explicit_adapter_trace.json";
const errorsName = "tixl_mesh_draw_explicit_adapter_errors.json";
const mslName = "generated_explicit_adapter.metal";
const frameStatsName = "frame_stats.json";

test("TiXL mesh draw explicit adapter docs state only the named adapter scope", () => {
  const source = fs.readFileSync(contractPath, "utf8");

  assert.match(source, /TiXL Mesh Draw Explicit Adapter Proof/);
  assert.match(source, /TixlMeshDrawExplicitAdapterProof answers:/);
  assert.match(source, /handwritten_explicit_msl_adapter/);
  assert.match(source, /explicitAdapterProof: true/);
  assert.match(source, /explicitAdapterProofPresent: true/);
  assert.match(source, /backendReplacementReady: false/);
  assert.match(source, /fullPbrResourceBinding: false/);
  assert.match(source, /hlslToMslTranslation: false/);
  assert.match(source, /tixlRuntimeParity: false/);
  assert.match(source, /nativeGpuParityComplete: false/);
  assert.match(source, /pbrVisualCorrectness: false/);
});

test("TiXL mesh draw explicit adapter fixture pins evidence artifacts and bounded claims", () => {
  const graph = readJson(fixturePath);

  assert.equal(graph.graphId, "fixture.tixl_mesh_draw_explicit_adapter_proof");
  assert.equal(graph.kind, "TixlMeshDrawExplicitAdapterProof");
  assert.equal(
    graph.explicitTranslationStrategyArtifact,
    "docs/runtime/artifacts/tixl_mesh_draw_explicit_translation_strategy/tixl_mesh_draw_explicit_translation_strategy_result.json",
  );
  assert.equal(
    graph.mslApproxArtifact,
    "docs/runtime/artifacts/tixl_mesh_draw_msl_approx/tixl_mesh_draw_msl_approx_result.json",
  );
  assert.equal(
    graph.metalExplicitMslArtifact,
    "docs/runtime/artifacts/metal_explicit_msl_proof/metal_explicit_msl_result.json",
  );
  assert.equal(graph.adapterScope.name, "handwritten_explicit_msl_adapter");
  assert.equal(graph.adapterScope.proofScope, "explicit_adapter_only");
  assert.deepEqual(graph.expected.claims, expectedClaims(true));
});

test("TiXL mesh draw explicit adapter shell emits positive adapter proof only", () => {
  const tmpDir = fs.mkdtempSync(path.join(os.tmpdir(), "tixl-mesh-draw-explicit-adapter-"));
  const run = spawnSync("python3", [scriptPath, fixturePath, tmpDir], {
    cwd: repoRoot,
    encoding: "utf8",
  });

  assert.equal(run.status, 0, run.stderr || run.stdout);
  const result = readJson(path.join(tmpDir, resultName));
  const trace = readJson(path.join(tmpDir, traceName));
  const errors = readJson(path.join(tmpDir, errorsName));
  const frameStats = readJson(path.join(tmpDir, frameStatsName));
  const msl = fs.readFileSync(path.join(tmpDir, mslName), "utf8");

  assert.deepEqual(errors, []);
  assert.equal(result.kind, "TixlMeshDrawExplicitAdapterProof");
  assert.equal(result.ok, true);
  assert.equal(result.status, "proven_explicit_mesh_draw_adapter");
  assert.deepEqual(result.claims, expectedClaims(true));
  assert.equal(result.adapterScope.name, "handwritten_explicit_msl_adapter");
  assert.equal(result.adapterScope.proofScope, "explicit_adapter_only");
  assert.equal(result.adapterScope.fullPbrResourceBindingConsumed, false);
  assert.equal(result.actualMetalProbe.status, "proven_explicit_mesh_draw_adapter");
  assert.equal(result.actualMetalProbe.actualCompilerRan, true);
  assert.equal(result.actualMetalProbe.actualMetalRan, true);
  assert.equal(result.actualMetalProbe.generatedMslArtifact, mslName);
  assert.deepEqual(result.actualMetalProbe.color0, [17, 113, 191, 255]);
  assert.equal(result.frameStats.frameDigest, frameStats.frameDigest);
  assert.equal(frameStats.width, 4);
  assert.equal(frameStats.height, 4);
  assert.equal(frameStats.byteCount, 64);
  assert.equal(frameStats.nonBlack, true);
  assert.equal(frameStats.varied, true);
  assert.match(msl, /my_world_explicit_adapter_vertex/);
  assert.match(msl, /my_world_explicit_adapter_fragment/);
  assert.match(msl, /\[\[vertex_id\]\]/);
  assert.match(msl, /\[\[stage_in\]\]/);
  assert.deepEqual(trace.map((entry) => entry.op), [
    "loadTixlMeshDrawExplicitAdapterFixture",
    "resolveInputArtifacts",
    "readInputArtifacts",
    "validateInputArtifacts",
    "writeExplicitAdapterMsl",
    "buildExplicitAdapterMetalProbe",
    "runExplicitAdapterMetalProbe",
    "validateExplicitAdapterMetalProbe",
    "proveExplicitAdapterScopeOnly",
    "publishTixlMeshDrawExplicitAdapterArtifacts",
  ]);
  assertPathClean(result, trace, errors, frameStats);
});

test("TiXL mesh draw explicit adapter shell blocks widened fixture claims", () => {
  const tmpDir = fs.mkdtempSync(path.join(os.tmpdir(), "tixl-mesh-draw-explicit-adapter-widened-"));
  const fixture = readJson(fixturePath);
  fixture.expected.claims.backendReplacementReady = true;
  fixture.expected.claims.tixlRuntimeParity = true;
  fixture.expected.claims.fullPbrResourceBinding = true;
  const badFixturePath = path.join(tmpDir, "widened-explicit-adapter.graph.json");
  fs.writeFileSync(badFixturePath, JSON.stringify(fixture, null, 2));

  const run = spawnSync("python3", [scriptPath, badFixturePath, tmpDir], {
    cwd: repoRoot,
    encoding: "utf8",
  });

  assert.equal(run.status, 1);
  const result = readJson(path.join(tmpDir, resultName));
  const errors = readJson(path.join(tmpDir, errorsName));
  assert.equal(result.ok, false);
  assert.equal(result.status, "blocked_invalid_fixture_expectations");
  const fields = errors.flatMap((error) => error.mismatches || []).map((mismatch) => mismatch.field);
  assert.ok(fields.includes("expected.claims.backendReplacementReady"));
  assert.ok(fields.includes("expected.claims.tixlRuntimeParity"));
  assert.ok(fields.includes("expected.claims.fullPbrResourceBinding"));
  assertPathClean(result, errors);
});

test("TiXL mesh draw explicit adapter shell blocks widened strategy evidence", () => {
  const tmpDir = fs.mkdtempSync(path.join(os.tmpdir(), "tixl-mesh-draw-explicit-adapter-strategy-"));
  const strategy = readJson(path.join(
    repoRoot,
    "docs/runtime/artifacts/tixl_mesh_draw_explicit_translation_strategy/tixl_mesh_draw_explicit_translation_strategy_result.json",
  ));
  strategy.selectedStrategy = "mechanical_hlsl_to_msl_translation";
  strategy.claims.hlslToMslTranslation = true;
  const strategyPath = path.join(tmpDir, "widened-strategy.json");
  fs.writeFileSync(strategyPath, JSON.stringify(strategy, null, 2));
  const fixture = readJson(fixturePath);
  fixture.explicitTranslationStrategyArtifact = strategyPath;
  const badFixturePath = path.join(tmpDir, "widened-strategy.graph.json");
  fs.writeFileSync(badFixturePath, JSON.stringify(fixture, null, 2));

  const run = spawnSync("python3", [scriptPath, badFixturePath, tmpDir], {
    cwd: repoRoot,
    encoding: "utf8",
  });

  assert.equal(run.status, 1);
  const result = readJson(path.join(tmpDir, resultName));
  const errors = readJson(path.join(tmpDir, errorsName));
  assert.equal(result.status, "blocked_invalid_input_artifact");
  const fields = errors.flatMap((error) => error.mismatches || []).map((mismatch) => mismatch.field);
  assert.ok(fields.includes("strategy.selectedStrategy"));
  assert.ok(fields.includes("strategy.claims.hlslToMslTranslation"));
  assertPathClean(result, errors);
});

test("TiXL mesh draw explicit adapter checked-in artifacts are path-clean and fresh", () => {
  const result = readJson(path.join(artifactDir, resultName));
  const trace = readJson(path.join(artifactDir, traceName));
  const errors = readJson(path.join(artifactDir, errorsName));
  const frameStats = readJson(path.join(artifactDir, frameStatsName));
  const msl = fs.readFileSync(path.join(artifactDir, mslName), "utf8");

  assert.equal(result.kind, "TixlMeshDrawExplicitAdapterProof");
  assert.equal(result.ok, true);
  assert.equal(result.status, "proven_explicit_mesh_draw_adapter");
  assert.deepEqual(result.claims, expectedClaims(true));
  assert.deepEqual(errors, []);
  assert.equal(result.frameStats.frameDigest, frameStats.frameDigest);
  assertPathClean(result, trace, errors, frameStats);

  const tmpDir = fs.mkdtempSync(path.join(os.tmpdir(), "tixl-mesh-draw-explicit-adapter-fresh-"));
  const run = spawnSync("python3", [scriptPath, fixturePath, tmpDir], {
    cwd: repoRoot,
    encoding: "utf8",
  });
  assert.equal(run.status, 0, run.stderr || run.stdout);
  assert.deepEqual(readJson(path.join(tmpDir, resultName)), result);
  assert.deepEqual(readJson(path.join(tmpDir, traceName)), trace);
  assert.deepEqual(readJson(path.join(tmpDir, errorsName)), errors);
  assert.deepEqual(readJson(path.join(tmpDir, frameStatsName)), frameStats);
  assert.equal(fs.readFileSync(path.join(tmpDir, mslName), "utf8"), msl);
});

function expectedClaims(proven) {
  return {
    explicitTranslationStrategyArtifactConsumed: proven,
    mslApproxArtifactConsumed: proven,
    metalExplicitMslArtifactConsumed: proven,
    selectedHandwrittenAdapterStrategyConsumed: proven,
    mslApproxEvidenceConsumed: proven,
    metalExplicitMslEvidenceConsumed: proven,
    actualCompilerRan: proven,
    actualMetalRan: proven,
    explicitAdapterProof: proven,
    explicitAdapterProofPresent: proven,
    fullPbrResourceBinding: false,
    backendReplacementReady: false,
    hlslToMslTranslation: false,
    tixlRuntimeParity: false,
    nativeGpuParityComplete: false,
    pbrVisualCorrectness: false,
  };
}

function assertPathClean(...values) {
  const text = JSON.stringify(values);
  assert.ok(!text.includes("/Users/"));
  assert.ok(!text.includes(repoRoot));
}

function readJson(filePath) {
  return JSON.parse(fs.readFileSync(filePath, "utf8"));
}

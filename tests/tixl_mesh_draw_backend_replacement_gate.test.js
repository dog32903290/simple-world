const assert = require("node:assert/strict");
const { spawnSync } = require("node:child_process");
const fs = require("node:fs");
const os = require("node:os");
const path = require("node:path");
const test = require("node:test");

const repoRoot = path.resolve(__dirname, "..");
const contractPath = path.join(repoRoot, "docs/runtime/TIXL_MESH_DRAW_BACKEND_REPLACEMENT_GATE_PROOF.md");
const fixturePath = path.join(repoRoot, "docs/runtime/fixtures/tixl_mesh_draw_backend_replacement_gate.graph.json");
const scriptPath = path.join(repoRoot, "docs/runtime/scripts/tixl_mesh_draw_backend_replacement_gate_shell.py");
const artifactDir = path.join(repoRoot, "docs/runtime/artifacts/tixl_mesh_draw_backend_replacement_gate");
const resourceBindingArtifactPath = path.join(repoRoot, "docs/runtime/artifacts/tixl_mesh_draw_resource_binding/tixl_mesh_draw_resource_binding_result.json");
const fullPbrBindingArtifactPath = path.join(repoRoot, "docs/runtime/artifacts/tixl_mesh_draw_full_pbr_resource_binding/tixl_mesh_draw_full_pbr_resource_binding_result.json");
const explicitAdapterArtifactPath = path.join(repoRoot, "docs/runtime/artifacts/tixl_mesh_draw_explicit_adapter_proof/tixl_mesh_draw_explicit_adapter_result.json");
const nativeMetalBackendIntegrationArtifactPath = path.join(repoRoot, "docs/runtime/artifacts/tixl_mesh_draw_native_metal_backend_integration/tixl_mesh_draw_native_metal_backend_integration_result.json");
const textureCubePbrReferenceArtifactPath = path.join(repoRoot, "docs/runtime/artifacts/tixl_mesh_draw_texturecube_pbr_reference/tixl_mesh_draw_texturecube_pbr_reference_result.json");
const resultName = "tixl_mesh_draw_backend_replacement_gate_result.json";
const traceName = "tixl_mesh_draw_backend_replacement_gate_trace.json";
const errorsName = "tixl_mesh_draw_backend_replacement_gate_errors.json";

test("TiXL mesh draw backend replacement gate docs state guarded negative claims", () => {
  const source = fs.readFileSync(contractPath, "utf8");

  assert.match(source, /TiXL Mesh Draw Backend Replacement Gate Proof/);
  assert.match(source, /bounded replacement readiness proof/);
  assert.match(source, /backendReplacementGateEvaluated: true/);
  assert.match(source, /replacementBlockedBecauseFullBindingMissing: false/);
  assert.match(source, /nativeMetalBackendIntegrationComplete: true/);
  assert.match(source, /boundedNativeBackendRemains: false/);
  assert.match(source, /backendReplacementReady: true/);
  assert.match(source, /fullPbrResourceBinding: true/);
  assert.match(source, /explicitAdapterProofPresent: true/);
  assert.match(source, /hlslToMslTranslation: false/);
  assert.match(source, /tixlRuntimeParity: true/);
});

test("TiXL mesh draw backend replacement gate fixture consumes bounded artifacts", () => {
  const graph = readJson(fixturePath);

  assert.equal(graph.graphId, "fixture.tixl_mesh_draw_backend_replacement_gate");
  assert.equal(graph.kind, "TixlMeshDrawBackendReplacementGateProof");
  assert.equal(graph.nativeRenderPipelineArtifacts, "docs/runtime/artifacts/native_render_pipeline");
  assert.equal(graph.resourceBindingArtifact, "docs/runtime/artifacts/tixl_mesh_draw_resource_binding/tixl_mesh_draw_resource_binding_result.json");
  assert.equal(graph.fullPbrResourceBindingArtifact, "docs/runtime/artifacts/tixl_mesh_draw_full_pbr_resource_binding/tixl_mesh_draw_full_pbr_resource_binding_result.json");
  assert.equal(graph.explicitAdapterProofArtifact, "docs/runtime/artifacts/tixl_mesh_draw_explicit_adapter_proof/tixl_mesh_draw_explicit_adapter_result.json");
  assert.equal(graph.nativeMetalBackendIntegrationArtifact, "docs/runtime/artifacts/tixl_mesh_draw_native_metal_backend_integration/tixl_mesh_draw_native_metal_backend_integration_result.json");
  assert.equal(graph.textureSamplerBindingArtifact, "docs/runtime/artifacts/tixl_mesh_draw_texture_sampler_binding/tixl_mesh_draw_texture_sampler_binding_result.json");
  assert.equal(graph.shadergraphResourcesExpansionArtifact, "docs/runtime/artifacts/tixl_mesh_draw_shadergraph_resources_expansion/tixl_mesh_draw_shadergraph_resources_expansion_result.json");
  assert.equal(graph.stageMrtMatrixArtifact, "docs/runtime/artifacts/tixl_mesh_draw_stage_mrt_matrix/tixl_mesh_draw_stage_mrt_matrix_result.json");
  assert.equal(graph.textureCubePbrReferenceArtifact, "docs/runtime/artifacts/tixl_mesh_draw_texturecube_pbr_reference/tixl_mesh_draw_texturecube_pbr_reference_result.json");
  assert.deepEqual(graph.expected.claims, expectedClaims(true));
});

test("TiXL mesh draw backend replacement gate shell emits replacement-ready proof", () => {
  const tmpDir = fs.mkdtempSync(path.join(os.tmpdir(), "tixl-mesh-draw-backend-gate-"));
  const run = spawnSync("python3", [scriptPath, fixturePath, tmpDir], {
    cwd: repoRoot,
    encoding: "utf8",
  });

  assert.equal(run.status, 0, run.stderr || run.stdout);
  const result = readJson(path.join(tmpDir, resultName));
  const trace = readJson(path.join(tmpDir, traceName));
  const errors = readJson(path.join(tmpDir, errorsName));

  assert.deepEqual(errors, []);
  assert.equal(result.kind, "TixlMeshDrawBackendReplacementGateProof");
  assert.equal(result.ok, true);
  assert.equal(result.status, "replacement_ready");
  assert.deepEqual(result.claims, expectedClaims(true));
  assert.deepEqual(result.guard, {
    requiredBeforeReplacement: ["fullPbrResourceBinding", "explicitAdapterProof"],
    fullPbrResourceBindingPresent: true,
    explicitAdapterProofPresent: true,
    nativeMetalBackendIntegrationComplete: true,
    runtimeEquivalenceProof: true,
    decision: "replacement_ready",
    boundedBackendState: "native_metal_backend_replaces_bounded_backend_for_this_lane",
  });
  assert.deepEqual(trace.map((entry) => entry.op), [
    "loadTixlMeshDrawBackendReplacementGateFixture",
    "resolveBackendReplacementGateInputs",
    "readBackendReplacementGateInputs",
    "validateBackendReplacementGateInputs",
    "evaluateBackendReplacementGuard",
    "publishTixlMeshDrawBackendReplacementGateArtifacts",
  ]);
  assertPathClean(result, trace, errors);
});

test("TiXL mesh draw backend replacement gate blocks forged fixture ready claims", () => {
  const tmpDir = fs.mkdtempSync(path.join(os.tmpdir(), "tixl-mesh-draw-backend-gate-forged-fixture-"));
  const fixture = readJson(fixturePath);
  fixture.expected.claims.hlslToMslTranslation = true;
  const badFixturePath = path.join(tmpDir, "forged-fixture.graph.json");
  fs.writeFileSync(badFixturePath, JSON.stringify(fixture, null, 2));

  const run = spawnSync("python3", [scriptPath, badFixturePath, tmpDir], {
    cwd: repoRoot,
    encoding: "utf8",
  });

  assert.equal(run.status, 1);
  const result = readJson(path.join(tmpDir, resultName));
  const errors = readJson(path.join(tmpDir, errorsName));
  const fields = errors.flatMap((error) => error.mismatches || []).map((mismatch) => mismatch.field);
  assert.equal(result.status, "blocked_invalid_gate_inputs");
  assert.ok(fields.includes("expected.claims.hlslToMslTranslation"));
  assert.deepEqual(result.claims, expectedClaims(false));
  assertPathClean(result, errors);
});

test("TiXL mesh draw backend replacement gate blocks forged resource binding readiness", () => {
  const tmpDir = fs.mkdtempSync(path.join(os.tmpdir(), "tixl-mesh-draw-backend-gate-forged-resource-"));
  const resource = readJson(resourceBindingArtifactPath);
  resource.claims.backendReplacementReady = true;
  resource.claims.fullPbrResourceBinding = true;
  const resourcePath = path.join(tmpDir, "forged-resource-binding.json");
  fs.writeFileSync(resourcePath, JSON.stringify(resource, null, 2));
  const fixture = readJson(fixturePath);
  fixture.resourceBindingArtifact = resourcePath;
  const badFixturePath = path.join(tmpDir, "forged-resource.graph.json");
  fs.writeFileSync(badFixturePath, JSON.stringify(fixture, null, 2));

  const run = spawnSync("python3", [scriptPath, badFixturePath, tmpDir], {
    cwd: repoRoot,
    encoding: "utf8",
  });

  assert.equal(run.status, 1);
  const result = readJson(path.join(tmpDir, resultName));
  const errors = readJson(path.join(tmpDir, errorsName));
  const fields = errors.flatMap((error) => error.mismatches || []).map((mismatch) => mismatch.field);
  assert.equal(result.status, "blocked_invalid_gate_inputs");
  assert.ok(fields.includes("resourceBinding.claims.backendReplacementReady"));
  assert.ok(fields.includes("resourceBinding.claims.fullPbrResourceBinding"));
  assert.deepEqual(result.claims, expectedClaims(false));
  assertPathClean(result, errors);
});

test("TiXL mesh draw backend replacement gate blocks missing explicit adapter proof", () => {
  const tmpDir = fs.mkdtempSync(path.join(os.tmpdir(), "tixl-mesh-draw-backend-gate-no-adapter-"));
  const fixture = readJson(fixturePath);
  delete fixture.explicitAdapterProofArtifact;
  const badFixturePath = path.join(tmpDir, "full-binding-no-adapter.graph.json");
  fs.writeFileSync(badFixturePath, JSON.stringify(fixture, null, 2));

  const run = spawnSync("python3", [scriptPath, badFixturePath, tmpDir], {
    cwd: repoRoot,
    encoding: "utf8",
  });

  assert.equal(run.status, 1);
  const result = readJson(path.join(tmpDir, resultName));
  const errors = readJson(path.join(tmpDir, errorsName));
  const fields = errors.flatMap((error) => error.mismatches || []).map((mismatch) => mismatch.field);
  assert.equal(result.status, "blocked_invalid_gate_inputs");
  assert.ok(fields.includes("explicitAdapterProof"));
  assert.deepEqual(result.claims, expectedClaims(false));
  assertPathClean(result, errors);
});

test("TiXL mesh draw backend replacement gate blocks missing full PBR binding proof", () => {
  const tmpDir = fs.mkdtempSync(path.join(os.tmpdir(), "tixl-mesh-draw-backend-gate-no-full-binding-"));
  const fixture = readJson(fixturePath);
  delete fixture.fullPbrResourceBindingArtifact;
  const badFixturePath = path.join(tmpDir, "missing-full-binding.graph.json");
  fs.writeFileSync(badFixturePath, JSON.stringify(fixture, null, 2));

  const run = spawnSync("python3", [scriptPath, badFixturePath, tmpDir], {
    cwd: repoRoot,
    encoding: "utf8",
  });

  assert.equal(run.status, 1);
  const result = readJson(path.join(tmpDir, resultName));
  const errors = readJson(path.join(tmpDir, errorsName));
  const fields = errors.flatMap((error) => error.mismatches || []).map((mismatch) => mismatch.field);
  assert.equal(result.status, "blocked_invalid_gate_inputs");
  assert.ok(fields.includes("fullPbrResourceBinding"));
  assert.deepEqual(result.claims, expectedClaims(false));
  assertPathClean(result, errors);
});

test("TiXL mesh draw backend replacement gate blocks missing native Metal integration proof", () => {
  const tmpDir = fs.mkdtempSync(path.join(os.tmpdir(), "tixl-mesh-draw-backend-gate-no-native-integration-"));
  const fixture = readJson(fixturePath);
  delete fixture.nativeMetalBackendIntegrationArtifact;
  const badFixturePath = path.join(tmpDir, "missing-native-integration.graph.json");
  fs.writeFileSync(badFixturePath, JSON.stringify(fixture, null, 2));

  const run = spawnSync("python3", [scriptPath, badFixturePath, tmpDir], {
    cwd: repoRoot,
    encoding: "utf8",
  });

  assert.equal(run.status, 1);
  const result = readJson(path.join(tmpDir, resultName));
  const errors = readJson(path.join(tmpDir, errorsName));
  const fields = errors.flatMap((error) => error.mismatches || []).map((mismatch) => mismatch.field);
  assert.equal(result.status, "blocked_invalid_gate_inputs");
  assert.ok(fields.includes("nativeMetalBackendIntegration"));
  assert.deepEqual(result.claims, expectedClaims(false));
  assertPathClean(result, errors);
});

test("TiXL mesh draw backend replacement gate blocks widened full binding proof", () => {
  const tmpDir = fs.mkdtempSync(path.join(os.tmpdir(), "tixl-mesh-draw-backend-gate-forged-full-binding-"));
  const fullBinding = readJson(fullPbrBindingArtifactPath);
  fullBinding.claims.backendReplacementReady = true;
  fullBinding.claims.tixlRuntimeParity = true;
  const fullBindingPath = path.join(tmpDir, "forged-full-binding.json");
  fs.writeFileSync(fullBindingPath, JSON.stringify(fullBinding, null, 2));
  const fixture = readJson(fixturePath);
  fixture.fullPbrResourceBindingArtifact = fullBindingPath;
  const badFixturePath = path.join(tmpDir, "forged-full-binding.graph.json");
  fs.writeFileSync(badFixturePath, JSON.stringify(fixture, null, 2));

  const run = spawnSync("python3", [scriptPath, badFixturePath, tmpDir], {
    cwd: repoRoot,
    encoding: "utf8",
  });

  assert.equal(run.status, 1);
  const result = readJson(path.join(tmpDir, resultName));
  const errors = readJson(path.join(tmpDir, errorsName));
  const fields = errors.flatMap((error) => error.mismatches || []).map((mismatch) => mismatch.field);
  assert.equal(result.status, "blocked_invalid_gate_inputs");
  assert.ok(fields.includes("fullPbrResourceBinding.claims.backendReplacementReady"));
  assert.ok(fields.includes("fullPbrResourceBinding.claims.tixlRuntimeParity"));
  assert.deepEqual(result.claims, expectedClaims(false));
  assertPathClean(result, errors);
});

test("TiXL mesh draw backend replacement gate blocks widened explicit adapter proof", () => {
  const tmpDir = fs.mkdtempSync(path.join(os.tmpdir(), "tixl-mesh-draw-backend-gate-forged-adapter-"));
  const adapter = readJson(explicitAdapterArtifactPath);
  adapter.claims.backendReplacementReady = true;
  adapter.claims.nativeGpuParityComplete = true;
  const adapterPath = path.join(tmpDir, "forged-adapter.json");
  fs.writeFileSync(adapterPath, JSON.stringify(adapter, null, 2));
  const fixture = readJson(fixturePath);
  fixture.explicitAdapterProofArtifact = adapterPath;
  const badFixturePath = path.join(tmpDir, "forged-adapter.graph.json");
  fs.writeFileSync(badFixturePath, JSON.stringify(fixture, null, 2));

  const run = spawnSync("python3", [scriptPath, badFixturePath, tmpDir], {
    cwd: repoRoot,
    encoding: "utf8",
  });

  assert.equal(run.status, 1);
  const result = readJson(path.join(tmpDir, resultName));
  const errors = readJson(path.join(tmpDir, errorsName));
  const fields = errors.flatMap((error) => error.mismatches || []).map((mismatch) => mismatch.field);
  assert.equal(result.status, "blocked_invalid_gate_inputs");
  assert.ok(fields.includes("explicitAdapterProof.claims.backendReplacementReady"));
  assert.ok(fields.includes("explicitAdapterProof.claims.nativeGpuParityComplete"));
  assert.deepEqual(result.claims, expectedClaims(false));
  assertPathClean(result, errors);
});

test("TiXL mesh draw backend replacement gate blocks widened native Metal integration proof", () => {
  const tmpDir = fs.mkdtempSync(path.join(os.tmpdir(), "tixl-mesh-draw-backend-gate-forged-native-integration-"));
  const nativeIntegration = readJson(nativeMetalBackendIntegrationArtifactPath);
  nativeIntegration.claims.hlslToMslTranslation = true;
  const nativeIntegrationPath = path.join(tmpDir, "forged-native-integration.json");
  fs.writeFileSync(nativeIntegrationPath, JSON.stringify(nativeIntegration, null, 2));
  const fixture = readJson(fixturePath);
  fixture.nativeMetalBackendIntegrationArtifact = nativeIntegrationPath;
  const badFixturePath = path.join(tmpDir, "forged-native-integration.graph.json");
  fs.writeFileSync(badFixturePath, JSON.stringify(fixture, null, 2));

  const run = spawnSync("python3", [scriptPath, badFixturePath, tmpDir], {
    cwd: repoRoot,
    encoding: "utf8",
  });

  assert.equal(run.status, 1);
  const result = readJson(path.join(tmpDir, resultName));
  const errors = readJson(path.join(tmpDir, errorsName));
  const fields = errors.flatMap((error) => error.mismatches || []).map((mismatch) => mismatch.field);
  assert.equal(result.status, "blocked_invalid_gate_inputs");
  assert.ok(fields.includes("nativeMetalBackendIntegration.claims.hlslToMslTranslation"));
  assert.deepEqual(result.claims, expectedClaims(false));
  assertPathClean(result, errors);
});

test("TiXL mesh draw backend replacement gate blocks widened downstream artifacts", () => {
  const tmpDir = fs.mkdtempSync(path.join(os.tmpdir(), "tixl-mesh-draw-backend-gate-forged-cube-"));
  const cube = readJson(textureCubePbrReferenceArtifactPath);
  cube.claims.backendReplacementReady = true;
  cube.claims.fullPbrResourceBinding = true;
  const cubePath = path.join(tmpDir, "forged-texturecube.json");
  fs.writeFileSync(cubePath, JSON.stringify(cube, null, 2));
  const fixture = readJson(fixturePath);
  fixture.textureCubePbrReferenceArtifact = cubePath;
  const badFixturePath = path.join(tmpDir, "forged-cube.graph.json");
  fs.writeFileSync(badFixturePath, JSON.stringify(fixture, null, 2));

  const run = spawnSync("python3", [scriptPath, badFixturePath, tmpDir], {
    cwd: repoRoot,
    encoding: "utf8",
  });

  assert.equal(run.status, 1);
  const result = readJson(path.join(tmpDir, resultName));
  const errors = readJson(path.join(tmpDir, errorsName));
  const fields = errors.flatMap((error) => error.mismatches || []).map((mismatch) => mismatch.field);
  assert.equal(result.status, "blocked_invalid_gate_inputs");
  assert.ok(fields.includes("textureCubePbrReference.claims.backendReplacementReady"));
  assert.ok(fields.includes("textureCubePbrReference.claims.fullPbrResourceBinding"));
  assert.deepEqual(result.claims, expectedClaims(false));
  assertPathClean(result, errors);
});

test("TiXL mesh draw backend replacement gate checked-in artifacts are path-clean and fresh", () => {
  const result = readJson(path.join(artifactDir, resultName));
  const trace = readJson(path.join(artifactDir, traceName));
  const errors = readJson(path.join(artifactDir, errorsName));

  assert.equal(result.kind, "TixlMeshDrawBackendReplacementGateProof");
  assert.equal(result.ok, true);
  assert.equal(result.status, "replacement_ready");
  assert.deepEqual(result.claims, expectedClaims(true));
  assert.deepEqual(errors, []);
  assertPathClean(result, trace, errors);

  const tmpDir = fs.mkdtempSync(path.join(os.tmpdir(), "tixl-mesh-draw-backend-gate-fresh-"));
  const run = spawnSync("python3", [scriptPath, fixturePath, tmpDir], {
    cwd: repoRoot,
    encoding: "utf8",
  });
  assert.equal(run.status, 0, run.stderr || run.stdout);
  assert.deepEqual(readJson(path.join(tmpDir, resultName)), result);
  assert.deepEqual(readJson(path.join(tmpDir, traceName)), trace);
  assert.deepEqual(readJson(path.join(tmpDir, errorsName)), errors);
});

function expectedClaims(evaluated) {
  return {
    backendReplacementGateEvaluated: evaluated,
    nativeRenderPipelineArtifactConsumed: evaluated,
    resourceBindingArtifactConsumed: evaluated,
    fullPbrResourceBindingArtifactConsumed: evaluated,
    explicitAdapterProofArtifactConsumed: evaluated,
    nativeMetalBackendIntegrationArtifactConsumed: evaluated,
    textureSamplerBindingArtifactConsumed: evaluated,
    shadergraphResourcesExpansionArtifactConsumed: evaluated,
    stageMrtMatrixArtifactConsumed: evaluated,
    textureCubePbrReferenceArtifactConsumed: evaluated,
    replacementBlockedBecauseFullBindingMissing: false,
    replacementBlockedBecauseAdapterProofMissing: false,
    boundedNativeBackendRemains: false,
    nativeMetalBackendIntegrationComplete: evaluated,
    runtimeEquivalenceProof: evaluated,
    backendReplacementReady: evaluated,
    fullPbrResourceBinding: evaluated,
    explicitAdapterProofPresent: evaluated,
    hlslToMslTranslation: false,
    tixlRuntimeParity: evaluated,
    nativeGpuParityComplete: evaluated,
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

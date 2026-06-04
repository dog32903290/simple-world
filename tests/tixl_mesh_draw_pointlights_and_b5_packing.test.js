const assert = require("node:assert/strict");
const { spawnSync } = require("node:child_process");
const fs = require("node:fs");
const os = require("node:os");
const path = require("node:path");
const test = require("node:test");

const repoRoot = path.resolve(__dirname, "..");
const contractPath = path.join(repoRoot, "docs/runtime/TIXL_MESH_DRAW_POINTLIGHTS_AND_B5_PACKING_VERDICT.md");
const fixturePath = path.join(repoRoot, "docs/runtime/fixtures/tixl_mesh_draw_pointlights_and_b5_packing.graph.json");
const scriptPath = path.join(repoRoot, "docs/runtime/scripts/tixl_mesh_draw_pointlights_and_b5_packing_shell.py");
const artifactDir = path.join(repoRoot, "docs/runtime/artifacts/tixl_mesh_draw_pointlights_and_b5_packing");
const layoutArtifactPath = path.join(
  repoRoot,
  "docs/runtime/artifacts/tixl_mesh_draw_constant_buffer_layout/tixl_mesh_draw_constant_buffer_layout_result.json",
);
const sourceAuditArtifactPath = path.join(
  repoRoot,
  "docs/runtime/artifacts/tixl_mesh_draw_shader_source_audit/tixl_mesh_draw_shader_source_audit_result.json",
);
const priorNativeArtifactPath = path.join(
  repoRoot,
  "docs/runtime/artifacts/tixl_mesh_draw_constant_buffer_native_packing/tixl_mesh_draw_constant_buffer_native_packing_result.json",
);
const pointLightShaderPath = path.join(
  repoRoot,
  "external/tixl/Operators/Lib/Assets/shaders/shared/point-light.hlsl",
);
const resultName = "tixl_mesh_draw_pointlights_and_b5_packing_result.json";
const traceName = "tixl_mesh_draw_pointlights_and_b5_packing_trace.json";
const errorsName = "tixl_mesh_draw_pointlights_and_b5_packing_errors.json";

test("TiXL mesh draw pointlights and b5 packing docs define a conservative closure verdict", () => {
  const source = fs.readFileSync(contractPath, "utf8");

  assert.match(source, /TiXL Mesh Draw PointLights And B5 Packing Verdict/);
  assert.match(source, /TixlMeshDrawPointLightsAndB5PackingVerdict answers:/);
  assert.match(source, /prior native packing proof for b0\/b1\/b2\/b4/);
  assert.match(source, /b3 PointLights -> Metal buffer\(5\), size 400/);
  assert.match(source, /PointLight array stride 48/);
  assert.match(source, /ActiveLightCount 384/);
  assert.match(source, /blocked_needs_pointlight_native_probe/);
  assert.match(source, /b5_packing_blocked_until_shadergraph_param_expansion/);
  assert.match(source, /not texture\/sampler mapping/);
  assert.match(source, /not full PBR resource binding/);
  assert.match(source, /not backend replacement/);
  assert.match(source, /b5DuplicateParamsPackingProven: false/);
  assert.match(source, /constantBufferAdapterComplete: false/);
  assert.match(source, /textureSamplerMapping: false/);
  assert.match(source, /fullPbrResourceBinding: false/);
  assert.match(source, /backendReplacementReady: false/);
  assert.match(source, /hlslToMslTranslation: false/);
});

test("TiXL mesh draw pointlights and b5 fixture pins b3 probe and b5 blocked expectations", () => {
  const graph = readJson(fixturePath);

  assert.equal(graph.graphId, "fixture.tixl_mesh_draw_pointlights_and_b5_packing");
  assert.equal(graph.kind, "TixlMeshDrawPointLightsAndB5PackingVerdict");
  assert.equal(
    graph.sourceAuditArtifact,
    "docs/runtime/artifacts/tixl_mesh_draw_shader_source_audit/tixl_mesh_draw_shader_source_audit_result.json",
  );
  assert.equal(
    graph.layoutArtifact,
    "docs/runtime/artifacts/tixl_mesh_draw_constant_buffer_layout/tixl_mesh_draw_constant_buffer_layout_result.json",
  );
  assert.equal(
    graph.priorNativePackingArtifact,
    "docs/runtime/artifacts/tixl_mesh_draw_constant_buffer_native_packing/tixl_mesh_draw_constant_buffer_native_packing_result.json",
  );
  assert.deepEqual(graph.probeRegisters, ["b3"]);
  assert.deepEqual(graph.blockedRegisters, ["b5"]);
  assert.deepEqual(graph.expected.claims, expectedClaims(false));
  assert.deepEqual(graph.expected.b3PointLightsPacking, expectedB3Packing());
});

test("TiXL mesh draw pointlights and b5 shell proves b3 with Metal or blocks without widening claims", () => {
  const tmpDir = fs.mkdtempSync(path.join(os.tmpdir(), "tixl-mesh-draw-pointlights-b5-"));
  const run = spawnSync("python3", [scriptPath, fixturePath, tmpDir], {
    cwd: repoRoot,
    encoding: "utf8",
  });

  assert.ok(run.status === 0 || run.status === 1, run.stderr || run.stdout);
  const result = readJson(path.join(tmpDir, resultName));
  const trace = readJson(path.join(tmpDir, traceName));
  const errors = readJson(path.join(tmpDir, errorsName));

  assert.equal(result.kind, "TixlMeshDrawPointLightsAndB5PackingVerdict");
  assert.equal(trace[0].fixture, "docs/runtime/fixtures/tixl_mesh_draw_pointlights_and_b5_packing.graph.json");
  assert.equal(
    result.inputArtifacts.sourceAudit.path,
    "docs/runtime/artifacts/tixl_mesh_draw_shader_source_audit/tixl_mesh_draw_shader_source_audit_result.json",
  );
  assert.equal(
    result.inputArtifacts.constantBufferLayout.path,
    "docs/runtime/artifacts/tixl_mesh_draw_constant_buffer_layout/tixl_mesh_draw_constant_buffer_layout_result.json",
  );
  assert.equal(
    result.inputArtifacts.priorNativePacking.path,
    "docs/runtime/artifacts/tixl_mesh_draw_constant_buffer_native_packing/tixl_mesh_draw_constant_buffer_native_packing_result.json",
  );
  assert.equal(result.claims.priorNativePackingArtifactConsumed, true);
  assert.equal(result.claims.constantBufferLayoutArtifactConsumed, true);
  assert.equal(result.claims.b5DuplicateParamsPackingProven, false);
  assert.equal(result.claims.b5RequiresShadergraphParamExpansion, true);
  assert.equal(result.claims.constantBufferAdapterComplete, false);
  assert.equal(result.claims.textureSamplerMapping, false);
  assert.equal(result.claims.fullPbrResourceBinding, false);
  assert.equal(result.claims.backendReplacementReady, false);
  assert.equal(result.claims.hlslToMslTranslation, false);
  assert.equal(result.claims.tixlRuntimeParity, false);
  assert.equal(result.claims.pbrVisualCorrectness, false);
  assert.deepEqual(result.b5Verdict, expectedB5Verdict());
  assertPathClean(result, trace, errors);

  if (run.status === 0) {
    assert.equal(result.ok, true);
    assert.equal(result.status, "proven_b3_pointlights_packing_b5_blocked");
    assert.equal(result.claims.actualMetalPointLightProbeRan, true);
    assert.equal(result.claims.b3PointLightsPackingProven, true);
    assert.deepEqual(result.claims, expectedClaims(true));
    assert.deepEqual(compactProvenNativePacking(result.provenNativePacking), [expectedB3Packing()]);
    assert.deepEqual(errors, []);
    return;
  }

  assert.equal(result.ok, false);
  assert.equal(result.status, "blocked_needs_pointlight_native_probe");
  assert.equal(result.claims.actualMetalPointLightProbeRan, false);
  assert.equal(result.claims.b3PointLightsPackingProven, false);
  assert.deepEqual(result.claims, expectedClaims(false));
  assert.ok(errors.length > 0);
});

test("TiXL mesh draw pointlights and b5 shell blocks stale prior native packing artifact", () => {
  const tmpDir = fs.mkdtempSync(path.join(os.tmpdir(), "tixl-mesh-draw-pointlights-b5-stale-prior-"));
  const prior = readJson(priorNativeArtifactPath);
  prior.claims.actualMetalPackingProbeRan = false;
  prior.provenNativePacking = prior.provenNativePacking.filter((buffer) => buffer.register !== "b4");
  const priorPath = path.join(tmpDir, "stale-prior.json");
  fs.writeFileSync(priorPath, JSON.stringify(prior, null, 2));
  const fixture = readJson(fixturePath);
  fixture.priorNativePackingArtifact = priorPath;
  const brokenFixturePath = path.join(tmpDir, "stale-prior.graph.json");
  fs.writeFileSync(brokenFixturePath, JSON.stringify(fixture, null, 2));

  const run = spawnSync("python3", [scriptPath, brokenFixturePath, tmpDir], {
    cwd: repoRoot,
    encoding: "utf8",
  });

  assert.equal(run.status, 1);
  const result = readJson(path.join(tmpDir, resultName));
  const errors = readJson(path.join(tmpDir, errorsName));
  assert.equal(result.ok, false);
  assert.equal(result.status, "blocked_invalid_input_artifact");
  assert.equal(result.claims.priorNativePackingArtifactConsumed, false);
  assert.equal(result.claims.b3PointLightsPackingProven, false);
  assert.equal(errors[0].code, "tixl_mesh_draw_pointlights_and_b5_packing.invalid_prior_native_packing_artifact");
  assertPathClean(result, errors);
});

test("TiXL mesh draw pointlights and b5 shell blocks invented b5 layout fields", () => {
  const tmpDir = fs.mkdtempSync(path.join(os.tmpdir(), "tixl-mesh-draw-pointlights-b5-invented-b5-"));
  const layout = readJson(layoutArtifactPath);
  const b5 = layout.constantBuffers.find((buffer) => buffer.register === "b5");
  b5.fields = [{ name: "InventedFloat", type: "float" }];
  layout.duplicateNamePolicy.b5.fieldsKnownFromSourceAudit = true;
  const layoutPath = path.join(tmpDir, "invented-b5-layout.json");
  fs.writeFileSync(layoutPath, JSON.stringify(layout, null, 2));
  const fixture = readJson(fixturePath);
  fixture.layoutArtifact = layoutPath;
  const brokenFixturePath = path.join(tmpDir, "invented-b5.graph.json");
  fs.writeFileSync(brokenFixturePath, JSON.stringify(fixture, null, 2));

  const run = spawnSync("python3", [scriptPath, brokenFixturePath, tmpDir], {
    cwd: repoRoot,
    encoding: "utf8",
  });

  assert.equal(run.status, 1);
  const result = readJson(path.join(tmpDir, resultName));
  const errors = readJson(path.join(tmpDir, errorsName));
  assert.equal(result.ok, false);
  assert.equal(result.status, "blocked_invalid_input_artifact");
  assert.equal(result.claims.constantBufferLayoutArtifactConsumed, false);
  assert.equal(result.claims.b5DuplicateParamsPackingProven, false);
  assert.equal(result.claims.b5RequiresShadergraphParamExpansion, true);
  assert.equal(errors[0].code, "tixl_mesh_draw_pointlights_and_b5_packing.invalid_layout_artifact");
  const mismatchFields = errors.flatMap((error) => error.mismatches || []).map((mismatch) => mismatch.field);
  assert.ok(mismatchFields.includes("constantBuffers.b5.fields"));
  assert.ok(mismatchFields.includes("duplicateNamePolicy.b5.fieldsKnownFromSourceAudit"));
  assertPathClean(result, errors);
});

test("TiXL mesh draw pointlights and b5 shell blocks invented b5 source audit fields", () => {
  const tmpDir = fs.mkdtempSync(path.join(os.tmpdir(), "tixl-mesh-draw-pointlights-b5-invented-source-"));
  const sourceAudit = readJson(sourceAuditArtifactPath);
  const b5 = sourceAudit.requiredBuffers.find((buffer) => buffer.register === "b5" && buffer.name === "Params");
  b5.fields = [{ type: "float", name: "InventedFloat", array: null }];
  sourceAudit.constants.push({
    buffer: "Params",
    register: "b5",
    name: "InventedFloat",
    type: "float",
    array: null,
  });
  const sourceAuditPath = path.join(tmpDir, "invented-b5-source-audit.json");
  fs.writeFileSync(sourceAuditPath, JSON.stringify(sourceAudit, null, 2));
  const fixture = readJson(fixturePath);
  fixture.sourceAuditArtifact = sourceAuditPath;
  const brokenFixturePath = path.join(tmpDir, "invented-b5-source.graph.json");
  fs.writeFileSync(brokenFixturePath, JSON.stringify(fixture, null, 2));

  const run = spawnSync("python3", [scriptPath, brokenFixturePath, tmpDir], {
    cwd: repoRoot,
    encoding: "utf8",
  });

  assert.equal(run.status, 1);
  const result = readJson(path.join(tmpDir, resultName));
  const errors = readJson(path.join(tmpDir, errorsName));
  assert.equal(result.ok, false);
  assert.equal(result.status, "blocked_invalid_input_artifact");
  assert.equal(result.claims.b5DuplicateParamsPackingProven, false);
  assert.equal(errors[0].code, "tixl_mesh_draw_pointlights_and_b5_packing.invalid_source_audit_artifact");
  const mismatchFields = errors.flatMap((error) => error.mismatches || []).map((mismatch) => mismatch.field);
  assert.ok(mismatchFields.includes("requiredBuffers.b5.fields"));
  assert.ok(mismatchFields.includes("constants.b3_b5"));
  assertPathClean(result, errors);
});

test("TiXL mesh draw pointlights and b5 shell blocks PointLight shader source drift", () => {
  const tmpDir = fs.mkdtempSync(path.join(os.tmpdir(), "tixl-mesh-draw-pointlights-b5-source-drift-"));
  const shader = fs.readFileSync(pointLightShaderPath, "utf8");
  const drifted = shader.replace(
    /float3 position;\s*float intensity;/,
    "float intensity;\n    float3 position;",
  );
  const shaderPath = path.join(tmpDir, "point-light-drifted.hlsl");
  fs.writeFileSync(shaderPath, drifted);
  const fixture = readJson(fixturePath);
  fixture.pointLightShaderSource = shaderPath;
  const brokenFixturePath = path.join(tmpDir, "pointlight-source-drift.graph.json");
  fs.writeFileSync(brokenFixturePath, JSON.stringify(fixture, null, 2));

  const run = spawnSync("python3", [scriptPath, brokenFixturePath, tmpDir], {
    cwd: repoRoot,
    encoding: "utf8",
  });

  assert.equal(run.status, 1);
  const result = readJson(path.join(tmpDir, resultName));
  const errors = readJson(path.join(tmpDir, errorsName));
  assert.equal(result.ok, false);
  assert.equal(result.status, "blocked_invalid_pointlight_source_evidence");
  assert.equal(result.claims.b3PointLightsPackingProven, false);
  assert.equal(errors[0].code, "tixl_mesh_draw_pointlights_and_b5_packing.invalid_pointlight_source_evidence");
  const mismatchFields = errors.flatMap((error) => error.mismatches || []).map((mismatch) => mismatch.field);
  assert.ok(mismatchFields.includes("pointLightShaderSource.structFields"));
  assertPathClean(result, errors);
});

test("TiXL mesh draw pointlights and b5 shell blocks widened fixture claims", () => {
  const tmpDir = fs.mkdtempSync(path.join(os.tmpdir(), "tixl-mesh-draw-pointlights-b5-widened-fixture-"));
  const fixture = readJson(fixturePath);
  fixture.expected.claims.constantBufferAdapterComplete = true;
  fixture.expected.claims.fullPbrResourceBinding = true;
  fixture.expected.claims.backendReplacementReady = true;
  const brokenFixturePath = path.join(tmpDir, "widened-fixture.graph.json");
  fs.writeFileSync(brokenFixturePath, JSON.stringify(fixture, null, 2));

  const run = spawnSync("python3", [scriptPath, brokenFixturePath, tmpDir], {
    cwd: repoRoot,
    encoding: "utf8",
  });

  assert.equal(run.status, 1);
  const result = readJson(path.join(tmpDir, resultName));
  const errors = readJson(path.join(tmpDir, errorsName));
  assert.equal(result.ok, false);
  assert.equal(result.status, "blocked_invalid_fixture");
  assert.equal(result.claims.constantBufferAdapterComplete, false);
  assert.equal(result.claims.fullPbrResourceBinding, false);
  assert.equal(result.claims.backendReplacementReady, false);
  assert.equal(errors[0].code, "tixl_mesh_draw_pointlights_and_b5_packing.invalid_fixture_expectations");
  assertPathClean(result, errors);
});

test("TiXL mesh draw pointlights and b5 checked-in artifacts match the current shell output", () => {
  const tmpDir = fs.mkdtempSync(path.join(os.tmpdir(), "tixl-mesh-draw-pointlights-b5-stale-check-"));
  const run = spawnSync("python3", [scriptPath, fixturePath, tmpDir], {
    cwd: repoRoot,
    encoding: "utf8",
  });
  assert.ok(run.status === 0 || run.status === 1, run.stderr || run.stdout);

  const checkedInResult = readJson(path.join(artifactDir, resultName));
  const checkedInTrace = readJson(path.join(artifactDir, traceName));
  const checkedInErrors = readJson(path.join(artifactDir, errorsName));
  const freshResult = readJson(path.join(tmpDir, resultName));
  const freshTrace = readJson(path.join(tmpDir, traceName));
  const freshErrors = readJson(path.join(tmpDir, errorsName));

  assert.deepEqual(checkedInResult, freshResult);
  assert.deepEqual(checkedInTrace, freshTrace);
  assert.deepEqual(checkedInErrors, freshErrors);
  assertPathClean(checkedInResult, checkedInTrace, checkedInErrors);
});

function expectedClaims(b3PointLightsPackingProven) {
  return {
    priorNativePackingArtifactConsumed: true,
    constantBufferLayoutArtifactConsumed: true,
    actualMetalPointLightProbeRan: b3PointLightsPackingProven,
    b3PointLightsPackingProven,
    b5DuplicateParamsPackingProven: false,
    b5RequiresShadergraphParamExpansion: true,
    constantBufferAdapterComplete: false,
    textureSamplerMapping: false,
    fullPbrResourceBinding: false,
    backendReplacementReady: false,
    hlslToMslTranslation: false,
    tixlRuntimeParity: false,
    pbrVisualCorrectness: false,
  };
}

function expectedB3Packing() {
  return {
    register: "b3",
    name: "PointLights",
    semanticRole: "mesh_draw_point_lights",
    metalBuffer: 5,
    sizeBytes: 400,
    arrayStrideBytes: 48,
    arrayLength: 8,
    activeLightCountOffset: 384,
    offsets: {
      "Lights[0].position": 0,
      "Lights[0].intensity": 12,
      "Lights[0].color": 16,
      "Lights[0].range": 32,
      "Lights[0].decay": 36,
      "Lights[0].__padding": 40,
      "Lights[1].position": 48,
      "Lights[7].position": 336,
      ActiveLightCount: 384,
    },
  };
}

function expectedB5Verdict() {
  return {
    register: "b5",
    name: "Params",
    semanticRole: "shadergraph_duplicate_params",
    status: "b5_packing_blocked_until_shadergraph_param_expansion",
    fieldsFromLayout: [],
    fieldsKnownFromSourceAudit: false,
    provenNativePacking: false,
    reason: "The current layout/source-audit artifact records duplicate shadergraph Params at b5 but no concrete fields.",
  };
}

function compactProvenNativePacking(buffers) {
  return buffers.map((buffer) => ({
    register: buffer.register,
    name: buffer.name,
    semanticRole: buffer.semanticRole,
    metalBuffer: buffer.metalBuffer,
    sizeBytes: buffer.sizeBytes,
    arrayStrideBytes: buffer.arrayStrideBytes,
    arrayLength: buffer.arrayLength,
    activeLightCountOffset: buffer.activeLightCountOffset,
    offsets: buffer.offsets,
  }));
}

function assertPathClean(...values) {
  const text = JSON.stringify(values);
  assert.ok(!text.includes("/Users/"));
  assert.ok(!text.includes(repoRoot));
}

function readJson(filePath) {
  return JSON.parse(fs.readFileSync(filePath, "utf8"));
}

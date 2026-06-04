const assert = require("node:assert/strict");
const { spawnSync } = require("node:child_process");
const fs = require("node:fs");
const os = require("node:os");
const path = require("node:path");
const test = require("node:test");

const repoRoot = path.resolve(__dirname, "..");
const contractPath = path.join(repoRoot, "docs/runtime/TIXL_MESH_DRAW_B5_SHADERGRAPH_PARAMS_EXPANSION_VERDICT.md");
const fixturePath = path.join(repoRoot, "docs/runtime/fixtures/tixl_mesh_draw_b5_shadergraph_params_expansion.graph.json");
const scriptPath = path.join(repoRoot, "docs/runtime/scripts/tixl_mesh_draw_b5_shadergraph_params_expansion_shell.py");
const artifactDir = path.join(repoRoot, "docs/runtime/artifacts/tixl_mesh_draw_b5_shadergraph_params_expansion");
const sourceAuditPath = path.join(repoRoot, "docs/runtime/artifacts/tixl_mesh_draw_shader_source_audit/tixl_mesh_draw_shader_source_audit_result.json");
const layoutPath = path.join(repoRoot, "docs/runtime/artifacts/tixl_mesh_draw_constant_buffer_layout/tixl_mesh_draw_constant_buffer_layout_result.json");
const pointlightsPath = path.join(repoRoot, "docs/runtime/artifacts/tixl_mesh_draw_pointlights_and_b5_packing/tixl_mesh_draw_pointlights_and_b5_packing_result.json");
const resultName = "tixl_mesh_draw_b5_shadergraph_params_expansion_result.json";
const traceName = "tixl_mesh_draw_b5_shadergraph_params_expansion_trace.json";
const errorsName = "tixl_mesh_draw_b5_shadergraph_params_expansion_errors.json";

test("TiXL mesh draw b5 shadergraph params docs define a source-backed expansion verdict", () => {
  const source = fs.readFileSync(contractPath, "utf8");

  assert.match(source, /TiXL Mesh Draw B5 ShaderGraph Params Expansion Verdict/);
  assert.match(source, /cbuffer Params : register\(b5\)/);
  assert.match(source, /\/\*\{FLOAT_PARAMS\}\*\//);
  assert.match(source, /ShaderGraphNode\.CollectAllNodeParams/);
  assert.match(source, /FloatParams/);
  assert.match(source, /SphereSDF/);
  assert.match(source, /prove_native_b5_packing_from_source_backed_shadergraph_params/);
});

test("TiXL mesh draw b5 shadergraph params fixture pins bounded claims", () => {
  const graph = readJson(fixturePath);

  assert.equal(graph.graphId, "fixture.tixl_mesh_draw_b5_shadergraph_params_expansion");
  assert.equal(graph.kind, "TixlMeshDrawB5ShadergraphParamsExpansionVerdict");
  assert.equal(graph.expected.status, "expanded_b5_shadergraph_params_source_backed");
  assert.equal(graph.shadergraphParamExpansion.rootNode.tixlSymbolChildId, "04426d9c-b039-4a92-9b1f-61186b4df2e5");
  assert.deepEqual(graph.expected.claims, expectedClaims(true));
});

test("TiXL mesh draw b5 shadergraph params shell emits source-backed SphereSDF b5 fields", () => {
  const tmpDir = fs.mkdtempSync(path.join(os.tmpdir(), "tixl-b5-shadergraph-expanded-"));
  const run = spawnSync("python3", [scriptPath, fixturePath, tmpDir], {
    cwd: repoRoot,
    encoding: "utf8",
  });

  assert.equal(run.status, 0, run.stderr || run.stdout);
  const result = readJson(path.join(tmpDir, resultName));
  const trace = readJson(path.join(tmpDir, traceName));
  const errors = readJson(path.join(tmpDir, errorsName));

  assert.deepEqual(errors, []);
  assert.equal(result.kind, "TixlMeshDrawB5ShadergraphParamsExpansionVerdict");
  assert.equal(result.ok, true);
  assert.equal(result.status, "expanded_b5_shadergraph_params_source_backed");
  assert.deepEqual(result.claims, expectedClaims(true));
  assert.equal(result.expansion.register, "b5");
  assert.equal(result.expansion.templateHole, "FLOAT_PARAMS");
  assert.equal(result.expansion.rootNode.prefix, "SphereSDF_nG1CBDm_");
  assert.deepEqual(result.expansion.fields.map((field) => [field.type, field.name, field.offsetBytes]), [
    ["float3", "SphereSDF_nG1CBDm_Center", 0],
    ["float", "SphereSDF_nG1CBDm_Radius", 12],
  ]);
  assert.deepEqual(result.expansion.floatBuffer.values, [-1.4845504, 0, 0.54366434, 0.5]);
  assert.equal(result.expansion.floatBuffer.sizeBytes, 16);
  assert.match(result.expansion.generatedHlsl, /float3  SphereSDF_nG1CBDm_Center;/);
  assert.match(result.expansion.generatedHlsl, /float  SphereSDF_nG1CBDm_Radius;/);
  assert.equal(result.expansion.proofBoundary.nativeB5PackingProven, false);
  assert.equal(result.nextWork.requiredNext, "prove_native_b5_packing_from_source_backed_shadergraph_params");
  assert.equal(result.sourceFacts.generator.collectsParamsWith, "ShaderGraphNode.CollectAllNodeParams");
  assert.equal(trace[0].fixture, "docs/runtime/fixtures/tixl_mesh_draw_b5_shadergraph_params_expansion.graph.json");
  assertPathClean(result, trace, errors);
});

test("TiXL mesh draw b5 shadergraph params shell blocks missing FragmentField expansion graph", () => {
  const tmpDir = fs.mkdtempSync(path.join(os.tmpdir(), "tixl-b5-shadergraph-missing-expansion-"));
  const fixture = readJson(fixturePath);
  delete fixture.shadergraphParamExpansion;
  const brokenFixture = path.join(tmpDir, "fixture.graph.json");
  fs.writeFileSync(brokenFixture, JSON.stringify(fixture, null, 2));

  const run = spawnSync("python3", [scriptPath, brokenFixture, tmpDir], {
    cwd: repoRoot,
    encoding: "utf8",
  });

  assert.equal(run.status, 1);
  const result = readJson(path.join(tmpDir, resultName));
  const errors = readJson(path.join(tmpDir, errorsName));
  assert.equal(result.ok, false);
  assert.equal(result.status, "blocked_b5_shadergraph_params_not_expanded");
  assert.equal(result.claims.b5ShadergraphParamsExpanded, false);
  assert.equal(errors[0].code, "tixl_mesh_draw_b5_shadergraph_params_expansion.invalid_shadergraph_param_expansion");
  assertPathClean(result, errors);
});

test("TiXL mesh draw b5 shadergraph params shell blocks forged SphereSDF Center values", () => {
  const tmpDir = fs.mkdtempSync(path.join(os.tmpdir(), "tixl-b5-shadergraph-forged-center-"));
  const fixture = readJson(fixturePath);
  fixture.shadergraphParamExpansion.rootNode.params.Center.x = 123.456;
  const brokenFixture = path.join(tmpDir, "fixture.graph.json");
  fs.writeFileSync(brokenFixture, JSON.stringify(fixture, null, 2));

  const run = spawnSync("python3", [scriptPath, brokenFixture, tmpDir], {
    cwd: repoRoot,
    encoding: "utf8",
  });

  assert.equal(run.status, 1);
  const result = readJson(path.join(tmpDir, resultName));
  const errors = readJson(path.join(tmpDir, errorsName));
  assert.equal(result.ok, false);
  assert.equal(result.status, "blocked_b5_shadergraph_params_not_expanded");
  assert.equal(result.claims.b5ShadergraphParamsExpanded, false);
  assert.equal(errors[0].code, "tixl_mesh_draw_b5_shadergraph_params_expansion.invalid_shadergraph_param_expansion");
  const fields = errors.flatMap((error) => error.mismatches || []).map((mismatch) => mismatch.field);
  assert.ok(fields.includes("shadergraphParamExpansion.rootNode.params.Center"));
  assertPathClean(result, errors);
});

test("TiXL mesh draw b5 shadergraph params shell blocks forged SphereSDF Radius values", () => {
  const tmpDir = fs.mkdtempSync(path.join(os.tmpdir(), "tixl-b5-shadergraph-forged-radius-"));
  const fixture = readJson(fixturePath);
  fixture.shadergraphParamExpansion.rootNode.params.Radius = 9.25;
  const brokenFixture = path.join(tmpDir, "fixture.graph.json");
  fs.writeFileSync(brokenFixture, JSON.stringify(fixture, null, 2));

  const run = spawnSync("python3", [scriptPath, brokenFixture, tmpDir], {
    cwd: repoRoot,
    encoding: "utf8",
  });

  assert.equal(run.status, 1);
  const result = readJson(path.join(tmpDir, resultName));
  const errors = readJson(path.join(tmpDir, errorsName));
  assert.equal(result.ok, false);
  assert.equal(result.status, "blocked_b5_shadergraph_params_not_expanded");
  assert.equal(result.claims.b5ShadergraphParamsExpanded, false);
  assert.equal(errors[0].code, "tixl_mesh_draw_b5_shadergraph_params_expansion.invalid_shadergraph_param_expansion");
  const fields = errors.flatMap((error) => error.mismatches || []).map((mismatch) => mismatch.field);
  assert.ok(fields.includes("shadergraphParamExpansion.rootNode.params.Radius"));
  assertPathClean(result, errors);
});

test("TiXL mesh draw b5 shadergraph params shell blocks invented source-audit b5 fields", () => {
  const tmpDir = fs.mkdtempSync(path.join(os.tmpdir(), "tixl-b5-shadergraph-invented-source-"));
  const sourceAudit = readJson(sourceAuditPath);
  const b5 = sourceAudit.requiredBuffers.find((buffer) => buffer.register === "b5" && buffer.name === "Params");
  b5.fields = [{ name: "Invented", type: "float", array: null }];
  const sourceAuditArtifact = path.join(tmpDir, "invented-source-audit.json");
  fs.writeFileSync(sourceAuditArtifact, JSON.stringify(sourceAudit, null, 2));
  const fixture = readJson(fixturePath);
  fixture.sourceAuditArtifact = sourceAuditArtifact;
  const brokenFixture = path.join(tmpDir, "fixture.graph.json");
  fs.writeFileSync(brokenFixture, JSON.stringify(fixture, null, 2));

  const run = spawnSync("python3", [scriptPath, brokenFixture, tmpDir], {
    cwd: repoRoot,
    encoding: "utf8",
  });

  assert.equal(run.status, 1);
  const result = readJson(path.join(tmpDir, resultName));
  const errors = readJson(path.join(tmpDir, errorsName));
  assert.equal(result.status, "blocked_invalid_b5_expansion_provenance");
  assert.equal(result.claims.b5ShadergraphParamsExpanded, false);
  assert.equal(errors[0].code, "tixl_mesh_draw_b5_shadergraph_params_expansion.invalid_source_audit_artifact");
  const fields = errors.flatMap((error) => error.mismatches || []).map((mismatch) => mismatch.field);
  assert.ok(fields.includes("requiredBuffers.b5.fields"));
  assertPathClean(result, errors);
});

test("TiXL mesh draw b5 shadergraph params shell blocks fake b5 provenance", () => {
  const tmpDir = fs.mkdtempSync(path.join(os.tmpdir(), "tixl-b5-shadergraph-fake-provenance-"));
  const sourceAudit = readJson(sourceAuditPath);
  const b5 = sourceAudit.requiredBuffers.find((buffer) => buffer.register === "b5" && buffer.name === "Params");
  b5.fields = [{
    name: "Invented",
    type: "float",
    array: null,
    provenance: {
      sourceKind: "ShaderGraphNode.CollectAllNodeParams",
      sourcePaths: ["does/not/exist.cs"],
    },
  }];
  const sourceAuditArtifact = path.join(tmpDir, "fake-provenance-source-audit.json");
  fs.writeFileSync(sourceAuditArtifact, JSON.stringify(sourceAudit, null, 2));
  const fixture = readJson(fixturePath);
  fixture.sourceAuditArtifact = sourceAuditArtifact;
  const brokenFixture = path.join(tmpDir, "fixture.graph.json");
  fs.writeFileSync(brokenFixture, JSON.stringify(fixture, null, 2));

  const run = spawnSync("python3", [scriptPath, brokenFixture, tmpDir], {
    cwd: repoRoot,
    encoding: "utf8",
  });

  assert.equal(run.status, 1);
  const result = readJson(path.join(tmpDir, resultName));
  const errors = readJson(path.join(tmpDir, errorsName));
  assert.equal(result.ok, false);
  assert.equal(result.status, "blocked_invalid_b5_expansion_provenance");
  assert.equal(result.claims.b5ShadergraphParamsExpanded, false);
  assert.equal(errors[0].code, "tixl_mesh_draw_b5_shadergraph_params_expansion.invalid_source_audit_artifact");
  const fields = errors.flatMap((error) => error.mismatches || []).map((mismatch) => mismatch.field);
  assert.ok(fields.includes("requiredBuffers.b5.fields"));
  assertPathClean(result, errors);
});

test("TiXL mesh draw b5 shadergraph params shell blocks invented layout b5 fields", () => {
  const tmpDir = fs.mkdtempSync(path.join(os.tmpdir(), "tixl-b5-shadergraph-invented-layout-"));
  const layout = readJson(layoutPath);
  const b5 = layout.constantBuffers.find((buffer) => buffer.register === "b5");
  b5.fields = [{ name: "Invented", type: "float" }];
  layout.duplicateNamePolicy.b5.fieldsKnownFromSourceAudit = true;
  const layoutArtifact = path.join(tmpDir, "invented-layout.json");
  fs.writeFileSync(layoutArtifact, JSON.stringify(layout, null, 2));
  const fixture = readJson(fixturePath);
  fixture.constantBufferLayoutArtifact = layoutArtifact;
  const brokenFixture = path.join(tmpDir, "fixture.graph.json");
  fs.writeFileSync(brokenFixture, JSON.stringify(fixture, null, 2));

  const run = spawnSync("python3", [scriptPath, brokenFixture, tmpDir], {
    cwd: repoRoot,
    encoding: "utf8",
  });

  assert.equal(run.status, 1);
  const result = readJson(path.join(tmpDir, resultName));
  const errors = readJson(path.join(tmpDir, errorsName));
  assert.equal(result.status, "blocked_invalid_b5_expansion_provenance");
  assert.equal(result.claims.b5FieldsSourceBacked, false);
  assert.equal(errors[0].code, "tixl_mesh_draw_b5_shadergraph_params_expansion.invalid_layout_artifact");
  const fields = errors.flatMap((error) => error.mismatches || []).map((mismatch) => mismatch.field);
  assert.ok(fields.includes("constantBuffers.b5.fields"));
  assertPathClean(result, errors);
});

test("TiXL mesh draw b5 shadergraph params shell blocks stale or widened pointlights verdict", () => {
  const tmpDir = fs.mkdtempSync(path.join(os.tmpdir(), "tixl-b5-shadergraph-stale-pointlights-"));
  const pointlights = readJson(pointlightsPath);
  pointlights.claims.b3PointLightsPackingProven = false;
  pointlights.claims.constantBufferAdapterComplete = true;
  const pointlightsArtifact = path.join(tmpDir, "stale-pointlights.json");
  fs.writeFileSync(pointlightsArtifact, JSON.stringify(pointlights, null, 2));
  const fixture = readJson(fixturePath);
  fixture.pointlightsAndB5PackingArtifact = pointlightsArtifact;
  const brokenFixture = path.join(tmpDir, "fixture.graph.json");
  fs.writeFileSync(brokenFixture, JSON.stringify(fixture, null, 2));

  const run = spawnSync("python3", [scriptPath, brokenFixture, tmpDir], {
    cwd: repoRoot,
    encoding: "utf8",
  });

  assert.equal(run.status, 1);
  const result = readJson(path.join(tmpDir, resultName));
  const errors = readJson(path.join(tmpDir, errorsName));
  assert.equal(result.status, "blocked_invalid_input_artifact");
  assert.equal(result.claims.b3PointLightsPackingProven, false);
  assert.equal(errors[0].code, "tixl_mesh_draw_b5_shadergraph_params_expansion.invalid_pointlights_verdict");
  const fields = errors.flatMap((error) => error.mismatches || []).map((mismatch) => mismatch.field);
  assert.ok(fields.includes("pointlights.claims.b3PointLightsPackingProven"));
  assert.ok(fields.includes("pointlights.claims.constantBufferAdapterComplete"));
  assertPathClean(result, errors);
});

test("TiXL mesh draw b5 shadergraph params shell blocks extra true upstream claims", () => {
  const tmpDir = fs.mkdtempSync(path.join(os.tmpdir(), "tixl-b5-shadergraph-extra-claim-"));
  const pointlights = readJson(pointlightsPath);
  pointlights.claims.nativeCompileParity = true;
  pointlights.claims.hlslToMslTranslationProven = true;
  const pointlightsArtifact = path.join(tmpDir, "extra-claim-pointlights.json");
  fs.writeFileSync(pointlightsArtifact, JSON.stringify(pointlights, null, 2));
  const fixture = readJson(fixturePath);
  fixture.pointlightsAndB5PackingArtifact = pointlightsArtifact;
  const brokenFixture = path.join(tmpDir, "fixture.graph.json");
  fs.writeFileSync(brokenFixture, JSON.stringify(fixture, null, 2));

  const run = spawnSync("python3", [scriptPath, brokenFixture, tmpDir], {
    cwd: repoRoot,
    encoding: "utf8",
  });

  assert.equal(run.status, 1);
  const result = readJson(path.join(tmpDir, resultName));
  const errors = readJson(path.join(tmpDir, errorsName));
  assert.equal(result.status, "blocked_invalid_input_artifact");
  assert.equal(result.claims.b5ShadergraphParamsExpanded, false);
  assert.equal(errors[0].code, "tixl_mesh_draw_b5_shadergraph_params_expansion.invalid_pointlights_verdict");
  const fields = errors.flatMap((error) => error.mismatches || []).map((mismatch) => mismatch.field);
  assert.ok(fields.includes("pointlights.claims.nativeCompileParity"));
  assert.ok(fields.includes("pointlights.claims.hlslToMslTranslationProven"));
  assertPathClean(result, errors);
});

test("TiXL mesh draw b5 shadergraph params checked-in artifacts match fresh shell output", () => {
  const tmpDir = fs.mkdtempSync(path.join(os.tmpdir(), "tixl-b5-shadergraph-stale-check-"));
  const run = spawnSync("python3", [scriptPath, fixturePath, tmpDir], {
    cwd: repoRoot,
    encoding: "utf8",
  });
  assert.equal(run.status, 0, run.stderr || run.stdout);

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

function expectedClaims(expanded) {
  return {
    sourceAuditArtifactConsumed: true,
    constantBufferLayoutArtifactConsumed: true,
    pointlightsAndB5PackingArtifactConsumed: true,
    b3PointLightsPackingProven: true,
    b5ShadergraphParamsExpanded: expanded,
    b5FieldsSourceBacked: expanded,
    b5NativePackingReady: false,
    constantBufferAdapterComplete: false,
    textureSamplerMapping: false,
    fullPbrResourceBinding: false,
    backendReplacementReady: false,
    hlslToMslTranslation: false,
    tixlRuntimeParity: false,
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

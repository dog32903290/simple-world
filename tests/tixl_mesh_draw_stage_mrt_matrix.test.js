const assert = require("node:assert/strict");
const { spawnSync } = require("node:child_process");
const fs = require("node:fs");
const os = require("node:os");
const path = require("node:path");
const test = require("node:test");

const repoRoot = path.resolve(__dirname, "..");
const contractPath = path.join(repoRoot, "docs/runtime/TIXL_MESH_DRAW_STAGE_MRT_MATRIX_PROOF.md");
const fixturePath = path.join(repoRoot, "docs/runtime/fixtures/tixl_mesh_draw_stage_mrt_matrix.graph.json");
const scriptPath = path.join(repoRoot, "docs/runtime/scripts/tixl_mesh_draw_stage_mrt_matrix_shell.py");
const artifactDir = path.join(repoRoot, "docs/runtime/artifacts/tixl_mesh_draw_stage_mrt_matrix");
const resultName = "tixl_mesh_draw_stage_mrt_matrix_result.json";
const traceName = "tixl_mesh_draw_stage_mrt_matrix_trace.json";
const errorsName = "tixl_mesh_draw_stage_mrt_matrix_errors.json";
const mslName = "generated_stage_mrt_matrix_probe.metal";

test("TiXL mesh draw stage/MRT/matrix docs state a source-backed bounded lane", () => {
  const source = fs.readFileSync(contractPath, "utf8");

  assert.match(source, /TiXL Mesh Draw Stage MRT Matrix Proof/);
  assert.match(source, /vsMain\(uint id : SV_VertexID\)/);
  assert.match(source, /psInput\.pixelPosition : SV_POSITION/);
  assert.match(source, /psOutput\.Color : SV_Target0/);
  assert.match(source, /psOutput\.Normal : SV_Target1/);
  assert.match(source, /ddx/);
  assert.match(source, /ddy/);
  assert.match(source, /discard/);
  assert.match(source, /mul\(vector, matrix\)/);
  assert.match(source, /actualMetalProbeRan: true/);
  assert.match(source, /hlslToMslTranslation: false/);
  assert.match(source, /textureCubeSampleLevelGetDimensionsProven: false/);
  assert.match(source, /backendReplacementReady: false/);
});

test("TiXL mesh draw stage/MRT/matrix fixture pins donor and prior artifacts", () => {
  const graph = readJson(fixturePath);

  assert.equal(graph.graphId, "fixture.tixl_mesh_draw_stage_mrt_matrix");
  assert.equal(graph.kind, "TixlMeshDrawStageMrtMatrixProof");
  assert.equal(graph.donorHlsl, "external/tixl/Operators/Lib/Assets/shaders/3d/mesh/mesh-Draw.hlsl");
  assert.equal(graph.sourceAuditArtifact, "docs/runtime/artifacts/tixl_mesh_draw_shader_source_audit/tixl_mesh_draw_shader_source_audit_result.json");
  assert.equal(graph.shadergraphResourcesExpansionArtifact, "docs/runtime/artifacts/tixl_mesh_draw_shadergraph_resources_expansion/tixl_mesh_draw_shadergraph_resources_expansion_result.json");
  assert.equal(graph.b5NativePackingArtifact, "docs/runtime/artifacts/tixl_mesh_draw_b5_native_packing/tixl_mesh_draw_b5_native_packing_result.json");
  assert.equal(graph.textureSamplerBindingArtifact, "docs/runtime/artifacts/tixl_mesh_draw_texture_sampler_binding/tixl_mesh_draw_texture_sampler_binding_result.json");
  assert.equal(graph.adapterBoundary.route, "handwritten_explicit_msl_adapter");
  assert.equal(graph.adapterBoundary.proofType, "source_backed_semantics_only");
  assert.deepEqual(graph.expected.claims, expectedClaims(true));
});

test("TiXL mesh draw stage/MRT/matrix shell emits source-backed proof", () => {
  const tmpDir = fs.mkdtempSync(path.join(os.tmpdir(), "tixl-mesh-draw-stage-mrt-matrix-"));
  const run = spawnSync("python3", [scriptPath, fixturePath, tmpDir], {
    cwd: repoRoot,
    encoding: "utf8",
  });

  assert.equal(run.status, 0, run.stderr || run.stdout);
  const result = readJson(path.join(tmpDir, resultName));
  const trace = readJson(path.join(tmpDir, traceName));
  const errors = readJson(path.join(tmpDir, errorsName));

  assert.deepEqual(errors, []);
  assert.equal(result.kind, "TixlMeshDrawStageMrtMatrixProof");
  assert.equal(result.ok, true);
  assert.equal(result.status, "proven_tixl_mesh_draw_stage_mrt_matrix_semantics");
  assert.deepEqual(result.claims, expectedClaims(true));
  assert.equal(result.stageSemantics.vertexEntry, "vsMain(uint id : SV_VertexID)");
  assert.equal(result.mrtSemantics.target0, "psOutput.Color : SV_Target0; output.Color = litColor");
  assert.equal(result.mrtSemantics.target1, "psOutput.Normal : SV_Target1; output.Normal = float4(worldNormal, 1.0)");
  assert.equal(result.matrixSemantics.convention, "D3D/HLSL mul(vector, matrix)");
  assert.equal(result.sourceFacts.vsMainReadsIndexedPbrVertex, true);
  assert.equal(result.sourceFacts.fragmentUsesDdxDdy, true);
  assert.equal(result.sourceFacts.fragmentUsesDiscard, true);
  assert.equal(result.explicitMetalProbe.status, "proven_explicit_metal_stage_mrt_matrix_probe");
  assert.equal(result.explicitMetalProbe.actualCompilerRan, true);
  assert.equal(result.explicitMetalProbe.actualMetalRan, true);
  assert.deepEqual(result.explicitMetalProbe.target0, [13, 90, 111, 255]);
  assert.deepEqual(result.explicitMetalProbe.target1, [31, 37, 41, 255]);
  assert.equal(result.explicitMetalProbe.tixlDonorHlslMetalProbeRan, false);
  assert.equal(result.explicitMetalProbe.generatedMslArtifact, mslName);
  assert.deepEqual(trace.map((entry) => entry.op), [
    "loadTixlMeshDrawStageMrtMatrixFixture",
    "resolveInputArtifacts",
    "readInputArtifactsAndDonorSource",
    "validateInputArtifacts",
    "parseDonorHlslStageMrtMatrixSemantics",
    "writeExplicitMetalStageMrtMatrixProbe",
    "buildExplicitMetalStageMrtMatrixProbe",
    "runExplicitMetalStageMrtMatrixProbe",
    "validateExplicitMetalStageMrtMatrixProbe",
    "proveStageMrtMatrixSemanticsForHandwrittenAdapter",
    "publishTixlMeshDrawStageMrtMatrixArtifacts",
  ]);
  assertPathClean(result, trace, errors);
});

test("TiXL mesh draw stage/MRT/matrix shell blocks missing donor stage semantics", () => {
  const tmpDir = fs.mkdtempSync(path.join(os.tmpdir(), "tixl-mesh-draw-missing-stage-"));
  const donor = fs.readFileSync(path.join(repoRoot, "external/tixl/Operators/Lib/Assets/shaders/3d/mesh/mesh-Draw.hlsl"), "utf8")
    .replace("uint id : SV_VertexID", "uint id : TEXCOORD0");
  const donorPath = path.join(tmpDir, "forged-mesh-Draw.hlsl");
  fs.writeFileSync(donorPath, donor);
  const fixture = readJson(fixturePath);
  fixture.donorHlsl = donorPath;
  const badFixturePath = path.join(tmpDir, "missing-stage.graph.json");
  fs.writeFileSync(badFixturePath, JSON.stringify(fixture, null, 2));

  const run = spawnSync("python3", [scriptPath, badFixturePath, tmpDir], {
    cwd: repoRoot,
    encoding: "utf8",
  });

  assert.equal(run.status, 1);
  const result = readJson(path.join(tmpDir, resultName));
  const errors = readJson(path.join(tmpDir, errorsName));
  assert.equal(result.status, "blocked_invalid_donor_semantics");
  assert.equal(result.claims.handwrittenMeshDrawAdapterStageMrtMatrixProof, false);
  const fields = errors.flatMap((error) => error.mismatches || []).map((mismatch) => mismatch.field);
  assert.ok(fields.includes("donorHlsl.vsMainUsesSvVertexId"));
  assertPathClean(result, errors);
});

test("TiXL mesh draw stage/MRT/matrix shell blocks missing MRT semantics", () => {
  const tmpDir = fs.mkdtempSync(path.join(os.tmpdir(), "tixl-mesh-draw-missing-mrt-"));
  const donor = fs.readFileSync(path.join(repoRoot, "external/tixl/Operators/Lib/Assets/shaders/3d/mesh/mesh-Draw.hlsl"), "utf8")
    .replace("float4 Normal : SV_Target1;", "float4 Normal : COLOR1;");
  const donorPath = path.join(tmpDir, "forged-mesh-Draw.hlsl");
  fs.writeFileSync(donorPath, donor);
  const fixture = readJson(fixturePath);
  fixture.donorHlsl = donorPath;
  const badFixturePath = path.join(tmpDir, "missing-mrt.graph.json");
  fs.writeFileSync(badFixturePath, JSON.stringify(fixture, null, 2));

  const run = spawnSync("python3", [scriptPath, badFixturePath, tmpDir], {
    cwd: repoRoot,
    encoding: "utf8",
  });

  assert.equal(run.status, 1);
  const result = readJson(path.join(tmpDir, resultName));
  const errors = readJson(path.join(tmpDir, errorsName));
  assert.equal(result.status, "blocked_invalid_donor_semantics");
  const fields = errors.flatMap((error) => error.mismatches || []).map((mismatch) => mismatch.field);
  assert.ok(fields.includes("donorHlsl.psOutputNormalTarget1"));
  assertPathClean(result, errors);
});

test("TiXL mesh draw stage/MRT/matrix shell blocks forged source audit", () => {
  const tmpDir = fs.mkdtempSync(path.join(os.tmpdir(), "tixl-mesh-draw-forged-audit-"));
  const sourceAudit = readJson(path.join(repoRoot, "docs/runtime/artifacts/tixl_mesh_draw_shader_source_audit/tixl_mesh_draw_shader_source_audit_result.json"));
  sourceAudit.entryPoints.psMain.found = false;
  sourceAudit.claims.hlslToMslTranslation = true;
  const sourceAuditPath = path.join(tmpDir, "forged-source-audit.json");
  fs.writeFileSync(sourceAuditPath, JSON.stringify(sourceAudit, null, 2));
  const fixture = readJson(fixturePath);
  fixture.sourceAuditArtifact = sourceAuditPath;
  const badFixturePath = path.join(tmpDir, "forged-audit.graph.json");
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
  assert.ok(fields.includes("sourceAudit.entryPoints.psMain.found"));
  assert.ok(fields.includes("sourceAudit.claims.hlslToMslTranslation"));
  assertPathClean(result, errors);
});

test("TiXL mesh draw stage/MRT/matrix shell blocks widened fixture claims", () => {
  const tmpDir = fs.mkdtempSync(path.join(os.tmpdir(), "tixl-mesh-draw-widened-fixture-"));
  const fixture = readJson(fixturePath);
  fixture.expected.claims.fullPbrResourceBinding = true;
  fixture.expected.claims.backendReplacementReady = true;
  fixture.expected.claims.textureCubeSampleLevelGetDimensionsProven = true;
  const badFixturePath = path.join(tmpDir, "widened-fixture.graph.json");
  fs.writeFileSync(badFixturePath, JSON.stringify(fixture, null, 2));

  const run = spawnSync("python3", [scriptPath, badFixturePath, tmpDir], {
    cwd: repoRoot,
    encoding: "utf8",
  });

  assert.equal(run.status, 1);
  const result = readJson(path.join(tmpDir, resultName));
  const errors = readJson(path.join(tmpDir, errorsName));
  assert.equal(result.status, "blocked_invalid_fixture_expectations");
  const fields = errors.flatMap((error) => error.mismatches || []).map((mismatch) => mismatch.field);
  assert.ok(fields.includes("expected.claims"));
  assert.ok(fields.includes("expected.claims.fullPbrResourceBinding"));
  assert.ok(fields.includes("expected.claims.backendReplacementReady"));
  assert.ok(fields.includes("expected.claims.textureCubeSampleLevelGetDimensionsProven"));
  assertPathClean(result, errors);
});

test("TiXL mesh draw stage/MRT/matrix checked-in artifacts are path-clean and fresh", () => {
  const result = readJson(path.join(artifactDir, resultName));
  const trace = readJson(path.join(artifactDir, traceName));
  const errors = readJson(path.join(artifactDir, errorsName));
  const msl = fs.readFileSync(path.join(artifactDir, mslName), "utf8");

  assert.equal(result.kind, "TixlMeshDrawStageMrtMatrixProof");
  assert.equal(result.ok, true);
  assert.equal(result.status, "proven_tixl_mesh_draw_stage_mrt_matrix_semantics");
  assert.deepEqual(errors, []);
  assert.deepEqual(result.claims, expectedClaims(true));
  assert.match(msl, /my_world_stage_mrt_matrix_vertex/);
  assert.match(msl, /my_world_stage_mrt_matrix_fragment/);
  assert.match(msl, /\[\[color\(0\)\]\]/);
  assert.match(msl, /\[\[color\(1\)\]\]/);
  assertPathClean(result, trace, errors);

  const tmpDir = fs.mkdtempSync(path.join(os.tmpdir(), "tixl-mesh-draw-stage-fresh-"));
  const run = spawnSync("python3", [scriptPath, fixturePath, tmpDir], {
    cwd: repoRoot,
    encoding: "utf8",
  });
  assert.equal(run.status, 0, run.stderr || run.stdout);
  assert.deepEqual(readJson(path.join(tmpDir, resultName)), result);
  assert.deepEqual(readJson(path.join(tmpDir, traceName)), trace);
  assert.deepEqual(readJson(path.join(tmpDir, errorsName)), errors);
  assert.equal(fs.readFileSync(path.join(tmpDir, mslName), "utf8"), msl);
});

function expectedClaims(proven) {
  return {
    donorHlslParsed: proven,
    sourceAuditArtifactConsumed: proven,
    shadergraphResourcesExpansionArtifactConsumed: proven,
    b5NativePackingArtifactConsumed: proven,
    textureSamplerBindingArtifactConsumed: proven,
    vertexStageSemanticProven: proven,
    pixelStageSemanticProven: proven,
    mrtTarget0ColorProven: proven,
    mrtTarget1NormalProven: proven,
    fragmentDerivativeSemanticsPresent: proven,
    alphaDiscardSemanticsPresent: proven,
    d3dMulVectorMatrixConventionPresent: proven,
    explicitMetalStageMrtMatrixProbeRan: proven,
    explicitMetalStageInProven: proven,
    explicitMetalMrtWriteProven: proven,
    explicitMetalVectorMatrixConventionProven: proven,
    handwrittenMeshDrawAdapterStageMrtMatrixProof: proven,
    sourceBackedOnly: proven,
    actualMetalProbeRan: proven,
    tixlDonorHlslMetalProbeRan: false,
    hlslToMslTranslation: false,
    fullPbrResourceBinding: false,
    textureCubeSampleLevelGetDimensionsProven: false,
    backendReplacementReady: false,
    tixlRuntimeParity: false,
    pbrVisualCorrectness: false,
    rendererIntegrationComplete: false,
    constantBufferAdapterComplete: false,
    fullTextureSamplerMapping: false,
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

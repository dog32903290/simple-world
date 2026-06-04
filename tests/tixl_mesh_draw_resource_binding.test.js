const assert = require("node:assert/strict");
const { spawnSync } = require("node:child_process");
const fs = require("node:fs");
const os = require("node:os");
const path = require("node:path");
const test = require("node:test");

const repoRoot = path.resolve(__dirname, "..");
const contractPath = path.join(repoRoot, "docs/runtime/TIXL_MESH_DRAW_RESOURCE_BINDING_PROOF.md");
const fixturePath = path.join(repoRoot, "docs/runtime/fixtures/tixl_mesh_draw_resource_binding.graph.json");
const scriptPath = path.join(repoRoot, "docs/runtime/scripts/tixl_mesh_draw_resource_binding_shell.py");
const artifactDir = path.join(repoRoot, "docs/runtime/artifacts/tixl_mesh_draw_resource_binding");
const resultName = "tixl_mesh_draw_resource_binding_result.json";
const traceName = "tixl_mesh_draw_resource_binding_trace.json";
const errorsName = "tixl_mesh_draw_resource_binding_errors.json";

test("TiXL mesh draw resource binding docs stay inside the ledger boundary", () => {
  const source = fs.readFileSync(contractPath, "utf8");

  assert.match(source, /TiXL Mesh Draw Resource Binding Proof/);
  assert.match(source, /TixlMeshDrawResourceBindingProof answers:/);
  assert.match(source, /binding ledger/);
  assert.match(source, /not an HLSL-to-MSL translator/);
  assert.match(source, /not PBR visual correctness/);
  assert.match(source, /not TiXL\s+runtime parity/);
  assert.match(source, /not renderer\/backend replacement/);
  assert.match(source, /PbrVertices t0 -> Metal buffer\(0\)/);
  assert.match(source, /FaceIndices t1 -> Metal buffer\(1\)/);
  assert.match(source, /drawVertexCount faceCount \* 3/);
  assert.match(source, /declaredButUnbound/);
  assert.match(source, /b5 shadergraph Params/);
  assert.match(source, /t8\+ injected resources/);
  assert.match(source, /FIELD_FUNCTIONS/);
  assert.match(source, /FIELD_CALL/);
  assert.match(source, /meshBufferBindingObserved: true only after all three input artifacts validate/);
  assert.match(source, /fullPbrResourceBinding: false/);
  assert.match(source, /hlslToMslTranslation: false/);
  assert.match(source, /tixlRuntimeParity: false/);
  assert.match(source, /backendReplacementReady: false/);
});

test("TiXL mesh draw resource binding fixture points at the three required proof artifacts", () => {
  const graph = readJson(fixturePath);

  assert.equal(graph.graphId, "fixture.tixl_mesh_draw_resource_binding");
  assert.equal(graph.kind, "TixlMeshDrawResourceBindingProof");
  assert.equal(
    graph.sourceAuditArtifact,
    "docs/runtime/artifacts/tixl_mesh_draw_shader_source_audit/tixl_mesh_draw_shader_source_audit_result.json",
  );
  assert.equal(
    graph.bufferLayoutArtifact,
    "docs/runtime/artifacts/tixl_mesh_draw_buffer_layout/tixl_mesh_draw_buffer_layout_result.json",
  );
  assert.equal(
    graph.mslApproxArtifact,
    "docs/runtime/artifacts/tixl_mesh_draw_msl_approx/tixl_mesh_draw_msl_approx_result.json",
  );
  assert.equal(graph.expected.boundBufferCount, 2);
  assert.equal(graph.expected.declaredButUnboundCount, expectedDeclaredButUnbound().length);
  assert.equal(graph.expected.claims.meshBufferBindingObserved, true);
  assert.equal(graph.expected.claims.fullPbrResourceBinding, false);
});

test("TiXL mesh draw resource binding shell emits exact bound and unbound ledger", () => {
  const tmpDir = fs.mkdtempSync(path.join(os.tmpdir(), "tixl-mesh-draw-resource-binding-"));
  const run = spawnSync("python3", [scriptPath, fixturePath, tmpDir], {
    cwd: repoRoot,
    encoding: "utf8",
  });

  assert.equal(run.status, 0, run.stderr || run.stdout);
  const result = readJson(path.join(tmpDir, resultName));
  const trace = readJson(path.join(tmpDir, traceName));
  const errors = readJson(path.join(tmpDir, errorsName));

  assert.deepEqual(errors, []);
  assert.equal(result.kind, "TixlMeshDrawResourceBindingProof");
  assert.equal(result.ok, true);
  assert.equal(result.status, "summarized_tixl_mesh_draw_resource_binding");
  assert.deepEqual(result.claims, {
    meshBufferBindingObserved: true,
    fullPbrResourceBinding: false,
    hlslToMslTranslation: false,
    tixlRuntimeParity: false,
    backendReplacementReady: false,
  });
  assert.deepEqual(result.bindingLedger.boundNow, expectedBoundNow());
  assert.deepEqual(result.bindingLedger.declaredButUnbound, expectedDeclaredButUnbound());
  assert.equal(result.evidence.mslApproxBufferPackingObserved, true);
  assert.equal(result.evidence.frameDigest, "9c09adf221b57b49");
  assert.equal(result.evidence.controlFrameDigest, "7da9a417bf722b83");
  assert.notEqual(result.evidence.frameDigest, result.evidence.controlFrameDigest);
  assert.deepEqual(trace.map((entry) => entry.op), [
    "loadTixlMeshDrawResourceBindingFixture",
    "resolveInputArtifacts",
    "readInputArtifacts",
    "validateInputArtifacts",
    "buildBindingLedger",
    "publishTixlMeshDrawResourceBindingArtifacts",
  ]);
  assertPathClean(result, trace, errors);
});

test("TiXL mesh draw resource binding shell blocks missing source audit artifact", () => {
  const tmpDir = fs.mkdtempSync(path.join(os.tmpdir(), "tixl-mesh-draw-binding-missing-source-"));
  const fixture = readJson(fixturePath);
  fixture.sourceAuditArtifact = "docs/runtime/artifacts/tixl_mesh_draw_shader_source_audit/does-not-exist.json";
  const brokenFixturePath = path.join(tmpDir, "missing-source.graph.json");
  fs.writeFileSync(brokenFixturePath, JSON.stringify(fixture, null, 2));

  const run = spawnSync("python3", [scriptPath, brokenFixturePath, tmpDir], {
    cwd: repoRoot,
    encoding: "utf8",
  });

  assert.equal(run.status, 1);
  const result = readJson(path.join(tmpDir, resultName));
  const errors = readJson(path.join(tmpDir, errorsName));
  assert.equal(result.ok, false);
  assert.equal(result.status, "blocked_missing_input_artifact");
  assert.equal(result.claims.meshBufferBindingObserved, false);
  assert.equal(errors[0].code, "tixl_mesh_draw_resource_binding.source_audit_read_failed");
  assertPathClean(result, errors);
});

test("TiXL mesh draw resource binding shell blocks invalid source audit artifact", () => {
  const tmpDir = fs.mkdtempSync(path.join(os.tmpdir(), "tixl-mesh-draw-binding-invalid-source-"));
  const source = readJson(path.join(
    repoRoot,
    "docs/runtime/artifacts/tixl_mesh_draw_shader_source_audit/tixl_mesh_draw_shader_source_audit_result.json",
  ));
  source.resources = source.resources.filter((resource) => resource.name !== "NormalMap");
  const sourcePath = path.join(tmpDir, "invalid-source.json");
  fs.writeFileSync(sourcePath, JSON.stringify(source, null, 2));
  const fixture = readJson(fixturePath);
  fixture.sourceAuditArtifact = sourcePath;
  const brokenFixturePath = path.join(tmpDir, "invalid-source.graph.json");
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
  assert.equal(errors[0].code, "tixl_mesh_draw_resource_binding.invalid_source_audit_artifact");
  assert.ok(errors[0].mismatches.some((mismatch) => mismatch.field === "resources"));
  assertPathClean(result, errors);
});

test("TiXL mesh draw resource binding shell blocks source audit artifacts with extra resources", () => {
  const tmpDir = fs.mkdtempSync(path.join(os.tmpdir(), "tixl-mesh-draw-binding-extra-source-"));
  const source = readJson(path.join(
    repoRoot,
    "docs/runtime/artifacts/tixl_mesh_draw_shader_source_audit/tixl_mesh_draw_shader_source_audit_result.json",
  ));
  source.resources.push({
    kind: "Texture2D",
    elementType: "float4",
    name: "UnexpectedExtra",
    register: "t9",
  });
  const sourcePath = path.join(tmpDir, "extra-source.json");
  fs.writeFileSync(sourcePath, JSON.stringify(source, null, 2));
  const fixture = readJson(fixturePath);
  fixture.sourceAuditArtifact = sourcePath;
  const brokenFixturePath = path.join(tmpDir, "extra-source.graph.json");
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
  assert.equal(errors[0].code, "tixl_mesh_draw_resource_binding.invalid_source_audit_artifact");
  assert.ok(errors[0].mismatches.some((mismatch) => mismatch.field === "resources.extra"));
  assertPathClean(result, errors);
});

test("TiXL mesh draw resource binding shell blocks missing buffer layout artifact", () => {
  const tmpDir = fs.mkdtempSync(path.join(os.tmpdir(), "tixl-mesh-draw-binding-missing-layout-"));
  const fixture = readJson(fixturePath);
  fixture.bufferLayoutArtifact = "docs/runtime/artifacts/tixl_mesh_draw_buffer_layout/does-not-exist.json";
  const brokenFixturePath = path.join(tmpDir, "missing-layout.graph.json");
  fs.writeFileSync(brokenFixturePath, JSON.stringify(fixture, null, 2));

  const run = spawnSync("python3", [scriptPath, brokenFixturePath, tmpDir], {
    cwd: repoRoot,
    encoding: "utf8",
  });

  assert.equal(run.status, 1);
  const result = readJson(path.join(tmpDir, resultName));
  const errors = readJson(path.join(tmpDir, errorsName));
  assert.equal(result.status, "blocked_missing_input_artifact");
  assert.equal(errors[0].code, "tixl_mesh_draw_resource_binding.buffer_layout_read_failed");
  assertPathClean(result, errors);
});

test("TiXL mesh draw resource binding shell blocks invalid buffer layout artifact", () => {
  const tmpDir = fs.mkdtempSync(path.join(os.tmpdir(), "tixl-mesh-draw-binding-invalid-layout-"));
  const layout = readJson(path.join(
    repoRoot,
    "docs/runtime/artifacts/tixl_mesh_draw_buffer_layout/tixl_mesh_draw_buffer_layout_result.json",
  ));
  layout.pbrVertex.strideBytes = 84;
  const layoutPath = path.join(tmpDir, "invalid-layout.json");
  fs.writeFileSync(layoutPath, JSON.stringify(layout, null, 2));
  const fixture = readJson(fixturePath);
  fixture.bufferLayoutArtifact = layoutPath;
  const brokenFixturePath = path.join(tmpDir, "invalid-layout.graph.json");
  fs.writeFileSync(brokenFixturePath, JSON.stringify(fixture, null, 2));

  const run = spawnSync("python3", [scriptPath, brokenFixturePath, tmpDir], {
    cwd: repoRoot,
    encoding: "utf8",
  });

  assert.equal(run.status, 1);
  const result = readJson(path.join(tmpDir, resultName));
  const errors = readJson(path.join(tmpDir, errorsName));
  assert.equal(result.status, "blocked_invalid_input_artifact");
  assert.ok(errors.some((error) => error.code === "tixl_mesh_draw_resource_binding.invalid_buffer_layout_artifact"));
  assert.ok(errors.flatMap((error) => error.mismatches || []).some((mismatch) => mismatch.field === "pbrVertex.strideBytes"));
  assertPathClean(result, errors);
});

test("TiXL mesh draw resource binding shell blocks missing MSL approximation artifact", () => {
  const tmpDir = fs.mkdtempSync(path.join(os.tmpdir(), "tixl-mesh-draw-binding-missing-msl-"));
  const fixture = readJson(fixturePath);
  fixture.mslApproxArtifact = "docs/runtime/artifacts/tixl_mesh_draw_msl_approx/does-not-exist.json";
  const brokenFixturePath = path.join(tmpDir, "missing-msl.graph.json");
  fs.writeFileSync(brokenFixturePath, JSON.stringify(fixture, null, 2));

  const run = spawnSync("python3", [scriptPath, brokenFixturePath, tmpDir], {
    cwd: repoRoot,
    encoding: "utf8",
  });

  assert.equal(run.status, 1);
  const result = readJson(path.join(tmpDir, resultName));
  const errors = readJson(path.join(tmpDir, errorsName));
  assert.equal(result.status, "blocked_missing_input_artifact");
  assert.equal(errors[0].code, "tixl_mesh_draw_resource_binding.msl_approx_read_failed");
  assertPathClean(result, errors);
});

test("TiXL mesh draw resource binding shell blocks invalid MSL approximation artifact", () => {
  const tmpDir = fs.mkdtempSync(path.join(os.tmpdir(), "tixl-mesh-draw-binding-invalid-msl-"));
  const msl = readJson(path.join(
    repoRoot,
    "docs/runtime/artifacts/tixl_mesh_draw_msl_approx/tixl_mesh_draw_msl_approx_result.json",
  ));
  msl.claims.mslApproxBufferPackingObserved = false;
  const mslPath = path.join(tmpDir, "invalid-msl.json");
  fs.writeFileSync(mslPath, JSON.stringify(msl, null, 2));
  const fixture = readJson(fixturePath);
  fixture.mslApproxArtifact = mslPath;
  const brokenFixturePath = path.join(tmpDir, "invalid-msl.graph.json");
  fs.writeFileSync(brokenFixturePath, JSON.stringify(fixture, null, 2));

  const run = spawnSync("python3", [scriptPath, brokenFixturePath, tmpDir], {
    cwd: repoRoot,
    encoding: "utf8",
  });

  assert.equal(run.status, 1);
  const result = readJson(path.join(tmpDir, resultName));
  const errors = readJson(path.join(tmpDir, errorsName));
  assert.equal(result.status, "blocked_invalid_input_artifact");
  assert.ok(errors.some((error) => error.code === "tixl_mesh_draw_resource_binding.invalid_msl_approx_artifact"));
  assert.ok(errors.flatMap((error) => error.mismatches || []).some((mismatch) => mismatch.field === "claims.mslApproxBufferPackingObserved"));
  assertPathClean(result, errors);
});

test("TiXL mesh draw resource binding shell blocks widened MSL approximation claims", () => {
  const tmpDir = fs.mkdtempSync(path.join(os.tmpdir(), "tixl-mesh-draw-binding-widened-msl-"));
  const msl = readJson(path.join(
    repoRoot,
    "docs/runtime/artifacts/tixl_mesh_draw_msl_approx/tixl_mesh_draw_msl_approx_result.json",
  ));
  msl.claims.fullPbrResourceBinding = true;
  msl.claims.backendReplacementReady = true;
  const mslPath = path.join(tmpDir, "widened-msl.json");
  fs.writeFileSync(mslPath, JSON.stringify(msl, null, 2));
  const fixture = readJson(fixturePath);
  fixture.mslApproxArtifact = mslPath;
  const brokenFixturePath = path.join(tmpDir, "widened-msl.graph.json");
  fs.writeFileSync(brokenFixturePath, JSON.stringify(fixture, null, 2));

  const run = spawnSync("python3", [scriptPath, brokenFixturePath, tmpDir], {
    cwd: repoRoot,
    encoding: "utf8",
  });

  assert.equal(run.status, 1);
  const result = readJson(path.join(tmpDir, resultName));
  const errors = readJson(path.join(tmpDir, errorsName));
  assert.equal(result.status, "blocked_invalid_input_artifact");
  assert.ok(errors.some((error) => error.code === "tixl_mesh_draw_resource_binding.invalid_msl_approx_artifact"));
  const mismatchFields = errors.flatMap((error) => error.mismatches || []).map((mismatch) => mismatch.field);
  assert.ok(mismatchFields.includes("claims.fullPbrResourceBinding"));
  assert.ok(mismatchFields.includes("claims.backendReplacementReady"));
  assertPathClean(result, errors);
});

test("TiXL mesh draw resource binding checked-in artifacts are path-clean and source-clean", () => {
  const result = readJson(path.join(artifactDir, resultName));
  const trace = readJson(path.join(artifactDir, traceName));
  const errors = readJson(path.join(artifactDir, errorsName));
  const combined = JSON.stringify([result, trace, errors]);

  assert.equal(result.kind, "TixlMeshDrawResourceBindingProof");
  assert.equal(result.ok, true);
  assert.deepEqual(errors, []);
  assert.deepEqual(result.bindingLedger.boundNow, expectedBoundNow());
  assert.deepEqual(result.bindingLedger.declaredButUnbound, expectedDeclaredButUnbound());
  assertPathClean(result, trace, errors);
  assert.ok(!combined.includes("/Users/"));
  assert.ok(!combined.includes("#include"));
  assert.ok(!combined.includes("struct psInput"));
  assert.ok(!combined.includes("float4x4 CameraToClipSpace"));
});

function expectedBoundNow() {
  return [
    {
      sourceRegister: "t0",
      sourceName: "PbrVertices",
      sourceKind: "StructuredBuffer<PbrVertex>",
      metalBinding: "buffer(0)",
      strideBytes: 80,
      observedIn: "TixlMeshDrawMslApproxProof",
    },
    {
      sourceRegister: "t1",
      sourceName: "FaceIndices",
      sourceKind: "StructuredBuffer<int3>",
      metalBinding: "buffer(1)",
      strideBytes: 12,
      drawVertexCount: 3,
      drawVertexCountFormula: "faceCount * 3",
      faceCount: 1,
      observedIn: "TixlMeshDrawMslApproxProof",
    },
  ];
}

function expectedDeclaredButUnbound() {
  return [
    { sourceRegister: "b0", sourceName: "Transforms", sourceKind: "cbuffer", reason: "not bound in explicit MSL approximation" },
    { sourceRegister: "b1", sourceName: "Params", sourceKind: "cbuffer", reason: "not bound in explicit MSL approximation" },
    { sourceRegister: "b2", sourceName: "FogParams", sourceKind: "cbuffer", reason: "not bound in explicit MSL approximation" },
    { sourceRegister: "b3", sourceName: "PointLights", sourceKind: "cbuffer", reason: "not bound in explicit MSL approximation" },
    { sourceRegister: "b4", sourceName: "PbrParams", sourceKind: "cbuffer", reason: "material constants not bound in explicit MSL approximation" },
    { sourceRegister: "b5", sourceName: "Params", sourceKind: "shadergraph params cbuffer", reason: "not bound in explicit MSL approximation" },
    { sourceRegister: "t2", sourceName: "BaseColorMap", sourceKind: "Texture2D<float4>", reason: "not bound in explicit MSL approximation" },
    { sourceRegister: "t3", sourceName: "EmissiveColorMap", sourceKind: "Texture2D<float4>", reason: "not bound in explicit MSL approximation" },
    { sourceRegister: "t4", sourceName: "RSMOMap", sourceKind: "Texture2D<float4>", reason: "not bound in explicit MSL approximation" },
    { sourceRegister: "t5", sourceName: "NormalMap", sourceKind: "Texture2D<float4>", reason: "not bound in explicit MSL approximation" },
    { sourceRegister: "t6", sourceName: "PrefilteredSpecular", sourceKind: "TextureCube<float4>", reason: "not bound in explicit MSL approximation" },
    { sourceRegister: "t7", sourceName: "BRDFLookup", sourceKind: "Texture2D<float4>", reason: "not bound in explicit MSL approximation" },
    { sourceRegister: "s0", sourceName: "WrappedSampler", sourceKind: "sampler", reason: "not bound in explicit MSL approximation" },
    { sourceRegister: "s1", sourceName: "ClampedSampler", sourceKind: "sampler", reason: "not bound in explicit MSL approximation" },
    { sourceRegister: "t8+", sourceName: "injected resources", sourceKind: "shader template resources", reason: "RESOURCES(t8) template hole not expanded or bound in explicit MSL approximation" },
    { sourceRegister: "FLOAT_PARAMS", sourceName: "shadergraph float params", sourceKind: "shader template constants", reason: "template hole not expanded or bound in explicit MSL approximation" },
    { sourceRegister: "GLOBALS", sourceName: "shadergraph globals", sourceKind: "shader template declarations", reason: "template hole not expanded or bound in explicit MSL approximation" },
    { sourceRegister: "FIELD_FUNCTIONS", sourceName: "shadergraph field functions", sourceKind: "shader template functions", reason: "template hole not expanded or bound in explicit MSL approximation" },
    { sourceRegister: "FIELD_CALL", sourceName: "shadergraph field call", sourceKind: "shader template expression", reason: "template hole not expanded or bound in explicit MSL approximation" },
  ];
}

function assertPathClean(...values) {
  const text = JSON.stringify(values);
  assert.ok(!text.includes("/Users/"));
  assert.ok(!text.includes(repoRoot));
}

function readJson(filePath) {
  return JSON.parse(fs.readFileSync(filePath, "utf8"));
}

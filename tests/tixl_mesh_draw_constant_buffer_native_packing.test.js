const assert = require("node:assert/strict");
const { spawnSync } = require("node:child_process");
const fs = require("node:fs");
const os = require("node:os");
const path = require("node:path");
const test = require("node:test");

const repoRoot = path.resolve(__dirname, "..");
const contractPath = path.join(repoRoot, "docs/runtime/TIXL_MESH_DRAW_CONSTANT_BUFFER_NATIVE_PACKING_PROOF.md");
const fixturePath = path.join(repoRoot, "docs/runtime/fixtures/tixl_mesh_draw_constant_buffer_native_packing.graph.json");
const scriptPath = path.join(repoRoot, "docs/runtime/scripts/tixl_mesh_draw_constant_buffer_native_packing_shell.py");
const artifactDir = path.join(repoRoot, "docs/runtime/artifacts/tixl_mesh_draw_constant_buffer_native_packing");
const layoutArtifactPath = path.join(
  repoRoot,
  "docs/runtime/artifacts/tixl_mesh_draw_constant_buffer_layout/tixl_mesh_draw_constant_buffer_layout_result.json",
);
const resultName = "tixl_mesh_draw_constant_buffer_native_packing_result.json";
const traceName = "tixl_mesh_draw_constant_buffer_native_packing_trace.json";
const errorsName = "tixl_mesh_draw_constant_buffer_native_packing_errors.json";

test("TiXL mesh draw constant buffer native packing docs define a narrow proof lane", () => {
  const source = fs.readFileSync(contractPath, "utf8");

  assert.match(source, /TiXL Mesh Draw Constant Buffer Native Packing Proof/);
  assert.match(source, /TixlMeshDrawConstantBufferNativePackingProof answers:/);
  assert.match(source, /handwritten_explicit_msl_adapter/);
  assert.match(source, /b0 Transforms/);
  assert.match(source, /b1 Params/);
  assert.match(source, /b2 FogParams/);
  assert.match(source, /b4 PbrParams/);
  assert.match(source, /b3 PointLights.*pending/s);
  assert.match(source, /b5 duplicate Params.*pending/s);
  assert.match(source, /not texture\/sampler mapping/);
  assert.match(source, /not full PBR resource binding/);
  assert.match(source, /not backend replacement/);
  assert.match(source, /nativePackingProofComplete: false/);
  assert.match(source, /b0b5AdapterReady: false/);
  assert.match(source, /textureSamplerMapping: false/);
  assert.match(source, /fullPbrResourceBinding: false/);
  assert.match(source, /backendReplacementReady: false/);
});

test("TiXL mesh draw constant buffer native packing fixture pins conservative expectations", () => {
  const graph = readJson(fixturePath);

  assert.equal(graph.graphId, "fixture.tixl_mesh_draw_constant_buffer_native_packing");
  assert.equal(graph.kind, "TixlMeshDrawConstantBufferNativePackingProof");
  assert.equal(
    graph.layoutArtifact,
    "docs/runtime/artifacts/tixl_mesh_draw_constant_buffer_layout/tixl_mesh_draw_constant_buffer_layout_result.json",
  );
  assert.deepEqual(graph.probeRegisters, ["b0", "b1", "b2", "b4"]);
  assert.deepEqual(graph.pendingRegisters, ["b3", "b5"]);
  assert.deepEqual(graph.expected.claims, expectedClaims(false));
  assert.deepEqual(graph.expected.provenNativePacking, expectedProvenNativePacking());
});

test("TiXL mesh draw constant buffer native packing shell runs real Metal or blocks without widening claims", () => {
  const tmpDir = fs.mkdtempSync(path.join(os.tmpdir(), "tixl-mesh-draw-cbuffer-native-packing-"));
  const run = spawnSync("python3", [scriptPath, fixturePath, tmpDir], {
    cwd: repoRoot,
    encoding: "utf8",
  });

  assert.ok(run.status === 0 || run.status === 1, run.stderr || run.stdout);
  const result = readJson(path.join(tmpDir, resultName));
  const trace = readJson(path.join(tmpDir, traceName));
  const errors = readJson(path.join(tmpDir, errorsName));

  assert.equal(result.kind, "TixlMeshDrawConstantBufferNativePackingProof");
  assert.equal(trace[0].fixture, "docs/runtime/fixtures/tixl_mesh_draw_constant_buffer_native_packing.graph.json");
  assert.equal(result.layoutArtifact.path, "docs/runtime/artifacts/tixl_mesh_draw_constant_buffer_layout/tixl_mesh_draw_constant_buffer_layout_result.json");
  assert.equal(result.claims.constantBufferLayoutArtifactConsumed, true);
  assert.equal(result.claims.nativePackingProofComplete, false);
  assert.equal(result.claims.b0b5AdapterReady, false);
  assert.equal(result.claims.textureSamplerMapping, false);
  assert.equal(result.claims.fullPbrResourceBinding, false);
  assert.equal(result.claims.backendReplacementReady, false);
  assert.equal(result.claims.hlslToMslTranslation, false);
  assert.equal(result.claims.tixlRuntimeParity, false);
  assert.equal(result.claims.pbrVisualCorrectness, false);
  assert.deepEqual(result.pendingNativePacking, expectedPendingNativePacking());
  assertPathClean(result, trace, errors);

  if (run.status === 0) {
    assert.equal(result.ok, true);
    assert.equal(result.status, "proven_partial_native_constant_buffer_packing");
    assert.equal(result.claims.actualMetalPackingProbeRan, true);
    assert.deepEqual(result.claims, expectedClaims(true));
    assert.deepEqual(compactProvenNativePacking(result.provenNativePacking), expectedProvenNativePacking());
    assert.deepEqual(errors, []);
    return;
  }

  assert.equal(result.ok, false);
  assert.equal(result.status, "blocked_needs_native_packing_probe");
  assert.equal(result.claims.actualMetalPackingProbeRan, false);
  assert.deepEqual(result.claims, expectedClaims(false));
  assert.ok(errors.length > 0);
});

test("TiXL mesh draw constant buffer native packing shell blocks invalid layout artifact", () => {
  const tmpDir = fs.mkdtempSync(path.join(os.tmpdir(), "tixl-mesh-draw-cbuffer-native-invalid-layout-"));
  const invalidLayoutPath = path.join(tmpDir, "invalid-layout.json");
  fs.writeFileSync(invalidLayoutPath, JSON.stringify({
    kind: "TixlMeshDrawConstantBufferLayoutProof",
    ok: true,
    status: "classified_tixl_mesh_draw_constant_buffer_layout",
    selectedStrategy: "handwritten_explicit_msl_adapter",
    constantBuffers: [],
    bindingPolicy: { readiness: "bounded_partial" },
    claims: { fullPbrResourceBinding: true },
  }, null, 2));
  const fixture = readJson(fixturePath);
  fixture.layoutArtifact = invalidLayoutPath;
  const brokenFixturePath = path.join(tmpDir, "invalid-layout.graph.json");
  fs.writeFileSync(brokenFixturePath, JSON.stringify(fixture, null, 2));

  const run = spawnSync("python3", [scriptPath, brokenFixturePath, tmpDir], {
    cwd: repoRoot,
    encoding: "utf8",
  });

  assert.equal(run.status, 1);
  const result = readJson(path.join(tmpDir, resultName));
  const errors = readJson(path.join(tmpDir, errorsName));
  assert.equal(result.ok, false);
  assert.equal(result.status, "blocked_invalid_layout_artifact");
  assert.equal(result.claims.constantBufferLayoutArtifactConsumed, false);
  assert.equal(result.claims.actualMetalPackingProbeRan, false);
  assert.equal(errors[0].code, "tixl_mesh_draw_constant_buffer_native_packing.invalid_layout_artifact");
  const mismatchFields = errors.flatMap((error) => error.mismatches || []).map((mismatch) => mismatch.field);
  assert.ok(mismatchFields.includes("constantBuffers"));
  assert.ok(mismatchFields.includes("claims.fullPbrResourceBinding"));
  assertPathClean(result, errors);
});

test("TiXL mesh draw constant buffer native packing shell blocks widened fixture claims", () => {
  const tmpDir = fs.mkdtempSync(path.join(os.tmpdir(), "tixl-mesh-draw-cbuffer-native-widened-fixture-"));
  const fixture = readJson(fixturePath);
  fixture.expected.claims.nativePackingProofComplete = true;
  fixture.expected.claims.b0b5AdapterReady = true;
  fixture.expected.claims.fullPbrResourceBinding = true;
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
  assert.equal(result.claims.nativePackingProofComplete, false);
  assert.equal(result.claims.b0b5AdapterReady, false);
  assert.equal(result.claims.fullPbrResourceBinding, false);
  assert.equal(errors[0].code, "tixl_mesh_draw_constant_buffer_native_packing.invalid_fixture_expectations");
  assertPathClean(result, errors);
});

test("TiXL mesh draw constant buffer native packing checked-in artifacts match the current shell output", () => {
  const tmpDir = fs.mkdtempSync(path.join(os.tmpdir(), "tixl-mesh-draw-cbuffer-native-stale-check-"));
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

test("TiXL mesh draw constant buffer native packing source layout artifact exists", () => {
  assert.equal(fs.existsSync(layoutArtifactPath), true);
  const layout = readJson(layoutArtifactPath);
  assert.equal(layout.kind, "TixlMeshDrawConstantBufferLayoutProof");
  assert.equal(layout.ok, true);
  assert.equal(layout.selectedStrategy, "handwritten_explicit_msl_adapter");
  assert.equal(layout.claims.b0b5LayoutNeedsNativePackingProof, true);
  assert.equal(layout.claims.fullPbrResourceBinding, false);
  assert.equal(layout.claims.backendReplacementReady, false);
});

function expectedClaims(actualMetalPackingProbeRan) {
  return {
    constantBufferLayoutArtifactConsumed: true,
    actualMetalPackingProbeRan,
    nativePackingProofComplete: false,
    b0b5AdapterReady: false,
    textureSamplerMapping: false,
    fullPbrResourceBinding: false,
    backendReplacementReady: false,
    hlslToMslTranslation: false,
    tixlRuntimeParity: false,
    pbrVisualCorrectness: false,
  };
}

function expectedProvenNativePacking() {
  return [
    {
      register: "b0",
      name: "Transforms",
      metalBuffer: 2,
      sizeBytes: 640,
      offsets: {
        CameraToClipSpace: 0,
        ClipSpaceToCamera: 64,
        WorldToCamera: 128,
        CameraToWorld: 192,
        WorldToClipSpace: 256,
        ClipSpaceToWorld: 320,
        ObjectToWorld: 384,
        WorldToObject: 448,
        ObjectToCamera: 512,
        ObjectToClipSpace: 576,
      },
    },
    {
      register: "b1",
      name: "Params",
      semanticRole: "mesh_draw_material_params",
      metalBuffer: 3,
      sizeBytes: 32,
      offsets: {
        Color: 0,
        AlphaCutOff: 16,
        UseFlatShading: 20,
        SpecularAA: 24,
      },
    },
    {
      register: "b2",
      name: "FogParams",
      metalBuffer: 4,
      sizeBytes: 32,
      offsets: {
        FogColor: 0,
        FogDistance: 16,
        FogBias: 20,
      },
    },
    {
      register: "b4",
      name: "PbrParams",
      metalBuffer: 6,
      sizeBytes: 48,
      offsets: {
        BaseColor: 0,
        EmissiveColor: 16,
        Roughness: 32,
        Specular: 36,
        Metal: 40,
      },
    },
  ];
}

function expectedPendingNativePacking() {
  return [
    {
      register: "b3",
      name: "PointLights",
      reason: "PointLight element layout is not proven in this tiny packing probe.",
    },
    {
      register: "b5",
      name: "Params",
      semanticRole: "shadergraph_duplicate_params",
      reason: "The source audit has no concrete b5 Params fields to pack.",
    },
  ];
}

function compactProvenNativePacking(buffers) {
  return buffers.map((buffer) => ({
    register: buffer.register,
    name: buffer.name,
    ...(buffer.semanticRole ? { semanticRole: buffer.semanticRole } : {}),
    metalBuffer: buffer.metalBuffer,
    sizeBytes: buffer.sizeBytes,
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

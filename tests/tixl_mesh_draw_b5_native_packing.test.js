const assert = require("node:assert/strict");
const { spawnSync } = require("node:child_process");
const fs = require("node:fs");
const os = require("node:os");
const path = require("node:path");
const test = require("node:test");

const repoRoot = path.resolve(__dirname, "..");
const contractPath = path.join(repoRoot, "docs/runtime/TIXL_MESH_DRAW_B5_NATIVE_PACKING_PROOF.md");
const fixturePath = path.join(repoRoot, "docs/runtime/fixtures/tixl_mesh_draw_b5_native_packing.graph.json");
const scriptPath = path.join(repoRoot, "docs/runtime/scripts/tixl_mesh_draw_b5_native_packing_shell.py");
const artifactDir = path.join(repoRoot, "docs/runtime/artifacts/tixl_mesh_draw_b5_native_packing");
const expansionPath = path.join(
  repoRoot,
  "docs/runtime/artifacts/tixl_mesh_draw_b5_shadergraph_params_expansion/tixl_mesh_draw_b5_shadergraph_params_expansion_result.json",
);
const pointlightsPath = path.join(
  repoRoot,
  "docs/runtime/artifacts/tixl_mesh_draw_pointlights_and_b5_packing/tixl_mesh_draw_pointlights_and_b5_packing_result.json",
);
const resultName = "tixl_mesh_draw_b5_native_packing_result.json";
const traceName = "tixl_mesh_draw_b5_native_packing_trace.json";
const errorsName = "tixl_mesh_draw_b5_native_packing_errors.json";
const mslName = "generated_b5_shadergraph_params_packing_probe.metal";

test("TiXL mesh draw b5 native packing docs define the source-backed Metal probe lane", () => {
  const source = fs.readFileSync(contractPath, "utf8");

  assert.match(source, /TiXL Mesh Draw B5 Native Packing Proof/);
  assert.match(source, /b5 Params -> Metal buffer\(7\)/);
  assert.match(source, /SphereSDF_nG1CBDm_Center/);
  assert.match(source, /SphereSDF_nG1CBDm_Radius/);
  assert.match(source, /actualMetalB5PackingProbeRan: true/);
  assert.match(source, /constantBufferAdapterComplete: false/);
});

test("TiXL mesh draw b5 native packing fixture pins the exact b5 buffer contract", () => {
  const graph = readJson(fixturePath);

  assert.equal(graph.graphId, "fixture.tixl_mesh_draw_b5_native_packing");
  assert.equal(graph.kind, "TixlMeshDrawB5NativePackingProof");
  assert.equal(
    graph.b5ShadergraphParamsExpansionArtifact,
    "docs/runtime/artifacts/tixl_mesh_draw_b5_shadergraph_params_expansion/tixl_mesh_draw_b5_shadergraph_params_expansion_result.json",
  );
  assert.equal(
    graph.constantBufferLayoutArtifact,
    "docs/runtime/artifacts/tixl_mesh_draw_constant_buffer_layout/tixl_mesh_draw_constant_buffer_layout_result.json",
  );
  assert.equal(
    graph.pointlightsAndB5PackingArtifact,
    "docs/runtime/artifacts/tixl_mesh_draw_pointlights_and_b5_packing/tixl_mesh_draw_pointlights_and_b5_packing_result.json",
  );
  assert.deepEqual(graph.probeRegisters, ["b5"]);
  assert.deepEqual(graph.expected.b5NativePacking, expectedB5Packing());
  assert.deepEqual(graph.expected.claims, expectedClaims(false));
});

test("TiXL mesh draw b5 native packing shell proves b5 with Metal or blocks without widening claims", () => {
  const tmpDir = fs.mkdtempSync(path.join(os.tmpdir(), "tixl-b5-native-packing-"));
  const run = spawnSync("python3", [scriptPath, fixturePath, tmpDir], {
    cwd: repoRoot,
    encoding: "utf8",
  });

  assert.ok(run.status === 0 || run.status === 1, run.stderr || run.stdout);
  const result = readJson(path.join(tmpDir, resultName));
  const trace = readJson(path.join(tmpDir, traceName));
  const errors = readJson(path.join(tmpDir, errorsName));
  const generatedMsl = fs.readFileSync(path.join(tmpDir, mslName), "utf8");

  assert.equal(result.kind, "TixlMeshDrawB5NativePackingProof");
  assert.equal(trace[0].fixture, "docs/runtime/fixtures/tixl_mesh_draw_b5_native_packing.graph.json");
  assert.equal(result.inputArtifacts.b5ShadergraphParamsExpansion.path, "docs/runtime/artifacts/tixl_mesh_draw_b5_shadergraph_params_expansion/tixl_mesh_draw_b5_shadergraph_params_expansion_result.json");
  assert.equal(result.inputArtifacts.constantBufferLayout.path, "docs/runtime/artifacts/tixl_mesh_draw_constant_buffer_layout/tixl_mesh_draw_constant_buffer_layout_result.json");
  assert.equal(result.inputArtifacts.pointlightsAndB5Packing.path, "docs/runtime/artifacts/tixl_mesh_draw_pointlights_and_b5_packing/tixl_mesh_draw_pointlights_and_b5_packing_result.json");
  assert.match(generatedMsl, /constant B5Params& b5 \[\[buffer\(7\)\]\]/);
  assert.match(generatedMsl, /packed_float3 SphereSDF_nG1CBDm_Center/);
  assert.match(generatedMsl, /float SphereSDF_nG1CBDm_Radius/);
  assert.ok(trace.some((entry) => entry.op === "packB5HostBytesFromExpansionArtifact"));
  assert.equal(result.claims.b5ShadergraphParamsExpansionArtifactConsumed, true);
  assert.equal(result.claims.constantBufferLayoutArtifactConsumed, true);
  assert.equal(result.claims.pointlightsAndB5PackingArtifactConsumed, true);
  assert.equal(result.claims.b3PointLightsPackingProven, true);
  assert.equal(result.claims.b5ShadergraphParamsExpanded, true);
  assert.equal(result.claims.constantBufferAdapterComplete, false);
  assert.equal(result.claims.textureSamplerMapping, false);
  assert.equal(result.claims.fullPbrResourceBinding, false);
  assert.equal(result.claims.backendReplacementReady, false);
  assert.equal(result.claims.hlslToMslTranslation, false);
  assert.equal(result.claims.tixlRuntimeParity, false);
  assert.equal(result.claims.pbrVisualCorrectness, false);
  assertPathClean(result, trace, errors);

  if (run.status === 0) {
    assert.equal(result.ok, true);
    assert.equal(result.status, "proven_b5_shadergraph_params_native_packing");
    assert.equal(result.claims.actualMetalB5PackingProbeRan, true);
    assert.equal(result.claims.b5NativePackingProven, true);
    assert.deepEqual(result.claims, expectedClaims(true));
    assert.deepEqual(result.provenNativePacking, expectedB5Packing());
    assert.deepEqual(errors, []);
    return;
  }

  assert.equal(result.ok, false);
  assert.equal(result.status, "blocked_needs_b5_native_packing_probe");
  assert.equal(result.claims.actualMetalB5PackingProbeRan, false);
  assert.equal(result.claims.b5NativePackingProven, false);
  assert.deepEqual(result.claims, expectedClaims(false));
  assert.ok(errors.length > 0);
});

test("TiXL mesh draw b5 native packing shell packs host b5 bytes from the expansion artifact", () => {
  const source = fs.readFileSync(scriptPath, "utf8");

  assert.match(source, /run_native_probe\(repo_root, expansion\)/);
  assert.match(source, /def b5_host_bytes_from_expansion\(expansion/);
  assert.match(source, /expansion\.floatBuffer\.values \+ expansion\.fields\.offsetBytes/);
  assert.match(source, /b5_bytes\.hex\(\)/);
  assert.doesNotMatch(source, /putFloat\s*\(/);
  assert.doesNotMatch(source, /-1\.4845504f/);
  assert.doesNotMatch(source, /0\.54366434f/);
});

test("TiXL mesh draw b5 native packing shell blocks forged expansion field values and offsets before probe", () => {
  const tmpDir = fs.mkdtempSync(path.join(os.tmpdir(), "tixl-b5-native-forged-fields-"));
  const expansion = readJson(expansionPath);
  expansion.expansion.fields[0].offsetBytes = 4;
  expansion.expansion.floatBuffer.values[3] = 9.25;
  const expansionArtifact = path.join(tmpDir, "forged-expansion.json");
  fs.writeFileSync(expansionArtifact, JSON.stringify(expansion, null, 2));
  const fixture = readJson(fixturePath);
  fixture.b5ShadergraphParamsExpansionArtifact = expansionArtifact;
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
  assert.equal(result.status, "blocked_invalid_b5_shadergraph_params_expansion");
  assert.equal(result.claims.actualMetalB5PackingProbeRan, false);
  assert.equal(result.claims.b5NativePackingProven, false);
  assert.equal(errors[0].code, "tixl_mesh_draw_b5_native_packing.invalid_b5_shadergraph_params_expansion");
  const fields = errors.flatMap((error) => error.mismatches || []).map((mismatch) => mismatch.field);
  assert.ok(fields.includes("expansion.fields"));
  assert.ok(fields.includes("expansion.floatBuffer.values"));
  assertPathClean(result, errors);
});

test("TiXL mesh draw b5 native packing shell blocks upstream expansion artifacts that already claim native/backend completion", () => {
  const tmpDir = fs.mkdtempSync(path.join(os.tmpdir(), "tixl-b5-native-widened-expansion-"));
  const expansion = readJson(expansionPath);
  expansion.claims.b5NativePackingReady = true;
  expansion.claims.constantBufferAdapterComplete = true;
  expansion.claims.backendReplacementReady = true;
  expansion.expansion.proofBoundary.nativeB5PackingProven = true;
  const expansionArtifact = path.join(tmpDir, "widened-expansion.json");
  fs.writeFileSync(expansionArtifact, JSON.stringify(expansion, null, 2));
  const fixture = readJson(fixturePath);
  fixture.b5ShadergraphParamsExpansionArtifact = expansionArtifact;
  const brokenFixture = path.join(tmpDir, "fixture.graph.json");
  fs.writeFileSync(brokenFixture, JSON.stringify(fixture, null, 2));

  const run = spawnSync("python3", [scriptPath, brokenFixture, tmpDir], {
    cwd: repoRoot,
    encoding: "utf8",
  });

  assert.equal(run.status, 1);
  const result = readJson(path.join(tmpDir, resultName));
  const errors = readJson(path.join(tmpDir, errorsName));
  assert.equal(result.status, "blocked_invalid_b5_shadergraph_params_expansion");
  assert.equal(errors[0].code, "tixl_mesh_draw_b5_native_packing.invalid_b5_shadergraph_params_expansion");
  const fields = errors.flatMap((error) => error.mismatches || []).map((mismatch) => mismatch.field);
  assert.ok(fields.includes("claims.b5NativePackingReady"));
  assert.ok(fields.includes("claims.constantBufferAdapterComplete"));
  assert.ok(fields.includes("claims.backendReplacementReady"));
  assert.ok(fields.includes("expansion.proofBoundary.nativeB5PackingProven"));
  assertPathClean(result, errors);
});

test("TiXL mesh draw b5 native packing shell blocks missing or invalid pointlights artifact", () => {
  const tmpDir = fs.mkdtempSync(path.join(os.tmpdir(), "tixl-b5-native-stale-pointlights-"));
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
  assert.equal(result.claims.pointlightsAndB5PackingArtifactConsumed, false);
  assert.equal(result.claims.b5NativePackingProven, false);
  assert.equal(errors[0].code, "tixl_mesh_draw_b5_native_packing.invalid_pointlights_verdict");
  const fields = errors.flatMap((error) => error.mismatches || []).map((mismatch) => mismatch.field);
  assert.ok(fields.includes("pointlights.claims.b3PointLightsPackingProven"));
  assert.ok(fields.includes("pointlights.claims.constantBufferAdapterComplete"));
  assertPathClean(result, errors);
});

test("TiXL mesh draw b5 native packing shell blocks widened fixture claims", () => {
  const tmpDir = fs.mkdtempSync(path.join(os.tmpdir(), "tixl-b5-native-widened-fixture-"));
  const fixture = readJson(fixturePath);
  fixture.expected.claims.constantBufferAdapterComplete = true;
  fixture.expected.b5NativePacking.sizeBytes = 32;
  const brokenFixture = path.join(tmpDir, "fixture.graph.json");
  fs.writeFileSync(brokenFixture, JSON.stringify(fixture, null, 2));

  const run = spawnSync("python3", [scriptPath, brokenFixture, tmpDir], {
    cwd: repoRoot,
    encoding: "utf8",
  });

  assert.equal(run.status, 1);
  const result = readJson(path.join(tmpDir, resultName));
  const errors = readJson(path.join(tmpDir, errorsName));
  assert.equal(result.status, "blocked_invalid_fixture");
  assert.equal(result.claims.constantBufferAdapterComplete, false);
  assert.equal(errors[0].code, "tixl_mesh_draw_b5_native_packing.invalid_fixture_expectations");
  const fields = errors.flatMap((error) => error.mismatches || []).map((mismatch) => mismatch.field);
  assert.ok(fields.includes("expected.claims"));
  assert.ok(fields.includes("expected.b5NativePacking"));
  assertPathClean(result, errors);
});

test("TiXL mesh draw b5 native packing checked-in artifacts match fresh shell output when Metal is available", () => {
  const tmpDir = fs.mkdtempSync(path.join(os.tmpdir(), "tixl-b5-native-stale-check-"));
  const run = spawnSync("python3", [scriptPath, fixturePath, tmpDir], {
    cwd: repoRoot,
    encoding: "utf8",
  });

  if (run.status === 1) {
    const result = readJson(path.join(tmpDir, resultName));
    assert.equal(result.status, "blocked_needs_b5_native_packing_probe");
    return;
  }

  assert.equal(run.status, 0, run.stderr || run.stdout);
  assert.deepEqual(readJson(path.join(artifactDir, resultName)), readJson(path.join(tmpDir, resultName)));
  assert.deepEqual(readJson(path.join(artifactDir, traceName)), readJson(path.join(tmpDir, traceName)));
  assert.deepEqual(readJson(path.join(artifactDir, errorsName)), readJson(path.join(tmpDir, errorsName)));
  assert.equal(fs.readFileSync(path.join(artifactDir, mslName), "utf8"), fs.readFileSync(path.join(tmpDir, mslName), "utf8"));
});

function expectedB5Packing() {
  return {
    register: "b5",
    name: "Params",
    semanticRole: "shadergraph_duplicate_params",
    metalBuffer: 7,
    sizeBytes: 16,
    offsets: {
      SphereSDF_nG1CBDm_Center: 0,
      SphereSDF_nG1CBDm_Radius: 12,
    },
    values: {
      SphereSDF_nG1CBDm_Center: [-1.4845504, 0, 0.54366434],
      SphereSDF_nG1CBDm_Radius: 0.5,
    },
  };
}

function expectedClaims(proven) {
  return {
    b5ShadergraphParamsExpansionArtifactConsumed: true,
    constantBufferLayoutArtifactConsumed: true,
    pointlightsAndB5PackingArtifactConsumed: true,
    b3PointLightsPackingProven: true,
    b5ShadergraphParamsExpanded: true,
    b5FieldsSourceBacked: true,
    actualMetalB5PackingProbeRan: proven,
    b5NativePackingProven: proven,
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

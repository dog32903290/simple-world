const assert = require("node:assert/strict");
const { spawnSync } = require("node:child_process");
const fs = require("node:fs");
const os = require("node:os");
const path = require("node:path");
const test = require("node:test");

const repoRoot = path.resolve(__dirname, "..");
const contractPath = path.join(repoRoot, "docs/runtime/TIXL_MESH_DRAW_NATIVE_METAL_BACKEND_INTEGRATION_PROOF.md");
const fixturePath = path.join(repoRoot, "docs/runtime/fixtures/tixl_mesh_draw_native_metal_backend_integration.graph.json");
const scriptPath = path.join(repoRoot, "docs/runtime/scripts/tixl_mesh_draw_native_metal_backend_integration_shell.py");
const artifactDir = path.join(repoRoot, "docs/runtime/artifacts/tixl_mesh_draw_native_metal_backend_integration");
const fullPbrBindingArtifactPath = path.join(repoRoot, "docs/runtime/artifacts/tixl_mesh_draw_full_pbr_resource_binding/tixl_mesh_draw_full_pbr_resource_binding_result.json");
const resultName = "tixl_mesh_draw_native_metal_backend_integration_result.json";
const traceName = "tixl_mesh_draw_native_metal_backend_integration_trace.json";
const errorsName = "tixl_mesh_draw_native_metal_backend_integration_errors.json";

test("Native Metal backend integration docs define bounded parity without HLSL translation", () => {
  const source = fs.readFileSync(contractPath, "utf8");
  assert.match(source, /TiXL Mesh Draw Native Metal Backend Integration Proof/);
  assert.match(source, /backendReplacementReady: true/);
  assert.match(source, /nativeGpuParityComplete: true/);
  assert.match(source, /tixlRuntimeParity: true/);
  assert.match(source, /hlslToMslTranslation: false/);
});

test("Native Metal backend integration shell emits bounded native parity proof", () => {
  const tmpDir = fs.mkdtempSync(path.join(os.tmpdir(), "tixl-native-metal-backend-"));
  const run = spawnSync("python3", [scriptPath, fixturePath, tmpDir], {
    cwd: repoRoot,
    encoding: "utf8",
  });

  assert.equal(run.status, 0, run.stderr || run.stdout);
  const result = readJson(path.join(tmpDir, resultName));
  const trace = readJson(path.join(tmpDir, traceName));
  const errors = readJson(path.join(tmpDir, errorsName));

  assert.deepEqual(errors, []);
  assert.equal(result.kind, "TixlMeshDrawNativeMetalBackendIntegrationProof");
  assert.equal(result.ok, true);
  assert.equal(result.status, "proven_native_metal_backend_integration_for_bounded_mesh_draw_pbr_lane");
  assert.deepEqual(result.claims, {
    nativeRenderPipelineArtifactConsumed: true,
    fullPbrResourceBindingArtifactConsumed: true,
    explicitAdapterProofArtifactConsumed: true,
    actualMetalBackendProbeRan: true,
    nativeBackendIntegrationComplete: true,
    runtimeEquivalenceProof: true,
    backendReplacementReady: true,
    nativeGpuParityComplete: true,
    tixlRuntimeParity: true,
    fullPbrResourceBinding: true,
    explicitAdapterProofPresent: true,
    hlslToMslTranslation: false,
  });
  assert.equal(result.nativeDrawBoundary.status, "supported");
  assert.equal(result.nativeDrawBoundary.backendCanCompileNow, true);
  assert.equal(result.equivalence.command.drawCalls, 1);
  assert.equal(result.equivalence.frame.nonBlack, true);
  assert.equal(result.equivalence.frame.varied, true);
  assert.deepEqual(result.equivalence.boundedPbrReference.expectedRgba8, [68, 62, 54, 255]);
  assert.deepEqual(result.equivalence.boundedPbrReference.actualRgba8, [68, 62, 54, 255]);
  assert.ok(trace.map((entry) => entry.op).includes("runNativeMetalBackendIntegrationProbe"));
  assertPathClean(result, trace, errors);
});

test("Native Metal backend integration blocks widened full binding claims before proof", () => {
  const tmpDir = fs.mkdtempSync(path.join(os.tmpdir(), "tixl-native-metal-backend-forged-binding-"));
  const fullBinding = readJson(fullPbrBindingArtifactPath);
  fullBinding.claims.hlslToMslTranslation = true;
  const fullBindingPath = path.join(tmpDir, "forged-full-binding.json");
  fs.writeFileSync(fullBindingPath, JSON.stringify(fullBinding, null, 2));

  const fixture = readJson(fixturePath);
  fixture.fullPbrResourceBindingArtifact = fullBindingPath;
  const forgedFixturePath = path.join(tmpDir, "forged-native-metal-backend.graph.json");
  fs.writeFileSync(forgedFixturePath, JSON.stringify(fixture, null, 2));

  const run = spawnSync("python3", [scriptPath, forgedFixturePath, tmpDir], {
    cwd: repoRoot,
    encoding: "utf8",
  });

  assert.equal(run.status, 1);
  const result = readJson(path.join(tmpDir, resultName));
  const errors = readJson(path.join(tmpDir, errorsName));
  const fields = errors.flatMap((error) => error.mismatches || []).map((mismatch) => mismatch.field);
  assert.equal(result.status, "blocked_invalid_input_artifact");
  assert.ok(fields.includes("fullPbrResourceBinding.claims.hlslToMslTranslation"));
  assert.equal(result.claims.backendReplacementReady, false);
  assert.equal(result.claims.nativeGpuParityComplete, false);
  assertPathClean(result, errors);
});

test("Native Metal backend integration checked-in artifacts are path-clean and fresh", () => {
  const result = readJson(path.join(artifactDir, resultName));
  const trace = readJson(path.join(artifactDir, traceName));
  const errors = readJson(path.join(artifactDir, errorsName));
  assert.equal(result.kind, "TixlMeshDrawNativeMetalBackendIntegrationProof");
  assert.equal(result.ok, true);
  assert.deepEqual(errors, []);
  assertPathClean(result, trace, errors);

  const tmpDir = fs.mkdtempSync(path.join(os.tmpdir(), "tixl-native-metal-backend-fresh-"));
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

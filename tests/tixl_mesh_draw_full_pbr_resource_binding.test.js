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
const textureSamplerArtifactPath = path.join(repoRoot, "docs/runtime/artifacts/tixl_mesh_draw_texture_sampler_binding/tixl_mesh_draw_texture_sampler_binding_result.json");
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

test("Full PBR binding shell blocks widened input artifact claims", () => {
  const tmpDir = fs.mkdtempSync(path.join(os.tmpdir(), "tixl-full-pbr-binding-forged-texture-"));
  const forgedTextureSampler = readJson(textureSamplerArtifactPath);
  forgedTextureSampler.claims.backendReplacementReady = true;
  forgedTextureSampler.claims.tixlRuntimeParity = true;
  const forgedTextureSamplerPath = path.join(tmpDir, "forged-texture-sampler.json");
  fs.writeFileSync(forgedTextureSamplerPath, JSON.stringify(forgedTextureSampler, null, 2));

  const fixture = readJson(fixturePath);
  fixture.textureSamplerBindingArtifact = forgedTextureSamplerPath;
  const forgedFixturePath = path.join(tmpDir, "forged-full-pbr-binding.graph.json");
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
  assert.ok(fields.includes("textureSamplerBinding.claims.backendReplacementReady"));
  assert.ok(fields.includes("textureSamplerBinding.claims.tixlRuntimeParity"));
  assert.equal(result.claims.fullPbrResourceBinding, false);
  assert.equal(result.claims.backendReplacementReady, false);
  assertPathClean(result, errors);
});

function assertPathClean(...values) {
  const text = JSON.stringify(values);
  assert.ok(!text.includes("/Users/"));
  assert.ok(!text.includes(repoRoot));
}

function readJson(filePath) {
  return JSON.parse(fs.readFileSync(filePath, "utf8"));
}

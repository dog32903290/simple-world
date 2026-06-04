const assert = require("node:assert/strict");
const { spawnSync } = require("node:child_process");
const fs = require("node:fs");
const os = require("node:os");
const path = require("node:path");
const test = require("node:test");

const repoRoot = path.resolve(__dirname, "..");
const contractPath = path.join(repoRoot, "docs/runtime/METAL_EXPLICIT_MSL_PROOF.md");
const fixturePath = path.join(repoRoot, "docs/runtime/fixtures/metal_explicit_msl_proof.graph.json");
const scriptPath = path.join(repoRoot, "docs/runtime/scripts/metal_explicit_msl_proof_shell.py");
const artifactDir = path.join(repoRoot, "docs/runtime/artifacts/metal_explicit_msl_proof");

test("Metal explicit MSL proof contract names the narrow proof and nonclaims", () => {
  const source = fs.readFileSync(contractPath, "utf8");

  assert.match(source, /MetalExplicitMslProof answers:/);
  assert.match(source, /explicit MSL source -> real Metal compile -> offscreen render -> RGBA readback stats/);
  assert.match(source, /not renderer integration/);
  assert.match(source, /not NativeDrawShaderCompileProof integration/);
  assert.match(source, /not TiXL\/HLSL translation/);
  assert.match(source, /not PBR parity/);
  assert.match(source, /actualCompilerRan/);
  assert.match(source, /actualMetalRan/);
});

test("Metal explicit MSL proof shell runs real Metal or blocks without fake frame", () => {
  const tmpDir = fs.mkdtempSync(path.join(os.tmpdir(), "metal-explicit-msl-"));
  const run = spawnSync("python3", [scriptPath, fixturePath, tmpDir], {
    cwd: repoRoot,
    encoding: "utf8",
  });

  const result = readArtifact(tmpDir, "metal_explicit_msl_result.json");
  const trace = readArtifact(tmpDir, "metal_explicit_msl_trace.json");
  const errors = readArtifact(tmpDir, "metal_explicit_msl_errors.json");

  assert.ok(run.status === 0 || run.status === 1, run.stderr || run.stdout);
  assert.equal(result.kind, "MetalExplicitMslProof");
  assert.equal(trace[0].fixture, "docs/runtime/fixtures/metal_explicit_msl_proof.graph.json");
  assert.ok(!JSON.stringify(trace).includes("/Users/"));

  if (run.status === 0) {
    const stats = readArtifact(tmpDir, "frame_stats.json");

    assert.equal(result.ok, true);
    assert.equal(result.status, "rendered");
    assert.equal(result.claims.actualCompilerRan, true);
    assert.equal(result.claims.actualMetalRan, true);
    assert.equal(result.claims.rendererIntegration, false);
    assert.equal(result.claims.tixlHlslTranslation, false);
    assert.equal(result.claims.pbrParity, false);
    assert.deepEqual(errors, []);
    assert.equal(stats.width, 8);
    assert.equal(stats.height, 8);
    assert.equal(stats.byteCount, 8 * 8 * 4);
    assert.equal(stats.nonBlack, true);
    assert.equal(stats.varied, true);
    return;
  }

  assert.equal(result.ok, false);
  assert.equal(result.status, "blocked_metal_device_unavailable");
  assert.equal(result.claims.actualCompilerRan, false);
  assert.equal(result.claims.actualMetalRan, false);
  assert.ok(errors.some((error) => /Metal device unavailable/i.test(error.message || "")));
  assert.equal(fs.existsSync(path.join(tmpDir, "frame_stats.json")), false);
});

test("Metal explicit MSL proof reports invalid MSL compiler diagnostics without frame success", () => {
  const tmpDir = fs.mkdtempSync(path.join(os.tmpdir(), "metal-explicit-msl-invalid-"));
  const invalidFixturePath = path.join(tmpDir, "invalid.graph.json");
  const fixture = JSON.parse(fs.readFileSync(fixturePath, "utf8"));
  fixture.graphId = "fixture.metal_explicit_msl_invalid";
  fixture.explicitMslSource = "#include <metal_stdlib>\nusing namespace metal;\nfragment float4 my_world_fragment_broken() { return float4(1.0) }\n";
  fs.writeFileSync(invalidFixturePath, JSON.stringify(fixture, null, 2));

  const run = spawnSync("python3", [scriptPath, invalidFixturePath, tmpDir], {
    cwd: repoRoot,
    encoding: "utf8",
  });

  assert.equal(run.status, 1);
  const result = readArtifact(tmpDir, "metal_explicit_msl_result.json");
  const errors = readArtifact(tmpDir, "metal_explicit_msl_errors.json");

  assert.equal(result.ok, false);
  if (result.status === "blocked_metal_device_unavailable") {
    assert.ok(errors.some((error) => /Metal device unavailable/i.test(error.message || "")));
    return;
  }

  assert.equal(result.status, "compile_failed");
  assert.equal(result.claims.actualCompilerRan, true);
  assert.equal(result.claims.actualMetalRan, false);
  assert.ok(errors.some((error) => error.code === "metal_explicit_msl.compile_failed"));
  assert.equal(fs.existsSync(path.join(tmpDir, "frame_stats.json")), false);
});

test("Metal explicit MSL checked-in artifacts stay path-clean and current", () => {
  const result = readArtifact(artifactDir, "metal_explicit_msl_result.json");
  const trace = readArtifact(artifactDir, "metal_explicit_msl_trace.json");
  const errors = readArtifact(artifactDir, "metal_explicit_msl_errors.json");

  assert.equal(result.kind, "MetalExplicitMslProof");
  assert.ok(result.status === "rendered" || result.status === "blocked_metal_device_unavailable");
  assert.ok(!JSON.stringify({ result, trace, errors }).includes("/Users/"));

  if (result.status === "rendered") {
    const stats = readArtifact(artifactDir, "frame_stats.json");
    assert.equal(result.ok, true);
    assert.equal(result.claims.actualCompilerRan, true);
    assert.equal(result.claims.actualMetalRan, true);
    assert.equal(stats.nonBlack, true);
    assert.equal(stats.varied, true);
    return;
  }

  assert.equal(result.ok, false);
  assert.equal(result.claims.actualCompilerRan, false);
  assert.equal(result.claims.actualMetalRan, false);
  assert.ok(errors.some((error) => /Metal device unavailable/i.test(error.message || "")));
});

function readArtifact(dir, name) {
  return JSON.parse(fs.readFileSync(path.join(dir, name), "utf8"));
}

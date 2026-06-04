const assert = require("node:assert/strict");
const { spawnSync } = require("node:child_process");
const fs = require("node:fs");
const os = require("node:os");
const path = require("node:path");
const test = require("node:test");

const repoRoot = path.resolve(__dirname, "..");
const contractPath = path.join(repoRoot, "docs/runtime/RENDERER_PROOF_CONTRACT.md");
const backendContractPath = path.join(repoRoot, "docs/runtime/RENDERER_BACKEND_CONTRACT.md");
const materialContractPath = path.join(repoRoot, "docs/runtime/MATERIAL_PBR_CONTRACT.md");
const dangerPath = path.join(repoRoot, "docs/runtime/DANGER_NODE_EXPERIMENTS.md");
const scriptPath = path.join(repoRoot, "docs/runtime/scripts/native_render_shell.py");
const commandPath = path.join(repoRoot, "docs/runtime/artifacts/material_pbr_scope/mesh_pbr_draw_command.json");
const artifactDir = path.join(repoRoot, "docs/runtime/artifacts/native_renderer");

test("Renderer proof contract names command-to-frame boundary without claiming GPU parity", () => {
  const source = fs.readFileSync(contractPath, "utf8");
  const backendSource = fs.readFileSync(backendContractPath, "utf8");

  assert.match(source, /RendererProof := MeshPbrDrawCommand -> render trace \+ nonblack frame artifact/);
  assert.match(source, /native_render_shell\.py/);
  assert.match(source, /native_render_trace\.json/);
  assert.match(source, /native_render_errors\.json/);
  assert.match(source, /frame\.ppm/);
  assert.match(source, /This is not a GPU backend, not Metal, not DX11/);
  assert.match(source, /blocked until native GPU renderer:[\s\S]*TiXL MeshBuffers \/ BufferWithViews parity/);
  assert.match(source, /RENDERER_BACKEND_CONTRACT\.md/);
  assert.match(source, /current `softwareProof` backend role/);
  assert.match(backendSource, /softwareProof := deterministic artifact backend/);
});

test("Native render shell consumes MeshPbrDrawCommand and emits nonblack frame proof", () => {
  const run = spawnSync("python3", [scriptPath, commandPath, artifactDir], {
    cwd: repoRoot,
    encoding: "utf8",
  });

  assert.equal(run.status, 0, run.stderr);

  const trace = JSON.parse(fs.readFileSync(path.join(artifactDir, "native_render_trace.json"), "utf8"));
  const errors = JSON.parse(fs.readFileSync(path.join(artifactDir, "native_render_errors.json"), "utf8"));
  const stats = JSON.parse(fs.readFileSync(path.join(artifactDir, "frame_stats.json"), "utf8"));
  const frame = fs.readFileSync(path.join(artifactDir, "frame.ppm"), "utf8");

  assert.deepEqual(trace.map((entry) => entry.op), [
    "loadCommand",
    "validateCommand",
    "bindInputAssembler",
    "bindShaderStage",
    "bindRasterizer",
    "bindOutputMerger",
    "draw",
    "writeFrame",
    "measureFrame",
  ]);
  assert.equal(trace[1].selectedMaterialId, "glass");
  assert.equal(trace[3].shaderSource, "Lib:shaders/3d/mesh/mesh-Draw.hlsl");
  assert.deepEqual(trace[3].constantBuffers, ["transforms", "context", "pointLights", "pbr:glass"]);
  assert.deepEqual(errors, []);
  assert.equal(stats.width, 320);
  assert.equal(stats.height, 180);
  assert.equal(stats.pixelCount, 57600);
  assert.ok(stats.nonblackPixels > 0);
  assert.ok(stats.nonblackPixels < stats.pixelCount);
  assert.ok(stats.brightPixels > 0);
  assert.match(frame, /^P3\n320 180\n255\n/);
});

test("Native render shell rejects incomplete PBR command instead of drawing fake frame", () => {
  const tmpDir = fs.mkdtempSync(path.join(os.tmpdir(), "native-render-bad-"));
  const badCommand = path.join(tmpDir, "bad_command.json");
  fs.writeFileSync(badCommand, JSON.stringify({
    ok: true,
    meshId: "cube",
    selectedMaterialId: "glass",
    shaderSource: "Lib:shaders/3d/mesh/mesh-Draw.hlsl",
    vertexShaderEntry: "vsMain",
    pixelShaderEntry: "psMain",
    constantBuffers: ["transforms", "context", "pointLights"],
    shaderResources: ["DefaultAlbedoColorSrv"],
    commandOps: ["inputAssembler", "shaderStage", "draw"],
  }, null, 2));

  const run = spawnSync("python3", [scriptPath, badCommand, tmpDir], {
    cwd: repoRoot,
    encoding: "utf8",
  });

  assert.equal(run.status, 1);

  const errors = JSON.parse(fs.readFileSync(path.join(tmpDir, "native_render_errors.json"), "utf8"));
  const stats = JSON.parse(fs.readFileSync(path.join(tmpDir, "frame_stats.json"), "utf8"));
  const frame = fs.readFileSync(path.join(tmpDir, "frame.ppm"), "utf8");

  assert.deepEqual(errors.map((error) => error.code), [
    "native_render.invalid_command_ops",
    "native_render.incomplete_pbr_binding",
    "native_render.incomplete_pbr_binding",
  ]);
  assert.deepEqual(stats, {});
  assert.equal(frame, "");
});

test("Material and danger docs point to renderer proof artifacts", () => {
  const materialSource = fs.readFileSync(materialContractPath, "utf8");
  const dangerSource = fs.readFileSync(dangerPath, "utf8");

  assert.match(materialSource, /RENDERER_PROOF_CONTRACT\.md/);
  assert.match(materialSource, /native_render_shell\.py/);
  assert.match(dangerSource, /RENDERER_PROOF_CONTRACT\.md/);
  assert.match(dangerSource, /tests\/native_render_contract\.test\.js/);
});

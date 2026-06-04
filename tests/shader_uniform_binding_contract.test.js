const assert = require("node:assert/strict");
const { spawnSync } = require("node:child_process");
const fs = require("node:fs");
const os = require("node:os");
const path = require("node:path");
const test = require("node:test");

const repoRoot = path.resolve(__dirname, "..");
const contractPath = path.join(repoRoot, "docs/runtime/SHADER_UNIFORM_BINDING_CONTRACT.md");
const fixturePath = path.join(repoRoot, "docs/runtime/fixtures/shader_uniform_binding.graph.json");
const scriptPath = path.join(repoRoot, "docs/runtime/scripts/shader_uniform_binding_shell.py");
const artifactDir = path.join(repoRoot, "docs/runtime/artifacts/shader_uniform_binding");

test("ShaderUniformBinding contract names live value to uniform and frame input boundaries", () => {
  const source = fs.readFileSync(contractPath, "utf8");

  assert.match(source, /ShaderUniformBinding answers:/);
  assert.match(source, /which live control value is allowed to become which shader uniform/);
  assert.match(source, /ValueSource -> ShaderUniformBinding -> RenderFrameInput -> NativeRendererBackend/);
  assert.match(source, /ShaderPreviewInputBridge/);
  assert.match(source, /bindingId is required/);
  assert.match(source, /uniformName is required/);
  assert.match(source, /u_loudness -> RenderFrameInput\.loudness/);
  assert.match(source, /Other uniforms remain shader bindings/);
});

test("ShaderUniformBinding fixture carries donor evidence and a u_loudness binding", () => {
  const graph = JSON.parse(fs.readFileSync(fixturePath, "utf8"));
  const uniform = graph.nodes.find((node) => node.id === "n_loudness_uniform");

  assert.equal(graph.graphId, "fixture.shader_uniform_binding");
  assert.ok(graph.metadata.donorEvidence.some((entry) => entry.endsWith("ShaderPreviewInputBridge.h")));
  assert.equal(graph.targetProgram.programId, "program.sphere_sdf_raymarch.fragment");
  assert.equal(uniform.params.bindingId, "uniform.loudness");
  assert.equal(uniform.params.uniformName, "u_loudness");
  assert.equal(graph.frame.frameIndex, 42);
});

test("ShaderUniformBinding shell emits uniform snapshot, bindings, frame input, and trace", () => {
  const run = spawnSync("python3", [scriptPath, fixturePath, artifactDir], {
    cwd: repoRoot,
    encoding: "utf8",
  });

  assert.equal(run.status, 0, run.stderr || run.stdout);

  const snapshot = readArtifact("shader_uniform_snapshot.json");
  const bindings = readArtifact("shader_uniform_bindings.json");
  const frameInput = readArtifact("render_frame_input.json");
  const trace = readArtifact("uniform_binding_trace.json");
  const errors = readArtifact("shader_uniform_binding_errors.json");

  assert.deepEqual(errors, []);
  assert.equal(snapshot.kind, "shaderUniformBindingSnapshot");
  assert.equal(snapshot.ok, true);
  assert.equal(snapshot.status, "ready");
  assert.equal(snapshot.sampleCounter, 4096);
  assert.deepEqual(snapshot.uniforms[0], {
    bindingId: "uniform.loudness",
    uniformName: "u_loudness",
    value: 0.37,
    sampleCounter: 4096,
    sourceNodeId: "n_loudness_value",
    sourcePort: "value",
    targetProgramId: "program.sphere_sdf_raymarch.fragment",
  });
  assert.equal(bindings.uniforms.length, 1);
  assert.equal(frameInput.kind, "RenderFrameInput");
  assert.equal(frameInput.loudness, 0.37);
  assert.equal(frameInput.frameIndex, 42);
  assert.deepEqual(trace.map((entry) => entry.op), [
    "loadShaderUniformBindingGraph",
    "constantFloat",
    "bindUniformFloat",
    "publishShaderUniformBindingArtifacts",
  ]);
});

test("ShaderUniformBinding shell refuses missing uniform names and keeps fallback frame input", () => {
  const tmpDir = fs.mkdtempSync(path.join(os.tmpdir(), "shader-uniform-bad-"));
  const badPath = path.join(tmpDir, "bad.graph.json");
  const graph = JSON.parse(fs.readFileSync(fixturePath, "utf8"));
  graph.nodes.find((node) => node.id === "n_loudness_uniform").params.uniformName = "";
  graph.frame.fallbackLoudness = 0.12;
  fs.writeFileSync(badPath, JSON.stringify(graph, null, 2));

  const run = spawnSync("python3", [scriptPath, badPath, tmpDir], {
    cwd: repoRoot,
    encoding: "utf8",
  });

  assert.equal(run.status, 1);
  const snapshot = JSON.parse(fs.readFileSync(path.join(tmpDir, "shader_uniform_snapshot.json"), "utf8"));
  const frameInput = JSON.parse(fs.readFileSync(path.join(tmpDir, "render_frame_input.json"), "utf8"));
  const errors = JSON.parse(fs.readFileSync(path.join(tmpDir, "shader_uniform_binding_errors.json"), "utf8"));

  assert.equal(snapshot.ok, false);
  assert.equal(errors[0].code, "shader_uniform_binding.missing_uniform_name");
  assert.equal(frameInput.loudness, 0.12);
  assert.equal(frameInput.sourceSnapshotOk, false);
});

function readArtifact(name) {
  return JSON.parse(fs.readFileSync(path.join(artifactDir, name), "utf8"));
}

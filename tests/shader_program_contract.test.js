const assert = require("node:assert/strict");
const { spawnSync } = require("node:child_process");
const fs = require("node:fs");
const os = require("node:os");
const path = require("node:path");
const test = require("node:test");

const repoRoot = path.resolve(__dirname, "..");
const contractPath = path.join(repoRoot, "docs/runtime/SHADER_PROGRAM_CONTRACT.md");
const fixturePath = path.join(repoRoot, "docs/runtime/fixtures/shader_program_contract.graph.json");
const scriptPath = path.join(repoRoot, "docs/runtime/scripts/shader_program_shell.py");
const artifactDir = path.join(repoRoot, "docs/runtime/artifacts/shader_program");

test("ShaderProgram contract names source, stage, binding, backend, and failure boundaries", () => {
  const source = fs.readFileSync(contractPath, "utf8");

  assert.match(source, /ShaderProgram answers:/);
  assert.match(source, /which generated shader source, stages, entry symbols, and bindings may a renderer backend compile/);
  assert.match(source, /ShaderGraph -> ShaderProgram -> RenderGraph -> RendererBackend/);
  assert.match(source, /sourceHash/);
  assert.match(source, /compileBackend/);
  assert.match(source, /lastValidPolicy/);
  assert.match(source, /do not replace the live program with an invalid program/);
  assert.match(source, /existing WebGL2 probe/);
});

test("ShaderProgram fixture packages the SphereSDF -> RaymarchField shader proof", () => {
  const graph = JSON.parse(fs.readFileSync(fixturePath, "utf8"));
  const program = graph.shaderProgram;

  assert.equal(graph.graphId, "fixture.shader_program_contract");
  assert.equal(program.programId, "program.sphere_sdf_raymarch.fragment");
  assert.equal(program.sourceGraph, "docs/runtime/fixtures/sphere_sdf_raymarch.graph.json");
  assert.equal(program.language, "GLSL_ES_300");
  assert.equal(program.compileBackend, "webgl2ShaderProbe");
  assert.deepEqual(program.stages, [{ stage: "fragment", entryPoint: "raymarch_field_1" }]);
  assert.deepEqual(program.entrySymbols, ["sdSphere", "myworld_field", "raymarch_field_1"]);
  assert.equal(program.lastValidPolicy.replaceLiveProgram, false);
});

test("ShaderProgram shell emits package, binding, compile request, and last-valid artifacts", () => {
  const run = spawnSync("python3", [scriptPath, fixturePath, artifactDir], {
    cwd: repoRoot,
    encoding: "utf8",
  });

  assert.equal(run.status, 0, run.stderr || run.stdout);

  const shader = fs.readFileSync(path.join(artifactDir, "shader_source.glsl"), "utf8");
  const pkg = readArtifact("shader_program_package.json");
  const bindings = readArtifact("shader_program_bindings.json");
  const compileRequest = readArtifact("shader_program_compile_request.json");
  const lastValid = readArtifact("shader_program_last_valid_policy.json");
  const errors = readArtifact("shader_program_errors.json");

  assert.deepEqual(errors, []);
  assert.match(shader, /float sdSphere\(/);
  assert.match(shader, /MyWorldField myworld_field/);
  assert.match(shader, /float raymarch_field_1\(/);
  assert.equal(pkg.status, "ok");
  assert.equal(pkg.language, "GLSL_ES_300");
  assert.equal(pkg.compileBackend, "webgl2ShaderProbe");
  assert.match(pkg.sourceHash, /^[a-f0-9]{64}$/);
  assert.deepEqual(bindings.uniforms, []);
  assert.deepEqual(bindings.textures, []);
  assert.equal(compileRequest.actualCompileProof, "docs/runtime/scripts/check_shader_webgl.js");
  assert.equal(lastValid.onCompileError, "keepLastValidProgram");
  assert.equal(lastValid.replaceLiveProgram, false);
});

test("ShaderProgram shell refuses missing entry symbols instead of fabricating program success", () => {
  const tmpDir = fs.mkdtempSync(path.join(os.tmpdir(), "shader-program-bad-"));
  const badPath = path.join(tmpDir, "bad.graph.json");
  const graph = JSON.parse(fs.readFileSync(fixturePath, "utf8"));
  graph.shaderProgram.entrySymbols.push("missing_program_symbol");
  fs.writeFileSync(badPath, JSON.stringify(graph, null, 2));

  const run = spawnSync("python3", [scriptPath, badPath, tmpDir], {
    cwd: repoRoot,
    encoding: "utf8",
  });

  assert.equal(run.status, 1);
  const pkg = JSON.parse(fs.readFileSync(path.join(tmpDir, "shader_program_package.json"), "utf8"));
  const errors = JSON.parse(fs.readFileSync(path.join(tmpDir, "shader_program_errors.json"), "utf8"));
  assert.equal(pkg.status, "error");
  assert.equal(errors[0].code, "shader_program.missing_entry_symbol");
  assert.equal(errors[0].symbol, "missing_program_symbol");
});

function readArtifact(name) {
  return JSON.parse(fs.readFileSync(path.join(artifactDir, name), "utf8"));
}

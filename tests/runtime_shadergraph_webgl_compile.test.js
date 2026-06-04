const assert = require("node:assert/strict");
const fs = require("node:fs");
const os = require("node:os");
const path = require("node:path");
const test = require("node:test");
const { spawnSync } = require("node:child_process");

const repoRoot = path.resolve(__dirname, "..");
const shellCompiler = path.join(repoRoot, "docs/runtime/scripts/compile_shadergraph_shell.py");
const webglCheck = path.join(repoRoot, "docs/runtime/scripts/check_shader_webgl.js");
const fixture = path.join(repoRoot, "docs/runtime/fixtures/sphere_sdf_raymarch.graph.json");

test("generated ShaderGraph shell source compiles as a WebGL2 fragment shader", () => {
  const outDir = fs.mkdtempSync(path.join(os.tmpdir(), "myworld-webgl-check-"));
  const shell = spawnSync("python3", [shellCompiler, fixture, outDir], {
    cwd: repoRoot,
    encoding: "utf8",
  });
  assert.equal(shell.status, 0, shell.stderr || shell.stdout);

  const check = spawnSync("node", [
    webglCheck,
    path.join(outDir, "shader_source.glsl"),
    outDir,
  ], {
    cwd: repoRoot,
    encoding: "utf8",
  });

  assert.equal(check.status, 0, check.stderr || check.stdout);

  const result = JSON.parse(fs.readFileSync(path.join(outDir, "webgl_compile.json"), "utf8"));
  assert.equal(result.ok, true, result.fragmentLog);
  assert.equal(result.backend, "webgl2");
  assert.match(result.fragmentSource, /raymarch_field_1/);
});

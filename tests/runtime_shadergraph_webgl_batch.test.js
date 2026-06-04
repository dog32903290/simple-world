const assert = require("node:assert/strict");
const fs = require("node:fs");
const os = require("node:os");
const path = require("node:path");
const test = require("node:test");
const { spawnSync } = require("node:child_process");

const repoRoot = path.resolve(__dirname, "..");
const shellCompiler = path.join(repoRoot, "docs/runtime/scripts/compile_shadergraph_shell.py");
const batchCheck = path.join(repoRoot, "docs/runtime/scripts/check_shader_webgl_batch.js");
const fixture = path.join(repoRoot, "docs/runtime/fixtures/sphere_sdf_raymarch.graph.json");

test("reuses one WebGL2 context so warm shader compiles stay under one second", () => {
  const outDir = fs.mkdtempSync(path.join(os.tmpdir(), "myworld-webgl-batch-"));
  const shell = spawnSync("python3", [shellCompiler, fixture, outDir], {
    cwd: repoRoot,
    encoding: "utf8",
  });
  assert.equal(shell.status, 0, shell.stderr || shell.stdout);

  const check = spawnSync("node", [
    batchCheck,
    path.join(outDir, "shader_source.glsl"),
    outDir,
  ], {
    cwd: repoRoot,
    encoding: "utf8",
    timeout: 30000,
  });

  assert.equal(check.status, 0, check.stderr || check.stdout);

  const result = JSON.parse(fs.readFileSync(path.join(outDir, "webgl_compile_batch.json"), "utf8"));
  assert.equal(result.ok, true, JSON.stringify(result, null, 2));
  assert.equal(result.runs.length, 2);
  assert.equal(result.runs[0].ok, true);
  assert.equal(result.runs[1].ok, true);
  assert.ok(result.runs[1].durationMs < 1000, `warm compile took ${result.runs[1].durationMs}ms`);
});

const assert = require("node:assert/strict");
const fs = require("node:fs");
const os = require("node:os");
const path = require("node:path");
const test = require("node:test");
const { spawnSync } = require("node:child_process");

const repoRoot = path.resolve(__dirname, "..");
const converter = path.join(repoRoot, "docs/runtime/scripts/convert_vuo_mesh_shell.py");
const fixture = path.join(repoRoot, "docs/runtime/fixtures/vuo_triangle_mesh.snapshot.json");

test("converts a Vuo-like CPU mesh snapshot into a My World Mesh contract", () => {
  const outDir = fs.mkdtempSync(path.join(os.tmpdir(), "myworld-vuo-mesh-"));
  const result = spawnSync("python3", [converter, fixture, outDir], {
    cwd: repoRoot,
    encoding: "utf8",
  });

  assert.equal(result.status, 0, result.stderr || result.stdout);

  const mesh = JSON.parse(fs.readFileSync(path.join(outDir, "mesh_contract.json"), "utf8"));
  const stats = JSON.parse(fs.readFileSync(path.join(outDir, "mesh_stats.json"), "utf8"));
  const errors = JSON.parse(fs.readFileSync(path.join(outDir, "errors.json"), "utf8"));

  assert.deepEqual(errors, []);
  assert.equal(mesh.type, "Mesh");
  assert.equal(mesh.topology, "triangles");
  assert.equal(mesh.source.host, "vuo");
  assert.equal(mesh.ownership.cpu, "copied");
  assert.deepEqual(mesh.attributes.position.semantic, "position");
  assert.deepEqual(mesh.indices, [0, 1, 2]);
  assert.deepEqual(stats.bounds.min, [-1, -1, 0]);
  assert.deepEqual(stats.bounds.max, [1, 1, 0]);
  assert.deepEqual(stats.attributes, ["position", "normal", "uv", "color"]);
});

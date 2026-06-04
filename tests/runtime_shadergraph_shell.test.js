const assert = require("node:assert/strict");
const fs = require("node:fs");
const os = require("node:os");
const path = require("node:path");
const test = require("node:test");
const { spawnSync } = require("node:child_process");

const repoRoot = path.resolve(__dirname, "..");
const compiler = path.join(repoRoot, "docs/runtime/scripts/compile_shadergraph_shell.py");
const fixture = path.join(repoRoot, "docs/runtime/fixtures/sphere_sdf_raymarch.graph.json");

test("compiles SphereSDF -> RaymarchField into inspectable shader artifacts", () => {
  const outDir = fs.mkdtempSync(path.join(os.tmpdir(), "myworld-shadergraph-shell-"));
  const result = spawnSync("python3", [compiler, fixture, outDir], {
    cwd: repoRoot,
    encoding: "utf8",
  });

  assert.equal(result.status, 0, result.stderr || result.stdout);

  const shader = fs.readFileSync(path.join(outDir, "shader_source.glsl"), "utf8");
  const errors = JSON.parse(fs.readFileSync(path.join(outDir, "errors.json"), "utf8"));
  const cookOrder = JSON.parse(fs.readFileSync(path.join(outDir, "cook_order.json"), "utf8"));

  assert.deepEqual(errors, []);
  assert.deepEqual(cookOrder.map((entry) => entry.node_id), ["sphere_sdf_1", "raymarch_field_1"]);
  assert.match(shader, /float sdSphere\(/);
  assert.match(shader, /MyWorldField myworld_field/);
  assert.match(shader, /raymarch_field_1/);
  assert.match(shader, /sdSphere\(p, vec3\(0\.0, 0\.0, 0\.0\), 0\.5\)/);
});

test("fixture records TiXL naming, category, and primary type colors", () => {
  const graph = JSON.parse(fs.readFileSync(fixture, "utf8"));
  const sphere = graph.nodes.find((node) => node.id === "sphere_sdf_1");
  const raymarch = graph.nodes.find((node) => node.id === "raymarch_field_1");

  assert.equal(sphere.title, "my_SphereSDF");
  assert.equal(sphere.sourceEvidence.tixlCategoryPath, "Operators/Lib/field/generate/sdf");
  assert.deepEqual(sphere.sourceEvidence.primaryOutput, { name: "Result", type: "ShaderGraphNode" });
  assert.deepEqual(sphere.sourceEvidence.typeColor, { name: "ColorForShaderGraph", hex: "#D142B3" });

  assert.equal(raymarch.title, "my_RaymarchField");
  assert.equal(raymarch.sourceEvidence.tixlCategoryPath, "Operators/Lib/field/render");
  assert.deepEqual(raymarch.sourceEvidence.primaryOutput, { name: "DrawCommand", type: "Command" });
  assert.deepEqual(raymarch.sourceEvidence.typeColor, { name: "ColorForCommands", hex: "#22B8C2" });
});

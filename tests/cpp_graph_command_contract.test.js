const assert = require("node:assert/strict");
const fs = require("node:fs");
const os = require("node:os");
const path = require("node:path");
const { spawnSync } = require("node:child_process");
const test = require("node:test");

const repoRoot = path.resolve(__dirname, "..");
const scriptPath = path.join(repoRoot, "docs/runtime/scripts/cpp_graph_command_contract_shell.py");

function readJson(filePath) {
  return JSON.parse(fs.readFileSync(filePath, "utf8"));
}

function runFixture(fixtureName) {
  const fixturePath = path.join(repoRoot, "docs/runtime/fixtures", fixtureName);
  const tmpDir = fs.mkdtempSync(path.join(os.tmpdir(), "cpp-graph-command-contract-"));
  const run = spawnSync("python3", [scriptPath, fixturePath, tmpDir], {
    cwd: repoRoot,
    encoding: "utf8"
  });
  return { run, tmpDir };
}

test("C++ graph command contract creates connects edits validates and builds runtime graph", () => {
  const { run, tmpDir } = runFixture("cpp_graph_command_contract.graph.json");
  assert.equal(run.status, 0, run.stderr || run.stdout);

  const result = readJson(path.join(tmpDir, "cpp_graph_command_contract_result.json"));
  const document = readJson(path.join(tmpDir, "graph_document.json"));
  const runtimeGraph = readJson(path.join(tmpDir, "runtime_graph.json"));
  const diagnostics = readJson(path.join(tmpDir, "diagnostics.json"));

  assert.equal(result.status, "passed");
  assert.equal(result.claims.cppCommandDispatcher, true);
  assert.equal(result.claims.runtimeDirty, true);
  assert.deepEqual(diagnostics, []);
  assert.equal(document.nodes.length, 2);
  assert.equal(document.edges.length, 1);
  assert.equal(document.edges[0].from.nodeId, "sphere_sdf_1");
  assert.equal(document.edges[0].to.nodeId, "raymarch_field_1");
  assert.deepEqual(runtimeGraph.cookOrder, ["sphere_sdf_1", "raymarch_field_1"]);
});

test("C++ graph command contract rejects invalid cable type without creating edge", () => {
  const { run, tmpDir } = runFixture("cpp_graph_command_contract_bad_type.graph.json");
  assert.equal(run.status, 0, run.stderr || run.stdout);

  const result = readJson(path.join(tmpDir, "cpp_graph_command_contract_result.json"));
  const document = readJson(path.join(tmpDir, "graph_document.json"));
  const diagnostics = readJson(path.join(tmpDir, "diagnostics.json"));

  assert.equal(result.status, "diagnosed");
  assert.equal(document.edges.length, 0);
  assert.equal(diagnostics[0].code, "graph.edge.type_mismatch");
});

test("C++ graph command contract save reload preserves graph and drops unsafe UI state", () => {
  const { run, tmpDir } = runFixture("cpp_graph_command_contract_save_reload.graph.json");
  assert.equal(run.status, 0, run.stderr || run.stdout);

  const reloaded = readJson(path.join(tmpDir, "reloaded_graph_document.json"));
  assert.equal(reloaded.nodes.length, 2);
  assert.equal(reloaded.edges.length, 1);
  assert.equal(reloaded.nodes.find((node) => node.id === "sphere_sdf_1").params.radius, 0.33);
  assert.equal(Object.prototype.hasOwnProperty.call(reloaded, "selectedNodeIds"), false);
  assert.equal(Object.prototype.hasOwnProperty.call(reloaded, "cableDrag"), false);
});

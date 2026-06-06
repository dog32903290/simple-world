const assert = require("node:assert/strict");
const fs = require("node:fs");
const os = require("node:os");
const path = require("node:path");
const { spawnSync } = require("node:child_process");
const test = require("node:test");

const repoRoot = path.resolve(__dirname, "..");
const scriptPath = path.join(repoRoot, "docs/runtime/scripts/cpp_graph_command_contract_shell.py");
const {
  replayInteractionCommands,
} = require("../docs/runtime/scripts/graph_interaction_contract.js");

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

function replayJsFixture(fixtureName) {
  const fixture = readJson(path.join(repoRoot, "docs/runtime/fixtures", fixtureName));
  return replayInteractionCommands(fixture);
}

function sortedById(items) {
  return [...items].sort((a, b) => a.id.localeCompare(b.id));
}

function normalizeNodes(document) {
  return sortedById(document.nodes).map((node) => ({
    id: node.id,
    type: node.type,
    position: node.position,
  }));
}

function normalizeEdgeEndpoints(document) {
  return [...document.edges]
    .map((edge) => ({ from: edge.from, to: edge.to }))
    .sort((a, b) => {
      const left = `${a.from.nodeId}.${a.from.port}->${a.to.nodeId}.${a.to.port}`;
      const right = `${b.from.nodeId}.${b.from.port}->${b.to.nodeId}.${b.to.port}`;
      return left.localeCompare(right);
    });
}

function diagnosticCodes(diagnostics) {
  return diagnostics.map((diagnostic) => diagnostic.code).sort();
}

function nodeById(document, nodeId) {
  const node = document.nodes.find((candidate) => candidate.id === nodeId);
  assert.ok(node, `missing node ${nodeId}`);
  return node;
}

function assertParamsMatchJsReference({ jsDocument, cppDocument }) {
  for (const cppNode of cppDocument.nodes) {
    const jsNode = nodeById(jsDocument, cppNode.id);
    assert.deepEqual(cppNode.params, jsNode.params, `params for ${cppNode.id} differ from JS reference`);
  }
}

function assertNoUnsafeUiState(value, unsafeKeys, pathLabel = "$") {
  if (Array.isArray(value)) {
    value.forEach((item, index) => assertNoUnsafeUiState(item, unsafeKeys, `${pathLabel}[${index}]`));
    return;
  }

  if (!value || typeof value !== "object") {
    return;
  }

  for (const key of Object.keys(value)) {
    assert.equal(unsafeKeys.has(key), false, `unsafe UI state key ${key} leaked at ${pathLabel}`);
    assertNoUnsafeUiState(value[key], unsafeKeys, `${pathLabel}.${key}`);
  }
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

test("valid graph command fixture matches JS reference contract on graph and runtime semantics", () => {
  const fixtureName = "cpp_graph_command_contract.graph.json";
  const jsReference = replayJsFixture(fixtureName);
  const { run, tmpDir } = runFixture(fixtureName);
  assert.equal(run.status, 0, run.stderr || run.stdout);

  const cppResult = readJson(path.join(tmpDir, "cpp_graph_command_contract_result.json"));
  const cppDocument = readJson(path.join(tmpDir, "graph_document.json"));
  const cppRuntimeGraph = readJson(path.join(tmpDir, "runtime_graph.json"));
  const cppDiagnostics = readJson(path.join(tmpDir, "diagnostics.json"));

  assert.deepEqual(normalizeNodes(cppDocument), normalizeNodes(jsReference.graphDocument));
  assertParamsMatchJsReference({
    jsDocument: jsReference.graphDocument,
    cppDocument,
  });
  assert.deepEqual(normalizeEdgeEndpoints(cppDocument), normalizeEdgeEndpoints(jsReference.graphDocument));
  assert.deepEqual(diagnosticCodes(cppDiagnostics), diagnosticCodes(jsReference.diagnostics));
  assert.equal(cppResult.claims.runtimeDirty, jsReference.state.runtimeDirty);
  assert.deepEqual(cppRuntimeGraph.cookOrder, jsReference.runtimeGraph.cookOrder);

  assertNoUnsafeUiState(jsReference.graphDocument, new Set(["selectedNodeIds", "cableDrag"]));
  assertNoUnsafeUiState(cppDocument, new Set(["selectedNodeIds", "cableDrag"]));
});

test("C++ graph command contract rejects invalid cable type without creating edge", () => {
  const { run, tmpDir } = runFixture("cpp_graph_command_contract_bad_type.graph.json");
  assert.equal(run.status, 0, run.stderr || run.stdout);

  const result = readJson(path.join(tmpDir, "cpp_graph_command_contract_result.json"));
  const document = readJson(path.join(tmpDir, "graph_document.json"));
  const diagnostics = readJson(path.join(tmpDir, "diagnostics.json"));

  assert.equal(result.status, "diagnosed");
  assert.equal(document.edges.length, 0);
  assert.deepEqual(diagnostics.map((diagnostic) => diagnostic.code), ["graph.edge.type_mismatch"]);
});

test("C++ graph command contract save reload preserves graph and drops unsafe UI state", () => {
  const fixtureName = "cpp_graph_command_contract_save_reload.graph.json";
  const fixture = readJson(path.join(repoRoot, "docs/runtime/fixtures", fixtureName));
  const { run, tmpDir } = runFixture(fixtureName);
  assert.equal(run.status, 0, run.stderr || run.stdout);

  const reloaded = readJson(path.join(tmpDir, "reloaded_graph_document.json"));
  assert.equal(reloaded.nodes.length, 2);
  assert.equal(reloaded.edges.length, 1);
  assert.equal(reloaded.nodes.find((node) => node.id === "sphere_sdf_1").params.radius, 0.33);
  assertNoUnsafeUiState(reloaded, new Set([
    ...fixture.assertReloadDrops,
    "selection",
    "nodeIds",
    "interaction"
  ]));
});

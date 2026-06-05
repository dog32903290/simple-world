const assert = require("node:assert/strict");
const { spawnSync } = require("node:child_process");
const fs = require("node:fs");
const os = require("node:os");
const path = require("node:path");
const test = require("node:test");

const repoRoot = path.resolve(__dirname, "..");
const contractPath = path.join(repoRoot, "docs/runtime/NATIVE_RUNTIME_GRAPH_INCREMENTAL_BUILDER_PROOF.md");
const fixturePath = path.join(repoRoot, "docs/runtime/fixtures/native_runtime_graph_incremental_builder.graph.json");
const scriptPath = path.join(repoRoot, "docs/runtime/scripts/native_runtime_graph_incremental_builder_shell.py");
const artifactDir = path.join(repoRoot, "docs/runtime/artifacts/native_runtime_graph_incremental_builder");

test("NativeRuntimeGraphIncrementalBuilder contract names command replay structural hash and executable reuse", () => {
  const source = fs.readFileSync(contractPath, "utf8");

  assert.match(source, /NativeRuntimeGraphIncrementalBuilderProof answers:/);
  assert.match(source, /command replay -> runtimeGraph diff -> executable node reuse -> cook order artifact/);
  assert.match(source, /runtimeGraph is built from commandGraph replay/);
  assert.match(source, /not a general optimizer/);
});

test("NativeRuntimeGraphIncrementalBuilder fixture declares initial graph and live topology edit", () => {
  const graph = JSON.parse(fs.readFileSync(fixturePath, "utf8"));

  assert.equal(graph.graphId, "fixture.native_runtime_graph_incremental_builder");
  assert.deepEqual(graph.expected.initialCookOrder, ["gradient_1", "blend_1", "output_1"]);
  assert.deepEqual(graph.expected.rebuiltCookOrder, ["gradient_1", "blob_1", "blend_1", "output_1"]);
  assert.deepEqual(graph.liveCommands.map((entry) => entry.command.op), ["createNode", "setParam", "connect"]);
});

test("NativeRuntimeGraphIncrementalBuilder shell emits incremental rebuild artifacts", () => {
  const tmpDir = fs.mkdtempSync(path.join(os.tmpdir(), "native-runtime-graph-incremental-"));
  const run = spawnSync("python3", [scriptPath, fixturePath, tmpDir], {
    cwd: repoRoot,
    encoding: "utf8",
  });

  assert.equal(run.status, 0, run.stderr || run.stdout);

  const result = readArtifact(tmpDir, "native_runtime_graph_incremental_builder_result.json");
  const initialGraph = readArtifact(tmpDir, "runtime_graph_initial.json");
  const rebuiltGraph = readArtifact(tmpDir, "runtime_graph_rebuilt.json");
  const diff = readArtifact(tmpDir, "runtime_graph_diff.json");
  const executableReuse = readArtifact(tmpDir, "executable_reuse_ledger.json");
  const commandLog = readArtifact(tmpDir, "command_log.json");
  const errors = readArtifact(tmpDir, "native_runtime_graph_incremental_builder_errors.json");

  assert.deepEqual(errors, []);
  assert.equal(result.kind, "NativeRuntimeGraphIncrementalBuilderProof");
  assert.equal(result.ok, true);
  assert.equal(result.claims.builtFromCommandReplay, true);
  assert.equal(result.claims.incrementalRebuild, true);
  assert.equal(result.claims.unaffectedExecutableReused, true);
  assert.equal(result.claims.cookOrderRecomputed, true);

  assert.deepEqual(initialGraph.cookOrder, ["gradient_1", "blend_1", "output_1"]);
  assert.deepEqual(rebuiltGraph.cookOrder, ["gradient_1", "blob_1", "blend_1", "output_1"]);
  assert.equal(rebuiltGraph.nodes.find((node) => node.id === "blob_1").type, "image.generate.blob");
  assert.equal(diff.addedNodes[0], "blob_1");
  assert.deepEqual(diff.affectedNodes, ["blob_1", "blend_1", "output_1"]);
  assert.deepEqual(executableReuse.reusedNodes, ["gradient_1"]);
  assert.deepEqual(executableReuse.rebuiltNodes, ["blob_1", "blend_1", "output_1"]);
  assert.ok(executableReuse.entries.find((entry) => entry.nodeId === "gradient_1").structuralHashBefore === executableReuse.entries.find((entry) => entry.nodeId === "gradient_1").structuralHashAfter);
  assert.deepEqual(commandLog.map((entry) => entry.source), ["fixture", "fixture", "fixture", "fixture", "live.importer", "live.importer", "live.importer"]);
  assert.ok(!JSON.stringify({ result, initialGraph, rebuiltGraph, diff, executableReuse, commandLog }).includes("/Users/"));
});

test("NativeRuntimeGraphIncrementalBuilder refuses runtimeGraph direct patching", () => {
  const tmpDir = fs.mkdtempSync(path.join(os.tmpdir(), "native-runtime-graph-incremental-bad-"));
  const badFixture = path.join(tmpDir, "bad.graph.json");
  const graph = JSON.parse(fs.readFileSync(fixturePath, "utf8"));
  graph.graphId = "fixture.native_runtime_graph_incremental_builder.bad";
  graph.liveCommands[0] = {
    source: "runtimeGraph.patch",
    kind: "directRuntimeGraphPatch",
    patch: { addNode: { id: "blob_1", type: "image.generate.blob" } }
  };
  fs.writeFileSync(badFixture, JSON.stringify(graph, null, 2));

  const run = spawnSync("python3", [scriptPath, badFixture, tmpDir], {
    cwd: repoRoot,
    encoding: "utf8",
  });

  assert.equal(run.status, 1);
  const result = readArtifact(tmpDir, "native_runtime_graph_incremental_builder_result.json");
  const errors = readArtifact(tmpDir, "native_runtime_graph_incremental_builder_errors.json");

  assert.equal(result.ok, false);
  assert.equal(result.status, "runtime_graph_command_contract_failed");
  assert.equal(errors[0].code, "native_runtime_graph.direct_runtime_graph_patch");
});

test("NativeRuntimeGraphIncrementalBuilder checked-in artifacts are path-clean and current", () => {
  const run = spawnSync("python3", [scriptPath, fixturePath, artifactDir], {
    cwd: repoRoot,
    encoding: "utf8",
  });

  assert.equal(run.status, 0, run.stderr || run.stdout);

  const result = readArtifact(artifactDir, "native_runtime_graph_incremental_builder_result.json");
  const diff = readArtifact(artifactDir, "runtime_graph_diff.json");
  const executableReuse = readArtifact(artifactDir, "executable_reuse_ledger.json");

  assert.equal(result.ok, true);
  assert.deepEqual(diff.affectedNodes, ["blob_1", "blend_1", "output_1"]);
  assert.deepEqual(executableReuse.reusedNodes, ["gradient_1"]);
  assert.ok(!JSON.stringify({ result, diff, executableReuse }).includes("/Users/"));
});

function readArtifact(dir, name) {
  return JSON.parse(fs.readFileSync(path.join(dir, name), "utf8"));
}

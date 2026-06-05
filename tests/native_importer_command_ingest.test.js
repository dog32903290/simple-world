const assert = require("node:assert/strict");
const { spawnSync } = require("node:child_process");
const fs = require("node:fs");
const os = require("node:os");
const path = require("node:path");
const test = require("node:test");

const repoRoot = path.resolve(__dirname, "..");
const contractPath = path.join(repoRoot, "docs/runtime/NATIVE_IMPORTER_COMMAND_INGEST_PROOF.md");
const fixturePath = path.join(repoRoot, "docs/runtime/fixtures/native_importer_command_ingest.graph.json");
const scriptPath = path.join(repoRoot, "docs/runtime/scripts/native_importer_command_ingest_shell.py");
const artifactDir = path.join(repoRoot, "docs/runtime/artifacts/native_importer_command_ingest");

test("NativeImporterCommandIngest contract makes importer a command producer only", () => {
  const source = fs.readFileSync(contractPath, "utf8");

  assert.match(source, /NativeImporterCommandIngestProof answers:/);
  assert.match(source, /external document -> import command stream -> replayed editorGraph -> runtimeGraph/);
  assert.match(source, /direct imported graph mutation is forbidden/);
  assert.match(source, /not a full file-format importer/);
});

test("NativeImporterCommandIngest fixture keeps external document separate from command replay", () => {
  const graph = JSON.parse(fs.readFileSync(fixturePath, "utf8"));

  assert.equal(graph.graphId, "fixture.native_importer_command_ingest");
  assert.equal(graph.importer.mode, "commandIngest");
  assert.equal(graph.importer.externalDocument.kind, "simple_world.import.v1");
  assert.equal(graph.importer.externalDocument.nodes.length, 3);
  assert.deepEqual(graph.expected.runtimeCookOrder, ["gradient_1", "blob_1", "blend_1", "output_1"]);
});

test("NativeImporterCommandIngest shell emits import commands runtimeGraph and artifacts", () => {
  const tmpDir = fs.mkdtempSync(path.join(os.tmpdir(), "native-importer-command-ingest-"));
  const run = spawnSync("python3", [scriptPath, fixturePath, tmpDir], {
    cwd: repoRoot,
    encoding: "utf8",
  });

  assert.equal(run.status, 0, run.stderr || run.stdout);

  const result = readArtifact(tmpDir, "native_importer_command_ingest_result.json");
  const importCommands = readArtifact(tmpDir, "import_command_stream.json");
  const commandLog = readArtifact(tmpDir, "command_log.json");
  const runtimeGraph = readArtifact(tmpDir, "runtime_graph.json");
  const runtimeFrame = readArtifact(tmpDir, "runtime_frame_artifact.json");
  const diagnostics = readArtifact(tmpDir, "import_diagnostics.json");
  const errors = readArtifact(tmpDir, "native_importer_command_ingest_errors.json");

  assert.deepEqual(errors, []);
  assert.equal(result.kind, "NativeImporterCommandIngestProof");
  assert.equal(result.ok, true);
  assert.equal(result.status, "importer_command_ingest_ready");
  assert.equal(result.claims.importerMutationUsesCommandGraph, true);
  assert.equal(result.claims.externalDocumentStoredAsGraphTruth, false);
  assert.equal(result.claims.runtimeGraphBuiltFromReplay, true);

  assert.deepEqual(importCommands.commands.map((command) => command.op), [
    "createNode",
    "setNodePosition",
    "setParam",
    "createNode",
    "setNodePosition",
    "setParam",
    "createNode",
    "setNodePosition",
    "setParam",
    "connect",
    "connect",
    "createNode",
    "connect"
  ]);
  assert.ok(commandLog.every((entry) => entry.source === "importer.simple_world.import.v1"));
  assert.deepEqual(runtimeGraph.cookOrder, ["gradient_1", "blob_1", "blend_1", "output_1"]);
  assert.equal(runtimeGraph.nodes.find((node) => node.id === "blob_1").params.radius, 0.31);
  assert.equal(runtimeFrame.frameIndex, 1);
  assert.equal(diagnostics.items[0].code, "importer.command_stream.ready");
  assert.ok(!JSON.stringify({ result, importCommands, commandLog, runtimeGraph, runtimeFrame, diagnostics }).includes("/Users/"));
});

test("NativeImporterCommandIngest refuses direct imported graph truth", () => {
  const tmpDir = fs.mkdtempSync(path.join(os.tmpdir(), "native-importer-command-ingest-bad-"));
  const badFixture = path.join(tmpDir, "bad.graph.json");
  const graph = JSON.parse(fs.readFileSync(fixturePath, "utf8"));
  graph.graphId = "fixture.native_importer_command_ingest.bad";
  graph.importer.mode = "directGraphMutation";
  graph.importer.editorGraph = {
    nodes: graph.importer.externalDocument.nodes,
    edges: graph.importer.externalDocument.edges
  };
  fs.writeFileSync(badFixture, JSON.stringify(graph, null, 2));

  const run = spawnSync("python3", [scriptPath, badFixture, tmpDir], {
    cwd: repoRoot,
    encoding: "utf8",
  });

  assert.equal(run.status, 1);
  const result = readArtifact(tmpDir, "native_importer_command_ingest_result.json");
  const errors = readArtifact(tmpDir, "native_importer_command_ingest_errors.json");

  assert.equal(result.ok, false);
  assert.equal(result.status, "import_command_contract_failed");
  assert.equal(errors[0].code, "native_importer.direct_graph_mutation");
});

test("NativeImporterCommandIngest checked-in artifacts are path-clean and current", () => {
  const run = spawnSync("python3", [scriptPath, fixturePath, artifactDir], {
    cwd: repoRoot,
    encoding: "utf8",
  });

  assert.equal(run.status, 0, run.stderr || run.stdout);

  const result = readArtifact(artifactDir, "native_importer_command_ingest_result.json");
  const importCommands = readArtifact(artifactDir, "import_command_stream.json");
  const runtimeFrame = readArtifact(artifactDir, "runtime_frame_artifact.json");

  assert.equal(result.ok, true);
  assert.equal(importCommands.commands.length, 13);
  assert.equal(runtimeFrame.runtimeGraphSource, "command_replay");
  assert.ok(!JSON.stringify({ result, importCommands, runtimeFrame }).includes("/Users/"));
});

function readArtifact(dir, name) {
  return JSON.parse(fs.readFileSync(path.join(dir, name), "utf8"));
}

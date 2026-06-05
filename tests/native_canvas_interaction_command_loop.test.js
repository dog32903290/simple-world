const assert = require("node:assert/strict");
const { spawnSync } = require("node:child_process");
const fs = require("node:fs");
const os = require("node:os");
const path = require("node:path");
const test = require("node:test");

const repoRoot = path.resolve(__dirname, "..");
const contractPath = path.join(repoRoot, "docs/runtime/NATIVE_CANVAS_INTERACTION_COMMAND_LOOP_PROOF.md");
const fixturePath = path.join(repoRoot, "docs/runtime/fixtures/native_canvas_interaction_command_loop.graph.json");
const scriptPath = path.join(repoRoot, "docs/runtime/scripts/native_canvas_interaction_command_loop_shell.py");
const artifactDir = path.join(repoRoot, "docs/runtime/artifacts/native_canvas_interaction_command_loop");

test("NativeCanvasInteractionCommandLoop contract binds library canvas and inspector to commandGraph", () => {
  const source = fs.readFileSync(contractPath, "utf8");

  assert.match(source, /NativeCanvasInteractionCommandLoopProof answers:/);
  assert.match(source, /library pick -> canvas place -> canvas connect -> inspector edit -> runtime frame/);
  assert.match(source, /view-local graph truth is forbidden/);
  assert.match(source, /not final interaction parity/);
});

test("NativeCanvasInteractionCommandLoop fixture declares multi-region user actions as commands", () => {
  const graph = JSON.parse(fs.readFileSync(fixturePath, "utf8"));

  assert.equal(graph.graphId, "fixture.native_canvas_interaction_command_loop");
  assert.equal(graph.appShell.mode, "nativeCanvasInteractionCommandLoop");
  assert.deepEqual(graph.ui.layout.regions, ["toolbar", "library", "canvas", "inspector", "diagnostics"]);
  assert.deepEqual(graph.ui.expectedActionSources, ["ui.library", "ui.canvas", "ui.canvas", "ui.inspector"]);
  assert.deepEqual(graph.expected.runtimeCookOrder, ["gradient_1", "blend_1", "render_target_1", "output_1"]);
});

test("NativeCanvasInteractionCommandLoop shell emits command-backed interaction artifacts", () => {
  const tmpDir = fs.mkdtempSync(path.join(os.tmpdir(), "native-canvas-command-loop-"));
  const run = spawnSync("python3", [scriptPath, fixturePath, tmpDir], {
    cwd: repoRoot,
    encoding: "utf8",
  });

  assert.equal(run.status, 0, run.stderr || run.stdout);

  const result = readArtifact(tmpDir, "native_canvas_interaction_command_loop_result.json");
  const interaction = readArtifact(tmpDir, "interaction_trace.json");
  const hitTest = readArtifact(tmpDir, "canvas_hit_test.json");
  const commandLog = readArtifact(tmpDir, "command_log.json");
  const runtimeGraph = readArtifact(tmpDir, "runtime_graph.json");
  const runtimeFrame = readArtifact(tmpDir, "runtime_frame_artifact.json");
  const errors = readArtifact(tmpDir, "native_canvas_interaction_command_loop_errors.json");

  assert.deepEqual(errors, []);
  assert.equal(result.kind, "NativeCanvasInteractionCommandLoopProof");
  assert.equal(result.ok, true);
  assert.equal(result.status, "canvas_interaction_loop_ready");
  assert.equal(result.claims.libraryMutationUsesCommandGraph, true);
  assert.equal(result.claims.canvasMutationUsesCommandGraph, true);
  assert.equal(result.claims.inspectorMutationUsesCommandGraph, true);
  assert.equal(result.claims.viewLocalGraphTruth, false);

  assert.deepEqual(interaction.steps.map((step) => step.source), ["ui.library", "ui.canvas", "ui.canvas", "ui.inspector", "runtime"]);
  assert.deepEqual(commandLog.slice(-4).map((entry) => entry.source), ["ui.library", "ui.canvas", "ui.canvas", "ui.inspector"]);
  assert.equal(commandLog.at(-4).command.op, "createNode");
  assert.equal(commandLog.at(-3).command.op, "setNodePosition");
  assert.equal(commandLog.at(-2).command.op, "connect");
  assert.equal(commandLog.at(-1).command.op, "setParam");
  assert.equal(hitTest.selectedNodeId, "blend_1");
  assert.equal(hitTest.hitRegion, "node.body");
  assert.deepEqual(runtimeGraph.cookOrder, ["gradient_1", "blend_1", "render_target_1", "output_1"]);
  assert.equal(runtimeGraph.nodes.find((node) => node.id === "blend_1").position.x, 420);
  assert.equal(runtimeFrame.canvasSurface, "MTKView");
  assert.equal(runtimeFrame.lastInteractionSource, "ui.inspector");
  assert.ok(!JSON.stringify({ result, interaction, hitTest, commandLog, runtimeGraph, runtimeFrame }).includes("/Users/"));
});

test("NativeCanvasInteractionCommandLoop refuses canvas direct graph mutation", () => {
  const tmpDir = fs.mkdtempSync(path.join(os.tmpdir(), "native-canvas-command-loop-bad-"));
  const badFixture = path.join(tmpDir, "bad.graph.json");
  const graph = JSON.parse(fs.readFileSync(fixturePath, "utf8"));
  graph.graphId = "fixture.native_canvas_interaction_command_loop.bad";
  graph.ui.actions[1] = {
    source: "ui.canvas",
    kind: "directMutation",
    mutation: { op: "setNodePosition", id: "blend_1", position: { x: 420, y: 240 } }
  };
  fs.writeFileSync(badFixture, JSON.stringify(graph, null, 2));

  const run = spawnSync("python3", [scriptPath, badFixture, tmpDir], {
    cwd: repoRoot,
    encoding: "utf8",
  });

  assert.equal(run.status, 1);
  const result = readArtifact(tmpDir, "native_canvas_interaction_command_loop_result.json");
  const errors = readArtifact(tmpDir, "native_canvas_interaction_command_loop_errors.json");

  assert.equal(result.ok, false);
  assert.equal(result.status, "ui_command_contract_failed");
  assert.equal(errors[0].code, "native_canvas_interaction.ui_action_not_command");
  assert.equal(errors[0].source, "ui.canvas");
});

test("NativeCanvasInteractionCommandLoop checked-in artifacts are path-clean and current", () => {
  const run = spawnSync("python3", [scriptPath, fixturePath, artifactDir], {
    cwd: repoRoot,
    encoding: "utf8",
  });

  assert.equal(run.status, 0, run.stderr || run.stdout);

  const result = readArtifact(artifactDir, "native_canvas_interaction_command_loop_result.json");
  const interaction = readArtifact(artifactDir, "interaction_trace.json");
  const runtimeFrame = readArtifact(artifactDir, "runtime_frame_artifact.json");

  assert.equal(result.ok, true);
  assert.equal(interaction.steps.length, 5);
  assert.equal(runtimeFrame.nativeSurfaceReady, true);
  assert.ok(!JSON.stringify({ result, interaction, runtimeFrame }).includes("/Users/"));
});

function readArtifact(dir, name) {
  return JSON.parse(fs.readFileSync(path.join(dir, name), "utf8"));
}

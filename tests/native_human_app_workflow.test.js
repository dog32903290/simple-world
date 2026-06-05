const assert = require("node:assert/strict");
const { spawnSync } = require("node:child_process");
const fs = require("node:fs");
const os = require("node:os");
const path = require("node:path");
const test = require("node:test");

const repoRoot = path.resolve(__dirname, "..");
const contractPath = path.join(repoRoot, "docs/runtime/NATIVE_HUMAN_APP_WORKFLOW_PROOF.md");
const fixturePath = path.join(repoRoot, "docs/runtime/fixtures/native_human_app_workflow.graph.json");
const scriptPath = path.join(repoRoot, "docs/runtime/scripts/native_human_app_workflow_shell.py");
const artifactDir = path.join(repoRoot, "docs/runtime/artifacts/native_human_app_workflow");

test("NativeHumanAppWorkflow contract names a human-facing app without outrunning commandGraph", () => {
  const source = fs.readFileSync(contractPath, "utf8");

  assert.match(source, /NativeHumanAppWorkflowProof answers:/);
  assert.match(source, /GraphContract -> native UI hierarchy -> commandGraph action -> runtime frame evidence/);
  assert.match(source, /toolbar/);
  assert.match(source, /canvas/);
  assert.match(source, /inspector/);
  assert.match(source, /diagnostics strip/);
  assert.match(source, /not marketing skin/);
  assert.match(source, /not view-local graph truth/);
});

test("NativeHumanAppWorkflow fixture declares UI layout and command-backed workflow", () => {
  const graph = JSON.parse(fs.readFileSync(fixturePath, "utf8"));

  assert.equal(graph.graphId, "fixture.native_human_app_workflow");
  assert.equal(graph.appShell.mode, "nativeHumanWorkflow");
  assert.deepEqual(graph.ui.layout.regions, ["toolbar", "library", "canvas", "inspector", "diagnostics"]);
  assert.equal(graph.ui.actions[0].command.op, "setParam");
  assert.equal(graph.ui.actions[0].command.id, "gradient_1");
  assert.equal(graph.ui.actions[0].source, "ui.inspector");
  assert.deepEqual(graph.expected.runtimeCookOrder, ["gradient_1", "feedback_1", "render_target_1", "output_1"]);
});

test("NativeHumanAppWorkflow shell emits native UI hierarchy workflow and runtime artifacts", () => {
  const tmpDir = fs.mkdtempSync(path.join(os.tmpdir(), "native-human-workflow-"));
  const run = spawnSync("python3", [scriptPath, fixturePath, tmpDir], {
    cwd: repoRoot,
    encoding: "utf8",
  });

  assert.equal(run.status, 0, run.stderr || run.stdout);

  const result = readArtifact(tmpDir, "native_human_app_workflow_result.json");
  const hierarchy = readArtifact(tmpDir, "native_ui_hierarchy.json");
  const workflow = readArtifact(tmpDir, "workflow_trace.json");
  const inspector = readArtifact(tmpDir, "inspector_state.json");
  const diagnostics = readArtifact(tmpDir, "diagnostics_strip.json");
  const runtimeFrame = readArtifact(tmpDir, "runtime_frame_artifact.json");
  const commandLog = readArtifact(tmpDir, "command_log.json");
  const errors = readArtifact(tmpDir, "native_human_app_workflow_errors.json");

  assert.deepEqual(errors, []);
  assert.equal(result.kind, "NativeHumanAppWorkflowProof");
  assert.equal(result.ok, true);
  assert.equal(result.status, "human_workflow_ready");
  assert.equal(result.claims.nativeHumanFacingUi, true);
  assert.equal(result.claims.uiMutationUsesCommandGraph, true);
  assert.equal(result.claims.runtimeFrameLinked, true);
  assert.equal(result.claims.viewLocalGraphTruth, false);

  assert.equal(hierarchy.kind, "NativeHumanUiHierarchy");
  assert.equal(hierarchy.nativeProbe.actualAppKitRan, true);
  assert.equal(hierarchy.nativeProbe.actualMetalKitViewCreated, true);
  assert.deepEqual(hierarchy.regions.map((entry) => entry.id), ["toolbar", "library", "canvas", "inspector", "diagnostics"]);
  assert.equal(hierarchy.regions.find((entry) => entry.id === "canvas").nativeClass, "MTKView");
  assert.equal(hierarchy.regions.find((entry) => entry.id === "inspector").readsFrom, "NodeSpec+NodeInstance");
  assert.equal(hierarchy.regions.find((entry) => entry.id === "diagnostics").readsFrom, "runtime diagnostics");

  assert.equal(workflow.steps[0].op, "ui.selectNode");
  assert.equal(workflow.steps[1].op, "ui.dispatchCommand");
  assert.equal(workflow.steps[1].mutationPath, "commandGraph");
  assert.equal(workflow.steps[2].op, "runtime.buildFrame");
  assert.equal(inspector.selectedNodeId, "gradient_1");
  assert.equal(inspector.params.endColor.value[1], 0.82);
  assert.equal(inspector.params.endColor.owner, "NodeInstance");
  assert.equal(diagnostics.items[0].code, "runtime.frame.ready");
  assert.equal(runtimeFrame.canvasSurface, "MTKView");
  assert.equal(runtimeFrame.frameIndex, 1);
  assert.equal(commandLog.at(-1).source, "ui.inspector");
  assert.equal(commandLog.at(-1).command.param, "endColor");
  assert.ok(!JSON.stringify({ result, hierarchy, workflow, inspector, diagnostics, runtimeFrame, commandLog }).includes("/Users/"));
});

test("NativeHumanAppWorkflow refuses UI mutations that bypass commandGraph", () => {
  const tmpDir = fs.mkdtempSync(path.join(os.tmpdir(), "native-human-workflow-bad-"));
  const badFixture = path.join(tmpDir, "bad.graph.json");
  const graph = JSON.parse(fs.readFileSync(fixturePath, "utf8"));
  graph.graphId = "fixture.native_human_app_workflow.bad";
  graph.ui.actions[0] = {
    source: "ui.inspector",
    kind: "directMutation",
    nodeId: "gradient_1",
    param: "endColor",
    value: [0.9, 0.82, 0.25, 1]
  };
  fs.writeFileSync(badFixture, JSON.stringify(graph, null, 2));

  const run = spawnSync("python3", [scriptPath, badFixture, tmpDir], {
    cwd: repoRoot,
    encoding: "utf8",
  });

  assert.equal(run.status, 1);
  const result = readArtifact(tmpDir, "native_human_app_workflow_result.json");
  const errors = readArtifact(tmpDir, "native_human_app_workflow_errors.json");

  assert.equal(result.ok, false);
  assert.equal(result.status, "ui_command_contract_failed");
  assert.equal(errors[0].code, "native_human_workflow.ui_action_not_command");
  assert.equal(errors[0].source, "ui.inspector");
});

test("NativeHumanAppWorkflow checked-in artifacts are path-clean and current", () => {
  const run = spawnSync("python3", [scriptPath, fixturePath, artifactDir], {
    cwd: repoRoot,
    encoding: "utf8",
  });

  assert.equal(run.status, 0, run.stderr || run.stdout);

  const result = readArtifact(artifactDir, "native_human_app_workflow_result.json");
  const hierarchy = readArtifact(artifactDir, "native_ui_hierarchy.json");
  const inspector = readArtifact(artifactDir, "inspector_state.json");
  const runtimeFrame = readArtifact(artifactDir, "runtime_frame_artifact.json");

  assert.equal(result.ok, true);
  assert.equal(result.claims.nativeHumanFacingUi, true);
  assert.equal(hierarchy.regions.length, 5);
  assert.equal(inspector.selectedNodeId, "gradient_1");
  assert.equal(runtimeFrame.nativeSurfaceReady, true);
  assert.ok(!JSON.stringify({ result, hierarchy, inspector, runtimeFrame }).includes("/Users/"));
});

function readArtifact(dir, name) {
  return JSON.parse(fs.readFileSync(path.join(dir, name), "utf8"));
}

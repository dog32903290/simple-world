const assert = require("node:assert/strict");
const { spawnSync } = require("node:child_process");
const fs = require("node:fs");
const os = require("node:os");
const path = require("node:path");
const test = require("node:test");

const repoRoot = path.resolve(__dirname, "..");
const contractPath = path.join(repoRoot, "docs/runtime/NATIVE_AI_WORKER_AUTHORING_ASSIST_PROOF.md");
const fixturePath = path.join(repoRoot, "docs/runtime/fixtures/native_ai_worker_authoring_assist.graph.json");
const scriptPath = path.join(repoRoot, "docs/runtime/scripts/native_ai_worker_authoring_assist_shell.py");
const artifactDir = path.join(repoRoot, "docs/runtime/artifacts/native_ai_worker_authoring_assist");

test("NativeAIWorkerAuthoringAssist contract bounds AI authoring to command plans", () => {
  const source = fs.readFileSync(contractPath, "utf8");

  assert.match(source, /NativeAIWorkerAuthoringAssistProof answers:/);
  assert.match(source, /bounded intent -> AI command plan -> commandGraph validation -> runtime artifact -> diagnostics/);
  assert.match(source, /AI proposal is not graph truth/);
  assert.match(source, /direct editorGraph mutation is rejected/);
  assert.match(source, /not broad natural-language patch authoring/);
});

test("NativeAIWorkerAuthoringAssist fixture starts from intent not graph mutation", () => {
  const graph = JSON.parse(fs.readFileSync(fixturePath, "utf8"));

  assert.equal(graph.graphId, "fixture.native_ai_worker_authoring_assist");
  assert.equal(graph.aiWorker.mode, "boundedAuthoringAssist");
  assert.equal(graph.intent.kind, "simple_world.intent.texture_patch.v1");
  assert.equal(graph.expected.commandPlanLength, 13);
  assert.deepEqual(graph.expected.runtimeCookOrder, ["gradient_1", "blob_1", "blend_1", "feedback_1", "render_target_1", "output_1"]);
  assert.equal(Object.hasOwn(graph.aiWorker, "editorGraph"), false);
  assert.ok(graph.aiWorker.allowedCommands.includes("createNode"));
  assert.ok(graph.aiWorker.allowedCommands.includes("setParam"));
  assert.ok(graph.aiWorker.allowedCommands.includes("connect"));
});

test("NativeAIWorkerAuthoringAssist shell validates plan applies commands renders diagnostics and repairs", () => {
  const tmpDir = fs.mkdtempSync(path.join(os.tmpdir(), "native-ai-authoring-assist-"));
  const run = spawnSync("python3", [scriptPath, fixturePath, tmpDir], {
    cwd: repoRoot,
    encoding: "utf8",
  });

  assert.equal(run.status, 0, run.stderr || run.stdout);

  const result = readArtifact(tmpDir, "native_ai_worker_authoring_assist_result.json");
  const intent = readArtifact(tmpDir, "bounded_intent.json");
  const plan = readArtifact(tmpDir, "ai_command_plan.json");
  const validation = readArtifact(tmpDir, "command_plan_validation.json");
  const commandLog = readArtifact(tmpDir, "command_log.json");
  const runtimeGraph = readArtifact(tmpDir, "runtime_graph.json");
  const runtimeFrame = readArtifact(tmpDir, "runtime_frame_artifact.json");
  const diagnostics = readArtifact(tmpDir, "diagnostics.json");
  const repairLog = readArtifact(tmpDir, "ai_authoring_repair_log.json");
  const errors = readArtifact(tmpDir, "native_ai_worker_authoring_assist_errors.json");

  assert.deepEqual(errors, []);
  assert.equal(result.kind, "NativeAIWorkerAuthoringAssistProof");
  assert.equal(result.ok, true);
  assert.equal(result.status, "authored_render_ready");
  assert.equal(result.claims.boundedIntentAuthoring, true);
  assert.equal(result.claims.commandPlanValidated, true);
  assert.equal(result.claims.commandGraphOnlyMutation, true);
  assert.equal(result.claims.runtimeArtifactRendered, true);
  assert.equal(result.claims.diagnosticFeedbackObserved, true);
  assert.equal(result.claims.directGraphMutationRejected, true);
  assert.equal(result.claims.broadNaturalLanguageAuthoring, false);

  assert.equal(intent.kind, "simple_world.intent.texture_patch.v1");
  assert.equal(plan.commands.length, 13);
  assert.equal(validation.ok, true);
  assert.ok(commandLog.every((entry) => entry.source === "aiWorker.authoringAssist"));
  assert.deepEqual(runtimeGraph.cookOrder, ["gradient_1", "blob_1", "blend_1", "feedback_1", "render_target_1", "output_1"]);
  assert.equal(runtimeFrame.runtimeGraphSource, "ai_command_replay");
  assert.deepEqual(diagnostics.items.map((entry) => entry.code), ["authoring.feedback_injection.low", "render.non_black_frame"]);
  assert.equal(repairLog[0].appliedCommand.op, "setParam");
  assert.equal(repairLog[0].appliedCommand.id, "feedback_1");
  assert.equal(repairLog[0].mutationPath, "commandGraph");
  assert.ok(!JSON.stringify({ result, intent, plan, validation, commandLog, runtimeGraph, runtimeFrame, diagnostics, repairLog }).includes("/Users/"));
});

test("NativeAIWorkerAuthoringAssist rejects direct graph mutation proposals", () => {
  const tmpDir = fs.mkdtempSync(path.join(os.tmpdir(), "native-ai-authoring-assist-direct-"));
  const badFixture = path.join(tmpDir, "bad.graph.json");
  const graph = JSON.parse(fs.readFileSync(fixturePath, "utf8"));
  graph.graphId = "fixture.native_ai_worker_authoring_assist.direct_graph_bad";
  graph.aiWorker.proposal = {
    editorGraph: {
      nodes: [{ id: "blob_1", type: "texture.blob" }],
      edges: []
    }
  };
  fs.writeFileSync(badFixture, JSON.stringify(graph, null, 2));

  const run = spawnSync("python3", [scriptPath, badFixture, tmpDir], {
    cwd: repoRoot,
    encoding: "utf8",
  });

  assert.equal(run.status, 1);
  const result = readArtifact(tmpDir, "native_ai_worker_authoring_assist_result.json");
  const errors = readArtifact(tmpDir, "native_ai_worker_authoring_assist_errors.json");

  assert.equal(result.ok, false);
  assert.equal(result.status, "ai_command_plan_rejected");
  assert.equal(errors[0].code, "native_ai_authoring.direct_graph_mutation");
});

test("NativeAIWorkerAuthoringAssist rejects unknown command ops before graph mutation", () => {
  const tmpDir = fs.mkdtempSync(path.join(os.tmpdir(), "native-ai-authoring-assist-unknown-"));
  const badFixture = path.join(tmpDir, "bad.graph.json");
  const graph = JSON.parse(fs.readFileSync(fixturePath, "utf8"));
  graph.graphId = "fixture.native_ai_worker_authoring_assist.unknown_op_bad";
  graph.aiWorker.proposal = {
    commands: [
      { op: "createNode", id: "gradient_1", type: "texture.gradient" },
      { op: "mutateEditorGraph", path: "/nodes/blob_1", value: {} }
    ]
  };
  fs.writeFileSync(badFixture, JSON.stringify(graph, null, 2));

  const run = spawnSync("python3", [scriptPath, badFixture, tmpDir], {
    cwd: repoRoot,
    encoding: "utf8",
  });

  assert.equal(run.status, 1);
  const result = readArtifact(tmpDir, "native_ai_worker_authoring_assist_result.json");
  const validation = readArtifact(tmpDir, "command_plan_validation.json");
  const commandLog = readArtifact(tmpDir, "command_log.json");
  const errors = readArtifact(tmpDir, "native_ai_worker_authoring_assist_errors.json");

  assert.equal(result.ok, false);
  assert.equal(result.status, "ai_command_plan_rejected");
  assert.equal(validation.ok, false);
  assert.deepEqual(commandLog, []);
  assert.equal(errors[0].code, "native_ai_authoring.command_not_allowed");
});

test("NativeAIWorkerAuthoringAssist checked-in artifacts are path-clean and current", () => {
  const run = spawnSync("python3", [scriptPath, fixturePath, artifactDir], {
    cwd: repoRoot,
    encoding: "utf8",
  });

  assert.equal(run.status, 0, run.stderr || run.stdout);

  const result = readArtifact(artifactDir, "native_ai_worker_authoring_assist_result.json");
  const validation = readArtifact(artifactDir, "command_plan_validation.json");
  const runtimeFrame = readArtifact(artifactDir, "runtime_frame_artifact.json");
  const diagnostics = readArtifact(artifactDir, "diagnostics.json");

  assert.equal(result.ok, true);
  assert.equal(result.claims.commandGraphOnlyMutation, true);
  assert.equal(result.claims.broadNaturalLanguageAuthoring, false);
  assert.equal(validation.ok, true);
  assert.equal(runtimeFrame.runtimeGraphSource, "ai_command_replay");
  assert.ok(diagnostics.items.some((entry) => entry.code === "render.non_black_frame"));
  assert.ok(!JSON.stringify({ result, validation, runtimeFrame, diagnostics }).includes("/Users/"));
});

function readArtifact(dir, name) {
  return JSON.parse(fs.readFileSync(path.join(dir, name), "utf8"));
}

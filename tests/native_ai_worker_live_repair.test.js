const assert = require("node:assert/strict");
const { spawnSync } = require("node:child_process");
const fs = require("node:fs");
const os = require("node:os");
const path = require("node:path");
const test = require("node:test");

const repoRoot = path.resolve(__dirname, "..");
const contractPath = path.join(repoRoot, "docs/runtime/NATIVE_AI_WORKER_LIVE_REPAIR_PROOF.md");
const fixturePath = path.join(repoRoot, "docs/runtime/fixtures/native_ai_worker_live_repair.graph.json");
const scriptPath = path.join(repoRoot, "docs/runtime/scripts/native_ai_worker_live_repair_shell.py");
const artifactDir = path.join(repoRoot, "docs/runtime/artifacts/native_ai_worker_live_repair");

test("NativeAIWorkerLiveRepair contract binds AI to render diagnostics and commandGraph repair", () => {
  const source = fs.readFileSync(contractPath, "utf8");

  assert.match(source, /NativeAIWorkerLiveRepairProof answers:/);
  assert.match(source, /graph -> render -> diagnostics -> repair command -> render/);
  assert.match(source, /AI worker is not a chat sidecar/);
  assert.match(source, /No direct graph JSON surgery/);
  assert.match(source, /not broad natural-language patch authoring/);
});

test("NativeAIWorkerLiveRepair fixture starts as a rendered black frame and repairs through commands", () => {
  const graph = JSON.parse(fs.readFileSync(fixturePath, "utf8"));

  assert.equal(graph.graphId, "fixture.native_ai_worker_live_repair");
  assert.equal(graph.aiWorker.enabled, true);
  assert.equal(graph.aiWorker.maxRepairAttempts, 2);
  assert.deepEqual(graph.expected.initialDiagnostics, ["render.black_frame"]);
  assert.deepEqual(graph.expected.repairedCookOrder, ["gradient_1", "feedback_1", "render_target_1", "output_1"]);
  assert.ok(graph.commands.some((command) => command.op === "setParam" && command.id === "feedback_1" && command.param === "injection" && command.value === 0));
  assert.ok(graph.aiWorker.diagnosticRules.some((rule) => rule.code === "render.black_frame" && rule.repairCommand.op === "setParam"));
});

test("NativeAIWorkerLiveRepair shell renders, diagnoses, repairs by command, and rerenders", () => {
  const tmpDir = fs.mkdtempSync(path.join(os.tmpdir(), "native-ai-live-repair-"));
  const run = spawnSync("python3", [scriptPath, fixturePath, tmpDir], {
    cwd: repoRoot,
    encoding: "utf8",
  });

  assert.equal(run.status, 0, run.stderr || run.stdout);

  const result = readArtifact(tmpDir, "native_ai_worker_live_repair_result.json");
  const initialStats = readArtifact(tmpDir, "initial_render/frame_stats.json");
  const repairedStats = readArtifact(tmpDir, "repaired_render/frame_stats.json");
  const diagnostics = readArtifact(tmpDir, "diagnostics.json");
  const repairLog = readArtifact(tmpDir, "ai_repair_log.json");
  const commandLog = readArtifact(tmpDir, "command_log.json");
  const repairedFixture = readArtifact(tmpDir, "repaired_texture_patch.graph.json");
  const errors = readArtifact(tmpDir, "native_ai_worker_live_repair_errors.json");

  assert.deepEqual(errors, []);
  assert.equal(result.kind, "NativeAIWorkerLiveRepairProof");
  assert.equal(result.ok, true);
  assert.equal(result.status, "repaired_rendered");
  assert.equal(result.claims.initialRenderRan, true);
  assert.equal(result.claims.diagnosticsReadFromRenderArtifact, true);
  assert.equal(result.claims.aiRepairUsedCommandGraph, true);
  assert.equal(result.claims.repairedRenderRan, true);
  assert.equal(result.claims.broadNaturalLanguageAuthoring, false);

  assert.equal(initialStats.nonBlack, false);
  assert.equal(initialStats.varied, false);
  assert.equal(repairedStats.nonBlack, true);
  assert.equal(repairedStats.varied, true);
  assert.deepEqual(diagnostics.map((entry) => entry.code), ["render.black_frame"]);
  assert.equal(repairLog[0].diagnostic.code, "render.black_frame");
  assert.equal(repairLog[0].appliedCommand.op, "setParam");
  assert.equal(repairLog[0].appliedCommand.id, "feedback_1");
  assert.equal(repairLog[0].appliedCommand.param, "injection");
  assert.equal(repairLog[0].mutationPath, "commandGraph");
  assert.equal(commandLog.at(-1).source, "aiWorker");
  assert.equal(commandLog.at(-1).command.value, 0.58);
  assert.ok(repairedFixture.commands.some((command) => command.id === "feedback_1" && command.param === "injection" && command.value === 0.58));
  assert.ok(!JSON.stringify({ result, diagnostics, repairLog, commandLog, repairedFixture }).includes("/Users/"));
});

test("NativeAIWorkerLiveRepair checked-in artifacts are path-clean and current", () => {
  const run = spawnSync("python3", [scriptPath, fixturePath, artifactDir], {
    cwd: repoRoot,
    encoding: "utf8",
  });

  assert.equal(run.status, 0, run.stderr || run.stdout);

  const result = readArtifact(artifactDir, "native_ai_worker_live_repair_result.json");
  const diagnostics = readArtifact(artifactDir, "diagnostics.json");
  const repairLog = readArtifact(artifactDir, "ai_repair_log.json");
  const repairedStats = readArtifact(artifactDir, "repaired_render/frame_stats.json");

  assert.equal(result.ok, true);
  assert.equal(result.claims.aiRepairUsedCommandGraph, true);
  assert.equal(result.claims.broadNaturalLanguageAuthoring, false);
  assert.equal(diagnostics[0].code, "render.black_frame");
  assert.equal(repairLog[0].mutationPath, "commandGraph");
  assert.equal(repairedStats.nonBlack, true);
  assert.ok(!JSON.stringify({ result, diagnostics, repairLog, repairedStats }).includes("/Users/"));
});

function readArtifact(dir, name) {
  return JSON.parse(fs.readFileSync(path.join(dir, name), "utf8"));
}

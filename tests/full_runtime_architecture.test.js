const assert = require("node:assert/strict");
const { spawnSync } = require("node:child_process");
const fs = require("node:fs");
const os = require("node:os");
const path = require("node:path");
const test = require("node:test");

const repoRoot = path.resolve(__dirname, "..");
const contractPath = path.join(repoRoot, "docs/runtime/FULL_RUNTIME_ARCHITECTURE_PROOF.md");
const fixturePath = path.join(repoRoot, "docs/runtime/fixtures/full_runtime_architecture.graph.json");
const scriptPath = path.join(repoRoot, "docs/runtime/scripts/full_runtime_architecture_shell.py");
const artifactDir = path.join(repoRoot, "docs/runtime/artifacts/full_runtime_architecture");

test("Full runtime architecture contract names all eight completion axes", () => {
  const source = fs.readFileSync(contractPath, "utf8");

  assert.match(source, /FullRuntimeArchitectureProof answers:/);
  assert.match(source, /nativeAppShellCanvas: true/);
  assert.match(source, /commandGraphOnlyMutation: true/);
  assert.match(source, /runtimeGraphBuilt: true/);
  assert.match(source, /frameSchedulerLive: true/);
  assert.match(source, /gpuTextureRuntimeMetal: true/);
  assert.match(source, /shaderIrCodegenCache: true/);
  assert.match(source, /resourceAllocatorLifetime: true/);
  assert.match(source, /aiWorkerRepairLoop: true/);
  assert.match(source, /not the final polished product/);
});

test("Full runtime architecture fixture starts broken so AI repair must use commands", () => {
  const graph = JSON.parse(fs.readFileSync(fixturePath, "utf8"));

  assert.equal(graph.graphId, "fixture.full_runtime_architecture");
  assert.equal(graph.appShell.mode, "nativeHeadlessCanvas");
  assert.equal(graph.scheduler.clockOwner, "graph");
  assert.equal(graph.aiWorker.maxRepairAttempts, 2);
  assert.deepEqual(graph.expected.cookOrder, ["constant_bg", "blob_fg", "blend_1", "output_1"]);
  assert.ok(graph.commands.some((command) => command.op === "setParam" && command.id === "blend_1" && command.param === "inputCount" && command.value === 0));
  assert.ok(graph.aiWorker.diagnosticRules.some((rule) => rule.repairCommand.op === "setParam" && rule.repairCommand.value === 2));
});

test("Full runtime architecture shell emits eight-axis proof artifacts", () => {
  const tmpDir = fs.mkdtempSync(path.join(os.tmpdir(), "full-runtime-architecture-"));
  const run = spawnSync("python3", [scriptPath, fixturePath, tmpDir], {
    cwd: repoRoot,
    encoding: "utf8",
  });

  assert.ok(run.status === 0 || run.status === 1, run.stderr || run.stdout);

  const result = readArtifact(tmpDir, "full_runtime_architecture_result.json");
  const appShell = readArtifact(tmpDir, "native_app_shell.json");
  const commandLog = readArtifact(tmpDir, "command_log.json");
  const runtimeGraph = readArtifact(tmpDir, "runtime_graph.json");
  const frameTrace = readArtifact(tmpDir, "frame_scheduler_trace.json");
  const resourceLedger = readArtifact(tmpDir, "resource_ledger.json");
  const shaderIr = readArtifact(tmpDir, "shader_ir.json");
  const shaderCache = readArtifact(tmpDir, "shader_cache.json");
  const diagnostics = readArtifact(tmpDir, "diagnostics.json");
  const repairLog = readArtifact(tmpDir, "ai_repair_log.json");
  const metalResult = readArtifact(tmpDir, "gpu_patch/native_gpu_patch_runtime_slice_result.json");

  assert.equal(result.kind, "FullRuntimeArchitectureProof");
  assert.equal(result.graphId, "fixture.full_runtime_architecture");
  assert.equal(result.claims.nativeAppShellCanvas, true);
  assert.equal(result.claims.commandGraphOnlyMutation, true);
  assert.equal(result.claims.runtimeGraphBuilt, true);
  assert.equal(result.claims.frameSchedulerLive, true);
  assert.equal(result.claims.resourceAllocatorLifetime, true);
  assert.equal(result.claims.shaderIrCodegenCache, true);
  assert.equal(result.claims.aiWorkerRepairLoop, true);

  assert.equal(appShell.canvas.id, "mainCanvas");
  assert.equal(appShell.canvas.surface.kind, "MetalBackedTexture2D");
  assert.equal(commandLog.every((entry) => entry.source === "fixture" || entry.source === "aiWorker"), true);
  assert.equal(commandLog.some((entry) => entry.source === "aiWorker" && entry.command.param === "inputCount" && entry.command.value === 2), true);
  assert.deepEqual(runtimeGraph.cookOrder, ["constant_bg", "blob_fg", "blend_1", "output_1"]);
  assert.deepEqual(frameTrace.filter((entry) => entry.op === "frame.begin").map((entry) => entry.frame.frameIndex), [0, 1, 2]);
  assert.equal(resourceLedger.lifetimeEvents.some((entry) => entry.action === "allocate"), true);
  assert.equal(resourceLedger.lifetimeEvents.some((entry) => entry.action === "reuse"), true);
  assert.deepEqual(shaderIr.nodes.map((node) => node.op), ["ConstantImage", "Blob", "BlendImages"]);
  assert.equal(shaderCache.entries.length, 3);
  assert.equal(diagnostics.some((entry) => entry.code === "runtime.blend_input_count_mismatch"), true);
  assert.equal(repairLog[0].appliedCommand.param, "inputCount");

  if (run.status === 0) {
    assert.equal(result.ok, true);
    assert.equal(result.claims.gpuTextureRuntimeMetal, true);
    assert.equal(metalResult.status, "rendered");
    return;
  }

  assert.equal(result.ok, false);
  assert.equal(result.claims.gpuTextureRuntimeMetal, false);
  assert.match(result.status, /blocked_metal_device_unavailable|probe_failed/);
});

test("Full runtime architecture checked-in artifacts are path-clean and current", () => {
  const run = spawnSync("python3", [scriptPath, fixturePath, artifactDir], {
    cwd: repoRoot,
    encoding: "utf8",
  });

  assert.ok(run.status === 0 || run.status === 1, run.stderr || run.stdout);

  const result = readArtifact(artifactDir, "full_runtime_architecture_result.json");
  const payload = {
    result,
    appShell: readArtifact(artifactDir, "native_app_shell.json"),
    commandLog: readArtifact(artifactDir, "command_log.json"),
    runtimeGraph: readArtifact(artifactDir, "runtime_graph.json"),
    resourceLedger: readArtifact(artifactDir, "resource_ledger.json"),
    repairLog: readArtifact(artifactDir, "ai_repair_log.json"),
  };

  assert.equal(result.kind, "FullRuntimeArchitectureProof");
  assert.equal(result.claims.commandGraphOnlyMutation, true);
  assert.ok(!JSON.stringify(payload).includes("/Users/"));
});

function readArtifact(dir, name) {
  return JSON.parse(fs.readFileSync(path.join(dir, name), "utf8"));
}

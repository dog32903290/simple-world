const assert = require("node:assert/strict");
const { spawnSync } = require("node:child_process");
const fs = require("node:fs");
const os = require("node:os");
const path = require("node:path");
const test = require("node:test");

const repoRoot = path.resolve(__dirname, "..");
const contractPath = path.join(repoRoot, "docs/runtime/NATIVE_FRAME_SCHEDULER_LIVE_DIRTY_PROOF.md");
const fixturePath = path.join(repoRoot, "docs/runtime/fixtures/native_frame_scheduler_live_dirty.graph.json");
const scriptPath = path.join(repoRoot, "docs/runtime/scripts/native_frame_scheduler_live_dirty_shell.py");
const artifactDir = path.join(repoRoot, "docs/runtime/artifacts/native_frame_scheduler_live_dirty");

test("NativeFrameSchedulerLiveDirty contract names frame uniforms and command dirty propagation", () => {
  const source = fs.readFileSync(contractPath, "utf8");

  assert.match(source, /NativeFrameSchedulerLiveDirtyProof answers:/);
  assert.match(source, /commandGraph frame edit -> dirty closure -> scheduled cook set -> frame artifact/);
  assert.match(source, /u_time and u_frame are scheduler-owned uniforms/);
  assert.match(source, /not a renderer/);
});

test("NativeFrameSchedulerLiveDirty fixture separates startup dirty command dirty and frame-uniform dirty", () => {
  const graph = JSON.parse(fs.readFileSync(fixturePath, "utf8"));

  assert.equal(graph.graphId, "fixture.native_frame_scheduler_live_dirty");
  assert.equal(graph.scheduler.type, "FrameScheduler");
  assert.equal(graph.scheduler.clockOwner, "graph");
  assert.deepEqual(graph.expected.frameCookSets.map((entry) => entry.frameIndex), [0, 1, 2]);
  assert.deepEqual(graph.expected.frameCookSets[0].cookedNodes, ["u_time_1", "gradient_1", "blob_1", "blend_1", "output_1"]);
  assert.deepEqual(graph.expected.frameCookSets[1].cookedNodes, ["u_time_1", "gradient_1", "blend_1", "output_1"]);
  assert.deepEqual(graph.expected.frameCookSets[2].cookedNodes, ["u_time_1", "output_1"]);
});

test("NativeFrameSchedulerLiveDirty shell emits dirty trace and runtime frame artifacts", () => {
  const tmpDir = fs.mkdtempSync(path.join(os.tmpdir(), "native-frame-dirty-"));
  const run = spawnSync("python3", [scriptPath, fixturePath, tmpDir], {
    cwd: repoRoot,
    encoding: "utf8",
  });

  assert.equal(run.status, 0, run.stderr || run.stdout);

  const result = readArtifact(tmpDir, "native_frame_scheduler_live_dirty_result.json");
  const dirtyTrace = readArtifact(tmpDir, "live_dirty_trace.json");
  const commandLog = readArtifact(tmpDir, "command_log.json");
  const runtimeGraph = readArtifact(tmpDir, "runtime_graph.json");
  const frameArtifacts = readArtifact(tmpDir, "frame_artifacts.json");
  const errors = readArtifact(tmpDir, "native_frame_scheduler_live_dirty_errors.json");

  assert.deepEqual(errors, []);
  assert.equal(result.kind, "NativeFrameSchedulerLiveDirtyProof");
  assert.equal(result.ok, true);
  assert.equal(result.claims.schedulerOwnsFrameUniforms, true);
  assert.equal(result.claims.commandDirtyPropagation, true);
  assert.equal(result.claims.staticUnchangedNodeSkipped, true);

  assert.deepEqual(commandLog.map((entry) => entry.source), ["fixture", "fixture", "fixture", "fixture", "runtime.frame.1"]);
  assert.equal(commandLog.at(-1).command.param, "endColor");
  assert.deepEqual(runtimeGraph.cookOrder, ["u_time_1", "gradient_1", "blob_1", "blend_1", "output_1"]);

  assert.deepEqual(dirtyTrace.frames.map((frame) => frame.dirtyRoots), [
    ["startup"],
    ["u_time_1", "gradient_1"],
    ["u_time_1"]
  ]);
  assert.deepEqual(dirtyTrace.frames.map((frame) => frame.cookedNodes), [
    ["u_time_1", "gradient_1", "blob_1", "blend_1", "output_1"],
    ["u_time_1", "gradient_1", "blend_1", "output_1"],
    ["u_time_1", "output_1"]
  ]);
  assert.equal(dirtyTrace.frames[1].skippedCleanNodes.includes("blob_1"), true);
  assert.deepEqual(frameArtifacts.frames.map((frame) => frame.frameUniforms.u_frame), [0, 1, 2]);
  assert.deepEqual(frameArtifacts.frames.map((frame) => frame.frameUniforms.u_time), [0, 0.0166667, 0.0333334]);
  assert.ok(!JSON.stringify({ result, dirtyTrace, commandLog, runtimeGraph, frameArtifacts }).includes("/Users/"));
});

test("NativeFrameSchedulerLiveDirty refuses node-owned frame uniforms", () => {
  const tmpDir = fs.mkdtempSync(path.join(os.tmpdir(), "native-frame-dirty-bad-"));
  const badFixture = path.join(tmpDir, "bad.graph.json");
  const graph = JSON.parse(fs.readFileSync(fixturePath, "utf8"));
  graph.graphId = "fixture.native_frame_scheduler_live_dirty.bad";
  graph.nodes.find((node) => node.id === "u_time_1").clockOwner = "node";
  fs.writeFileSync(badFixture, JSON.stringify(graph, null, 2));

  const run = spawnSync("python3", [scriptPath, badFixture, tmpDir], {
    cwd: repoRoot,
    encoding: "utf8",
  });

  assert.equal(run.status, 1);
  const result = readArtifact(tmpDir, "native_frame_scheduler_live_dirty_result.json");
  const errors = readArtifact(tmpDir, "native_frame_scheduler_live_dirty_errors.json");

  assert.equal(result.ok, false);
  assert.equal(result.status, "frame_uniform_contract_failed");
  assert.equal(errors[0].code, "native_frame_scheduler.node_owned_frame_uniform");
});

test("NativeFrameSchedulerLiveDirty checked-in artifacts are path-clean and current", () => {
  const run = spawnSync("python3", [scriptPath, fixturePath, artifactDir], {
    cwd: repoRoot,
    encoding: "utf8",
  });

  assert.equal(run.status, 0, run.stderr || run.stdout);

  const result = readArtifact(artifactDir, "native_frame_scheduler_live_dirty_result.json");
  const dirtyTrace = readArtifact(artifactDir, "live_dirty_trace.json");
  const frameArtifacts = readArtifact(artifactDir, "frame_artifacts.json");

  assert.equal(result.ok, true);
  assert.equal(dirtyTrace.frames.length, 3);
  assert.equal(frameArtifacts.frames.at(-1).publishedNode, "output_1");
  assert.ok(!JSON.stringify({ result, dirtyTrace, frameArtifacts }).includes("/Users/"));
});

function readArtifact(dir, name) {
  return JSON.parse(fs.readFileSync(path.join(dir, name), "utf8"));
}

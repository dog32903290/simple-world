const assert = require("node:assert/strict");
const { spawnSync } = require("node:child_process");
const fs = require("node:fs");
const path = require("node:path");
const test = require("node:test");

const repoRoot = path.resolve(__dirname, "..");
const contractPath = path.join(repoRoot, "docs/runtime/FRAME_SCHEDULER_CONTRACT.md");
const runtimeContractPath = path.join(repoRoot, "docs/runtime/MY_WORLD_RUNTIME_CONTRACT.md");
const fixturePath = path.join(repoRoot, "docs/runtime/fixtures/frame_scheduler_constant_feedback.graph.json");
const scriptPath = path.join(repoRoot, "docs/runtime/scripts/frame_scheduler_shell.py");
const artifactDir = path.join(repoRoot, "docs/runtime/artifacts/frame_scheduler");

test("FrameScheduler contract names graph-level time ownership instead of per-node clocks", () => {
  const contract = fs.readFileSync(contractPath, "utf8");
  const runtimeContract = fs.readFileSync(runtimeContractPath, "utf8");

  assert.match(contract, /FrameScheduler answers:/);
  assert.match(contract, /who owns visual time for this graph frame/);
  assert.match(contract, /FrameScheduler := graph-level owner of frameIndex, time, deltaTime, and cook order/);
  assert.match(contract, /Visual nodes do not own time/);
  assert.match(contract, /state nodes update once per frame boundary/);
  assert.match(contract, /Vuo event cables may still exist in generated proof compositions/);
  assert.match(contract, /clock ownership and state boundary/);

  assert.match(runtimeContract, /## Main Clock Contract/);
  assert.match(runtimeContract, /FrameScheduler/);
});

test("FrameScheduler fixture describes one shared frame clock and no per-node clock owners", () => {
  const graph = JSON.parse(fs.readFileSync(fixturePath, "utf8"));

  assert.equal(graph.graphId, "fixture.frame_scheduler_constant_feedback");
  assert.equal(graph.scheduler.type, "FrameScheduler");
  assert.equal(graph.scheduler.clockOwner, "graph");
  assert.deepEqual(graph.scheduler.frames.map((frame) => frame.frameIndex), [0, 1, 2]);
  assert.deepEqual(graph.expected.cookOrder, [
    "constant_a",
    "constant_b",
    "blend_1",
    "keep_previous_1",
    "output_1",
  ]);
  assert.equal(graph.expected.stateNodeUpdateCountPerFrame, 1);
  assert.equal(graph.expected.allNodesShareFrameContext, true);
  for (const node of graph.nodes) {
    assert.equal(node.clockOwner, undefined);
  }
});

test("FrameScheduler shell emits synchronized frame observations and state boundary trace", () => {
  const run = spawnSync("python3", [scriptPath, fixturePath, artifactDir], {
    cwd: repoRoot,
    encoding: "utf8",
  });

  assert.equal(run.status, 0, run.stderr);

  const trace = JSON.parse(fs.readFileSync(path.join(artifactDir, "frame_scheduler_trace.json"), "utf8"));
  const errors = JSON.parse(fs.readFileSync(path.join(artifactDir, "frame_scheduler_errors.json"), "utf8"));
  const observations = JSON.parse(fs.readFileSync(path.join(artifactDir, "node_observations.json"), "utf8"));
  const stateTrace = JSON.parse(fs.readFileSync(path.join(artifactDir, "state_trace.json"), "utf8"));

  assert.deepEqual(errors, []);
  assert.equal(trace[0].op, "loadGraph");
  assert.equal(trace[0].clockOwner, "graph");
  assert.deepEqual(trace[0].cookOrder, ["constant_a", "constant_b", "blend_1", "keep_previous_1", "output_1"]);

  const frameBegins = trace.filter((entry) => entry.op === "frame.begin");
  const publishes = trace.filter((entry) => entry.op === "frame.publish");
  assert.deepEqual(frameBegins.map((entry) => entry.frame.frameIndex), [0, 1, 2]);
  assert.deepEqual(publishes.map((entry) => entry.frameIndex), [0, 1, 2]);
  assert.ok(publishes.every((entry) => entry.ok === true));

  for (const frame of frameBegins.map((entry) => entry.frame)) {
    const frameObservations = observations.filter((entry) => entry.frame.frameIndex === frame.frameIndex);
    assert.deepEqual(frameObservations.map((entry) => entry.nodeId), [
      "constant_a",
      "constant_b",
      "blend_1",
      "keep_previous_1",
      "output_1",
    ]);
    assert.ok(frameObservations.every((entry) => entry.frame.time === frame.time));
    assert.ok(frameObservations.every((entry) => entry.frame.deltaTime === frame.deltaTime));
  }

  assert.deepEqual(stateTrace.map((entry) => entry.frameIndex), [0, 1, 2]);
  assert.deepEqual(stateTrace.map((entry) => entry.previousFrame), [null, 0, 1]);
  assert.deepEqual(stateTrace.map((entry) => entry.currentFrame), [0, 1, 2]);
});

test("FrameScheduler shell refuses invalid clock ownership", () => {
  const tmpDir = fs.mkdtempSync(path.join(require("node:os").tmpdir(), "frame-scheduler-bad-"));
  const badGraph = JSON.parse(fs.readFileSync(fixturePath, "utf8"));
  badGraph.scheduler.clockOwner = "node";
  const badPath = path.join(tmpDir, "bad.graph.json");
  fs.writeFileSync(badPath, JSON.stringify(badGraph, null, 2));

  const run = spawnSync("python3", [scriptPath, badPath, tmpDir], {
    cwd: repoRoot,
    encoding: "utf8",
  });

  assert.equal(run.status, 1);
  const errors = JSON.parse(fs.readFileSync(path.join(tmpDir, "frame_scheduler_errors.json"), "utf8"));
  assert.equal(errors[0].code, "frame_scheduler.invalid_clock_owner");
});

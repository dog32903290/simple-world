const assert = require("node:assert/strict");
const { spawnSync } = require("node:child_process");
const fs = require("node:fs");
const os = require("node:os");
const path = require("node:path");
const test = require("node:test");

const repoRoot = path.resolve(__dirname, "..");
const contractPath = path.join(repoRoot, "docs/runtime/RENDER_GRAPH_CONTRACT.md");
const fixturePath = path.join(repoRoot, "docs/runtime/fixtures/render_graph_passes.graph.json");
const scriptPath = path.join(repoRoot, "docs/runtime/scripts/render_graph_shell.py");
const artifactDir = path.join(repoRoot, "docs/runtime/artifacts/render_graph");

test("RenderGraph contract names pass ordering and resource access boundaries", () => {
  const source = fs.readFileSync(contractPath, "utf8");

  assert.match(source, /RenderGraph answers:/);
  assert.match(source, /which passes run in this frame, in what order, and which resources do they read or write/);
  assert.match(source, /FrameScheduler -> RenderGraph -> RendererBackend/);
  assert.match(source, /Pass order must be deterministic and derived from declared dependencies/);
  assert.match(source, /RenderTargetWrite/);
  assert.match(source, /ShaderResourceRead/);
  assert.match(source, /FrameOutputRead/);
  assert.match(source, /resourceBarrier/);
  assert.match(source, /not a real GPU barrier/);
});

test("RenderGraph fixture declares a 4K multi-pass frame with explicit dependencies", () => {
  const graph = JSON.parse(fs.readFileSync(fixturePath, "utf8"));

  assert.equal(graph.graphId, "fixture.render_graph_passes");
  assert.deepEqual(graph.expected.passOrder, ["mainColorPass", "postFxPass", "publishFrame"]);
  assert.equal(graph.resources[0].resolution.width, 3840);
  assert.equal(graph.resources[0].resolution.height, 2160);
  assert.deepEqual(graph.passes[1].dependsOn, ["mainColorPass"]);
  assert.deepEqual(graph.passes[2].dependsOn, ["postFxPass"]);
  assert.deepEqual(graph.expected.resourceBarriers, [
    { resource: "main.color", before: "RenderTargetWrite", after: "ShaderResourceRead" },
    { resource: "postfx.color", before: "RenderTargetWrite", after: "FrameOutputRead" },
  ]);
});

test("RenderGraph shell emits pass plan, access ledger, and resource barriers", () => {
  const run = spawnSync("python3", [scriptPath, fixturePath, artifactDir], {
    cwd: repoRoot,
    encoding: "utf8",
  });

  assert.equal(run.status, 0, run.stderr);

  const trace = readArtifact("render_graph_trace.json");
  const passPlan = readArtifact("render_pass_plan.json");
  const ledger = readArtifact("resource_access_ledger.json");
  const errors = readArtifact("render_graph_errors.json");

  assert.deepEqual(errors, []);
  assert.equal(trace[0].op, "loadRenderGraph");
  assert.deepEqual(passPlan.passOrder, ["mainColorPass", "postFxPass", "publishFrame"]);
  assert.deepEqual(trace.filter((entry) => entry.op === "pass.begin").map((entry) => entry.passId), [
    "mainColorPass",
    "postFxPass",
    "publishFrame",
  ]);
  assert.deepEqual(trace.filter((entry) => entry.op === "resourceBarrier").map(({ resource, before, after }) => ({ resource, before, after })), [
    { resource: "main.color", before: "RenderTargetWrite", after: "ShaderResourceRead" },
    { resource: "postfx.color", before: "RenderTargetWrite", after: "FrameOutputRead" },
  ]);
  assert.equal(ledger.barrierCount, 2);
  assert.equal(ledger.latestAccess["main.color"], "ShaderResourceRead");
  assert.equal(ledger.latestAccess["postfx.color"], "FrameOutputRead");
  assert.equal(ledger.latestAccess["main.depth"], "DepthStencilWrite");
});

test("RenderGraph shell refuses missing resources instead of fabricating pass success", () => {
  const tmpDir = fs.mkdtempSync(path.join(os.tmpdir(), "render-graph-bad-"));
  const badPath = path.join(tmpDir, "bad.graph.json");
  const graph = JSON.parse(fs.readFileSync(fixturePath, "utf8"));
  graph.passes[1].reads[0].resource = "missing.color";
  fs.writeFileSync(badPath, JSON.stringify(graph, null, 2));

  const run = spawnSync("python3", [scriptPath, badPath, tmpDir], {
    cwd: repoRoot,
    encoding: "utf8",
  });

  assert.equal(run.status, 1);
  const errors = JSON.parse(fs.readFileSync(path.join(tmpDir, "render_graph_errors.json"), "utf8"));
  assert.equal(errors[0].code, "render_graph.missing_resource");
  assert.equal(errors[0].resource, "missing.color");
});

function readArtifact(name) {
  return JSON.parse(fs.readFileSync(path.join(artifactDir, name), "utf8"));
}

const assert = require("node:assert/strict");
const fs = require("node:fs");
const os = require("node:os");
const path = require("node:path");
const { spawnSync } = require("node:child_process");
const test = require("node:test");

const repoRoot = path.resolve(__dirname, "..");
const fixturePath = path.join(repoRoot, "docs/runtime/fixtures/main_clock_constant_pipeline.vuo_graph.json");
const creatorFixturePath = path.join(repoRoot, "docs/runtime/fixtures/creator_constant_blend_no_clock.graph.json");
const generatorPath = path.join(repoRoot, "docs/runtime/scripts/generate_vuo_main_clock_proof.py");
const generatedPath = path.join(repoRoot, "vuo-compositions/generated/myworld-main-clock-generated-proof.vuo");
const generatedCreatorPath = path.join(repoRoot, "vuo-compositions/generated/myworld-clockless-creator-generated-proof.vuo");
const frameSchedulerContractPath = path.join(repoRoot, "docs/runtime/FRAME_SCHEDULER_CONTRACT.md");

test("main-clock Vuo fixture declares frame-cooked ports without hand-authored event edges", () => {
  const fixture = JSON.parse(fs.readFileSync(fixturePath, "utf8"));
  const schedulerContract = fs.readFileSync(frameSchedulerContractPath, "utf8");

  assert.equal(fixture.kind, "vuoMainClockProofGraph");
  assert.equal(fixture.name, "myworld-main-clock-generated-proof");
  assert.deepEqual(fixture.clockedPorts, [
    { node: "ConstantBackground", port: "renderTick" },
    { node: "ConstantForeground", port: "renderTick" },
    { node: "Blend", port: "renderTick" },
    { node: "KeepPreviousFrame", port: "update" },
    { node: "RenderBlendWindow", port: "refresh" },
    { node: "ClearRenderTarget", port: "renderTick" },
    { node: "RenderClearWindow", port: "refresh" },
  ]);

  for (const edge of fixture.edges) {
    assert.notEqual(edge.from[0], "FireOnDisplayRefresh");
    assert.notEqual(edge.from[0], "MainClock");
  }

  assert.match(schedulerContract, /creator\s+graph data should not hand-author per-node clock edges/);
  assert.match(schedulerContract, /without making every source node run on its own private pulse/);
});

test("creator-layer Vuo fixture has no authored clock ports or clock edges", () => {
  const fixture = JSON.parse(fs.readFileSync(creatorFixturePath, "utf8"));

  assert.equal(fixture.kind, "myWorldCreatorGraph");
  assert.equal(fixture.name, "myworld-clockless-creator-generated-proof");
  assert.equal(fixture.clockedPorts, undefined);
  assert.equal(JSON.stringify(fixture.publicPorts), undefined);

  const publicPortText = fixture.nodes.flatMap((node) => [
    ...(node.publicPorts?.inputs || []),
    ...(node.publicPorts?.outputs || []),
  ]).join(" ");
  assert.doesNotMatch(publicPortText, /(^|\s)(renderTick|refresh|update|clockIn|HostTime)(\s|$)/);

  for (const edge of fixture.edges) {
    assert.notEqual(edge.from[0], "FireOnDisplayRefresh");
    assert.notEqual(edge.from[0], "MainClock");
    assert.doesNotMatch(edge.from.join("."), /renderTick|refresh|update/);
    assert.doesNotMatch(edge.to.join("."), /renderTick|refresh|update/);
  }
});

test("main-clock generator emits a Vuo composition with generated clock edges", () => {
  const tempPath = path.join(os.tmpdir(), `main-clock-generated-${process.pid}.vuo`);
  const result = spawnSync("python3", [generatorPath, fixturePath, "--output", tempPath], {
    cwd: repoRoot,
    encoding: "utf8",
  });

  assert.equal(result.status, 0, result.stderr);
  const report = JSON.parse(result.stdout);
  const source = fs.readFileSync(tempPath, "utf8");

  assert.equal(report.ok, true);
  assert.equal(report.clockedPortCount, 7);
  assert.match(source, /Generated My World main-clock Vuo proof/);
  assert.match(source, /MainClock \[type="my\.runtime\.clock\.mainClock"/);
  assert.match(source, /RenderBlendWindow \[type="vuo\.image\.render\.window2" version="4\.0\.0"/);
  assert.match(source, /FireOnDisplayRefresh:requestedFrame -> MainClock:HostTime/);
  assert.match(source, /MainClock:renderTick -> ConstantBackground:renderTick/);
  assert.match(source, /MainClock:renderTick -> Blend:renderTick/);
  assert.match(source, /MainClock:renderTick -> KeepPreviousFrame:update/);
  assert.match(source, /MainClock:renderTick -> RenderBlendWindow:refresh/);
  assert.match(source, /MainClock:renderTick -> ClearRenderTarget:renderTick/);
  assert.match(source, /ClearRenderTarget:ClearedColorBuffer -> RenderClearWindow:image/);
  assert.doesNotMatch(source, /FireOnDisplayRefresh:requestedFrame -> ConstantBackground:renderTick/);
  assert.doesNotMatch(source, /FireOnDisplayRefresh:requestedFrame -> Blend:renderTick/);
});

test("main-clock generator infers hidden clock edges from creator graph node types", () => {
  const tempPath = path.join(os.tmpdir(), `clockless-creator-generated-${process.pid}.vuo`);
  const result = spawnSync("python3", [generatorPath, creatorFixturePath, "--output", tempPath], {
    cwd: repoRoot,
    encoding: "utf8",
  });

  assert.equal(result.status, 0, result.stderr);
  const report = JSON.parse(result.stdout);
  const source = fs.readFileSync(tempPath, "utf8");

  assert.equal(report.ok, true);
  assert.equal(report.clockedPortsSource, "inferredFromNodeTypes");
  assert.equal(report.clockedPortCount, 5);
  assert.match(source, /creator graph owns only semantic edges; the generator infers MainClock edges/);
  assert.match(source, /MainClock:renderTick -> ConstantBackground:renderTick/);
  assert.match(source, /MainClock:renderTick -> ConstantForeground:renderTick/);
  assert.match(source, /MainClock:renderTick -> Blend:renderTick/);
  assert.match(source, /MainClock:renderTick -> KeepPreviousFrame:update/);
  assert.match(source, /MainClock:renderTick -> LivePreview:refresh/);
  assert.match(source, /KeepPreviousFrame:CurrentFrame -> LivePreview:image/);
  assert.doesNotMatch(source, /FireOnDisplayRefresh:requestedFrame -> LivePreview:refresh/);
});

test("checked-in generated main-clock proof stays in sync with the fixture", () => {
  const source = fs.readFileSync(generatedPath, "utf8");

  assert.match(source, /This file is generated by docs\/runtime\/scripts\/generate_vuo_main_clock_proof\.py/);
  assert.match(source, /MainClock:renderTick -> ConstantForeground:renderTick/);
  assert.match(source, /MainClock:renderTick -> RenderClearWindow:refresh/);
  assert.doesNotMatch(source, /FireOnDisplayRefresh:requestedFrame -> ClearRenderTarget:renderTick/);
});

test("checked-in clockless creator generated proof stays in sync with hidden clock inference", () => {
  const source = fs.readFileSync(generatedCreatorPath, "utf8");

  assert.match(source, /The creator graph owns only semantic edges; the generator infers MainClock edges from node type contracts/);
  assert.match(source, /MainClock:renderTick -> Blend:renderTick/);
  assert.match(source, /MainClock:renderTick -> LivePreview:refresh/);
  assert.match(source, /KeepPreviousFrame:CurrentFrame -> LivePreview:image/);
});

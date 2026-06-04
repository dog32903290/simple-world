const assert = require("node:assert/strict");
const { spawnSync } = require("node:child_process");
const fs = require("node:fs");
const os = require("node:os");
const path = require("node:path");
const test = require("node:test");

const repoRoot = path.resolve(__dirname, "..");
const contractPath = path.join(repoRoot, "docs/runtime/NATIVE_RENDER_PIPELINE_CONTRACT.md");
const fixturePath = path.join(repoRoot, "docs/runtime/fixtures/native_render_pipeline.graph.json");
const scriptPath = path.join(repoRoot, "docs/runtime/scripts/native_render_pipeline_shell.py");
const artifactDir = path.join(repoRoot, "docs/runtime/artifacts/native_render_pipeline");

test("NativeRenderPipeline contract connects uniform binding, shader program, and native backend", () => {
  const source = fs.readFileSync(contractPath, "utf8");

  assert.match(source, /NativeRenderPipeline answers:/);
  assert.match(source, /ResourceLifetime -> CommandStream -> ShaderUniformBinding -> ShaderProgram -> NativeRendererBackend -> CapturedFrame/);
  assert.match(source, /resource_lifetime_shell\.py/);
  assert.match(source, /command_stream_pipeline_shell\.py/);
  assert.match(source, /shader_uniform_binding_shell\.py/);
  assert.match(source, /shader_program_shell\.py/);
  assert.match(source, /native_renderer_backend_interface_shell\.py/);
  assert.match(source, /CapturedFrame\.nonBlackSample == true/);
  assert.match(source, /without hidden UI state/);
});

test("NativeRenderPipeline fixture points at the three existing layer fixtures", () => {
  const graph = JSON.parse(fs.readFileSync(fixturePath, "utf8"));

  assert.equal(graph.graphId, "fixture.native_render_pipeline");
  assert.equal(graph.shaderUniformBindingFixture, "docs/runtime/fixtures/shader_uniform_binding.graph.json");
  assert.equal(graph.shaderProgramFixture, "docs/runtime/fixtures/shader_program_contract.graph.json");
  assert.equal(graph.renderGraphFixture, "docs/runtime/fixtures/render_graph_passes.graph.json");
  assert.equal(graph.resourceLifetimeFixture, "docs/runtime/fixtures/resource_lifetime.graph.json");
  assert.equal(graph.commandStreamFixture, "docs/runtime/fixtures/command_stream_pipeline.graph.json");
  assert.equal(graph.nativeBackendFixture, "docs/runtime/fixtures/native_renderer_backend_interface.graph.json");
  assert.equal(graph.expected.targetProgramId, "program.sphere_sdf_raymarch.fragment");
  assert.equal(graph.expected.loudness, 0.37);
  assert.equal(graph.expected.resourceLifetimeOk, true);
});

test("NativeRenderPipeline shell emits one connected headless frame proof", () => {
  const run = spawnSync("python3", [scriptPath, fixturePath, artifactDir], {
    cwd: repoRoot,
    encoding: "utf8",
  });

  assert.equal(run.status, 0, run.stderr || run.stdout);

  const summary = readArtifact("pipeline_summary.json");
  const trace = readArtifact("pipeline_trace.json");
  const frameInput = readArtifact("render_frame_input.json");
  const renderPassPlan = readArtifact("render_pass_plan.json");
  const resourceAccessLedger = readArtifact("resource_access_ledger.json");
  const resourceRegistry = readArtifact("resource_registry.json");
  const invalidationLedger = readArtifact("view_invalidation_ledger.json");
  const commandSummary = readArtifact("command_stream_summary.json");
  const commandResult = readArtifact("command_stream_result.json");
  const capturedFrame = readArtifact("captured_frame_contract.json");
  const errors = readArtifact("native_render_pipeline_errors.json");

  assert.deepEqual(errors, []);
  assert.equal(summary.kind, "NativeRenderPipelineProof");
  assert.equal(summary.ok, true);
  assert.equal(summary.layers.shaderUniformBinding, "ready");
  assert.equal(summary.layers.shaderProgram, "ok");
  assert.equal(summary.layers.renderGraph, "ok");
  assert.equal(summary.layers.resourceLifetime, "ok");
  assert.equal(summary.layers.commandStream, "ok");
  assert.equal(summary.layers.nativeBackend, "rendered");
  assert.equal(summary.layers.capturedFrame, "captured");
  assert.equal(summary.liveTextureCount, 3);
  assert.deepEqual(summary.renderGraphPassOrder, ["mainColorPass", "postFxPass", "publishFrame"]);
  assert.equal(summary.renderGraphBarrierCount, 2);
  assert.equal(summary.invalidatedViewCount, 0);
  assert.equal(summary.drawCalls, 1);
  assert.equal(summary.targetProgramId, "program.sphere_sdf_raymarch.fragment");
  assert.equal(summary.loudness, 0.37);
  assert.equal(summary.importsOldUi, false);
  assert.equal(summary.nonBlackSample, true);

  assert.equal(frameInput.sourceSnapshotOk, true);
  assert.equal(frameInput.loudness, 0.37);
  assert.deepEqual(renderPassPlan.passOrder, ["mainColorPass", "postFxPass", "publishFrame"]);
  assert.equal(resourceAccessLedger.barrierCount, 2);
  assert.equal(resourceRegistry.resources["main.color"].disposed, false);
  assert.equal(resourceRegistry.resources["main.color"].width, 3840);
  assert.equal(resourceRegistry.resources["main.depth"].disposed, false);
  assert.equal(resourceRegistry.resources["postfx.color"].disposed, false);
  assert.equal(resourceRegistry.views["main.color.rtv"].ok, true);
  assert.equal(resourceRegistry.views["main.depth.dsv"].ok, true);
  assert.deepEqual(invalidationLedger.invalidatedViews, []);
  assert.equal(commandSummary.ok, true);
  assert.equal(commandSummary.drawCalls, 1);
  assert.equal(commandResult.ok, true);
  assert.equal(commandResult.stats.drawCalls, 1);
  assert.equal(capturedFrame.nonBlackSample, true);
  assert.deepEqual(trace.map((entry) => entry.op), [
    "loadNativeRenderPipeline",
    "renderGraph.begin",
    "renderGraph.end",
    "resourceLifetime.begin",
    "resourceLifetime.end",
    "commandStream.begin",
    "commandStream.end",
    "uniform.begin",
    "uniform.end",
    "shaderProgram.begin",
    "shaderProgram.end",
    "nativeBackend.begin",
    "nativeBackend.end",
    "publishNativeRenderPipelineArtifacts",
  ]);
});

test("NativeRenderPipeline shell refuses program mismatches instead of rendering hidden glue", () => {
  const tmpDir = fs.mkdtempSync(path.join(os.tmpdir(), "native-pipeline-bad-"));
  const badUniformFixture = path.join(tmpDir, "bad-uniform.graph.json");
  const badPipelineFixture = path.join(tmpDir, "bad-pipeline.graph.json");

  const uniform = JSON.parse(fs.readFileSync(path.join(repoRoot, "docs/runtime/fixtures/shader_uniform_binding.graph.json"), "utf8"));
  uniform.targetProgram.programId = "program.other";
  fs.writeFileSync(badUniformFixture, JSON.stringify(uniform, null, 2));

  const pipeline = JSON.parse(fs.readFileSync(fixturePath, "utf8"));
  pipeline.shaderUniformBindingFixture = badUniformFixture;
  fs.writeFileSync(badPipelineFixture, JSON.stringify(pipeline, null, 2));

  const run = spawnSync("python3", [scriptPath, badPipelineFixture, tmpDir], {
    cwd: repoRoot,
    encoding: "utf8",
  });

  assert.equal(run.status, 1);
  const summary = JSON.parse(fs.readFileSync(path.join(tmpDir, "pipeline_summary.json"), "utf8"));
  const errors = JSON.parse(fs.readFileSync(path.join(tmpDir, "native_render_pipeline_errors.json"), "utf8"));

  assert.equal(summary.ok, false);
  assert.equal(errors[0].code, "native_render_pipeline.program_mismatch");
  assert.equal(errors[0].uniformTargetProgramId, "program.other");
  assert.equal(errors[0].shaderProgramId, "program.sphere_sdf_raymarch.fragment");
});

function readArtifact(name) {
  return JSON.parse(fs.readFileSync(path.join(artifactDir, name), "utf8"));
}

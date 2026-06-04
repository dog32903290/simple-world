const assert = require("node:assert/strict");
const { spawnSync } = require("node:child_process");
const fs = require("node:fs");
const os = require("node:os");
const path = require("node:path");
const test = require("node:test");

const repoRoot = path.resolve(__dirname, "..");
const fixturePath = path.join(repoRoot, "docs/runtime/fixtures/command_stream_pipeline.graph.json");
const resourceFixturePath = path.join(repoRoot, "docs/runtime/fixtures/resource_lifetime.graph.json");
const renderGraphFixturePath = path.join(repoRoot, "docs/runtime/fixtures/render_graph_passes.graph.json");
const materialFixturePath = path.join(repoRoot, "docs/runtime/fixtures/material_pbr_scope.graph.json");
const resourceScriptPath = path.join(repoRoot, "docs/runtime/scripts/resource_lifetime_shell.py");
const renderGraphScriptPath = path.join(repoRoot, "docs/runtime/scripts/render_graph_shell.py");
const materialScriptPath = path.join(repoRoot, "docs/runtime/scripts/material_pbr_scope_shell.py");
const scriptPath = path.join(repoRoot, "docs/runtime/scripts/command_stream_pipeline_shell.py");
const artifactDir = path.join(repoRoot, "docs/runtime/artifacts/command_stream_pipeline");

function runResourceLifetime(outDir) {
  const run = spawnSync("python3", [resourceScriptPath, resourceFixturePath, outDir], {
    cwd: repoRoot,
    encoding: "utf8",
  });
  assert.equal(run.status, 0, run.stderr || run.stdout);
  return path.join(outDir, "resource_registry.json");
}

function runRenderGraph(outDir) {
  const run = spawnSync("python3", [renderGraphScriptPath, renderGraphFixturePath, outDir], {
    cwd: repoRoot,
    encoding: "utf8",
  });
  assert.equal(run.status, 0, run.stderr || run.stdout);
  return path.join(outDir, "render_pass_plan.json");
}

function runMaterialPbr(outDir) {
  const run = spawnSync("python3", [materialScriptPath, materialFixturePath, outDir], {
    cwd: repoRoot,
    encoding: "utf8",
  });
  assert.equal(run.status, 0, run.stderr || run.stdout);
  return path.join(outDir, "mesh_pbr_draw_command.json");
}

test("CommandStreamPipeline fixture declares a mesh draw command against ResourceLifetime views", () => {
  const graph = JSON.parse(fs.readFileSync(fixturePath, "utf8"));

  assert.equal(graph.graphId, "fixture.command_stream_pipeline");
  assert.equal(graph.renderGraphBinding.passId, "mainColorPass");
  assert.equal(graph.renderGraphBinding.command, "commandStream");
  assert.equal(graph.renderTarget.colorViewId, "main.color.rtv");
  assert.equal(graph.command.topology, "TriangleList");
  assert.equal(graph.command.vertexShaderEntry, "vsMain");
  assert.equal(graph.command.pixelShaderEntry, "psMain");
  assert.deepEqual(graph.command.commandOps, ["inputAssembler", "shaderStage", "rasterizer", "outputMerger", "draw"]);
});

test("CommandStreamPipeline shell executes draw command using ResourceLifetime view identity", () => {
  const registryPath = runResourceLifetime(path.join(artifactDir, "resource_lifetime"));
  const renderPassPlanPath = runRenderGraph(path.join(artifactDir, "render_graph"));
  const run = spawnSync("python3", [scriptPath, fixturePath, registryPath, artifactDir, renderPassPlanPath], {
    cwd: repoRoot,
    encoding: "utf8",
  });

  assert.equal(run.status, 0, run.stderr || run.stdout);

  const summary = readArtifact("command_stream_summary.json");
  const result = readArtifact("command_stream_result.json");
  const errors = readArtifact("command_stream_pipeline_errors.json");

  assert.deepEqual(errors, []);
  assert.equal(summary.kind, "CommandStreamPipelineProof");
  assert.equal(summary.ok, true);
  assert.equal(summary.renderGraphPassId, "mainColorPass");
  assert.equal(summary.colorViewId, "main.color.rtv");
  assert.equal(summary.drawCalls, 1);
  assert.equal(summary.triangles, 1);
  assert.equal(result.ok, true);
  assert.equal(result.stats.drawCalls, 1);
  assert.equal(result.trace.find((entry) => entry.op === "bindOutputMerger").renderTargetViews[0].type, "RTV");
  assert.equal(result.trace.find((entry) => entry.op === "bindOutputMerger").renderTargetViews[0].textureId, "main.color");
  assert.equal(result.trace.find((entry) => entry.op === "draw").renderTargetViews[0].textureId, "main.color");
});

test("CommandStreamPipeline shell can execute a Material/PBR draw command artifact", () => {
  const tmpDir = fs.mkdtempSync(path.join(os.tmpdir(), "command-stream-material-"));
  const registryPath = runResourceLifetime(path.join(tmpDir, "resource_lifetime"));
  const renderPassPlanPath = runRenderGraph(path.join(tmpDir, "render_graph"));
  const drawCommandPath = runMaterialPbr(path.join(tmpDir, "material_pbr"));
  const run = spawnSync("python3", [scriptPath, fixturePath, registryPath, tmpDir, renderPassPlanPath, drawCommandPath], {
    cwd: repoRoot,
    encoding: "utf8",
  });

  assert.equal(run.status, 0, run.stderr || run.stdout);
  const summary = JSON.parse(fs.readFileSync(path.join(tmpDir, "command_stream_summary.json"), "utf8"));
  const result = JSON.parse(fs.readFileSync(path.join(tmpDir, "command_stream_result.json"), "utf8"));
  const shaderStage = result.trace.find((entry) => entry.op === "bindShaderStage");

  assert.equal(summary.ok, true);
  assert.equal(summary.commandSource, "drawCommandArtifact");
  assert.equal(summary.selectedMaterialId, "glass");
  assert.equal(summary.drawCalls, 1);
  assert.ok(shaderStage.constantBuffers.includes("pbr:glass"));
  assert.ok(shaderStage.shaderResources.includes("studio_small_08_prefiltered"));
});

test("CommandStreamPipeline shell refuses RenderGraph pass plans that do not expose commandStream color writes", () => {
  const tmpDir = fs.mkdtempSync(path.join(os.tmpdir(), "command-stream-render-graph-bad-"));
  const registryPath = runResourceLifetime(path.join(tmpDir, "resource_lifetime"));
  const renderGraphDir = path.join(tmpDir, "render_graph");
  const planPath = runRenderGraph(renderGraphDir);
  const plan = JSON.parse(fs.readFileSync(planPath, "utf8"));
  plan.passes.find((row) => row.passId === "mainColorPass").commands = ["clear"];
  const badPlanPath = path.join(tmpDir, "bad-render-pass-plan.json");
  fs.writeFileSync(badPlanPath, JSON.stringify(plan, null, 2));

  const run = spawnSync("python3", [scriptPath, fixturePath, registryPath, tmpDir, badPlanPath], {
    cwd: repoRoot,
    encoding: "utf8",
  });

  assert.equal(run.status, 1);
  const errors = JSON.parse(fs.readFileSync(path.join(tmpDir, "command_stream_pipeline_errors.json"), "utf8"));
  assert.equal(errors[0].code, "command_stream_pipeline.render_graph_command_missing");
  assert.equal(errors[0].passId, "mainColorPass");
});

test("CommandStreamPipeline shell refuses missing pixel shader stage before pipeline accepts draw", () => {
  const tmpDir = fs.mkdtempSync(path.join(os.tmpdir(), "command-stream-pipeline-bad-"));
  const registryPath = runResourceLifetime(path.join(tmpDir, "resource_lifetime"));
  const badPath = path.join(tmpDir, "bad-command.graph.json");
  const graph = JSON.parse(fs.readFileSync(fixturePath, "utf8"));
  graph.command.pixelShaderEntry = null;
  fs.writeFileSync(badPath, JSON.stringify(graph, null, 2));

  const run = spawnSync("python3", [scriptPath, badPath, registryPath, tmpDir], {
    cwd: repoRoot,
    encoding: "utf8",
  });

  assert.equal(run.status, 1);
  const summary = JSON.parse(fs.readFileSync(path.join(tmpDir, "command_stream_summary.json"), "utf8"));
  const errors = JSON.parse(fs.readFileSync(path.join(tmpDir, "command_stream_pipeline_errors.json"), "utf8"));
  assert.equal(summary.ok, false);
  assert.equal(errors[0].code, "command_stream.missing_shader_stage");
});

function readArtifact(name) {
  return JSON.parse(fs.readFileSync(path.join(artifactDir, name), "utf8"));
}

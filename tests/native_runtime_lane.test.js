const assert = require("node:assert/strict");
const { spawnSync } = require("node:child_process");
const fs = require("node:fs");
const path = require("node:path");
const test = require("node:test");

const repoRoot = path.resolve(__dirname, "..");
const contractPath = path.join(repoRoot, "docs/runtime/NATIVE_RUNTIME_LANE.md");
const rendererBackendContractPath = path.join(repoRoot, "docs/runtime/RENDERER_BACKEND_CONTRACT.md");
const fixturePath = path.join(repoRoot, "docs/runtime/fixtures/native_runtime_lane.graph.json");
const scriptPath = path.join(repoRoot, "docs/runtime/scripts/native_runtime_lane.py");
const artifactDir = path.join(repoRoot, "docs/runtime/artifacts/native_runtime_lane");

test("Native runtime lane contract names the first Command -> RenderTarget -> TextureView -> frame path", () => {
  const source = fs.readFileSync(contractPath, "utf8");
  const backendSource = fs.readFileSync(rendererBackendContractPath, "utf8");

  assert.match(source, /NativeRuntimeLane := graph fixture -> command stream -> render target resources -> texture views -> renderer artifact/);
  assert.match(source, /CommandStream/);
  assert.match(source, /RenderTarget/);
  assert.match(source, /TextureView identity/);
  assert.match(source, /native_runtime_lane\.py/);
  assert.match(source, /resource_registry\.json/);
  assert.match(source, /texture_views\.json/);
  assert.match(source, /native_runtime_lane_trace\.json/);
  assert.match(source, /not a Metal or DX11 backend/);
  assert.match(source, /RENDERER_BACKEND_CONTRACT\.md/);
  assert.match(source, /`softwareProof` backend role/);
  assert.match(backendSource, /RendererBackend answers:/);
});

test("Native runtime lane fixture is a replayable text graph with explicit stages", () => {
  const graph = JSON.parse(fs.readFileSync(fixturePath, "utf8"));

  assert.equal(graph.graphId, "fixture.native_runtime_lane");
  assert.deepEqual(graph.pipeline, [
    "materialPbrScope",
    "commandStream",
    "renderTargetResources",
    "textureViewIdentity",
    "nativeRenderer",
  ]);
  assert.equal(graph.inputs.materialGraph, "docs/runtime/fixtures/material_pbr_scope.graph.json");
  assert.equal(graph.renderTarget.id, "mainRenderTarget");
  assert.deepEqual(graph.renderTarget.resolution, { width: 320, height: 180 });
  assert.equal(graph.renderTarget.colorBuffer.id, "rt.color");
  assert.equal(graph.renderTarget.depthBuffer.id, "rt.depth");
});

test("Native runtime lane emits replay trace, resource registry, texture views, and nonblack frame", () => {
  const run = spawnSync("python3", [scriptPath, fixturePath, artifactDir], {
    cwd: repoRoot,
    encoding: "utf8",
  });

  assert.equal(run.status, 0, run.stderr);

  const trace = JSON.parse(fs.readFileSync(path.join(artifactDir, "native_runtime_lane_trace.json"), "utf8"));
  const errors = JSON.parse(fs.readFileSync(path.join(artifactDir, "native_runtime_lane_errors.json"), "utf8"));
  const registry = JSON.parse(fs.readFileSync(path.join(artifactDir, "resource_registry.json"), "utf8"));
  const views = JSON.parse(fs.readFileSync(path.join(artifactDir, "texture_views.json"), "utf8"));
  const stats = JSON.parse(fs.readFileSync(path.join(artifactDir, "native_renderer/frame_stats.json"), "utf8"));
  const frame = fs.readFileSync(path.join(artifactDir, "native_renderer/frame.ppm"), "utf8");

  assert.deepEqual(trace.map((entry) => entry.op), [
    "loadGraph",
    "runMaterialPbrScope",
    "commandStream.accept",
    "renderTarget.allocate",
    "textureView.create",
    "commandStream.execute",
    "runNativeRenderer",
    "publishArtifacts",
  ]);
  assert.deepEqual(errors, []);
  assert.equal(trace[2].command.selectedMaterialId, "glass");
  assert.equal(trace[3].resourceId, "mainRenderTarget");
  assert.equal(trace[5].result.ok, true);
  assert.equal(trace[5].result.stats.drawCalls, 1);
  const bindInputAssembler = trace[5].result.trace.find((entry) => entry.op === "bindInputAssembler");
  assert.ok(bindInputAssembler);
  assert.equal(bindInputAssembler.topology, "TriangleList");
  assert.deepEqual(bindInputAssembler.vertexBuffer, { buffer: "cube.vertexBuffer", srv: "cube.vertexSrv" });
  assert.deepEqual(bindInputAssembler.indexBuffer, { buffer: "cube.indexBuffer", srv: "cube.indexSrv" });
  const bindShaderStage = trace[5].result.trace.find((entry) => entry.op === "bindShaderStage");
  assert.ok(bindShaderStage);
  assert.equal(bindShaderStage.vertexShader, "vsMain");
  assert.equal(bindShaderStage.pixelShader, "psMain");
  assert.ok(bindShaderStage.constantBuffers.includes("pbr:glass"));
  assert.equal(bindShaderStage.shaderResources.length, 6);
  const bindRasterizer = trace[5].result.trace.find((entry) => entry.op === "bindRasterizer");
  assert.ok(bindRasterizer);
  assert.equal(bindRasterizer.rasterizerState.culling, "Back");
  assert.deepEqual(bindRasterizer.viewports[0], { x: 0, y: 0, width: 320, height: 180, minDepth: 0, maxDepth: 1 });
  const bindOutputMerger = trace[5].result.trace.find((entry) => entry.op === "bindOutputMerger");
  assert.ok(bindOutputMerger);
  assert.equal(bindOutputMerger.blendState, "opaque");
  assert.equal(bindOutputMerger.depthStencilState, "defaultDepth");
  assert.deepEqual(bindOutputMerger.blendFactor, [1, 1, 1, 1]);
  assert.equal(trace[5].result.finalState.vertexShader, null);
  assert.equal(trace[5].result.finalState.pixelShader, null);
  assert.equal(trace[5].result.finalState.rasterizerState, null);
  assert.deepEqual(trace[5].result.finalState.viewports, []);
  assert.equal(trace[5].result.finalState.blendState, null);
  assert.equal(trace[5].result.finalState.depthStencilState, null);
  assert.deepEqual(trace[5].result.finalState.constantBuffers, []);
  assert.equal(trace[6].frameStats.nonblackPixels, stats.nonblackPixels);

  assert.equal(registry.resources["rt.color"].kind, "Texture2D");
  assert.deepEqual(registry.resources["rt.color"].bindFlags, ["RenderTarget", "ShaderResource"]);
  assert.deepEqual(registry.resources["rt.depth"].bindFlags, ["DepthStencil", "ShaderResource"]);

  assert.equal(views.views["rt.color.srv"].ok, true);
  assert.equal(views.views["rt.color.rtv"].ok, true);
  assert.equal(views.views["rt.color.uav"].ok, false);
  assert.equal(views.views["rt.depth.srv"].format, "R32_Float");
  assert.equal(views.views["rt.depth.dsv"].ok, true);

  assert.ok(stats.nonblackPixels > 0);
  assert.match(frame, /^P3\n320 180\n255\n/);
});

test("Native runtime lane refuses missing command output instead of fabricating resources", () => {
  const tmpDir = fs.mkdtempSync(path.join(require("node:os").tmpdir(), "native-lane-bad-"));
  const badGraphPath = path.join(tmpDir, "bad.graph.json");
  fs.writeFileSync(badGraphPath, JSON.stringify({
    version: "0.1",
    graphId: "fixture.native_runtime_lane.bad",
    pipeline: ["materialPbrScope", "commandStream", "renderTargetResources", "textureViewIdentity", "nativeRenderer"],
    inputs: {
      materialGraph: "docs/runtime/fixtures/missing_material.graph.json",
    },
    renderTarget: {
      id: "mainRenderTarget",
      resolution: { width: 320, height: 180 },
      colorBuffer: { id: "rt.color" },
      depthBuffer: { id: "rt.depth" },
    },
  }, null, 2));

  const run = spawnSync("python3", [scriptPath, badGraphPath, tmpDir], {
    cwd: repoRoot,
    encoding: "utf8",
  });

  assert.equal(run.status, 1);
  const errors = JSON.parse(fs.readFileSync(path.join(tmpDir, "native_runtime_lane_errors.json"), "utf8"));
  const registry = JSON.parse(fs.readFileSync(path.join(tmpDir, "resource_registry.json"), "utf8"));

  assert.equal(errors[0].code, "native_lane.material_scope_failed");
  assert.deepEqual(registry.resources, {});
});

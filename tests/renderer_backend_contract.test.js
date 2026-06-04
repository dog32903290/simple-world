const assert = require("node:assert/strict");
const { spawnSync } = require("node:child_process");
const fs = require("node:fs");
const os = require("node:os");
const path = require("node:path");
const test = require("node:test");

const repoRoot = path.resolve(__dirname, "..");
const contractPath = path.join(repoRoot, "docs/runtime/RENDERER_BACKEND_CONTRACT.md");
const renderGraphContractPath = path.join(repoRoot, "docs/runtime/RENDER_GRAPH_CONTRACT.md");
const fixturePath = path.join(repoRoot, "docs/runtime/fixtures/renderer_backend_contract.graph.json");
const scriptPath = path.join(repoRoot, "docs/runtime/scripts/renderer_backend_shell.py");
const artifactDir = path.join(repoRoot, "docs/runtime/artifacts/renderer_backend");

test("RendererBackend contract separates backend capability truth from node vocabulary", () => {
  const source = fs.readFileSync(contractPath, "utf8");
  const renderGraphSource = fs.readFileSync(renderGraphContractPath, "utf8");

  assert.match(source, /RendererBackend answers:/);
  assert.match(source, /which host\/backend can execute this render contract, and which capabilities are real/);
  assert.match(source, /CreatorGraph -> FrameScheduler -> RenderGraph -> RendererBackend -> FrameOutput/);
  assert.match(source, /softwareProof := deterministic artifact backend/);
  assert.match(source, /vuoHost := visible body-layer host/);
  assert.match(source, /webgl2ShaderProbe := shader compile pressure only/);
  assert.match(source, /metalNative := future low-latency native GPU backend/);
  assert.match(source, /supportsTextureViews/);
  assert.match(source, /supportsCommandStream/);
  assert.match(source, /4K output is a\s+capability check/);
  assert.match(source, /RENDER_GRAPH_CONTRACT\.md/);
  assert.match(source, /RenderGraph` owns pass\s+order, reads\/writes, and resource hazard visibility/);
  assert.match(renderGraphSource, /RenderGraph answers:/);
  assert.match(source, /prevents Vuo,\s+WebGL2, software proof, and future native GPU backends from being confused/);
});

test("RendererBackend fixture declares backend candidates and 4K frame contract", () => {
  const graph = JSON.parse(fs.readFileSync(fixturePath, "utf8"));

  assert.equal(graph.graphId, "fixture.renderer_backend_contract");
  assert.deepEqual(graph.requestedRenderContract.resolution, { width: 3840, height: 2160 });
  assert.equal(graph.requestedRenderContract.frameOutput.kind, "fileFrame");
  assert.equal(graph.requestedRenderContract.frameOutput.requires4k, true);

  const backends = Object.fromEntries(graph.backends.map((backend) => [backend.id, backend]));
  assert.equal(backends.softwareProof.capabilities.supportsCommandStream, true);
  assert.equal(backends.softwareProof.capabilities.supportsFrameArtifact, true);
  assert.equal(backends.softwareProof.capabilities.supports4kOutput, true);
  assert.equal(backends.vuoHost.capabilities.supportsWindowOutput, true);
  assert.equal(backends.vuoHost.capabilities.supportsCommandStream, false);
  assert.equal(backends.vuoHost.capabilities.supportsTextureViews, false);
  assert.equal(backends.webgl2ShaderProbe.capabilities.supportsShaderCompile, true);
  assert.equal(backends.webgl2ShaderProbe.capabilities.supportsCommandStream, false);
  assert.equal(backends.metalNative.status, "planned");
});

test("RendererBackend shell emits capability, selection, pass, resource, and output artifacts", () => {
  const run = spawnSync("python3", [scriptPath, fixturePath, artifactDir], {
    cwd: repoRoot,
    encoding: "utf8",
  });

  assert.equal(run.status, 0, run.stderr);

  const capabilities = readArtifact("backend_capabilities.json");
  const selection = readArtifact("backend_selection.json");
  const passPlan = readArtifact("render_pass_plan.json");
  const resourceLifetime = readArtifact("resource_lifetime_plan.json");
  const frameOutput = readArtifact("frame_output_contract.json");
  const errors = readArtifact("renderer_backend_errors.json");

  assert.deepEqual(errors, []);
  assert.equal(capabilities.backends.softwareProof.capabilities.maxTextureSize, 8192);
  assert.equal(capabilities.backends.vuoHost.capabilities.supportsTextureViews, false);
  assert.equal(selection.selectedBackend, "softwareProof");
  assert.equal(selection.candidates.find((candidate) => candidate.backendId === "softwareProof").ok, true);
  assert.deepEqual(selection.candidates.find((candidate) => candidate.backendId === "vuoHost").missing, [
    "supportsCommandStream",
    "supportsFrameArtifact",
  ]);
  assert.ok(selection.candidates.find((candidate) => candidate.backendId === "webgl2ShaderProbe").missing.includes("supportsOffscreenRenderTarget"));
  assert.ok(selection.candidates.find((candidate) => candidate.backendId === "metalNative").missing.includes("backend not available"));

  assert.deepEqual(passPlan.passOrder, ["mainColorPass", "publishFrame"]);
  assert.equal(passPlan.backendId, "softwareProof");
  assert.equal(resourceLifetime.resources[0].resourceId, "mainRenderTarget.color");
  assert.equal(resourceLifetime.resources[0].role, "ColorBuffer");
  assert.equal(resourceLifetime.resources[0].lifetime, "frame");
  assert.deepEqual(resourceLifetime.resources[0].resolution, { width: 3840, height: 2160 });
  assert.equal(resourceLifetime.resources[1].role, "DepthBuffer");
  assert.equal(frameOutput.backendId, "softwareProof");
  assert.equal(frameOutput.frameOutput.kind, "fileFrame");
  assert.equal(frameOutput.publishBoundary, "FrameOutput");
});

test("RendererBackend shell refuses impossible 4K command-stream backend instead of fabricating parity", () => {
  const tmpDir = fs.mkdtempSync(path.join(os.tmpdir(), "renderer-backend-bad-"));
  const badPath = path.join(tmpDir, "bad.graph.json");
  const graph = JSON.parse(fs.readFileSync(fixturePath, "utf8"));
  graph.backends = graph.backends.map((backend) => (
    backend.id === "softwareProof"
      ? { ...backend, status: "disabled" }
      : backend
  ));
  fs.writeFileSync(badPath, JSON.stringify(graph, null, 2));

  const run = spawnSync("python3", [scriptPath, badPath, tmpDir], {
    cwd: repoRoot,
    encoding: "utf8",
  });

  assert.equal(run.status, 1);
  const errors = JSON.parse(fs.readFileSync(path.join(tmpDir, "renderer_backend_errors.json"), "utf8"));
  const selection = JSON.parse(fs.readFileSync(path.join(tmpDir, "backend_selection.json"), "utf8"));

  assert.equal(selection.selectedBackend, null);
  assert.equal(errors[0].code, "renderer_backend.no_backend_satisfies_required_capabilities");
  assert.ok(selection.candidates.find((candidate) => candidate.backendId === "vuoHost").missing.includes("supportsCommandStream"));
});

function readArtifact(name) {
  return JSON.parse(fs.readFileSync(path.join(artifactDir, name), "utf8"));
}

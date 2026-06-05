const assert = require("node:assert/strict");
const { spawnSync } = require("node:child_process");
const fs = require("node:fs");
const os = require("node:os");
const path = require("node:path");
const test = require("node:test");

const repoRoot = path.resolve(__dirname, "..");
const contractPath = path.join(repoRoot, "docs/runtime/NATIVE_TEXTURE_PATCH_PRODUCT_RUNTIME_PROOF.md");
const fixturePath = path.join(repoRoot, "docs/runtime/fixtures/native_texture_patch_product_runtime.graph.json");
const scriptPath = path.join(repoRoot, "docs/runtime/scripts/native_texture_patch_product_runtime_shell.py");
const artifactDir = path.join(repoRoot, "docs/runtime/artifacts/native_texture_patch_product_runtime");

test("NativeTexturePatchProductRuntime contract extends beyond ConstantImage Blob BlendImages", () => {
  const source = fs.readFileSync(contractPath, "utf8");

  assert.match(source, /NativeTexturePatchProductRuntimeProof answers:/);
  assert.match(source, /Gradient -> Feedback -> RenderTarget/);
  assert.match(source, /Gradient/);
  assert.match(source, /Feedback/);
  assert.match(source, /RenderTarget/);
  assert.match(source, /not a complete texture runtime/);
  assert.match(source, /not a complete shader language/);
});

test("NativeTexturePatchProductRuntime fixture is command-authored for gradient feedback render target", () => {
  const graph = JSON.parse(fs.readFileSync(fixturePath, "utf8"));

  assert.equal(graph.graphId, "fixture.native_texture_patch_product_runtime");
  assert.equal(graph.scheduler.clockOwner, "graph");
  assert.deepEqual(graph.expected.cookOrder, ["gradient_1", "feedback_1", "render_target_1", "output_1"]);
  assert.ok(graph.commands.some((command) => command.op === "createNode" && command.type === "image.generate.gradient"));
  assert.ok(graph.commands.some((command) => command.op === "createNode" && command.type === "image.memory.feedback"));
  assert.ok(graph.commands.some((command) => command.op === "createNode" && command.type === "image.output.renderTarget"));
  assert.ok(graph.commands.every((command) => ["createNode", "setParam", "connect"].includes(command.op)));
});

test("NativeTexturePatchProductRuntime shell emits Metal gradient feedback render target proof artifacts", () => {
  const tmpDir = fs.mkdtempSync(path.join(os.tmpdir(), "native-texture-product-"));
  const run = spawnSync("python3", [scriptPath, fixturePath, tmpDir], {
    cwd: repoRoot,
    encoding: "utf8",
  });

  assert.equal(run.status, 0, run.stderr || run.stdout);

  const result = readArtifact(tmpDir, "native_texture_patch_product_runtime_result.json");
  const runtimeGraph = readArtifact(tmpDir, "runtime_graph.json");
  const resourceLedger = readArtifact(tmpDir, "resource_ledger.json");
  const shaderIr = readArtifact(tmpDir, "shader_ir.json");
  const shaderCache = readArtifact(tmpDir, "shader_cache.json");
  const frameStats = readArtifact(tmpDir, "frame_stats.json");
  const errors = readArtifact(tmpDir, "native_texture_patch_product_runtime_errors.json");
  const generatedSource = fs.readFileSync(path.join(tmpDir, "generated_texture_patch.metal"), "utf8");

  assert.deepEqual(errors, []);
  assert.equal(result.kind, "NativeTexturePatchProductRuntimeProof");
  assert.equal(result.ok, true);
  assert.equal(result.status, "rendered");
  assert.equal(result.claims.commandGraphReplayed, true);
  assert.equal(result.claims.runtimeGraphBuilt, true);
  assert.equal(result.claims.gradientMetalPass, true);
  assert.equal(result.claims.feedbackMetalPass, true);
  assert.equal(result.claims.renderTargetMetalPass, true);
  assert.equal(result.claims.actualMetalRan, true);
  assert.equal(result.claims.completeTextureRuntime, false);
  assert.equal(result.claims.completeShaderLanguage, false);

  assert.deepEqual(runtimeGraph.cookOrder, ["gradient_1", "feedback_1", "render_target_1", "output_1"]);
  assert.deepEqual(resourceLedger.renderPasses.map((pass) => pass.nodeId), ["gradient_1", "feedback_1", "render_target_1"]);
  assert.deepEqual(resourceLedger.renderPasses[1].reads, ["gradient_1.texture", "feedback_1.history"]);
  assert.deepEqual(resourceLedger.renderPasses[1].writes, ["feedback_1.ping", "feedback_1.pong"]);
  assert.equal(resourceLedger.resources["render_target_1.texture"].role, "RenderTarget");
  assert.deepEqual(shaderIr.nodes.map((node) => node.op), ["Gradient", "Feedback", "RenderTarget"]);
  assert.deepEqual(shaderCache.entries.map((entry) => entry.pipelineName), [
    "gradient_1_pipeline",
    "feedback_1_pipeline",
    "render_target_1_pipeline",
  ]);
  assert.match(generatedSource, /fragment float4 gradient_1_fragment/);
  assert.match(generatedSource, /fragment float4 feedback_1_fragment/);
  assert.match(generatedSource, /fragment float4 render_target_1_fragment/);
  assert.match(generatedSource, /texture2d<float> gradientTexture/);
  assert.match(generatedSource, /texture2d<float> feedbackHistory/);
  assert.equal(frameStats.width, 64);
  assert.equal(frameStats.height, 64);
  assert.equal(frameStats.nonBlack, true);
  assert.equal(frameStats.varied, true);
  assert.ok(!JSON.stringify({ result, runtimeGraph, resourceLedger, shaderIr, shaderCache }).includes("/Users/"));
});

test("NativeTexturePatchProductRuntime checked-in artifacts are path-clean and current", () => {
  const run = spawnSync("python3", [scriptPath, fixturePath, artifactDir], {
    cwd: repoRoot,
    encoding: "utf8",
  });

  assert.equal(run.status, 0, run.stderr || run.stdout);

  const result = readArtifact(artifactDir, "native_texture_patch_product_runtime_result.json");
  const resourceLedger = readArtifact(artifactDir, "resource_ledger.json");
  const shaderIr = readArtifact(artifactDir, "shader_ir.json");
  const frameStats = readArtifact(artifactDir, "frame_stats.json");

  assert.equal(result.ok, true);
  assert.equal(result.claims.completeTextureRuntime, false);
  assert.equal(resourceLedger.resources["feedback_1.history"].role, "FeedbackHistory");
  assert.deepEqual(shaderIr.nodes.map((node) => node.op), ["Gradient", "Feedback", "RenderTarget"]);
  assert.equal(frameStats.varied, true);
  assert.ok(!JSON.stringify({ result, resourceLedger, shaderIr, frameStats }).includes("/Users/"));
});

function readArtifact(dir, name) {
  return JSON.parse(fs.readFileSync(path.join(dir, name), "utf8"));
}

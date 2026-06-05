const assert = require("node:assert/strict");
const { spawnSync } = require("node:child_process");
const fs = require("node:fs");
const os = require("node:os");
const path = require("node:path");
const test = require("node:test");

const repoRoot = path.resolve(__dirname, "..");
const contractPath = path.join(repoRoot, "docs/runtime/NATIVE_GPU_PATCH_RUNTIME_SLICE_PROOF.md");
const fixturePath = path.join(repoRoot, "docs/runtime/fixtures/native_gpu_patch_runtime_slice.graph.json");
const scriptPath = path.join(repoRoot, "docs/runtime/scripts/native_gpu_patch_runtime_slice_shell.py");
const artifactDir = path.join(repoRoot, "docs/runtime/artifacts/native_gpu_patch_runtime_slice");

test("Native GPU patch runtime contract names bounded claims and nonclaims", () => {
  const source = fs.readFileSync(contractPath, "utf8");

  assert.match(source, /NativeGpuPatchRuntimeSliceProof answers:/);
  assert.match(source, /command-authored 2D texture patch/);
  assert.match(source, /commandGraphReplayed: true/);
  assert.match(source, /runtimeGraphBuilt: true/);
  assert.match(source, /frameSchedulerRan: true/);
  assert.match(source, /resourceAllocatorRan: true/);
  assert.match(source, /shaderCodegenCacheBuilt: true/);
  assert.match(source, /actualMetalRan: true/);
  assert.match(source, /nativeCanvasComplete: false/);
  assert.match(source, /aiWorkerRepairLoop: false/);
  assert.match(source, /genericShaderIrComplete: false/);
  assert.match(source, /vuoParity: false/);
});

test("Native GPU patch runtime fixture is command-authored and requests the first texture patch", () => {
  const graph = JSON.parse(fs.readFileSync(fixturePath, "utf8"));

  assert.equal(graph.graphId, "fixture.native_gpu_patch_runtime_slice");
  assert.equal(graph.scheduler.clockOwner, "graph");
  assert.deepEqual(graph.expected.cookOrder, ["constant_bg", "blob_fg", "blend_1", "output_1"]);
  assert.ok(graph.commands.some((command) => command.op === "createNode" && command.id === "constant_bg"));
  assert.ok(graph.commands.some((command) => command.op === "createNode" && command.id === "blob_fg"));
  assert.ok(graph.commands.some((command) => command.op === "createNode" && command.id === "blend_1"));
  assert.ok(graph.commands.some((command) => command.op === "connect" && command.to[0] === "blend_1"));
  assert.equal(graph.expected.claims.nativeCanvasComplete, false);
});

test("Native GPU patch runtime shell emits runtime, resource, shader, and Metal proof artifacts", () => {
  const tmpDir = fs.mkdtempSync(path.join(os.tmpdir(), "native-gpu-patch-runtime-"));
  const run = spawnSync("python3", [scriptPath, fixturePath, tmpDir], {
    cwd: repoRoot,
    encoding: "utf8",
  });

  assert.ok(run.status === 0 || run.status === 1, run.stderr || run.stdout);

  const result = readArtifact(tmpDir, "native_gpu_patch_runtime_slice_result.json");
  const trace = readArtifact(tmpDir, "native_gpu_patch_runtime_slice_trace.json");
  const errors = readArtifact(tmpDir, "native_gpu_patch_runtime_slice_errors.json");
  const runtimeGraph = readArtifact(tmpDir, "runtime_graph.json");
  const resourceLedger = readArtifact(tmpDir, "resource_ledger.json");
  const shaderCache = readArtifact(tmpDir, "shader_cache.json");
  const generatedSource = fs.readFileSync(path.join(tmpDir, "generated_patch.metal"), "utf8");

  assert.equal(result.kind, "NativeGpuPatchRuntimeSliceProof");
  assert.equal(result.graphId, "fixture.native_gpu_patch_runtime_slice");
  assert.equal(result.claims.commandGraphReplayed, true);
  assert.equal(result.claims.runtimeGraphBuilt, true);
  assert.equal(result.claims.frameSchedulerRan, true);
  assert.equal(result.claims.resourceAllocatorRan, true);
  assert.equal(result.claims.shaderCodegenCacheBuilt, true);
  assert.equal(result.claims.nativeCanvasComplete, false);
  assert.equal(result.claims.aiWorkerRepairLoop, false);
  assert.equal(result.claims.genericShaderIrComplete, false);
  assert.equal(result.claims.vuoParity, false);
  assert.equal(result.claims.backendReplacementReady, false);

  assert.deepEqual(runtimeGraph.cookOrder, ["constant_bg", "blob_fg", "blend_1", "output_1"]);
  assert.deepEqual(runtimeGraph.frames.map((frame) => frame.frameIndex), [0, 1]);
  assert.equal(resourceLedger.resources["constant_bg.texture"].ownerNode, "constant_bg");
  assert.equal(resourceLedger.resources["blob_fg.texture"].ownerNode, "blob_fg");
  assert.equal(resourceLedger.resources["blend_1.output"].ownerNode, "blend_1");
  assert.equal(resourceLedger.views["blend_1.output.srv"].ok, true);
  assert.deepEqual(resourceLedger.renderPasses.map((pass) => pass.nodeId), ["constant_bg", "blob_fg", "blend_1"]);
  assert.deepEqual(resourceLedger.renderPasses.map((pass) => pass.writes), [
    ["constant_bg.texture"],
    ["blob_fg.texture"],
    ["blend_1.output"],
  ]);
  assert.deepEqual(resourceLedger.renderPasses[2].reads, ["constant_bg.texture", "blob_fg.texture"]);
  assert.equal(shaderCache.entries.length, 3);
  assert.ok(shaderCache.entries.some((entry) => entry.nodeId === "blend_1" && entry.cacheKey.includes("image.use.blendImages")));
  assert.deepEqual(shaderCache.entries.map((entry) => entry.pipelineName), [
    "constant_bg_pipeline",
    "blob_fg_pipeline",
    "blend_1_pipeline",
  ]);
  assert.match(generatedSource, /fragment float4 my_world_fragment/);
  assert.match(generatedSource, /fragment float4 constant_bg_fragment/);
  assert.match(generatedSource, /fragment float4 blob_fg_fragment/);
  assert.match(generatedSource, /fragment float4 blend_1_fragment/);
  assert.match(generatedSource, /texture2d<float> constantTexture/);
  assert.match(generatedSource, /texture2d<float> blobTexture/);
  assert.match(generatedSource, /constant_bg_color/);
  assert.match(generatedSource, /blobSample/);
  assert.match(generatedSource, /mix\(constantColor, blobColor/);
  assert.ok(!JSON.stringify({ result, trace, errors, runtimeGraph, resourceLedger, shaderCache }).includes("/Users/"));

  if (run.status === 0) {
    const frameStats = readArtifact(tmpDir, "frame_stats.json");
    assert.equal(result.ok, true);
    assert.equal(result.status, "rendered");
    assert.equal(result.claims.actualCompilerRan, true);
    assert.equal(result.claims.actualMetalRan, true);
    assert.deepEqual(errors, []);
    assert.equal(frameStats.width, 64);
    assert.equal(frameStats.height, 64);
    assert.equal(frameStats.nonBlack, true);
    assert.equal(frameStats.varied, true);
    return;
  }

  assert.equal(result.ok, false);
  assert.equal(result.status, "blocked_metal_device_unavailable");
  assert.equal(result.claims.actualCompilerRan, false);
  assert.equal(result.claims.actualMetalRan, false);
  assert.ok(errors.some((error) => /Metal device unavailable/i.test(error.message || "")));
  assert.equal(fs.existsSync(path.join(tmpDir, "frame_stats.json")), false);
});

test("Native GPU patch runtime checked-in artifacts are path-clean and current", () => {
  const run = spawnSync("python3", [scriptPath, fixturePath, artifactDir], {
    cwd: repoRoot,
    encoding: "utf8",
  });

  assert.ok(run.status === 0 || run.status === 1, run.stderr || run.stdout);

  const result = readArtifact(artifactDir, "native_gpu_patch_runtime_slice_result.json");
  const trace = readArtifact(artifactDir, "native_gpu_patch_runtime_slice_trace.json");
  const errors = readArtifact(artifactDir, "native_gpu_patch_runtime_slice_errors.json");
  const runtimeGraph = readArtifact(artifactDir, "runtime_graph.json");
  const resourceLedger = readArtifact(artifactDir, "resource_ledger.json");
  const shaderCache = readArtifact(artifactDir, "shader_cache.json");

  assert.equal(result.kind, "NativeGpuPatchRuntimeSliceProof");
  assert.ok(!JSON.stringify({ result, trace, errors, runtimeGraph, resourceLedger, shaderCache }).includes("/Users/"));
  assert.equal(runtimeGraph.graphId, "fixture.native_gpu_patch_runtime_slice");
  assert.equal(resourceLedger.resources["blend_1.output"].role, "ColorBuffer");
  assert.equal(resourceLedger.renderPasses.length, 3);
  assert.ok(shaderCache.generatedSource === "generated_patch.metal");

  if (result.status === "rendered") {
    const frameStats = readArtifact(artifactDir, "frame_stats.json");
    assert.equal(frameStats.nonBlack, true);
    assert.equal(frameStats.varied, true);
  }
});

function readArtifact(dir, name) {
  return JSON.parse(fs.readFileSync(path.join(dir, name), "utf8"));
}

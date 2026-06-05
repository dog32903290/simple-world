const assert = require("node:assert/strict");
const { spawnSync } = require("node:child_process");
const fs = require("node:fs");
const os = require("node:os");
const path = require("node:path");
const test = require("node:test");

const repoRoot = path.resolve(__dirname, "..");
const contractPath = path.join(repoRoot, "docs/runtime/NATIVE_PRODUCT_CANVAS_SURFACE_PROOF.md");
const fixturePath = path.join(repoRoot, "docs/runtime/fixtures/native_product_canvas_surface.graph.json");
const scriptPath = path.join(repoRoot, "docs/runtime/scripts/native_product_canvas_surface_shell.py");
const artifactDir = path.join(repoRoot, "docs/runtime/artifacts/native_product_canvas_surface");

test("NativeProductCanvasSurface contract names a real native surface without final GUI overclaim", () => {
  const source = fs.readFileSync(contractPath, "utf8");

  assert.match(source, /NativeProductCanvasSurfaceProof answers:/);
  assert.match(source, /commandGraph -> native app window -> MTKView canvas -> runtime frame artifact/);
  assert.match(source, /not the final human-facing GUI skin/);
  assert.match(source, /not a headless shell renamed as product/);
  assert.match(source, /AppKit/);
  assert.match(source, /MetalKit/);
  assert.match(source, /MTKView/);
});

test("NativeProductCanvasSurface fixture enters through commands and requests a native window canvas", () => {
  const graph = JSON.parse(fs.readFileSync(fixturePath, "utf8"));

  assert.equal(graph.graphId, "fixture.native_product_canvas_surface");
  assert.equal(graph.appShell.mode, "nativeWindowCanvas");
  assert.equal(graph.appShell.window.title, "simple_world runtime surface proof");
  assert.equal(graph.appShell.canvas.kind, "MTKView");
  assert.equal(graph.appShell.canvas.pixelFormat, "RGBA8_Unorm");
  assert.equal(graph.scheduler.clockOwner, "graph");
  assert.deepEqual(graph.expected.cookOrder, ["constant_bg", "blob_fg", "blend_1", "output_1"]);
  assert.ok(graph.commands.every((command) => ["createNode", "setParam", "connect"].includes(command.op)));
});

test("NativeProductCanvasSurface shell emits native app, canvas, runtime, and frame artifacts", () => {
  const tmpDir = fs.mkdtempSync(path.join(os.tmpdir(), "native-product-canvas-"));
  const run = spawnSync("python3", [scriptPath, fixturePath, tmpDir], {
    cwd: repoRoot,
    encoding: "utf8",
  });

  assert.equal(run.status, 0, run.stderr || run.stdout);

  const result = readArtifact(tmpDir, "native_product_canvas_surface_result.json");
  const appSurface = readArtifact(tmpDir, "native_app_surface.json");
  const canvasSurface = readArtifact(tmpDir, "canvas_surface.json");
  const runtimeGraph = readArtifact(tmpDir, "runtime_graph.json");
  const frameArtifact = readArtifact(tmpDir, "runtime_frame_artifact.json");
  const commandLog = readArtifact(tmpDir, "command_log.json");
  const errors = readArtifact(tmpDir, "native_product_canvas_surface_errors.json");

  assert.equal(result.kind, "NativeProductCanvasSurfaceProof");
  assert.equal(result.graphId, "fixture.native_product_canvas_surface");
  assert.equal(result.claims.commandGraphOnlyMutation, true);
  assert.equal(result.claims.runtimeGraphAttached, true);
  assert.equal(result.claims.headlessShellRenamed, false);

  assert.equal(appSurface.kind, "NativeAppWindowSurface");
  assert.equal(appSurface.frameworks.app, "AppKit");
  assert.equal(appSurface.frameworks.canvas, "MetalKit");
  assert.equal(appSurface.window.title, "simple_world runtime surface proof");
  assert.equal(appSurface.window.visibleRequested, true);
  assert.equal(appSurface.canvas.kind, "MTKView");
  assert.equal(appSurface.canvas.mutationPath, "commandGraph");

  assert.equal(canvasSurface.kind, "NativeCanvasSurface");
  assert.equal(canvasSurface.backingLayer, "CAMetalLayer");
  assert.equal(canvasSurface.viewClass, "MTKView");
  assert.equal(canvasSurface.acceptsRuntimeFrames, true);

  assert.deepEqual(runtimeGraph.cookOrder, ["constant_bg", "blob_fg", "blend_1", "output_1"]);
  assert.equal(frameArtifact.kind, "RuntimeFrameArtifact");
  assert.equal(frameArtifact.frameIndex, 1);
  assert.equal(frameArtifact.canvasSurface, "MTKView");
  assert.equal(commandLog.every((entry) => entry.source === "fixture"), true);

  assert.deepEqual(errors, []);
  assert.equal(result.ok, true);
  assert.equal(result.status, "native_surface_ready");
  assert.equal(result.claims.nativeWindowCanvasSurface, true);
  assert.equal(appSurface.nativeProbe.status, "native_surface_ready");
  assert.equal(appSurface.nativeProbe.actualAppKitRan, true);
  assert.equal(appSurface.nativeProbe.actualMetalKitViewCreated, true);
});

test("NativeProductCanvasSurface checked-in artifacts are path-clean and current", () => {
  const run = spawnSync("python3", [scriptPath, fixturePath, artifactDir], {
    cwd: repoRoot,
    encoding: "utf8",
  });

  assert.equal(run.status, 0, run.stderr || run.stdout);

  const result = readArtifact(artifactDir, "native_product_canvas_surface_result.json");
  const payload = {
    result,
    appSurface: readArtifact(artifactDir, "native_app_surface.json"),
    canvasSurface: readArtifact(artifactDir, "canvas_surface.json"),
    runtimeGraph: readArtifact(artifactDir, "runtime_graph.json"),
  };

  assert.equal(result.kind, "NativeProductCanvasSurfaceProof");
  assert.equal(result.ok, true);
  assert.equal(result.claims.commandGraphOnlyMutation, true);
  assert.equal(result.claims.nativeWindowCanvasSurface, true);
  assert.equal(result.claims.headlessShellRenamed, false);
  assert.ok(!JSON.stringify(payload).includes("/Users/"));
});

function readArtifact(dir, name) {
  return JSON.parse(fs.readFileSync(path.join(dir, name), "utf8"));
}

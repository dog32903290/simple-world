const assert = require("node:assert/strict");
const { spawnSync } = require("node:child_process");
const fs = require("node:fs");
const os = require("node:os");
const path = require("node:path");
const test = require("node:test");

const repoRoot = path.resolve(__dirname, "..");
const contractPath = path.join(repoRoot, "docs/runtime/NATIVE_RENDERER_BACKEND_INTERFACE.md");
const fixturePath = path.join(repoRoot, "docs/runtime/fixtures/native_renderer_backend_interface.graph.json");
const scriptPath = path.join(repoRoot, "docs/runtime/scripts/native_renderer_backend_interface_shell.py");
const artifactDir = path.join(repoRoot, "docs/runtime/artifacts/native_renderer_backend_interface");

test("NativeRendererBackend interface contract borrows runtime shape without importing old UI", () => {
  const source = fs.readFileSync(contractPath, "utf8");

  assert.match(source, /NativeRendererBackend answers:/);
  assert.match(source, /ShaderProgram -> NativeRendererBackend -> CapturedFrame/);
  assert.match(source, /RenderBackend\.h/);
  assert.match(source, /ShaderPreviewInputBridge\.h/);
  assert.match(source, /compileShader/);
  assert.match(source, /captureFrame/);
  assert.match(source, /last-valid-frame failure policy/);
  assert.match(source, /do not carry is the old app UI/);
  assert.match(source, /not the Metal backend yet/);
});

test("NativeRendererBackend fixture points at ShaderProgram and my-world donor evidence", () => {
  const graph = JSON.parse(fs.readFileSync(fixturePath, "utf8"));

  assert.equal(graph.graphId, "fixture.native_renderer_backend_interface");
  assert.equal(graph.shaderProgramFixture, "docs/runtime/fixtures/shader_program_contract.graph.json");
  assert.ok(graph.metadata.donorEvidence.some((entry) => entry.endsWith("RenderBackend.h")));
  assert.ok(graph.metadata.donorEvidence.some((entry) => entry.endsWith("ShaderPreviewInputBridge.h")));
  assert.equal(graph.backend.capabilities.supportsShaderProgramPackage, true);
  assert.equal(graph.backend.capabilities.supportsLastValidFrame, true);
  assert.equal(graph.requestedFrame.resolution.width, 3840);
  assert.equal(graph.requestedFrame.loudness, 0.37);
});

test("NativeRendererBackend shell compiles packaged shader and emits backend lifecycle artifacts", () => {
  const run = spawnSync("python3", [scriptPath, fixturePath, artifactDir], {
    cwd: repoRoot,
    encoding: "utf8",
  });

  assert.equal(run.status, 0, run.stderr || run.stdout);

  const iface = readArtifact("native_backend_interface.json");
  const compileResult = readArtifact("shader_compile_result.json");
  const status = readArtifact("backend_status.json");
  const frameInput = readArtifact("render_frame_input.json");
  const capturedFrame = readArtifact("captured_frame_contract.json");
  const errors = readArtifact("native_renderer_backend_errors.json");

  assert.deepEqual(errors, []);
  assert.equal(iface.importsOldUi, false);
  assert.deepEqual(iface.operationsRun, ["compileShader", "resize", "renderFrame", "captureFrame"]);
  assert.equal(iface.shaderProgramId, "program.sphere_sdf_raymarch.fragment");
  assert.ok(iface.acceptedProgramShape.includes("sourceHash"));
  assert.ok(iface.acceptedProgramShape.includes("lastValidPolicy"));

  assert.equal(compileResult.ok, true);
  assert.equal(compileResult.status, "compiled");
  assert.equal(compileResult.preservesLastValidFrame, false);
  assert.match(compileResult.sourceHash, /^[a-f0-9]{64}$/);

  assert.equal(status.kind, "RenderBackendStatus");
  assert.equal(status.lastOperation, "render");
  assert.equal(status.hasRenderableProgram, true);
  assert.equal(status.viewportWidth, 3840);
  assert.equal(status.viewportHeight, 2160);

  assert.equal(frameInput.kind, "RenderFrameInput");
  assert.equal(frameInput.frameIndex, 42);
  assert.equal(frameInput.loudness, 0.37);

  assert.equal(capturedFrame.kind, "CapturedFrame");
  assert.equal(capturedFrame.ok, true);
  assert.equal(capturedFrame.status, "captured");
  assert.equal(capturedFrame.requestedResolution.width, 3840);
  assert.equal(capturedFrame.nonBlackSample, true);
});

test("NativeRendererBackend shell preserves last valid frame when ShaderProgram packaging fails", () => {
  const tmpDir = fs.mkdtempSync(path.join(os.tmpdir(), "native-backend-bad-"));
  const badShaderFixture = path.join(tmpDir, "bad-shader-program.graph.json");
  const badNativeFixture = path.join(tmpDir, "bad-native.graph.json");

  const shaderProgram = JSON.parse(fs.readFileSync(path.join(repoRoot, "docs/runtime/fixtures/shader_program_contract.graph.json"), "utf8"));
  shaderProgram.shaderProgram.entrySymbols.push("missing_native_backend_symbol");
  fs.writeFileSync(badShaderFixture, JSON.stringify(shaderProgram, null, 2));

  const nativeFixture = JSON.parse(fs.readFileSync(fixturePath, "utf8"));
  nativeFixture.shaderProgramFixture = badShaderFixture;
  nativeFixture.backend.initialState.previousValidProgram = true;
  fs.writeFileSync(badNativeFixture, JSON.stringify(nativeFixture, null, 2));

  const run = spawnSync("python3", [scriptPath, badNativeFixture, tmpDir], {
    cwd: repoRoot,
    encoding: "utf8",
  });

  assert.equal(run.status, 1);
  const compileResult = JSON.parse(fs.readFileSync(path.join(tmpDir, "shader_compile_result.json"), "utf8"));
  const status = JSON.parse(fs.readFileSync(path.join(tmpDir, "backend_status.json"), "utf8"));
  const errors = JSON.parse(fs.readFileSync(path.join(tmpDir, "native_renderer_backend_errors.json"), "utf8"));

  assert.equal(errors[0].code, "native_backend.shader_program_package_failed");
  assert.equal(compileResult.ok, false);
  assert.equal(compileResult.status, "compile_failed");
  assert.equal(compileResult.preservesLastValidFrame, true);
  assert.equal(status.hasRenderableProgram, true);
  assert.equal(status.preservesLastValidFrame, true);
  assert.match(status.lastError, /keeping last valid frame/);
});

function readArtifact(name) {
  return JSON.parse(fs.readFileSync(path.join(artifactDir, name), "utf8"));
}

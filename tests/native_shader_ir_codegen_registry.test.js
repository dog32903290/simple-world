const assert = require("node:assert/strict");
const { spawnSync } = require("node:child_process");
const fs = require("node:fs");
const os = require("node:os");
const path = require("node:path");
const test = require("node:test");

const repoRoot = path.resolve(__dirname, "..");
const contractPath = path.join(repoRoot, "docs/runtime/NATIVE_SHADER_IR_CODEGEN_REGISTRY_PROOF.md");
const fixturePath = path.join(repoRoot, "docs/runtime/fixtures/native_shader_ir_codegen_registry.graph.json");
const scriptPath = path.join(repoRoot, "docs/runtime/scripts/native_shader_ir_codegen_registry_shell.py");
const artifactDir = path.join(repoRoot, "docs/runtime/artifacts/native_shader_ir_codegen_registry");

test("NativeShaderIrCodegenRegistry contract moves codegen out of per-shell hand adapters", () => {
  const source = fs.readFileSync(contractPath, "utf8");

  assert.match(source, /NativeShaderIrCodegenRegistryProof answers:/);
  assert.match(source, /NodeSpec registry -> ShaderIR -> codegen cache -> generated MSL/);
  assert.match(source, /not a complete shader language/);
  assert.match(source, /not HLSL-to-MSL translation/);
  assert.match(source, /unknown nodes emit diagnostics/);
});

test("NativeShaderIrCodegenRegistry fixture mixes six texture ops through commandGraph", () => {
  const graph = JSON.parse(fs.readFileSync(fixturePath, "utf8"));

  assert.equal(graph.graphId, "fixture.native_shader_ir_codegen_registry");
  assert.equal(graph.scheduler.clockOwner, "graph");
  assert.deepEqual(graph.expected.irOps, ["ConstantImage", "Blob", "BlendImages", "Gradient", "Feedback", "RenderTarget"]);
  assert.ok(graph.commands.some((command) => command.op === "createNode" && command.type === "image.constant"));
  assert.ok(graph.commands.some((command) => command.op === "createNode" && command.type === "image.generate.basic.blob"));
  assert.ok(graph.commands.some((command) => command.op === "createNode" && command.type === "image.use.blendImages"));
  assert.ok(graph.commands.some((command) => command.op === "createNode" && command.type === "image.generate.gradient"));
  assert.ok(graph.commands.some((command) => command.op === "createNode" && command.type === "image.memory.feedback"));
  assert.ok(graph.commands.some((command) => command.op === "createNode" && command.type === "image.output.renderTarget"));
});

test("NativeShaderIrCodegenRegistry shell emits registry-driven IR, cache, source, and diagnostics", () => {
  const tmpDir = fs.mkdtempSync(path.join(os.tmpdir(), "native-shader-registry-"));
  const run = spawnSync("python3", [scriptPath, fixturePath, tmpDir], {
    cwd: repoRoot,
    encoding: "utf8",
  });

  assert.equal(run.status, 0, run.stderr || run.stdout);

  const result = readArtifact(tmpDir, "native_shader_ir_codegen_registry_result.json");
  const registry = readArtifact(tmpDir, "node_codegen_registry.json");
  const shaderIr = readArtifact(tmpDir, "shader_ir.json");
  const shaderCache = readArtifact(tmpDir, "shader_cache.json");
  const diagnostics = readArtifact(tmpDir, "diagnostics.json");
  const errors = readArtifact(tmpDir, "native_shader_ir_codegen_registry_errors.json");
  const source = fs.readFileSync(path.join(tmpDir, "generated_registry_patch.metal"), "utf8");

  assert.deepEqual(errors, []);
  assert.deepEqual(diagnostics, []);
  assert.equal(result.kind, "NativeShaderIrCodegenRegistryProof");
  assert.equal(result.ok, true);
  assert.equal(result.claims.registryDrivenCodegen, true);
  assert.equal(result.claims.perShellHandAdapter, false);
  assert.equal(result.claims.completeShaderLanguage, false);
  assert.equal(result.claims.hlslToMslTranslation, false);

  assert.deepEqual(registry.nodeTypes, [
    "image.constant",
    "image.generate.basic.blob",
    "image.use.blendImages",
    "image.generate.gradient",
    "image.memory.feedback",
    "image.output.renderTarget",
  ]);
  assert.deepEqual(shaderIr.nodes.map((node) => node.op), ["ConstantImage", "Blob", "BlendImages", "Gradient", "Feedback", "RenderTarget"]);
  assert.equal(shaderIr.nodes[0].resources.writes[0], "constant_bg.texture");
  assert.deepEqual(shaderIr.nodes[2].resources.reads, ["constant_bg.texture", "blob_fg.texture"]);
  assert.deepEqual(shaderIr.nodes[4].resources.reads, ["gradient_1.texture", "feedback_1.history"]);
  assert.deepEqual(shaderIr.nodes[4].resources.writes, ["feedback_1.ping", "feedback_1.pong"]);
  assert.equal(shaderCache.entries.length, 6);
  assert.ok(shaderCache.entries.every((entry) => entry.cacheKey.startsWith("ir:")));
  assert.ok(shaderCache.entries.every((entry) => entry.templateId));
  assert.match(source, /fragment float4 constant_bg_fragment/);
  assert.match(source, /fragment float4 blob_fg_fragment/);
  assert.match(source, /fragment float4 blend_1_fragment/);
  assert.match(source, /fragment float4 gradient_1_fragment/);
  assert.match(source, /fragment float4 feedback_1_fragment/);
  assert.match(source, /fragment float4 render_target_1_fragment/);
  assert.ok(!JSON.stringify({ result, registry, shaderIr, shaderCache, diagnostics }).includes("/Users/"));
});

test("NativeShaderIrCodegenRegistry refuses unknown node types before source generation", () => {
  const tmpDir = fs.mkdtempSync(path.join(os.tmpdir(), "native-shader-registry-bad-"));
  const badFixture = path.join(tmpDir, "bad.graph.json");
  const graph = JSON.parse(fs.readFileSync(fixturePath, "utf8"));
  graph.graphId = "fixture.native_shader_ir_codegen_registry.bad";
  graph.commands.splice(1, 0, { op: "createNode", id: "mystery_1", type: "image.fx.unknownMystery" });
  graph.expected.irOps = [];
  fs.writeFileSync(badFixture, JSON.stringify(graph, null, 2));

  const run = spawnSync("python3", [scriptPath, badFixture, tmpDir], {
    cwd: repoRoot,
    encoding: "utf8",
  });

  assert.equal(run.status, 1);
  const result = readArtifact(tmpDir, "native_shader_ir_codegen_registry_result.json");
  const diagnostics = readArtifact(tmpDir, "diagnostics.json");
  const errors = readArtifact(tmpDir, "native_shader_ir_codegen_registry_errors.json");

  assert.equal(result.ok, false);
  assert.equal(result.status, "diagnostics_failed");
  assert.equal(diagnostics[0].code, "shader_ir.unknown_node_type");
  assert.equal(diagnostics[0].nodeType, "image.fx.unknownMystery");
  assert.equal(errors[0].code, "shader_ir.codegen_blocked_by_diagnostics");
  assert.equal(fs.existsSync(path.join(tmpDir, "generated_registry_patch.metal")), false);
});

test("NativeShaderIrCodegenRegistry checked-in artifacts are path-clean and current", () => {
  const run = spawnSync("python3", [scriptPath, fixturePath, artifactDir], {
    cwd: repoRoot,
    encoding: "utf8",
  });

  assert.equal(run.status, 0, run.stderr || run.stdout);

  const result = readArtifact(artifactDir, "native_shader_ir_codegen_registry_result.json");
  const shaderIr = readArtifact(artifactDir, "shader_ir.json");
  const shaderCache = readArtifact(artifactDir, "shader_cache.json");
  const registry = readArtifact(artifactDir, "node_codegen_registry.json");

  assert.equal(result.ok, true);
  assert.equal(result.claims.registryDrivenCodegen, true);
  assert.equal(result.claims.completeShaderLanguage, false);
  assert.equal(shaderIr.nodes.length, 6);
  assert.equal(shaderCache.entries.length, 6);
  assert.equal(registry.nodeTypes.length, 6);
  assert.ok(!JSON.stringify({ result, shaderIr, shaderCache, registry }).includes("/Users/"));
});

function readArtifact(dir, name) {
  return JSON.parse(fs.readFileSync(path.join(dir, name), "utf8"));
}

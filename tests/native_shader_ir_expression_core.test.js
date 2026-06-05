const assert = require("node:assert/strict");
const { spawnSync } = require("node:child_process");
const fs = require("node:fs");
const os = require("node:os");
const path = require("node:path");
const test = require("node:test");

const repoRoot = path.resolve(__dirname, "..");
const contractPath = path.join(repoRoot, "docs/runtime/NATIVE_SHADER_IR_EXPRESSION_CORE_PROOF.md");
const fixturePath = path.join(repoRoot, "docs/runtime/fixtures/native_shader_ir_expression_core.graph.json");
const scriptPath = path.join(repoRoot, "docs/runtime/scripts/native_shader_ir_expression_core_shell.py");
const artifactDir = path.join(repoRoot, "docs/runtime/artifacts/native_shader_ir_expression_core");

test("NativeShaderIrExpressionCore contract admits expression trees without claiming complete shader language", () => {
  const source = fs.readFileSync(contractPath, "utf8");

  assert.match(source, /NativeShaderIrExpressionCoreProof answers:/);
  assert.match(source, /NodeSpec expression fields -> ShaderExpressionIR -> generated MSL -> Metal compile artifact/);
  assert.match(source, /core expression language/);
  assert.match(source, /not a complete shader language/);
});

test("NativeShaderIrExpressionCore fixture declares uniforms math and color composition", () => {
  const graph = JSON.parse(fs.readFileSync(fixturePath, "utf8"));

  assert.equal(graph.graphId, "fixture.native_shader_ir_expression_core");
  assert.deepEqual(graph.expected.expressionOps, ["uniform", "uv", "swizzle", "const", "sin", "mul", "add", "smoothstep", "mix", "vec4"]);
  assert.equal(graph.shaderExpression.root.op, "vec4");
  assert.ok(graph.shaderExpression.uniforms.some((uniform) => uniform.name === "u_time"));
});

test("NativeShaderIrExpressionCore shell emits expression IR cache source and Metal compile artifact", () => {
  const tmpDir = fs.mkdtempSync(path.join(os.tmpdir(), "native-shader-expression-core-"));
  const run = spawnSync("python3", [scriptPath, fixturePath, tmpDir], {
    cwd: repoRoot,
    encoding: "utf8",
  });

  assert.equal(run.status, 0, run.stderr || run.stdout);

  const result = readArtifact(tmpDir, "native_shader_ir_expression_core_result.json");
  const expressionIr = readArtifact(tmpDir, "shader_expression_ir.json");
  const shaderCache = readArtifact(tmpDir, "shader_expression_cache.json");
  const diagnostics = readArtifact(tmpDir, "diagnostics.json");
  const compile = readArtifact(tmpDir, "metal_compile.json");
  const errors = readArtifact(tmpDir, "native_shader_ir_expression_core_errors.json");
  const source = fs.readFileSync(path.join(tmpDir, "generated_expression_core.metal"), "utf8");

  assert.deepEqual(errors, []);
  assert.deepEqual(diagnostics, []);
  assert.equal(result.kind, "NativeShaderIrExpressionCoreProof");
  assert.equal(result.ok, true);
  assert.equal(result.claims.coreExpressionLanguage, true);
  assert.equal(result.claims.metalCompiled, true);
  assert.equal(result.claims.completeShaderLanguage, false);
  assert.equal(result.claims.unsafeExpressionBlocked, true);

  assert.equal(expressionIr.root.op, "vec4");
  assert.deepEqual(expressionIr.uniforms.map((uniform) => uniform.name), ["u_time", "u_intensity"]);
  assert.ok(expressionIr.allowedOps.includes("smoothstep"));
  assert.equal(shaderCache.entries[0].cacheKey.startsWith("expr:"), true);
  assert.equal(compile.status, "compiled");
  assert.match(source, /struct ExpressionUniforms/);
  assert.match(source, /fragment float4 expression_core_fragment/);
  assert.match(source, /sin\(uniforms.u_time/);
  assert.match(source, /smoothstep/);
  assert.match(source, /mix/);
  assert.ok(!JSON.stringify({ result, expressionIr, shaderCache, diagnostics, compile }).includes("/Users/"));
});

test("NativeShaderIrExpressionCore refuses unknown or unsafe expression ops before source generation", () => {
  const tmpDir = fs.mkdtempSync(path.join(os.tmpdir(), "native-shader-expression-core-bad-"));
  const badFixture = path.join(tmpDir, "bad.graph.json");
  const graph = JSON.parse(fs.readFileSync(fixturePath, "utf8"));
  graph.graphId = "fixture.native_shader_ir_expression_core.bad";
  graph.shaderExpression.root.args[0] = { op: "forLoop", args: [] };
  fs.writeFileSync(badFixture, JSON.stringify(graph, null, 2));

  const run = spawnSync("python3", [scriptPath, badFixture, tmpDir], {
    cwd: repoRoot,
    encoding: "utf8",
  });

  assert.equal(run.status, 1);
  const result = readArtifact(tmpDir, "native_shader_ir_expression_core_result.json");
  const diagnostics = readArtifact(tmpDir, "diagnostics.json");
  const errors = readArtifact(tmpDir, "native_shader_ir_expression_core_errors.json");

  assert.equal(result.ok, false);
  assert.equal(result.status, "diagnostics_failed");
  assert.equal(diagnostics[0].code, "shader_expression.unsupported_op");
  assert.equal(diagnostics[0].op, "forLoop");
  assert.equal(errors[0].code, "shader_expression.codegen_blocked_by_diagnostics");
  assert.equal(fs.existsSync(path.join(tmpDir, "generated_expression_core.metal")), false);
});

test("NativeShaderIrExpressionCore checked-in artifacts are path-clean and current", () => {
  const run = spawnSync("python3", [scriptPath, fixturePath, artifactDir], {
    cwd: repoRoot,
    encoding: "utf8",
  });

  assert.equal(run.status, 0, run.stderr || run.stdout);

  const result = readArtifact(artifactDir, "native_shader_ir_expression_core_result.json");
  const expressionIr = readArtifact(artifactDir, "shader_expression_ir.json");
  const compile = readArtifact(artifactDir, "metal_compile.json");

  assert.equal(result.ok, true);
  assert.equal(result.claims.coreExpressionLanguage, true);
  assert.equal(expressionIr.root.op, "vec4");
  assert.equal(compile.status, "compiled");
  assert.ok(!JSON.stringify({ result, expressionIr, compile }).includes("/Users/"));
});

function readArtifact(dir, name) {
  return JSON.parse(fs.readFileSync(path.join(dir, name), "utf8"));
}

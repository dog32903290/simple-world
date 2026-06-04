const assert = require("node:assert/strict");
const { spawnSync } = require("node:child_process");
const fs = require("node:fs");
const os = require("node:os");
const path = require("node:path");
const test = require("node:test");

const repoRoot = path.resolve(__dirname, "..");
const contractPath = path.join(repoRoot, "docs/runtime/NATIVE_DRAW_SHADER_COMPILE_PROOF.md");
const fixturePath = path.join(repoRoot, "docs/runtime/fixtures/native_draw_shader_compile_proof.graph.json");
const scriptPath = path.join(repoRoot, "docs/runtime/scripts/native_draw_shader_compile_proof_shell.py");
const artifactDir = path.join(repoRoot, "docs/runtime/artifacts/native_draw_shader_compile_proof");
const packageArtifactPath = path.join(
  repoRoot,
  "docs/runtime/artifacts/native_render_pipeline/shader_program/shader_program_package.json",
);

test("NativeDrawShaderCompileProof contract separates donor diagnostic and explicit Metal proof boundaries", () => {
  const source = fs.readFileSync(contractPath, "utf8");

  assert.match(source, /NativeDrawShaderCompileProof answers:/);
  assert.match(source, /donor HLSL path is a compile diagnostic only/);
  assert.match(source, /does not draw pixels/);
  assert.match(source, /explicit native MSL path delegates to `MetalExplicitMslProof`/);
  assert.match(source, /8x8 compile\/render\/readback evidence/);
  assert.match(source, /not renderer or backend\/pipeline integration/);
  assert.match(source, /not Metal parity for\s+donor shaders/);
  assert.match(source, /not TiXL parity/);
  assert.match(source, /not PBR visual correctness/);
  assert.match(source, /shader_program_package\.json requestedDrawShader -> NativeDrawShaderCompileProof -> compile result\/trace\/errors artifacts/);
  assert.match(source, /requestedDrawShader\.nativeSource\(MSL\) -> NativeDrawShaderCompileProof -> MetalExplicitMslProof/);
  assert.match(source, /compiled_explicit_msl_with_metal_proof/);
  assert.match(source, /explicitMslMetalProof/);
});

test("NativeDrawShaderCompileProof fixture points at the existing ShaderProgram package artifact", () => {
  const graph = JSON.parse(fs.readFileSync(fixturePath, "utf8"));

  assert.equal(graph.graphId, "fixture.native_draw_shader_compile_proof");
  assert.equal(
    graph.shaderProgramPackage,
    "docs/runtime/artifacts/native_render_pipeline/shader_program/shader_program_package.json",
  );
  assert.equal(graph.expected.requestedDrawShaderSource, "Lib:shaders/3d/mesh/mesh-Draw.hlsl");
  assert.equal(graph.expected.sourceLanguage, "HLSL_TIXL_DONOR");
  assert.equal(graph.expected.vertexEntry, "vsMain");
  assert.equal(graph.expected.pixelEntry, "psMain");
});

test("NativeDrawShaderCompileProof shell emits artifacts and blocks TiXL donor HLSL", () => {
  const tmpDir = fs.mkdtempSync(path.join(os.tmpdir(), "native-draw-donor-"));
  const run = spawnSync("python3", [scriptPath, fixturePath, tmpDir], {
    cwd: repoRoot,
    encoding: "utf8",
  });

  assert.equal(run.status, 1);

  const result = readArtifact(tmpDir, "native_draw_shader_compile_result.json");
  const trace = readArtifact(tmpDir, "native_draw_shader_compile_trace.json");
  const errors = readArtifact(tmpDir, "native_draw_shader_compile_errors.json");

  assert.equal(result.kind, "NativeDrawShaderCompileProof");
  assert.equal(result.ok, false);
  assert.equal(result.status, "blocked_missing_native_source");
  assert.equal(result.programId, "program.sphere_sdf_raymarch.fragment");
  assert.equal(result.requestedDrawShader.source, "Lib:shaders/3d/mesh/mesh-Draw.hlsl");
  assert.equal(result.requestedDrawShader.sourceLanguage, "HLSL_TIXL_DONOR");
  assert.equal(result.requestedDrawShader.vertexEntry, "vsMain");
  assert.equal(result.requestedDrawShader.fragmentEntry, "psMain");
  assert.equal(result.requestedDrawShader.sourceDerivedFrom, "Lib:shaders/3d/mesh/mesh-Draw.hlsl");
  assert.equal(result.requestedDrawShader.donorSource, "Lib:shaders/3d/mesh/mesh-Draw.hlsl");
  assert.equal(result.claims.nativeCompileParity, false);
  assert.equal(result.claims.actualCompilerRan, false);
  assert.equal(result.claims.metalParity, false);
  assert.equal(result.claims.tixlParity, false);
  assert.equal(result.claims.pbrVisualCorrectness, false);

  assert.equal(errors[0].code, "native_draw_shader_compile.native_source_missing");
  assert.equal(errors[0].sourceLanguage, "HLSL_TIXL_DONOR");
  assert.equal(errors[0].donorSource, "Lib:shaders/3d/mesh/mesh-Draw.hlsl");
  assert.deepEqual(trace.map((entry) => entry.op), [
    "loadNativeDrawShaderCompileProof",
    "loadShaderProgramPackage",
    "validateRequestedDrawShader",
    "blockDonorHlslNativeCompileClaim",
    "publishNativeDrawShaderCompileArtifacts",
  ]);
  assert.equal(trace[0].fixture, "docs/runtime/fixtures/native_draw_shader_compile_proof.graph.json");
  assert.equal(
    trace[1].package,
    "docs/runtime/artifacts/native_render_pipeline/shader_program/shader_program_package.json",
  );
  assert.ok(!JSON.stringify(trace).includes("/Users/"));
});

test("NativeDrawShaderCompileProof checked-in artifacts stay path-clean and current", () => {
  const result = readArtifact(artifactDir, "native_draw_shader_compile_result.json");
  const trace = readArtifact(artifactDir, "native_draw_shader_compile_trace.json");
  const errors = readArtifact(artifactDir, "native_draw_shader_compile_errors.json");

  assert.equal(result.ok, false);
  assert.equal(result.status, "blocked_missing_native_source");
  assert.ok(errors.some((error) => error.code === "native_draw_shader_compile.native_source_missing"));
  assert.equal(trace[0].fixture, "docs/runtime/fixtures/native_draw_shader_compile_proof.graph.json");
  assert.equal(
    trace[1].package,
    "docs/runtime/artifacts/native_render_pipeline/shader_program/shader_program_package.json",
  );
  assert.ok(!JSON.stringify(trace).includes("/Users/"));
});

test("NativeDrawShaderCompileProof refuses a package missing requestedDrawShader", () => {
  const tmpDir = fs.mkdtempSync(path.join(os.tmpdir(), "native-draw-missing-request-"));
  const packagePath = path.join(tmpDir, "shader_program_package.json");
  const graphPath = path.join(tmpDir, "graph.json");
  const pkg = JSON.parse(fs.readFileSync(packageArtifactPath, "utf8"));
  delete pkg.requestedDrawShader;
  fs.writeFileSync(packagePath, JSON.stringify(pkg, null, 2));
  fs.writeFileSync(graphPath, JSON.stringify({ graphId: "fixture.missing", shaderProgramPackage: packagePath }, null, 2));

  const run = spawnSync("python3", [scriptPath, graphPath, tmpDir], {
    cwd: repoRoot,
    encoding: "utf8",
  });

  assert.equal(run.status, 1);
  const result = readArtifact(tmpDir, "native_draw_shader_compile_result.json");
  const errors = readArtifact(tmpDir, "native_draw_shader_compile_errors.json");
  assert.equal(result.ok, false);
  assert.equal(result.status, "missing_requested_draw_shader");
  assert.equal(errors[0].code, "native_draw_shader_compile.requested_draw_shader_missing");
});

test("NativeDrawShaderCompileProof blocks TiXL donor HLSL even when language/status are naively flipped", () => {
  const tmpDir = fs.mkdtempSync(path.join(os.tmpdir(), "native-draw-naive-flip-"));
  const packagePath = path.join(tmpDir, "shader_program_package.json");
  const graphPath = path.join(tmpDir, "graph.json");
  const pkg = JSON.parse(fs.readFileSync(packageArtifactPath, "utf8"));
  pkg.status = "compiled";
  pkg.requestedDrawShader.language = "MSL";
  pkg.requestedDrawShader.compileParity = "compiled";
  fs.writeFileSync(packagePath, JSON.stringify(pkg, null, 2));
  fs.writeFileSync(graphPath, JSON.stringify({ graphId: "fixture.naive", shaderProgramPackage: packagePath }, null, 2));

  const run = spawnSync("python3", [scriptPath, graphPath, tmpDir], {
    cwd: repoRoot,
    encoding: "utf8",
  });

  assert.equal(run.status, 1);
  const result = readArtifact(tmpDir, "native_draw_shader_compile_result.json");
  const errors = readArtifact(tmpDir, "native_draw_shader_compile_errors.json");
  assert.equal(result.ok, false);
  assert.equal(result.status, "blocked_missing_native_source");
  assert.equal(result.requestedDrawShader.sourceLanguage, "MSL");
  assert.equal(result.requestedDrawShader.sourceDerivedFrom, "Lib:shaders/3d/mesh/mesh-Draw.hlsl");
  assert.equal(errors[0].code, "native_draw_shader_compile.native_source_missing");
});

test("NativeDrawShaderCompileProof runs actual Metal proof for explicit native MSL source", () => {
  const tmpDir = fs.mkdtempSync(path.join(os.tmpdir(), "native-draw-msl-source-"));
  const packagePath = path.join(tmpDir, "shader_program_package.json");
  const graphPath = path.join(tmpDir, "graph.json");
  const pkg = JSON.parse(fs.readFileSync(packageArtifactPath, "utf8"));
  pkg.requestedDrawShader.nativeSource = {
    language: "MSL",
    vertexEntry: "my_world_vertex",
    fragmentEntry: "my_world_fragment",
    sourceText: `#include <metal_stdlib>
using namespace metal;

struct VertexOut
{
    float4 position [[position]];
    float2 uv;
};

vertex VertexOut my_world_vertex(uint vertexId [[vertex_id]])
{
    const float2 positions[4] = {
        float2(-1.0, -1.0),
        float2( 1.0, -1.0),
        float2(-1.0,  1.0),
        float2( 1.0,  1.0)
    };

    VertexOut out;
    out.position = float4(positions[vertexId], 0.0, 1.0);
    out.uv = positions[vertexId] * 0.5 + 0.5;
    return out;
}

fragment float4 my_world_fragment(VertexOut in [[stage_in]])
{
    return float4(in.uv.x, 0.25 + 0.5 * in.uv.y, 1.0 - in.uv.x, 1.0);
}
`,
  };
  fs.writeFileSync(packagePath, JSON.stringify(pkg, null, 2));
  fs.writeFileSync(graphPath, JSON.stringify({ graphId: "fixture.native", shaderProgramPackage: packagePath }, null, 2));

  const run = spawnSync("python3", [scriptPath, graphPath, tmpDir], {
    cwd: repoRoot,
    encoding: "utf8",
  });

  assert.ok(run.status === 0 || run.status === 1, run.stderr || run.stdout);
  const result = readArtifact(tmpDir, "native_draw_shader_compile_result.json");
  const errors = readArtifact(tmpDir, "native_draw_shader_compile_errors.json");
  assert.equal(result.nativeSource.language, "MSL");
  assert.equal(result.nativeSource.vertexEntry, "my_world_vertex");
  assert.equal(result.nativeSource.fragmentEntry, "my_world_fragment");

  if (run.status === 0) {
    assert.deepEqual(errors, []);
    assert.equal(result.ok, true);
    assert.equal(result.status, "compiled_explicit_msl_with_metal_proof");
    assert.equal(result.claims.actualCompilerRan, true);
    assert.equal(result.claims.actualMetalRan, true);
    assert.equal(result.claims.nativeCompileParity, true);
    assert.equal(result.claims.explicitMslMetalProof, true);
    assert.equal(result.claims.metalParity, false);
    assert.equal(result.claims.tixlParity, false);
    assert.equal(result.claims.pbrVisualCorrectness, false);
    assert.equal(result.metalProof.status, "rendered");
    assert.equal(result.metalProof.width, 8);
    assert.equal(result.metalProof.height, 8);
    assert.equal(result.metalProof.nonBlack, true);
    assert.equal(result.metalProof.varied, true);
    return;
  }

  assert.equal(result.ok, false);
  assert.equal(result.claims.actualCompilerRan, false);
  assert.equal(result.claims.actualMetalRan, false);
  assert.equal(result.claims.nativeCompileParity, false);
  assert.equal(result.claims.explicitMslMetalProof, false);
  assertStableExplicitMslEnvironmentFailure(result, errors);
});

test("NativeDrawShaderCompileProof nests Metal proof compiler failure for invalid explicit MSL", () => {
  const tmpDir = fs.mkdtempSync(path.join(os.tmpdir(), "native-draw-msl-invalid-"));
  const packagePath = path.join(tmpDir, "shader_program_package.json");
  const graphPath = path.join(tmpDir, "graph.json");
  const pkg = JSON.parse(fs.readFileSync(packageArtifactPath, "utf8"));
  pkg.requestedDrawShader.nativeSource = {
    language: "MSL",
    vertexEntry: "my_world_vertex",
    fragmentEntry: "my_world_fragment",
    sourceText: `#include <metal_stdlib>
using namespace metal;
vertex float4 my_world_vertex(uint vertexId [[vertex_id]]) { return float4(0.0); }
fragment float4 my_world_fragment() { return float4(1.0) }
`,
  };
  fs.writeFileSync(packagePath, JSON.stringify(pkg, null, 2));
  fs.writeFileSync(graphPath, JSON.stringify({ graphId: "fixture.native.invalid", shaderProgramPackage: packagePath }, null, 2));

  const run = spawnSync("python3", [scriptPath, graphPath, tmpDir], {
    cwd: repoRoot,
    encoding: "utf8",
  });

  assert.equal(run.status, 1);
  const result = readArtifact(tmpDir, "native_draw_shader_compile_result.json");
  const errors = readArtifact(tmpDir, "native_draw_shader_compile_errors.json");

  assert.equal(result.ok, false);
  assert.equal(result.claims.nativeCompileParity, false);
  assert.equal(result.claims.explicitMslMetalProof, false);
  assert.equal(result.claims.tixlParity, false);
  assert.equal(result.claims.pbrVisualCorrectness, false);
  assert.equal(errors[0].code, "native_draw_shader_compile.metal_proof_failed");

  if (result.status === "blocked_metal_device_unavailable") {
    assert.equal(errors[0].metalStatus, "blocked_metal_device_unavailable");
    assert.equal(result.claims.actualCompilerRan, false);
    assert.equal(result.claims.actualMetalRan, false);
    return;
  }

  if (result.status === "metal_explicit_msl_proof_unavailable") {
    assertStableExplicitMslEnvironmentFailure(result, errors);
    return;
  }

  assert.equal(result.status, "metal_explicit_msl_proof_failed");
  assert.equal(result.metalProof.status, "compile_failed");
  assert.equal(errors[0].metalStatus, "compile_failed");
  assert.ok(errors[0].metalErrors.some((error) => error.code === "metal_explicit_msl.compile_failed"));
  assert.equal(result.claims.actualCompilerRan, true);
  assert.equal(result.claims.actualMetalRan, false);
});

test("NativeDrawShaderCompileProof reports stable unavailable status when Metal proof command is missing", () => {
  const tmpDir = fs.mkdtempSync(path.join(os.tmpdir(), "native-draw-msl-unavailable-"));
  const packagePath = path.join(tmpDir, "shader_program_package.json");
  const graphPath = path.join(tmpDir, "graph.json");
  const pkg = JSON.parse(fs.readFileSync(packageArtifactPath, "utf8"));
  pkg.requestedDrawShader.nativeSource = {
    language: "MSL",
    vertexEntry: "my_world_vertex",
    fragmentEntry: "my_world_fragment",
    sourceText: `#include <metal_stdlib>
using namespace metal;
vertex float4 my_world_vertex(uint vertexId [[vertex_id]]) { return float4(0.0); }
fragment float4 my_world_fragment() { return float4(1.0); }
`,
  };
  fs.writeFileSync(packagePath, JSON.stringify(pkg, null, 2));
  fs.writeFileSync(graphPath, JSON.stringify({ graphId: "fixture.native.unavailable", shaderProgramPackage: packagePath }, null, 2));

  const run = spawnSync("python3", [scriptPath, graphPath, tmpDir], {
    cwd: repoRoot,
    encoding: "utf8",
    env: {
      ...process.env,
      NATIVE_DRAW_SHADER_COMPILE_METAL_PROOF_COMMAND: "definitely-not-a-metal-proof-command",
    },
  });

  assert.equal(run.status, 1);
  const result = readArtifact(tmpDir, "native_draw_shader_compile_result.json");
  const errors = readArtifact(tmpDir, "native_draw_shader_compile_errors.json");

  assert.equal(result.ok, false);
  assert.equal(result.status, "metal_explicit_msl_proof_unavailable");
  assert.equal(result.claims.actualCompilerRan, false);
  assert.equal(result.claims.actualMetalRan, false);
  assert.equal(result.claims.nativeCompileParity, false);
  assert.equal(result.claims.explicitMslMetalProof, false);
  assert.equal(errors[0].code, "native_draw_shader_compile.metal_proof_unavailable");
  assert.equal(errors[0].metalStatus, "metal_explicit_msl_proof_unavailable");
  assert.equal(errors[0].metalErrors[0].code, "metal_explicit_msl.proof_unavailable");
  assert.ok(!JSON.stringify({ result, errors }).includes("Traceback"));
});

function readArtifact(dir, name) {
  return JSON.parse(fs.readFileSync(path.join(dir, name), "utf8"));
}

function assertStableExplicitMslEnvironmentFailure(result, errors) {
  if (result.status === "blocked_metal_device_unavailable") {
    assert.equal(errors[0].code, "native_draw_shader_compile.metal_proof_failed");
    assert.equal(errors[0].metalStatus, "blocked_metal_device_unavailable");
    return;
  }

  assert.equal(result.status, "metal_explicit_msl_proof_unavailable");
  assert.equal(errors[0].code, "native_draw_shader_compile.metal_proof_unavailable");
  assert.equal(errors[0].metalStatus, "metal_explicit_msl_proof_unavailable");
  assert.ok(!JSON.stringify({ result, errors }).includes("Traceback"));
}

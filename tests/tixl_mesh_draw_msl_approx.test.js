const assert = require("node:assert/strict");
const { spawnSync } = require("node:child_process");
const fs = require("node:fs");
const os = require("node:os");
const path = require("node:path");
const test = require("node:test");

const repoRoot = path.resolve(__dirname, "..");
const contractPath = path.join(repoRoot, "docs/runtime/TIXL_MESH_DRAW_MSL_APPROX_PROOF.md");
const fixturePath = path.join(repoRoot, "docs/runtime/fixtures/tixl_mesh_draw_msl_approx.graph.json");
const scriptPath = path.join(repoRoot, "docs/runtime/scripts/tixl_mesh_draw_msl_approx_shell.py");
const artifactDir = path.join(repoRoot, "docs/runtime/artifacts/tixl_mesh_draw_msl_approx");
const layoutArtifactPath = path.join(
  repoRoot,
  "docs/runtime/artifacts/tixl_mesh_draw_buffer_layout/tixl_mesh_draw_buffer_layout_result.json",
);
const resultName = "tixl_mesh_draw_msl_approx_result.json";
const traceName = "tixl_mesh_draw_msl_approx_trace.json";
const errorsName = "tixl_mesh_draw_msl_approx_errors.json";

test("TiXL mesh draw MSL approximation docs stay inside the proof boundary", () => {
  const source = fs.readFileSync(contractPath, "utf8");

  assert.match(source, /TiXL Mesh Draw MSL Approximation Proof/);
  assert.match(source, /TixlMeshDrawMslApproxProof answers:/);
  assert.match(source, /explicit MSL approximation/);
  assert.match(source, /vertex_id/);
  assert.match(source, /FaceIndices/);
  assert.match(source, /not TiXL PBR parity/);
  assert.match(source, /not TiXL runtime parity/);
  assert.match(source, /not HLSL-to-MSL translation/);
  assert.match(source, /not native DrawMesh runtime/);
  assert.match(source, /not renderer\/backend replacement/);
  assert.match(source, /actualCompilerRan/);
  assert.match(source, /actualMetalRan/);
  assert.match(source, /mslApproximationRendered/);
  assert.match(source, /layoutArtifactConsumed/);
  assert.match(source, /mslApproxBufferPackingObserved: true only for this approximation probe/);
  assert.match(source, /hlslToMslTranslation: false/);
  assert.match(source, /pbrVisualCorrectness: false/);
});

test("TiXL mesh draw MSL approximation fixture points at layout artifact and tiny mesh", () => {
  const graph = readJson(fixturePath);

  assert.equal(graph.graphId, "fixture.tixl_mesh_draw_msl_approx");
  assert.equal(
    graph.layoutArtifact,
    "docs/runtime/artifacts/tixl_mesh_draw_buffer_layout/tixl_mesh_draw_buffer_layout_result.json",
  );
  assert.equal(graph.mesh.vertices.length, 3);
  assert.equal(graph.mesh.faces.length, 1);
  assert.deepEqual(graph.mesh.faces[0], [0, 1, 2]);
  for (const vertex of graph.mesh.vertices) {
    assert.equal(vertex.position.length, 3);
    assert.equal(vertex.normal.length, 3);
    assert.equal(vertex.tangent.length, 3);
    assert.equal(vertex.bitangent.length, 3);
    assert.equal(vertex.texCoord.length, 2);
    assert.equal(vertex.texCoord2.length, 2);
    assert.equal(typeof vertex.selected, "number");
    assert.equal(vertex.colorRGB.length, 3);
  }
  assert.equal(graph.expected.pbrVertexStrideBytes, 80);
  assert.equal(graph.expected.faceIndicesStrideBytes, 12);
  assert.equal(graph.expected.claims.mslApproxBufferPackingObserved, true);
  assert.equal(graph.expected.claims.tixlRuntimeParity, false);
  assert.equal(graph.expected.claims.hlslToMslTranslation, false);
  assert.equal(graph.expected.claims.pbrVisualCorrectness, false);
  assert.equal(graph.expected.claims.drawMeshRuntime, false);
});

test("TiXL mesh draw MSL approximation shell runs real Metal or blocks without fake frame", () => {
  const tmpDir = fs.mkdtempSync(path.join(os.tmpdir(), "tixl-mesh-draw-msl-approx-"));
  const run = spawnSync("python3", [scriptPath, fixturePath, tmpDir], {
    cwd: repoRoot,
    encoding: "utf8",
  });

  const result = readJson(path.join(tmpDir, resultName));
  const trace = readJson(path.join(tmpDir, traceName));
  const errors = readJson(path.join(tmpDir, errorsName));

  assert.ok(run.status === 0 || run.status === 1, run.stderr || run.stdout);
  assert.equal(result.kind, "TixlMeshDrawMslApproxProof");
  assert.equal(trace[0].fixture, "docs/runtime/fixtures/tixl_mesh_draw_msl_approx.graph.json");
  assertPathClean(result, trace, errors);
  assert.equal(fs.existsSync(path.join(tmpDir, "generated_explicit_msl_approx.metal")), true);
  assert.equal(fs.existsSync(path.join(tmpDir, "mesh_payload.json")), true);

  if (run.status === 0) {
    const stats = readJson(path.join(tmpDir, "frame_stats.json"));

    assert.equal(result.ok, true);
    assert.equal(result.status, "rendered_tixl_mesh_draw_msl_approximation");
    assert.equal(result.layoutArtifact.path, "docs/runtime/artifacts/tixl_mesh_draw_buffer_layout/tixl_mesh_draw_buffer_layout_result.json");
    assert.equal(result.layoutArtifact.previousMetalBufferPackingParity, false);
    assert.equal(result.meshSummary.vertexCount, 3);
    assert.equal(result.meshSummary.faceCount, 1);
    assert.equal(result.meshSummary.drawVertexCount, 3);
    assert.equal(result.meshSummary.pbrVertexStrideBytes, 80);
    assert.equal(result.meshSummary.faceIndicesStrideBytes, 12);
    assert.equal(result.controlFrameStats.nonBlack, false);
    assert.equal(result.controlFrameStats.nonBlackPixels, 0);
    assert.notEqual(result.controlFrameStats.frameDigest, result.frameStats.frameDigest);
    assert.equal(result.claims.layoutArtifactConsumed, true);
    assert.equal(result.claims.actualCompilerRan, true);
    assert.equal(result.claims.actualMetalRan, true);
    assert.equal(result.claims.mslApproximationRendered, true);
    assert.equal(result.claims.mslApproxBufferPackingObserved, true);
    assert.equal(result.claims.tixlRuntimeParity, false);
    assert.equal(result.claims.hlslToMslTranslation, false);
    assert.equal(result.claims.pbrVisualCorrectness, false);
    assert.equal(result.claims.drawMeshRuntime, false);
    assert.deepEqual(errors, []);
    assert.equal(stats.width, 16);
    assert.equal(stats.height, 16);
    assert.equal(stats.byteCount, 16 * 16 * 4);
    assert.equal(stats.nonBlack, true);
    assert.equal(stats.varied, true);
    assert.equal(stats.nonBlackPixels, 84);
    assert.equal(stats.uniqueColorSamples, 85);
    assert.equal(stats.opaquePixels, 256);
    assert.equal(stats.frameDigest, result.frameStats.frameDigest);
    return;
  }

  assert.equal(result.ok, false);
  assert.equal(result.status, "blocked_metal_device_unavailable");
  assert.equal(result.claims.layoutArtifactConsumed, true);
  assert.equal(result.claims.actualCompilerRan, false);
  assert.equal(result.claims.actualMetalRan, false);
  assert.equal(result.claims.mslApproximationRendered, false);
  assert.equal(result.claims.mslApproxBufferPackingObserved, false);
  assert.ok(errors.some((error) => /Metal device unavailable/i.test(error.message || "")));
  assert.equal(fs.existsSync(path.join(tmpDir, "frame_stats.json")), false);
});

test("TiXL mesh draw MSL approximation shell blocks missing layout artifact", () => {
  const tmpDir = fs.mkdtempSync(path.join(os.tmpdir(), "tixl-mesh-draw-msl-missing-layout-"));
  const fixture = readJson(fixturePath);
  fixture.graphId = "fixture.tixl_mesh_draw_msl_approx.missing_layout";
  fixture.layoutArtifact = "docs/runtime/artifacts/tixl_mesh_draw_buffer_layout/does-not-exist.json";
  const brokenFixturePath = path.join(tmpDir, "missing-layout.graph.json");
  fs.writeFileSync(brokenFixturePath, JSON.stringify(fixture, null, 2));

  const run = spawnSync("python3", [scriptPath, brokenFixturePath, tmpDir], {
    cwd: repoRoot,
    encoding: "utf8",
  });

  assert.equal(run.status, 1);
  const result = readJson(path.join(tmpDir, resultName));
  const trace = readJson(path.join(tmpDir, traceName));
  const errors = readJson(path.join(tmpDir, errorsName));
  assert.equal(result.ok, false);
  assert.equal(result.status, "blocked_missing_layout_artifact");
  assert.equal(result.claims.layoutArtifactConsumed, false);
  assert.equal(errors[0].code, "tixl_mesh_draw_msl_approx.layout_artifact_read_failed");
  assert.equal(fs.existsSync(path.join(tmpDir, "frame_stats.json")), false);
  assertPathClean(result, trace, errors);
});

test("TiXL mesh draw MSL approximation shell blocks invalid layout artifact", () => {
  const tmpDir = fs.mkdtempSync(path.join(os.tmpdir(), "tixl-mesh-draw-msl-invalid-layout-"));
  const invalidLayoutPath = path.join(tmpDir, "invalid-layout.json");
  fs.writeFileSync(invalidLayoutPath, JSON.stringify({
    kind: "TixlMeshDrawBufferLayoutProof",
    ok: true,
    status: "summarized_tixl_mesh_draw_buffer_layout",
    pbrVertex: { strideBytes: 84, fields: [] },
    faceIndices: { elementType: "int3", strideBytes: 12 },
    claims: { metalBufferPackingParity: true },
  }, null, 2));
  const fixture = readJson(fixturePath);
  fixture.graphId = "fixture.tixl_mesh_draw_msl_approx.invalid_layout";
  fixture.layoutArtifact = invalidLayoutPath;
  const brokenFixturePath = path.join(tmpDir, "invalid-layout.graph.json");
  fs.writeFileSync(brokenFixturePath, JSON.stringify(fixture, null, 2));

  const run = spawnSync("python3", [scriptPath, brokenFixturePath, tmpDir], {
    cwd: repoRoot,
    encoding: "utf8",
  });

  assert.equal(run.status, 1);
  const result = readJson(path.join(tmpDir, resultName));
  const trace = readJson(path.join(tmpDir, traceName));
  const errors = readJson(path.join(tmpDir, errorsName));
  assert.equal(result.ok, false);
  assert.equal(result.status, "blocked_invalid_layout_artifact");
  assert.equal(result.claims.layoutArtifactConsumed, false);
  assert.equal(errors[0].code, "tixl_mesh_draw_msl_approx.invalid_layout_artifact");
  assert.ok(errors[0].mismatches.some((mismatch) => mismatch.field === "pbrVertex.strideBytes"));
  assert.ok(errors[0].mismatches.some((mismatch) => mismatch.field === "claims.metalBufferPackingParity"));
  assert.equal(fs.existsSync(path.join(tmpDir, "frame_stats.json")), false);
  assertPathClean(result, trace, errors);
});

test("TiXL mesh draw MSL approximation shell reports invalid MSL compiler diagnostics", () => {
  const tmpDir = fs.mkdtempSync(path.join(os.tmpdir(), "tixl-mesh-draw-msl-invalid-source-"));
  const fixture = readJson(fixturePath);
  fixture.graphId = "fixture.tixl_mesh_draw_msl_approx.invalid_source";
  fixture.explicitMslSource = `#include <metal_stdlib>
using namespace metal;
struct PbrVertex80
{
    packed_float3 position;
    packed_float3 normal;
    packed_float3 tangent;
    packed_float3 bitangent;
    packed_float2 texCoord;
    packed_float2 texCoord2;
    float selected;
    packed_float3 colorRGB;
};
struct VertexOut { float4 position [[position]]; float3 color; };
vertex VertexOut my_world_mesh_vertex(
    uint vertexId [[vertex_id]],
    device const PbrVertex80* vertices [[buffer(0)]],
    device const packed_int3* faceIndices [[buffer(1)]])
{
    const uint faceId = vertexId / 3u;
    const uint corner = vertexId - faceId * 3u;
    const packed_int3 face = faceIndices[faceId];
    const int vertexIndex = face[corner];
    const PbrVertex80 pbrVertex = vertices[vertexIndex];
    VertexOut out;
    out.position = float4(float3(pbrVertex.position), 1.0);
    out.color = float3(pbrVertex.colorRGB);
    return out;
}
fragment float4 my_world_mesh_fragment(VertexOut in [[stage_in]]) { return float4(in.color, 1.0) }
`;
  const invalidFixturePath = path.join(tmpDir, "invalid-source.graph.json");
  fs.writeFileSync(invalidFixturePath, JSON.stringify(fixture, null, 2));

  const run = spawnSync("python3", [scriptPath, invalidFixturePath, tmpDir], {
    cwd: repoRoot,
    encoding: "utf8",
  });

  assert.equal(run.status, 1);
  const result = readJson(path.join(tmpDir, resultName));
  const errors = readJson(path.join(tmpDir, errorsName));

  assert.equal(result.ok, false);
  if (result.status === "blocked_metal_device_unavailable") {
    assert.ok(errors.some((error) => /Metal device unavailable/i.test(error.message || "")));
    return;
  }

  assert.equal(result.status, "compile_failed");
  assert.equal(result.claims.layoutArtifactConsumed, true);
  assert.equal(result.claims.actualCompilerRan, true);
  assert.equal(result.claims.actualMetalRan, false);
  assert.equal(result.claims.mslApproximationRendered, false);
  assert.equal(result.claims.mslApproxBufferPackingObserved, false);
  assert.ok(errors.some((error) => error.code === "tixl_mesh_draw_msl_approx.compile_failed"));
  assert.equal(fs.existsSync(path.join(tmpDir, "frame_stats.json")), false);
});

test("TiXL mesh draw MSL approximation checked-in artifacts are path-clean", () => {
  const result = readJson(path.join(artifactDir, resultName));
  const trace = readJson(path.join(artifactDir, traceName));
  const errors = readJson(path.join(artifactDir, errorsName));
  const combined = JSON.stringify([result, trace, errors]);

  assert.equal(result.kind, "TixlMeshDrawMslApproxProof");
  assert.ok(
    result.status === "rendered_tixl_mesh_draw_msl_approximation"
    || result.status === "blocked_metal_device_unavailable"
    || result.status === "blocked_missing_layout_artifact"
    || result.status === "blocked_invalid_layout_artifact",
  );
  assertPathClean(result, trace, errors);
  assert.ok(!combined.includes("/Users/"));

  if (result.status === "rendered_tixl_mesh_draw_msl_approximation") {
    const stats = readJson(path.join(artifactDir, "frame_stats.json"));
    assert.equal(result.ok, true);
    assert.equal(result.claims.layoutArtifactConsumed, true);
    assert.equal(result.claims.actualCompilerRan, true);
    assert.equal(result.claims.actualMetalRan, true);
    assert.equal(result.claims.mslApproximationRendered, true);
    assert.equal(result.claims.mslApproxBufferPackingObserved, true);
    assert.equal(result.claims.tixlRuntimeParity, false);
    assert.equal(result.claims.hlslToMslTranslation, false);
    assert.equal(result.claims.pbrVisualCorrectness, false);
    assert.equal(result.claims.drawMeshRuntime, false);
    assert.equal(stats.nonBlack, true);
    assert.equal(stats.varied, true);
    assert.equal(stats.nonBlackPixels, 84);
    assert.equal(stats.uniqueColorSamples, 85);
    assert.equal(stats.opaquePixels, 256);
    assert.equal(result.controlFrameStats.nonBlackPixels, 0);
    assert.notEqual(result.controlFrameStats.frameDigest, result.frameStats.frameDigest);
    return;
  }

  assert.equal(result.ok, false);
  assert.equal(result.claims.actualMetalRan, false);
  assert.equal(result.claims.mslApproximationRendered, false);
  assert.equal(result.claims.mslApproxBufferPackingObserved, false);
});

test("TiXL mesh draw MSL approximation source layout artifact exists for default checked-in proof", () => {
  assert.equal(fs.existsSync(layoutArtifactPath), true);
  const layout = readJson(layoutArtifactPath);
  assert.equal(layout.kind, "TixlMeshDrawBufferLayoutProof");
  assert.equal(layout.pbrVertex.strideBytes, 80);
  assert.equal(layout.faceIndices.strideBytes, 12);
  assert.equal(layout.claims.metalBufferPackingParity, false);
});

function assertPathClean(...values) {
  const text = JSON.stringify(values);
  assert.ok(!text.includes("/Users/"));
  assert.ok(!text.includes(repoRoot));
}

function readJson(filePath) {
  return JSON.parse(fs.readFileSync(filePath, "utf8"));
}

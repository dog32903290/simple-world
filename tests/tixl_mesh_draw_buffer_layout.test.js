const assert = require("node:assert/strict");
const { spawnSync } = require("node:child_process");
const fs = require("node:fs");
const os = require("node:os");
const path = require("node:path");
const test = require("node:test");

const repoRoot = path.resolve(__dirname, "..");
const contractPath = path.join(repoRoot, "docs/runtime/TIXL_MESH_DRAW_BUFFER_LAYOUT_PROOF.md");
const fixturePath = path.join(repoRoot, "docs/runtime/fixtures/tixl_mesh_draw_buffer_layout.graph.json");
const scriptPath = path.join(repoRoot, "docs/runtime/scripts/tixl_mesh_draw_buffer_layout_shell.py");
const artifactDir = path.join(repoRoot, "docs/runtime/artifacts/tixl_mesh_draw_buffer_layout");
const resultName = "tixl_mesh_draw_buffer_layout_result.json";
const traceName = "tixl_mesh_draw_buffer_layout_trace.json";
const errorsName = "tixl_mesh_draw_buffer_layout_errors.json";
const defaultLayoutSource = path.join(repoRoot, "external/tixl/Core/Rendering/PbrVertex.cs");
const defaultShaderSource = path.join(
  repoRoot,
  "external/tixl/Operators/Lib/Assets/shaders/3d/mesh/mesh-Draw.hlsl",
);
const defaultSourceAudit = path.join(
  repoRoot,
  "docs/runtime/artifacts/tixl_mesh_draw_shader_source_audit/tixl_mesh_draw_shader_source_audit_result.json",
);

test("TiXL mesh draw buffer layout docs define a layout proof, not draw/render/Metal parity", () => {
  const source = fs.readFileSync(contractPath, "utf8");

  assert.match(source, /TiXL Mesh Draw Buffer Layout Proof/);
  assert.match(source, /This is a layout proof/);
  assert.match(source, /not a draw proof/);
  assert.match(source, /not a render proof/);
  assert.match(source, /not Metal buffer parity/);
  assert.match(source, /not TiXL runtime parity/);
  assert.match(source, /not visual\s+correctness/);
  assert.match(source, /Artifacts must not contain donor source text/);
  assert.match(source, /PbrVertex\.cs/);
  assert.match(source, /mesh-Draw\.hlsl/);
  assert.match(source, /stride: 80 bytes/);
  assert.match(source, /StructuredBuffer<int3> FaceIndices/);
  assert.match(source, /face-index stride at 12 bytes/);
  assert.match(source, /drawVertexCount = faceCount \* 3/);
  assert.match(source, /does not claim triangulation/);
  assert.match(source, /blocked_missing_donor_layout_source/);
  assert.match(source, /blocked_missing_source_audit/);
  assert.match(source, /metalBufferPackingParity/);
});

test("TiXL mesh draw buffer layout fixture pins donor evidence and expected claims", () => {
  const graph = JSON.parse(fs.readFileSync(fixturePath, "utf8"));

  assert.equal(graph.graphId, "fixture.tixl_mesh_draw_buffer_layout");
  assert.equal(graph.donorLayoutSource, "external/tixl/Core/Rendering/PbrVertex.cs");
  assert.equal(graph.donorShaderSource, "external/tixl/Operators/Lib/Assets/shaders/3d/mesh/mesh-Draw.hlsl");
  assert.equal(
    graph.sourceAuditArtifact,
    "docs/runtime/artifacts/tixl_mesh_draw_shader_source_audit/tixl_mesh_draw_shader_source_audit_result.json",
  );
  assert.equal(graph.expected.pbrVertexStrideBytes, 80);
  assert.equal(graph.expected.faceIndicesStrideBytes, 12);
  assert.equal(graph.expected.topology, "TriangleList");
  assert.equal(graph.expected.drawVertexCountFormula, "faceCount * 3");
  assert.equal(graph.expected.claims.contractLayoutSummarized, true);
  assert.equal(graph.expected.claims.metalBufferPackingParity, false);
  assert.equal(graph.expected.claims.tixlRuntimeParity, false);
  assert.equal(graph.expected.claims.visualCorrectness, false);
});

test("TiXL mesh draw buffer layout shell handles the default fixture", () => {
  const tmpDir = fs.mkdtempSync(path.join(os.tmpdir(), "tixl-mesh-draw-buffer-layout-"));
  const run = spawnSync("python3", [scriptPath, fixturePath, tmpDir], {
    cwd: repoRoot,
    encoding: "utf8",
  });
  const result = readJson(path.join(tmpDir, resultName));
  const trace = readJson(path.join(tmpDir, traceName));
  const errors = readJson(path.join(tmpDir, errorsName));

  if (!fs.existsSync(defaultLayoutSource)) {
    assert.equal(run.status, 1);
    assert.equal(result.ok, false);
    assert.equal(result.status, "blocked_missing_donor_layout_source");
    assert.equal(errors[0].code, "tixl_mesh_draw_buffer_layout.donor_layout_source_missing");
    assertPathClean(result, trace, errors);
    return;
  }
  if (!fs.existsSync(defaultSourceAudit)) {
    assert.equal(run.status, 1);
    assert.equal(result.ok, false);
    assert.equal(result.status, "blocked_missing_source_audit");
    assert.equal(errors[0].code, "tixl_mesh_draw_buffer_layout.source_audit_missing");
    assertPathClean(result, trace, errors);
    return;
  }
  if (!fs.existsSync(defaultShaderSource)) {
    assert.equal(run.status, 1);
    assert.equal(result.ok, false);
    assert.equal(result.status, "blocked_missing_donor_shader_source");
    assert.equal(errors[0].code, "tixl_mesh_draw_buffer_layout.donor_shader_source_missing");
    assertPathClean(result, trace, errors);
    return;
  }

  assert.equal(run.status, 0, run.stderr || run.stdout);
  assert.equal(result.kind, "TixlMeshDrawBufferLayoutProof");
  assert.equal(result.ok, true);
  assert.equal(result.status, "summarized_tixl_mesh_draw_buffer_layout");
  assert.equal(result.evidence.donorLayoutSource.path, "external/tixl/Core/Rendering/PbrVertex.cs");
  assert.equal(result.evidence.donorShaderSource.path, "external/tixl/Operators/Lib/Assets/shaders/3d/mesh/mesh-Draw.hlsl");
  assert.equal(
    result.evidence.sourceAuditArtifact.path,
    "docs/runtime/artifacts/tixl_mesh_draw_shader_source_audit/tixl_mesh_draw_shader_source_audit_result.json",
  );
  assert.equal(result.evidence.sourceAuditArtifact.kind, "TixlMeshDrawShaderSourceAudit");
  assert.equal(result.pbrVertex.struct, "PbrVertex");
  assert.equal(result.pbrVertex.layoutKind, "Explicit");
  assert.equal(result.pbrVertex.strideBytes, 80);
  assert.deepEqual(layoutFields(result.pbrVertex.fields), expectedPbrVertexFields());
  assert.equal(result.faceIndices.buffer, "FaceIndices");
  assert.equal(result.faceIndices.found, true);
  assert.equal(result.faceIndices.elementType, "int3");
  assert.equal(result.faceIndices.register, "t1");
  assert.equal(result.faceIndices.strideBytes, 12);
  assert.equal(result.drawContract.topology, "TriangleList");
  assert.equal(result.drawContract.drawVertexCountFormula, "faceCount * 3");
  assert.equal(result.drawContract.triangulationClaimed, false);
  assert.ok(result.semanticBlockers.some((blocker) => blocker.code === "metal_buffer_packing_parity_not_proven"));
  assert.ok(result.semanticBlockers.some((blocker) => blocker.code === "tixl_runtime_parity_not_proven"));
  assert.ok(result.semanticBlockers.some((blocker) => blocker.code === "visual_correctness_not_proven"));
  assert.equal(result.claims.contractLayoutSummarized, true);
  assert.equal(result.claims.metalBufferPackingParity, false);
  assert.equal(result.claims.tixlRuntimeParity, false);
  assert.equal(result.claims.visualCorrectness, false);
  assert.deepEqual(errors, []);
  assert.deepEqual(trace.map((entry) => entry.op), [
    "loadTixlMeshDrawBufferLayoutFixture",
    "resolveDonorEvidence",
    "summarizePbrVertexLayout",
    "summarizeFaceIndicesLayout",
    "publishTixlMeshDrawBufferLayoutArtifacts",
  ]);
  assertPathClean(result, trace, errors);
});

test("TiXL mesh draw buffer layout shell blocks a missing donor layout source", () => {
  const tmpDir = fs.mkdtempSync(path.join(os.tmpdir(), "tixl-mesh-draw-buffer-missing-layout-"));
  const fixture = JSON.parse(fs.readFileSync(fixturePath, "utf8"));
  fixture.graphId = "fixture.tixl_mesh_draw_buffer_layout.missing_layout";
  fixture.donorLayoutSource = "external/tixl/Core/Rendering/DoesNotExistPbrVertex.cs";
  const missingFixturePath = path.join(tmpDir, "missing-layout.graph.json");
  fs.writeFileSync(missingFixturePath, JSON.stringify(fixture, null, 2));

  const run = spawnSync("python3", [scriptPath, missingFixturePath, tmpDir], {
    cwd: repoRoot,
    encoding: "utf8",
  });

  assert.equal(run.status, 1);
  const result = readJson(path.join(tmpDir, resultName));
  const trace = readJson(path.join(tmpDir, traceName));
  const errors = readJson(path.join(tmpDir, errorsName));
  assert.equal(result.ok, false);
  assert.equal(result.status, "blocked_missing_donor_layout_source");
  assert.equal(result.evidence.donorLayoutSource.path, "external/tixl/Core/Rendering/DoesNotExistPbrVertex.cs");
  assert.equal(result.claims.contractLayoutSummarized, false);
  assert.equal(errors[0].code, "tixl_mesh_draw_buffer_layout.donor_layout_source_missing");
  assertPathClean(result, trace, errors);
});

test("TiXL mesh draw buffer layout shell blocks a missing source audit artifact", () => {
  const tmpDir = fs.mkdtempSync(path.join(os.tmpdir(), "tixl-mesh-draw-buffer-missing-audit-"));
  const fixture = JSON.parse(fs.readFileSync(fixturePath, "utf8"));
  fixture.graphId = "fixture.tixl_mesh_draw_buffer_layout.missing_audit";
  fixture.sourceAuditArtifact = "docs/runtime/artifacts/tixl_mesh_draw_shader_source_audit/does-not-exist.json";
  const missingFixturePath = path.join(tmpDir, "missing-audit.graph.json");
  fs.writeFileSync(missingFixturePath, JSON.stringify(fixture, null, 2));

  const run = spawnSync("python3", [scriptPath, missingFixturePath, tmpDir], {
    cwd: repoRoot,
    encoding: "utf8",
  });

  assert.equal(run.status, 1);
  const result = readJson(path.join(tmpDir, resultName));
  const trace = readJson(path.join(tmpDir, traceName));
  const errors = readJson(path.join(tmpDir, errorsName));
  assert.equal(result.ok, false);
  assert.equal(result.status, "blocked_missing_source_audit");
  assert.equal(
    result.evidence.sourceAuditArtifact.path,
    "docs/runtime/artifacts/tixl_mesh_draw_shader_source_audit/does-not-exist.json",
  );
  assert.equal(result.claims.contractLayoutSummarized, false);
  assert.equal(errors[0].code, "tixl_mesh_draw_buffer_layout.source_audit_missing");
  assertPathClean(result, trace, errors);
});

test("TiXL mesh draw buffer layout shell blocks a missing donor shader source", () => {
  const tmpDir = fs.mkdtempSync(path.join(os.tmpdir(), "tixl-mesh-draw-buffer-missing-shader-"));
  const fixture = JSON.parse(fs.readFileSync(fixturePath, "utf8"));
  fixture.graphId = "fixture.tixl_mesh_draw_buffer_layout.missing_shader";
  fixture.donorShaderSource = "external/tixl/Operators/Lib/Assets/shaders/3d/mesh/does-not-exist.hlsl";
  const missingFixturePath = path.join(tmpDir, "missing-shader.graph.json");
  fs.writeFileSync(missingFixturePath, JSON.stringify(fixture, null, 2));

  const run = spawnSync("python3", [scriptPath, missingFixturePath, tmpDir], {
    cwd: repoRoot,
    encoding: "utf8",
  });

  assert.equal(run.status, 1);
  const result = readJson(path.join(tmpDir, resultName));
  const trace = readJson(path.join(tmpDir, traceName));
  const errors = readJson(path.join(tmpDir, errorsName));
  assert.equal(result.ok, false);
  assert.equal(result.status, "blocked_missing_donor_shader_source");
  assert.equal(
    result.evidence.donorShaderSource.path,
    "external/tixl/Operators/Lib/Assets/shaders/3d/mesh/does-not-exist.hlsl",
  );
  assert.equal(result.claims.contractLayoutSummarized, false);
  assert.equal(errors[0].code, "tixl_mesh_draw_buffer_layout.donor_shader_source_missing");
  assertPathClean(result, trace, errors);
});

test("TiXL mesh draw buffer layout shell blocks broken source audit artifacts", () => {
  const tmpDir = fs.mkdtempSync(path.join(os.tmpdir(), "tixl-mesh-draw-buffer-broken-audit-"));
  const brokenAuditPath = path.join(tmpDir, "broken-source-audit.json");
  fs.writeFileSync(brokenAuditPath, JSON.stringify({
    kind: "WrongAudit",
    ok: false,
    status: "not_the_audit",
    donorSource: {
      path: "external/tixl/wrong.hlsl",
      sha256: "wrong",
    },
    resources: [
      { name: "PbrVertices", elementType: "PbrVertex", register: "t9" },
      { name: "FaceIndices", elementType: "int3", register: "t8" },
    ],
  }, null, 2));

  const fixture = JSON.parse(fs.readFileSync(fixturePath, "utf8"));
  fixture.graphId = "fixture.tixl_mesh_draw_buffer_layout.broken_audit";
  fixture.sourceAuditArtifact = brokenAuditPath;
  const brokenFixturePath = path.join(tmpDir, "broken-audit.graph.json");
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
  assert.equal(result.status, "blocked_layout_contract_mismatch");
  assert.equal(result.claims.contractLayoutSummarized, false);
  const mismatchBlocker = result.semanticBlockers.find((blocker) => blocker.code === "layout_contract_mismatch");
  assert.ok(mismatchBlocker);
  const mismatchFields = mismatchBlocker.mismatches.map((mismatch) => mismatch.field);
  assert.ok(mismatchFields.includes("sourceAudit.kind"));
  assert.ok(mismatchFields.includes("sourceAudit.ok"));
  assert.ok(mismatchFields.includes("sourceAudit.status"));
  assert.ok(mismatchFields.includes("sourceAudit.donorSource.path"));
  assert.ok(mismatchFields.includes("sourceAudit.donorSource.sha256"));
  assert.ok(mismatchFields.includes("sourceAudit.resources.PbrVertices"));
  assert.ok(mismatchFields.includes("sourceAudit.resources.FaceIndices"));
  assert.equal(errors[0].code, "tixl_mesh_draw_buffer_layout.layout_contract_mismatch");
  assertPathClean(result, trace, errors);
});

test("TiXL mesh draw buffer layout shell writes artifacts when layout parsing fails", () => {
  const tmpDir = fs.mkdtempSync(path.join(os.tmpdir(), "tixl-mesh-draw-buffer-parse-fail-"));
  const layoutPath = path.join(tmpDir, "PbrVertex.cs");
  fs.writeFileSync(layoutPath, `
using System.Runtime.InteropServices;

[StructLayout(LayoutKind.Explicit, Size = Stride)]
public struct PbrVertex
{
  [FieldOffset(0)]
  public Vector3 Position;

  [FieldOffset(1 << 2)]
  public Vector3 Normal;

  public const int Stride = 20 * 4;
}
`);
  const fixture = JSON.parse(fs.readFileSync(fixturePath, "utf8"));
  fixture.graphId = "fixture.tixl_mesh_draw_buffer_layout.parse_fail";
  fixture.donorLayoutSource = layoutPath;
  const parseFailFixturePath = path.join(tmpDir, "parse-fail.graph.json");
  fs.writeFileSync(parseFailFixturePath, JSON.stringify(fixture, null, 2));

  const run = spawnSync("python3", [scriptPath, parseFailFixturePath, tmpDir], {
    cwd: repoRoot,
    encoding: "utf8",
  });

  assert.equal(run.status, 1);
  const result = readJson(path.join(tmpDir, resultName));
  const trace = readJson(path.join(tmpDir, traceName));
  const errors = readJson(path.join(tmpDir, errorsName));
  assert.equal(result.ok, false);
  assert.equal(result.status, "blocked_layout_parse_failed");
  assert.equal(result.claims.contractLayoutSummarized, false);
  assert.equal(errors[0].code, "tixl_mesh_draw_buffer_layout.layout_parse_failed");
  assert.ok(result.semanticBlockers.some((blocker) => blocker.code === "layout_parse_failed"));
  assertPathClean(result, trace, errors);
});

test("TiXL mesh draw buffer layout checked-in artifacts are path-clean summaries", () => {
  const result = readJson(path.join(artifactDir, resultName));
  const trace = readJson(path.join(artifactDir, traceName));
  const errors = readJson(path.join(artifactDir, errorsName));
  const combined = JSON.stringify([result, trace, errors]);

  assert.equal(result.kind, "TixlMeshDrawBufferLayoutProof");
  assert.ok(
    result.status === "summarized_tixl_mesh_draw_buffer_layout"
    || result.status === "blocked_missing_donor_layout_source"
    || result.status === "blocked_missing_source_audit"
    || result.status === "blocked_missing_donor_shader_source",
  );
  assertPathClean(result, trace, errors);
  assert.ok(!combined.includes("/Users/"));
  assert.ok(!combined.includes("public Vector3 Position"));
  assert.ok(!combined.includes("public const int Stride"));
  assert.ok(!combined.includes("PbrVertex vertex = PbrVertices"));
  assert.ok(!combined.includes("int faceIndex = id / 3"));
  assert.equal(result.claims.metalBufferPackingParity, false);
  assert.equal(result.claims.tixlRuntimeParity, false);
  assert.equal(result.claims.visualCorrectness, false);
  if (result.ok) {
    assert.equal(result.claims.contractLayoutSummarized, true);
    assert.equal(result.pbrVertex.strideBytes, 80);
    assert.deepEqual(layoutFields(result.pbrVertex.fields), expectedPbrVertexFields());
    assert.equal(result.faceIndices.strideBytes, 12);
  }
});

function expectedPbrVertexFields() {
  return [
    { name: "Position", type: "float3", offsetBytes: 0, sizeBytes: 12 },
    { name: "Normal", type: "float3", offsetBytes: 12, sizeBytes: 12 },
    { name: "Tangent", type: "float3", offsetBytes: 24, sizeBytes: 12 },
    { name: "Bitangent", type: "float3", offsetBytes: 36, sizeBytes: 12 },
    { name: "TexCoord", type: "float2", offsetBytes: 48, sizeBytes: 8 },
    { name: "TexCoord2", type: "float2", offsetBytes: 56, sizeBytes: 8 },
    { name: "Selected", type: "float", offsetBytes: 64, sizeBytes: 4 },
    { name: "ColorRGB", type: "float3", offsetBytes: 68, sizeBytes: 12 },
  ];
}

function layoutFields(fields) {
  return fields.map((field) => ({
    name: field.name,
    type: field.type,
    offsetBytes: field.offsetBytes,
    sizeBytes: field.sizeBytes,
  }));
}

function assertPathClean(...values) {
  const text = JSON.stringify(values);
  assert.ok(!text.includes("/Users/"));
  assert.ok(!text.includes(repoRoot));
}

function readJson(filePath) {
  return JSON.parse(fs.readFileSync(filePath, "utf8"));
}

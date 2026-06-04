const assert = require("node:assert/strict");
const { spawnSync } = require("node:child_process");
const fs = require("node:fs");
const os = require("node:os");
const path = require("node:path");
const test = require("node:test");

const repoRoot = path.resolve(__dirname, "..");
const contractPath = path.join(repoRoot, "docs/runtime/TIXL_MESH_DRAW_SHADER_SOURCE_AUDIT.md");
const fixturePath = path.join(repoRoot, "docs/runtime/fixtures/tixl_mesh_draw_shader_source_audit.graph.json");
const scriptPath = path.join(repoRoot, "docs/runtime/scripts/tixl_mesh_draw_shader_source_audit_shell.py");
const artifactDir = path.join(repoRoot, "docs/runtime/artifacts/tixl_mesh_draw_shader_source_audit");
const resultName = "tixl_mesh_draw_shader_source_audit_result.json";
const traceName = "tixl_mesh_draw_shader_source_audit_trace.json";
const errorsName = "tixl_mesh_draw_shader_source_audit_errors.json";
const defaultDonorPath = path.join(
  repoRoot,
  "external/tixl/Operators/Lib/Assets/shaders/3d/mesh/mesh-Draw.hlsl",
);

test("TiXL mesh draw shader source audit docs define a source audit, not compile/render/parity", () => {
  const source = fs.readFileSync(contractPath, "utf8");

  assert.match(source, /TiXL Mesh Draw Shader Source Audit/);
  assert.match(source, /This is a source audit/);
  assert.match(source, /not a compile proof/);
  assert.match(source, /not a render proof/);
  assert.match(source, /not TiXL parity/);
  assert.match(source, /not\s+HLSL-to-MSL translation/);
  assert.match(source, /not native backend integration/);
  assert.match(source, /not PBR visual\s+correctness/);
  assert.match(source, /Artifacts must not contain the full donor source text/);
  assert.match(source, /blocked_missing_donor_source/);
  assert.match(source, /hlslToMslTranslationProven/);
});

test("TiXL mesh draw shader source audit fixture pins donor path and include roots", () => {
  const graph = JSON.parse(fs.readFileSync(fixturePath, "utf8"));

  assert.equal(graph.graphId, "fixture.tixl_mesh_draw_shader_source_audit");
  assert.equal(graph.donorSource, "external/tixl/Operators/Lib/Assets/shaders/3d/mesh/mesh-Draw.hlsl");
  assert.deepEqual(graph.includeSearchRoots, ["external/tixl/Operators/Lib/Assets/shaders"]);
  assert.equal(graph.expected.vertexEntry, "vsMain");
  assert.equal(graph.expected.pixelEntry, "psMain");
  assert.ok(graph.expected.directIncludes.includes("shared/pbr-render.hlsl"));
  assert.equal(graph.expected.claims.hlslToMslTranslationProven, false);
  assert.equal(graph.expected.claims.tixlParity, false);
  assert.equal(graph.expected.claims.nativeCompileParity, false);
  assert.equal(graph.expected.claims.pbrVisualCorrectness, false);
});

test("TiXL mesh draw shader source audit shell handles the default fixture", () => {
  const tmpDir = fs.mkdtempSync(path.join(os.tmpdir(), "tixl-mesh-draw-audit-"));
  const run = spawnSync("python3", [scriptPath, fixturePath, tmpDir], {
    cwd: repoRoot,
    encoding: "utf8",
  });
  const result = readJson(path.join(tmpDir, resultName));
  const trace = readJson(path.join(tmpDir, traceName));
  const errors = readJson(path.join(tmpDir, errorsName));

  if (!fs.existsSync(defaultDonorPath)) {
    assert.equal(run.status, 1);
    assert.equal(result.ok, false);
    assert.equal(result.status, "blocked_missing_donor_source");
    assert.equal(errors[0].code, "tixl_mesh_draw_shader_source_audit.donor_source_missing");
    assertPathClean(result, trace, errors);
    return;
  }

  assert.equal(run.status, 0, run.stderr || run.stdout);
  assert.equal(result.kind, "TixlMeshDrawShaderSourceAudit");
  assert.equal(result.ok, true);
  assert.equal(result.status, "audited_tixl_mesh_draw_source");
  assert.equal(result.donorSource.path, "external/tixl/Operators/Lib/Assets/shaders/3d/mesh/mesh-Draw.hlsl");
  assert.equal(result.entryPoints.vsMain.found, true);
  assert.equal(result.entryPoints.psMain.found, true);
  assertIncludes(result.includeGraph.directIncludes, [
    "shared/point.hlsl",
    "shared/quat-functions.hlsl",
    "shared/point-light.hlsl",
    "shared/pbr.hlsl",
    "shared/pbr-render.hlsl",
  ]);
  assert.ok(result.includeGraph.edges.some((edge) => (
    edge.include === "shared/pbr.hlsl"
    && edge.from === "external/tixl/Operators/Lib/Assets/shaders/shared/pbr-render.hlsl"
  )));
  assert.ok(result.requiredBuffers.some((buffer) => buffer.name === "Transforms" && buffer.register === "b0"));
  assert.ok(result.resources.some((resource) => resource.name === "PbrVertices" && resource.register === "t0"));
  assert.ok(result.resources.some((resource) => resource.name === "BRDFLookup" && resource.register === "t7"));
  assert.ok(result.samplers.some((sampler) => sampler.name === "WrappedSampler" && sampler.register === "s0"));
  assert.ok(result.constants.some((constant) => constant.name === "ObjectToClipSpace" && constant.buffer === "Transforms"));
  assert.ok(result.templateHoles.some((hole) => hole.name === "FIELD_CALL"));
  assert.ok(result.semanticBlockers.some((blocker) => blocker.code === "requires_hlsl_to_msl_translation_lane"));
  assert.ok(result.semanticBlockers.some((blocker) => blocker.code === "shader_template_holes_require_tixl_expansion"));
  assert.ok(result.symbolSummary.functions.includes("ComputePbr"));
  assert.ok(result.symbolSummary.functions.includes("vsMain"));
  assert.ok(result.symbolSummary.functions.includes("psMain"));
  assert.ok(!result.symbolSummary.functions.includes("if"));
  assert.ok(!result.symbolSummary.functions.includes("float4"));
  assert.ok(!result.symbolSummary.functions.includes("normalize"));
  assert.equal(result.claims.hlslToMslTranslationProven, false);
  assert.equal(result.claims.tixlParity, false);
  assert.equal(result.claims.nativeCompileParity, false);
  assert.equal(result.claims.pbrVisualCorrectness, false);
  assert.deepEqual(errors, []);
  assert.deepEqual(trace.map((entry) => entry.op), [
    "loadTixlMeshDrawShaderSourceAuditFixture",
    "resolveDonorSource",
    "parseIncludeGraph",
    "publishTixlMeshDrawShaderSourceAuditArtifacts",
  ]);
  assertPathClean(result, trace, errors);
});

test("TiXL mesh draw shader source audit blocks a donor with missing required entry points", () => {
  const tmpDir = fs.mkdtempSync(path.join(os.tmpdir(), "tixl-mesh-draw-missing-entry-"));
  const donorPath = path.join(tmpDir, "mesh-Draw.hlsl");
  fs.writeFileSync(donorPath, `
struct psInput
{
  float4 pixelPosition;
};

psInput vsMain(uint id)
{
  psInput output;
  output.pixelPosition = float4(0, 0, 0, 1);
  return output;
}
`);
  const fixture = {
    graphId: "fixture.tixl_mesh_draw_shader_source_audit.missing_entry",
    donorSource: donorPath,
    includeSearchRoots: [],
  };
  const missingEntryFixturePath = path.join(tmpDir, "missing-entry.graph.json");
  fs.writeFileSync(missingEntryFixturePath, JSON.stringify(fixture, null, 2));

  const run = spawnSync("python3", [scriptPath, missingEntryFixturePath, tmpDir], {
    cwd: repoRoot,
    encoding: "utf8",
  });

  assert.equal(run.status, 1);
  const result = readJson(path.join(tmpDir, resultName));
  const trace = readJson(path.join(tmpDir, traceName));
  const errors = readJson(path.join(tmpDir, errorsName));
  assert.equal(result.ok, false);
  assert.equal(result.status, "blocked_missing_entry_points");
  assert.equal(result.entryPoints.vsMain.found, true);
  assert.equal(result.entryPoints.psMain.found, false);
  assert.ok(result.semanticBlockers.some((blocker) => (
    blocker.code === "missing_shader_entry_points"
    && blocker.missing.includes("psMain")
  )));
  assert.ok(errors.some((error) => (
    error.code === "tixl_mesh_draw_shader_source_audit.entry_points_missing"
    && error.missing.includes("psMain")
  )));
  assertPathClean(result, trace, errors);
});

test("TiXL mesh draw shader source audit shell blocks a missing donor source", () => {
  const tmpDir = fs.mkdtempSync(path.join(os.tmpdir(), "tixl-mesh-draw-missing-"));
  const fixture = JSON.parse(fs.readFileSync(fixturePath, "utf8"));
  fixture.graphId = "fixture.tixl_mesh_draw_shader_source_audit.missing";
  fixture.donorSource = "external/tixl/Operators/Lib/Assets/shaders/3d/mesh/does-not-exist.hlsl";
  const missingFixturePath = path.join(tmpDir, "missing.graph.json");
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
  assert.equal(result.status, "blocked_missing_donor_source");
  assert.equal(result.donorSource.path, "external/tixl/Operators/Lib/Assets/shaders/3d/mesh/does-not-exist.hlsl");
  assert.equal(errors[0].code, "tixl_mesh_draw_shader_source_audit.donor_source_missing");
  assertPathClean(result, trace, errors);
});

test("TiXL mesh draw shader source checked-in artifacts are path-clean summaries", () => {
  const result = readJson(path.join(artifactDir, resultName));
  const trace = readJson(path.join(artifactDir, traceName));
  const errors = readJson(path.join(artifactDir, errorsName));
  const combined = JSON.stringify([result, trace, errors]);

  assert.equal(result.kind, "TixlMeshDrawShaderSourceAudit");
  assert.ok(result.status === "audited_tixl_mesh_draw_source" || result.status === "blocked_missing_donor_source");
  assertPathClean(result, trace, errors);
  assert.ok(!combined.includes("/Users/"));
  assert.ok(!combined.includes("psOutput psMain(psInput pin)"));
  assert.ok(!combined.includes("PbrVertex vertex = PbrVertices"));
  assert.ok(!combined.includes("float4 litColor = ComputePbr()"));
  assert.equal(result.claims.hlslToMslTranslationProven, false);
  assert.equal(result.claims.tixlParity, false);
  assert.equal(result.claims.nativeCompileParity, false);
  assert.equal(result.claims.pbrVisualCorrectness, false);
});

function assertIncludes(directIncludes, includeNames) {
  const names = directIncludes.map((include) => include.include);
  for (const includeName of includeNames) {
    assert.ok(names.includes(includeName), `missing include ${includeName}`);
  }
}

function assertPathClean(...values) {
  const text = JSON.stringify(values);
  assert.ok(!text.includes("/Users/"));
  assert.ok(!text.includes(repoRoot));
}

function readJson(filePath) {
  return JSON.parse(fs.readFileSync(filePath, "utf8"));
}

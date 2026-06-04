const assert = require("node:assert/strict");
const { spawnSync } = require("node:child_process");
const fs = require("node:fs");
const os = require("node:os");
const path = require("node:path");
const test = require("node:test");

const repoRoot = path.resolve(__dirname, "..");
const contractPath = path.join(repoRoot, "docs/runtime/TIXL_MESH_DRAW_SHADERGRAPH_RESOURCES_EXPANSION_PROOF.md");
const fixturePath = path.join(repoRoot, "docs/runtime/fixtures/tixl_mesh_draw_shadergraph_resources_expansion.graph.json");
const scriptPath = path.join(repoRoot, "docs/runtime/scripts/tixl_mesh_draw_shadergraph_resources_expansion_shell.py");
const artifactDir = path.join(repoRoot, "docs/runtime/artifacts/tixl_mesh_draw_shadergraph_resources_expansion");
const b5ArtifactPath = path.join(repoRoot, "docs/runtime/artifacts/tixl_mesh_draw_b5_shadergraph_params_expansion/tixl_mesh_draw_b5_shadergraph_params_expansion_result.json");
const resultName = "tixl_mesh_draw_shadergraph_resources_expansion_result.json";
const traceName = "tixl_mesh_draw_shadergraph_resources_expansion_trace.json";
const errorsName = "tixl_mesh_draw_shadergraph_resources_expansion_errors.json";

test("TiXL mesh draw shadergraph resources docs define an empty t8 resources proof", () => {
  const source = fs.readFileSync(contractPath, "utf8");

  assert.match(source, /TiXL Mesh Draw ShaderGraph Resources Expansion Proof/);
  assert.match(source, /\/\*\{RESOURCES\(t8\)\}\*\//);
  assert.match(source, /ShaderGraphNode\.CollectResources/);
  assert.match(source, /IGraphNodeOp\.AppendShaderResources/);
  assert.match(source, /SphereSDF/);
  assert.match(source, /current SphereSDF fixture has zero t8\+ resources/);
  assert.match(source, /does not prove real SRV creation/);
  assert.match(source, /prove_stage_mrt_matrix_semantics_for_handwritten_mesh_draw_adapter/);
  assert.match(source, /prove_texturecube_samplelevel_getdimensions_and_pbr_visual_reference/);
});

test("TiXL mesh draw shadergraph resources fixture pins bounded claims", () => {
  const graph = readJson(fixturePath);

  assert.equal(graph.graphId, "fixture.tixl_mesh_draw_shadergraph_resources_expansion");
  assert.equal(graph.kind, "TixlMeshDrawShadergraphResourcesExpansionProof");
  assert.equal(graph.expected.status, "proven_empty_t8_shadergraph_resources_for_sphere_sdf_fixture");
  assert.equal(graph.shadergraphResourceExpansion.rootNode.tixlSymbolChildId, "04426d9c-b039-4a92-9b1f-61186b4df2e5");
  assert.deepEqual(graph.expected.claims, expectedClaims());
  assertForbiddenClaimsFalse(graph.expected.claims);
});

test("TiXL mesh draw shadergraph resources shell proves empty t8 resources for current SphereSDF fixture", () => {
  const tmpDir = fs.mkdtempSync(path.join(os.tmpdir(), "tixl-resources-empty-"));
  const run = spawnSync("python3", [scriptPath, fixturePath, tmpDir], {
    cwd: repoRoot,
    encoding: "utf8",
  });

  assert.equal(run.status, 0, run.stderr || run.stdout);
  const result = readJson(path.join(tmpDir, resultName));
  const trace = readJson(path.join(tmpDir, traceName));
  const errors = readJson(path.join(tmpDir, errorsName));

  assert.deepEqual(errors, []);
  assert.equal(result.kind, "TixlMeshDrawShadergraphResourcesExpansionProof");
  assert.equal(result.ok, true);
  assert.equal(result.status, "proven_empty_t8_shadergraph_resources_for_sphere_sdf_fixture");
  assert.deepEqual(result.claims, expectedClaims());
  assertForbiddenClaimsFalse(result.claims);
  assert.equal(result.resourceExpansion.registerStart, "t8");
  assert.equal(result.resourceExpansion.hook, "RESOURCES(t8)");
  assert.deepEqual(result.resourceExpansion.baseShaderResources.map((resource) => [resource.register, resource.name]), [
    ["t0", "PbrVertices"],
    ["t1", "FaceIndices"],
    ["t2", "BaseColorMap"],
    ["t3", "EmissiveColorMap"],
    ["t4", "RSMOMap"],
    ["t5", "NormalMap"],
    ["t6", "PrefilteredSpecular"],
    ["t7", "BRDFLookup"],
  ]);
  assert.deepEqual(result.resourceExpansion.visitedShaderGraphNodes, ["SphereSDF_nG1CBDm"]);
  assert.deepEqual(result.resourceExpansion.resourceTypes, []);
  assert.deepEqual(result.resourceExpansion.resourceReferences, []);
  assert.deepEqual(result.resourceExpansion.resourceDefinitions, []);
  assert.equal(result.resourceExpansion.resourceViewsCount, 0);
  assert.equal(result.resourceExpansion.generatedResourceHlsl, "");
  assert.equal(result.resourceExpansion.currentFixtureT8ResourcesEmpty, true);
  assert.equal(result.stageAppendBehavior.validated, true);
  assert.equal(result.stageAppendBehavior.sourceOutput, "GenerateShaderGraphCode.Resources");
  assert.equal(result.stageAppendBehavior.targetInput, "SetPixelAndVertexShaderStage.VariousResources");
  assert.equal(result.stageAppendBehavior.appendedAfterOrdinaryShaderResources, true);
  assert.equal(trace[0].fixture, "docs/runtime/fixtures/tixl_mesh_draw_shadergraph_resources_expansion.graph.json");
  assertPathClean(result, trace, errors);
});

test("TiXL mesh draw shadergraph resources shell blocks missing RESOURCES(t8) hook", () => {
  const tmpDir = fs.mkdtempSync(path.join(os.tmpdir(), "tixl-resources-missing-hook-"));
  const fixture = readJson(fixturePath);
  const source = fs.readFileSync(path.join(repoRoot, fixture.meshDrawTemplateSource), "utf8").replace("/*{RESOURCES(t8)}*/", "");
  const forgedTemplate = path.join(tmpDir, "mesh-Draw.hlsl");
  fs.writeFileSync(forgedTemplate, source);
  fixture.meshDrawTemplateSource = forgedTemplate;
  const brokenFixture = writeTempFixture(tmpDir, fixture);

  const run = spawnSync("python3", [scriptPath, brokenFixture, tmpDir], {
    cwd: repoRoot,
    encoding: "utf8",
  });

  assert.equal(run.status, 1);
  const result = readJson(path.join(tmpDir, resultName));
  const errors = readJson(path.join(tmpDir, errorsName));
  assert.equal(result.ok, false);
  assert.equal(result.status, "blocked_invalid_source_facts");
  assert.equal(result.claims.resourcesHookFound, false);
  assert.equal(errors[0].code, "tixl_mesh_draw_shadergraph_resources_expansion.invalid_source_facts");
  assertPathClean(result, errors);
});

test("TiXL mesh draw shadergraph resources shell blocks broken CollectResources injection path", () => {
  const tmpDir = fs.mkdtempSync(path.join(os.tmpdir(), "tixl-resources-broken-generator-"));
  const fixture = readJson(fixturePath);
  const source = fs.readFileSync(path.join(repoRoot, fixture.generateShaderGraphCodeSource), "utf8").replace("_graphNode.CollectResources", "_graphNode.CollectResources_REMOVED");
  const forgedGenerator = path.join(tmpDir, "GenerateShaderGraphCode.cs");
  fs.writeFileSync(forgedGenerator, source);
  fixture.generateShaderGraphCodeSource = forgedGenerator;
  const brokenFixture = writeTempFixture(tmpDir, fixture);

  const run = spawnSync("python3", [scriptPath, brokenFixture, tmpDir], {
    cwd: repoRoot,
    encoding: "utf8",
  });

  assert.equal(run.status, 1);
  const result = readJson(path.join(tmpDir, resultName));
  const errors = readJson(path.join(tmpDir, errorsName));
  assert.equal(result.status, "blocked_invalid_source_facts");
  assert.equal(result.claims.collectResourcesPathValidated, false);
  const fields = errors.flatMap((error) => error.mismatches || []).map((mismatch) => mismatch.field);
  assert.ok(fields.includes("GenerateShaderGraphCode.cs.CollectResources"));
  assertPathClean(result, errors);
});

test("TiXL mesh draw shadergraph resources shell blocks non-empty default AppendShaderResources", () => {
  const tmpDir = fs.mkdtempSync(path.join(os.tmpdir(), "tixl-resources-nonempty-default-"));
  const fixture = readJson(fixturePath);
  const original = fs.readFileSync(path.join(repoRoot, fixture.graphNodeOpSource), "utf8");
  const source = original.replace(/void\s+AppendShaderResources\s*\(\s*ref\s+List<ShaderGraphNode\.SrvBufferReference>\s+list\s*\)\s*\{\s*\}/, "void AppendShaderResources(ref List<ShaderGraphNode.SrvBufferReference> list)\n    {\n        list.Clear();\n    }");
  assert.notEqual(source, original);
  const forgedOp = path.join(tmpDir, "IGraphNodeOp.cs");
  fs.writeFileSync(forgedOp, source);
  fixture.graphNodeOpSource = forgedOp;
  const brokenFixture = writeTempFixture(tmpDir, fixture);

  const run = spawnSync("python3", [scriptPath, brokenFixture, tmpDir], {
    cwd: repoRoot,
    encoding: "utf8",
  });

  assert.equal(run.status, 1);
  const result = readJson(path.join(tmpDir, resultName));
  const errors = readJson(path.join(tmpDir, errorsName));
  assert.equal(result.status, "blocked_invalid_source_facts");
  assert.equal(result.claims.sphereSdfAppendResourcesEmpty, false);
  const fields = errors.flatMap((error) => error.mismatches || []).map((mismatch) => mismatch.field);
  assert.ok(fields.includes("IGraphNodeOp.cs.AppendShaderResources"));
  assertPathClean(result, errors);
});

test("TiXL mesh draw shadergraph resources shell blocks SphereSDF resource override", () => {
  const tmpDir = fs.mkdtempSync(path.join(os.tmpdir(), "tixl-resources-sphere-override-"));
  const fixture = readJson(fixturePath);
  const source = fs.readFileSync(path.join(repoRoot, fixture.shadergraphResourceExpansion.rootNode.sourceEvidence.csharp), "utf8").replace("\n\n}", "\n\n    void IGraphNodeOp.AppendShaderResources(ref List<ShaderGraphNode.SrvBufferReference> list) { }\n}");
  const forgedSphere = path.join(tmpDir, "SphereSDF.cs");
  fs.writeFileSync(forgedSphere, source);
  fixture.shadergraphResourceExpansion.rootNode.sourceEvidence.csharp = forgedSphere;
  const brokenFixture = writeTempFixture(tmpDir, fixture);

  const run = spawnSync("python3", [scriptPath, brokenFixture, tmpDir], {
    cwd: repoRoot,
    encoding: "utf8",
  });

  assert.equal(run.status, 1);
  const result = readJson(path.join(tmpDir, resultName));
  const errors = readJson(path.join(tmpDir, errorsName));
  assert.equal(result.status, "blocked_non_empty_t8_resources");
  assert.equal(result.claims.sphereSdfAppendResourcesEmpty, false);
  const fields = errors.flatMap((error) => error.mismatches || []).map((mismatch) => mismatch.field);
  assert.ok(fields.includes("SphereSDF.cs.AppendShaderResources"));
  assertPathClean(result, errors);
});

test("TiXL mesh draw shadergraph resources shell blocks widened b5 artifact claims", () => {
  const tmpDir = fs.mkdtempSync(path.join(os.tmpdir(), "tixl-resources-widened-b5-"));
  const fixture = readJson(fixturePath);
  const b5 = readJson(b5ArtifactPath);
  b5.claims.fullPbrResourceBinding = true;
  b5.claims.nonEmptyT8ResourcesProven = true;
  const forgedB5 = path.join(tmpDir, "b5.json");
  fs.writeFileSync(forgedB5, JSON.stringify(b5, null, 2));
  fixture.b5ShadergraphParamsExpansionArtifact = forgedB5;
  const brokenFixture = writeTempFixture(tmpDir, fixture);

  const run = spawnSync("python3", [scriptPath, brokenFixture, tmpDir], {
    cwd: repoRoot,
    encoding: "utf8",
  });

  assert.equal(run.status, 1);
  const result = readJson(path.join(tmpDir, resultName));
  const errors = readJson(path.join(tmpDir, errorsName));
  assert.equal(result.status, "blocked_invalid_b5_expansion_artifact");
  assert.equal(result.claims.b5ExpansionArtifactConsumed, false);
  const fields = errors.flatMap((error) => error.mismatches || []).map((mismatch) => mismatch.field);
  assert.ok(fields.includes("b5.claims.fullPbrResourceBinding"));
  assert.ok(fields.includes("b5.claims.nonEmptyT8ResourcesProven"));
  assertPathClean(result, errors);
});

test("TiXL mesh draw shadergraph resources shell blocks widened fixture claims", () => {
  const tmpDir = fs.mkdtempSync(path.join(os.tmpdir(), "tixl-resources-widened-fixture-"));
  const fixture = readJson(fixturePath);
  fixture.expected.claims.backendReplacementReady = true;
  fixture.expected.claims.fullPbrResourceBinding = true;
  const brokenFixture = writeTempFixture(tmpDir, fixture);

  const run = spawnSync("python3", [scriptPath, brokenFixture, tmpDir], {
    cwd: repoRoot,
    encoding: "utf8",
  });

  assert.equal(run.status, 1);
  const result = readJson(path.join(tmpDir, resultName));
  const errors = readJson(path.join(tmpDir, errorsName));
  assert.equal(result.status, "blocked_invalid_fixture");
  assert.equal(errors[0].code, "tixl_mesh_draw_shadergraph_resources_expansion.invalid_fixture_expectations");
  assertPathClean(result, errors);
});

test("TiXL mesh draw shadergraph resources shell blocks forged SphereSDF root identity", () => {
  const tmpDir = fs.mkdtempSync(path.join(os.tmpdir(), "tixl-resources-forged-root-"));
  const fixture = readJson(fixturePath);
  fixture.shadergraphResourceExpansion.rootNode.prefix = "SphereSDF_FAKE";
  fixture.shadergraphResourceExpansion.rootNode.tixlSymbolChildId = "fake-child-id";
  const brokenFixture = writeTempFixture(tmpDir, fixture);

  const run = spawnSync("python3", [scriptPath, brokenFixture, tmpDir], {
    cwd: repoRoot,
    encoding: "utf8",
  });

  assert.equal(run.status, 1);
  const result = readJson(path.join(tmpDir, resultName));
  const errors = readJson(path.join(tmpDir, errorsName));
  assert.equal(result.ok, false);
  assert.equal(result.status, "blocked_invalid_source_facts");
  assert.equal(result.claims.currentFixtureT8ResourcesEmpty, false);
  const fields = errors.flatMap((error) => error.mismatches || []).map((mismatch) => mismatch.field);
  assert.ok(fields.includes("shadergraphResourceExpansion.rootNode.prefix"));
  assert.ok(fields.includes("shadergraphResourceExpansion.rootNode.tixlSymbolChildId"));
  assertPathClean(result, errors);
});

test("TiXL mesh draw shadergraph resources shell blocks missing ShaderTests source evidence", () => {
  const tmpDir = fs.mkdtempSync(path.join(os.tmpdir(), "tixl-resources-missing-t3-"));
  const fixture = readJson(fixturePath);
  fixture.shadergraphResourceExpansion.source = path.join(tmpDir, "missing-source.t3");
  fixture.shadergraphResourceExpansion.rootNode.sourceEvidence.example = path.join(tmpDir, "missing-example.t3");
  const brokenFixture = writeTempFixture(tmpDir, fixture);

  const run = spawnSync("python3", [scriptPath, brokenFixture, tmpDir], {
    cwd: repoRoot,
    encoding: "utf8",
  });

  assert.equal(run.status, 1);
  const result = readJson(path.join(tmpDir, resultName));
  const errors = readJson(path.join(tmpDir, errorsName));
  assert.equal(result.ok, false);
  assert.equal(result.status, "blocked_invalid_source_facts");
  assert.equal(result.claims.currentFixtureT8ResourcesEmpty, false);
  const fields = errors.flatMap((error) => error.mismatches || []).map((mismatch) => mismatch.field);
  assert.ok(fields.includes("SphereSDF.sourceEvidence"));
  assertPathClean(result, errors);
});

test("TiXL mesh draw shadergraph resources checked-in artifacts match fresh shell output", () => {
  const tmpDir = fs.mkdtempSync(path.join(os.tmpdir(), "tixl-resources-stale-check-"));
  const run = spawnSync("python3", [scriptPath, fixturePath, tmpDir], {
    cwd: repoRoot,
    encoding: "utf8",
  });
  assert.equal(run.status, 0, run.stderr || run.stdout);

  const checkedInResult = readJson(path.join(artifactDir, resultName));
  const checkedInTrace = readJson(path.join(artifactDir, traceName));
  const checkedInErrors = readJson(path.join(artifactDir, errorsName));
  const freshResult = readJson(path.join(tmpDir, resultName));
  const freshTrace = readJson(path.join(tmpDir, traceName));
  const freshErrors = readJson(path.join(tmpDir, errorsName));

  assert.deepEqual(checkedInResult, freshResult);
  assert.deepEqual(checkedInTrace, freshTrace);
  assert.deepEqual(checkedInErrors, freshErrors);
  assertPathClean(checkedInResult, checkedInTrace, checkedInErrors);
});

function expectedClaims() {
  return {
    sourceFilesValidated: true,
    b5ExpansionArtifactConsumed: true,
    resourcesHookFound: true,
    collectResourcesPathValidated: true,
    sphereSdfAppendResourcesEmpty: true,
    currentFixtureT8ResourcesEmpty: true,
    shadergraphResourcesExpansionProven: true,
    stageAppendBehaviorSourceValidated: true,
    nonEmptyT8ResourcesProven: false,
    realSrvCreationProven: false,
    constantBufferAdapterComplete: false,
    textureSamplerMapping: false,
    nativeCompileParity: false,
    hlslToMslTranslationProven: false,
    fullPbrResourceBinding: false,
    backendReplacementReady: false,
    hlslToMslTranslation: false,
    tixlRuntimeParity: false,
    pbrVisualCorrectness: false,
    rendererIntegrationComplete: false,
  };
}

function assertForbiddenClaimsFalse(claims) {
  for (const field of [
    "nonEmptyT8ResourcesProven",
    "realSrvCreationProven",
    "constantBufferAdapterComplete",
    "textureSamplerMapping",
    "nativeCompileParity",
    "hlslToMslTranslationProven",
    "fullPbrResourceBinding",
    "backendReplacementReady",
    "hlslToMslTranslation",
    "tixlRuntimeParity",
    "pbrVisualCorrectness",
    "rendererIntegrationComplete",
  ]) {
    assert.equal(claims[field], false, `${field} must stay false`);
  }
}

function writeTempFixture(tmpDir, fixture) {
  const brokenFixture = path.join(tmpDir, "fixture.graph.json");
  fs.writeFileSync(brokenFixture, JSON.stringify(fixture, null, 2));
  return brokenFixture;
}

function assertPathClean(...values) {
  const text = JSON.stringify(values);
  assert.ok(!text.includes("/Users/"));
  assert.ok(!text.includes(repoRoot));
}

function readJson(filePath) {
  return JSON.parse(fs.readFileSync(filePath, "utf8"));
}

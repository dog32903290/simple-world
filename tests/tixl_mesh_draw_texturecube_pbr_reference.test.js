const assert = require("node:assert/strict");
const crypto = require("node:crypto");
const { spawnSync } = require("node:child_process");
const fs = require("node:fs");
const os = require("node:os");
const path = require("node:path");
const test = require("node:test");

const repoRoot = path.resolve(__dirname, "..");
const contractPath = path.join(repoRoot, "docs/runtime/TIXL_MESH_DRAW_TEXTURECUBE_PBR_REFERENCE_PROOF.md");
const fixturePath = path.join(repoRoot, "docs/runtime/fixtures/tixl_mesh_draw_texturecube_pbr_reference.graph.json");
const scriptPath = path.join(repoRoot, "docs/runtime/scripts/tixl_mesh_draw_texturecube_pbr_reference_shell.py");
const artifactDir = path.join(repoRoot, "docs/runtime/artifacts/tixl_mesh_draw_texturecube_pbr_reference");
const resultName = "tixl_mesh_draw_texturecube_pbr_reference_result.json";
const traceName = "tixl_mesh_draw_texturecube_pbr_reference_trace.json";
const errorsName = "tixl_mesh_draw_texturecube_pbr_reference_errors.json";
const mslName = "generated_texturecube_pbr_reference_probe.metal";

test("TiXL mesh draw TextureCube/PBR reference docs state the bounded proof", () => {
  const source = fs.readFileSync(contractPath, "utf8");

  assert.match(source, /TiXL Mesh Draw TextureCube PBR Reference Proof/);
  assert.match(source, /TextureCube SampleLevel -> Metal texturecube\.sample.*level\(0\.0\).*level\(1\.0\)/s);
  assert.match(source, /TextureCube GetDimensions -> Metal get_width\(0\), get_height\(0\), get_width\(1\), get_height\(1\), get_num_mip_levels\(\)/);
  assert.match(source, /boundedPbrVisualReferenceEstablished: true/);
  assert.match(source, /pbrVisualCorrectness: false/);
  assert.match(source, /fullPbrResourceBinding: false/);
  assert.match(source, /backendReplacementReady: false/);
  assert.match(source, /hlslToMslTranslation: false/);
  assert.match(source, /TiXL runtime parity: false/);
});

test("TiXL mesh draw TextureCube/PBR reference fixture consumes prior bounded artifacts", () => {
  const graph = readJson(fixturePath);

  assert.equal(graph.graphId, "fixture.tixl_mesh_draw_texturecube_pbr_reference");
  assert.equal(graph.kind, "TixlMeshDrawTextureCubePbrReferenceProof");
  assert.equal(
    graph.stageMrtMatrixArtifact,
    "docs/runtime/artifacts/tixl_mesh_draw_stage_mrt_matrix/tixl_mesh_draw_stage_mrt_matrix_result.json",
  );
  assert.equal(
    graph.textureSamplerBindingArtifact,
    "docs/runtime/artifacts/tixl_mesh_draw_texture_sampler_binding/tixl_mesh_draw_texture_sampler_binding_result.json",
  );
  assert.equal(
    graph.b5NativePackingArtifact,
    "docs/runtime/artifacts/tixl_mesh_draw_b5_native_packing/tixl_mesh_draw_b5_native_packing_result.json",
  );
  assert.equal(
    graph.shadergraphResourcesExpansionArtifact,
    "docs/runtime/artifacts/tixl_mesh_draw_shadergraph_resources_expansion/tixl_mesh_draw_shadergraph_resources_expansion_result.json",
  );
  assert.equal(
    graph.hlslToMslVerdictArtifact,
    "docs/runtime/artifacts/tixl_mesh_draw_hlsl_to_msl_verdict/tixl_mesh_draw_hlsl_to_msl_verdict_result.json",
  );
  assert.equal(
    graph.sourceAuditArtifact,
    "docs/runtime/artifacts/tixl_mesh_draw_shader_source_audit/tixl_mesh_draw_shader_source_audit_result.json",
  );
  assert.deepEqual(graph.textureCubeApiMapping, {
    sourceApi: ["TextureCube.SampleLevel", "TextureCube.GetDimensions"],
    metalApi: ["texturecube.sample(..., level(0.0))", "texturecube.sample(..., level(1.0))", "get_width(0)", "get_height(0)", "get_width(1)", "get_height(1)", "get_num_mip_levels()"],
  });
  assert.deepEqual(graph.expected.claims, expectedClaims(true));
});

test("TiXL mesh draw TextureCube/PBR reference shell publishes bounded artifacts", () => {
  const tmpDir = fs.mkdtempSync(path.join(os.tmpdir(), "tixl-texturecube-pbr-reference-"));
  const run = spawnSync("python3", [scriptPath, fixturePath, tmpDir], {
    cwd: repoRoot,
    encoding: "utf8",
  });

  assert.ok(run.status === 0 || run.status === 1, run.stderr || run.stdout);
  const result = readJson(path.join(tmpDir, resultName));
  const trace = readJson(path.join(tmpDir, traceName));
  const errors = readJson(path.join(tmpDir, errorsName));

  assert.equal(result.kind, "TixlMeshDrawTextureCubePbrReferenceProof");
  assert.deepEqual(trace.slice(0, 5).map((entry) => entry.op), [
    "loadTixlMeshDrawTextureCubePbrReferenceFixture",
    "resolveInputArtifacts",
    "readInputArtifacts",
    "validateInputArtifacts",
    "validateFixtureExpectations",
  ]);
  assertPathClean(result, trace, errors);

  if (run.status === 1) {
    assert.equal(result.ok, false);
    assert.equal(result.status, "blocked_metal_device_unavailable");
    assert.equal(result.claims.actualMetalTextureCubeProbeRan, false);
    assert.equal(fs.existsSync(path.join(tmpDir, mslName)), false);
    assert.ok(errors.some((error) => /Metal device unavailable/i.test(error.message || "")));
    return;
  }

  assert.deepEqual(errors, []);
  assert.equal(result.ok, true);
  assert.equal(result.status, "proven_texturecube_samplelevel_getdimensions_and_bounded_pbr_reference");
  assert.deepEqual(result.claims, expectedClaims(true));
  assert.deepEqual(result.textureCubeApiProbe.dimensions, { width: 4, height: 4, mipLevels: 2 });
  assert.deepEqual(result.textureCubeApiProbe.mip1Dimensions, { width: 2, height: 2 });
  assert.deepEqual(result.textureCubeApiProbe.sampleLevel0Rgba8, [52, 86, 120, 255]);
  assert.deepEqual(result.textureCubeApiProbe.sampleLevel1Rgba8, [140, 30, 200, 255]);
  assert.equal(result.textureCubeApiProbe.generatedMslArtifact, mslName);
  assert.deepEqual(result.boundedPbrVisualReference.sentinelRgba8, [68, 62, 54, 255]);
  assert.equal(result.boundedPbrVisualReference.comparison.status, "matched_bounded_sentinel");
  assert.equal(fs.existsSync(path.join(tmpDir, mslName)), true);
});

test("TiXL mesh draw TextureCube/PBR reference shell blocks missing cube API source evidence", () => {
  for (const missing of ["SampleLevel", "GetDimensions", "donorSource"]) {
    const tmpDir = fs.mkdtempSync(path.join(os.tmpdir(), `tixl-texturecube-missing-${missing}-`));
    const source = readJson(path.join(
      repoRoot,
      "docs/runtime/artifacts/tixl_mesh_draw_shader_source_audit/tixl_mesh_draw_shader_source_audit_result.json",
    ));
    if (missing === "donorSource") {
      delete source.donorSource;
    } else {
      const pbrRenderEntry = source.includeGraph.files.find((file) => file.source.path.endsWith("shared/pbr-render.hlsl"));
      const donorPath = path.join(repoRoot, pbrRenderEntry.source.path);
      let donorText = fs.readFileSync(donorPath, "utf8");
      if (missing === "SampleLevel") {
        donorText = donorText.replaceAll("SampleLevel", "SampleLevel_MISSING");
      } else {
        donorText = donorText.replaceAll("GetDimensions", "GetDimensions_MISSING");
      }
      const relativeBrokenDonor = path.join("tmp-test-donors", `mesh-Draw-missing-${missing}.hlsl`);
      const brokenDonorPath = path.join(tmpDir, relativeBrokenDonor);
      fs.mkdirSync(path.dirname(brokenDonorPath), { recursive: true });
      fs.writeFileSync(brokenDonorPath, donorText);
      pbrRenderEntry.source.path = path.relative(repoRoot, brokenDonorPath);
      pbrRenderEntry.source.sha256 = crypto.createHash("sha256").update(donorText).digest("hex");
    }
    const sourcePath = path.join(tmpDir, "missing-cube-api-source.json");
    fs.writeFileSync(sourcePath, JSON.stringify(source, null, 2));
    const fixture = readJson(fixturePath);
    fixture.sourceAuditArtifact = sourcePath;
    const brokenFixturePath = path.join(tmpDir, "missing-cube-api.graph.json");
    fs.writeFileSync(brokenFixturePath, JSON.stringify(fixture, null, 2));

    const run = spawnSync("python3", [scriptPath, brokenFixturePath, tmpDir], {
      cwd: repoRoot,
      encoding: "utf8",
    });

    assert.equal(run.status, 1, missing);
    const result = readJson(path.join(tmpDir, resultName));
    const errors = readJson(path.join(tmpDir, errorsName));
    const fields = errors.flatMap((error) => error.mismatches || []).map((mismatch) => mismatch.field);
    assert.equal(result.status, "blocked_invalid_input_artifact");
    assert.equal(result.claims.actualMetalTextureCubeProbeRan, false);
    if (missing === "donorSource") {
      assert.ok(fields.includes("sourceAudit.donorSource.path"));
    } else {
      assert.ok(fields.some((field) => field.startsWith(`sourceAudit.sourceGraph.TextureCube.${missing}`)));
    }
    assert.equal(fs.existsSync(path.join(tmpDir, mslName)), false);
    assertPathClean(result, errors);
  }
});

test("TiXL mesh draw TextureCube/PBR reference shell sanitizes external absolute artifact paths", () => {
  const tmpDir = fs.mkdtempSync(path.join(os.tmpdir(), "tixl-texturecube-external-path-"));
  const source = readJson(path.join(
    repoRoot,
    "docs/runtime/artifacts/tixl_mesh_draw_shader_source_audit/tixl_mesh_draw_shader_source_audit_result.json",
  ));
  const externalSourcePath = path.join(tmpDir, "external-source-audit.json");
  fs.writeFileSync(externalSourcePath, JSON.stringify(source, null, 2));
  const fixture = readJson(fixturePath);
  fixture.sourceAuditArtifact = externalSourcePath;
  const externalFixturePath = path.join(tmpDir, "external-path.graph.json");
  fs.writeFileSync(externalFixturePath, JSON.stringify(fixture, null, 2));

  const run = spawnSync("python3", [scriptPath, externalFixturePath, tmpDir], {
    cwd: repoRoot,
    encoding: "utf8",
  });

  assert.equal(run.status, 0, run.stderr || run.stdout);
  const result = readJson(path.join(tmpDir, resultName));
  const trace = readJson(path.join(tmpDir, traceName));
  const errors = readJson(path.join(tmpDir, errorsName));
  assert.equal(result.inputArtifacts.sourceAudit.path, "external-artifact:external-source-audit.json");
  assertPathClean(result, trace, errors);
});

test("TiXL mesh draw TextureCube/PBR reference shell blocks stale or missing prior artifacts", () => {
  const tmpDir = fs.mkdtempSync(path.join(os.tmpdir(), "tixl-texturecube-stale-stage-"));
  const stage = readJson(path.join(
    repoRoot,
    "docs/runtime/artifacts/tixl_mesh_draw_stage_mrt_matrix/tixl_mesh_draw_stage_mrt_matrix_result.json",
  ));
  stage.claims.textureSamplerBindingArtifactConsumed = false;
  stage.status = "stale_stage_without_texture_sampler";
  const stagePath = path.join(tmpDir, "stale-stage.json");
  fs.writeFileSync(stagePath, JSON.stringify(stage, null, 2));
  const fixture = readJson(fixturePath);
  fixture.stageMrtMatrixArtifact = stagePath;
  fixture.textureSamplerBindingArtifact = path.join(tmpDir, "missing-texture-sampler.json");
  const brokenFixturePath = path.join(tmpDir, "stale-priors.graph.json");
  fs.writeFileSync(brokenFixturePath, JSON.stringify(fixture, null, 2));

  const run = spawnSync("python3", [scriptPath, brokenFixturePath, tmpDir], {
    cwd: repoRoot,
    encoding: "utf8",
  });

  assert.equal(run.status, 1);
  const result = readJson(path.join(tmpDir, resultName));
  const errors = readJson(path.join(tmpDir, errorsName));
  assert.equal(result.status, "blocked_missing_or_invalid_input_artifact");
  assert.equal(result.claims.actualMetalTextureCubeProbeRan, false);
  assert.ok(errors.some((error) => error.code === "tixl_mesh_draw_texturecube_pbr_reference.texture_sampler_binding_read_failed"));
  assert.ok(errors.some((error) => error.code === "tixl_mesh_draw_texturecube_pbr_reference.invalid_stage_mrt_matrix_artifact"));
  assertPathClean(result, errors);
});

test("TiXL mesh draw TextureCube/PBR reference shell blocks forged widened claims", () => {
  const tmpDir = fs.mkdtempSync(path.join(os.tmpdir(), "tixl-texturecube-widened-"));
  const fixture = readJson(fixturePath);
  fixture.expected.claims.fullPbrResourceBinding = true;
  fixture.expected.claims.pbrVisualCorrectness = true;
  fixture.expected.claims.hlslToMslTranslation = true;
  const brokenFixturePath = path.join(tmpDir, "widened-fixture.graph.json");
  fs.writeFileSync(brokenFixturePath, JSON.stringify(fixture, null, 2));

  const run = spawnSync("python3", [scriptPath, brokenFixturePath, tmpDir], {
    cwd: repoRoot,
    encoding: "utf8",
  });

  assert.equal(run.status, 1);
  const result = readJson(path.join(tmpDir, resultName));
  const errors = readJson(path.join(tmpDir, errorsName));
  const fields = errors.flatMap((error) => error.mismatches || []).map((mismatch) => mismatch.field);
  assert.equal(result.status, "blocked_invalid_fixture_expectations");
  assert.ok(fields.includes("expected.claims.fullPbrResourceBinding"));
  assert.ok(fields.includes("expected.claims.pbrVisualCorrectness"));
  assert.ok(fields.includes("expected.claims.hlslToMslTranslation"));
  assert.equal(result.claims.actualMetalTextureCubeProbeRan, false);
  assertPathClean(result, errors);
});

test("TiXL mesh draw TextureCube/PBR reference checked-in artifacts are path-clean and fresh when Metal succeeds", () => {
  const result = readJson(path.join(artifactDir, resultName));
  const trace = readJson(path.join(artifactDir, traceName));
  const errors = readJson(path.join(artifactDir, errorsName));

  assert.equal(result.kind, "TixlMeshDrawTextureCubePbrReferenceProof");
  assert.ok(result.status === "proven_texturecube_samplelevel_getdimensions_and_bounded_pbr_reference" || result.status === "blocked_metal_device_unavailable");
  assertPathClean(result, trace, errors);
  assert.ok(!JSON.stringify({ result, trace, errors }).includes("/Users/"));

  if (result.status !== "proven_texturecube_samplelevel_getdimensions_and_bounded_pbr_reference") {
    assert.equal(result.ok, false);
    assert.equal(result.claims.actualMetalTextureCubeProbeRan, false);
    assert.equal(fs.existsSync(path.join(artifactDir, mslName)), false);
    return;
  }

  assert.equal(result.ok, true);
  assert.deepEqual(errors, []);
  assert.deepEqual(result.claims, expectedClaims(true));

  const tmpDir = fs.mkdtempSync(path.join(os.tmpdir(), "tixl-texturecube-pbr-fresh-"));
  const run = spawnSync("python3", [scriptPath, fixturePath, tmpDir], {
    cwd: repoRoot,
    encoding: "utf8",
  });
  assert.equal(run.status, 0, run.stderr || run.stdout);
  assert.deepEqual(readJson(path.join(tmpDir, resultName)), result);
  assert.deepEqual(readJson(path.join(tmpDir, traceName)), trace);
  assert.deepEqual(readJson(path.join(tmpDir, errorsName)), errors);
  assert.equal(
    fs.readFileSync(path.join(tmpDir, mslName), "utf8"),
    fs.readFileSync(path.join(artifactDir, mslName), "utf8"),
  );
});

function expectedClaims(actualProbeRan) {
  return {
    sourceAuditArtifactConsumed: actualProbeRan,
    stageMrtMatrixArtifactConsumed: actualProbeRan,
    textureSamplerBindingArtifactConsumed: actualProbeRan,
    b5NativePackingArtifactConsumed: actualProbeRan,
    shadergraphResourcesExpansionArtifactConsumed: actualProbeRan,
    hlslToMslVerdictArtifactConsumed: actualProbeRan,
    actualMetalTextureCubeProbeRan: actualProbeRan,
    textureCubeSampleLevelProven: actualProbeRan,
    textureCubeGetDimensionsProven: actualProbeRan,
    boundedPbrVisualReferenceEstablished: actualProbeRan,
    fullPbrResourceBinding: false,
    backendReplacementReady: false,
    hlslToMslTranslation: false,
    tixlRuntimeParity: false,
    pbrVisualCorrectness: false,
    rendererIntegrationComplete: false,
    fullTextureSamplerMapping: false,
    nativeCompileParity: false,
  };
}

function assertPathClean(...values) {
  const text = JSON.stringify(values);
  assert.ok(!text.includes("/Users/"));
  assert.ok(!text.includes(repoRoot));
}

function readJson(filePath) {
  return JSON.parse(fs.readFileSync(filePath, "utf8"));
}

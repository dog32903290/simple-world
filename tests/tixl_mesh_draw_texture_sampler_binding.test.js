const assert = require("node:assert/strict");
const { spawnSync } = require("node:child_process");
const fs = require("node:fs");
const os = require("node:os");
const path = require("node:path");
const test = require("node:test");

const repoRoot = path.resolve(__dirname, "..");
const contractPath = path.join(repoRoot, "docs/runtime/TIXL_MESH_DRAW_TEXTURE_SAMPLER_BINDING_PROOF.md");
const fixturePath = path.join(repoRoot, "docs/runtime/fixtures/tixl_mesh_draw_texture_sampler_binding.graph.json");
const scriptPath = path.join(repoRoot, "docs/runtime/scripts/tixl_mesh_draw_texture_sampler_binding_shell.py");
const artifactDir = path.join(repoRoot, "docs/runtime/artifacts/tixl_mesh_draw_texture_sampler_binding");
const resultName = "tixl_mesh_draw_texture_sampler_binding_result.json";
const traceName = "tixl_mesh_draw_texture_sampler_binding_trace.json";
const errorsName = "tixl_mesh_draw_texture_sampler_binding_errors.json";

test("TiXL mesh draw texture sampler binding docs state the narrow four-slot lane", () => {
  const source = fs.readFileSync(contractPath, "utf8");

  assert.match(source, /TiXL Mesh Draw Texture Sampler Binding Proof/);
  assert.match(source, /t2 BaseColorMap -> Metal texture\(2\)/);
  assert.match(source, /t7 BRDFLookup -> Metal texture\(7\)/);
  assert.match(source, /s0 WrappedSampler -> Metal sampler\(0\)/);
  assert.match(source, /s1 ClampedSampler -> Metal sampler\(1\)/);
  assert.match(source, /actualMetalTextureSamplerProbeRan: true/);
  assert.match(source, /boundedTextureSamplerMappingSubset: \["t2", "t7", "s0", "s1"\]/);
  assert.match(source, /fullPbrResourceBinding: false/);
  assert.match(source, /backendReplacementReady: false/);
  assert.match(source, /constantBufferAdapterComplete: false/);
});

test("TiXL mesh draw texture sampler binding fixture points at source, prior, and strategy artifacts", () => {
  const graph = readJson(fixturePath);

  assert.equal(graph.graphId, "fixture.tixl_mesh_draw_texture_sampler_binding");
  assert.equal(graph.kind, "TixlMeshDrawTextureSamplerBindingProof");
  assert.equal(
    graph.sourceAuditArtifact,
    "docs/runtime/artifacts/tixl_mesh_draw_shader_source_audit/tixl_mesh_draw_shader_source_audit_result.json",
  );
  assert.equal(
    graph.priorResourceBindingArtifact,
    "docs/runtime/artifacts/tixl_mesh_draw_resource_binding/tixl_mesh_draw_resource_binding_result.json",
  );
  assert.equal(
    graph.strategyArtifact,
    "docs/runtime/artifacts/tixl_mesh_draw_explicit_translation_strategy/tixl_mesh_draw_explicit_translation_strategy_result.json",
  );
  assert.deepEqual(graph.adapterMapping.boundSubset, ["t2", "t7", "s0", "s1"]);
  assert.deepEqual(graph.expected.claims, expectedClaims(true));
});

test("TiXL mesh draw texture sampler binding shell validates artifacts before Metal probe", () => {
  const tmpDir = fs.mkdtempSync(path.join(os.tmpdir(), "tixl-mesh-draw-texture-sampler-"));
  const run = spawnSync("python3", [scriptPath, fixturePath, tmpDir], {
    cwd: repoRoot,
    encoding: "utf8",
  });

  assert.ok(run.status === 0 || run.status === 1, run.stderr || run.stdout);
  const result = readJson(path.join(tmpDir, resultName));
  const trace = readJson(path.join(tmpDir, traceName));
  const errors = readJson(path.join(tmpDir, errorsName));

  assert.equal(result.kind, "TixlMeshDrawTextureSamplerBindingProof");
  assert.deepEqual(trace.slice(0, 5).map((entry) => entry.op), [
    "loadTixlMeshDrawTextureSamplerBindingFixture",
    "resolveInputArtifacts",
    "readInputArtifacts",
    "validateInputArtifacts",
    "validateFixtureExpectations",
  ]);
  assertPathClean(result, trace, errors);

  if (run.status === 1) {
    assert.equal(result.ok, false);
    assert.equal(result.status, "blocked_metal_device_unavailable");
    assert.equal(result.claims.actualMetalTextureSamplerProbeRan, false);
    assert.equal(fs.existsSync(path.join(tmpDir, "generated_texture_sampler_probe.metal")), false);
    assert.ok(errors.some((error) => /Metal device unavailable/i.test(error.message || "")));
    return;
  }

  assert.deepEqual(errors, []);
  assert.equal(result.ok, true);
  assert.equal(result.status, "proven_tixl_mesh_draw_texture_sampler_binding");
  assert.deepEqual(result.claims, expectedClaims(true));
  assert.deepEqual(result.bindingLedger.boundNow, expectedBoundNow());
  assert.deepEqual(result.evidence.sentinelReadback.expectedWords, result.evidence.sentinelReadback.actualWords);
  assert.equal(result.evidence.sentinelReadback.actualWords[0], 0x11223344);
  assert.equal(result.evidence.sentinelReadback.actualWords[1], 0xa1b2c3d4);
  assert.equal(result.evidence.sentinelReadback.actualWords[2], 0x778899aa);
  assert.equal(result.evidence.sentinelReadback.actualWords[3], 0x55667788);
  assert.equal(fs.existsSync(path.join(tmpDir, "generated_texture_sampler_probe.metal")), true);
});

test("TiXL mesh draw texture sampler binding shell blocks missing t2/t7/s0/s1 before probe", () => {
  for (const missing of ["t2", "t7", "s0", "s1"]) {
    const tmpDir = fs.mkdtempSync(path.join(os.tmpdir(), `tixl-mesh-draw-missing-${missing}-`));
    const source = readJson(path.join(
      repoRoot,
      "docs/runtime/artifacts/tixl_mesh_draw_shader_source_audit/tixl_mesh_draw_shader_source_audit_result.json",
    ));
    if (missing.startsWith("t")) {
      source.resources = source.resources.filter((resource) => resource.register !== missing);
    } else {
      source.samplers = source.samplers.filter((sampler) => sampler.register !== missing);
    }
    const sourcePath = path.join(tmpDir, "missing-source-slot.json");
    fs.writeFileSync(sourcePath, JSON.stringify(source, null, 2));
    const fixture = readJson(fixturePath);
    fixture.sourceAuditArtifact = sourcePath;
    const brokenFixturePath = path.join(tmpDir, "missing-source-slot.graph.json");
    fs.writeFileSync(brokenFixturePath, JSON.stringify(fixture, null, 2));

    const run = spawnSync("python3", [scriptPath, brokenFixturePath, tmpDir], {
      cwd: repoRoot,
      encoding: "utf8",
    });

    assert.equal(run.status, 1, missing);
    const result = readJson(path.join(tmpDir, resultName));
    const errors = readJson(path.join(tmpDir, errorsName));
    assert.equal(result.status, "blocked_invalid_input_artifact");
    assert.equal(result.claims.actualMetalTextureSamplerProbeRan, false);
    assert.equal(fs.existsSync(path.join(tmpDir, "generated_texture_sampler_probe.metal")), false);
    assert.ok(errors.some((error) => error.code === "tixl_mesh_draw_texture_sampler_binding.invalid_source_audit_artifact"));
    assertPathClean(result, errors);
  }
});

test("TiXL mesh draw texture sampler binding shell blocks widened prior resource binding claims", () => {
  const tmpDir = fs.mkdtempSync(path.join(os.tmpdir(), "tixl-mesh-draw-widened-prior-"));
  const prior = readJson(path.join(
    repoRoot,
    "docs/runtime/artifacts/tixl_mesh_draw_resource_binding/tixl_mesh_draw_resource_binding_result.json",
  ));
  prior.claims.fullPbrResourceBinding = true;
  prior.claims.backendReplacementReady = true;
  prior.claims.boundedTextureSamplerMappingProven = true;
  const priorPath = path.join(tmpDir, "widened-prior.json");
  fs.writeFileSync(priorPath, JSON.stringify(prior, null, 2));
  const fixture = readJson(fixturePath);
  fixture.priorResourceBindingArtifact = priorPath;
  const brokenFixturePath = path.join(tmpDir, "widened-prior.graph.json");
  fs.writeFileSync(brokenFixturePath, JSON.stringify(fixture, null, 2));

  const run = spawnSync("python3", [scriptPath, brokenFixturePath, tmpDir], {
    cwd: repoRoot,
    encoding: "utf8",
  });

  assert.equal(run.status, 1);
  const result = readJson(path.join(tmpDir, resultName));
  const errors = readJson(path.join(tmpDir, errorsName));
  const fields = errors.flatMap((error) => error.mismatches || []).map((mismatch) => mismatch.field);
  assert.equal(result.status, "blocked_invalid_input_artifact");
  assert.ok(fields.includes("claims.fullPbrResourceBinding"));
  assert.ok(fields.includes("claims.backendReplacementReady"));
  assert.ok(fields.includes("claims.boundedTextureSamplerMappingProven"));
  assert.equal(result.claims.actualMetalTextureSamplerProbeRan, false);
  assertPathClean(result, errors);
});

test("TiXL mesh draw texture sampler binding shell blocks widened fixture expected claims", () => {
  const tmpDir = fs.mkdtempSync(path.join(os.tmpdir(), "tixl-mesh-draw-widened-fixture-"));
  const fixture = readJson(fixturePath);
  fixture.expected.claims.fullPbrResourceBinding = true;
  fixture.expected.claims.backendReplacementReady = true;
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
  assert.ok(fields.includes("expected.claims.backendReplacementReady"));
  assert.equal(result.claims.actualMetalTextureSamplerProbeRan, false);
  assertPathClean(result, errors);
});

test("TiXL mesh draw texture sampler binding checked-in artifacts are path-clean and fresh when Metal succeeds", () => {
  const result = readJson(path.join(artifactDir, resultName));
  const trace = readJson(path.join(artifactDir, traceName));
  const errors = readJson(path.join(artifactDir, errorsName));

  assert.equal(result.kind, "TixlMeshDrawTextureSamplerBindingProof");
  assert.ok(result.status === "proven_tixl_mesh_draw_texture_sampler_binding" || result.status === "blocked_metal_device_unavailable");
  assertPathClean(result, trace, errors);
  assert.ok(!JSON.stringify({ result, trace, errors }).includes("/Users/"));

  if (result.status !== "proven_tixl_mesh_draw_texture_sampler_binding") {
    assert.equal(result.ok, false);
    assert.equal(result.claims.actualMetalTextureSamplerProbeRan, false);
    assert.equal(fs.existsSync(path.join(artifactDir, "generated_texture_sampler_probe.metal")), false);
    return;
  }

  assert.equal(result.ok, true);
  assert.deepEqual(errors, []);
  assert.deepEqual(result.claims, expectedClaims(true));
  assert.deepEqual(result.bindingLedger.boundNow, expectedBoundNow());

  const tmpDir = fs.mkdtempSync(path.join(os.tmpdir(), "tixl-mesh-draw-texture-sampler-fresh-"));
  const run = spawnSync("python3", [scriptPath, fixturePath, tmpDir], {
    cwd: repoRoot,
    encoding: "utf8",
  });
  assert.equal(run.status, 0, run.stderr || run.stdout);
  assert.deepEqual(readJson(path.join(tmpDir, resultName)), result);
  assert.deepEqual(readJson(path.join(tmpDir, traceName)), trace);
  assert.deepEqual(readJson(path.join(tmpDir, errorsName)), errors);
  assert.equal(
    fs.readFileSync(path.join(tmpDir, "generated_texture_sampler_probe.metal"), "utf8"),
    fs.readFileSync(path.join(artifactDir, "generated_texture_sampler_probe.metal"), "utf8"),
  );
});

function expectedClaims(actualProbeRan) {
  return {
    sourceAuditArtifactConsumed: actualProbeRan,
    priorResourceBindingArtifactConsumed: actualProbeRan,
    actualMetalTextureSamplerProbeRan: actualProbeRan,
    t2BaseColorMapBindingProven: actualProbeRan,
    t7BrdfLookupBindingProven: actualProbeRan,
    s0WrappedSamplerBindingProven: actualProbeRan,
    s1ClampedSamplerBindingProven: actualProbeRan,
    boundedTextureSamplerMappingProven: actualProbeRan,
    fullPbrResourceBinding: false,
    t8ShadergraphResourcesExpanded: false,
    backendReplacementReady: false,
    hlslToMslTranslation: false,
    tixlRuntimeParity: false,
    pbrVisualCorrectness: false,
    rendererIntegrationComplete: false,
    constantBufferAdapterComplete: false,
  };
}

function expectedBoundNow() {
  return [
    {
      sourceRegister: "t2",
      sourceName: "BaseColorMap",
      sourceKind: "Texture2D<float4>",
      metalBinding: "texture(2)",
      sentinelWord: "0x11223344",
      observedIn: "actual Metal texture/sampler compute probe",
    },
    {
      sourceRegister: "t7",
      sourceName: "BRDFLookup",
      sourceKind: "Texture2D<float4>",
      metalBinding: "texture(7)",
      sentinelWord: "0xa1b2c3d4",
      observedIn: "actual Metal texture/sampler compute probe",
    },
    {
      sourceRegister: "s0",
      sourceName: "WrappedSampler",
      sourceKind: "sampler",
      metalBinding: "sampler(0)",
      sentinelWord: "0x778899aa",
      observedIn: "actual Metal texture/sampler compute probe",
    },
    {
      sourceRegister: "s1",
      sourceName: "ClampedSampler",
      sourceKind: "sampler",
      metalBinding: "sampler(1)",
      sentinelWord: "0x55667788",
      observedIn: "actual Metal texture/sampler compute probe",
    },
  ];
}

function assertPathClean(...values) {
  const text = JSON.stringify(values);
  assert.ok(!text.includes("/Users/"));
  assert.ok(!text.includes(repoRoot));
}

function readJson(filePath) {
  return JSON.parse(fs.readFileSync(filePath, "utf8"));
}

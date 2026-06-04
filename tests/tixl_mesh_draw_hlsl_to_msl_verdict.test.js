const assert = require("node:assert/strict");
const { spawnSync } = require("node:child_process");
const fs = require("node:fs");
const os = require("node:os");
const path = require("node:path");
const test = require("node:test");

const repoRoot = path.resolve(__dirname, "..");
const contractPath = path.join(repoRoot, "docs/runtime/TIXL_MESH_DRAW_HLSL_TO_MSL_VERDICT.md");
const fixturePath = path.join(repoRoot, "docs/runtime/fixtures/tixl_mesh_draw_hlsl_to_msl_verdict.graph.json");
const scriptPath = path.join(repoRoot, "docs/runtime/scripts/tixl_mesh_draw_hlsl_to_msl_verdict_shell.py");
const artifactDir = path.join(repoRoot, "docs/runtime/artifacts/tixl_mesh_draw_hlsl_to_msl_verdict");
const resultName = "tixl_mesh_draw_hlsl_to_msl_verdict_result.json";
const traceName = "tixl_mesh_draw_hlsl_to_msl_verdict_trace.json";
const errorsName = "tixl_mesh_draw_hlsl_to_msl_verdict_errors.json";

test("TiXL mesh draw HLSL-to-MSL verdict docs reject mechanical parity and stay bounded", () => {
  const source = fs.readFileSync(contractPath, "utf8");

  assert.match(source, /TiXL Mesh Draw HLSL-to-MSL Verdict/);
  assert.match(source, /TixlMeshDrawHlslToMslTranslationVerdict answers:/);
  assert.match(source, /mechanical translation for mesh draw parity is rejected/);
  assert.match(source, /not an HLSL-to-MSL translator/);
  assert.match(source, /not full PBR resource binding/);
  assert.match(source, /not backend replacement/);
  assert.match(source, /b0-b5/);
  assert.match(source, /t0-t7/);
  assert.match(source, /s0-s1/);
  assert.match(source, /t8\+/);
  assert.match(source, /FLOAT_PARAMS/);
  assert.match(source, /GLOBALS/);
  assert.match(source, /FIELD_FUNCTIONS/);
  assert.match(source, /FIELD_CALL/);
  assert.match(source, /duplicate Params/);
  assert.match(source, /ddx\/ddy/);
  assert.match(source, /discard/);
  assert.match(source, /SV_Target0\/1/);
  assert.match(source, /SV_VertexID/);
  assert.match(source, /VPOS/);
  assert.match(source, /mul\(vector, matrix\)/);
  assert.match(source, /TextureCube SampleLevel\/GetDimensions/);
  assert.match(source, /missing PBR visual reference/);
});

test("TiXL mesh draw HLSL-to-MSL verdict fixture names all required blockers", () => {
  const graph = readJson(fixturePath);

  assert.equal(graph.graphId, "fixture.tixl_mesh_draw_hlsl_to_msl_verdict");
  assert.equal(graph.kind, "TixlMeshDrawHlslToMslTranslationVerdict");
  assert.equal(
    graph.sourceAuditArtifact,
    "docs/runtime/artifacts/tixl_mesh_draw_shader_source_audit/tixl_mesh_draw_shader_source_audit_result.json",
  );
  assert.equal(
    graph.resourceBindingArtifact,
    "docs/runtime/artifacts/tixl_mesh_draw_resource_binding/tixl_mesh_draw_resource_binding_result.json",
  );
  assert.deepEqual(graph.expected.claims, expectedClaims());
  assert.deepEqual(graph.expected.requiredBlockerCodes, expectedBlockerCodes());
});

test("TiXL mesh draw HLSL-to-MSL verdict shell emits rejected verdict with structured facts", () => {
  const tmpDir = fs.mkdtempSync(path.join(os.tmpdir(), "tixl-mesh-draw-hlsl-msl-verdict-"));
  const run = spawnSync("python3", [scriptPath, fixturePath, tmpDir], {
    cwd: repoRoot,
    encoding: "utf8",
  });

  assert.equal(run.status, 0, run.stderr || run.stdout);
  const result = readJson(path.join(tmpDir, resultName));
  const trace = readJson(path.join(tmpDir, traceName));
  const errors = readJson(path.join(tmpDir, errorsName));

  assert.deepEqual(errors, []);
  assert.equal(result.kind, "TixlMeshDrawHlslToMslTranslationVerdict");
  assert.equal(result.ok, true);
  assert.equal(result.status, "rejected_for_mesh_draw_parity");
  assert.equal(result.mechanicalTranslationStatus, "rejected_for_mesh_draw_parity");
  assert.equal(result.verdict, "reject_mechanical_hlsl_to_msl_for_mesh_draw_parity");
  assert.deepEqual(result.claims, expectedClaims());
  assert.deepEqual(result.blockerFacts.map((fact) => fact.code), expectedBlockerCodes());
  assert.equal(result.evidence.sourceAudit.status, "audited_tixl_mesh_draw_source");
  assert.equal(result.evidence.resourceBinding.status, "summarized_tixl_mesh_draw_resource_binding");
  assert.equal(result.evidence.resourceBinding.fullPbrResourceBinding, false);
  assert.equal(result.evidence.resourceBinding.hlslToMslTranslation, false);
  assert.equal(result.evidence.resourceBinding.backendReplacementReady, false);
  assert.deepEqual(trace.map((entry) => entry.op), [
    "loadTixlMeshDrawHlslToMslVerdictFixture",
    "resolveInputArtifacts",
    "readInputArtifacts",
    "validateInputArtifacts",
    "buildHlslToMslVerdict",
    "publishTixlMeshDrawHlslToMslVerdictArtifacts",
  ]);
  assertFactsCoverRequiredEvidence(result.blockerFacts);
  assertPathClean(result, trace, errors);
});

test("TiXL mesh draw HLSL-to-MSL verdict shell blocks source audit artifacts missing required fields", () => {
  const tmpDir = fs.mkdtempSync(path.join(os.tmpdir(), "tixl-mesh-draw-verdict-missing-source-field-"));
  const source = readJson(path.join(
    repoRoot,
    "docs/runtime/artifacts/tixl_mesh_draw_shader_source_audit/tixl_mesh_draw_shader_source_audit_result.json",
  ));
  delete source.templateHoles;
  const sourcePath = path.join(tmpDir, "source-missing-template-holes.json");
  fs.writeFileSync(sourcePath, JSON.stringify(source, null, 2));
  const fixture = readJson(fixturePath);
  fixture.sourceAuditArtifact = sourcePath;
  const brokenFixturePath = path.join(tmpDir, "missing-source-field.graph.json");
  fs.writeFileSync(brokenFixturePath, JSON.stringify(fixture, null, 2));

  const run = spawnSync("python3", [scriptPath, brokenFixturePath, tmpDir], {
    cwd: repoRoot,
    encoding: "utf8",
  });

  assert.equal(run.status, 1);
  const result = readJson(path.join(tmpDir, resultName));
  const errors = readJson(path.join(tmpDir, errorsName));
  assert.equal(result.ok, false);
  assert.equal(result.status, "blocked_invalid_input_artifact");
  assert.equal(errors[0].code, "tixl_mesh_draw_hlsl_to_msl_verdict.invalid_source_audit_artifact");
  assert.ok(errors[0].mismatches.some((mismatch) => mismatch.field === "templateHoles"));
  assert.equal(result.claims.hlslToMslTranslation, false);
  assertPathClean(result, errors);
});

test("TiXL mesh draw HLSL-to-MSL verdict shell blocks source audit artifacts missing semantic blockers", () => {
  const tmpDir = fs.mkdtempSync(path.join(os.tmpdir(), "tixl-mesh-draw-verdict-missing-semantic-"));
  const source = readJson(path.join(
    repoRoot,
    "docs/runtime/artifacts/tixl_mesh_draw_shader_source_audit/tixl_mesh_draw_shader_source_audit_result.json",
  ));
  source.semanticBlockers = source.semanticBlockers.filter(
    (blocker) => blocker.code !== "requires_pbr_visual_reference",
  );
  const sourcePath = path.join(tmpDir, "source-missing-pbr-visual-reference.json");
  fs.writeFileSync(sourcePath, JSON.stringify(source, null, 2));
  const fixture = readJson(fixturePath);
  fixture.sourceAuditArtifact = sourcePath;
  const brokenFixturePath = path.join(tmpDir, "missing-pbr-visual-reference.graph.json");
  fs.writeFileSync(brokenFixturePath, JSON.stringify(fixture, null, 2));

  const run = spawnSync("python3", [scriptPath, brokenFixturePath, tmpDir], {
    cwd: repoRoot,
    encoding: "utf8",
  });

  assert.equal(run.status, 1);
  const result = readJson(path.join(tmpDir, resultName));
  const errors = readJson(path.join(tmpDir, errorsName));
  assert.equal(result.ok, false);
  assert.equal(result.status, "blocked_invalid_input_artifact");
  assert.equal(errors[0].code, "tixl_mesh_draw_hlsl_to_msl_verdict.invalid_source_audit_artifact");
  assert.ok(errors[0].mismatches.some((mismatch) =>
    mismatch.field === "semanticBlockers" && mismatch.expected === "requires_pbr_visual_reference",
  ));
  assertPathClean(result, errors);
});

test("TiXL mesh draw HLSL-to-MSL verdict shell blocks source audit artifacts with extra resources", () => {
  const tmpDir = fs.mkdtempSync(path.join(os.tmpdir(), "tixl-mesh-draw-verdict-extra-resource-"));
  const source = readJson(path.join(
    repoRoot,
    "docs/runtime/artifacts/tixl_mesh_draw_shader_source_audit/tixl_mesh_draw_shader_source_audit_result.json",
  ));
  source.resources.push({
    kind: "Texture2D",
    elementType: "float4",
    name: "UnexpectedExtra",
    register: "t9",
  });
  const sourcePath = path.join(tmpDir, "source-extra-resource.json");
  fs.writeFileSync(sourcePath, JSON.stringify(source, null, 2));
  const fixture = readJson(fixturePath);
  fixture.sourceAuditArtifact = sourcePath;
  const brokenFixturePath = path.join(tmpDir, "extra-resource.graph.json");
  fs.writeFileSync(brokenFixturePath, JSON.stringify(fixture, null, 2));

  const run = spawnSync("python3", [scriptPath, brokenFixturePath, tmpDir], {
    cwd: repoRoot,
    encoding: "utf8",
  });

  assert.equal(run.status, 1);
  const result = readJson(path.join(tmpDir, resultName));
  const errors = readJson(path.join(tmpDir, errorsName));
  assert.equal(result.status, "blocked_invalid_input_artifact");
  assert.ok(errors[0].mismatches.some((mismatch) => mismatch.field === "resources.extra"));
  assertPathClean(result, errors);
});

test("TiXL mesh draw HLSL-to-MSL verdict shell blocks widened resource binding claims", () => {
  const tmpDir = fs.mkdtempSync(path.join(os.tmpdir(), "tixl-mesh-draw-verdict-widened-binding-"));
  const binding = readJson(path.join(
    repoRoot,
    "docs/runtime/artifacts/tixl_mesh_draw_resource_binding/tixl_mesh_draw_resource_binding_result.json",
  ));
  binding.claims.fullPbrResourceBinding = true;
  binding.claims.hlslToMslTranslation = true;
  binding.claims.backendReplacementReady = true;
  const bindingPath = path.join(tmpDir, "widened-binding.json");
  fs.writeFileSync(bindingPath, JSON.stringify(binding, null, 2));
  const fixture = readJson(fixturePath);
  fixture.resourceBindingArtifact = bindingPath;
  const brokenFixturePath = path.join(tmpDir, "widened-binding.graph.json");
  fs.writeFileSync(brokenFixturePath, JSON.stringify(fixture, null, 2));

  const run = spawnSync("python3", [scriptPath, brokenFixturePath, tmpDir], {
    cwd: repoRoot,
    encoding: "utf8",
  });

  assert.equal(run.status, 1);
  const result = readJson(path.join(tmpDir, resultName));
  const errors = readJson(path.join(tmpDir, errorsName));
  assert.equal(result.status, "blocked_invalid_input_artifact");
  assert.ok(errors.some((error) => error.code === "tixl_mesh_draw_hlsl_to_msl_verdict.invalid_resource_binding_artifact"));
  const mismatchFields = errors.flatMap((error) => error.mismatches || []).map((mismatch) => mismatch.field);
  assert.ok(mismatchFields.includes("claims.fullPbrResourceBinding"));
  assert.ok(mismatchFields.includes("claims.hlslToMslTranslation"));
  assert.ok(mismatchFields.includes("claims.backendReplacementReady"));
  assertPathClean(result, errors);
});

test("TiXL mesh draw HLSL-to-MSL verdict shell blocks stale resource binding provenance", () => {
  const tmpDir = fs.mkdtempSync(path.join(os.tmpdir(), "tixl-mesh-draw-verdict-stale-binding-"));
  const binding = readJson(path.join(
    repoRoot,
    "docs/runtime/artifacts/tixl_mesh_draw_resource_binding/tixl_mesh_draw_resource_binding_result.json",
  ));
  binding.inputArtifacts.mslApprox.status = "stale";
  binding.evidence.mslApproxBufferPackingObserved = false;
  const bindingPath = path.join(tmpDir, "stale-binding.json");
  fs.writeFileSync(bindingPath, JSON.stringify(binding, null, 2));
  const fixture = readJson(fixturePath);
  fixture.resourceBindingArtifact = bindingPath;
  const brokenFixturePath = path.join(tmpDir, "stale-binding.graph.json");
  fs.writeFileSync(brokenFixturePath, JSON.stringify(fixture, null, 2));

  const run = spawnSync("python3", [scriptPath, brokenFixturePath, tmpDir], {
    cwd: repoRoot,
    encoding: "utf8",
  });

  assert.equal(run.status, 1);
  const result = readJson(path.join(tmpDir, resultName));
  const errors = readJson(path.join(tmpDir, errorsName));
  assert.equal(result.status, "blocked_invalid_input_artifact");
  const mismatchFields = errors.flatMap((error) => error.mismatches || []).map((mismatch) => mismatch.field);
  assert.ok(mismatchFields.includes("inputArtifacts.mslApprox.status"));
  assert.ok(mismatchFields.includes("evidence.mslApproxBufferPackingObserved"));
  assertPathClean(result, errors);
});

test("TiXL mesh draw HLSL-to-MSL verdict shell blocks fixtures that omit a required blocker", () => {
  const tmpDir = fs.mkdtempSync(path.join(os.tmpdir(), "tixl-mesh-draw-verdict-missing-blocker-"));
  const fixture = readJson(fixturePath);
  fixture.expected.requiredBlockerCodes = fixture.expected.requiredBlockerCodes.filter(
    (code) => code !== "texturecube_samplelevel_getdimensions_requires_msl_texture_mapping",
  );
  const brokenFixturePath = path.join(tmpDir, "missing-blocker.graph.json");
  fs.writeFileSync(brokenFixturePath, JSON.stringify(fixture, null, 2));

  const run = spawnSync("python3", [scriptPath, brokenFixturePath, tmpDir], {
    cwd: repoRoot,
    encoding: "utf8",
  });

  assert.equal(run.status, 1);
  const result = readJson(path.join(tmpDir, resultName));
  const errors = readJson(path.join(tmpDir, errorsName));
  assert.equal(result.status, "blocked_invalid_fixture");
  assert.equal(errors[0].code, "tixl_mesh_draw_hlsl_to_msl_verdict.invalid_fixture_expected_blockers");
  assert.ok(errors[0].mismatches.some((mismatch) => mismatch.field === "expected.requiredBlockerCodes"));
  assertPathClean(result, errors);
});

test("TiXL mesh draw HLSL-to-MSL verdict checked-in artifacts are path-clean and conservative", () => {
  const result = readJson(path.join(artifactDir, resultName));
  const trace = readJson(path.join(artifactDir, traceName));
  const errors = readJson(path.join(artifactDir, errorsName));
  const combined = JSON.stringify([result, trace, errors]);

  assert.deepEqual(errors, []);
  assert.equal(result.kind, "TixlMeshDrawHlslToMslTranslationVerdict");
  assert.equal(result.ok, true);
  assert.equal(result.status, "rejected_for_mesh_draw_parity");
  assert.equal(result.claims.hlslToMslTranslation, false);
  assert.equal(result.claims.fullPbrResourceBinding, false);
  assert.equal(result.claims.backendReplacementReady, false);
  assert.deepEqual(result.blockerFacts.map((fact) => fact.code), expectedBlockerCodes());
  assertFactsCoverRequiredEvidence(result.blockerFacts);
  assertPathClean(result, trace, errors);
  assert.ok(!combined.includes("/Users/"));
  assert.ok(!combined.includes("#include"));
  assert.ok(!combined.includes("struct psInput"));
  assert.ok(!combined.includes("float4x4 CameraToClipSpace"));
});

function expectedClaims() {
  return {
    translationRiskClassified: true,
    mechanicalTranslationForMeshDrawParity: false,
    hlslToMslTranslation: false,
    fullPbrResourceBinding: false,
    tixlRuntimeParity: false,
    pbrVisualCorrectness: false,
    backendReplacementReady: false,
  };
}

function expectedBlockerCodes() {
  return [
    "cbuffer_register_set_requires_explicit_layout_policy",
    "texture_and_cube_register_set_requires_resource_mapping",
    "sampler_register_set_requires_sampler_mapping",
    "template_resources_t8_plus_require_tixl_expansion",
    "template_holes_require_tixl_shadergraph_expansion",
    "duplicate_params_cbuffer_requires_disambiguation",
    "global_frag_state_requires_rewrite",
    "derivatives_require_fragment_stage_mapping",
    "discard_requires_fragment_control_flow_mapping",
    "mrt_sv_target_outputs_require_render_target_contract",
    "system_semantics_require_stage_attribute_mapping",
    "d3d_mul_order_requires_matrix_convention_proof",
    "texturecube_samplelevel_getdimensions_requires_msl_texture_mapping",
    "pbr_visual_reference_missing",
    "resource_binding_proof_is_partial",
  ];
}

function assertFactsCoverRequiredEvidence(facts) {
  const byCode = new Map(facts.map((fact) => [fact.code, fact]));
  assert.deepEqual(byCode.get("cbuffer_register_set_requires_explicit_layout_policy").registers, ["b0", "b1", "b2", "b3", "b4", "b5"]);
  assert.deepEqual(byCode.get("texture_and_cube_register_set_requires_resource_mapping").registers, ["t0", "t1", "t2", "t3", "t4", "t5", "t6", "t7"]);
  assert.deepEqual(byCode.get("sampler_register_set_requires_sampler_mapping").registers, ["s0", "s1"]);
  assert.deepEqual(byCode.get("template_holes_require_tixl_shadergraph_expansion").holes, [
    "FLOAT_PARAMS",
    "GLOBALS",
    "FIELD_FUNCTIONS",
    "FIELD_CALL",
  ]);
  assert.deepEqual(byCode.get("duplicate_params_cbuffer_requires_disambiguation").registers, ["b1", "b5"]);
  assert.deepEqual(byCode.get("derivatives_require_fragment_stage_mapping").symbols, ["ddx", "ddy"]);
  assert.deepEqual(byCode.get("mrt_sv_target_outputs_require_render_target_contract").semantics, ["SV_Target0", "SV_Target1"]);
  assert.deepEqual(byCode.get("system_semantics_require_stage_attribute_mapping").semantics, ["SV_VertexID", "VPOS", "SV_POSITION"]);
  assert.deepEqual(byCode.get("texturecube_samplelevel_getdimensions_requires_msl_texture_mapping").symbols, [
    "TextureCube",
    "SampleLevel",
    "GetDimensions",
  ]);
  assert.equal(byCode.get("pbr_visual_reference_missing").sourceBlocker, "requires_pbr_visual_reference");
}

function assertPathClean(...values) {
  const text = JSON.stringify(values);
  assert.ok(!text.includes("/Users/"));
  assert.ok(!text.includes(repoRoot));
}

function readJson(filePath) {
  return JSON.parse(fs.readFileSync(filePath, "utf8"));
}

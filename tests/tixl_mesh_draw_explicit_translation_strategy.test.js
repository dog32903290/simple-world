const assert = require("node:assert/strict");
const { spawnSync } = require("node:child_process");
const fs = require("node:fs");
const os = require("node:os");
const path = require("node:path");
const test = require("node:test");

const repoRoot = path.resolve(__dirname, "..");
const contractPath = path.join(repoRoot, "docs/runtime/TIXL_MESH_DRAW_EXPLICIT_TRANSLATION_STRATEGY.md");
const fixturePath = path.join(repoRoot, "docs/runtime/fixtures/tixl_mesh_draw_explicit_translation_strategy.graph.json");
const scriptPath = path.join(repoRoot, "docs/runtime/scripts/tixl_mesh_draw_explicit_translation_strategy_shell.py");
const artifactDir = path.join(repoRoot, "docs/runtime/artifacts/tixl_mesh_draw_explicit_translation_strategy");
const resultName = "tixl_mesh_draw_explicit_translation_strategy_result.json";
const traceName = "tixl_mesh_draw_explicit_translation_strategy_trace.json";
const errorsName = "tixl_mesh_draw_explicit_translation_strategy_errors.json";

test("TiXL mesh draw explicit translation strategy docs select handwritten adapter and stay bounded", () => {
  const source = fs.readFileSync(contractPath, "utf8");

  assert.match(source, /TiXL Mesh Draw Explicit Translation Strategy/);
  assert.match(source, /TixlMeshDrawExplicitTranslationStrategy answers:/);
  assert.match(source, /handwritten_explicit_msl_adapter/);
  assert.match(source, /mechanical HLSL-to-MSL.*rejected/);
  assert.match(source, /full cross-compiler.*not selected/);
  assert.match(source, /not a translator/);
  assert.match(source, /not full PBR resource binding/);
  assert.match(source, /not backend replacement/);
  for (const gate of expectedAdapterGates()) {
    assert.match(source, new RegExp(escapeRegExp(gate)));
  }
});

test("TiXL mesh draw explicit translation strategy fixture pins conservative expected claims", () => {
  const graph = readJson(fixturePath);

  assert.equal(graph.graphId, "fixture.tixl_mesh_draw_explicit_translation_strategy");
  assert.equal(graph.kind, "TixlMeshDrawExplicitTranslationStrategy");
  assert.equal(
    graph.sourceAuditArtifact,
    "docs/runtime/artifacts/tixl_mesh_draw_shader_source_audit/tixl_mesh_draw_shader_source_audit_result.json",
  );
  assert.equal(
    graph.verdictArtifact,
    "docs/runtime/artifacts/tixl_mesh_draw_hlsl_to_msl_verdict/tixl_mesh_draw_hlsl_to_msl_verdict_result.json",
  );
  assert.equal(
    graph.resourceBindingArtifact,
    "docs/runtime/artifacts/tixl_mesh_draw_resource_binding/tixl_mesh_draw_resource_binding_result.json",
  );
  assert.deepEqual(graph.expected.claims, expectedClaims());
  assert.deepEqual(graph.expected.nextAdapterGates, expectedAdapterGates());
  assert.deepEqual(graph.expected.requiredVerdictBlockerCodes, expectedVerdictBlockerCodes());
});

test("TiXL mesh draw explicit translation strategy shell emits selected handwritten adapter", () => {
  const tmpDir = fs.mkdtempSync(path.join(os.tmpdir(), "tixl-mesh-draw-explicit-strategy-"));
  const run = spawnSync("python3", [scriptPath, fixturePath, tmpDir], {
    cwd: repoRoot,
    encoding: "utf8",
  });

  assert.equal(run.status, 0, run.stderr || run.stdout);
  const result = readJson(path.join(tmpDir, resultName));
  const trace = readJson(path.join(tmpDir, traceName));
  const errors = readJson(path.join(tmpDir, errorsName));

  assert.deepEqual(errors, []);
  assert.equal(result.kind, "TixlMeshDrawExplicitTranslationStrategy");
  assert.equal(result.ok, true);
  assert.equal(result.status, "selected_handwritten_explicit_msl_adapter");
  assert.equal(result.selectedStrategy, "handwritten_explicit_msl_adapter");
  assert.equal(result.rejectedStrategy, "mechanical_hlsl_to_msl_translation");
  assert.equal(result.unselectedLargerStrategy, "full_cross_compiler");
  assert.deepEqual(result.claims, expectedClaims());
  assert.deepEqual(result.nextAdapterGates, expectedAdapterGates());
  assert.deepEqual(result.acceptanceGates.map((gate) => gate.code), expectedAcceptanceGateCodes());
  assert.deepEqual(result.evidence.observedAdapter.boundRegisters, ["t0", "t1"]);
  assert.deepEqual(result.evidence.pendingResourceFamilies, ["b0-b5", "t2-t7", "s0-s1", "t8+"]);
  assert.deepEqual(result.evidence.verdict.requiredBlockerCodes, expectedVerdictBlockerCodes());
  assert.deepEqual(trace.map((entry) => entry.op), [
    "loadTixlMeshDrawExplicitTranslationStrategyFixture",
    "resolveInputArtifacts",
    "readInputArtifacts",
    "validateInputArtifacts",
    "selectExplicitTranslationStrategy",
    "publishTixlMeshDrawExplicitTranslationStrategyArtifacts",
  ]);
  assertPathClean(result, trace, errors);
});

test("TiXL mesh draw explicit translation strategy shell blocks verdicts that widen translation or backend claims", () => {
  const tmpDir = fs.mkdtempSync(path.join(os.tmpdir(), "tixl-mesh-draw-explicit-strategy-widened-"));
  const verdict = readJson(path.join(
    repoRoot,
    "docs/runtime/artifacts/tixl_mesh_draw_hlsl_to_msl_verdict/tixl_mesh_draw_hlsl_to_msl_verdict_result.json",
  ));
  verdict.claims.hlslToMslTranslation = true;
  verdict.claims.backendReplacementReady = true;
  const verdictPath = path.join(tmpDir, "widened-verdict.json");
  fs.writeFileSync(verdictPath, JSON.stringify(verdict, null, 2));
  const fixture = readJson(fixturePath);
  fixture.verdictArtifact = verdictPath;
  const brokenFixturePath = path.join(tmpDir, "widened-verdict.graph.json");
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
  assert.equal(errors[0].code, "tixl_mesh_draw_explicit_translation_strategy.invalid_verdict_artifact");
  const mismatchFields = errors[0].mismatches.map((mismatch) => mismatch.field);
  assert.ok(mismatchFields.includes("claims.hlslToMslTranslation"));
  assert.ok(mismatchFields.includes("claims.backendReplacementReady"));
  assert.equal(result.claims.hlslToMslTranslation, false);
  assert.equal(result.claims.backendReplacementReady, false);
  assertPathClean(result, errors);
});

test("TiXL mesh draw explicit translation strategy shell blocks source audit artifacts with extra resources", () => {
  const tmpDir = fs.mkdtempSync(path.join(os.tmpdir(), "tixl-mesh-draw-explicit-strategy-extra-source-"));
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
  const brokenFixturePath = path.join(tmpDir, "extra-source.graph.json");
  fs.writeFileSync(brokenFixturePath, JSON.stringify(fixture, null, 2));

  const run = spawnSync("python3", [scriptPath, brokenFixturePath, tmpDir], {
    cwd: repoRoot,
    encoding: "utf8",
  });

  assert.equal(run.status, 1);
  const result = readJson(path.join(tmpDir, resultName));
  const errors = readJson(path.join(tmpDir, errorsName));
  assert.equal(result.status, "blocked_invalid_input_artifact");
  assert.equal(errors[0].code, "tixl_mesh_draw_explicit_translation_strategy.invalid_source_audit_artifact");
  assert.ok(errors[0].mismatches.some((mismatch) => mismatch.field === "resources.extra"));
  assertPathClean(result, errors);
});

test("TiXL mesh draw explicit translation strategy shell blocks verdicts missing blocker facts", () => {
  const tmpDir = fs.mkdtempSync(path.join(os.tmpdir(), "tixl-mesh-draw-explicit-strategy-missing-blocker-"));
  const verdict = readJson(path.join(
    repoRoot,
    "docs/runtime/artifacts/tixl_mesh_draw_hlsl_to_msl_verdict/tixl_mesh_draw_hlsl_to_msl_verdict_result.json",
  ));
  verdict.blockerFacts = verdict.blockerFacts.filter(
    (fact) => fact.code !== "texturecube_samplelevel_getdimensions_requires_msl_texture_mapping",
  );
  const verdictPath = path.join(tmpDir, "missing-blocker-verdict.json");
  fs.writeFileSync(verdictPath, JSON.stringify(verdict, null, 2));
  const fixture = readJson(fixturePath);
  fixture.verdictArtifact = verdictPath;
  const brokenFixturePath = path.join(tmpDir, "missing-blocker.graph.json");
  fs.writeFileSync(brokenFixturePath, JSON.stringify(fixture, null, 2));

  const run = spawnSync("python3", [scriptPath, brokenFixturePath, tmpDir], {
    cwd: repoRoot,
    encoding: "utf8",
  });

  assert.equal(run.status, 1);
  const result = readJson(path.join(tmpDir, resultName));
  const errors = readJson(path.join(tmpDir, errorsName));
  assert.equal(result.status, "blocked_invalid_input_artifact");
  assert.equal(errors[0].code, "tixl_mesh_draw_explicit_translation_strategy.invalid_verdict_artifact");
  assert.ok(errors[0].mismatches.some((mismatch) => mismatch.field === "blockerFacts"));
  assertPathClean(result, errors);
});

test("TiXL mesh draw explicit translation strategy shell blocks stale resource binding provenance", () => {
  const tmpDir = fs.mkdtempSync(path.join(os.tmpdir(), "tixl-mesh-draw-explicit-strategy-stale-binding-"));
  const binding = readJson(path.join(
    repoRoot,
    "docs/runtime/artifacts/tixl_mesh_draw_resource_binding/tixl_mesh_draw_resource_binding_result.json",
  ));
  binding.inputArtifacts.bufferLayout.status = "stale";
  binding.evidence.frameDigest = binding.evidence.controlFrameDigest;
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
  assert.equal(errors[0].code, "tixl_mesh_draw_explicit_translation_strategy.invalid_resource_binding_artifact");
  const mismatchFields = errors.flatMap((error) => error.mismatches || []).map((mismatch) => mismatch.field);
  assert.ok(mismatchFields.includes("inputArtifacts.bufferLayout.status"));
  assert.ok(mismatchFields.includes("evidence.frameDigest"));
  assertPathClean(result, errors);
});

test("TiXL mesh draw explicit translation strategy shell blocks resource binding without t0/t1-only adapter evidence", () => {
  const tmpDir = fs.mkdtempSync(path.join(os.tmpdir(), "tixl-mesh-draw-explicit-strategy-resource-binding-"));
  const binding = readJson(path.join(
    repoRoot,
    "docs/runtime/artifacts/tixl_mesh_draw_resource_binding/tixl_mesh_draw_resource_binding_result.json",
  ));
  binding.bindingLedger.boundNow.push({
    sourceRegister: "t2",
    sourceName: "BaseColorMap",
    sourceKind: "Texture2D<float4>",
    metalBinding: "texture(0)",
  });
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
  assert.equal(errors[0].code, "tixl_mesh_draw_explicit_translation_strategy.invalid_resource_binding_artifact");
  assert.ok(errors[0].mismatches.some((mismatch) => mismatch.field === "bindingLedger.boundNow"));
  assertPathClean(result, errors);
});

test("TiXL mesh draw explicit translation strategy checked-in artifacts are conservative and path-clean", () => {
  const result = readJson(path.join(artifactDir, resultName));
  const trace = readJson(path.join(artifactDir, traceName));
  const errors = readJson(path.join(artifactDir, errorsName));
  const combined = JSON.stringify([result, trace, errors]);

  assert.deepEqual(errors, []);
  assert.equal(result.kind, "TixlMeshDrawExplicitTranslationStrategy");
  assert.equal(result.ok, true);
  assert.equal(result.status, "selected_handwritten_explicit_msl_adapter");
  assert.equal(result.claims.mechanicalTranslationRejected, true);
  assert.equal(result.claims.selectedStrategy, "handwritten_explicit_msl_adapter");
  assert.equal(result.claims.fullCrossCompilerSelected, false);
  assert.equal(result.claims.explicitAdapterReadyForFullPbr, false);
  assert.equal(result.claims.fullPbrResourceBinding, false);
  assert.equal(result.claims.backendReplacementReady, false);
  assert.equal(result.claims.tixlRuntimeParity, false);
  assert.deepEqual(result.nextAdapterGates, expectedAdapterGates());
  assertPathClean(result, trace, errors);
  assert.ok(!combined.includes("/Users/"));
  assert.ok(!combined.includes("#include"));
  assert.ok(!combined.includes("struct psInput"));
  assert.ok(!combined.includes("float4x4 CameraToClipSpace"));
});

function expectedClaims() {
  return {
    mechanicalTranslationRejected: true,
    selectedStrategy: "handwritten_explicit_msl_adapter",
    fullCrossCompilerSelected: false,
    explicitAdapterReadyForFullPbr: false,
    fullPbrResourceBinding: false,
    backendReplacementReady: false,
    tixlRuntimeParity: false,
    hlslToMslTranslation: false,
    pbrVisualCorrectness: false,
  };
}

function expectedAdapterGates() {
  return [
    "constant buffer layout b0-b5",
    "texture/sampler mapping t2-t7/s0-s1",
    "t8+ shadergraph expansion",
    "MRT contract",
    "stage semantics",
    "matrix convention",
    "cube mip query",
    "PBR visual reference",
  ];
}

function expectedAcceptanceGateCodes() {
  return [
    "valid_source_audit_artifact",
    "valid_rejected_hlsl_to_msl_verdict_artifact",
    "mechanical_translation_rejected",
    "observed_adapter_limited_to_t0_t1",
    "pending_full_pbr_resource_binding_acknowledged",
    "selected_handwritten_explicit_msl_adapter",
    "full_cross_compiler_not_selected",
  ];
}

function expectedVerdictBlockerCodes() {
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

function assertPathClean(...values) {
  const text = JSON.stringify(values);
  assert.ok(!text.includes("/Users/"));
  assert.ok(!text.includes(repoRoot));
}

function escapeRegExp(value) {
  return value.replace(/[.*+?^${}()|[\]\\]/g, "\\$&");
}

function readJson(filePath) {
  return JSON.parse(fs.readFileSync(filePath, "utf8"));
}

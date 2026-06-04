const assert = require("node:assert/strict");
const { spawnSync } = require("node:child_process");
const fs = require("node:fs");
const os = require("node:os");
const path = require("node:path");
const test = require("node:test");

const repoRoot = path.resolve(__dirname, "..");
const contractPath = path.join(repoRoot, "docs/runtime/TIXL_MESH_DRAW_CONSTANT_BUFFER_LAYOUT_PROOF.md");
const fixturePath = path.join(repoRoot, "docs/runtime/fixtures/tixl_mesh_draw_constant_buffer_layout.graph.json");
const scriptPath = path.join(repoRoot, "docs/runtime/scripts/tixl_mesh_draw_constant_buffer_layout_shell.py");
const artifactDir = path.join(repoRoot, "docs/runtime/artifacts/tixl_mesh_draw_constant_buffer_layout");
const resultName = "tixl_mesh_draw_constant_buffer_layout_result.json";
const traceName = "tixl_mesh_draw_constant_buffer_layout_trace.json";
const errorsName = "tixl_mesh_draw_constant_buffer_layout_errors.json";

test("TiXL mesh draw constant buffer layout docs define a bounded b0-b5 proof lane", () => {
  const source = fs.readFileSync(contractPath, "utf8");

  assert.match(source, /TiXL Mesh Draw Constant Buffer Layout Proof/);
  assert.match(source, /TixlMeshDrawConstantBufferLayoutProof answers:/);
  assert.match(source, /handwritten_explicit_msl_adapter/);
  assert.match(source, /b0-b5/);
  assert.match(source, /not a shader implementation/);
  assert.match(source, /not texture\/sampler mapping/);
  assert.match(source, /not full PBR resource binding/);
  assert.match(source, /not backend replacement/);
  assert.match(source, /native packing proof/);
  assert.match(source, /b5 `Params`/);
  assert.match(source, /disambiguated from b1 `Params`/);
  for (const buffer of expectedConstantBuffers()) {
    assert.match(source, new RegExp(`${buffer.register} ${escapeRegExp(buffer.name)}`));
    for (const field of buffer.fields) {
      assert.match(source, new RegExp(`${escapeRegExp(field.name)}\\s+${escapeRegExp(field.type)}`));
    }
  }
});

test("TiXL mesh draw constant buffer layout fixture pins conservative expectations", () => {
  const graph = readJson(fixturePath);

  assert.equal(graph.graphId, "fixture.tixl_mesh_draw_constant_buffer_layout");
  assert.equal(graph.kind, "TixlMeshDrawConstantBufferLayoutProof");
  assert.equal(
    graph.sourceAuditArtifact,
    "docs/runtime/artifacts/tixl_mesh_draw_shader_source_audit/tixl_mesh_draw_shader_source_audit_result.json",
  );
  assert.equal(
    graph.strategyArtifact,
    "docs/runtime/artifacts/tixl_mesh_draw_explicit_translation_strategy/tixl_mesh_draw_explicit_translation_strategy_result.json",
  );
  assert.deepEqual(graph.expected.claims, expectedClaims());
  assert.deepEqual(graph.expected.constantBuffers, expectedConstantBuffers());
  assert.deepEqual(graph.expected.acceptanceGateCodes, expectedAcceptanceGateCodes());
});

test("TiXL mesh draw constant buffer layout shell emits b0-b5 layout and bounded policy", () => {
  const tmpDir = fs.mkdtempSync(path.join(os.tmpdir(), "tixl-mesh-draw-cbuffer-layout-"));
  const run = spawnSync("python3", [scriptPath, fixturePath, tmpDir], {
    cwd: repoRoot,
    encoding: "utf8",
  });

  assert.equal(run.status, 0, run.stderr || run.stdout);
  const result = readJson(path.join(tmpDir, resultName));
  const trace = readJson(path.join(tmpDir, traceName));
  const errors = readJson(path.join(tmpDir, errorsName));

  assert.deepEqual(errors, []);
  assert.equal(result.kind, "TixlMeshDrawConstantBufferLayoutProof");
  assert.equal(result.ok, true);
  assert.equal(result.status, "classified_tixl_mesh_draw_constant_buffer_layout");
  assert.equal(result.selectedStrategy, "handwritten_explicit_msl_adapter");
  assert.deepEqual(result.claims, expectedClaims());
  assert.deepEqual(result.acceptanceGates.map((gate) => gate.code), expectedAcceptanceGateCodes());
  assert.deepEqual(compactConstantBuffers(result.constantBuffers), expectedConstantBuffers());
  assert.deepEqual(result.constantBufferRegisters, ["b0", "b1", "b2", "b3", "b4", "b5"]);
  assert.equal(result.duplicateNamePolicy.name, "Params");
  assert.equal(result.duplicateNamePolicy.b1.semanticRole, "mesh_draw_material_params");
  assert.equal(result.duplicateNamePolicy.b5.semanticRole, "shadergraph_duplicate_params");
  assert.equal(result.duplicateNamePolicy.b5.disambiguatedFrom, "b1:Params");
  assert.equal(result.duplicateNamePolicy.b5.fieldsKnownFromSourceAudit, false);
  assert.equal(result.bindingPolicy.readiness, "bounded_partial");
  assert.deepEqual(result.bindingPolicy.reservedMetalBufferRange, [2, 7]);
  assert.equal(result.bindingPolicy.nativePackingProofRequired, true);
  assert.equal(result.bindingPolicy.backendBindingImplemented, false);
  assert.equal(result.bindingPolicy.textureSamplerMappingIncluded, false);
  assert.ok(result.semanticBlockers.some((blocker) => blocker.code === "b0_b5_layout_needs_native_packing_proof"));
  assert.ok(result.semanticBlockers.some((blocker) => blocker.code === "texture_sampler_mapping_not_in_scope"));
  assert.ok(result.semanticBlockers.some((blocker) => blocker.code === "backend_replacement_not_ready"));
  assert.deepEqual(trace.map((entry) => entry.op), [
    "loadTixlMeshDrawConstantBufferLayoutFixture",
    "resolveInputArtifacts",
    "readInputArtifacts",
    "validateInputArtifacts",
    "classifyConstantBufferLayout",
    "publishTixlMeshDrawConstantBufferLayoutArtifacts",
  ]);
  assertPathClean(result, trace, errors);
});

test("TiXL mesh draw constant buffer layout shell fails closed when source audit loses b0-b5 facts", () => {
  const tmpDir = fs.mkdtempSync(path.join(os.tmpdir(), "tixl-mesh-draw-cbuffer-broken-source-"));
  const source = readJson(path.join(
    repoRoot,
    "docs/runtime/artifacts/tixl_mesh_draw_shader_source_audit/tixl_mesh_draw_shader_source_audit_result.json",
  ));
  source.constants = source.constants.filter((constant) => constant.name !== "ObjectToClipSpace");
  source.requiredBuffers = source.requiredBuffers.filter((buffer) => buffer.register !== "b4");
  const sourcePath = path.join(tmpDir, "source-missing-cbuffer.json");
  fs.writeFileSync(sourcePath, JSON.stringify(source, null, 2));
  const fixture = readJson(fixturePath);
  fixture.sourceAuditArtifact = sourcePath;
  const brokenFixturePath = path.join(tmpDir, "missing-cbuffer.graph.json");
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
  assert.equal(result.claims.constantBufferLayoutClassified, false);
  assert.equal(errors[0].code, "tixl_mesh_draw_constant_buffer_layout.invalid_source_audit_artifact");
  const mismatchFields = errors.flatMap((error) => error.mismatches || []).map((mismatch) => mismatch.field);
  assert.ok(mismatchFields.includes("constants.b0.ObjectToClipSpace"));
  assert.ok(mismatchFields.includes("requiredBuffers.b4"));
  assertPathClean(result, errors);
});

test("TiXL mesh draw constant buffer layout shell blocks strategy artifacts that do not select the handwritten adapter", () => {
  const tmpDir = fs.mkdtempSync(path.join(os.tmpdir(), "tixl-mesh-draw-cbuffer-broken-strategy-"));
  const strategy = readJson(path.join(
    repoRoot,
    "docs/runtime/artifacts/tixl_mesh_draw_explicit_translation_strategy/tixl_mesh_draw_explicit_translation_strategy_result.json",
  ));
  strategy.selectedStrategy = "full_cross_compiler";
  strategy.claims.selectedStrategy = "full_cross_compiler";
  strategy.claims.fullPbrResourceBinding = true;
  strategy.claims.backendReplacementReady = true;
  const strategyPath = path.join(tmpDir, "widened-strategy.json");
  fs.writeFileSync(strategyPath, JSON.stringify(strategy, null, 2));
  const fixture = readJson(fixturePath);
  fixture.strategyArtifact = strategyPath;
  const brokenFixturePath = path.join(tmpDir, "widened-strategy.graph.json");
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
  assert.equal(result.claims.selectedStrategyConsumed, false);
  assert.equal(errors[0].code, "tixl_mesh_draw_constant_buffer_layout.invalid_strategy_artifact");
  const mismatchFields = errors.flatMap((error) => error.mismatches || []).map((mismatch) => mismatch.field);
  assert.ok(mismatchFields.includes("selectedStrategy"));
  assert.ok(mismatchFields.includes("claims.fullPbrResourceBinding"));
  assert.ok(mismatchFields.includes("claims.backendReplacementReady"));
  assertPathClean(result, errors);
});

test("TiXL mesh draw constant buffer layout shell blocks widened fixture claims", () => {
  const tmpDir = fs.mkdtempSync(path.join(os.tmpdir(), "tixl-mesh-draw-cbuffer-widened-fixture-"));
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
  assert.equal(result.ok, false);
  assert.equal(result.status, "blocked_invalid_fixture");
  assert.equal(result.claims.fullPbrResourceBinding, false);
  assert.equal(result.claims.backendReplacementReady, false);
  assert.equal(errors[0].code, "tixl_mesh_draw_constant_buffer_layout.invalid_fixture_expectations");
  assertPathClean(result, errors);
});

test("TiXL mesh draw constant buffer layout checked-in artifacts are conservative and path-clean", () => {
  const result = readJson(path.join(artifactDir, resultName));
  const trace = readJson(path.join(artifactDir, traceName));
  const errors = readJson(path.join(artifactDir, errorsName));
  const combined = JSON.stringify([result, trace, errors]);

  assert.deepEqual(errors, []);
  assert.equal(result.kind, "TixlMeshDrawConstantBufferLayoutProof");
  assert.equal(result.ok, true);
  assert.equal(result.status, "classified_tixl_mesh_draw_constant_buffer_layout");
  assert.deepEqual(result.claims, expectedClaims());
  assert.deepEqual(compactConstantBuffers(result.constantBuffers), expectedConstantBuffers());
  assert.equal(result.bindingPolicy.readiness, "bounded_partial");
  assert.equal(result.bindingPolicy.backendBindingImplemented, false);
  assert.equal(result.bindingPolicy.nativePackingProofRequired, true);
  assert.equal(result.claims.textureSamplerMapping, false);
  assert.equal(result.claims.fullPbrResourceBinding, false);
  assert.equal(result.claims.backendReplacementReady, false);
  assertPathClean(result, trace, errors);
  assert.ok(!combined.includes("/Users/"));
  assert.ok(!combined.includes("#include"));
  assert.ok(!combined.includes("struct psInput"));
  assert.ok(!combined.includes("float4x4 CameraToClipSpace"));
  assert.ok(!combined.includes("Texture2D"));
});

function expectedClaims() {
  return {
    selectedStrategyConsumed: true,
    constantBufferLayoutClassified: true,
    constantBufferBindingPolicyReady: "bounded_partial",
    b0b5LayoutNeedsNativePackingProof: true,
    textureSamplerMapping: false,
    fullPbrResourceBinding: false,
    backendReplacementReady: false,
    hlslToMslTranslation: false,
    tixlRuntimeParity: false,
    pbrVisualCorrectness: false,
  };
}

function expectedAcceptanceGateCodes() {
  return [
    "valid_source_audit_artifact",
    "selected_handwritten_explicit_msl_adapter_consumed",
    "exact_b0_b5_layout_classified",
    "duplicate_params_disambiguated",
    "constant_buffer_policy_bounded_partial",
    "native_packing_proof_still_required",
    "no_texture_sampler_or_backend_claims",
  ];
}

function expectedConstantBuffers() {
  return [
    {
      register: "b0",
      name: "Transforms",
      semanticRole: "mesh_draw_transforms",
      fields: [
        { name: "CameraToClipSpace", type: "float4x4" },
        { name: "ClipSpaceToCamera", type: "float4x4" },
        { name: "WorldToCamera", type: "float4x4" },
        { name: "CameraToWorld", type: "float4x4" },
        { name: "WorldToClipSpace", type: "float4x4" },
        { name: "ClipSpaceToWorld", type: "float4x4" },
        { name: "ObjectToWorld", type: "float4x4" },
        { name: "WorldToObject", type: "float4x4" },
        { name: "ObjectToCamera", type: "float4x4" },
        { name: "ObjectToClipSpace", type: "float4x4" },
      ],
    },
    {
      register: "b1",
      name: "Params",
      semanticRole: "mesh_draw_material_params",
      fields: [
        { name: "Color", type: "float4" },
        { name: "AlphaCutOff", type: "float" },
        { name: "UseFlatShading", type: "float" },
        { name: "SpecularAA", type: "float" },
      ],
    },
    {
      register: "b2",
      name: "FogParams",
      semanticRole: "mesh_draw_fog_params",
      fields: [
        { name: "FogColor", type: "float4" },
        { name: "FogDistance", type: "float" },
        { name: "FogBias", type: "float" },
      ],
    },
    {
      register: "b3",
      name: "PointLights",
      semanticRole: "mesh_draw_point_lights",
      fields: [
        { name: "Lights", type: "PointLight", array: "[8]" },
        { name: "ActiveLightCount", type: "int" },
      ],
    },
    {
      register: "b4",
      name: "PbrParams",
      semanticRole: "mesh_draw_pbr_params",
      fields: [
        { name: "BaseColor", type: "float4" },
        { name: "EmissiveColor", type: "float4" },
        { name: "Roughness", type: "float" },
        { name: "Specular", type: "float" },
        { name: "Metal", type: "float" },
      ],
    },
    {
      register: "b5",
      name: "Params",
      semanticRole: "shadergraph_duplicate_params",
      fields: [],
    },
  ];
}

function compactConstantBuffers(buffers) {
  return buffers.map((buffer) => ({
    register: buffer.register,
    name: buffer.name,
    semanticRole: buffer.semanticRole,
    fields: buffer.fields.map((field) => {
      const compact = { name: field.name, type: field.type };
      if (field.array) compact.array = field.array;
      return compact;
    }),
  }));
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

const assert = require("node:assert/strict");
const { spawnSync } = require("node:child_process");
const fs = require("node:fs");
const os = require("node:os");
const path = require("node:path");
const test = require("node:test");

const repoRoot = path.resolve(__dirname, "..");
const contractPath = path.join(repoRoot, "docs/runtime/RUNTIME_CLOSURE_REPORT.md");
const fixturePath = path.join(repoRoot, "docs/runtime/fixtures/runtime_closure_report.graph.json");
const scriptPath = path.join(repoRoot, "docs/runtime/scripts/runtime_closure_report_shell.py");
const artifactDir = path.join(repoRoot, "docs/runtime/artifacts/runtime_closure_report");
const pipelineArtifactDir = path.join(repoRoot, "docs/runtime/artifacts/native_render_pipeline");
const shadergraphResourcesArtifactPath = path.join(repoRoot, "docs/runtime/artifacts/tixl_mesh_draw_shadergraph_resources_expansion/tixl_mesh_draw_shadergraph_resources_expansion_result.json");
const stageMrtMatrixArtifactPath = path.join(repoRoot, "docs/runtime/artifacts/tixl_mesh_draw_stage_mrt_matrix/tixl_mesh_draw_stage_mrt_matrix_result.json");
const textureCubePbrReferenceArtifactPath = path.join(repoRoot, "docs/runtime/artifacts/tixl_mesh_draw_texturecube_pbr_reference/tixl_mesh_draw_texturecube_pbr_reference_result.json");
const backendReplacementGateArtifactPath = path.join(repoRoot, "docs/runtime/artifacts/tixl_mesh_draw_backend_replacement_gate/tixl_mesh_draw_backend_replacement_gate_result.json");

test("RuntimeClosureReport docs describe a bounded closure ledger, not Metal parity completion", () => {
  const source = fs.readFileSync(contractPath, "utf8");

  assert.match(source, /Runtime Closure Report/);
  assert.match(source, /closure ledger/);
  assert.match(source, /lane-scoped closure/);
  assert.match(source, /current native_render_pipeline\/headless proof lane/);
  assert.match(source, /not repo-wide runtime completion/);
  assert.match(source, /not Metal\/native GPU parity completion/);
  assert.match(source, /not TiXL parity completion/);
  assert.match(source, /native HLSL\/Metal compile remains bounded/);
  assert.match(source, /MetalExplicitMslProof/);
  assert.match(source, /NativeDrawShaderCompileProof/);
  assert.match(source, /explicit MSL proof exists/);
  assert.match(source, /TiXL mesh draw MSL approximation proof now shows/);
  assert.match(source, /TiXL mesh draw resource binding proof records/);
  assert.match(source, /PbrVertices t0 -> buffer\(0\)/);
  assert.match(source, /TiXL mesh draw texture\/sampler binding proof now consumes/);
  assert.match(source, /t2 BaseColorMap -> texture\(2\)/);
  assert.match(source, /t7 BRDFLookup -> texture\(7\)/);
  assert.match(source, /s0 WrappedSampler -> sampler\(0\)/);
  assert.match(source, /s1 ClampedSampler -> sampler\(1\)/);
  assert.match(source, /does not prove t3-t6,\s+full PBR resource binding, or backend replacement/);
  assert.match(source, /TiXL mesh draw\s+ShaderGraph resources expansion proof now proves/);
  assert.match(source, /current SphereSDF fixture has zero t8\+ resources/);
  assert.match(source, /TiXL mesh draw stage\/MRT\/matrix proof now parses/);
  assert.match(source, /vsMain\(uint id : SV_VertexID\)/);
  assert.match(source, /SV_Target0/);
  assert.match(source, /SV_Target1/);
  assert.match(source, /mul\(vector, matrix\)/);
  assert.match(source, /runs a tiny explicit Metal adapter probe/);
  assert.match(source, /does not translate TiXL donor HLSL to MSL, prove\s+TextureCube behavior, prove full PBR, or replace the backend/);
  assert.match(source, /removing\s+`prove_stage_mrt_matrix_semantics_for_handwritten_mesh_draw_adapter`/);
  assert.match(source, /prove_texturecube_samplelevel_getdimensions_and_pbr_visual_reference/);
  assert.match(source, /TextureCube SampleLevel \/ GetDimensions proof now maps/);
  assert.match(source, /boundedPbrVisualReferenceEstablished/);
  assert.match(source, /removing\s+`prove_texturecube_samplelevel_getdimensions_and_pbr_visual_reference`/);
  assert.match(source, /does not discharge the TiXL donor HLSL\s+boundary/);
  assert.match(source, /Backend Replacement Gate proof now consumes/);
  assert.match(source, /backendReplacementGateEvaluated/);
  assert.match(source, /bounded_native_backend_replacement_guarded/);
  assert.match(source, /backendReplacementReady: false/);
  assert.match(source, /fullPbrResourceBinding: false/);
  assert.match(source, /docs\/runtime\/artifacts\/native_render_pipeline/);
});

test("RuntimeClosureReport fixture points at the native render pipeline proof artifacts", () => {
  const graph = JSON.parse(fs.readFileSync(fixturePath, "utf8"));

  assert.equal(graph.graphId, "fixture.runtime_closure_report");
  assert.equal(graph.nativeRenderPipelineArtifacts, "docs/runtime/artifacts/native_render_pipeline");
  assert.equal(graph.tixlMeshDrawShadergraphResourcesExpansionArtifact, "docs/runtime/artifacts/tixl_mesh_draw_shadergraph_resources_expansion/tixl_mesh_draw_shadergraph_resources_expansion_result.json");
  assert.equal(graph.tixlMeshDrawStageMrtMatrixArtifact, "docs/runtime/artifacts/tixl_mesh_draw_stage_mrt_matrix/tixl_mesh_draw_stage_mrt_matrix_result.json");
  assert.equal(graph.tixlMeshDrawTextureCubePbrReferenceArtifact, "docs/runtime/artifacts/tixl_mesh_draw_texturecube_pbr_reference/tixl_mesh_draw_texturecube_pbr_reference_result.json");
  assert.equal(graph.tixlMeshDrawBackendReplacementGateArtifact, "docs/runtime/artifacts/tixl_mesh_draw_backend_replacement_gate/tixl_mesh_draw_backend_replacement_gate_result.json");
  assert.equal(graph.expected.overallStatus, "proven_with_bounded_native_backend");
  assert.equal(graph.expected.drawCalls, 1);
  assert.equal(graph.expected.commandSource, "drawCommandArtifact");
  assert.equal(graph.expected.nonBlackSample, true);
});

test("RuntimeClosureReport shell emits a closure ledger from existing proof artifacts", () => {
  const run = spawnSync("python3", [scriptPath, fixturePath, artifactDir], {
    cwd: repoRoot,
    encoding: "utf8",
  });

  assert.equal(run.status, 0, run.stderr || run.stdout);

  const report = readArtifact("runtime_closure_report.json");
  const trace = readArtifact("runtime_closure_trace.json");
  const errors = readArtifact("runtime_closure_errors.json");

  assert.deepEqual(errors, []);
  assert.equal(report.kind, "RuntimeClosureReport");
  assert.equal(report.ok, true);
  assert.equal(report.overallStatus, "proven_with_bounded_native_backend");
  assert.ok(report.proven.includes("core_headless_pipeline"));
  assert.ok(report.proven.includes("tixl_mesh_draw_stage_mrt_matrix_semantics"));
  assert.ok(report.proven.includes("tixl_mesh_draw_texturecube_samplelevel_getdimensions"));
  assert.ok(report.bounded.includes("native_hlsl_metal_compile"));
  assert.ok(report.bounded.includes("shadergraph_t8_resources_empty_for_sphere_sdf_fixture"));
  assert.ok(report.bounded.includes("bounded_pbr_visual_reference"));
  assert.ok(report.bounded.includes("bounded_native_backend_replacement_guarded"));
  assert.deepEqual(report.broken, []);
  assert.deepEqual(report.requiredNext, []);
  assert.ok(!report.requiredNext.includes("replace_bounded_backend_interface_only_after_full_resource_binding_and_adapter_proof"));
  assert.ok(!report.requiredNext.includes("prove_texturecube_samplelevel_getdimensions_and_pbr_visual_reference"));
  assert.ok(!report.requiredNext.includes("prove_stage_mrt_matrix_semantics_for_handwritten_mesh_draw_adapter"));
  assert.ok(!report.requiredNext.includes("expand_t8_shadergraph_resources_and_set_mrt_stage_matrix_cube_pbr_reference_gates"));
  assert.ok(!report.requiredNext.includes("map_handwritten_explicit_msl_adapter_textures_samplers_t2_t7_s0_s1"));
  assert.ok(!report.requiredNext.includes("prove_native_b5_packing_from_source_backed_shadergraph_params"));
  assert.ok(!report.requiredNext.includes("produce_source_backed_shadergraph_param_expansion_artifact_for_b5"));
  assert.ok(!report.requiredNext.includes("expand_shadergraph_duplicate_params_b5_before_full_constant_buffer_adapter"));
  assert.ok(!report.requiredNext.includes("prove_or_reject_hlsl_to_msl_translation_for_mesh_draw"));
  assert.ok(!report.requiredNext.includes("bind_full_pbr_texture_sampler_set_after_hlsl_to_msl_translation"));
  assert.ok(!report.requiredNext.includes("replace_bounded_backend_interface_after_resource_binding_and_hlsl_to_msl_proof"));
  assert.ok(!report.requiredNext.includes("prove_native_mesh_resource_binding_against_pbrvertex_faceindices_layout"));
  assert.ok(!report.requiredNext.includes("implement_msl_draw_approximation_from_tixl_mesh_draw_buffer_layout"));
  assert.ok(!report.requiredNext.includes("implement_native_draw_shader_compile_parity"));
  assert.ok(!report.requiredNext.includes("replace_bounded_backend_interface_with_native_compile_proof"));
  assert.deepEqual(report.summary, {
    drawCalls: 1,
    selectedMaterialId: "glass",
    nativeDrawShaderStatus: "compileParityNotClaimed",
    backendCanCompileNow: false,
    nonBlackSample: true,
    shadergraphResourcesExpansionStatus: "proven_empty_t8_shadergraph_resources_for_sphere_sdf_fixture",
    stageMrtMatrixStatus: "proven_tixl_mesh_draw_stage_mrt_matrix_semantics",
    textureCubePbrReferenceStatus: "proven_texturecube_samplelevel_getdimensions_and_bounded_pbr_reference",
    backendReplacementGateStatus: "guarded_backend_replacement_blocked",
    backendReplacementReady: false,
    fullPbrResourceBinding: false,
    nativeGpuParityComplete: false,
  });

  assert.equal(report.evidence.pipelineSummary, "docs/runtime/artifacts/native_render_pipeline/pipeline_summary.json");
  assert.equal(report.evidence.commandStreamSummary, "docs/runtime/artifacts/native_render_pipeline/command_stream_summary.json");
  assert.equal(report.evidence.shaderProgramPackage, "docs/runtime/artifacts/native_render_pipeline/shader_program/shader_program_package.json");
  assert.equal(report.evidence.nativeBackendInterface, "docs/runtime/artifacts/native_render_pipeline/native_backend/native_backend_interface.json");
  assert.equal(report.evidence.backendStatus, "docs/runtime/artifacts/native_render_pipeline/native_backend/backend_status.json");
  assert.equal(report.evidence.pipelineErrors, "docs/runtime/artifacts/native_render_pipeline/native_render_pipeline_errors.json");
  assert.equal(report.evidence.shadergraphResourcesExpansion, "docs/runtime/artifacts/tixl_mesh_draw_shadergraph_resources_expansion/tixl_mesh_draw_shadergraph_resources_expansion_result.json");
  assert.equal(report.evidence.stageMrtMatrix, "docs/runtime/artifacts/tixl_mesh_draw_stage_mrt_matrix/tixl_mesh_draw_stage_mrt_matrix_result.json");
  assert.equal(report.evidence.textureCubePbrReference, "docs/runtime/artifacts/tixl_mesh_draw_texturecube_pbr_reference/tixl_mesh_draw_texturecube_pbr_reference_result.json");
  assert.equal(report.evidence.backendReplacementGate, "docs/runtime/artifacts/tixl_mesh_draw_backend_replacement_gate/tixl_mesh_draw_backend_replacement_gate_result.json");

  assert.deepEqual(trace.map((entry) => entry.op), [
    "loadRuntimeClosureFixture",
    "readNativeRenderPipelineArtifacts",
    "readShadergraphResourcesExpansionArtifact",
    "readStageMrtMatrixArtifact",
    "readTextureCubePbrReferenceArtifact",
    "readBackendReplacementGateArtifact",
    "evaluateCoreHeadlessPipeline",
    "evaluateNativeCompileBoundary",
    "evaluateShadergraphResourcesExpansion",
    "evaluateStageMrtMatrixSemantics",
    "evaluateTextureCubePbrReference",
    "evaluateBackendReplacementGate",
    "publishRuntimeClosureReport",
  ]);
});

test("RuntimeClosureReport shell fails when pipeline_summary.ok is false", () => {
  const tmpDir = fs.mkdtempSync(path.join(os.tmpdir(), "runtime-closure-bad-"));
  const badArtifactDir = path.join(tmpDir, "native_render_pipeline");
  const badOutDir = path.join(tmpDir, "closure_report");
  fs.cpSync(pipelineArtifactDir, badArtifactDir, { recursive: true });

  const pipelineSummaryPath = path.join(badArtifactDir, "pipeline_summary.json");
  const pipelineSummary = JSON.parse(fs.readFileSync(pipelineSummaryPath, "utf8"));
  pipelineSummary.ok = false;
  fs.writeFileSync(pipelineSummaryPath, JSON.stringify(pipelineSummary, null, 2));

  const fixture = JSON.parse(fs.readFileSync(fixturePath, "utf8"));
  fixture.nativeRenderPipelineArtifacts = badArtifactDir;
  const badFixturePath = path.join(tmpDir, "runtime_closure_report.graph.json");
  fs.writeFileSync(badFixturePath, JSON.stringify(fixture, null, 2));

  const run = spawnSync("python3", [scriptPath, badFixturePath, badOutDir], {
    cwd: repoRoot,
    encoding: "utf8",
  });

  assert.equal(run.status, 1);
  const report = JSON.parse(fs.readFileSync(path.join(badOutDir, "runtime_closure_report.json"), "utf8"));
  const errors = JSON.parse(fs.readFileSync(path.join(badOutDir, "runtime_closure_errors.json"), "utf8"));

  assert.equal(report.ok, false);
  assert.ok(report.broken.includes("core_headless_pipeline"));
  assert.equal(errors[0].code, "runtime_closure.pipeline_not_ok");
  assert.equal(errors[0].path, path.join(badArtifactDir, "pipeline_summary.json"));
});

test("RuntimeClosureReport shell fails when native render pipeline errors are non-empty", () => {
  const tmpDir = fs.mkdtempSync(path.join(os.tmpdir(), "runtime-closure-errors-"));
  const badArtifactDir = path.join(tmpDir, "native_render_pipeline");
  const badOutDir = path.join(tmpDir, "closure_report");
  fs.cpSync(pipelineArtifactDir, badArtifactDir, { recursive: true });

  const pipelineErrorsPath = path.join(badArtifactDir, "native_render_pipeline_errors.json");
  fs.writeFileSync(pipelineErrorsPath, JSON.stringify([
    {
      code: "native_render_pipeline.synthetic_review_failure",
      message: "review negative case",
    },
  ], null, 2));

  const fixture = JSON.parse(fs.readFileSync(fixturePath, "utf8"));
  fixture.nativeRenderPipelineArtifacts = badArtifactDir;
  const badFixturePath = path.join(tmpDir, "runtime_closure_report.graph.json");
  fs.writeFileSync(badFixturePath, JSON.stringify(fixture, null, 2));

  const run = spawnSync("python3", [scriptPath, badFixturePath, badOutDir], {
    cwd: repoRoot,
    encoding: "utf8",
  });

  assert.equal(run.status, 1);
  const report = JSON.parse(fs.readFileSync(path.join(badOutDir, "runtime_closure_report.json"), "utf8"));
  const errors = JSON.parse(fs.readFileSync(path.join(badOutDir, "runtime_closure_errors.json"), "utf8"));

  assert.equal(report.ok, false);
  assert.ok(report.broken.includes("core_headless_pipeline"));
  const pipelineError = errors.find((error) => error.code === "runtime_closure.pipeline_errors_present");
  assert.ok(pipelineError);
  assert.equal(pipelineError.count, 1);
});

test("RuntimeClosureReport shell fails when shadergraph resources expansion artifact is missing", () => {
  const tmpDir = fs.mkdtempSync(path.join(os.tmpdir(), "runtime-closure-missing-resources-"));
  const badOutDir = path.join(tmpDir, "closure_report");
  const fixture = JSON.parse(fs.readFileSync(fixturePath, "utf8"));
  fixture.tixlMeshDrawShadergraphResourcesExpansionArtifact = path.join(tmpDir, "missing_resources_result.json");
  const badFixturePath = path.join(tmpDir, "runtime_closure_report.graph.json");
  fs.writeFileSync(badFixturePath, JSON.stringify(fixture, null, 2));

  const run = spawnSync("python3", [scriptPath, badFixturePath, badOutDir], {
    cwd: repoRoot,
    encoding: "utf8",
  });

  assert.equal(run.status, 1);
  const report = JSON.parse(fs.readFileSync(path.join(badOutDir, "runtime_closure_report.json"), "utf8"));
  const errors = JSON.parse(fs.readFileSync(path.join(badOutDir, "runtime_closure_errors.json"), "utf8"));

  assert.equal(report.ok, false);
  assert.ok(!report.bounded.includes("shadergraph_t8_resources_empty_for_sphere_sdf_fixture"));
  assert.ok(report.requiredNext.includes("expand_t8_shadergraph_resources_and_set_mrt_stage_matrix_cube_pbr_reference_gates"));
  assert.ok(errors.some((error) => error.code === "runtime_closure.shadergraph_resources_expansion_read_failed"));
  assert.ok(errors.some((error) => error.code === "runtime_closure.shadergraph_resources_expansion_not_proven"));
});

test("RuntimeClosureReport shell fails when shadergraph resources expansion artifact widens claims", () => {
  const tmpDir = fs.mkdtempSync(path.join(os.tmpdir(), "runtime-closure-bad-resources-"));
  const badOutDir = path.join(tmpDir, "closure_report");
  const forgedArtifact = JSON.parse(fs.readFileSync(shadergraphResourcesArtifactPath, "utf8"));
  forgedArtifact.graphId = "fixture.some_other_shadergraph_resources_expansion";
  forgedArtifact.resourceExpansion.visitedShaderGraphNodes = ["SomeOtherNode"];
  forgedArtifact.resourceExpansion.resourceReferences = ["FakeTexture"];
  forgedArtifact.resourceExpansion.resourceDefinitions = ["Texture2D FakeTexture : register(t8);"];
  forgedArtifact.resourceExpansion.currentFixtureT8ResourcesEmpty = false;
  forgedArtifact.claims.realSrvCreationProven = true;
  forgedArtifact.resourceExpansion.resourceViewsCount = 1;
  const forgedArtifactPath = path.join(tmpDir, "resources_result.json");
  fs.writeFileSync(forgedArtifactPath, JSON.stringify(forgedArtifact, null, 2));

  const fixture = JSON.parse(fs.readFileSync(fixturePath, "utf8"));
  fixture.tixlMeshDrawShadergraphResourcesExpansionArtifact = forgedArtifactPath;
  const badFixturePath = path.join(tmpDir, "runtime_closure_report.graph.json");
  fs.writeFileSync(badFixturePath, JSON.stringify(fixture, null, 2));

  const run = spawnSync("python3", [scriptPath, badFixturePath, badOutDir], {
    cwd: repoRoot,
    encoding: "utf8",
  });

  assert.equal(run.status, 1);
  const report = JSON.parse(fs.readFileSync(path.join(badOutDir, "runtime_closure_report.json"), "utf8"));
  const errors = JSON.parse(fs.readFileSync(path.join(badOutDir, "runtime_closure_errors.json"), "utf8"));

  assert.equal(report.ok, false);
  const resourcesError = errors.find((error) => error.code === "runtime_closure.shadergraph_resources_expansion_not_proven");
  assert.ok(resourcesError);
  const fields = resourcesError.mismatches.map((mismatch) => mismatch.field);
  assert.ok(fields.includes("graphId"));
  assert.ok(fields.includes("resourceExpansion.visitedShaderGraphNodes"));
  assert.ok(fields.includes("resourceExpansion.resourceReferences"));
  assert.ok(fields.includes("resourceExpansion.resourceDefinitions"));
  assert.ok(fields.includes("resourceExpansion.currentFixtureT8ResourcesEmpty"));
  assert.ok(fields.includes("claims.realSrvCreationProven"));
  assert.ok(fields.includes("resourceExpansion.resourceViewsCount"));
});

test("RuntimeClosureReport shell fails when stage/MRT/matrix artifact is missing", () => {
  const tmpDir = fs.mkdtempSync(path.join(os.tmpdir(), "runtime-closure-missing-stage-"));
  const badOutDir = path.join(tmpDir, "closure_report");
  const fixture = JSON.parse(fs.readFileSync(fixturePath, "utf8"));
  fixture.tixlMeshDrawStageMrtMatrixArtifact = path.join(tmpDir, "missing_stage_result.json");
  const badFixturePath = path.join(tmpDir, "runtime_closure_report.graph.json");
  fs.writeFileSync(badFixturePath, JSON.stringify(fixture, null, 2));

  const run = spawnSync("python3", [scriptPath, badFixturePath, badOutDir], {
    cwd: repoRoot,
    encoding: "utf8",
  });

  assert.equal(run.status, 1);
  const report = JSON.parse(fs.readFileSync(path.join(badOutDir, "runtime_closure_report.json"), "utf8"));
  const errors = JSON.parse(fs.readFileSync(path.join(badOutDir, "runtime_closure_errors.json"), "utf8"));

  assert.equal(report.ok, false);
  assert.ok(!report.proven.includes("tixl_mesh_draw_stage_mrt_matrix_semantics"));
  assert.ok(report.requiredNext.includes("prove_stage_mrt_matrix_semantics_for_handwritten_mesh_draw_adapter"));
  assert.ok(errors.some((error) => error.code === "runtime_closure.stage_mrt_matrix_read_failed"));
  assert.ok(errors.some((error) => error.code === "runtime_closure.stage_mrt_matrix_not_proven"));
});

test("RuntimeClosureReport shell fails when stage/MRT/matrix artifact widens claims", () => {
  const tmpDir = fs.mkdtempSync(path.join(os.tmpdir(), "runtime-closure-bad-stage-"));
  const badOutDir = path.join(tmpDir, "closure_report");
  const forgedArtifact = JSON.parse(fs.readFileSync(stageMrtMatrixArtifactPath, "utf8"));
  forgedArtifact.graphId = "fixture.some_other_stage_mrt_matrix";
  forgedArtifact.claims.hlslToMslTranslation = true;
  forgedArtifact.claims.textureCubeSampleLevelGetDimensionsProven = true;
  forgedArtifact.sourceFacts.psOutputNormalTarget1 = false;
  forgedArtifact.explicitMetalProbe.status = "forged_probe";
  forgedArtifact.explicitMetalProbe.actualMetalRan = false;
  forgedArtifact.explicitMetalProbe.target0 = [0, 0, 0, 0];
  forgedArtifact.explicitMetalProbe.target1 = [0, 0, 0, 0];
  forgedArtifact.explicitMetalProbe.tixlDonorHlslMetalProbeRan = true;
  const forgedArtifactPath = path.join(tmpDir, "stage_result.json");
  fs.writeFileSync(forgedArtifactPath, JSON.stringify(forgedArtifact, null, 2));

  const fixture = JSON.parse(fs.readFileSync(fixturePath, "utf8"));
  fixture.tixlMeshDrawStageMrtMatrixArtifact = forgedArtifactPath;
  const badFixturePath = path.join(tmpDir, "runtime_closure_report.graph.json");
  fs.writeFileSync(badFixturePath, JSON.stringify(fixture, null, 2));

  const run = spawnSync("python3", [scriptPath, badFixturePath, badOutDir], {
    cwd: repoRoot,
    encoding: "utf8",
  });

  assert.equal(run.status, 1);
  const report = JSON.parse(fs.readFileSync(path.join(badOutDir, "runtime_closure_report.json"), "utf8"));
  const errors = JSON.parse(fs.readFileSync(path.join(badOutDir, "runtime_closure_errors.json"), "utf8"));

  assert.equal(report.ok, false);
  assert.ok(report.requiredNext.includes("prove_stage_mrt_matrix_semantics_for_handwritten_mesh_draw_adapter"));
  const stageError = errors.find((error) => error.code === "runtime_closure.stage_mrt_matrix_not_proven");
  assert.ok(stageError);
  const fields = stageError.mismatches.map((mismatch) => mismatch.field);
  assert.ok(fields.includes("graphId"));
  assert.ok(fields.includes("claims.hlslToMslTranslation"));
  assert.ok(fields.includes("claims.textureCubeSampleLevelGetDimensionsProven"));
  assert.ok(fields.includes("sourceFacts.psOutputNormalTarget1"));
  assert.ok(fields.includes("explicitMetalProbe.status"));
  assert.ok(fields.includes("explicitMetalProbe.actualMetalRan"));
  assert.ok(fields.includes("explicitMetalProbe.target0"));
  assert.ok(fields.includes("explicitMetalProbe.target1"));
  assert.ok(fields.includes("explicitMetalProbe.tixlDonorHlslMetalProbeRan"));
});

test("RuntimeClosureReport shell fails and keeps cube/PBR gate when TextureCube/PBR reference artifact is missing", () => {
  const tmpDir = fs.mkdtempSync(path.join(os.tmpdir(), "runtime-closure-missing-texturecube-"));
  const badOutDir = path.join(tmpDir, "closure_report");
  const fixture = JSON.parse(fs.readFileSync(fixturePath, "utf8"));
  fixture.tixlMeshDrawTextureCubePbrReferenceArtifact = path.join(tmpDir, "missing_texturecube_result.json");
  const badFixturePath = path.join(tmpDir, "runtime_closure_report.graph.json");
  fs.writeFileSync(badFixturePath, JSON.stringify(fixture, null, 2));

  const run = spawnSync("python3", [scriptPath, badFixturePath, badOutDir], {
    cwd: repoRoot,
    encoding: "utf8",
  });

  assert.equal(run.status, 1);
  const report = JSON.parse(fs.readFileSync(path.join(badOutDir, "runtime_closure_report.json"), "utf8"));
  const errors = JSON.parse(fs.readFileSync(path.join(badOutDir, "runtime_closure_errors.json"), "utf8"));

  assert.equal(report.ok, false);
  assert.ok(!report.proven.includes("tixl_mesh_draw_texturecube_samplelevel_getdimensions"));
  assert.ok(!report.bounded.includes("bounded_pbr_visual_reference"));
  assert.ok(report.requiredNext.includes("prove_texturecube_samplelevel_getdimensions_and_pbr_visual_reference"));
  assert.ok(errors.some((error) => error.code === "runtime_closure.texturecube_pbr_reference_read_failed"));
  assert.ok(errors.some((error) => error.code === "runtime_closure.texturecube_pbr_reference_not_proven"));
});

test("RuntimeClosureReport shell fails and keeps cube/PBR gate when TextureCube/PBR reference artifact widens claims", () => {
  const tmpDir = fs.mkdtempSync(path.join(os.tmpdir(), "runtime-closure-bad-texturecube-"));
  const badOutDir = path.join(tmpDir, "closure_report");
  const forgedArtifact = JSON.parse(fs.readFileSync(textureCubePbrReferenceArtifactPath, "utf8"));
  forgedArtifact.graphId = "fixture.some_other_texturecube_pbr_reference";
  forgedArtifact.claims.fullPbrResourceBinding = true;
  forgedArtifact.claims.pbrVisualCorrectness = true;
  forgedArtifact.claims.hlslToMslTranslation = true;
  forgedArtifact.textureCubeApiProbe.status = "forged_probe";
  forgedArtifact.textureCubeApiProbe.actualMetalRan = false;
  forgedArtifact.textureCubeApiProbe.dimensions = { width: 8, height: 8, mipLevels: 1 };
  forgedArtifact.textureCubeApiProbe.mip1Dimensions = { width: 4, height: 4 };
  forgedArtifact.textureCubeApiProbe.sampleLevel1Rgba8 = [0, 0, 0, 255];
  forgedArtifact.boundedPbrVisualReference.comparison.status = "forged_full_pbr_match";
  const forgedArtifactPath = path.join(tmpDir, "texturecube_result.json");
  fs.writeFileSync(forgedArtifactPath, JSON.stringify(forgedArtifact, null, 2));

  const fixture = JSON.parse(fs.readFileSync(fixturePath, "utf8"));
  fixture.tixlMeshDrawTextureCubePbrReferenceArtifact = forgedArtifactPath;
  const badFixturePath = path.join(tmpDir, "runtime_closure_report.graph.json");
  fs.writeFileSync(badFixturePath, JSON.stringify(fixture, null, 2));

  const run = spawnSync("python3", [scriptPath, badFixturePath, badOutDir], {
    cwd: repoRoot,
    encoding: "utf8",
  });

  assert.equal(run.status, 1);
  const report = JSON.parse(fs.readFileSync(path.join(badOutDir, "runtime_closure_report.json"), "utf8"));
  const errors = JSON.parse(fs.readFileSync(path.join(badOutDir, "runtime_closure_errors.json"), "utf8"));

  assert.equal(report.ok, false);
  assert.ok(report.requiredNext.includes("prove_texturecube_samplelevel_getdimensions_and_pbr_visual_reference"));
  const textureCubeError = errors.find((error) => error.code === "runtime_closure.texturecube_pbr_reference_not_proven");
  assert.ok(textureCubeError);
  const fields = textureCubeError.mismatches.map((mismatch) => mismatch.field);
  assert.ok(fields.includes("graphId"));
  assert.ok(fields.includes("claims.fullPbrResourceBinding"));
  assert.ok(fields.includes("claims.pbrVisualCorrectness"));
  assert.ok(fields.includes("claims.hlslToMslTranslation"));
  assert.ok(fields.includes("textureCubeApiProbe.status"));
  assert.ok(fields.includes("textureCubeApiProbe.actualMetalRan"));
  assert.ok(fields.includes("textureCubeApiProbe.dimensions"));
  assert.ok(fields.includes("textureCubeApiProbe.mip1Dimensions"));
  assert.ok(fields.includes("textureCubeApiProbe.sampleLevel1Rgba8"));
  assert.ok(fields.includes("boundedPbrVisualReference.comparison.status"));
});

test("RuntimeClosureReport shell fails and keeps backend replacement gate when gate proof is missing", () => {
  const tmpDir = fs.mkdtempSync(path.join(os.tmpdir(), "runtime-closure-missing-backend-gate-"));
  const badOutDir = path.join(tmpDir, "closure_report");
  const fixture = JSON.parse(fs.readFileSync(fixturePath, "utf8"));
  fixture.tixlMeshDrawBackendReplacementGateArtifact = path.join(tmpDir, "missing_backend_gate_result.json");
  const badFixturePath = path.join(tmpDir, "runtime_closure_report.graph.json");
  fs.writeFileSync(badFixturePath, JSON.stringify(fixture, null, 2));

  const run = spawnSync("python3", [scriptPath, badFixturePath, badOutDir], {
    cwd: repoRoot,
    encoding: "utf8",
  });

  assert.equal(run.status, 1);
  const report = JSON.parse(fs.readFileSync(path.join(badOutDir, "runtime_closure_report.json"), "utf8"));
  const errors = JSON.parse(fs.readFileSync(path.join(badOutDir, "runtime_closure_errors.json"), "utf8"));

  assert.equal(report.ok, false);
  assert.ok(!report.bounded.includes("bounded_native_backend_replacement_guarded"));
  assert.ok(report.requiredNext.includes("replace_bounded_backend_interface_only_after_full_resource_binding_and_adapter_proof"));
  assert.ok(errors.some((error) => error.code === "runtime_closure.backend_replacement_gate_read_failed"));
  assert.ok(errors.some((error) => error.code === "runtime_closure.backend_replacement_gate_not_proven"));
});

test("RuntimeClosureReport shell fails and keeps backend replacement gate when gate proof widens claims", () => {
  const tmpDir = fs.mkdtempSync(path.join(os.tmpdir(), "runtime-closure-bad-backend-gate-"));
  const badOutDir = path.join(tmpDir, "closure_report");
  const forgedArtifact = JSON.parse(fs.readFileSync(backendReplacementGateArtifactPath, "utf8"));
  forgedArtifact.graphId = "fixture.some_other_backend_replacement_gate";
  forgedArtifact.claims.backendReplacementReady = true;
  forgedArtifact.claims.fullPbrResourceBinding = true;
  forgedArtifact.claims.tixlRuntimeParity = true;
  forgedArtifact.guard.decision = "replacement_ready";
  forgedArtifact.guard.fullPbrResourceBindingPresent = true;
  const forgedArtifactPath = path.join(tmpDir, "backend_gate_result.json");
  fs.writeFileSync(forgedArtifactPath, JSON.stringify(forgedArtifact, null, 2));

  const fixture = JSON.parse(fs.readFileSync(fixturePath, "utf8"));
  fixture.tixlMeshDrawBackendReplacementGateArtifact = forgedArtifactPath;
  const badFixturePath = path.join(tmpDir, "runtime_closure_report.graph.json");
  fs.writeFileSync(badFixturePath, JSON.stringify(fixture, null, 2));

  const run = spawnSync("python3", [scriptPath, badFixturePath, badOutDir], {
    cwd: repoRoot,
    encoding: "utf8",
  });

  assert.equal(run.status, 1);
  const report = JSON.parse(fs.readFileSync(path.join(badOutDir, "runtime_closure_report.json"), "utf8"));
  const errors = JSON.parse(fs.readFileSync(path.join(badOutDir, "runtime_closure_errors.json"), "utf8"));

  assert.equal(report.ok, false);
  assert.ok(report.requiredNext.includes("replace_bounded_backend_interface_only_after_full_resource_binding_and_adapter_proof"));
  const gateError = errors.find((error) => error.code === "runtime_closure.backend_replacement_gate_not_proven");
  assert.ok(gateError);
  const fields = gateError.mismatches.map((mismatch) => mismatch.field);
  assert.ok(fields.includes("graphId"));
  assert.ok(fields.includes("claims.backendReplacementReady"));
  assert.ok(fields.includes("claims.fullPbrResourceBinding"));
  assert.ok(fields.includes("claims.tixlRuntimeParity"));
  assert.ok(fields.includes("guard.decision"));
  assert.ok(fields.includes("guard.fullPbrResourceBindingPresent"));
});

test("RuntimeClosureReport shell fails when native compile boundary is broken instead of bounded", () => {
  const tmpDir = fs.mkdtempSync(path.join(os.tmpdir(), "runtime-closure-native-boundary-"));
  const badArtifactDir = path.join(tmpDir, "native_render_pipeline");
  const badOutDir = path.join(tmpDir, "closure_report");
  fs.cpSync(pipelineArtifactDir, badArtifactDir, { recursive: true });

  const backendInterfacePath = path.join(badArtifactDir, "native_backend/native_backend_interface.json");
  const backendInterface = JSON.parse(fs.readFileSync(backendInterfacePath, "utf8"));
  delete backendInterface.nativeDrawBoundary;
  fs.writeFileSync(backendInterfacePath, JSON.stringify(backendInterface, null, 2));

  const fixture = JSON.parse(fs.readFileSync(fixturePath, "utf8"));
  fixture.nativeRenderPipelineArtifacts = badArtifactDir;
  const badFixturePath = path.join(tmpDir, "runtime_closure_report.graph.json");
  fs.writeFileSync(badFixturePath, JSON.stringify(fixture, null, 2));

  const run = spawnSync("python3", [scriptPath, badFixturePath, badOutDir], {
    cwd: repoRoot,
    encoding: "utf8",
  });

  assert.equal(run.status, 1);
  const report = JSON.parse(fs.readFileSync(path.join(badOutDir, "runtime_closure_report.json"), "utf8"));

  assert.equal(report.ok, false);
  assert.equal(report.overallStatus, "broken");
  assert.ok(report.broken.includes("native_hlsl_metal_compile"));
});

test("RuntimeClosureReport shell fails when native compile boundary is supported instead of bounded", () => {
  const tmpDir = fs.mkdtempSync(path.join(os.tmpdir(), "runtime-closure-native-supported-"));
  const badArtifactDir = path.join(tmpDir, "native_render_pipeline");
  const badOutDir = path.join(tmpDir, "closure_report");
  fs.cpSync(pipelineArtifactDir, badArtifactDir, { recursive: true });

  const backendInterfacePath = path.join(badArtifactDir, "native_backend/native_backend_interface.json");
  const backendInterface = JSON.parse(fs.readFileSync(backendInterfacePath, "utf8"));
  backendInterface.nativeDrawBoundary.status = "supported";
  backendInterface.nativeDrawBoundary.backendCanCompileNow = true;
  fs.writeFileSync(backendInterfacePath, JSON.stringify(backendInterface, null, 2));

  const backendStatusPath = path.join(badArtifactDir, "native_backend/backend_status.json");
  const backendStatus = JSON.parse(fs.readFileSync(backendStatusPath, "utf8"));
  backendStatus.nativeDrawShaderStatus = "supported";
  backendStatus.nativeDrawShaderCanCompileNow = true;
  fs.writeFileSync(backendStatusPath, JSON.stringify(backendStatus, null, 2));

  const fixture = JSON.parse(fs.readFileSync(fixturePath, "utf8"));
  fixture.nativeRenderPipelineArtifacts = badArtifactDir;
  const badFixturePath = path.join(tmpDir, "runtime_closure_report.graph.json");
  fs.writeFileSync(badFixturePath, JSON.stringify(fixture, null, 2));

  const run = spawnSync("python3", [scriptPath, badFixturePath, badOutDir], {
    cwd: repoRoot,
    encoding: "utf8",
  });

  assert.equal(run.status, 1);
  const report = JSON.parse(fs.readFileSync(path.join(badOutDir, "runtime_closure_report.json"), "utf8"));

  assert.equal(report.ok, false);
  assert.equal(report.overallStatus, "broken");
  assert.ok(!report.bounded.includes("native_hlsl_metal_compile"));
});

function readArtifact(name) {
  return JSON.parse(fs.readFileSync(path.join(artifactDir, name), "utf8"));
}

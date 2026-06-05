#!/usr/bin/env python3
"""
Publish the backend replacement gate proof for the bounded TiXL mesh draw lane.

This proof consumes the positive full PBR binding, explicit adapter, and native
Metal backend integration proofs. It opens the bounded backend replacement gate
only for this TiXL mesh draw/PBR lane and still does not claim HLSL translation.
"""

from __future__ import annotations

import json
import sys
from pathlib import Path
from typing import Any


RESULT_NAME = "tixl_mesh_draw_backend_replacement_gate_result.json"
TRACE_NAME = "tixl_mesh_draw_backend_replacement_gate_trace.json"
ERRORS_NAME = "tixl_mesh_draw_backend_replacement_gate_errors.json"

DEFAULT_NATIVE_RENDER_PIPELINE_ARTIFACTS = "docs/runtime/artifacts/native_render_pipeline"
DEFAULT_RESOURCE_BINDING_ARTIFACT = "docs/runtime/artifacts/tixl_mesh_draw_resource_binding/tixl_mesh_draw_resource_binding_result.json"
DEFAULT_FULL_PBR_RESOURCE_BINDING_ARTIFACT = "docs/runtime/artifacts/tixl_mesh_draw_full_pbr_resource_binding/tixl_mesh_draw_full_pbr_resource_binding_result.json"
DEFAULT_EXPLICIT_ADAPTER_PROOF_ARTIFACT = "docs/runtime/artifacts/tixl_mesh_draw_explicit_adapter_proof/tixl_mesh_draw_explicit_adapter_result.json"
DEFAULT_NATIVE_METAL_BACKEND_INTEGRATION_ARTIFACT = "docs/runtime/artifacts/tixl_mesh_draw_native_metal_backend_integration/tixl_mesh_draw_native_metal_backend_integration_result.json"
DEFAULT_TEXTURE_SAMPLER_BINDING_ARTIFACT = "docs/runtime/artifacts/tixl_mesh_draw_texture_sampler_binding/tixl_mesh_draw_texture_sampler_binding_result.json"
DEFAULT_SHADERGRAPH_RESOURCES_EXPANSION_ARTIFACT = "docs/runtime/artifacts/tixl_mesh_draw_shadergraph_resources_expansion/tixl_mesh_draw_shadergraph_resources_expansion_result.json"
DEFAULT_STAGE_MRT_MATRIX_ARTIFACT = "docs/runtime/artifacts/tixl_mesh_draw_stage_mrt_matrix/tixl_mesh_draw_stage_mrt_matrix_result.json"
DEFAULT_TEXTURECUBE_PBR_REFERENCE_ARTIFACT = "docs/runtime/artifacts/tixl_mesh_draw_texturecube_pbr_reference/tixl_mesh_draw_texturecube_pbr_reference_result.json"

FORBIDDEN_TRUE_CLAIMS = [
    "backendReplacementReady",
    "fullPbrResourceBinding",
    "hlslToMslTranslation",
    "tixlRuntimeParity",
    "nativeGpuParityComplete",
    "rendererIntegrationComplete",
    "pbrVisualCorrectness",
]


def main() -> int:
    if len(sys.argv) != 3:
        print("usage: tixl_mesh_draw_backend_replacement_gate_shell.py <fixture.graph.json> <out_dir>", file=sys.stderr)
        return 2

    fixture_path = Path(sys.argv[1]).expanduser().resolve()
    out_dir = Path(sys.argv[2]).expanduser().resolve()
    out_dir.mkdir(parents=True, exist_ok=True)

    repo_root = Path(__file__).resolve().parents[3]
    trace: list[dict[str, Any]] = [{
        "op": "loadTixlMeshDrawBackendReplacementGateFixture",
        "fixture": display_path(fixture_path, repo_root),
    }]
    errors: list[dict[str, Any]] = []

    fixture = read_json(fixture_path, errors, "tixl_mesh_draw_backend_replacement_gate.fixture_read_failed", repo_root)
    if fixture is None:
        result = default_result(None, "blocked_missing_fixture", {})
        publish(out_dir, result, trace, errors)
        return 1

    result, run_trace, run_errors = run_gate(repo_root, fixture_path, fixture)
    trace.extend(run_trace)
    errors.extend(run_errors)
    trace.append({
        "op": "publishTixlMeshDrawBackendReplacementGateArtifacts",
        "ok": result.get("ok") is True and not errors,
    })
    publish(out_dir, result, trace, errors)
    return 0 if result.get("ok") is True and not errors else 1


def run_gate(repo_root: Path, fixture_path: Path, fixture: dict[str, Any]) -> tuple[dict[str, Any], list[dict[str, Any]], list[dict[str, Any]]]:
    trace: list[dict[str, Any]] = []
    errors: list[dict[str, Any]] = []
    graph_id = fixture.get("graphId")

    native_dir = resolve_path(repo_root, fixture_path, fixture.get("nativeRenderPipelineArtifacts"), DEFAULT_NATIVE_RENDER_PIPELINE_ARTIFACTS)
    paths = {
        "resourceBinding": resolve_path(repo_root, fixture_path, fixture.get("resourceBindingArtifact"), DEFAULT_RESOURCE_BINDING_ARTIFACT),
        "fullPbrResourceBinding": resolve_path(repo_root, fixture_path, fixture.get("fullPbrResourceBindingArtifact"), DEFAULT_FULL_PBR_RESOURCE_BINDING_ARTIFACT),
        "explicitAdapterProof": resolve_path(repo_root, fixture_path, fixture.get("explicitAdapterProofArtifact"), DEFAULT_EXPLICIT_ADAPTER_PROOF_ARTIFACT),
        "nativeMetalBackendIntegration": resolve_path(repo_root, fixture_path, fixture.get("nativeMetalBackendIntegrationArtifact"), DEFAULT_NATIVE_METAL_BACKEND_INTEGRATION_ARTIFACT),
        "textureSamplerBinding": resolve_path(repo_root, fixture_path, fixture.get("textureSamplerBindingArtifact"), DEFAULT_TEXTURE_SAMPLER_BINDING_ARTIFACT),
        "shadergraphResourcesExpansion": resolve_path(repo_root, fixture_path, fixture.get("shadergraphResourcesExpansionArtifact"), DEFAULT_SHADERGRAPH_RESOURCES_EXPANSION_ARTIFACT),
        "stageMrtMatrix": resolve_path(repo_root, fixture_path, fixture.get("stageMrtMatrixArtifact"), DEFAULT_STAGE_MRT_MATRIX_ARTIFACT),
        "textureCubePbrReference": resolve_path(repo_root, fixture_path, fixture.get("textureCubePbrReferenceArtifact"), DEFAULT_TEXTURECUBE_PBR_REFERENCE_ARTIFACT),
    }
    trace.append({
        "op": "resolveBackendReplacementGateInputs",
        "nativeRenderPipelineArtifacts": display_path(native_dir, repo_root),
        **{f"{key}Artifact": display_path(path, repo_root) for key, path in paths.items()},
    })

    native_artifacts = read_native_pipeline(native_dir, repo_root, errors)
    artifacts = {key: read_json(path, errors, f"tixl_mesh_draw_backend_replacement_gate.{key}_read_failed", repo_root) for key, path in paths.items()}
    input_summaries = {
        "nativeRenderPipeline": summarize_native_pipeline(native_dir, native_artifacts, repo_root),
        **{key: summarize_artifact(paths[key], artifact, repo_root) for key, artifact in artifacts.items()},
    }
    trace.append({
        "op": "readBackendReplacementGateInputs",
        "nativePipelineRead": native_artifacts.get("pipelineSummary") is not None and native_artifacts.get("backendInterface") is not None,
        **{f"{key}Read": artifacts[key] is not None for key in artifacts},
    })

    validation_errors: list[dict[str, Any]] = []
    validation_errors.extend(validate_native_pipeline(native_artifacts))
    validation_errors.extend(validate_resource_binding(artifacts.get("resourceBinding")))
    validation_errors.extend(validate_full_pbr_resource_binding(artifacts.get("fullPbrResourceBinding")))
    validation_errors.extend(validate_explicit_adapter_proof(artifacts.get("explicitAdapterProof")))
    validation_errors.extend(validate_native_metal_backend_integration(artifacts.get("nativeMetalBackendIntegration")))
    validation_errors.extend(validate_texture_sampler_binding(artifacts.get("textureSamplerBinding")))
    validation_errors.extend(validate_shadergraph_resources(artifacts.get("shadergraphResourcesExpansion")))
    validation_errors.extend(validate_stage_mrt_matrix(artifacts.get("stageMrtMatrix")))
    validation_errors.extend(validate_texturecube_pbr_reference(artifacts.get("textureCubePbrReference")))
    validation_errors.extend(validate_fixture_expectations(fixture))
    validation_errors.extend(validate_adapter_gate(fixture, artifacts.get("fullPbrResourceBinding"), artifacts.get("explicitAdapterProof")))
    trace.append({
        "op": "validateBackendReplacementGateInputs",
        "valid": not validation_errors,
        "mismatchCount": len(validation_errors),
    })
    if validation_errors:
        errors.append({
            "code": "tixl_mesh_draw_backend_replacement_gate.invalid_gate_inputs",
            "message": "backend replacement gate inputs either widened readiness claims or lost required bounded evidence",
            "mismatches": validation_errors,
        })
        return default_result(graph_id, "blocked_invalid_gate_inputs", input_summaries), trace, errors

    result = build_success_result(graph_id, input_summaries)
    trace.append({
        "op": "evaluateBackendReplacementGuard",
        "backendReplacementReady": True,
        "fullPbrResourceBinding": True,
        "adapterProofPresent": True,
        "nativeMetalBackendIntegrationComplete": True,
    })
    return result, trace, errors


def read_native_pipeline(native_dir: Path, repo_root: Path, errors: list[dict[str, Any]]) -> dict[str, Any]:
    return {
        "pipelineSummary": read_json(native_dir / "pipeline_summary.json", errors, "tixl_mesh_draw_backend_replacement_gate.pipeline_summary_read_failed", repo_root),
        "backendInterface": read_json(native_dir / "native_backend" / "native_backend_interface.json", errors, "tixl_mesh_draw_backend_replacement_gate.native_backend_interface_read_failed", repo_root),
        "backendStatus": read_json(native_dir / "native_backend" / "backend_status.json", errors, "tixl_mesh_draw_backend_replacement_gate.backend_status_read_failed", repo_root),
        "pipelineErrors": read_json(native_dir / "native_render_pipeline_errors.json", errors, "tixl_mesh_draw_backend_replacement_gate.pipeline_errors_read_failed", repo_root),
    }


def validate_native_pipeline(native: dict[str, Any]) -> list[dict[str, Any]]:
    mismatches: list[dict[str, Any]] = []
    summary = native.get("pipelineSummary") if isinstance(native.get("pipelineSummary"), dict) else {}
    backend = native.get("backendInterface") if isinstance(native.get("backendInterface"), dict) else {}
    status = native.get("backendStatus") if isinstance(native.get("backendStatus"), dict) else {}
    pipeline_errors = native.get("pipelineErrors")
    boundary = backend.get("nativeDrawBoundary") if isinstance(backend.get("nativeDrawBoundary"), dict) else {}
    if summary.get("ok") is not True:
        mismatches.append({"field": "nativeRenderPipeline.pipeline_summary.ok", "expected": True, "actual": summary.get("ok")})
    if summary.get("drawCalls") != 1:
        mismatches.append({"field": "nativeRenderPipeline.pipeline_summary.drawCalls", "expected": 1, "actual": summary.get("drawCalls")})
    if summary.get("nonBlackSample") is not True:
        mismatches.append({"field": "nativeRenderPipeline.pipeline_summary.nonBlackSample", "expected": True, "actual": summary.get("nonBlackSample")})
    if pipeline_errors != []:
        mismatches.append({"field": "nativeRenderPipeline.native_render_pipeline_errors", "expected": [], "actual": pipeline_errors})
    if boundary.get("status") != "compileParityNotClaimed":
        mismatches.append({"field": "nativeBackendInterface.nativeDrawBoundary.status", "expected": "compileParityNotClaimed", "actual": boundary.get("status")})
    if boundary.get("backendCanCompileNow") is not False:
        mismatches.append({"field": "nativeBackendInterface.nativeDrawBoundary.backendCanCompileNow", "expected": False, "actual": boundary.get("backendCanCompileNow")})
    if status.get("nativeDrawShaderStatus") != "compileParityNotClaimed":
        mismatches.append({"field": "backendStatus.nativeDrawShaderStatus", "expected": "compileParityNotClaimed", "actual": status.get("nativeDrawShaderStatus")})
    return mismatches


def validate_resource_binding(artifact: Any) -> list[dict[str, Any]]:
    mismatches: list[dict[str, Any]] = []
    if not isinstance(artifact, dict):
        return [{"field": "resourceBinding", "expected": "object", "actual": type(artifact).__name__}]
    if artifact.get("kind") != "TixlMeshDrawResourceBindingProof":
        mismatches.append({"field": "resourceBinding.kind", "expected": "TixlMeshDrawResourceBindingProof", "actual": artifact.get("kind")})
    if artifact.get("ok") is not True:
        mismatches.append({"field": "resourceBinding.ok", "expected": True, "actual": artifact.get("ok")})
    if artifact.get("status") != "summarized_tixl_mesh_draw_resource_binding":
        mismatches.append({"field": "resourceBinding.status", "expected": "summarized_tixl_mesh_draw_resource_binding", "actual": artifact.get("status")})
    claims = artifact.get("claims") if isinstance(artifact.get("claims"), dict) else {}
    for field in ("backendReplacementReady", "hlslToMslTranslation", "tixlRuntimeParity"):
        if claims.get(field) is not False:
            mismatches.append({"field": f"resourceBinding.claims.{field}", "expected": False, "actual": claims.get(field)})
    if claims.get("fullPbrResourceBinding") is not False:
        mismatches.append({"field": "resourceBinding.claims.fullPbrResourceBinding", "expected": False, "actual": claims.get("fullPbrResourceBinding")})
    ledger = artifact.get("bindingLedger") if isinstance(artifact.get("bindingLedger"), dict) else {}
    bound = {item.get("sourceRegister") for item in list_items(ledger.get("boundNow"))}
    if bound != {"t0", "t1"}:
        mismatches.append({"field": "resourceBinding.bindingLedger.boundNow", "expected": ["t0", "t1"], "actual": sorted(register for register in bound if register)})
    return mismatches


def validate_full_pbr_resource_binding(artifact: Any) -> list[dict[str, Any]]:
    mismatches = validate_common_artifact(
        artifact,
        "fullPbrResourceBinding",
        "TixlMeshDrawFullPbrResourceBindingProof",
        "proven_full_pbr_resource_binding",
    )
    if not isinstance(artifact, dict):
        return mismatches
    claims = artifact.get("claims") if isinstance(artifact.get("claims"), dict) else {}
    expected_true = (
        "sourceAuditArtifactConsumed",
        "meshBufferBindingArtifactConsumed",
        "constantBufferPackingArtifactsConsumed",
        "textureSamplerBindingArtifactConsumed",
        "shadergraphResourcesExpansionArtifactConsumed",
        "textureCubePbrReferenceArtifactConsumed",
        "actualMetalFullBindingProbeRan",
        "fullPbrResourceBinding",
    )
    for field in expected_true:
        if claims.get(field) is not True:
            mismatches.append({"field": f"fullPbrResourceBinding.claims.{field}", "expected": True, "actual": claims.get(field)})
    for field in ("backendReplacementReady", "explicitAdapterProof", "hlslToMslTranslation", "tixlRuntimeParity", "nativeGpuParityComplete", "pbrVisualCorrectness"):
        if claims.get(field) is not False:
            mismatches.append({"field": f"fullPbrResourceBinding.claims.{field}", "expected": False, "actual": claims.get(field)})
    ledger = artifact.get("bindingLedger") if isinstance(artifact.get("bindingLedger"), dict) else {}
    bound_registers = set(ledger.get("boundRegisters") or [])
    expected_bound = {"b0", "b1", "b2", "b3", "b4", "b5", "t0", "t1", "t2", "t3", "t4", "t5", "t6", "t7", "s0", "s1"}
    if bound_registers != expected_bound:
        mismatches.append({"field": "fullPbrResourceBinding.bindingLedger.boundRegisters", "expected": sorted(expected_bound), "actual": sorted(bound_registers)})
    t8 = ledger.get("t8ShadergraphResources") if isinstance(ledger.get("t8ShadergraphResources"), dict) else {}
    if t8.get("status") != "proven_empty_for_current_fixture":
        mismatches.append({"field": "fullPbrResourceBinding.bindingLedger.t8ShadergraphResources.status", "expected": "proven_empty_for_current_fixture", "actual": t8.get("status")})
    return mismatches


def validate_explicit_adapter_proof(artifact: Any) -> list[dict[str, Any]]:
    mismatches = validate_common_artifact(
        artifact,
        "explicitAdapterProof",
        "TixlMeshDrawExplicitAdapterProof",
        "proven_explicit_mesh_draw_adapter",
    )
    if not isinstance(artifact, dict):
        return mismatches
    claims = artifact.get("claims") if isinstance(artifact.get("claims"), dict) else {}
    for field in (
        "explicitTranslationStrategyArtifactConsumed",
        "mslApproxArtifactConsumed",
        "metalExplicitMslArtifactConsumed",
        "selectedHandwrittenAdapterStrategyConsumed",
        "mslApproxEvidenceConsumed",
        "metalExplicitMslEvidenceConsumed",
        "actualCompilerRan",
        "actualMetalRan",
        "explicitAdapterProof",
        "explicitAdapterProofPresent",
    ):
        if claims.get(field) is not True:
            mismatches.append({"field": f"explicitAdapterProof.claims.{field}", "expected": True, "actual": claims.get(field)})
    for field in ("fullPbrResourceBinding", "backendReplacementReady", "hlslToMslTranslation", "tixlRuntimeParity", "nativeGpuParityComplete", "pbrVisualCorrectness"):
        if claims.get(field) is not False:
            mismatches.append({"field": f"explicitAdapterProof.claims.{field}", "expected": False, "actual": claims.get(field)})
    frame_stats = artifact.get("frameStats") if isinstance(artifact.get("frameStats"), dict) else {}
    if frame_stats.get("nonBlack") is not True:
        mismatches.append({"field": "explicitAdapterProof.frameStats.nonBlack", "expected": True, "actual": frame_stats.get("nonBlack")})
    if frame_stats.get("varied") is not True:
        mismatches.append({"field": "explicitAdapterProof.frameStats.varied", "expected": True, "actual": frame_stats.get("varied")})
    return mismatches


def validate_native_metal_backend_integration(artifact: Any) -> list[dict[str, Any]]:
    mismatches = validate_common_artifact(
        artifact,
        "nativeMetalBackendIntegration",
        "TixlMeshDrawNativeMetalBackendIntegrationProof",
        "proven_native_metal_backend_integration_for_bounded_mesh_draw_pbr_lane",
    )
    if not isinstance(artifact, dict):
        return mismatches
    claims = artifact.get("claims") if isinstance(artifact.get("claims"), dict) else {}
    expected = {
        "nativeRenderPipelineArtifactConsumed": True,
        "fullPbrResourceBindingArtifactConsumed": True,
        "explicitAdapterProofArtifactConsumed": True,
        "actualMetalBackendProbeRan": True,
        "nativeBackendIntegrationComplete": True,
        "runtimeEquivalenceProof": True,
        "backendReplacementReady": True,
        "nativeGpuParityComplete": True,
        "tixlRuntimeParity": True,
        "fullPbrResourceBinding": True,
        "explicitAdapterProofPresent": True,
        "hlslToMslTranslation": False,
    }
    for field, value in expected.items():
        if claims.get(field) is not value:
            mismatches.append({"field": f"nativeMetalBackendIntegration.claims.{field}", "expected": value, "actual": claims.get(field)})
    boundary = artifact.get("nativeDrawBoundary") if isinstance(artifact.get("nativeDrawBoundary"), dict) else {}
    if boundary.get("status") != "supported" or boundary.get("backendCanCompileNow") is not True:
        mismatches.append({"field": "nativeMetalBackendIntegration.nativeDrawBoundary", "expected": {"status": "supported", "backendCanCompileNow": True}, "actual": boundary})
    equivalence = artifact.get("equivalence") if isinstance(artifact.get("equivalence"), dict) else {}
    frame = equivalence.get("frame") if isinstance(equivalence.get("frame"), dict) else {}
    if frame.get("nonBlack") is not True or frame.get("varied") is not True:
        mismatches.append({"field": "nativeMetalBackendIntegration.equivalence.frame", "expected": {"nonBlack": True, "varied": True}, "actual": frame})
    return mismatches


def validate_texture_sampler_binding(artifact: Any) -> list[dict[str, Any]]:
    mismatches = validate_common_artifact(
        artifact,
        "textureSamplerBinding",
        "TixlMeshDrawTextureSamplerBindingProof",
        "proven_tixl_mesh_draw_texture_sampler_binding",
    )
    if not isinstance(artifact, dict):
        return mismatches
    claims = artifact.get("claims") if isinstance(artifact.get("claims"), dict) else {}
    for field in ("boundedTextureSamplerMappingProven", "t2BaseColorMapBindingProven", "t7BrdfLookupBindingProven", "s0WrappedSamplerBindingProven", "s1ClampedSamplerBindingProven"):
        if claims.get(field) is not True:
            mismatches.append({"field": f"textureSamplerBinding.claims.{field}", "expected": True, "actual": claims.get(field)})
    validate_false_claims(claims, "textureSamplerBinding.claims", mismatches)
    return mismatches


def validate_shadergraph_resources(artifact: Any) -> list[dict[str, Any]]:
    mismatches = validate_common_artifact(
        artifact,
        "shadergraphResourcesExpansion",
        "TixlMeshDrawShadergraphResourcesExpansionProof",
        "proven_empty_t8_shadergraph_resources_for_sphere_sdf_fixture",
    )
    if isinstance(artifact, dict):
        claims = artifact.get("claims") if isinstance(artifact.get("claims"), dict) else {}
        if claims.get("shadergraphResourcesExpansionProven") is not True:
            mismatches.append({"field": "shadergraphResourcesExpansion.claims.shadergraphResourcesExpansionProven", "expected": True, "actual": claims.get("shadergraphResourcesExpansionProven")})
        validate_false_claims(claims, "shadergraphResourcesExpansion.claims", mismatches)
    return mismatches


def validate_stage_mrt_matrix(artifact: Any) -> list[dict[str, Any]]:
    mismatches = validate_common_artifact(
        artifact,
        "stageMrtMatrix",
        "TixlMeshDrawStageMrtMatrixProof",
        "proven_tixl_mesh_draw_stage_mrt_matrix_semantics",
    )
    if isinstance(artifact, dict):
        claims = artifact.get("claims") if isinstance(artifact.get("claims"), dict) else {}
        for field in ("handwrittenMeshDrawAdapterStageMrtMatrixProof", "sourceBackedOnly"):
            if claims.get(field) is not True:
                mismatches.append({"field": f"stageMrtMatrix.claims.{field}", "expected": True, "actual": claims.get(field)})
        validate_false_claims(claims, "stageMrtMatrix.claims", mismatches)
    return mismatches


def validate_texturecube_pbr_reference(artifact: Any) -> list[dict[str, Any]]:
    mismatches = validate_common_artifact(
        artifact,
        "textureCubePbrReference",
        "TixlMeshDrawTextureCubePbrReferenceProof",
        "proven_texturecube_samplelevel_getdimensions_and_bounded_pbr_reference",
    )
    if isinstance(artifact, dict):
        claims = artifact.get("claims") if isinstance(artifact.get("claims"), dict) else {}
        for field in ("textureCubeSampleLevelProven", "textureCubeGetDimensionsProven", "boundedPbrVisualReferenceEstablished"):
            if claims.get(field) is not True:
                mismatches.append({"field": f"textureCubePbrReference.claims.{field}", "expected": True, "actual": claims.get(field)})
        validate_false_claims(claims, "textureCubePbrReference.claims", mismatches)
    return mismatches


def validate_common_artifact(artifact: Any, prefix: str, kind: str, status: str) -> list[dict[str, Any]]:
    if not isinstance(artifact, dict):
        return [{"field": prefix, "expected": "object", "actual": type(artifact).__name__}]
    mismatches: list[dict[str, Any]] = []
    if artifact.get("kind") != kind:
        mismatches.append({"field": f"{prefix}.kind", "expected": kind, "actual": artifact.get("kind")})
    if artifact.get("ok") is not True:
        mismatches.append({"field": f"{prefix}.ok", "expected": True, "actual": artifact.get("ok")})
    if artifact.get("status") != status:
        mismatches.append({"field": f"{prefix}.status", "expected": status, "actual": artifact.get("status")})
    return mismatches


def validate_fixture_expectations(fixture: dict[str, Any]) -> list[dict[str, Any]]:
    mismatches: list[dict[str, Any]] = []
    if fixture.get("kind") != "TixlMeshDrawBackendReplacementGateProof":
        mismatches.append({"field": "kind", "expected": "TixlMeshDrawBackendReplacementGateProof", "actual": fixture.get("kind")})
    if fixture.get("graphId") != "fixture.tixl_mesh_draw_backend_replacement_gate":
        mismatches.append({"field": "graphId", "expected": "fixture.tixl_mesh_draw_backend_replacement_gate", "actual": fixture.get("graphId")})
    expected = fixture.get("expected") if isinstance(fixture.get("expected"), dict) else {}
    if expected.get("status") != "replacement_ready":
        mismatches.append({"field": "expected.status", "expected": "replacement_ready", "actual": expected.get("status")})
    if not fixture.get("fullPbrResourceBindingArtifact"):
        mismatches.append({"field": "fullPbrResourceBinding", "expected": "fullPbrResourceBindingArtifact path", "actual": fixture.get("fullPbrResourceBindingArtifact")})
    if not fixture.get("explicitAdapterProofArtifact"):
        mismatches.append({"field": "explicitAdapterProof", "expected": "explicitAdapterProofArtifact path", "actual": fixture.get("explicitAdapterProofArtifact")})
    if not fixture.get("nativeMetalBackendIntegrationArtifact"):
        mismatches.append({"field": "nativeMetalBackendIntegration", "expected": "nativeMetalBackendIntegrationArtifact path", "actual": fixture.get("nativeMetalBackendIntegrationArtifact")})
    claims = expected.get("claims") if isinstance(expected.get("claims"), dict) else {}
    for field, value in claim_flags(True).items():
        if claims.get(field) is not value:
            mismatches.append({"field": f"expected.claims.{field}", "expected": value, "actual": claims.get(field)})
    return mismatches


def validate_adapter_gate(fixture: dict[str, Any], full_pbr_resource_binding: Any, explicit_adapter_proof: Any) -> list[dict[str, Any]]:
    mismatches: list[dict[str, Any]] = []
    full_claims = full_pbr_resource_binding.get("claims") if isinstance(full_pbr_resource_binding, dict) and isinstance(full_pbr_resource_binding.get("claims"), dict) else {}
    adapter_claims = explicit_adapter_proof.get("claims") if isinstance(explicit_adapter_proof, dict) and isinstance(explicit_adapter_proof.get("claims"), dict) else {}
    if full_claims.get("fullPbrResourceBinding") is not True:
        mismatches.append({
            "field": "fullPbrResourceBinding",
            "expected": "positive full PBR resource binding proof",
            "actual": full_claims.get("fullPbrResourceBinding"),
        })
    if adapter_claims.get("explicitAdapterProofPresent") is not True:
        mismatches.append({
            "field": "explicitAdapterProof",
            "expected": "positive explicit adapter proof",
            "actual": adapter_claims.get("explicitAdapterProofPresent"),
        })
    return mismatches


def validate_false_claims(claims: dict[str, Any], prefix: str, mismatches: list[dict[str, Any]]) -> None:
    for field in FORBIDDEN_TRUE_CLAIMS:
        if claims.get(field) is True:
            mismatches.append({"field": f"{prefix}.{field}", "expected": False, "actual": claims.get(field)})


def build_success_result(graph_id: Any, artifacts: dict[str, Any]) -> dict[str, Any]:
    return {
        "kind": "TixlMeshDrawBackendReplacementGateProof",
        "graphId": graph_id,
        "ok": True,
        "status": "replacement_ready",
        "message": "backend replacement gate evaluated; bounded native Metal backend integration and runtime equivalence are proven for this lane",
        "inputArtifacts": artifacts,
        "guard": {
            "requiredBeforeReplacement": [
                "fullPbrResourceBinding",
                "explicitAdapterProof"
            ],
            "fullPbrResourceBindingPresent": True,
            "explicitAdapterProofPresent": True,
            "nativeMetalBackendIntegrationComplete": True,
            "runtimeEquivalenceProof": True,
            "decision": "replacement_ready",
            "boundedBackendState": "native_metal_backend_replaces_bounded_backend_for_this_lane"
        },
        "notProven": [
            "HLSL-to-MSL translation",
            "generic TiXL clone parity",
            "Vuo parity"
        ],
        "claims": claim_flags(True),
    }


def default_result(graph_id: Any, status: str, artifacts: dict[str, Any]) -> dict[str, Any]:
    return {
        "kind": "TixlMeshDrawBackendReplacementGateProof",
        "graphId": graph_id,
        "ok": False,
        "status": status,
        "inputArtifacts": artifacts,
        "guard": {
            "requiredBeforeReplacement": [
                "fullPbrResourceBinding",
                "explicitAdapterProof"
            ],
            "decision": "replacement_not_evaluated"
        },
        "claims": claim_flags(False),
    }


def claim_flags(evaluated: bool) -> dict[str, bool]:
    return {
        "backendReplacementGateEvaluated": evaluated,
        "nativeRenderPipelineArtifactConsumed": evaluated,
        "resourceBindingArtifactConsumed": evaluated,
        "fullPbrResourceBindingArtifactConsumed": evaluated,
        "explicitAdapterProofArtifactConsumed": evaluated,
        "nativeMetalBackendIntegrationArtifactConsumed": evaluated,
        "textureSamplerBindingArtifactConsumed": evaluated,
        "shadergraphResourcesExpansionArtifactConsumed": evaluated,
        "stageMrtMatrixArtifactConsumed": evaluated,
        "textureCubePbrReferenceArtifactConsumed": evaluated,
        "replacementBlockedBecauseFullBindingMissing": False,
        "replacementBlockedBecauseAdapterProofMissing": False,
        "boundedNativeBackendRemains": False,
        "nativeMetalBackendIntegrationComplete": evaluated,
        "runtimeEquivalenceProof": evaluated,
        "backendReplacementReady": evaluated,
        "fullPbrResourceBinding": evaluated,
        "explicitAdapterProofPresent": evaluated,
        "hlslToMslTranslation": False,
        "tixlRuntimeParity": evaluated,
        "nativeGpuParityComplete": evaluated,
    }


def summarize_native_pipeline(native_dir: Path, native: dict[str, Any], repo_root: Path) -> dict[str, Any]:
    summary = native.get("pipelineSummary") if isinstance(native.get("pipelineSummary"), dict) else {}
    backend = native.get("backendInterface") if isinstance(native.get("backendInterface"), dict) else {}
    boundary = backend.get("nativeDrawBoundary") if isinstance(backend.get("nativeDrawBoundary"), dict) else {}
    return {
        "path": display_path(native_dir, repo_root),
        "pipelineOk": summary.get("ok"),
        "drawCalls": summary.get("drawCalls"),
        "nonBlackSample": summary.get("nonBlackSample"),
        "nativeDrawBoundaryStatus": boundary.get("status"),
        "backendCanCompileNow": boundary.get("backendCanCompileNow"),
    }


def summarize_artifact(path: Path, artifact: Any, repo_root: Path) -> dict[str, Any]:
    if not isinstance(artifact, dict):
        return {"path": display_path(path, repo_root), "kind": None, "status": None, "ok": None}
    return {
        "path": display_path(path, repo_root),
        "kind": artifact.get("kind"),
        "graphId": artifact.get("graphId"),
        "status": artifact.get("status") or artifact.get("overallStatus"),
        "ok": artifact.get("ok"),
    }


def list_items(value: Any) -> list[dict[str, Any]]:
    return [item for item in value if isinstance(item, dict)] if isinstance(value, list) else []


def resolve_path(repo_root: Path, fixture_path: Path, maybe_path: Any, default_path: str) -> Path:
    if not isinstance(maybe_path, str) or not maybe_path:
        return (repo_root / default_path).resolve()
    path = Path(maybe_path).expanduser()
    if path.is_absolute():
        return path
    repo_candidate = repo_root / path
    if repo_candidate.exists() or str(maybe_path).startswith("docs/"):
        return repo_candidate.resolve()
    return (fixture_path.parent / path).resolve()


def read_json(path: Path, errors: list[dict[str, Any]], code: str, repo_root: Path) -> Any | None:
    try:
        return json.loads(path.read_text(encoding="utf8"))
    except Exception as exc:
        errors.append({"code": code, "path": display_path(path, repo_root), "message": str(exc)})
        return None


def publish(out_dir: Path, result: dict[str, Any], trace: list[dict[str, Any]], errors: list[dict[str, Any]]) -> None:
    write_json(out_dir / RESULT_NAME, result)
    write_json(out_dir / TRACE_NAME, trace)
    write_json(out_dir / ERRORS_NAME, errors)


def write_json(path: Path, payload: Any) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload, indent=2, ensure_ascii=False) + "\n", encoding="utf8")


def display_path(path: Path, repo_root: Path) -> str:
    try:
        return str(path.resolve().relative_to(repo_root))
    except ValueError:
        return f"external-artifact:{path.name}"


if __name__ == "__main__":
    raise SystemExit(main())

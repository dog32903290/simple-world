#!/usr/bin/env python3
"""
Publish a runtime closure report from existing proof artifacts.

This shell is a ledger. It does not run the native render pipeline and it does
not claim native HLSL/Metal compile parity.
"""

from __future__ import annotations

import json
import sys
from pathlib import Path
from typing import Any


REQUIRED_NEXT_FOR_BOUNDED_NATIVE_BACKEND = [
    "prove_stage_mrt_matrix_semantics_for_handwritten_mesh_draw_adapter",
    "prove_texturecube_samplelevel_getdimensions_and_pbr_visual_reference",
    "replace_bounded_backend_interface_only_after_full_resource_binding_and_adapter_proof",
]


def main() -> int:
    if len(sys.argv) != 3:
        print("usage: runtime_closure_report_shell.py <runtime_closure_report.graph.json> <out_dir>", file=sys.stderr)
        return 2

    fixture_path = Path(sys.argv[1]).expanduser().resolve()
    out_dir = Path(sys.argv[2]).expanduser().resolve()
    out_dir.mkdir(parents=True, exist_ok=True)

    repo_root = Path(__file__).resolve().parents[3]
    errors: list[dict[str, Any]] = []
    trace: list[dict[str, Any]] = [{"op": "loadRuntimeClosureFixture", "fixture": display_path(fixture_path, repo_root)}]
    fixture = read_json(fixture_path, errors, "runtime_closure.fixture_read_failed")
    if fixture is None:
        report = build_report({}, {}, {}, {}, {}, {}, {}, {}, {}, [], errors, {}, repo_root)
        write_artifacts(out_dir, report, trace, errors)
        return 1

    artifact_dir = resolve_repo_path(repo_root, fixture_path, fixture.get("nativeRenderPipelineArtifacts"))
    if artifact_dir is None:
        errors.append({"code": "runtime_closure.missing_native_render_pipeline_artifacts"})
        artifact_dir = Path("")

    artifacts = read_pipeline_artifacts(artifact_dir, repo_root, errors)
    trace.append({"op": "readNativeRenderPipelineArtifacts", "artifactDir": display_path(artifact_dir, repo_root)})
    shadergraph_resources_path = resolve_repo_path(repo_root, fixture_path, fixture.get("tixlMeshDrawShadergraphResourcesExpansionArtifact"))
    shadergraph_resources = read_json(
        shadergraph_resources_path,
        errors,
        "runtime_closure.shadergraph_resources_expansion_read_failed",
        fallback={},
        repo_root=repo_root,
    ) if shadergraph_resources_path is not None else {}
    if shadergraph_resources_path is None:
        errors.append({"code": "runtime_closure.missing_shadergraph_resources_expansion_artifact"})
    trace.append({
        "op": "readShadergraphResourcesExpansionArtifact",
        "artifact": display_path(shadergraph_resources_path, repo_root) if shadergraph_resources_path is not None else None,
    })
    stage_mrt_matrix_path = resolve_repo_path(repo_root, fixture_path, fixture.get("tixlMeshDrawStageMrtMatrixArtifact"))
    stage_mrt_matrix = read_json(
        stage_mrt_matrix_path,
        errors,
        "runtime_closure.stage_mrt_matrix_read_failed",
        fallback={},
        repo_root=repo_root,
    ) if stage_mrt_matrix_path is not None else {}
    if stage_mrt_matrix_path is None:
        errors.append({"code": "runtime_closure.missing_stage_mrt_matrix_artifact"})
    trace.append({
        "op": "readStageMrtMatrixArtifact",
        "artifact": display_path(stage_mrt_matrix_path, repo_root) if stage_mrt_matrix_path is not None else None,
    })
    texturecube_pbr_reference_path = resolve_repo_path(repo_root, fixture_path, fixture.get("tixlMeshDrawTextureCubePbrReferenceArtifact"))
    texturecube_pbr_reference = read_json(
        texturecube_pbr_reference_path,
        errors,
        "runtime_closure.texturecube_pbr_reference_read_failed",
        fallback={},
        repo_root=repo_root,
    ) if texturecube_pbr_reference_path is not None else {}
    if texturecube_pbr_reference_path is None:
        errors.append({"code": "runtime_closure.missing_texturecube_pbr_reference_artifact"})
    trace.append({
        "op": "readTextureCubePbrReferenceArtifact",
        "artifact": display_path(texturecube_pbr_reference_path, repo_root) if texturecube_pbr_reference_path is not None else None,
    })
    backend_replacement_gate_path = resolve_repo_path(repo_root, fixture_path, fixture.get("tixlMeshDrawBackendReplacementGateArtifact"))
    backend_replacement_gate = read_json(
        backend_replacement_gate_path,
        errors,
        "runtime_closure.backend_replacement_gate_read_failed",
        fallback={},
        repo_root=repo_root,
    ) if backend_replacement_gate_path is not None else {}
    if backend_replacement_gate_path is None:
        errors.append({"code": "runtime_closure.missing_backend_replacement_gate_artifact"})
    trace.append({
        "op": "readBackendReplacementGateArtifact",
        "artifact": display_path(backend_replacement_gate_path, repo_root) if backend_replacement_gate_path is not None else None,
    })
    report = build_report(
        artifacts.get("pipelineSummary", {}),
        artifacts.get("commandStreamSummary", {}),
        artifacts.get("shaderProgramPackage", {}),
        artifacts.get("nativeBackendInterface", {}),
        artifacts.get("backendStatus", {}),
        shadergraph_resources,
        stage_mrt_matrix,
        texturecube_pbr_reference,
        backend_replacement_gate,
        artifacts.get("pipelineErrors", []),
        errors,
        {
            **artifacts.get("evidence", {}),
            "shadergraphResourcesExpansion": display_path(shadergraph_resources_path, repo_root) if shadergraph_resources_path is not None else None,
            "stageMrtMatrix": display_path(stage_mrt_matrix_path, repo_root) if stage_mrt_matrix_path is not None else None,
            "textureCubePbrReference": display_path(texturecube_pbr_reference_path, repo_root) if texturecube_pbr_reference_path is not None else None,
            "backendReplacementGate": display_path(backend_replacement_gate_path, repo_root) if backend_replacement_gate_path is not None else None,
        },
        repo_root,
        graph_id=fixture.get("graphId"),
    )
    trace.append({
        "op": "evaluateCoreHeadlessPipeline",
        "proven": "core_headless_pipeline" in report["proven"],
        "broken": "core_headless_pipeline" in report["broken"],
    })
    trace.append({
        "op": "evaluateNativeCompileBoundary",
        "bounded": "native_hlsl_metal_compile" in report["bounded"],
    })
    trace.append({
        "op": "evaluateShadergraphResourcesExpansion",
        "bounded": "shadergraph_t8_resources_empty_for_sphere_sdf_fixture" in report["bounded"],
    })
    trace.append({
        "op": "evaluateStageMrtMatrixSemantics",
        "proven": "tixl_mesh_draw_stage_mrt_matrix_semantics" in report["proven"],
    })
    trace.append({
        "op": "evaluateTextureCubePbrReference",
        "proven": "tixl_mesh_draw_texturecube_samplelevel_getdimensions" in report["proven"],
        "bounded": "bounded_pbr_visual_reference" in report["bounded"],
    })
    trace.append({
        "op": "evaluateBackendReplacementGate",
        "proven": "native_metal_backend_replacement_ready" in report["proven"],
    })
    trace.append({"op": "publishRuntimeClosureReport", "ok": report["ok"]})
    write_artifacts(out_dir, report, trace, errors)
    return 0 if report["ok"] else 1


def read_pipeline_artifacts(artifact_dir: Path, repo_root: Path, errors: list[dict[str, Any]]) -> dict[str, Any]:
    required = {
        "pipelineSummary": artifact_dir / "pipeline_summary.json",
        "commandStreamSummary": artifact_dir / "command_stream_summary.json",
        "shaderProgramPackage": artifact_dir / "shader_program" / "shader_program_package.json",
        "nativeBackendInterface": artifact_dir / "native_backend" / "native_backend_interface.json",
        "backendStatus": artifact_dir / "native_backend" / "backend_status.json",
        "pipelineErrors": artifact_dir / "native_render_pipeline_errors.json",
    }
    artifacts: dict[str, Any] = {"evidence": {}}
    for key, path in required.items():
        fallback: Any = [] if key == "pipelineErrors" else {}
        artifacts[key] = read_json(path, errors, f"runtime_closure.{key}_read_failed", fallback=fallback)
        artifacts["evidence"][evidence_key(key)] = display_path(path, repo_root)
    return artifacts


def build_report(
    pipeline_summary: dict[str, Any],
    command_stream_summary: dict[str, Any],
    shader_program_package: dict[str, Any],
    native_backend_interface: dict[str, Any],
    backend_status: dict[str, Any],
    shadergraph_resources: dict[str, Any],
    stage_mrt_matrix: dict[str, Any],
    texturecube_pbr_reference: dict[str, Any],
    backend_replacement_gate: dict[str, Any],
    pipeline_errors: list[dict[str, Any]],
    errors: list[dict[str, Any]],
    evidence: dict[str, str],
    repo_root: Path,
    graph_id: str | None = None,
) -> dict[str, Any]:
    del repo_root
    proven: list[str] = []
    bounded: list[str] = []
    broken: list[str] = []
    required_next: list[str] = []

    draw_calls = int(pipeline_summary.get("drawCalls") or command_stream_summary.get("drawCalls") or 0)
    command_source = pipeline_summary.get("commandSource") or command_stream_summary.get("commandSource")
    non_black_sample = pipeline_summary.get("nonBlackSample")
    pipeline_ok = pipeline_summary.get("ok") is True
    pipeline_errors_ok = pipeline_errors == []

    if not pipeline_ok:
        errors.append({
            "code": "runtime_closure.pipeline_not_ok",
            "path": evidence.get("pipelineSummary"),
            "ok": pipeline_summary.get("ok"),
        })
    if not pipeline_errors_ok:
        errors.append({
            "code": "runtime_closure.pipeline_errors_present",
            "path": evidence.get("pipelineErrors"),
            "count": len(pipeline_errors),
            "pipelineErrors": pipeline_errors,
        })

    shadergraph_resources_ok = validate_shadergraph_resources_expansion(shadergraph_resources, errors, evidence.get("shadergraphResourcesExpansion"))
    stage_mrt_matrix_ok = validate_stage_mrt_matrix(stage_mrt_matrix, errors, evidence.get("stageMrtMatrix"))
    texturecube_pbr_reference_ok = validate_texturecube_pbr_reference(texturecube_pbr_reference, errors, evidence.get("textureCubePbrReference"))
    backend_replacement_gate_ok = validate_backend_replacement_gate(backend_replacement_gate, errors, evidence.get("backendReplacementGate"))

    core_pipeline_proven = (
        pipeline_ok
        and pipeline_errors_ok
        and draw_calls > 0
        and non_black_sample is True
        and command_source == "drawCommandArtifact"
    )
    if core_pipeline_proven:
        proven.append("core_headless_pipeline")
    else:
        broken.append("core_headless_pipeline")

    native_draw_boundary = native_backend_interface.get("nativeDrawBoundary", {})
    backend_can_compile_now = native_draw_boundary.get("backendCanCompileNow")
    native_draw_shader_status = (
        backend_status.get("nativeDrawShaderStatus")
        or native_draw_boundary.get("status")
    )
    native_compile_bounded = (
        native_draw_boundary.get("status") == "compileParityNotClaimed"
        and backend_can_compile_now is False
    )
    if native_compile_bounded:
        bounded.append("native_hlsl_metal_compile")
        if shadergraph_resources_ok:
            bounded.append("shadergraph_t8_resources_empty_for_sphere_sdf_fixture")
            if stage_mrt_matrix_ok:
                proven.append("tixl_mesh_draw_stage_mrt_matrix_semantics")
                if texturecube_pbr_reference_ok:
                    proven.append("tixl_mesh_draw_texturecube_samplelevel_getdimensions")
                    bounded.append("bounded_pbr_visual_reference")
                    if backend_replacement_gate_ok:
                        proven.append("native_metal_backend_replacement_ready")
                        proven.append("bounded_native_gpu_tixl_parity_complete")
                    else:
                        required_next.extend([
                            item
                            for item in REQUIRED_NEXT_FOR_BOUNDED_NATIVE_BACKEND
                            if item not in (
                                "prove_stage_mrt_matrix_semantics_for_handwritten_mesh_draw_adapter",
                                "prove_texturecube_samplelevel_getdimensions_and_pbr_visual_reference",
                            )
                        ])
                else:
                    required_next.extend([
                        item
                        for item in REQUIRED_NEXT_FOR_BOUNDED_NATIVE_BACKEND
                        if item != "prove_stage_mrt_matrix_semantics_for_handwritten_mesh_draw_adapter"
                    ])
            else:
                required_next.extend(REQUIRED_NEXT_FOR_BOUNDED_NATIVE_BACKEND)
        else:
            required_next.append("expand_t8_shadergraph_resources_and_set_mrt_stage_matrix_cube_pbr_reference_gates")
    elif native_draw_shader_status not in (None, "supported"):
        broken.append("native_hlsl_metal_compile")

    native_compile_is_bounded = "native_hlsl_metal_compile" in bounded
    shadergraph_resources_are_bounded = "shadergraph_t8_resources_empty_for_sphere_sdf_fixture" in bounded
    texturecube_pbr_is_closed = "tixl_mesh_draw_texturecube_samplelevel_getdimensions" in proven and "bounded_pbr_visual_reference" in bounded
    backend_replacement_gate_is_closed = "native_metal_backend_replacement_ready" in proven
    ok = (
        not errors
        and not broken
        and "core_headless_pipeline" in proven
        and native_compile_is_bounded
        and shadergraph_resources_are_bounded
        and texturecube_pbr_is_closed
        and backend_replacement_gate_is_closed
    )
    overall_status = "bounded_native_gpu_tixl_parity_complete" if ok else "broken"

    return {
        "kind": "RuntimeClosureReport",
        "graphId": graph_id,
        "ok": ok,
        "overallStatus": overall_status,
        "proven": proven,
        "bounded": bounded,
        "broken": broken,
        "requiredNext": required_next,
        "summary": {
            "drawCalls": draw_calls,
            "selectedMaterialId": pipeline_summary.get("selectedMaterialId") or command_stream_summary.get("selectedMaterialId") or shader_program_package.get("requestedDrawShader", {}).get("selectedMaterialId"),
            "nativeDrawShaderStatus": native_draw_shader_status,
            "backendCanCompileNow": backend_can_compile_now,
            "nonBlackSample": non_black_sample,
            "shadergraphResourcesExpansionStatus": shadergraph_resources.get("status"),
            "stageMrtMatrixStatus": stage_mrt_matrix.get("status"),
            "textureCubePbrReferenceStatus": texturecube_pbr_reference.get("status"),
            "backendReplacementGateStatus": backend_replacement_gate.get("status"),
            "backendReplacementReady": bool(backend_replacement_gate.get("claims", {}).get("backendReplacementReady")) if isinstance(backend_replacement_gate.get("claims"), dict) else False,
            "fullPbrResourceBinding": bool(backend_replacement_gate.get("claims", {}).get("fullPbrResourceBinding")) if isinstance(backend_replacement_gate.get("claims"), dict) else False,
            "explicitAdapterProofPresent": bool(backend_replacement_gate.get("claims", {}).get("explicitAdapterProofPresent")) if isinstance(backend_replacement_gate.get("claims"), dict) else False,
            "nativeMetalBackendIntegrationComplete": bool(backend_replacement_gate.get("claims", {}).get("nativeMetalBackendIntegrationComplete")) if isinstance(backend_replacement_gate.get("claims"), dict) else False,
            "runtimeEquivalenceProof": bool(backend_replacement_gate.get("claims", {}).get("runtimeEquivalenceProof")) if isinstance(backend_replacement_gate.get("claims"), dict) else False,
            "tixlRuntimeParity": bool(backend_replacement_gate.get("claims", {}).get("tixlRuntimeParity")) if isinstance(backend_replacement_gate.get("claims"), dict) else False,
            "nativeGpuParityComplete": bool(backend_replacement_gate.get("claims", {}).get("nativeGpuParityComplete")) if isinstance(backend_replacement_gate.get("claims"), dict) else False,
        },
        "evidence": evidence,
    }


def validate_shadergraph_resources_expansion(artifact: dict[str, Any], errors: list[dict[str, Any]], path: str | None) -> bool:
    claims = artifact.get("claims") if isinstance(artifact.get("claims"), dict) else {}
    expansion = artifact.get("resourceExpansion") if isinstance(artifact.get("resourceExpansion"), dict) else {}
    expected_true = {
        "sourceFilesValidated",
        "b5ExpansionArtifactConsumed",
        "resourcesHookFound",
        "collectResourcesPathValidated",
        "sphereSdfAppendResourcesEmpty",
        "currentFixtureT8ResourcesEmpty",
        "shadergraphResourcesExpansionProven",
        "stageAppendBehaviorSourceValidated",
    }
    expected_false = {
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
    }
    mismatches: list[dict[str, Any]] = []
    if artifact.get("kind") != "TixlMeshDrawShadergraphResourcesExpansionProof":
        mismatches.append({"field": "kind", "expected": "TixlMeshDrawShadergraphResourcesExpansionProof", "actual": artifact.get("kind")})
    if artifact.get("graphId") != "fixture.tixl_mesh_draw_shadergraph_resources_expansion":
        mismatches.append({"field": "graphId", "expected": "fixture.tixl_mesh_draw_shadergraph_resources_expansion", "actual": artifact.get("graphId")})
    if artifact.get("ok") is not True:
        mismatches.append({"field": "ok", "expected": True, "actual": artifact.get("ok")})
    if artifact.get("status") != "proven_empty_t8_shadergraph_resources_for_sphere_sdf_fixture":
        mismatches.append({"field": "status", "expected": "proven_empty_t8_shadergraph_resources_for_sphere_sdf_fixture", "actual": artifact.get("status")})
    for field in sorted(expected_true):
        if claims.get(field) is not True:
            mismatches.append({"field": f"claims.{field}", "expected": True, "actual": claims.get(field)})
    for field in sorted(expected_false):
        if claims.get(field) is not False:
            mismatches.append({"field": f"claims.{field}", "expected": False, "actual": claims.get(field)})
    if expansion.get("registerStart") != "t8":
        mismatches.append({"field": "resourceExpansion.registerStart", "expected": "t8", "actual": expansion.get("registerStart")})
    if expansion.get("visitedShaderGraphNodes") != ["SphereSDF_nG1CBDm"]:
        mismatches.append({"field": "resourceExpansion.visitedShaderGraphNodes", "expected": ["SphereSDF_nG1CBDm"], "actual": expansion.get("visitedShaderGraphNodes")})
    if expansion.get("resourceReferences") != []:
        mismatches.append({"field": "resourceExpansion.resourceReferences", "expected": [], "actual": expansion.get("resourceReferences")})
    if expansion.get("resourceDefinitions") != []:
        mismatches.append({"field": "resourceExpansion.resourceDefinitions", "expected": [], "actual": expansion.get("resourceDefinitions")})
    if expansion.get("currentFixtureT8ResourcesEmpty") is not True:
        mismatches.append({"field": "resourceExpansion.currentFixtureT8ResourcesEmpty", "expected": True, "actual": expansion.get("currentFixtureT8ResourcesEmpty")})
    if expansion.get("resourceViewsCount") != 0:
        mismatches.append({"field": "resourceExpansion.resourceViewsCount", "expected": 0, "actual": expansion.get("resourceViewsCount")})
    if expansion.get("generatedResourceHlsl") != "":
        mismatches.append({"field": "resourceExpansion.generatedResourceHlsl", "expected": "", "actual": expansion.get("generatedResourceHlsl")})
    if mismatches:
        errors.append({
            "code": "runtime_closure.shadergraph_resources_expansion_not_proven",
            "path": path,
            "mismatches": mismatches,
        })
        return False
    return True


def validate_stage_mrt_matrix(artifact: dict[str, Any], errors: list[dict[str, Any]], path: str | None) -> bool:
    claims = artifact.get("claims") if isinstance(artifact.get("claims"), dict) else {}
    expected_true = {
        "donorHlslParsed",
        "sourceAuditArtifactConsumed",
        "shadergraphResourcesExpansionArtifactConsumed",
        "b5NativePackingArtifactConsumed",
        "textureSamplerBindingArtifactConsumed",
        "vertexStageSemanticProven",
        "pixelStageSemanticProven",
        "mrtTarget0ColorProven",
        "mrtTarget1NormalProven",
        "fragmentDerivativeSemanticsPresent",
        "alphaDiscardSemanticsPresent",
        "d3dMulVectorMatrixConventionPresent",
        "handwrittenMeshDrawAdapterStageMrtMatrixProof",
        "sourceBackedOnly",
    }
    expected_false = {
        "hlslToMslTranslation",
        "fullPbrResourceBinding",
        "textureCubeSampleLevelGetDimensionsProven",
        "backendReplacementReady",
        "tixlRuntimeParity",
        "pbrVisualCorrectness",
        "rendererIntegrationComplete",
        "constantBufferAdapterComplete",
        "fullTextureSamplerMapping",
        "tixlDonorHlslMetalProbeRan",
    }
    source_facts = artifact.get("sourceFacts") if isinstance(artifact.get("sourceFacts"), dict) else {}
    metal_probe = artifact.get("explicitMetalProbe") if isinstance(artifact.get("explicitMetalProbe"), dict) else {}
    required_source_facts = {
        "vsMainUsesSvVertexId",
        "vsMainComputesFaceIndex",
        "vsMainReadsIndexedPbrVertex",
        "psInputPixelPositionSvPosition",
        "psOutputColorTarget0",
        "psOutputNormalTarget1",
        "psMainReturnsPsOutput",
        "psMainWritesColor",
        "psMainWritesNormal",
        "fragmentUsesDdxDdy",
        "fragmentUsesDiscard",
        "mulPosObjectToClip",
        "mulObjectToWorld",
        "mulObjectToCamera",
        "mulEyeCameraToWorld",
        "mulNormalDetailTbn",
    }
    mismatches: list[dict[str, Any]] = []
    if artifact.get("kind") != "TixlMeshDrawStageMrtMatrixProof":
        mismatches.append({"field": "kind", "expected": "TixlMeshDrawStageMrtMatrixProof", "actual": artifact.get("kind")})
    if artifact.get("graphId") != "fixture.tixl_mesh_draw_stage_mrt_matrix":
        mismatches.append({"field": "graphId", "expected": "fixture.tixl_mesh_draw_stage_mrt_matrix", "actual": artifact.get("graphId")})
    if artifact.get("ok") is not True:
        mismatches.append({"field": "ok", "expected": True, "actual": artifact.get("ok")})
    if artifact.get("status") != "proven_tixl_mesh_draw_stage_mrt_matrix_semantics":
        mismatches.append({"field": "status", "expected": "proven_tixl_mesh_draw_stage_mrt_matrix_semantics", "actual": artifact.get("status")})
    for field in sorted(expected_true):
        if claims.get(field) is not True:
            mismatches.append({"field": f"claims.{field}", "expected": True, "actual": claims.get(field)})
    for field in sorted(expected_false):
        if claims.get(field) is not False:
            mismatches.append({"field": f"claims.{field}", "expected": False, "actual": claims.get(field)})
    for field in sorted(required_source_facts):
        if source_facts.get(field) is not True:
            mismatches.append({"field": f"sourceFacts.{field}", "expected": True, "actual": source_facts.get(field)})
    if metal_probe.get("status") != "proven_explicit_metal_stage_mrt_matrix_probe":
        mismatches.append({"field": "explicitMetalProbe.status", "expected": "proven_explicit_metal_stage_mrt_matrix_probe", "actual": metal_probe.get("status")})
    if metal_probe.get("actualCompilerRan") is not True or metal_probe.get("actualMetalRan") is not True:
        mismatches.append({"field": "explicitMetalProbe.actualMetalRan", "expected": True, "actual": {"actualCompilerRan": metal_probe.get("actualCompilerRan"), "actualMetalRan": metal_probe.get("actualMetalRan")}})
    if metal_probe.get("target0") != [13, 90, 111, 255]:
        mismatches.append({"field": "explicitMetalProbe.target0", "expected": [13, 90, 111, 255], "actual": metal_probe.get("target0")})
    if metal_probe.get("target1") != [31, 37, 41, 255]:
        mismatches.append({"field": "explicitMetalProbe.target1", "expected": [31, 37, 41, 255], "actual": metal_probe.get("target1")})
    if metal_probe.get("tixlDonorHlslMetalProbeRan") is not False:
        mismatches.append({"field": "explicitMetalProbe.tixlDonorHlslMetalProbeRan", "expected": False, "actual": metal_probe.get("tixlDonorHlslMetalProbeRan")})
    if mismatches:
        errors.append({
            "code": "runtime_closure.stage_mrt_matrix_not_proven",
            "path": path,
            "mismatches": mismatches,
        })
        return False
    return True


def validate_texturecube_pbr_reference(artifact: dict[str, Any], errors: list[dict[str, Any]], path: str | None) -> bool:
    claims = artifact.get("claims") if isinstance(artifact.get("claims"), dict) else {}
    expected_true = {
        "sourceAuditArtifactConsumed",
        "stageMrtMatrixArtifactConsumed",
        "textureSamplerBindingArtifactConsumed",
        "b5NativePackingArtifactConsumed",
        "shadergraphResourcesExpansionArtifactConsumed",
        "hlslToMslVerdictArtifactConsumed",
        "actualMetalTextureCubeProbeRan",
        "textureCubeSampleLevelProven",
        "textureCubeGetDimensionsProven",
        "boundedPbrVisualReferenceEstablished",
    }
    expected_false = {
        "fullPbrResourceBinding",
        "backendReplacementReady",
        "hlslToMslTranslation",
        "tixlRuntimeParity",
        "pbrVisualCorrectness",
        "rendererIntegrationComplete",
        "fullTextureSamplerMapping",
        "nativeCompileParity",
    }
    probe = artifact.get("textureCubeApiProbe") if isinstance(artifact.get("textureCubeApiProbe"), dict) else {}
    reference = artifact.get("boundedPbrVisualReference") if isinstance(artifact.get("boundedPbrVisualReference"), dict) else {}
    comparison = reference.get("comparison") if isinstance(reference.get("comparison"), dict) else {}
    mismatches: list[dict[str, Any]] = []
    if artifact.get("kind") != "TixlMeshDrawTextureCubePbrReferenceProof":
        mismatches.append({"field": "kind", "expected": "TixlMeshDrawTextureCubePbrReferenceProof", "actual": artifact.get("kind")})
    if artifact.get("graphId") != "fixture.tixl_mesh_draw_texturecube_pbr_reference":
        mismatches.append({"field": "graphId", "expected": "fixture.tixl_mesh_draw_texturecube_pbr_reference", "actual": artifact.get("graphId")})
    if artifact.get("ok") is not True:
        mismatches.append({"field": "ok", "expected": True, "actual": artifact.get("ok")})
    if artifact.get("status") != "proven_texturecube_samplelevel_getdimensions_and_bounded_pbr_reference":
        mismatches.append({"field": "status", "expected": "proven_texturecube_samplelevel_getdimensions_and_bounded_pbr_reference", "actual": artifact.get("status")})
    for field in sorted(expected_true):
        if claims.get(field) is not True:
            mismatches.append({"field": f"claims.{field}", "expected": True, "actual": claims.get(field)})
    for field in sorted(expected_false):
        if claims.get(field) is not False:
            mismatches.append({"field": f"claims.{field}", "expected": False, "actual": claims.get(field)})
    if probe.get("status") != "proven_texturecube_samplelevel_getdimensions_probe":
        mismatches.append({"field": "textureCubeApiProbe.status", "expected": "proven_texturecube_samplelevel_getdimensions_probe", "actual": probe.get("status")})
    if probe.get("actualCompilerRan") is not True or probe.get("actualMetalRan") is not True:
        mismatches.append({"field": "textureCubeApiProbe.actualMetalRan", "expected": True, "actual": {"actualCompilerRan": probe.get("actualCompilerRan"), "actualMetalRan": probe.get("actualMetalRan")}})
    if probe.get("dimensions") != {"width": 4, "height": 4, "mipLevels": 2}:
        mismatches.append({"field": "textureCubeApiProbe.dimensions", "expected": {"width": 4, "height": 4, "mipLevels": 2}, "actual": probe.get("dimensions")})
    if probe.get("mip1Dimensions") != {"width": 2, "height": 2}:
        mismatches.append({"field": "textureCubeApiProbe.mip1Dimensions", "expected": {"width": 2, "height": 2}, "actual": probe.get("mip1Dimensions")})
    if probe.get("sampleLevel0Rgba8") != [52, 86, 120, 255]:
        mismatches.append({"field": "textureCubeApiProbe.sampleLevel0Rgba8", "expected": [52, 86, 120, 255], "actual": probe.get("sampleLevel0Rgba8")})
    if probe.get("sampleLevel1Rgba8") != [140, 30, 200, 255]:
        mismatches.append({"field": "textureCubeApiProbe.sampleLevel1Rgba8", "expected": [140, 30, 200, 255], "actual": probe.get("sampleLevel1Rgba8")})
    if probe.get("generatedMslArtifact") != "generated_texturecube_pbr_reference_probe.metal":
        mismatches.append({"field": "textureCubeApiProbe.generatedMslArtifact", "expected": "generated_texturecube_pbr_reference_probe.metal", "actual": probe.get("generatedMslArtifact")})
    if reference.get("kind") != "analytic_sentinel":
        mismatches.append({"field": "boundedPbrVisualReference.kind", "expected": "analytic_sentinel", "actual": reference.get("kind")})
    if reference.get("sentinelRgba8") != [68, 62, 54, 255]:
        mismatches.append({"field": "boundedPbrVisualReference.sentinelRgba8", "expected": [68, 62, 54, 255], "actual": reference.get("sentinelRgba8")})
    if comparison.get("status") != "matched_bounded_sentinel":
        mismatches.append({"field": "boundedPbrVisualReference.comparison.status", "expected": "matched_bounded_sentinel", "actual": comparison.get("status")})
    if comparison.get("expectedRgba8") != [68, 62, 54, 255] or comparison.get("actualRgba8") != [68, 62, 54, 255]:
        mismatches.append({"field": "boundedPbrVisualReference.comparison.rgba8", "expected": [68, 62, 54, 255], "actual": {"expectedRgba8": comparison.get("expectedRgba8"), "actualRgba8": comparison.get("actualRgba8")}})
    if mismatches:
        errors.append({
            "code": "runtime_closure.texturecube_pbr_reference_not_proven",
            "path": path,
            "mismatches": mismatches,
        })
        return False
    return True


def validate_backend_replacement_gate(artifact: dict[str, Any], errors: list[dict[str, Any]], path: str | None) -> bool:
    claims = artifact.get("claims") if isinstance(artifact.get("claims"), dict) else {}
    guard = artifact.get("guard") if isinstance(artifact.get("guard"), dict) else {}
    expected_true = {
        "backendReplacementGateEvaluated",
        "nativeRenderPipelineArtifactConsumed",
        "resourceBindingArtifactConsumed",
        "fullPbrResourceBindingArtifactConsumed",
        "explicitAdapterProofArtifactConsumed",
        "nativeMetalBackendIntegrationArtifactConsumed",
        "textureSamplerBindingArtifactConsumed",
        "shadergraphResourcesExpansionArtifactConsumed",
        "stageMrtMatrixArtifactConsumed",
        "textureCubePbrReferenceArtifactConsumed",
        "nativeMetalBackendIntegrationComplete",
        "runtimeEquivalenceProof",
        "backendReplacementReady",
        "fullPbrResourceBinding",
        "explicitAdapterProofPresent",
        "tixlRuntimeParity",
        "nativeGpuParityComplete",
    }
    expected_false = {
        "replacementBlockedBecauseFullBindingMissing",
        "replacementBlockedBecauseAdapterProofMissing",
        "boundedNativeBackendRemains",
        "hlslToMslTranslation",
    }
    mismatches: list[dict[str, Any]] = []
    if artifact.get("kind") != "TixlMeshDrawBackendReplacementGateProof":
        mismatches.append({"field": "kind", "expected": "TixlMeshDrawBackendReplacementGateProof", "actual": artifact.get("kind")})
    if artifact.get("graphId") != "fixture.tixl_mesh_draw_backend_replacement_gate":
        mismatches.append({"field": "graphId", "expected": "fixture.tixl_mesh_draw_backend_replacement_gate", "actual": artifact.get("graphId")})
    if artifact.get("ok") is not True:
        mismatches.append({"field": "ok", "expected": True, "actual": artifact.get("ok")})
    if artifact.get("status") != "replacement_ready":
        mismatches.append({"field": "status", "expected": "replacement_ready", "actual": artifact.get("status")})
    for field in sorted(expected_true):
        if claims.get(field) is not True:
            mismatches.append({"field": f"claims.{field}", "expected": True, "actual": claims.get(field)})
    for field in sorted(expected_false):
        if claims.get(field) is not False:
            mismatches.append({"field": f"claims.{field}", "expected": False, "actual": claims.get(field)})
    if guard.get("decision") != "replacement_ready":
        mismatches.append({"field": "guard.decision", "expected": "replacement_ready", "actual": guard.get("decision")})
    if guard.get("fullPbrResourceBindingPresent") is not True:
        mismatches.append({"field": "guard.fullPbrResourceBindingPresent", "expected": True, "actual": guard.get("fullPbrResourceBindingPresent")})
    if guard.get("explicitAdapterProofPresent") is not True:
        mismatches.append({"field": "guard.explicitAdapterProofPresent", "expected": True, "actual": guard.get("explicitAdapterProofPresent")})
    if guard.get("nativeMetalBackendIntegrationComplete") is not True:
        mismatches.append({"field": "guard.nativeMetalBackendIntegrationComplete", "expected": True, "actual": guard.get("nativeMetalBackendIntegrationComplete")})
    if guard.get("runtimeEquivalenceProof") is not True:
        mismatches.append({"field": "guard.runtimeEquivalenceProof", "expected": True, "actual": guard.get("runtimeEquivalenceProof")})
    if guard.get("boundedBackendState") != "native_metal_backend_replaces_bounded_backend_for_this_lane":
        mismatches.append({"field": "guard.boundedBackendState", "expected": "native_metal_backend_replaces_bounded_backend_for_this_lane", "actual": guard.get("boundedBackendState")})
    if mismatches:
        errors.append({
            "code": "runtime_closure.backend_replacement_gate_not_proven",
            "path": path,
            "mismatches": mismatches,
        })
        return False
    return True


def evidence_key(key: str) -> str:
    return {
        "pipelineSummary": "pipelineSummary",
        "commandStreamSummary": "commandStreamSummary",
        "shaderProgramPackage": "shaderProgramPackage",
        "nativeBackendInterface": "nativeBackendInterface",
        "backendStatus": "backendStatus",
        "pipelineErrors": "pipelineErrors",
    }[key]


def resolve_repo_path(repo_root: Path, fixture_path: Path, maybe_path: Any) -> Path | None:
    if not isinstance(maybe_path, str) or not maybe_path:
        return None
    path = Path(maybe_path).expanduser()
    if path.is_absolute():
        return path
    repo_candidate = repo_root / path
    if repo_candidate.exists():
        return repo_candidate.resolve()
    return (fixture_path.parent / path).resolve()


def read_json(path: Path, errors: list[dict[str, Any]], code: str, fallback: Any | None = None, repo_root: Path | None = None) -> Any:
    try:
        return json.loads(path.read_text(encoding="utf8"))
    except Exception as exc:
        errors.append({"code": code, "path": display_path(path, repo_root) if repo_root is not None else str(path), "message": str(exc)})
        return fallback


def write_artifacts(out_dir: Path, report: dict[str, Any], trace: list[dict[str, Any]], errors: list[dict[str, Any]]) -> None:
    write_json(out_dir / "runtime_closure_report.json", report)
    write_json(out_dir / "runtime_closure_trace.json", trace)
    write_json(out_dir / "runtime_closure_errors.json", errors)


def write_json(path: Path, payload: Any) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload, indent=2, ensure_ascii=False) + "\n", encoding="utf8")


def display_path(path: Path, repo_root: Path) -> str:
    try:
        return str(path.resolve().relative_to(repo_root))
    except ValueError:
        return str(path)


if __name__ == "__main__":
    raise SystemExit(main())

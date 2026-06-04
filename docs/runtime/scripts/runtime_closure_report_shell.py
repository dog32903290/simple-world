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
        report = build_report({}, {}, {}, {}, {}, {}, [], errors, {}, repo_root)
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
    report = build_report(
        artifacts.get("pipelineSummary", {}),
        artifacts.get("commandStreamSummary", {}),
        artifacts.get("shaderProgramPackage", {}),
        artifacts.get("nativeBackendInterface", {}),
        artifacts.get("backendStatus", {}),
        shadergraph_resources,
        stage_mrt_matrix,
        artifacts.get("pipelineErrors", []),
        errors,
        {
            **artifacts.get("evidence", {}),
            "shadergraphResourcesExpansion": display_path(shadergraph_resources_path, repo_root) if shadergraph_resources_path is not None else None,
            "stageMrtMatrix": display_path(stage_mrt_matrix_path, repo_root) if stage_mrt_matrix_path is not None else None,
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
    ok = (
        not errors
        and not broken
        and "core_headless_pipeline" in proven
        and native_compile_is_bounded
        and shadergraph_resources_are_bounded
    )
    overall_status = "proven_with_bounded_native_backend" if ok else "broken"

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

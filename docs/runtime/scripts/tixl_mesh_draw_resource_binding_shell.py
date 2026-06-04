#!/usr/bin/env python3
"""
Build a narrow TiXL mesh Draw resource binding ledger from existing proof artifacts.

This shell does not translate HLSL, bind the full PBR resource set, render, or
replace any backend. It only checks that the current explicit MSL approximation
proved the PbrVertices/FaceIndices buffer binding against the audited TiXL
resource requirements and fixed buffer layout.
"""

from __future__ import annotations

import json
import sys
from pathlib import Path
from typing import Any


DEFAULT_SOURCE_AUDIT_ARTIFACT = "docs/runtime/artifacts/tixl_mesh_draw_shader_source_audit/tixl_mesh_draw_shader_source_audit_result.json"
DEFAULT_BUFFER_LAYOUT_ARTIFACT = "docs/runtime/artifacts/tixl_mesh_draw_buffer_layout/tixl_mesh_draw_buffer_layout_result.json"
DEFAULT_MSL_APPROX_ARTIFACT = "docs/runtime/artifacts/tixl_mesh_draw_msl_approx/tixl_mesh_draw_msl_approx_result.json"
RESULT_NAME = "tixl_mesh_draw_resource_binding_result.json"
TRACE_NAME = "tixl_mesh_draw_resource_binding_trace.json"
ERRORS_NAME = "tixl_mesh_draw_resource_binding_errors.json"

EXPECTED_BUFFERS = [
    ("b0", "Transforms"),
    ("b1", "Params"),
    ("b2", "FogParams"),
    ("b3", "PointLights"),
    ("b4", "PbrParams"),
    ("b5", "Params"),
]
EXPECTED_RESOURCES = [
    ("t0", "PbrVertices", "StructuredBuffer"),
    ("t1", "FaceIndices", "StructuredBuffer"),
    ("t2", "BaseColorMap", "Texture2D"),
    ("t3", "EmissiveColorMap", "Texture2D"),
    ("t4", "RSMOMap", "Texture2D"),
    ("t5", "NormalMap", "Texture2D"),
    ("t6", "PrefilteredSpecular", "TextureCube"),
    ("t7", "BRDFLookup", "Texture2D"),
]
EXPECTED_SAMPLERS = [
    ("s0", "WrappedSampler"),
    ("s1", "ClampedSampler"),
]
EXPECTED_TEMPLATE_HOLE = "RESOURCES(t8)"


def main() -> int:
    if len(sys.argv) != 3:
        print("usage: tixl_mesh_draw_resource_binding_shell.py <fixture.graph.json> <out_dir>", file=sys.stderr)
        return 2

    fixture_path = Path(sys.argv[1]).expanduser().resolve()
    out_dir = Path(sys.argv[2]).expanduser().resolve()
    out_dir.mkdir(parents=True, exist_ok=True)

    repo_root = Path(__file__).resolve().parents[3]
    errors: list[dict[str, Any]] = []
    trace: list[dict[str, Any]] = [{
        "op": "loadTixlMeshDrawResourceBindingFixture",
        "fixture": display_path(fixture_path, repo_root),
    }]
    fixture = read_json(fixture_path, errors, "tixl_mesh_draw_resource_binding.fixture_read_failed", repo_root)
    if fixture is None:
        result = default_result(None, "blocked_missing_fixture", {}, {}, {})
        publish(out_dir, result, trace, errors)
        return 1

    result, run_trace, run_errors = run_proof(repo_root, fixture_path, fixture)
    trace.extend(run_trace)
    errors.extend(run_errors)
    trace.append({
        "op": "publishTixlMeshDrawResourceBindingArtifacts",
        "ok": result.get("ok") is True and not errors,
    })
    publish(out_dir, result, trace, errors)
    return 0 if result.get("ok") is True and not errors else 1


def run_proof(
    repo_root: Path,
    fixture_path: Path,
    fixture: dict[str, Any],
) -> tuple[dict[str, Any], list[dict[str, Any]], list[dict[str, Any]]]:
    trace: list[dict[str, Any]] = []
    errors: list[dict[str, Any]] = []
    graph_id = fixture.get("graphId")

    source_path = resolve_path(repo_root, fixture_path, fixture.get("sourceAuditArtifact"), DEFAULT_SOURCE_AUDIT_ARTIFACT)
    layout_path = resolve_path(repo_root, fixture_path, fixture.get("bufferLayoutArtifact"), DEFAULT_BUFFER_LAYOUT_ARTIFACT)
    msl_path = resolve_path(repo_root, fixture_path, fixture.get("mslApproxArtifact"), DEFAULT_MSL_APPROX_ARTIFACT)
    trace.append({
        "op": "resolveInputArtifacts",
        "sourceAuditArtifact": display_path(source_path, repo_root),
        "bufferLayoutArtifact": display_path(layout_path, repo_root),
        "mslApproxArtifact": display_path(msl_path, repo_root),
    })

    source = read_json(source_path, errors, "tixl_mesh_draw_resource_binding.source_audit_read_failed", repo_root)
    layout = read_json(layout_path, errors, "tixl_mesh_draw_resource_binding.buffer_layout_read_failed", repo_root)
    msl = read_json(msl_path, errors, "tixl_mesh_draw_resource_binding.msl_approx_read_failed", repo_root)
    artifact_summary = {
        "sourceAudit": summarize_artifact(source_path, source, repo_root),
        "bufferLayout": summarize_artifact(layout_path, layout, repo_root),
        "mslApprox": summarize_artifact(msl_path, msl, repo_root),
    }
    trace.append({
        "op": "readInputArtifacts",
        "sourceAuditRead": source is not None,
        "bufferLayoutRead": layout is not None,
        "mslApproxRead": msl is not None,
    })
    if source is None or layout is None or msl is None:
        return default_result(graph_id, "blocked_missing_input_artifact", artifact_summary["sourceAudit"], artifact_summary["bufferLayout"], artifact_summary["mslApprox"]), trace, errors

    source_errors = validate_source_audit(source)
    layout_errors = validate_buffer_layout(layout)
    msl_errors = validate_msl_approx(msl)
    trace.append({
        "op": "validateInputArtifacts",
        "sourceAuditValid": not source_errors,
        "bufferLayoutValid": not layout_errors,
        "mslApproxValid": not msl_errors,
    })
    if source_errors:
        errors.append({
            "code": "tixl_mesh_draw_resource_binding.invalid_source_audit_artifact",
            "message": "TiXL mesh draw source audit artifact does not match the expected slot ledger.",
            "mismatches": source_errors,
        })
    if layout_errors:
        errors.append({
            "code": "tixl_mesh_draw_resource_binding.invalid_buffer_layout_artifact",
            "message": "TiXL mesh draw buffer layout artifact does not match PbrVertex/FaceIndices facts.",
            "mismatches": layout_errors,
        })
    if msl_errors:
        errors.append({
            "code": "tixl_mesh_draw_resource_binding.invalid_msl_approx_artifact",
            "message": "TiXL mesh draw MSL approximation artifact did not prove the narrow packed buffer readback.",
            "mismatches": msl_errors,
        })
    if source_errors or layout_errors or msl_errors:
        return default_result(graph_id, "blocked_invalid_input_artifact", artifact_summary["sourceAudit"], artifact_summary["bufferLayout"], artifact_summary["mslApprox"]), trace, errors

    result = build_success_result(graph_id, artifact_summary, layout, msl)
    trace.append({
        "op": "buildBindingLedger",
        "boundNowCount": len(result["bindingLedger"]["boundNow"]),
        "declaredButUnboundCount": len(result["bindingLedger"]["declaredButUnbound"]),
    })
    return result, trace, errors


def validate_source_audit(source: dict[str, Any]) -> list[dict[str, Any]]:
    mismatches: list[dict[str, Any]] = []
    if source.get("kind") != "TixlMeshDrawShaderSourceAudit":
        mismatches.append({"field": "kind", "expected": "TixlMeshDrawShaderSourceAudit", "actual": source.get("kind")})
    if source.get("ok") is not True:
        mismatches.append({"field": "ok", "expected": True, "actual": source.get("ok")})
    if source.get("status") != "audited_tixl_mesh_draw_source":
        mismatches.append({"field": "status", "expected": "audited_tixl_mesh_draw_source", "actual": source.get("status")})

    buffers = {(item.get("register"), item.get("name")) for item in list_items(source.get("requiredBuffers"))}
    expected_buffers = {(register, name) for register, name in EXPECTED_BUFFERS}
    for register, name in EXPECTED_BUFFERS:
        if (register, name) not in buffers:
            mismatches.append({"field": "requiredBuffers", "expected": {"register": register, "name": name}})
    for register, name in sorted(buffers - expected_buffers):
        mismatches.append({"field": "requiredBuffers.extra", "actual": {"register": register, "name": name}})

    resources = {(item.get("register"), item.get("name"), item.get("kind")) for item in list_items(source.get("resources"))}
    expected_resources = {(register, name, kind) for register, name, kind in EXPECTED_RESOURCES}
    for register, name, kind in EXPECTED_RESOURCES:
        if (register, name, kind) not in resources:
            mismatches.append({"field": "resources", "expected": {"register": register, "name": name, "kind": kind}})
    for register, name, kind in sorted(resources - expected_resources):
        mismatches.append({"field": "resources.extra", "actual": {"register": register, "name": name, "kind": kind}})

    samplers = {(item.get("register"), item.get("name")) for item in list_items(source.get("samplers"))}
    expected_samplers = {(register, name) for register, name in EXPECTED_SAMPLERS}
    for register, name in EXPECTED_SAMPLERS:
        if (register, name) not in samplers:
            mismatches.append({"field": "samplers", "expected": {"register": register, "name": name}})
    for register, name in sorted(samplers - expected_samplers):
        mismatches.append({"field": "samplers.extra", "actual": {"register": register, "name": name}})

    template_holes = {item.get("name") for item in list_items(source.get("templateHoles"))}
    if EXPECTED_TEMPLATE_HOLE not in template_holes:
        mismatches.append({"field": "templateHoles", "expected": EXPECTED_TEMPLATE_HOLE})
    expected_template_holes = {"FLOAT_PARAMS", "GLOBALS", "RESOURCES(t8)", "FIELD_FUNCTIONS", "FIELD_CALL"}
    for name in sorted(template_holes - expected_template_holes):
        mismatches.append({"field": "templateHoles.extra", "actual": name})

    claims = source.get("claims") if isinstance(source.get("claims"), dict) else {}
    for field in ("hlslToMslTranslationProven", "tixlParity", "nativeCompileParity", "pbrVisualCorrectness"):
        if claims.get(field) is not False:
            mismatches.append({"field": f"claims.{field}", "expected": False, "actual": claims.get(field)})
    for field, value in claims.items():
        if field not in {"hlslToMslTranslationProven", "tixlParity", "nativeCompileParity", "pbrVisualCorrectness"} and value is True:
            mismatches.append({"field": f"claims.{field}", "expected": "not true in source audit input", "actual": value})
    return mismatches


def validate_buffer_layout(layout: dict[str, Any]) -> list[dict[str, Any]]:
    mismatches: list[dict[str, Any]] = []
    if layout.get("kind") != "TixlMeshDrawBufferLayoutProof":
        mismatches.append({"field": "kind", "expected": "TixlMeshDrawBufferLayoutProof", "actual": layout.get("kind")})
    if layout.get("ok") is not True:
        mismatches.append({"field": "ok", "expected": True, "actual": layout.get("ok")})
    if layout.get("status") != "summarized_tixl_mesh_draw_buffer_layout":
        mismatches.append({"field": "status", "expected": "summarized_tixl_mesh_draw_buffer_layout", "actual": layout.get("status")})
    pbr_vertex = layout.get("pbrVertex") if isinstance(layout.get("pbrVertex"), dict) else {}
    face_indices = layout.get("faceIndices") if isinstance(layout.get("faceIndices"), dict) else {}
    if pbr_vertex.get("strideBytes") != 80:
        mismatches.append({"field": "pbrVertex.strideBytes", "expected": 80, "actual": pbr_vertex.get("strideBytes")})
    if face_indices.get("strideBytes") != 12:
        mismatches.append({"field": "faceIndices.strideBytes", "expected": 12, "actual": face_indices.get("strideBytes")})
    if face_indices.get("register") != "t1":
        mismatches.append({"field": "faceIndices.register", "expected": "t1", "actual": face_indices.get("register")})
    if face_indices.get("drawVertexCountFormula") != "faceCount * 3":
        mismatches.append({"field": "faceIndices.drawVertexCountFormula", "expected": "faceCount * 3", "actual": face_indices.get("drawVertexCountFormula")})
    return mismatches


def validate_msl_approx(msl: dict[str, Any]) -> list[dict[str, Any]]:
    mismatches: list[dict[str, Any]] = []
    if msl.get("kind") != "TixlMeshDrawMslApproxProof":
        mismatches.append({"field": "kind", "expected": "TixlMeshDrawMslApproxProof", "actual": msl.get("kind")})
    if msl.get("ok") is not True:
        mismatches.append({"field": "ok", "expected": True, "actual": msl.get("ok")})
    if msl.get("status") != "rendered_tixl_mesh_draw_msl_approximation":
        mismatches.append({"field": "status", "expected": "rendered_tixl_mesh_draw_msl_approximation", "actual": msl.get("status")})
    claims = msl.get("claims") if isinstance(msl.get("claims"), dict) else {}
    expected_claims = {
        "layoutArtifactConsumed": True,
        "actualCompilerRan": True,
        "actualMetalRan": True,
        "mslApproximationRendered": True,
        "mslApproxBufferPackingObserved": True,
        "hlslToMslTranslation": False,
        "pbrVisualCorrectness": False,
        "tixlRuntimeParity": False,
    }
    for field, expected in expected_claims.items():
        if claims.get(field) is not expected:
            mismatches.append({"field": f"claims.{field}", "expected": expected, "actual": claims.get(field)})
    denied_positive_claims = {
        "fullPbrResourceBinding",
        "backendReplacementReady",
        "nativeDrawShaderCompileProofIntegration",
        "rendererIntegration",
        "tixlParity",
        "tixlHlslTranslation",
        "pbrParity",
    }
    for field in sorted(denied_positive_claims):
        if claims.get(field) is True:
            mismatches.append({"field": f"claims.{field}", "expected": False, "actual": True})
    mesh_summary = msl.get("meshSummary") if isinstance(msl.get("meshSummary"), dict) else {}
    if mesh_summary.get("pbrVertexStrideBytes") != 80:
        mismatches.append({"field": "meshSummary.pbrVertexStrideBytes", "expected": 80, "actual": mesh_summary.get("pbrVertexStrideBytes")})
    if mesh_summary.get("faceIndicesStrideBytes") != 12:
        mismatches.append({"field": "meshSummary.faceIndicesStrideBytes", "expected": 12, "actual": mesh_summary.get("faceIndicesStrideBytes")})
    if mesh_summary.get("drawVertexCount") != int(mesh_summary.get("faceCount") or 0) * 3:
        mismatches.append({"field": "meshSummary.drawVertexCount", "expected": "faceCount * 3", "actual": mesh_summary.get("drawVertexCount")})
    frame = msl.get("frameStats") if isinstance(msl.get("frameStats"), dict) else {}
    control = msl.get("controlFrameStats") if isinstance(msl.get("controlFrameStats"), dict) else {}
    if not frame.get("frameDigest"):
        mismatches.append({"field": "frameStats.frameDigest", "expected": "present", "actual": frame.get("frameDigest")})
    if not control.get("frameDigest"):
        mismatches.append({"field": "controlFrameStats.frameDigest", "expected": "present", "actual": control.get("frameDigest")})
    if frame.get("frameDigest") == control.get("frameDigest"):
        mismatches.append({"field": "frameStats.frameDigest", "expected": "different from control", "actual": frame.get("frameDigest")})
    return mismatches


def build_success_result(graph_id: Any, artifacts: dict[str, Any], layout: dict[str, Any], msl: dict[str, Any]) -> dict[str, Any]:
    mesh_summary = msl["meshSummary"]
    frame = msl["frameStats"]
    control = msl["controlFrameStats"]
    return {
        "kind": "TixlMeshDrawResourceBindingProof",
        "graphId": graph_id,
        "ok": True,
        "status": "summarized_tixl_mesh_draw_resource_binding",
        "message": "validated audited TiXL mesh draw slots against the current explicit MSL approximation buffer bindings",
        "inputArtifacts": artifacts,
        "bindingLedger": {
            "boundNow": [
                {
                    "sourceRegister": "t0",
                    "sourceName": "PbrVertices",
                    "sourceKind": "StructuredBuffer<PbrVertex>",
                    "metalBinding": "buffer(0)",
                    "strideBytes": layout["pbrVertex"]["strideBytes"],
                    "observedIn": "TixlMeshDrawMslApproxProof",
                },
                {
                    "sourceRegister": "t1",
                    "sourceName": "FaceIndices",
                    "sourceKind": "StructuredBuffer<int3>",
                    "metalBinding": "buffer(1)",
                    "strideBytes": layout["faceIndices"]["strideBytes"],
                    "drawVertexCount": mesh_summary["drawVertexCount"],
                    "drawVertexCountFormula": "faceCount * 3",
                    "faceCount": mesh_summary["faceCount"],
                    "observedIn": "TixlMeshDrawMslApproxProof",
                },
            ],
            "declaredButUnbound": declared_but_unbound(),
        },
        "evidence": {
            "mslApproxStatus": msl["status"],
            "frameDigest": frame.get("frameDigest"),
            "controlFrameDigest": control.get("frameDigest"),
            "nonBlackPixels": frame.get("nonBlackPixels"),
            "controlNonBlackPixels": control.get("nonBlackPixels"),
            "mslApproxBufferPackingObserved": msl["claims"]["mslApproxBufferPackingObserved"],
        },
        "claims": claim_flags(True),
    }


def declared_but_unbound() -> list[dict[str, Any]]:
    return [
        {"sourceRegister": "b0", "sourceName": "Transforms", "sourceKind": "cbuffer", "reason": "not bound in explicit MSL approximation"},
        {"sourceRegister": "b1", "sourceName": "Params", "sourceKind": "cbuffer", "reason": "not bound in explicit MSL approximation"},
        {"sourceRegister": "b2", "sourceName": "FogParams", "sourceKind": "cbuffer", "reason": "not bound in explicit MSL approximation"},
        {"sourceRegister": "b3", "sourceName": "PointLights", "sourceKind": "cbuffer", "reason": "not bound in explicit MSL approximation"},
        {"sourceRegister": "b4", "sourceName": "PbrParams", "sourceKind": "cbuffer", "reason": "material constants not bound in explicit MSL approximation"},
        {"sourceRegister": "b5", "sourceName": "Params", "sourceKind": "shadergraph params cbuffer", "reason": "not bound in explicit MSL approximation"},
        {"sourceRegister": "t2", "sourceName": "BaseColorMap", "sourceKind": "Texture2D<float4>", "reason": "not bound in explicit MSL approximation"},
        {"sourceRegister": "t3", "sourceName": "EmissiveColorMap", "sourceKind": "Texture2D<float4>", "reason": "not bound in explicit MSL approximation"},
        {"sourceRegister": "t4", "sourceName": "RSMOMap", "sourceKind": "Texture2D<float4>", "reason": "not bound in explicit MSL approximation"},
        {"sourceRegister": "t5", "sourceName": "NormalMap", "sourceKind": "Texture2D<float4>", "reason": "not bound in explicit MSL approximation"},
        {"sourceRegister": "t6", "sourceName": "PrefilteredSpecular", "sourceKind": "TextureCube<float4>", "reason": "not bound in explicit MSL approximation"},
        {"sourceRegister": "t7", "sourceName": "BRDFLookup", "sourceKind": "Texture2D<float4>", "reason": "not bound in explicit MSL approximation"},
        {"sourceRegister": "s0", "sourceName": "WrappedSampler", "sourceKind": "sampler", "reason": "not bound in explicit MSL approximation"},
        {"sourceRegister": "s1", "sourceName": "ClampedSampler", "sourceKind": "sampler", "reason": "not bound in explicit MSL approximation"},
        {"sourceRegister": "t8+", "sourceName": "injected resources", "sourceKind": "shader template resources", "reason": "RESOURCES(t8) template hole not expanded or bound in explicit MSL approximation"},
        {"sourceRegister": "FLOAT_PARAMS", "sourceName": "shadergraph float params", "sourceKind": "shader template constants", "reason": "template hole not expanded or bound in explicit MSL approximation"},
        {"sourceRegister": "GLOBALS", "sourceName": "shadergraph globals", "sourceKind": "shader template declarations", "reason": "template hole not expanded or bound in explicit MSL approximation"},
        {"sourceRegister": "FIELD_FUNCTIONS", "sourceName": "shadergraph field functions", "sourceKind": "shader template functions", "reason": "template hole not expanded or bound in explicit MSL approximation"},
        {"sourceRegister": "FIELD_CALL", "sourceName": "shadergraph field call", "sourceKind": "shader template expression", "reason": "template hole not expanded or bound in explicit MSL approximation"},
    ]


def default_result(
    graph_id: Any,
    status: str,
    source_summary: dict[str, Any],
    layout_summary: dict[str, Any],
    msl_summary: dict[str, Any],
) -> dict[str, Any]:
    return {
        "kind": "TixlMeshDrawResourceBindingProof",
        "graphId": graph_id,
        "ok": False,
        "status": status,
        "inputArtifacts": {
            "sourceAudit": source_summary,
            "bufferLayout": layout_summary,
            "mslApprox": msl_summary,
        },
        "bindingLedger": {
            "boundNow": [],
            "declaredButUnbound": [],
        },
        "claims": claim_flags(False),
    }


def claim_flags(mesh_buffer_binding_observed: bool) -> dict[str, bool]:
    return {
        "meshBufferBindingObserved": mesh_buffer_binding_observed,
        "fullPbrResourceBinding": False,
        "hlslToMslTranslation": False,
        "tixlRuntimeParity": False,
        "backendReplacementReady": False,
    }


def summarize_artifact(path: Path, artifact: Any, repo_root: Path) -> dict[str, Any]:
    if not isinstance(artifact, dict):
        return {"path": display_path(path, repo_root), "kind": None, "status": None, "ok": None}
    return {
        "path": display_path(path, repo_root),
        "kind": artifact.get("kind"),
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
        errors.append({"code": code, "path": display_path(path, repo_root), "message": clean_message(str(exc), repo_root)})
        return None


def publish(out_dir: Path, result: dict[str, Any], trace: list[dict[str, Any]], errors: list[dict[str, Any]]) -> None:
    write_json(out_dir / RESULT_NAME, result)
    write_json(out_dir / TRACE_NAME, trace)
    write_json(out_dir / ERRORS_NAME, errors)


def write_json(path: Path, payload: Any) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload, indent=2, ensure_ascii=False) + "\n", encoding="utf8")


def clean_text(text: str) -> str:
    return " ".join(text.split())


def clean_message(text: str, repo_root: Path) -> str:
    return clean_text(text.replace(str(repo_root), "."))


def display_path(path: Path | None, repo_root: Path) -> str | None:
    if path is None:
        return None
    try:
        return str(path.resolve().relative_to(repo_root))
    except ValueError:
        return f"outside_repo/{path.name}"


if __name__ == "__main__":
    raise SystemExit(main())

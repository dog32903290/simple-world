#!/usr/bin/env python3
"""
Compile and run a tiny Metal mesh draw approximation for TiXL DrawMesh layout.

This lane consumes the fixed PbrVertex/FaceIndices layout artifact and proves
only this explicit MSL approximation path. It does not translate TiXL HLSL and
does not implement a DrawMesh runtime.
"""

from __future__ import annotations

import json
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path
from typing import Any


DEFAULT_LAYOUT_ARTIFACT = "docs/runtime/artifacts/tixl_mesh_draw_buffer_layout/tixl_mesh_draw_buffer_layout_result.json"
RESULT_NAME = "tixl_mesh_draw_msl_approx_result.json"
TRACE_NAME = "tixl_mesh_draw_msl_approx_trace.json"
ERRORS_NAME = "tixl_mesh_draw_msl_approx_errors.json"
MSL_NAME = "generated_explicit_msl_approx.metal"
MESH_NAME = "mesh_payload.json"

EXPECTED_FIELDS = [
    {"name": "Position", "type": "float3", "offsetBytes": 0, "sizeBytes": 12},
    {"name": "Normal", "type": "float3", "offsetBytes": 12, "sizeBytes": 12},
    {"name": "Tangent", "type": "float3", "offsetBytes": 24, "sizeBytes": 12},
    {"name": "Bitangent", "type": "float3", "offsetBytes": 36, "sizeBytes": 12},
    {"name": "TexCoord", "type": "float2", "offsetBytes": 48, "sizeBytes": 8},
    {"name": "TexCoord2", "type": "float2", "offsetBytes": 56, "sizeBytes": 8},
    {"name": "Selected", "type": "float", "offsetBytes": 64, "sizeBytes": 4},
    {"name": "ColorRGB", "type": "float3", "offsetBytes": 68, "sizeBytes": 12},
]

DEFAULT_MSL_SOURCE = """#include <metal_stdlib>
using namespace metal;

struct PbrVertex80
{
    packed_float3 position;
    packed_float3 normal;
    packed_float3 tangent;
    packed_float3 bitangent;
    packed_float2 texCoord;
    packed_float2 texCoord2;
    float selected;
    packed_float3 colorRGB;
};

struct VertexOut
{
    float4 position [[position]];
    float3 color;
};

vertex VertexOut my_world_mesh_vertex(
    uint vertexId [[vertex_id]],
    device const PbrVertex80* vertices [[buffer(0)]],
    device const packed_int3* faceIndices [[buffer(1)]])
{
    const uint faceId = vertexId / 3u;
    const uint corner = vertexId - faceId * 3u;
    const packed_int3 face = faceIndices[faceId];
    const int vertexIndex = face[corner];
    const PbrVertex80 pbrVertex = vertices[vertexIndex];

    VertexOut out;
    out.position = float4(float3(pbrVertex.position), 1.0);
    out.color = saturate(float3(pbrVertex.colorRGB) + float3(pbrVertex.selected * 0.08));
    return out;
}

fragment float4 my_world_mesh_fragment(VertexOut in [[stage_in]])
{
    return float4(in.color, 1.0);
}
"""


def main() -> int:
    if len(sys.argv) != 3:
        print("usage: tixl_mesh_draw_msl_approx_shell.py <fixture.graph.json> <out_dir>", file=sys.stderr)
        return 2

    fixture_path = Path(sys.argv[1]).expanduser().resolve()
    out_dir = Path(sys.argv[2]).expanduser().resolve()
    out_dir.mkdir(parents=True, exist_ok=True)
    clear_optional_frame_artifacts(out_dir)

    repo_root = Path(__file__).resolve().parents[3]
    trace: list[dict[str, Any]] = [{
        "op": "loadTixlMeshDrawMslApproxFixture",
        "fixture": display_path(fixture_path, repo_root),
    }]
    errors: list[dict[str, Any]] = []

    fixture = read_json(fixture_path, errors, "tixl_mesh_draw_msl_approx.fixture_read_failed", repo_root)
    if fixture is None:
        result = default_result("fixture_read_failed", None, False)
        publish(out_dir, result, trace, errors, None)
        return 1

    result, run_trace, run_errors, frame_stats, msl_source, mesh_payload = run_proof(repo_root, fixture_path, fixture)
    trace.extend(run_trace)
    errors.extend(run_errors)
    write_text(out_dir / MSL_NAME, msl_source)
    if mesh_payload is not None:
        write_json(out_dir / MESH_NAME, mesh_payload)
    trace.append({"op": "publishTixlMeshDrawMslApproxArtifacts", "ok": result.get("ok") is True and not errors})
    publish(out_dir, result, trace, errors, frame_stats if result.get("ok") is True and not errors else None)
    return 0 if result.get("ok") is True and not errors else 1


def run_proof(
    repo_root: Path,
    fixture_path: Path,
    fixture: dict[str, Any],
) -> tuple[dict[str, Any], list[dict[str, Any]], list[dict[str, Any]], dict[str, Any] | None, str, dict[str, Any] | None]:
    trace: list[dict[str, Any]] = []
    errors: list[dict[str, Any]] = []
    graph_id = fixture.get("graphId")
    viewport = fixture.get("viewport") if isinstance(fixture.get("viewport"), dict) else {}
    width = int(viewport.get("width", 16))
    height = int(viewport.get("height", 16))
    layout_path = resolve_path(repo_root, fixture_path, fixture.get("layoutArtifact") or DEFAULT_LAYOUT_ARTIFACT)
    msl_source = fixture.get("explicitMslSource") if isinstance(fixture.get("explicitMslSource"), str) else DEFAULT_MSL_SOURCE

    trace.append({
        "op": "resolveLayoutArtifact",
        "layoutArtifact": display_path(layout_path, repo_root),
        "layoutArtifactExists": layout_path.exists(),
    })
    layout = read_json(layout_path, errors, "tixl_mesh_draw_msl_approx.layout_artifact_read_failed", repo_root)
    if layout is None:
        return default_result("blocked_missing_layout_artifact", graph_id, False), trace, errors, None, msl_source, None

    layout_errors = validate_layout_artifact(layout)
    trace.append({
        "op": "validateLayoutArtifact",
        "kind": layout.get("kind"),
        "ok": layout.get("ok"),
        "status": layout.get("status"),
        "pbrVertexStrideBytes": layout.get("pbrVertex", {}).get("strideBytes") if isinstance(layout.get("pbrVertex"), dict) else None,
        "faceIndicesStrideBytes": layout.get("faceIndices", {}).get("strideBytes") if isinstance(layout.get("faceIndices"), dict) else None,
        "previousMetalBufferPackingParity": layout.get("claims", {}).get("metalBufferPackingParity") if isinstance(layout.get("claims"), dict) else None,
        "valid": not layout_errors,
    })
    if layout_errors:
        errors.append({
            "code": "tixl_mesh_draw_msl_approx.invalid_layout_artifact",
            "message": "Layout artifact does not match the fixed PbrVertex/FaceIndices contract.",
            "mismatches": layout_errors,
        })
        return default_result("blocked_invalid_layout_artifact", graph_id, False), trace, errors, None, msl_source, None

    mesh_payload = normalize_mesh_payload(fixture.get("mesh"), errors)
    trace.append({
        "op": "validateMeshPayload",
        "vertexCount": len(mesh_payload.get("vertices", [])) if mesh_payload else 0,
        "faceCount": len(mesh_payload.get("faces", [])) if mesh_payload else 0,
    })
    if mesh_payload is None:
        return default_result("blocked_invalid_mesh_fixture", graph_id, True), trace, errors, None, msl_source, None

    if width < 1 or height < 1:
        errors.append({"code": "tixl_mesh_draw_msl_approx.invalid_viewport", "width": width, "height": height})
        return default_result("invalid_viewport", graph_id, True), trace, errors, None, msl_source, mesh_payload

    if not msl_source.strip():
        errors.append({"code": "tixl_mesh_draw_msl_approx.source_missing", "message": "explicit MSL source is required"})
        return default_result("source_missing", graph_id, True), trace, errors, None, msl_source, mesh_payload
    source_contract_errors = validate_msl_source_contract(msl_source)
    trace.append({
        "op": "validateMslSourceContract",
        "valid": not source_contract_errors,
        "requiredTokenCount": 8,
    })
    if source_contract_errors:
        errors.append({
            "code": "tixl_mesh_draw_msl_approx.source_contract_failed",
            "message": "MSL approximation must visibly consume the packed vertex and face buffers.",
            "mismatches": source_contract_errors,
        })
        return default_result("blocked_msl_source_contract_failed", graph_id, True), trace, errors, None, msl_source, mesh_payload

    build_dir = Path(tempfile.mkdtemp(prefix="tixl-mesh-draw-msl-approx-build-"))
    try:
        msl_path = build_dir / MSL_NAME
        mesh_path = build_dir / MESH_NAME
        control_mesh_path = build_dir / f"control_{MESH_NAME}"
        probe_bin = build_dir / "tixl_mesh_draw_msl_approx_probe"
        write_text(msl_path, msl_source)
        write_json(mesh_path, mesh_payload)
        control_mesh_payload = degenerate_control_mesh_payload(mesh_payload)
        write_json(control_mesh_path, control_mesh_payload)

        probe_source = repo_root / "docs/runtime/native/tixl_mesh_draw_msl_approx_probe.mm"
        compile_cmd = [
            "xcrun",
            "clang++",
            "-std=c++17",
            "-fobjc-arc",
            "-framework",
            "Metal",
            "-framework",
            "Foundation",
            str(probe_source),
            "-o",
            str(probe_bin),
        ]
        build = subprocess.run(compile_cmd, cwd=repo_root, text=True, capture_output=True)
        trace.append({
            "op": "buildTixlMeshDrawMslApproxProbe",
            "compiler": "xcrun clang++",
            "probe": display_path(probe_source, repo_root),
            "exitCode": build.returncode,
        })
        if build.returncode != 0:
            errors.append({
                "code": "tixl_mesh_draw_msl_approx.probe_build_failed",
                "message": clean_text(build.stderr or build.stdout or "probe build failed"),
            })
            return default_result("probe_build_failed", graph_id, True), trace, errors, None, msl_source, mesh_payload

        run = subprocess.run(
            [str(probe_bin), str(msl_path), str(mesh_path), str(width), str(height)],
            cwd=repo_root,
            text=True,
            capture_output=True,
        )
        control_run = subprocess.run(
            [str(probe_bin), str(msl_path), str(control_mesh_path), str(width), str(height)],
            cwd=repo_root,
            text=True,
            capture_output=True,
        )
    finally:
        shutil.rmtree(build_dir, ignore_errors=True)

    trace.append({"op": "runTixlMeshDrawMslApproxProbe", "exitCode": run.returncode})
    trace.append({"op": "runTixlMeshDrawMslApproxControlProbe", "exitCode": control_run.returncode})
    probe_payload = parse_probe_payload(run.stdout)
    if probe_payload is None:
        errors.append({
            "code": "tixl_mesh_draw_msl_approx.probe_output_invalid",
            "message": clean_text(run.stderr or run.stdout or "probe did not emit JSON"),
        })
        return default_result("probe_output_invalid", graph_id, True), trace, errors, None, msl_source, mesh_payload

    result = result_from_probe(graph_id, probe_payload)
    result["layoutArtifact"] = summarize_layout_artifact(layout_path, layout, repo_root)
    result["meshSummary"] = {
        "vertexCount": probe_payload.get("vertexCount", len(mesh_payload["vertices"])),
        "faceCount": probe_payload.get("faceCount", len(mesh_payload["faces"])),
        "drawVertexCount": probe_payload.get("drawVertexCount", len(mesh_payload["faces"]) * 3),
        "pbrVertexStrideBytes": 80,
        "faceIndicesStrideBytes": 12,
    }
    result["generatedMslArtifact"] = MSL_NAME

    if run.returncode != 0 or result.get("ok") is not True:
        errors.append(error_from_probe(str(result.get("status")), probe_payload))
        return result, trace, errors, None, msl_source, mesh_payload
    control_probe_payload = parse_probe_payload(control_run.stdout)
    if control_probe_payload is None:
        errors.append({
            "code": "tixl_mesh_draw_msl_approx.control_probe_output_invalid",
            "message": clean_text(control_run.stderr or control_run.stdout or "control probe did not emit JSON"),
        })
        result["ok"] = False
        result["status"] = "blocked_control_probe_output_invalid"
        result["claims"] = claim_flags(True, bool(probe_payload.get("actualCompilerRan")), bool(probe_payload.get("actualMetalRan")), False)
        return result, trace, errors, None, msl_source, mesh_payload
    control_stats = frame_stats_from_probe(control_probe_payload)
    result["controlFrameStats"] = control_stats
    sensitivity_errors = validate_buffer_sensitive_readback(probe_payload, control_probe_payload)
    if sensitivity_errors:
        errors.append({
            "code": "tixl_mesh_draw_msl_approx.buffer_sensitivity_failed",
            "message": "The approximation did not prove that packed vertex/face buffers affect the frame.",
            "mismatches": sensitivity_errors,
        })
        result["ok"] = False
        result["status"] = "blocked_buffer_sensitivity_failed"
        result["claims"] = claim_flags(True, bool(probe_payload.get("actualCompilerRan")), bool(probe_payload.get("actualMetalRan")), False)
        return result, trace, errors, None, msl_source, mesh_payload

    frame_stats = frame_stats_from_probe(probe_payload)
    result["frameStats"] = frame_stats
    return result, trace, errors, frame_stats, msl_source, mesh_payload


def validate_layout_artifact(layout: dict[str, Any]) -> list[dict[str, Any]]:
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
    fields = [
        {key: field.get(key) for key in ("name", "type", "offsetBytes", "sizeBytes")}
        for field in pbr_vertex.get("fields", [])
        if isinstance(field, dict)
    ]
    if fields != EXPECTED_FIELDS:
        mismatches.append({"field": "pbrVertex.fields", "expected": EXPECTED_FIELDS, "actual": fields})
    if face_indices.get("strideBytes") != 12:
        mismatches.append({"field": "faceIndices.strideBytes", "expected": 12, "actual": face_indices.get("strideBytes")})
    if face_indices.get("elementType") != "int3":
        mismatches.append({"field": "faceIndices.elementType", "expected": "int3", "actual": face_indices.get("elementType")})
    claims = layout.get("claims") if isinstance(layout.get("claims"), dict) else {}
    if claims.get("metalBufferPackingParity") is not False:
        mismatches.append({"field": "claims.metalBufferPackingParity", "expected": False, "actual": claims.get("metalBufferPackingParity")})
    return mismatches


def validate_msl_source_contract(source: str) -> list[dict[str, Any]]:
    required = [
        "struct PbrVertex80",
        "packed_float3 position",
        "packed_float2 texCoord",
        "packed_int3",
        "[[buffer(0)]]",
        "[[buffer(1)]]",
        "faceIndices[faceId]",
        "vertices[vertexIndex]",
    ]
    return [
        {"field": "explicitMslSource", "expectedToken": token}
        for token in required
        if token not in source
    ]


def degenerate_control_mesh_payload(mesh_payload: dict[str, Any]) -> dict[str, Any]:
    return {
        "vertices": mesh_payload["vertices"],
        "faces": [[0, 0, 0]],
    }


def frame_stats_from_probe(probe_payload: dict[str, Any]) -> dict[str, Any]:
    return {
        "width": probe_payload["width"],
        "height": probe_payload["height"],
        "byteCount": probe_payload["byteCount"],
        "nonBlack": probe_payload["nonBlack"],
        "varied": probe_payload["varied"],
        "nonBlackPixels": probe_payload["nonBlackPixels"],
        "uniqueColorSamples": probe_payload["uniqueColorSamples"],
        "frameDigest": probe_payload.get("frameDigest"),
        "opaquePixels": probe_payload.get("opaquePixels"),
    }


def validate_buffer_sensitive_readback(main_probe: dict[str, Any], control_probe: dict[str, Any]) -> list[dict[str, Any]]:
    mismatches: list[dict[str, Any]] = []
    pixel_count = int(main_probe.get("width", 0) or 0) * int(main_probe.get("height", 0) or 0)
    if main_probe.get("nonBlack") is not True:
        mismatches.append({"field": "main.nonBlack", "expected": True, "actual": main_probe.get("nonBlack")})
    if main_probe.get("varied") is not True:
        mismatches.append({"field": "main.varied", "expected": True, "actual": main_probe.get("varied")})
    if int(main_probe.get("nonBlackPixels") or 0) <= 0:
        mismatches.append({"field": "main.nonBlackPixels", "expected": "> 0", "actual": main_probe.get("nonBlackPixels")})
    if int(main_probe.get("uniqueColorSamples") or 0) <= 8:
        mismatches.append({"field": "main.uniqueColorSamples", "expected": "> 8", "actual": main_probe.get("uniqueColorSamples")})
    if pixel_count > 0 and main_probe.get("opaquePixels") != pixel_count:
        mismatches.append({"field": "main.opaquePixels", "expected": pixel_count, "actual": main_probe.get("opaquePixels")})
    if control_probe.get("status") != "rendered_tixl_mesh_draw_msl_approximation":
        mismatches.append({"field": "control.status", "expected": "rendered_tixl_mesh_draw_msl_approximation", "actual": control_probe.get("status")})
    if control_probe.get("frameDigest") == main_probe.get("frameDigest"):
        mismatches.append({"field": "control.frameDigest", "expected": "different from main", "actual": control_probe.get("frameDigest")})
    if int(control_probe.get("nonBlackPixels") or 0) >= int(main_probe.get("nonBlackPixels") or 0):
        mismatches.append({
            "field": "control.nonBlackPixels",
            "expected": "less than main nonBlackPixels",
            "actual": control_probe.get("nonBlackPixels"),
        })
    return mismatches


def normalize_mesh_payload(mesh: Any, errors: list[dict[str, Any]]) -> dict[str, Any] | None:
    if not isinstance(mesh, dict):
        errors.append({"code": "tixl_mesh_draw_msl_approx.mesh_missing", "message": "fixture mesh is required"})
        return None
    vertices = mesh.get("vertices")
    faces = mesh.get("faces")
    if not isinstance(vertices, list) or len(vertices) < 3:
        errors.append({"code": "tixl_mesh_draw_msl_approx.invalid_vertices", "message": "mesh.vertices must contain at least three vertices"})
        return None
    if not isinstance(faces, list) or not faces:
        errors.append({"code": "tixl_mesh_draw_msl_approx.invalid_faces", "message": "mesh.faces must contain at least one face"})
        return None

    normalized_vertices = []
    for index, vertex in enumerate(vertices):
        if not isinstance(vertex, dict):
            errors.append({"code": "tixl_mesh_draw_msl_approx.invalid_vertex", "index": index})
            return None
        normalized_vertices.append({
            "position": float_list(vertex.get("position"), 3, f"vertices[{index}].position", errors),
            "normal": float_list(vertex.get("normal"), 3, f"vertices[{index}].normal", errors),
            "tangent": float_list(vertex.get("tangent"), 3, f"vertices[{index}].tangent", errors),
            "bitangent": float_list(vertex.get("bitangent"), 3, f"vertices[{index}].bitangent", errors),
            "texCoord": float_list(vertex.get("texCoord"), 2, f"vertices[{index}].texCoord", errors),
            "texCoord2": float_list(vertex.get("texCoord2"), 2, f"vertices[{index}].texCoord2", errors),
            "selected": float(vertex.get("selected", 0.0)),
            "colorRGB": float_list(vertex.get("colorRGB"), 3, f"vertices[{index}].colorRGB", errors),
        })
        if errors:
            return None

    normalized_faces = []
    for index, face in enumerate(faces):
        if not isinstance(face, list) or len(face) != 3:
            errors.append({"code": "tixl_mesh_draw_msl_approx.invalid_face", "index": index, "message": "each face must be three vertex indices"})
            return None
        normalized_face = [int(value) for value in face]
        if any(value < 0 or value >= len(normalized_vertices) for value in normalized_face):
            errors.append({"code": "tixl_mesh_draw_msl_approx.face_index_out_of_range", "index": index, "face": normalized_face})
            return None
        normalized_faces.append(normalized_face)

    return {"vertices": normalized_vertices, "faces": normalized_faces}


def float_list(value: Any, length: int, field: str, errors: list[dict[str, Any]]) -> list[float]:
    if not isinstance(value, list) or len(value) != length:
        errors.append({"code": "tixl_mesh_draw_msl_approx.invalid_mesh_field", "field": field, "expectedLength": length})
        return []
    try:
        return [float(item) for item in value]
    except (TypeError, ValueError):
        errors.append({"code": "tixl_mesh_draw_msl_approx.invalid_mesh_field", "field": field, "expected": "numeric values"})
        return []


def result_from_probe(graph_id: Any, probe: dict[str, Any]) -> dict[str, Any]:
    status = str(probe.get("status") or "probe_failed")
    ok = status == "rendered_tixl_mesh_draw_msl_approximation"
    actual_compiler_ran = bool(probe.get("actualCompilerRan"))
    actual_metal_ran = bool(probe.get("actualMetalRan"))
    return {
        "kind": "TixlMeshDrawMslApproxProof",
        "graphId": graph_id,
        "ok": ok,
        "status": status,
        "backend": "Metal",
        "width": probe.get("width"),
        "height": probe.get("height"),
        "byteCount": probe.get("byteCount", 0),
        "message": probe.get("message", ""),
        "claims": claim_flags(True, actual_compiler_ran, actual_metal_ran, ok),
    }


def default_result(status: str, graph_id: Any, layout_consumed: bool) -> dict[str, Any]:
    return {
        "kind": "TixlMeshDrawMslApproxProof",
        "graphId": graph_id,
        "ok": False,
        "status": status,
        "backend": "Metal",
        "claims": claim_flags(layout_consumed, False, False, False),
    }


def claim_flags(layout_consumed: bool, actual_compiler_ran: bool, actual_metal_ran: bool, rendered: bool) -> dict[str, bool]:
    return {
        "layoutArtifactConsumed": layout_consumed,
        "actualCompilerRan": actual_compiler_ran,
        "actualMetalRan": actual_metal_ran,
        "mslApproximationRendered": rendered,
        "mslApproxBufferPackingObserved": rendered,
        "tixlRuntimeParity": False,
        "hlslToMslTranslation": False,
        "pbrVisualCorrectness": False,
        "drawMeshRuntime": False,
    }


def error_from_probe(status: str, probe: dict[str, Any]) -> dict[str, Any]:
    message = str(probe.get("message") or probe.get("compilerDiagnostic") or status)
    if status == "blocked_metal_device_unavailable":
        code = "tixl_mesh_draw_msl_approx.device_unavailable"
    elif status == "compile_failed":
        code = "tixl_mesh_draw_msl_approx.compile_failed"
    elif status == "pipeline_failed":
        code = "tixl_mesh_draw_msl_approx.pipeline_failed"
    elif status == "render_failed":
        code = "tixl_mesh_draw_msl_approx.render_failed"
    elif status == "mesh_payload_invalid":
        code = "tixl_mesh_draw_msl_approx.mesh_payload_invalid"
    else:
        code = "tixl_mesh_draw_msl_approx.probe_failed"
    error: dict[str, Any] = {"code": code, "message": clean_text(message)}
    if probe.get("compilerDiagnostic"):
        error["compilerDiagnostic"] = clean_text(str(probe["compilerDiagnostic"]))
    return error


def summarize_layout_artifact(path: Path, layout: dict[str, Any], repo_root: Path) -> dict[str, Any]:
    return {
        "path": display_path(path, repo_root),
        "kind": layout.get("kind"),
        "status": layout.get("status"),
        "ok": layout.get("ok"),
        "pbrVertexStrideBytes": layout.get("pbrVertex", {}).get("strideBytes") if isinstance(layout.get("pbrVertex"), dict) else None,
        "faceIndicesStrideBytes": layout.get("faceIndices", {}).get("strideBytes") if isinstance(layout.get("faceIndices"), dict) else None,
        "previousMetalBufferPackingParity": layout.get("claims", {}).get("metalBufferPackingParity") if isinstance(layout.get("claims"), dict) else None,
    }


def parse_probe_payload(stdout: str) -> dict[str, Any] | None:
    text = stdout.strip()
    if not text:
        return None
    try:
        return json.loads(text.splitlines()[-1])
    except json.JSONDecodeError:
        return None


def resolve_path(repo_root: Path, fixture_path: Path, maybe_path: Any) -> Path:
    if not isinstance(maybe_path, str) or not maybe_path:
        return repo_root / DEFAULT_LAYOUT_ARTIFACT
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


def publish(
    out_dir: Path,
    result: dict[str, Any],
    trace: list[dict[str, Any]],
    errors: list[dict[str, Any]],
    frame_stats: dict[str, Any] | None,
) -> None:
    write_json(out_dir / RESULT_NAME, result)
    write_json(out_dir / TRACE_NAME, trace)
    write_json(out_dir / ERRORS_NAME, errors)
    if frame_stats is not None:
        write_json(out_dir / "frame_stats.json", frame_stats)


def write_json(path: Path, payload: Any) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload, indent=2, ensure_ascii=False) + "\n", encoding="utf8")


def write_text(path: Path, payload: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(payload, encoding="utf8")


def clear_optional_frame_artifacts(out_dir: Path) -> None:
    for name in ("frame_stats.json",):
        target = out_dir / name
        if target.exists():
            target.unlink()


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

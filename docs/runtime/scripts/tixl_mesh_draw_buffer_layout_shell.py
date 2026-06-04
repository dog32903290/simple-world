#!/usr/bin/env python3
"""
Summarize TiXL mesh draw buffer layout facts without copying donor source.

This shell fixes the PbrVertex and FaceIndices packing contract needed by the
next MSL draw approximation lane. It does not compile, render, or claim Metal
buffer parity, TiXL runtime parity, or visual correctness.
"""

from __future__ import annotations

import ast
import hashlib
import json
import re
import sys
from pathlib import Path
from typing import Any


DEFAULT_LAYOUT_SOURCE = "external/tixl/Core/Rendering/PbrVertex.cs"
DEFAULT_SHADER_SOURCE = "external/tixl/Operators/Lib/Assets/shaders/3d/mesh/mesh-Draw.hlsl"
DEFAULT_SOURCE_AUDIT = "docs/runtime/artifacts/tixl_mesh_draw_shader_source_audit/tixl_mesh_draw_shader_source_audit_result.json"
RESULT_NAME = "tixl_mesh_draw_buffer_layout_result.json"
TRACE_NAME = "tixl_mesh_draw_buffer_layout_trace.json"
ERRORS_NAME = "tixl_mesh_draw_buffer_layout_errors.json"

FIELD_RE = re.compile(
    r"\[FieldOffset\(([^)]+)\)\]\s*public\s+(Vector[23]|float)\s+([A-Za-z_]\w*)\s*;",
    re.MULTILINE,
)
STRIDE_RE = re.compile(r"\bpublic\s+const\s+int\s+Stride\s*=\s*([^;]+);")
FACE_INDICES_RE = re.compile(
    r"\bStructuredBuffer\s*<\s*int3\s*>\s+FaceIndices\s*:\s*register\s*\(\s*(t\d+)\s*\)",
    re.MULTILINE,
)

TYPE_INFO = {
    "Vector3": {"contractType": "float3", "sizeBytes": 12},
    "Vector2": {"contractType": "float2", "sizeBytes": 8},
    "float": {"contractType": "float", "sizeBytes": 4},
}

CANONICAL_FIELDS = {
    "Position": "Position",
    "Normal": "Normal",
    "Tangent": "Tangent",
    "Bitangent": "Bitangent",
    "Texcoord": "TexCoord",
    "Texcoord2": "TexCoord2",
    "Selection": "Selected",
    "ColorRgb": "ColorRGB",
}

EXPECTED_LAYOUT = [
    {"name": "Position", "type": "float3", "offsetBytes": 0, "sizeBytes": 12},
    {"name": "Normal", "type": "float3", "offsetBytes": 12, "sizeBytes": 12},
    {"name": "Tangent", "type": "float3", "offsetBytes": 24, "sizeBytes": 12},
    {"name": "Bitangent", "type": "float3", "offsetBytes": 36, "sizeBytes": 12},
    {"name": "TexCoord", "type": "float2", "offsetBytes": 48, "sizeBytes": 8},
    {"name": "TexCoord2", "type": "float2", "offsetBytes": 56, "sizeBytes": 8},
    {"name": "Selected", "type": "float", "offsetBytes": 64, "sizeBytes": 4},
    {"name": "ColorRGB", "type": "float3", "offsetBytes": 68, "sizeBytes": 12},
]


def main() -> int:
    if len(sys.argv) != 3:
        print("usage: tixl_mesh_draw_buffer_layout_shell.py <fixture.graph.json> <out_dir>", file=sys.stderr)
        return 2

    fixture_path = Path(sys.argv[1]).expanduser().resolve()
    out_dir = Path(sys.argv[2]).expanduser().resolve()
    out_dir.mkdir(parents=True, exist_ok=True)

    repo_root = Path(__file__).resolve().parents[3]
    trace: list[dict[str, Any]] = [{
        "op": "loadTixlMeshDrawBufferLayoutFixture",
        "fixture": display_path(fixture_path, repo_root),
    }]
    errors: list[dict[str, Any]] = []
    fixture = read_json(fixture_path, errors, "tixl_mesh_draw_buffer_layout.fixture_read_failed", repo_root)
    if fixture is None:
        result = blocked_result(None, None, None, repo_root, None, "fixture_read_failed")
        write_artifacts(out_dir, result, trace, errors)
        return 1

    result, run_trace, run_errors = run_proof(fixture, fixture_path, repo_root)
    trace.extend(run_trace)
    errors.extend(run_errors)
    trace.append({
        "op": "publishTixlMeshDrawBufferLayoutArtifacts",
        "ok": result.get("ok") is True and not errors,
    })
    write_artifacts(out_dir, result, trace, errors)
    return 0 if result.get("ok") is True and not errors else 1


def run_proof(
    fixture: dict[str, Any],
    fixture_path: Path,
    repo_root: Path,
) -> tuple[dict[str, Any], list[dict[str, Any]], list[dict[str, Any]]]:
    errors: list[dict[str, Any]] = []
    trace: list[dict[str, Any]] = []
    layout_path = resolve_path(repo_root, fixture_path, fixture.get("donorLayoutSource") or DEFAULT_LAYOUT_SOURCE)
    shader_path = resolve_path(repo_root, fixture_path, fixture.get("donorShaderSource") or DEFAULT_SHADER_SOURCE)
    audit_path = resolve_path(repo_root, fixture_path, fixture.get("sourceAuditArtifact") or DEFAULT_SOURCE_AUDIT)

    trace.append({
        "op": "resolveDonorEvidence",
        "donorLayoutSource": display_path(layout_path, repo_root),
        "donorLayoutSourceExists": layout_path.exists(),
        "donorShaderSource": display_path(shader_path, repo_root),
        "donorShaderSourceExists": shader_path.exists(),
        "sourceAuditArtifact": display_path(audit_path, repo_root),
        "sourceAuditArtifactExists": audit_path.exists(),
    })

    if not layout_path.exists():
        errors.append({
            "code": "tixl_mesh_draw_buffer_layout.donor_layout_source_missing",
            "donorLayoutSource": display_path(layout_path, repo_root),
            "message": "TiXL PbrVertex donor source is not available in this checkout.",
        })
        return blocked_result(layout_path, shader_path, audit_path, repo_root, fixture.get("graphId"), "blocked_missing_donor_layout_source"), trace, errors

    if not audit_path.exists():
        errors.append({
            "code": "tixl_mesh_draw_buffer_layout.source_audit_missing",
            "sourceAuditArtifact": display_path(audit_path, repo_root),
            "message": "The TiXL mesh draw shader source audit artifact must exist before fixing buffer layout facts.",
        })
        return blocked_result(layout_path, shader_path, audit_path, repo_root, fixture.get("graphId"), "blocked_missing_source_audit"), trace, errors

    if not shader_path.exists():
        errors.append({
            "code": "tixl_mesh_draw_buffer_layout.donor_shader_source_missing",
            "donorShaderSource": display_path(shader_path, repo_root),
            "message": "TiXL mesh draw shader donor source is not available in this checkout.",
        })
        return blocked_result(layout_path, shader_path, audit_path, repo_root, fixture.get("graphId"), "blocked_missing_donor_shader_source"), trace, errors

    layout_text = layout_path.read_text(encoding="utf8")
    shader_text = shader_path.read_text(encoding="utf8")
    source_audit = read_json(audit_path, errors, "tixl_mesh_draw_buffer_layout.source_audit_read_failed", repo_root)
    if source_audit is None:
        return blocked_result(layout_path, shader_path, audit_path, repo_root, fixture.get("graphId"), "blocked_missing_source_audit"), trace, errors

    try:
        layout_summary = parse_pbr_vertex_layout(layout_text)
        face_summary = parse_face_indices(shader_text)
    except (SyntaxError, ValueError) as exc:
        errors.append({
            "code": "tixl_mesh_draw_buffer_layout.layout_parse_failed",
            "message": str(exc),
        })
        result = blocked_result(layout_path, shader_path, audit_path, repo_root, fixture.get("graphId"), "blocked_layout_parse_failed")
        result["semanticBlockers"] = [{
            "code": "layout_parse_failed",
            "message": "PbrVertex or FaceIndices donor evidence could not be parsed into stable layout facts.",
        }]
        return result, trace, errors
    trace.append({
        "op": "summarizePbrVertexLayout",
        "fieldCount": len(layout_summary.get("fields", [])),
        "strideBytes": layout_summary.get("strideBytes"),
    })
    trace.append({
        "op": "summarizeFaceIndicesLayout",
        "found": face_summary.get("found"),
        "strideBytes": face_summary.get("strideBytes"),
        "topology": face_summary.get("topology"),
    })

    blockers = semantic_blockers(layout_summary, face_summary, source_audit, shader_path, shader_text, repo_root)
    contract_mismatches = [blocker for blocker in blockers if blocker.get("code") == "layout_contract_mismatch"]
    if contract_mismatches:
        errors.append({
            "code": "tixl_mesh_draw_buffer_layout.layout_contract_mismatch",
            "blockers": contract_mismatches,
        })

    ok = not contract_mismatches and not errors
    status = "summarized_tixl_mesh_draw_buffer_layout" if ok else "blocked_layout_contract_mismatch"
    result = {
        "kind": "TixlMeshDrawBufferLayoutProof",
        "graphId": fixture.get("graphId"),
        "ok": ok,
        "status": status,
        "evidence": {
            "donorLayoutSource": summarize_source(layout_path, layout_text, repo_root),
            "donorShaderSource": summarize_source(shader_path, shader_text, repo_root),
            "sourceAuditArtifact": summarize_json_artifact(audit_path, source_audit, repo_root),
        },
        "pbrVertex": layout_summary,
        "faceIndices": face_summary,
        "drawContract": {
            "topology": "TriangleList",
            "drawVertexCountFormula": "faceCount * 3",
            "triangulationClaimed": False,
        },
        "semanticBlockers": blockers,
        "claims": claim_flags(ok),
    }
    return result, trace, errors


def parse_pbr_vertex_layout(text: str) -> dict[str, Any]:
    fields = []
    for match in FIELD_RE.finditer(text):
        donor_type = match.group(2)
        donor_name = match.group(3)
        type_info = TYPE_INFO[donor_type]
        fields.append({
            "name": CANONICAL_FIELDS.get(donor_name, donor_name),
            "donorName": donor_name,
            "type": type_info["contractType"],
            "donorType": donor_type,
            "offsetBytes": eval_int_expr(match.group(1)),
            "sizeBytes": type_info["sizeBytes"],
            "line": line_number(text, match.start()),
        })
    stride_match = STRIDE_RE.search(text)
    stride_bytes = eval_int_expr(stride_match.group(1)) if stride_match else None
    return {
        "struct": "PbrVertex",
        "layoutKind": "Explicit",
        "fields": fields,
        "strideBytes": stride_bytes,
    }


def parse_face_indices(text: str) -> dict[str, Any]:
    match = FACE_INDICES_RE.search(text)
    return {
        "buffer": "FaceIndices",
        "found": match is not None,
        "elementType": "int3" if match else None,
        "register": match.group(1) if match else None,
        "strideBytes": 12 if match else None,
        "topology": "TriangleList",
        "drawVertexCountFormula": "faceCount * 3",
        "triangulationClaimed": False,
    }


def semantic_blockers(
    layout_summary: dict[str, Any],
    face_summary: dict[str, Any],
    source_audit: dict[str, Any],
    shader_path: Path,
    shader_text: str,
    repo_root: Path,
) -> list[dict[str, Any]]:
    blockers = [
        {
            "code": "metal_buffer_packing_parity_not_proven",
            "message": "This lane fixes contract facts only; no Metal buffer packing proof has run.",
        },
        {
            "code": "tixl_runtime_parity_not_proven",
            "message": "This lane reads donor layout evidence only; it does not run TiXL or compare runtime output.",
        },
        {
            "code": "visual_correctness_not_proven",
            "message": "No draw, render, screenshot, or readback is performed in this lane.",
        },
    ]
    mismatch = []
    if layout_summary.get("strideBytes") != 80:
        mismatch.append({"field": "PbrVertex.strideBytes", "expected": 80, "actual": layout_summary.get("strideBytes")})
    compact_fields = [
        {key: field[key] for key in ("name", "type", "offsetBytes", "sizeBytes")}
        for field in layout_summary.get("fields", [])
    ]
    if compact_fields != EXPECTED_LAYOUT:
        mismatch.append({"field": "PbrVertex.fields", "expected": EXPECTED_LAYOUT, "actual": compact_fields})
    if face_summary.get("found") is not True:
        mismatch.append({"field": "FaceIndices", "expected": "StructuredBuffer<int3>", "actual": None})
    if face_summary.get("strideBytes") != 12:
        mismatch.append({"field": "FaceIndices.strideBytes", "expected": 12, "actual": face_summary.get("strideBytes")})
    audit_path = source_audit.get("donorSource", {}).get("path")
    audit_hash = source_audit.get("donorSource", {}).get("sha256")
    expected_shader_path = display_path(shader_path, repo_root)
    expected_shader_hash = hashlib.sha256(shader_text.encode("utf8")).hexdigest()
    if source_audit.get("kind") != "TixlMeshDrawShaderSourceAudit":
        mismatch.append({
            "field": "sourceAudit.kind",
            "expected": "TixlMeshDrawShaderSourceAudit",
            "actual": source_audit.get("kind"),
        })
    if source_audit.get("ok") is not True:
        mismatch.append({"field": "sourceAudit.ok", "expected": True, "actual": source_audit.get("ok")})
    if source_audit.get("status") != "audited_tixl_mesh_draw_source":
        mismatch.append({
            "field": "sourceAudit.status",
            "expected": "audited_tixl_mesh_draw_source",
            "actual": source_audit.get("status"),
        })
    if audit_path != expected_shader_path:
        mismatch.append({"field": "sourceAudit.donorSource.path", "expected": expected_shader_path, "actual": audit_path})
    if audit_hash != expected_shader_hash:
        mismatch.append({"field": "sourceAudit.donorSource.sha256", "expected": expected_shader_hash, "actual": audit_hash})
    resources = source_audit.get("resources", [])
    if not any(
        resource.get("name") == "PbrVertices"
        and resource.get("elementType") == "PbrVertex"
        and resource.get("register") == "t0"
        for resource in resources
    ):
        mismatch.append({"field": "sourceAudit.resources.PbrVertices", "expected": "StructuredBuffer<PbrVertex> t0", "actual": None})
    if not any(
        resource.get("name") == "FaceIndices"
        and resource.get("elementType") == "int3"
        and resource.get("register") == "t1"
        for resource in resources
    ):
        mismatch.append({"field": "sourceAudit.resources.FaceIndices", "expected": "StructuredBuffer<int3> t1", "actual": None})
    if mismatch:
        blockers.append({"code": "layout_contract_mismatch", "mismatches": mismatch})
    return blockers


def blocked_result(
    layout_path: Path | None,
    shader_path: Path | None,
    audit_path: Path | None,
    repo_root: Path,
    graph_id: str | None,
    status: str,
) -> dict[str, Any]:
    return {
        "kind": "TixlMeshDrawBufferLayoutProof",
        "graphId": graph_id,
        "ok": False,
        "status": status,
        "evidence": {
            "donorLayoutSource": {"path": display_path(layout_path, repo_root) if layout_path is not None else None},
            "donorShaderSource": {"path": display_path(shader_path, repo_root) if shader_path is not None else None},
            "sourceAuditArtifact": {"path": display_path(audit_path, repo_root) if audit_path is not None else None},
        },
        "pbrVertex": {"struct": "PbrVertex", "fields": [], "strideBytes": None},
        "faceIndices": {"buffer": "FaceIndices", "found": False, "elementType": None, "strideBytes": None},
        "drawContract": {
            "topology": "TriangleList",
            "drawVertexCountFormula": "faceCount * 3",
            "triangulationClaimed": False,
        },
        "semanticBlockers": [{
            "code": status,
            "message": "Required donor evidence is missing; buffer layout facts were not summarized.",
        }],
        "claims": claim_flags(False),
    }


def claim_flags(contract_layout_summarized: bool) -> dict[str, bool]:
    return {
        "contractLayoutSummarized": contract_layout_summarized,
        "metalBufferPackingParity": False,
        "tixlRuntimeParity": False,
        "visualCorrectness": False,
    }


def summarize_source(path: Path, text: str, repo_root: Path) -> dict[str, Any]:
    return {
        "path": display_path(path, repo_root),
        "sha256": hashlib.sha256(text.encode("utf8")).hexdigest(),
        "lineCount": len(text.splitlines()),
    }


def summarize_json_artifact(path: Path, payload: dict[str, Any], repo_root: Path) -> dict[str, Any]:
    text = json.dumps(payload, sort_keys=True, separators=(",", ":"))
    return {
        "path": display_path(path, repo_root),
        "sha256": hashlib.sha256(text.encode("utf8")).hexdigest(),
        "kind": payload.get("kind"),
        "status": payload.get("status"),
        "ok": payload.get("ok"),
    }


def eval_int_expr(expr: str) -> int | None:
    cleaned = re.sub(r"//.*", "", expr).strip()
    tree = ast.parse(cleaned, mode="eval")
    return int(eval_ast_int(tree.body))


def eval_ast_int(node: ast.AST) -> int:
    if isinstance(node, ast.Constant) and isinstance(node.value, int):
        return node.value
    if isinstance(node, ast.UnaryOp) and isinstance(node.op, ast.USub):
        return -eval_ast_int(node.operand)
    if isinstance(node, ast.BinOp) and isinstance(node.op, ast.Mult):
        return eval_ast_int(node.left) * eval_ast_int(node.right)
    if isinstance(node, ast.BinOp) and isinstance(node.op, ast.Add):
        return eval_ast_int(node.left) + eval_ast_int(node.right)
    if isinstance(node, ast.BinOp) and isinstance(node.op, ast.Sub):
        return eval_ast_int(node.left) - eval_ast_int(node.right)
    raise ValueError(f"unsupported integer expression: {ast.dump(node)}")


def resolve_path(repo_root: Path, fixture_path: Path, maybe_path: Any) -> Path:
    if not isinstance(maybe_path, str) or not maybe_path:
        return repo_root / DEFAULT_LAYOUT_SOURCE
    path = Path(maybe_path).expanduser()
    if path.is_absolute():
        return path
    repo_candidate = repo_root / path
    if repo_candidate.exists() or str(maybe_path).startswith(("external/", "docs/")):
        return repo_candidate.resolve()
    return (fixture_path.parent / path).resolve()


def read_json(path: Path, errors: list[dict[str, Any]], code: str, repo_root: Path) -> Any:
    try:
        return json.loads(path.read_text(encoding="utf8"))
    except Exception as exc:
        errors.append({"code": code, "path": display_path(path, repo_root), "message": str(exc)})
        return None


def write_artifacts(out_dir: Path, result: dict[str, Any], trace: list[dict[str, Any]], errors: list[dict[str, Any]]) -> None:
    write_json(out_dir / RESULT_NAME, result)
    write_json(out_dir / TRACE_NAME, trace)
    write_json(out_dir / ERRORS_NAME, errors)


def write_json(path: Path, payload: Any) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload, indent=2, ensure_ascii=False) + "\n", encoding="utf8")


def display_path(path: Path | None, repo_root: Path) -> str | None:
    if path is None:
        return None
    try:
        return str(path.resolve().relative_to(repo_root))
    except ValueError:
        return f"outside_repo/{path.name}"


def line_number(text: str, offset: int) -> int:
    return text.count("\n", 0, offset) + 1


if __name__ == "__main__":
    raise SystemExit(main())

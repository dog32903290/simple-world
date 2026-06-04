#!/usr/bin/env python3
"""
Publish a fail-closed verdict for TiXL mesh Draw b5 shadergraph Params expansion.

b5 is the duplicate Params cbuffer generated from the ShaderGraph FLOAT_PARAMS
template hole. The current artifacts have no concrete b5 fields, so this lane
records the blocker instead of inventing native packing.
"""

from __future__ import annotations

import json
import sys
from pathlib import Path
from typing import Any


RESULT_NAME = "tixl_mesh_draw_b5_shadergraph_params_expansion_result.json"
TRACE_NAME = "tixl_mesh_draw_b5_shadergraph_params_expansion_trace.json"
ERRORS_NAME = "tixl_mesh_draw_b5_shadergraph_params_expansion_errors.json"

DEFAULT_SOURCE_AUDIT = "docs/runtime/artifacts/tixl_mesh_draw_shader_source_audit/tixl_mesh_draw_shader_source_audit_result.json"
DEFAULT_LAYOUT = "docs/runtime/artifacts/tixl_mesh_draw_constant_buffer_layout/tixl_mesh_draw_constant_buffer_layout_result.json"
DEFAULT_POINTLIGHTS = "docs/runtime/artifacts/tixl_mesh_draw_pointlights_and_b5_packing/tixl_mesh_draw_pointlights_and_b5_packing_result.json"
DEFAULT_TEMPLATE = "external/tixl/Operators/Lib/Assets/shaders/3d/mesh/mesh-Draw.hlsl"
DEFAULT_GENERATOR = "external/tixl/Operators/Lib/field/render/_/GenerateShaderGraphCode.cs"
DEFAULT_COMPOSITION = "external/tixl/Operators/Lib/mesh/draw/DrawMesh.t3"

EXPECTED_CLAIMS = {
    "sourceAuditArtifactConsumed": True,
    "constantBufferLayoutArtifactConsumed": True,
    "pointlightsAndB5PackingArtifactConsumed": True,
    "b3PointLightsPackingProven": True,
    "b5ShadergraphParamsExpanded": False,
    "b5FieldsSourceBacked": False,
    "b5NativePackingReady": False,
    "constantBufferAdapterComplete": False,
    "textureSamplerMapping": False,
    "fullPbrResourceBinding": False,
    "backendReplacementReady": False,
    "hlslToMslTranslation": False,
    "tixlRuntimeParity": False,
    "pbrVisualCorrectness": False,
}


def main() -> int:
    if len(sys.argv) != 3:
        print("usage: tixl_mesh_draw_b5_shadergraph_params_expansion_shell.py <fixture.graph.json> <out_dir>", file=sys.stderr)
        return 2

    fixture_path = Path(sys.argv[1]).expanduser().resolve()
    out_dir = Path(sys.argv[2]).expanduser().resolve()
    out_dir.mkdir(parents=True, exist_ok=True)
    repo_root = Path(__file__).resolve().parents[3]
    trace: list[dict[str, Any]] = [{"op": "loadB5ShadergraphParamsExpansionFixture", "fixture": display_path(fixture_path, repo_root)}]
    errors: list[dict[str, Any]] = []

    fixture = read_json(fixture_path, errors, "tixl_mesh_draw_b5_shadergraph_params_expansion.fixture_read_failed", repo_root)
    if fixture is None:
        result = result_payload(None, "blocked_missing_fixture", {}, {}, {}, {}, False, False, False)
        publish(out_dir, result, trace, errors)
        return 1

    result, run_trace, run_errors = run_proof(repo_root, fixture_path, fixture)
    trace.extend(run_trace)
    errors.extend(run_errors)
    trace.append({"op": "publishB5ShadergraphParamsExpansionArtifacts", "ok": result.get("ok") is True and not errors})
    publish(out_dir, result, trace, errors)
    return 0 if result.get("ok") is True and not errors else 1


def run_proof(repo_root: Path, fixture_path: Path, fixture: dict[str, Any]) -> tuple[dict[str, Any], list[dict[str, Any]], list[dict[str, Any]]]:
    trace: list[dict[str, Any]] = []
    errors: list[dict[str, Any]] = []
    graph_id = fixture.get("graphId")

    fixture_errors = validate_fixture(fixture)
    if fixture_errors:
        errors.append({
            "code": "tixl_mesh_draw_b5_shadergraph_params_expansion.invalid_fixture_expectations",
            "message": "Fixture expectations must preserve the bounded b5 expansion verdict.",
            "mismatches": fixture_errors,
        })
        return result_payload(graph_id, "blocked_invalid_fixture", {}, {}, {}, {}, False, False, False), trace, errors

    source_audit_path = resolve_path(repo_root, fixture_path, fixture.get("sourceAuditArtifact"), DEFAULT_SOURCE_AUDIT)
    layout_path = resolve_path(repo_root, fixture_path, fixture.get("constantBufferLayoutArtifact"), DEFAULT_LAYOUT)
    pointlights_path = resolve_path(repo_root, fixture_path, fixture.get("pointlightsAndB5PackingArtifact"), DEFAULT_POINTLIGHTS)
    template_path = resolve_path(repo_root, fixture_path, fixture.get("meshDrawTemplateSource"), DEFAULT_TEMPLATE)
    generator_path = resolve_path(repo_root, fixture_path, fixture.get("generateShaderGraphCodeSource"), DEFAULT_GENERATOR)
    composition_path = resolve_path(repo_root, fixture_path, fixture.get("drawMeshCompositionSource"), DEFAULT_COMPOSITION)
    trace.append({
        "op": "resolveInputArtifacts",
        "sourceAuditArtifact": display_path(source_audit_path, repo_root),
        "constantBufferLayoutArtifact": display_path(layout_path, repo_root),
        "pointlightsAndB5PackingArtifact": display_path(pointlights_path, repo_root),
        "meshDrawTemplateSource": display_path(template_path, repo_root),
        "generateShaderGraphCodeSource": display_path(generator_path, repo_root),
        "drawMeshCompositionSource": display_path(composition_path, repo_root),
    })

    source_audit = read_json(source_audit_path, errors, "tixl_mesh_draw_b5_shadergraph_params_expansion.source_audit_read_failed", repo_root)
    layout = read_json(layout_path, errors, "tixl_mesh_draw_b5_shadergraph_params_expansion.layout_read_failed", repo_root)
    pointlights = read_json(pointlights_path, errors, "tixl_mesh_draw_b5_shadergraph_params_expansion.pointlights_verdict_read_failed", repo_root)
    input_summary = {
        "sourceAudit": summarize_artifact(source_audit_path, source_audit, repo_root),
        "constantBufferLayout": summarize_artifact(layout_path, layout, repo_root),
        "pointlightsAndB5Packing": summarize_artifact(pointlights_path, pointlights, repo_root),
    }
    if source_audit is None or layout is None or pointlights is None:
        return result_payload(graph_id, "blocked_missing_input_artifact", input_summary, {}, {}, {}, source_audit is not None, layout is not None, pointlights is not None), trace, errors

    source_errors = validate_source_facts(template_path, generator_path, composition_path, repo_root)
    source_facts = source_fact_summary(template_path, generator_path, composition_path, repo_root)
    if source_errors:
        errors.append({
            "code": "tixl_mesh_draw_b5_shadergraph_params_expansion.invalid_source_facts",
            "message": "b5 expansion depends on the TiXL FLOAT_PARAMS template and GenerateShaderGraphCode path.",
            "mismatches": source_errors,
        })

    upstream_errors = validate_upstream_pointlights(pointlights)
    if upstream_errors:
        errors.append({
            "code": "tixl_mesh_draw_b5_shadergraph_params_expansion.invalid_pointlights_verdict",
            "message": "b5 expansion must start after the b3 PointLights proof without widened claims.",
            "mismatches": upstream_errors,
        })

    b5_source_fields = b5_fields_from_source_audit(source_audit)
    b5_layout_fields = b5_fields_from_layout(layout)
    layout_errors = validate_layout_b5(layout, b5_layout_fields)
    source_audit_errors = validate_source_audit_b5(source_audit, b5_source_fields)
    if layout_errors:
        errors.append({
            "code": "tixl_mesh_draw_b5_shadergraph_params_expansion.invalid_layout_artifact",
            "message": "Layout artifact must keep b5 as shadergraph duplicate Params until source-backed expansion exists.",
            "mismatches": layout_errors,
        })
    if source_audit_errors:
        errors.append({
            "code": "tixl_mesh_draw_b5_shadergraph_params_expansion.invalid_source_audit_artifact",
            "message": "Source audit must keep b5 fieldless or provide source-backed ShaderGraph param provenance.",
            "mismatches": source_audit_errors,
        })

    trace.append({
        "op": "validateB5ShadergraphExpansionInputs",
        "sourceFactsValid": not source_errors,
        "pointlightsVerdictValid": not upstream_errors,
        "sourceAuditB5FieldCount": len(b5_source_fields),
        "layoutB5FieldCount": len(b5_layout_fields),
        "layoutValid": not layout_errors,
        "sourceAuditValid": not source_audit_errors,
    })
    if source_errors or upstream_errors or layout_errors or source_audit_errors:
        status = "blocked_invalid_b5_expansion_provenance" if source_audit_errors or layout_errors else "blocked_invalid_input_artifact"
        return result_payload(graph_id, status, input_summary, source_facts, expansion_verdict(b5_source_fields, b5_layout_fields), {}, False, False, False), trace, errors

    return result_payload(
        graph_id,
        "blocked_b5_shadergraph_params_not_expanded",
        input_summary,
        source_facts,
        expansion_verdict(b5_source_fields, b5_layout_fields),
        {"requiredNext": "produce_source_backed_shadergraph_param_expansion_artifact_for_b5"},
        True,
        True,
        True,
    ), trace, errors


def validate_fixture(fixture: dict[str, Any]) -> list[dict[str, Any]]:
    mismatches: list[dict[str, Any]] = []
    if fixture.get("graphId") != "fixture.tixl_mesh_draw_b5_shadergraph_params_expansion":
        mismatches.append({"field": "graphId", "expected": "fixture.tixl_mesh_draw_b5_shadergraph_params_expansion", "actual": fixture.get("graphId")})
    if fixture.get("kind") != "TixlMeshDrawB5ShadergraphParamsExpansionVerdict":
        mismatches.append({"field": "kind", "expected": "TixlMeshDrawB5ShadergraphParamsExpansionVerdict", "actual": fixture.get("kind")})
    expected = fixture.get("expected") if isinstance(fixture.get("expected"), dict) else {}
    if expected.get("status") != "blocked_b5_shadergraph_params_not_expanded":
        mismatches.append({"field": "expected.status", "expected": "blocked_b5_shadergraph_params_not_expanded", "actual": expected.get("status")})
    if expected.get("claims") != EXPECTED_CLAIMS:
        mismatches.append({"field": "expected.claims", "expected": EXPECTED_CLAIMS, "actual": expected.get("claims")})
    return mismatches


def validate_source_facts(template_path: Path, generator_path: Path, composition_path: Path, repo_root: Path) -> list[dict[str, Any]]:
    mismatches: list[dict[str, Any]] = []
    template = read_text(template_path)
    generator = read_text(generator_path)
    composition = read_text(composition_path)
    for field, text, needle in (
        ("meshDrawTemplateSource", template, "cbuffer Params : register(b5)"),
        ("meshDrawTemplateSource", template, "/*{FLOAT_PARAMS}*/"),
        ("generateShaderGraphCodeSource", generator, "CollectAllNodeParams"),
        ("generateShaderGraphCodeSource", generator, "AssembleAndInjectParameters"),
        ("generateShaderGraphCodeSource", generator, "CreateParameterBuffer(FloatParams, floatParams)"),
        ("generateShaderGraphCodeSource", generator, "ResourceUtils.WriteDynamicBufferData<float>"),
        ("drawMeshCompositionSource", composition, "/*GenerateShaderGraphCode*/"),
        ("drawMeshCompositionSource", composition, "/*FragmentField*/"),
    ):
        if text is None:
            mismatches.append({"field": field, "expected": "readable source file", "actual": "missing"})
            continue
        if needle not in text:
            mismatches.append({"field": field, "expected": needle, "actual": "missing"})
    return mismatches


def validate_upstream_pointlights(pointlights: dict[str, Any]) -> list[dict[str, Any]]:
    mismatches: list[dict[str, Any]] = []
    if pointlights.get("kind") != "TixlMeshDrawPointLightsAndB5PackingVerdict":
        mismatches.append({"field": "pointlights.kind", "expected": "TixlMeshDrawPointLightsAndB5PackingVerdict", "actual": pointlights.get("kind")})
    if pointlights.get("ok") is not True:
        mismatches.append({"field": "pointlights.ok", "expected": True, "actual": pointlights.get("ok")})
    if pointlights.get("status") != "proven_b3_pointlights_packing_b5_blocked":
        mismatches.append({"field": "pointlights.status", "expected": "proven_b3_pointlights_packing_b5_blocked", "actual": pointlights.get("status")})
    claims = pointlights.get("claims") if isinstance(pointlights.get("claims"), dict) else {}
    expected_true = {
        "priorNativePackingArtifactConsumed",
        "constantBufferLayoutArtifactConsumed",
        "actualMetalPointLightProbeRan",
        "b3PointLightsPackingProven",
        "b5RequiresShadergraphParamExpansion",
    }
    for field in expected_true:
        if claims.get(field) is not True:
            mismatches.append({"field": f"pointlights.claims.{field}", "expected": True, "actual": claims.get(field)})
    for field in ("b5DuplicateParamsPackingProven", "constantBufferAdapterComplete", "textureSamplerMapping", "fullPbrResourceBinding", "backendReplacementReady", "hlslToMslTranslation", "tixlRuntimeParity", "pbrVisualCorrectness"):
        if claims.get(field) is not False:
            mismatches.append({"field": f"pointlights.claims.{field}", "expected": False, "actual": claims.get(field)})
    for field, value in claims.items():
        if field not in expected_true and value is True:
            mismatches.append({"field": f"pointlights.claims.{field}", "expected": "no extra true claims", "actual": True})
    return mismatches


def validate_layout_b5(layout: dict[str, Any], fields: list[dict[str, Any]]) -> list[dict[str, Any]]:
    mismatches: list[dict[str, Any]] = []
    b5 = find_layout_b5(layout)
    if b5.get("name") != "Params" or b5.get("semanticRole") != "shadergraph_duplicate_params":
        mismatches.append({"field": "constantBuffers.b5", "expected": {"name": "Params", "semanticRole": "shadergraph_duplicate_params"}, "actual": {"name": b5.get("name"), "semanticRole": b5.get("semanticRole")}})
    policy = layout.get("duplicateNamePolicy", {}).get("b5", {}) if isinstance(layout.get("duplicateNamePolicy"), dict) else {}
    if fields == []:
        if policy.get("fieldsKnownFromSourceAudit") is not False:
            mismatches.append({"field": "duplicateNamePolicy.b5.fieldsKnownFromSourceAudit", "expected": False, "actual": policy.get("fieldsKnownFromSourceAudit")})
    else:
        mismatches.extend(validate_field_provenance(fields, "constantBuffers.b5.fields"))
    return mismatches


def validate_source_audit_b5(source_audit: dict[str, Any], fields: list[dict[str, Any]]) -> list[dict[str, Any]]:
    if fields == []:
        return []
    return [{
        "field": "requiredBuffers.b5.fields",
        "expected": [],
        "actual": fields,
        "reason": "This lane cannot accept concrete b5 fields until a separate source-backed ShaderGraph expansion artifact exists.",
    }]


def validate_field_provenance(fields: list[dict[str, Any]], prefix: str) -> list[dict[str, Any]]:
    del prefix
    return [{
        "field": "constantBuffers.b5.fields",
        "expected": [],
        "actual": fields,
        "reason": "This lane cannot accept concrete b5 fields until a separate source-backed ShaderGraph expansion artifact exists.",
    }]


def result_payload(
    graph_id: Any,
    status: str,
    input_summary: dict[str, Any],
    source_facts: dict[str, Any],
    expansion: dict[str, Any],
    next_work: dict[str, Any],
    source_audit_consumed: bool,
    layout_consumed: bool,
    pointlights_consumed: bool,
    ok: bool = False,
    expanded: bool = False,
) -> dict[str, Any]:
    return {
        "kind": "TixlMeshDrawB5ShadergraphParamsExpansionVerdict",
        "graphId": graph_id,
        "ok": ok,
        "status": status,
        "inputArtifacts": input_summary,
        "sourceFacts": source_facts,
        "expansion": expansion,
        "nextWork": next_work,
        "claims": {
            "sourceAuditArtifactConsumed": source_audit_consumed,
            "constantBufferLayoutArtifactConsumed": layout_consumed,
            "pointlightsAndB5PackingArtifactConsumed": pointlights_consumed,
            "b3PointLightsPackingProven": pointlights_consumed,
            "b5ShadergraphParamsExpanded": expanded,
            "b5FieldsSourceBacked": expanded,
            "b5NativePackingReady": False,
            "constantBufferAdapterComplete": False,
            "textureSamplerMapping": False,
            "fullPbrResourceBinding": False,
            "backendReplacementReady": False,
            "hlslToMslTranslation": False,
            "tixlRuntimeParity": False,
            "pbrVisualCorrectness": False,
        },
    }


def expansion_verdict(source_fields: list[dict[str, Any]], layout_fields: list[dict[str, Any]]) -> dict[str, Any]:
    return {
        "register": "b5",
        "name": "Params",
        "semanticRole": "shadergraph_duplicate_params",
        "templateHole": "FLOAT_PARAMS",
        "sourceAuditFields": source_fields,
        "layoutFields": layout_fields,
        "expanded": bool(source_fields or layout_fields),
        "reason": "b5 Params is generated from ShaderGraph FLOAT_PARAMS; current artifacts do not provide concrete source-backed fields.",
    }


def source_fact_summary(template_path: Path, generator_path: Path, composition_path: Path, repo_root: Path) -> dict[str, Any]:
    return {
        "meshDrawTemplate": {
            "path": display_path(template_path, repo_root),
            "cbuffer": "Params",
            "register": "b5",
            "templateHole": "FLOAT_PARAMS",
        },
        "generator": {
            "path": display_path(generator_path, repo_root),
            "collectsParamsWith": "ShaderGraphNode.CollectAllNodeParams",
            "injectsParamsWith": "AssembleAndInjectParameters",
            "writesFloatBufferWith": "CreateParameterBuffer(FloatParams, floatParams)",
        },
        "composition": {
            "path": display_path(composition_path, repo_root),
            "fragmentFieldFeedsGenerateShaderGraphCode": True,
        },
    }


def b5_fields_from_source_audit(source_audit: dict[str, Any]) -> list[dict[str, Any]]:
    for item in source_audit.get("requiredBuffers", []):
        if isinstance(item, dict) and item.get("register") == "b5" and item.get("name") == "Params":
            fields = item.get("fields")
            return fields if isinstance(fields, list) else []
    return []


def find_layout_b5(layout: dict[str, Any]) -> dict[str, Any]:
    for item in layout.get("constantBuffers", []):
        if isinstance(item, dict) and item.get("register") == "b5":
            return item
    return {}


def b5_fields_from_layout(layout: dict[str, Any]) -> list[dict[str, Any]]:
    fields = find_layout_b5(layout).get("fields")
    return fields if isinstance(fields, list) else []


def summarize_artifact(path: Path, artifact: Any, repo_root: Path) -> dict[str, Any]:
    summary: dict[str, Any] = {"path": display_path(path, repo_root)}
    if isinstance(artifact, dict):
        for key in ("kind", "ok", "status", "selectedStrategy"):
            if key in artifact:
                summary[key] = artifact[key]
    return summary


def read_json(path: Path, errors: list[dict[str, Any]], code: str, repo_root: Path) -> Any:
    try:
        return json.loads(path.read_text(encoding="utf8"))
    except Exception as exc:
        errors.append({"code": code, "path": display_path(path, repo_root), "message": str(exc)})
        return None


def read_text(path: Path) -> str | None:
    try:
        return path.read_text(encoding="utf8")
    except Exception:
        return None


def resolve_path(repo_root: Path, fixture_path: Path, value: Any, default: str) -> Path:
    raw = value if isinstance(value, str) and value else default
    candidate = Path(raw).expanduser()
    if candidate.is_absolute():
        return candidate
    repo_candidate = (repo_root / candidate).resolve()
    if repo_candidate.exists():
        return repo_candidate
    return (fixture_path.parent / candidate).resolve()


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
        return str(path)


if __name__ == "__main__":
    raise SystemExit(main())

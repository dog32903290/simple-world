#!/usr/bin/env python3
"""
Classify TiXL mesh Draw b0-b5 constant-buffer layout facts.

This shell does not implement a shader, bind Metal buffers, map textures, or
replace a backend. It validates the selected explicit strategy plus the TiXL
source audit, then publishes the b0-b5 layout facts and a bounded partial
binding policy that still requires a native packing proof.
"""

from __future__ import annotations

import json
import sys
from pathlib import Path
from typing import Any


DEFAULT_SOURCE_AUDIT_ARTIFACT = "docs/runtime/artifacts/tixl_mesh_draw_shader_source_audit/tixl_mesh_draw_shader_source_audit_result.json"
DEFAULT_STRATEGY_ARTIFACT = "docs/runtime/artifacts/tixl_mesh_draw_explicit_translation_strategy/tixl_mesh_draw_explicit_translation_strategy_result.json"
RESULT_NAME = "tixl_mesh_draw_constant_buffer_layout_result.json"
TRACE_NAME = "tixl_mesh_draw_constant_buffer_layout_trace.json"
ERRORS_NAME = "tixl_mesh_draw_constant_buffer_layout_errors.json"

EXPECTED_CLAIMS = {
    "selectedStrategyConsumed": True,
    "constantBufferLayoutClassified": True,
    "constantBufferBindingPolicyReady": "bounded_partial",
    "b0b5LayoutNeedsNativePackingProof": True,
    "textureSamplerMapping": False,
    "fullPbrResourceBinding": False,
    "backendReplacementReady": False,
    "hlslToMslTranslation": False,
    "tixlRuntimeParity": False,
    "pbrVisualCorrectness": False,
}
ACCEPTANCE_GATE_CODES = [
    "valid_source_audit_artifact",
    "selected_handwritten_explicit_msl_adapter_consumed",
    "exact_b0_b5_layout_classified",
    "duplicate_params_disambiguated",
    "constant_buffer_policy_bounded_partial",
    "native_packing_proof_still_required",
    "no_texture_sampler_or_backend_claims",
]
EXPECTED_CONSTANT_BUFFERS = [
    {
        "register": "b0",
        "name": "Transforms",
        "semanticRole": "mesh_draw_transforms",
        "fields": [
            {"name": "CameraToClipSpace", "type": "float4x4"},
            {"name": "ClipSpaceToCamera", "type": "float4x4"},
            {"name": "WorldToCamera", "type": "float4x4"},
            {"name": "CameraToWorld", "type": "float4x4"},
            {"name": "WorldToClipSpace", "type": "float4x4"},
            {"name": "ClipSpaceToWorld", "type": "float4x4"},
            {"name": "ObjectToWorld", "type": "float4x4"},
            {"name": "WorldToObject", "type": "float4x4"},
            {"name": "ObjectToCamera", "type": "float4x4"},
            {"name": "ObjectToClipSpace", "type": "float4x4"},
        ],
    },
    {
        "register": "b1",
        "name": "Params",
        "semanticRole": "mesh_draw_material_params",
        "fields": [
            {"name": "Color", "type": "float4"},
            {"name": "AlphaCutOff", "type": "float"},
            {"name": "UseFlatShading", "type": "float"},
            {"name": "SpecularAA", "type": "float"},
        ],
    },
    {
        "register": "b2",
        "name": "FogParams",
        "semanticRole": "mesh_draw_fog_params",
        "fields": [
            {"name": "FogColor", "type": "float4"},
            {"name": "FogDistance", "type": "float"},
            {"name": "FogBias", "type": "float"},
        ],
    },
    {
        "register": "b3",
        "name": "PointLights",
        "semanticRole": "mesh_draw_point_lights",
        "fields": [
            {"name": "Lights", "type": "PointLight", "array": "[8]"},
            {"name": "ActiveLightCount", "type": "int"},
        ],
    },
    {
        "register": "b4",
        "name": "PbrParams",
        "semanticRole": "mesh_draw_pbr_params",
        "fields": [
            {"name": "BaseColor", "type": "float4"},
            {"name": "EmissiveColor", "type": "float4"},
            {"name": "Roughness", "type": "float"},
            {"name": "Specular", "type": "float"},
            {"name": "Metal", "type": "float"},
        ],
    },
    {
        "register": "b5",
        "name": "Params",
        "semanticRole": "shadergraph_duplicate_params",
        "fields": [],
    },
]


def main() -> int:
    if len(sys.argv) != 3:
        print("usage: tixl_mesh_draw_constant_buffer_layout_shell.py <fixture.graph.json> <out_dir>", file=sys.stderr)
        return 2

    fixture_path = Path(sys.argv[1]).expanduser().resolve()
    out_dir = Path(sys.argv[2]).expanduser().resolve()
    out_dir.mkdir(parents=True, exist_ok=True)
    repo_root = Path(__file__).resolve().parents[3]

    errors: list[dict[str, Any]] = []
    trace: list[dict[str, Any]] = [{
        "op": "loadTixlMeshDrawConstantBufferLayoutFixture",
        "fixture": display_path(fixture_path, repo_root),
    }]
    fixture = read_json(fixture_path, errors, "tixl_mesh_draw_constant_buffer_layout.fixture_read_failed", repo_root)
    if fixture is None:
        result = default_result(None, "blocked_missing_fixture", {}, {})
        publish(out_dir, result, trace, errors)
        return 1

    result, run_trace, run_errors = run_proof(repo_root, fixture_path, fixture)
    trace.extend(run_trace)
    errors.extend(run_errors)
    trace.append({
        "op": "publishTixlMeshDrawConstantBufferLayoutArtifacts",
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

    fixture_errors = validate_fixture_expectations(fixture)
    if fixture_errors:
        errors.append({
            "code": "tixl_mesh_draw_constant_buffer_layout.invalid_fixture_expectations",
            "message": "Fixture expectations must keep the constant-buffer lane bounded.",
            "mismatches": fixture_errors,
        })
        return default_result(graph_id, "blocked_invalid_fixture", {}, {}), trace, errors

    source_path = resolve_path(repo_root, fixture_path, fixture.get("sourceAuditArtifact"), DEFAULT_SOURCE_AUDIT_ARTIFACT)
    strategy_path = resolve_path(repo_root, fixture_path, fixture.get("strategyArtifact"), DEFAULT_STRATEGY_ARTIFACT)
    trace.append({
        "op": "resolveInputArtifacts",
        "sourceAuditArtifact": display_path(source_path, repo_root),
        "strategyArtifact": display_path(strategy_path, repo_root),
    })

    source = read_json(source_path, errors, "tixl_mesh_draw_constant_buffer_layout.source_audit_read_failed", repo_root)
    strategy = read_json(strategy_path, errors, "tixl_mesh_draw_constant_buffer_layout.strategy_read_failed", repo_root)
    source_summary = summarize_artifact(source_path, source, repo_root)
    strategy_summary = summarize_artifact(strategy_path, strategy, repo_root)
    trace.append({
        "op": "readInputArtifacts",
        "sourceAuditRead": source is not None,
        "strategyRead": strategy is not None,
    })
    if source is None or strategy is None:
        return default_result(graph_id, "blocked_missing_input_artifact", source_summary, strategy_summary), trace, errors

    source_errors = validate_source_audit(source)
    strategy_errors = validate_strategy(strategy)
    trace.append({
        "op": "validateInputArtifacts",
        "sourceAuditValid": not source_errors,
        "strategyValid": not strategy_errors,
    })
    if source_errors:
        errors.append({
            "code": "tixl_mesh_draw_constant_buffer_layout.invalid_source_audit_artifact",
            "message": "Source audit artifact lost required b0-b5 constant-buffer facts.",
            "mismatches": source_errors,
        })
    if strategy_errors:
        errors.append({
            "code": "tixl_mesh_draw_constant_buffer_layout.invalid_strategy_artifact",
            "message": "Explicit translation strategy artifact must stay selected and conservative.",
            "mismatches": strategy_errors,
        })
    if source_errors or strategy_errors:
        return default_result(graph_id, "blocked_invalid_input_artifact", source_summary, strategy_summary), trace, errors

    result = build_success_result(graph_id, source_summary, strategy_summary)
    trace.append({
        "op": "classifyConstantBufferLayout",
        "constantBufferCount": len(result["constantBuffers"]),
        "bindingPolicyReadiness": result["bindingPolicy"]["readiness"],
    })
    return result, trace, errors


def validate_fixture_expectations(fixture: dict[str, Any]) -> list[dict[str, Any]]:
    mismatches: list[dict[str, Any]] = []
    expected = fixture.get("expected") if isinstance(fixture.get("expected"), dict) else {}
    if expected.get("status") != "classified_tixl_mesh_draw_constant_buffer_layout":
        mismatches.append({"field": "expected.status", "expected": "classified_tixl_mesh_draw_constant_buffer_layout", "actual": expected.get("status")})
    if expected.get("claims") != EXPECTED_CLAIMS:
        mismatches.append({"field": "expected.claims", "expected": EXPECTED_CLAIMS, "actual": expected.get("claims")})
    if expected.get("acceptanceGateCodes") != ACCEPTANCE_GATE_CODES:
        mismatches.append({"field": "expected.acceptanceGateCodes", "expected": ACCEPTANCE_GATE_CODES, "actual": expected.get("acceptanceGateCodes")})
    if expected.get("constantBuffers") != EXPECTED_CONSTANT_BUFFERS:
        mismatches.append({"field": "expected.constantBuffers", "expected": EXPECTED_CONSTANT_BUFFERS, "actual": expected.get("constantBuffers")})
    return mismatches


def validate_source_audit(source: dict[str, Any]) -> list[dict[str, Any]]:
    mismatches: list[dict[str, Any]] = []
    if source.get("kind") != "TixlMeshDrawShaderSourceAudit":
        mismatches.append({"field": "kind", "expected": "TixlMeshDrawShaderSourceAudit", "actual": source.get("kind")})
    if source.get("ok") is not True:
        mismatches.append({"field": "ok", "expected": True, "actual": source.get("ok")})
    if source.get("status") != "audited_tixl_mesh_draw_source":
        mismatches.append({"field": "status", "expected": "audited_tixl_mesh_draw_source", "actual": source.get("status")})

    buffers = {(item.get("register"), item.get("name")) for item in list_items(source.get("requiredBuffers"))}
    expected_buffers = {(item["register"], item["name"]) for item in EXPECTED_CONSTANT_BUFFERS}
    for buffer in EXPECTED_CONSTANT_BUFFERS:
        expected = (buffer["register"], buffer["name"])
        if expected not in buffers:
            mismatches.append({"field": f"requiredBuffers.{buffer['register']}", "expected": {"register": buffer["register"], "name": buffer["name"]}})
    for register, name in sorted(buffers - expected_buffers):
        if str(register).startswith("b"):
            mismatches.append({"field": "requiredBuffers.extra", "actual": {"register": register, "name": name}})

    constants = {
        (item.get("register"), item.get("buffer"), item.get("name")): item
        for item in list_items(source.get("constants"))
    }
    expected_constant_keys: set[tuple[Any, Any, Any]] = set()
    for buffer in EXPECTED_CONSTANT_BUFFERS:
        for field in buffer["fields"]:
            key = (buffer["register"], buffer["name"], field["name"])
            expected_constant_keys.add(key)
            actual = constants.get(key)
            if actual is None:
                mismatches.append({"field": f"constants.{buffer['register']}.{field['name']}", "expected": field})
                continue
            if actual.get("type") != field["type"]:
                mismatches.append({"field": f"constants.{buffer['register']}.{field['name']}.type", "expected": field["type"], "actual": actual.get("type")})
            expected_array = field.get("array")
            actual_array = actual.get("array")
            if actual_array != expected_array:
                mismatches.append({"field": f"constants.{buffer['register']}.{field['name']}.array", "expected": expected_array, "actual": actual_array})
    for key, actual in sorted(constants.items()):
        register, _buffer, name = key
        if str(register).startswith("b") and key not in expected_constant_keys:
            mismatches.append({"field": "constants.extra", "actual": {"register": register, "name": name, "type": actual.get("type")}})

    claims = source.get("claims") if isinstance(source.get("claims"), dict) else {}
    for field in ("hlslToMslTranslationProven", "tixlParity", "nativeCompileParity", "pbrVisualCorrectness"):
        if claims.get(field) is not False:
            mismatches.append({"field": f"claims.{field}", "expected": False, "actual": claims.get(field)})
    for field, value in claims.items():
        if field not in {"hlslToMslTranslationProven", "tixlParity", "nativeCompileParity", "pbrVisualCorrectness"} and value is True:
            mismatches.append({"field": f"claims.{field}", "expected": "not true in source audit input", "actual": value})
    return mismatches


def validate_strategy(strategy: dict[str, Any]) -> list[dict[str, Any]]:
    mismatches: list[dict[str, Any]] = []
    if strategy.get("kind") != "TixlMeshDrawExplicitTranslationStrategy":
        mismatches.append({"field": "kind", "expected": "TixlMeshDrawExplicitTranslationStrategy", "actual": strategy.get("kind")})
    if strategy.get("ok") is not True:
        mismatches.append({"field": "ok", "expected": True, "actual": strategy.get("ok")})
    if strategy.get("status") != "selected_handwritten_explicit_msl_adapter":
        mismatches.append({"field": "status", "expected": "selected_handwritten_explicit_msl_adapter", "actual": strategy.get("status")})
    if strategy.get("selectedStrategy") != "handwritten_explicit_msl_adapter":
        mismatches.append({"field": "selectedStrategy", "expected": "handwritten_explicit_msl_adapter", "actual": strategy.get("selectedStrategy")})

    claims = strategy.get("claims") if isinstance(strategy.get("claims"), dict) else {}
    expected_claims = {
        "mechanicalTranslationRejected": True,
        "selectedStrategy": "handwritten_explicit_msl_adapter",
        "fullCrossCompilerSelected": False,
        "explicitAdapterReadyForFullPbr": False,
        "fullPbrResourceBinding": False,
        "backendReplacementReady": False,
        "tixlRuntimeParity": False,
        "hlslToMslTranslation": False,
        "pbrVisualCorrectness": False,
    }
    for field, expected in expected_claims.items():
        if claims.get(field) != expected:
            mismatches.append({"field": f"claims.{field}", "expected": expected, "actual": claims.get(field)})
    for field, value in claims.items():
        if field not in expected_claims and value is True:
            mismatches.append({"field": f"claims.{field}", "expected": "not true in strategy input", "actual": value})
    return mismatches


def build_success_result(
    graph_id: Any,
    source_summary: dict[str, Any],
    strategy_summary: dict[str, Any],
) -> dict[str, Any]:
    return {
        "kind": "TixlMeshDrawConstantBufferLayoutProof",
        "graphId": graph_id,
        "ok": True,
        "status": "classified_tixl_mesh_draw_constant_buffer_layout",
        "selectedStrategy": "handwritten_explicit_msl_adapter",
        "inputArtifacts": {
            "sourceAudit": source_summary,
            "explicitTranslationStrategy": strategy_summary,
        },
        "acceptanceGates": acceptance_gates(),
        "constantBufferRegisters": ["b0", "b1", "b2", "b3", "b4", "b5"],
        "constantBuffers": EXPECTED_CONSTANT_BUFFERS,
        "duplicateNamePolicy": {
            "name": "Params",
            "b1": {
                "register": "b1",
                "semanticRole": "mesh_draw_material_params",
            },
            "b5": {
                "register": "b5",
                "semanticRole": "shadergraph_duplicate_params",
                "disambiguatedFrom": "b1:Params",
                "fieldsKnownFromSourceAudit": False,
            },
        },
        "bindingPolicy": {
            "readiness": "bounded_partial",
            "reservedAfterMeshBuffers": ["PbrVertices t0 -> buffer(0)", "FaceIndices t1 -> buffer(1)"],
            "reservedMetalBufferRange": [2, 7],
            "candidateMapping": [
                {"sourceRegister": "b0", "sourceName": "Transforms", "metalBuffer": 2},
                {"sourceRegister": "b1", "sourceName": "Params", "semanticRole": "mesh_draw_material_params", "metalBuffer": 3},
                {"sourceRegister": "b2", "sourceName": "FogParams", "metalBuffer": 4},
                {"sourceRegister": "b3", "sourceName": "PointLights", "metalBuffer": 5},
                {"sourceRegister": "b4", "sourceName": "PbrParams", "metalBuffer": 6},
                {"sourceRegister": "b5", "sourceName": "Params", "semanticRole": "shadergraph_duplicate_params", "metalBuffer": 7},
            ],
            "nativePackingProofRequired": True,
            "backendBindingImplemented": False,
            "textureSamplerMappingIncluded": False,
        },
        "semanticBlockers": [
            {
                "code": "b0_b5_layout_needs_native_packing_proof",
                "reason": "Source audit records HLSL constants, but no native Metal struct packing probe has verified offsets or alignment.",
            },
            {
                "code": "texture_sampler_mapping_not_in_scope",
                "reason": "This lane classifies cbuffers only; t2-t7 textures and s0-s1 samplers remain pending.",
            },
            {
                "code": "backend_replacement_not_ready",
                "reason": "The bounded partial policy is not a backend binding implementation.",
            },
        ],
        "claims": EXPECTED_CLAIMS,
    }


def acceptance_gates() -> list[dict[str, Any]]:
    return [
        {"code": "valid_source_audit_artifact", "passed": True},
        {"code": "selected_handwritten_explicit_msl_adapter_consumed", "passed": True},
        {"code": "exact_b0_b5_layout_classified", "passed": True},
        {"code": "duplicate_params_disambiguated", "passed": True},
        {"code": "constant_buffer_policy_bounded_partial", "passed": True},
        {"code": "native_packing_proof_still_required", "passed": True},
        {"code": "no_texture_sampler_or_backend_claims", "passed": True},
    ]


def default_result(
    graph_id: Any,
    status: str,
    source_summary: dict[str, Any],
    strategy_summary: dict[str, Any],
) -> dict[str, Any]:
    return {
        "kind": "TixlMeshDrawConstantBufferLayoutProof",
        "graphId": graph_id,
        "ok": False,
        "status": status,
        "selectedStrategy": None,
        "inputArtifacts": {
            "sourceAudit": source_summary,
            "explicitTranslationStrategy": strategy_summary,
        },
        "constantBuffers": [],
        "acceptanceGates": [],
        "claims": {
            "selectedStrategyConsumed": False,
            "constantBufferLayoutClassified": False,
            "constantBufferBindingPolicyReady": False,
            "b0b5LayoutNeedsNativePackingProof": True,
            "textureSamplerMapping": False,
            "fullPbrResourceBinding": False,
            "backendReplacementReady": False,
            "hlslToMslTranslation": False,
            "tixlRuntimeParity": False,
            "pbrVisualCorrectness": False,
        },
    }


def summarize_artifact(path: Path, artifact: Any, repo_root: Path) -> dict[str, Any]:
    summary = {"path": display_path(path, repo_root)}
    if isinstance(artifact, dict):
        for key in ("kind", "ok", "status"):
            if key in artifact:
                summary[key] = artifact[key]
    return summary


def resolve_path(repo_root: Path, fixture_path: Path, value: Any, default: str) -> Path:
    raw = value if isinstance(value, str) and value else default
    candidate = Path(raw).expanduser()
    if candidate.is_absolute():
        return candidate
    fixture_relative = (fixture_path.parent / candidate).resolve()
    if fixture_relative.exists():
        return fixture_relative
    return (repo_root / candidate).resolve()


def read_json(path: Path, errors: list[dict[str, Any]], code: str, repo_root: Path) -> Any:
    try:
        return json.loads(path.read_text(encoding="utf8"))
    except Exception as exc:
        errors.append({"code": code, "path": display_path(path, repo_root), "message": str(exc)})
        return None


def publish(out_dir: Path, result: dict[str, Any], trace: list[dict[str, Any]], errors: list[dict[str, Any]]) -> None:
    write_json(out_dir / RESULT_NAME, result)
    write_json(out_dir / TRACE_NAME, trace)
    write_json(out_dir / ERRORS_NAME, errors)


def write_json(path: Path, value: Any) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, indent=2) + "\n", encoding="utf8")


def display_path(path: Path, repo_root: Path) -> str:
    try:
        return path.resolve().relative_to(repo_root.resolve()).as_posix()
    except ValueError:
        return path.as_posix()


def list_items(value: Any) -> list[dict[str, Any]]:
    return value if isinstance(value, list) else []


if __name__ == "__main__":
    raise SystemExit(main())

#!/usr/bin/env python3
"""
Classify TiXL mesh Draw HLSL-to-MSL translation risk from existing artifacts.

This shell intentionally does not translate HLSL, emit MSL, compile, render, or
replace a backend. It only validates prior proof artifacts and publishes a
fail-closed verdict for mechanical mesh draw parity.
"""

from __future__ import annotations

import json
import sys
from pathlib import Path
from typing import Any


DEFAULT_SOURCE_AUDIT_ARTIFACT = "docs/runtime/artifacts/tixl_mesh_draw_shader_source_audit/tixl_mesh_draw_shader_source_audit_result.json"
DEFAULT_RESOURCE_BINDING_ARTIFACT = "docs/runtime/artifacts/tixl_mesh_draw_resource_binding/tixl_mesh_draw_resource_binding_result.json"
RESULT_NAME = "tixl_mesh_draw_hlsl_to_msl_verdict_result.json"
TRACE_NAME = "tixl_mesh_draw_hlsl_to_msl_verdict_trace.json"
ERRORS_NAME = "tixl_mesh_draw_hlsl_to_msl_verdict_errors.json"

EXPECTED_CBUFFERS = [
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
EXPECTED_TEMPLATE_HOLES = {"FLOAT_PARAMS", "GLOBALS", "RESOURCES(t8)", "FIELD_FUNCTIONS", "FIELD_CALL"}
REQUIRED_BLOCKER_CODES = [
    "cbuffer_register_set_requires_explicit_layout_policy",
    "texture_and_cube_register_set_requires_resource_mapping",
    "sampler_register_set_requires_sampler_mapping",
    "template_resources_t8_plus_require_tixl_expansion",
    "template_holes_require_tixl_shadergraph_expansion",
    "duplicate_params_cbuffer_requires_disambiguation",
    "global_frag_state_requires_rewrite",
    "derivatives_require_fragment_stage_mapping",
    "discard_requires_fragment_control_flow_mapping",
    "mrt_sv_target_outputs_require_render_target_contract",
    "system_semantics_require_stage_attribute_mapping",
    "d3d_mul_order_requires_matrix_convention_proof",
    "texturecube_samplelevel_getdimensions_requires_msl_texture_mapping",
    "pbr_visual_reference_missing",
    "resource_binding_proof_is_partial",
]
EXPECTED_SEMANTIC_BLOCKERS = {
    "requires_hlsl_to_msl_translation_lane",
    "requires_native_mesh_resource_binding",
    "requires_pbr_visual_reference",
    "shader_template_holes_require_tixl_expansion",
    "duplicate_buffer_names_need_binding_policy",
}
EXPECTED_STRUCTS = {"FragmentMaterial", "PbrVertex", "PointLight", "psInput", "psOutput"}
EXPECTED_FUNCTIONS = {
    "ComputeNormal",
    "ComputePbr",
    "GetField",
    "psMain",
    "querySpecularTextureLevels",
    "vsMain",
}


def main() -> int:
    if len(sys.argv) != 3:
        print("usage: tixl_mesh_draw_hlsl_to_msl_verdict_shell.py <fixture.graph.json> <out_dir>", file=sys.stderr)
        return 2

    fixture_path = Path(sys.argv[1]).expanduser().resolve()
    out_dir = Path(sys.argv[2]).expanduser().resolve()
    out_dir.mkdir(parents=True, exist_ok=True)
    repo_root = Path(__file__).resolve().parents[3]

    errors: list[dict[str, Any]] = []
    trace: list[dict[str, Any]] = [{
        "op": "loadTixlMeshDrawHlslToMslVerdictFixture",
        "fixture": display_path(fixture_path, repo_root),
    }]
    fixture = read_json(fixture_path, errors, "tixl_mesh_draw_hlsl_to_msl_verdict.fixture_read_failed", repo_root)
    if fixture is None:
        result = default_result(None, "blocked_missing_fixture", {}, {})
        publish(out_dir, result, trace, errors)
        return 1

    result, run_trace, run_errors = run_verdict(repo_root, fixture_path, fixture)
    trace.extend(run_trace)
    errors.extend(run_errors)
    trace.append({
        "op": "publishTixlMeshDrawHlslToMslVerdictArtifacts",
        "ok": result.get("ok") is True and not errors,
    })
    publish(out_dir, result, trace, errors)
    return 0 if result.get("ok") is True and not errors else 1


def run_verdict(
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
            "code": "tixl_mesh_draw_hlsl_to_msl_verdict.invalid_fixture_expected_blockers",
            "message": "Fixture expected claims/blockers must match the conservative verdict lane.",
            "mismatches": fixture_errors,
        })
        return default_result(graph_id, "blocked_invalid_fixture", {}, {}), trace, errors

    source_path = resolve_path(repo_root, fixture_path, fixture.get("sourceAuditArtifact"), DEFAULT_SOURCE_AUDIT_ARTIFACT)
    binding_path = resolve_path(repo_root, fixture_path, fixture.get("resourceBindingArtifact"), DEFAULT_RESOURCE_BINDING_ARTIFACT)
    trace.append({
        "op": "resolveInputArtifacts",
        "sourceAuditArtifact": display_path(source_path, repo_root),
        "resourceBindingArtifact": display_path(binding_path, repo_root),
    })

    source = read_json(source_path, errors, "tixl_mesh_draw_hlsl_to_msl_verdict.source_audit_read_failed", repo_root)
    binding = read_json(binding_path, errors, "tixl_mesh_draw_hlsl_to_msl_verdict.resource_binding_read_failed", repo_root)
    source_summary = summarize_artifact(source_path, source, repo_root)
    binding_summary = summarize_artifact(binding_path, binding, repo_root)
    trace.append({
        "op": "readInputArtifacts",
        "sourceAuditRead": source is not None,
        "resourceBindingRead": binding is not None,
    })
    if source is None or binding is None:
        return default_result(graph_id, "blocked_missing_input_artifact", source_summary, binding_summary), trace, errors

    source_errors = validate_source_audit(source)
    binding_errors = validate_resource_binding(binding)
    trace.append({
        "op": "validateInputArtifacts",
        "sourceAuditValid": not source_errors,
        "resourceBindingValid": not binding_errors,
    })
    if source_errors:
        errors.append({
            "code": "tixl_mesh_draw_hlsl_to_msl_verdict.invalid_source_audit_artifact",
            "message": "Source audit artifact does not contain the required mesh draw translation risk facts.",
            "mismatches": source_errors,
        })
    if binding_errors:
        errors.append({
            "code": "tixl_mesh_draw_hlsl_to_msl_verdict.invalid_resource_binding_artifact",
            "message": "Resource binding artifact widened claims or lost required binding ledger facts.",
            "mismatches": binding_errors,
        })
    if source_errors or binding_errors:
        return default_result(graph_id, "blocked_invalid_input_artifact", source_summary, binding_summary), trace, errors

    result = build_success_result(graph_id, source_summary, binding_summary, source, binding)
    result_errors = validate_result_blockers(result)
    if result_errors:
        errors.append({
            "code": "tixl_mesh_draw_hlsl_to_msl_verdict.result_missing_blocker",
            "message": "Generated verdict omitted required blocker facts.",
            "mismatches": result_errors,
        })
        return default_result(graph_id, "blocked_internal_verdict_mismatch", source_summary, binding_summary), trace, errors

    trace.append({
        "op": "buildHlslToMslVerdict",
        "mechanicalTranslationStatus": result["mechanicalTranslationStatus"],
        "blockerCount": len(result["blockerFacts"]),
    })
    return result, trace, errors


def validate_fixture_expectations(fixture: dict[str, Any]) -> list[dict[str, Any]]:
    mismatches: list[dict[str, Any]] = []
    expected = fixture.get("expected") if isinstance(fixture.get("expected"), dict) else {}
    if expected.get("status") != "rejected_for_mesh_draw_parity":
        mismatches.append({"field": "expected.status", "expected": "rejected_for_mesh_draw_parity", "actual": expected.get("status")})
    if expected.get("mechanicalTranslationStatus") != "rejected_for_mesh_draw_parity":
        mismatches.append({"field": "expected.mechanicalTranslationStatus", "expected": "rejected_for_mesh_draw_parity", "actual": expected.get("mechanicalTranslationStatus")})
    if expected.get("minimumBlockerCount") != len(REQUIRED_BLOCKER_CODES):
        mismatches.append({"field": "expected.minimumBlockerCount", "expected": len(REQUIRED_BLOCKER_CODES), "actual": expected.get("minimumBlockerCount")})
    actual_codes = expected.get("requiredBlockerCodes")
    if actual_codes != REQUIRED_BLOCKER_CODES:
        mismatches.append({"field": "expected.requiredBlockerCodes", "expected": REQUIRED_BLOCKER_CODES, "actual": actual_codes})
    claims = expected.get("claims") if isinstance(expected.get("claims"), dict) else {}
    for field, value in claim_flags(True).items():
        if claims.get(field) is not value:
            mismatches.append({"field": f"expected.claims.{field}", "expected": value, "actual": claims.get(field)})
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
    expected_buffers = set(EXPECTED_CBUFFERS)
    for register, name in EXPECTED_CBUFFERS:
        if (register, name) not in buffers:
            mismatches.append({"field": "requiredBuffers", "expected": {"register": register, "name": name}})
    for register, name in sorted(buffers - expected_buffers):
        mismatches.append({"field": "requiredBuffers.extra", "actual": {"register": register, "name": name}})

    resources = {(item.get("register"), item.get("name"), item.get("kind")) for item in list_items(source.get("resources"))}
    expected_resources = set(EXPECTED_RESOURCES)
    for register, name, kind in EXPECTED_RESOURCES:
        if (register, name, kind) not in resources:
            mismatches.append({"field": "resources", "expected": {"register": register, "name": name, "kind": kind}})
    for register, name, kind in sorted(resources - expected_resources):
        mismatches.append({"field": "resources.extra", "actual": {"register": register, "name": name, "kind": kind}})

    samplers = {(item.get("register"), item.get("name")) for item in list_items(source.get("samplers"))}
    expected_samplers = set(EXPECTED_SAMPLERS)
    for register, name in EXPECTED_SAMPLERS:
        if (register, name) not in samplers:
            mismatches.append({"field": "samplers", "expected": {"register": register, "name": name}})
    for register, name in sorted(samplers - expected_samplers):
        mismatches.append({"field": "samplers.extra", "actual": {"register": register, "name": name}})

    raw_template_holes = source.get("templateHoles")
    if not isinstance(raw_template_holes, list):
        mismatches.append({"field": "templateHoles", "expected": sorted(EXPECTED_TEMPLATE_HOLES), "actual": type(raw_template_holes).__name__})
    template_holes = {item.get("name") for item in list_items(raw_template_holes)}
    for name in sorted(EXPECTED_TEMPLATE_HOLES):
        if name not in template_holes:
            mismatches.append({"field": "templateHoles", "expected": name})
    for name in sorted(template_holes - EXPECTED_TEMPLATE_HOLES):
        mismatches.append({"field": "templateHoles.extra", "actual": name})

    semantic_codes = {item.get("code") for item in list_items(source.get("semanticBlockers"))}
    for code in sorted(EXPECTED_SEMANTIC_BLOCKERS):
        if code not in semantic_codes:
            mismatches.append({"field": "semanticBlockers", "expected": code})
    for code in sorted(semantic_codes - EXPECTED_SEMANTIC_BLOCKERS):
        mismatches.append({"field": "semanticBlockers.extra", "actual": code})

    symbol_summary = source.get("symbolSummary") if isinstance(source.get("symbolSummary"), dict) else {}
    structs = set(symbol_summary.get("structs") if isinstance(symbol_summary.get("structs"), list) else [])
    functions = set(symbol_summary.get("functions") if isinstance(symbol_summary.get("functions"), list) else [])
    for name in sorted(EXPECTED_STRUCTS):
        if name not in structs:
            mismatches.append({"field": "symbolSummary.structs", "expected": name})
    for name in sorted(EXPECTED_FUNCTIONS):
        if name not in functions:
            mismatches.append({"field": "symbolSummary.functions", "expected": name})

    claims = source.get("claims") if isinstance(source.get("claims"), dict) else {}
    for field in ("hlslToMslTranslationProven", "tixlParity", "nativeCompileParity", "pbrVisualCorrectness"):
        if claims.get(field) is not False:
            mismatches.append({"field": f"claims.{field}", "expected": False, "actual": claims.get(field)})
    for field, value in claims.items():
        if field not in {"hlslToMslTranslationProven", "tixlParity", "nativeCompileParity", "pbrVisualCorrectness"} and value is True:
            mismatches.append({"field": f"claims.{field}", "expected": "not true in source audit input", "actual": value})
    return mismatches


def validate_resource_binding(binding: dict[str, Any]) -> list[dict[str, Any]]:
    mismatches: list[dict[str, Any]] = []
    if binding.get("kind") != "TixlMeshDrawResourceBindingProof":
        mismatches.append({"field": "kind", "expected": "TixlMeshDrawResourceBindingProof", "actual": binding.get("kind")})
    if binding.get("ok") is not True:
        mismatches.append({"field": "ok", "expected": True, "actual": binding.get("ok")})
    if binding.get("status") != "summarized_tixl_mesh_draw_resource_binding":
        mismatches.append({"field": "status", "expected": "summarized_tixl_mesh_draw_resource_binding", "actual": binding.get("status")})

    claims = binding.get("claims") if isinstance(binding.get("claims"), dict) else {}
    expected_claims = {
        "meshBufferBindingObserved": True,
        "fullPbrResourceBinding": False,
        "hlslToMslTranslation": False,
        "tixlRuntimeParity": False,
        "backendReplacementReady": False,
    }
    for field, expected in expected_claims.items():
        if claims.get(field) is not expected:
            mismatches.append({"field": f"claims.{field}", "expected": expected, "actual": claims.get(field)})
    for field, value in claims.items():
        if field not in expected_claims and value is True:
            mismatches.append({"field": f"claims.{field}", "expected": "not true in resource binding input", "actual": value})

    ledger = binding.get("bindingLedger") if isinstance(binding.get("bindingLedger"), dict) else {}
    bound = {(item.get("sourceRegister"), item.get("sourceName")) for item in list_items(ledger.get("boundNow"))}
    if ("t0", "PbrVertices") not in bound:
        mismatches.append({"field": "bindingLedger.boundNow", "expected": {"sourceRegister": "t0", "sourceName": "PbrVertices"}})
    if ("t1", "FaceIndices") not in bound:
        mismatches.append({"field": "bindingLedger.boundNow", "expected": {"sourceRegister": "t1", "sourceName": "FaceIndices"}})

    unbound = {(item.get("sourceRegister"), item.get("sourceName")) for item in list_items(ledger.get("declaredButUnbound"))}
    for register, name in [
        *EXPECTED_CBUFFERS,
        ("t2", "BaseColorMap"),
        ("t3", "EmissiveColorMap"),
        ("t4", "RSMOMap"),
        ("t5", "NormalMap"),
        ("t6", "PrefilteredSpecular"),
        ("t7", "BRDFLookup"),
        ("s0", "WrappedSampler"),
        ("s1", "ClampedSampler"),
        ("t8+", "injected resources"),
        ("FLOAT_PARAMS", "shadergraph float params"),
        ("GLOBALS", "shadergraph globals"),
        ("FIELD_FUNCTIONS", "shadergraph field functions"),
        ("FIELD_CALL", "shadergraph field call"),
    ]:
        if (register, name) not in unbound:
            mismatches.append({"field": "bindingLedger.declaredButUnbound", "expected": {"sourceRegister": register, "sourceName": name}})

    input_artifacts = binding.get("inputArtifacts") if isinstance(binding.get("inputArtifacts"), dict) else {}
    expected_inputs = {
        "sourceAudit": ("TixlMeshDrawShaderSourceAudit", "audited_tixl_mesh_draw_source"),
        "bufferLayout": ("TixlMeshDrawBufferLayoutProof", "summarized_tixl_mesh_draw_buffer_layout"),
        "mslApprox": ("TixlMeshDrawMslApproxProof", "rendered_tixl_mesh_draw_msl_approximation"),
    }
    for key, (kind, status) in expected_inputs.items():
        artifact = input_artifacts.get(key) if isinstance(input_artifacts.get(key), dict) else {}
        if artifact.get("kind") != kind:
            mismatches.append({"field": f"inputArtifacts.{key}.kind", "expected": kind, "actual": artifact.get("kind")})
        if artifact.get("status") != status:
            mismatches.append({"field": f"inputArtifacts.{key}.status", "expected": status, "actual": artifact.get("status")})
        if artifact.get("ok") is not True:
            mismatches.append({"field": f"inputArtifacts.{key}.ok", "expected": True, "actual": artifact.get("ok")})

    evidence = binding.get("evidence") if isinstance(binding.get("evidence"), dict) else {}
    expected_evidence = {
        "mslApproxStatus": "rendered_tixl_mesh_draw_msl_approximation",
        "frameDigest": "9c09adf221b57b49",
        "controlFrameDigest": "7da9a417bf722b83",
        "nonBlackPixels": 84,
        "controlNonBlackPixels": 0,
        "mslApproxBufferPackingObserved": True,
    }
    for field, expected in expected_evidence.items():
        if evidence.get(field) != expected:
            mismatches.append({"field": f"evidence.{field}", "expected": expected, "actual": evidence.get(field)})
    if evidence.get("frameDigest") == evidence.get("controlFrameDigest"):
        mismatches.append({"field": "evidence.frameDigest", "expected": "different from controlFrameDigest", "actual": evidence.get("frameDigest")})
    return mismatches


def build_success_result(
    graph_id: Any,
    source_summary: dict[str, Any],
    binding_summary: dict[str, Any],
    source: dict[str, Any],
    binding: dict[str, Any],
) -> dict[str, Any]:
    binding_claims = binding.get("claims", {})
    return {
        "kind": "TixlMeshDrawHlslToMslTranslationVerdict",
        "graphId": graph_id,
        "ok": True,
        "status": "rejected_for_mesh_draw_parity",
        "mechanicalTranslationStatus": "rejected_for_mesh_draw_parity",
        "verdict": "reject_mechanical_hlsl_to_msl_for_mesh_draw_parity",
        "message": "existing source and binding proof facts reject mechanical HLSL-to-MSL mesh draw parity",
        "inputArtifacts": {
            "sourceAudit": source_summary,
            "resourceBinding": binding_summary,
        },
        "evidence": {
            "sourceAudit": {
                "status": source.get("status"),
                "requiredBufferRegisters": [register for register, _name in EXPECTED_CBUFFERS],
                "resourceRegisters": [register for register, _name, _kind in EXPECTED_RESOURCES],
                "samplerRegisters": [register for register, _name in EXPECTED_SAMPLERS],
                "templateHoles": sorted(EXPECTED_TEMPLATE_HOLES),
            },
            "resourceBinding": {
                "status": binding.get("status"),
                "meshBufferBindingObserved": binding_claims.get("meshBufferBindingObserved"),
                "fullPbrResourceBinding": binding_claims.get("fullPbrResourceBinding"),
                "hlslToMslTranslation": binding_claims.get("hlslToMslTranslation"),
                "backendReplacementReady": binding_claims.get("backendReplacementReady"),
            },
        },
        "blockerFacts": blocker_facts(),
        "claims": claim_flags(True),
    }


def blocker_facts() -> list[dict[str, Any]]:
    return [
        {
            "code": "cbuffer_register_set_requires_explicit_layout_policy",
            "registers": ["b0", "b1", "b2", "b3", "b4", "b5"],
            "names": ["Transforms", "Params", "FogParams", "PointLights", "PbrParams", "Params"],
            "reason": "Metal constant-buffer layout and binding policy has not been proven for the full TiXL set.",
        },
        {
            "code": "texture_and_cube_register_set_requires_resource_mapping",
            "registers": ["t0", "t1", "t2", "t3", "t4", "t5", "t6", "t7"],
            "names": ["PbrVertices", "FaceIndices", "BaseColorMap", "EmissiveColorMap", "RSMOMap", "NormalMap", "PrefilteredSpecular", "BRDFLookup"],
            "reason": "Only t0/t1 buffer binding has been observed; texture and cube resources are not proven.",
        },
        {
            "code": "sampler_register_set_requires_sampler_mapping",
            "registers": ["s0", "s1"],
            "names": ["WrappedSampler", "ClampedSampler"],
            "reason": "Sampler-state translation and Metal binding have not been proven.",
        },
        {
            "code": "template_resources_t8_plus_require_tixl_expansion",
            "registers": ["t8+"],
            "holes": ["RESOURCES(t8)"],
            "reason": "Injected shadergraph resources require TiXL expansion before resource mapping can be complete.",
        },
        {
            "code": "template_holes_require_tixl_shadergraph_expansion",
            "holes": ["FLOAT_PARAMS", "GLOBALS", "FIELD_FUNCTIONS", "FIELD_CALL"],
            "reason": "The donor shader has unexpanded TiXL shadergraph template holes.",
        },
        {
            "code": "duplicate_params_cbuffer_requires_disambiguation",
            "registers": ["b1", "b5"],
            "names": ["Params"],
            "reason": "Two Params cbuffers need an explicit naming and binding policy.",
        },
        {
            "code": "global_frag_state_requires_rewrite",
            "symbols": ["frag"],
            "reason": "PBR helpers rely on mutable global fragment state that needs an explicit MSL rewrite policy.",
        },
        {
            "code": "derivatives_require_fragment_stage_mapping",
            "symbols": ["ddx", "ddy"],
            "reason": "Derivative semantics must be proven in the Metal fragment stage.",
        },
        {
            "code": "discard_requires_fragment_control_flow_mapping",
            "symbols": ["discard"],
            "reason": "Alpha-test discard needs Metal fragment control-flow parity proof.",
        },
        {
            "code": "mrt_sv_target_outputs_require_render_target_contract",
            "semantics": ["SV_Target0", "SV_Target1"],
            "reason": "Multiple render targets require an explicit native render target contract.",
        },
        {
            "code": "system_semantics_require_stage_attribute_mapping",
            "semantics": ["SV_VertexID", "VPOS", "SV_POSITION"],
            "reason": "D3D system semantics require explicit Metal stage attribute mapping.",
        },
        {
            "code": "d3d_mul_order_requires_matrix_convention_proof",
            "symbols": ["mul(vector, matrix)"],
            "reason": "D3D matrix/vector multiplication order must be proven against Metal conventions.",
        },
        {
            "code": "texturecube_samplelevel_getdimensions_requires_msl_texture_mapping",
            "symbols": ["TextureCube", "SampleLevel", "GetDimensions"],
            "reason": "Cube texture sampling and mip dimension queries require Metal texture API mapping proof.",
        },
        {
            "code": "pbr_visual_reference_missing",
            "sourceBlocker": "requires_pbr_visual_reference",
            "reason": "PBR lighting and IBL resources are summarized, but no visual reference or comparison has been rendered.",
        },
        {
            "code": "resource_binding_proof_is_partial",
            "boundNow": ["PbrVertices t0", "FaceIndices t1"],
            "unboundFamilies": ["b0-b5", "t2-t7", "s0-s1", "t8+", "template holes"],
            "reason": "The current resource binding proof intentionally observes only packed mesh buffers.",
        },
    ]


def validate_result_blockers(result: dict[str, Any]) -> list[dict[str, Any]]:
    codes = [fact.get("code") for fact in list_items(result.get("blockerFacts"))]
    if codes != REQUIRED_BLOCKER_CODES:
        return [{"field": "blockerFacts", "expected": REQUIRED_BLOCKER_CODES, "actual": codes}]
    return []


def default_result(
    graph_id: Any,
    status: str,
    source_summary: dict[str, Any],
    binding_summary: dict[str, Any],
) -> dict[str, Any]:
    return {
        "kind": "TixlMeshDrawHlslToMslTranslationVerdict",
        "graphId": graph_id,
        "ok": False,
        "status": status,
        "mechanicalTranslationStatus": "blocked",
        "verdict": "not_proven",
        "inputArtifacts": {
            "sourceAudit": source_summary,
            "resourceBinding": binding_summary,
        },
        "blockerFacts": [],
        "claims": claim_flags(False),
    }


def claim_flags(classified: bool) -> dict[str, bool]:
    return {
        "translationRiskClassified": classified,
        "mechanicalTranslationForMeshDrawParity": False,
        "hlslToMslTranslation": False,
        "fullPbrResourceBinding": False,
        "tixlRuntimeParity": False,
        "pbrVisualCorrectness": False,
        "backendReplacementReady": False,
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

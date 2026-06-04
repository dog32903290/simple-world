#!/usr/bin/env python3
"""
Publish a source-backed proof for TiXL mesh Draw ShaderGraph resource expansion.

This lane proves that the current SphereSDF fixture expands the RESOURCES(t8)
template hook to an empty resource section. It does not create SRVs and does not
claim full PBR binding or renderer integration.
"""

from __future__ import annotations

import json
import re
import sys
from pathlib import Path
from typing import Any


RESULT_NAME = "tixl_mesh_draw_shadergraph_resources_expansion_result.json"
TRACE_NAME = "tixl_mesh_draw_shadergraph_resources_expansion_trace.json"
ERRORS_NAME = "tixl_mesh_draw_shadergraph_resources_expansion_errors.json"

DEFAULT_B5_ARTIFACT = "docs/runtime/artifacts/tixl_mesh_draw_b5_shadergraph_params_expansion/tixl_mesh_draw_b5_shadergraph_params_expansion_result.json"
DEFAULT_TEMPLATE = "external/tixl/Operators/Lib/Assets/shaders/3d/mesh/mesh-Draw.hlsl"
DEFAULT_GENERATOR = "external/tixl/Operators/Lib/field/render/_/GenerateShaderGraphCode.cs"
DEFAULT_SHADER_GRAPH_NODE = "external/tixl/Core/DataTypes/ShaderGraphNode.cs"
DEFAULT_GRAPH_NODE_OP = "external/tixl/Core/DataTypes/ShaderGraph/IGraphNodeOp.cs"
DEFAULT_SPHERE_SDF = "external/tixl/Operators/Lib/field/generate/sdf/SphereSDF.cs"
DEFAULT_DRAW_MESH = "external/tixl/Operators/Lib/mesh/draw/DrawMesh.t3"
DEFAULT_STAGE = "external/tixl/Operators/Lib/render/_dx11/fxsetup/SetPixelAndVertexShaderStage.cs"

EXPECTED_CLAIMS = {
    "sourceFilesValidated": True,
    "b5ExpansionArtifactConsumed": True,
    "resourcesHookFound": True,
    "collectResourcesPathValidated": True,
    "sphereSdfAppendResourcesEmpty": True,
    "currentFixtureT8ResourcesEmpty": True,
    "shadergraphResourcesExpansionProven": True,
    "stageAppendBehaviorSourceValidated": True,
    "nonEmptyT8ResourcesProven": False,
    "realSrvCreationProven": False,
    "constantBufferAdapterComplete": False,
    "textureSamplerMapping": False,
    "nativeCompileParity": False,
    "hlslToMslTranslationProven": False,
    "fullPbrResourceBinding": False,
    "backendReplacementReady": False,
    "hlslToMslTranslation": False,
    "tixlRuntimeParity": False,
    "pbrVisualCorrectness": False,
    "rendererIntegrationComplete": False,
}

FORBIDDEN_TRUE_CLAIMS = {
    "nonEmptyT8ResourcesProven",
    "realSrvCreationProven",
    "constantBufferAdapterComplete",
    "textureSamplerMapping",
    "fullPbrResourceBinding",
    "backendReplacementReady",
    "hlslToMslTranslation",
    "tixlRuntimeParity",
    "pbrVisualCorrectness",
    "rendererIntegrationComplete",
    "nativeCompileParity",
    "hlslToMslTranslationProven",
}

B5_FORBIDDEN_TRUE_CLAIMS = FORBIDDEN_TRUE_CLAIMS | {"shadergraphResourcesExpansionProven"}


def main() -> int:
    if len(sys.argv) != 3:
        print("usage: tixl_mesh_draw_shadergraph_resources_expansion_shell.py <fixture.graph.json> <out_dir>", file=sys.stderr)
        return 2

    fixture_path = Path(sys.argv[1]).expanduser().resolve()
    out_dir = Path(sys.argv[2]).expanduser().resolve()
    out_dir.mkdir(parents=True, exist_ok=True)
    repo_root = Path(__file__).resolve().parents[3]
    trace: list[dict[str, Any]] = [{"op": "loadShadergraphResourcesExpansionFixture", "fixture": display_path(fixture_path, repo_root)}]
    errors: list[dict[str, Any]] = []

    fixture = read_json(fixture_path, errors, "tixl_mesh_draw_shadergraph_resources_expansion.fixture_read_failed", repo_root)
    if fixture is None:
        result = result_payload(None, "blocked_missing_fixture", {}, {}, {}, {}, False)
        publish(out_dir, result, trace, errors)
        return 1

    result, run_trace, run_errors = run_proof(repo_root, fixture_path, fixture)
    trace.extend(run_trace)
    errors.extend(run_errors)
    trace.append({"op": "publishShadergraphResourcesExpansionArtifacts", "ok": result.get("ok") is True and not errors})
    publish(out_dir, result, trace, errors)
    return 0 if result.get("ok") is True and not errors else 1


def run_proof(repo_root: Path, fixture_path: Path, fixture: dict[str, Any]) -> tuple[dict[str, Any], list[dict[str, Any]], list[dict[str, Any]]]:
    trace: list[dict[str, Any]] = []
    errors: list[dict[str, Any]] = []
    graph_id = fixture.get("graphId")

    fixture_errors = validate_fixture(fixture)
    if fixture_errors:
        errors.append({
            "code": "tixl_mesh_draw_shadergraph_resources_expansion.invalid_fixture_expectations",
            "message": "Fixture expectations must keep the t8 resources proof bounded.",
            "mismatches": fixture_errors,
        })
        return result_payload(graph_id, "blocked_invalid_fixture", {}, {}, {}, {}, False), trace, errors

    paths = resolve_inputs(repo_root, fixture_path, fixture)
    trace.append({
        "op": "resolveInputArtifacts",
        **{key: display_path(value, repo_root) for key, value in paths.items()},
    })

    b5_artifact = read_json(paths["b5Artifact"], errors, "tixl_mesh_draw_shadergraph_resources_expansion.b5_artifact_read_failed", repo_root)
    b5_summary = summarize_artifact(paths["b5Artifact"], b5_artifact, repo_root)
    if b5_artifact is None:
        return result_payload(graph_id, "blocked_missing_input_artifact", {"b5ShadergraphParamsExpansion": b5_summary}, {}, {}, {}, False), trace, errors

    source_eval = validate_source_facts(paths, fixture, repo_root)
    b5_errors = validate_b5_artifact(b5_artifact)
    trace.append({
        "op": "validateShadergraphResourcesInputs",
        "b5ArtifactValid": not b5_errors,
        "sourceFilesValidated": source_eval["claims"]["sourceFilesValidated"],
        "resourcesHookFound": source_eval["claims"]["resourcesHookFound"],
        "collectResourcesPathValidated": source_eval["claims"]["collectResourcesPathValidated"],
        "sphereSdfAppendResourcesEmpty": source_eval["claims"]["sphereSdfAppendResourcesEmpty"],
        "stageAppendBehaviorSourceValidated": source_eval["claims"]["stageAppendBehaviorSourceValidated"],
    })

    if b5_errors:
        errors.append({
            "code": "tixl_mesh_draw_shadergraph_resources_expansion.invalid_b5_expansion_artifact",
            "message": "The upstream b5 expansion artifact must stay source-backed and must not widen backend, parity, or t8 resource claims.",
            "mismatches": b5_errors,
        })
        return result_payload(
            graph_id,
            "blocked_invalid_b5_expansion_artifact",
            {"b5ShadergraphParamsExpansion": b5_summary},
            source_eval["sourceFacts"],
            source_eval["resourceExpansion"],
            source_eval["stageAppendBehavior"],
            False,
            claims_override=source_eval["claims"],
        ), trace, errors

    source_errors = source_eval["errors"]
    if source_errors:
        status = "blocked_non_empty_t8_resources" if any(error.get("field") == "SphereSDF.cs.AppendShaderResources" for error in source_errors) else "blocked_invalid_source_facts"
        errors.append({
            "code": "tixl_mesh_draw_shadergraph_resources_expansion.invalid_source_facts",
            "message": "The TiXL source path no longer proves empty t8 ShaderGraph resources for the current SphereSDF fixture.",
            "mismatches": source_errors,
        })
        return result_payload(
            graph_id,
            status,
            {"b5ShadergraphParamsExpansion": b5_summary},
            source_eval["sourceFacts"],
            source_eval["resourceExpansion"],
            source_eval["stageAppendBehavior"],
            True,
            claims_override=source_eval["claims"],
        ), trace, errors

    trace.append({
        "op": "proveEmptyT8ShadergraphResources",
        "rootNode": source_eval["resourceExpansion"]["visitedShaderGraphNodes"][0],
        "resourceReferences": 0,
        "generatedResourceHlsl": "",
    })
    return result_payload(
        graph_id,
        "proven_empty_t8_shadergraph_resources_for_sphere_sdf_fixture",
        {"b5ShadergraphParamsExpansion": b5_summary},
        source_eval["sourceFacts"],
        source_eval["resourceExpansion"],
        source_eval["stageAppendBehavior"],
        True,
        ok=True,
    ), trace, errors


def validate_fixture(fixture: dict[str, Any]) -> list[dict[str, Any]]:
    mismatches: list[dict[str, Any]] = []
    if fixture.get("graphId") != "fixture.tixl_mesh_draw_shadergraph_resources_expansion":
        mismatches.append({"field": "graphId", "expected": "fixture.tixl_mesh_draw_shadergraph_resources_expansion", "actual": fixture.get("graphId")})
    if fixture.get("kind") != "TixlMeshDrawShadergraphResourcesExpansionProof":
        mismatches.append({"field": "kind", "expected": "TixlMeshDrawShadergraphResourcesExpansionProof", "actual": fixture.get("kind")})
    expected = fixture.get("expected") if isinstance(fixture.get("expected"), dict) else {}
    if expected.get("status") != "proven_empty_t8_shadergraph_resources_for_sphere_sdf_fixture":
        mismatches.append({"field": "expected.status", "expected": "proven_empty_t8_shadergraph_resources_for_sphere_sdf_fixture", "actual": expected.get("status")})
    if expected.get("claims") != EXPECTED_CLAIMS:
        mismatches.append({"field": "expected.claims", "expected": EXPECTED_CLAIMS, "actual": expected.get("claims")})
    for field in FORBIDDEN_TRUE_CLAIMS:
        if expected.get("claims", {}).get(field) is True:
            mismatches.append({"field": f"expected.claims.{field}", "expected": False, "actual": True})
    return mismatches


def validate_source_facts(paths: dict[str, Path], fixture: dict[str, Any], repo_root: Path) -> dict[str, Any]:
    errors: list[dict[str, Any]] = []
    template = read_text(paths["meshDrawTemplate"])
    generator = read_text(paths["generator"])
    shader_graph_node = read_text(paths["shaderGraphNode"])
    graph_node_op = read_text(paths["graphNodeOp"])
    draw_mesh = read_text(paths["drawMeshComposition"])
    stage = read_text(paths["setPixelAndVertexShaderStage"])
    shadergraph_section = fixture.get("shadergraphResourceExpansion") if isinstance(fixture.get("shadergraphResourceExpansion"), dict) else {}
    root = fixture.get("shadergraphResourceExpansion", {}).get("rootNode", {}) if isinstance(fixture.get("shadergraphResourceExpansion"), dict) else {}
    expected_root = validate_root_fixture(root)
    errors.extend(expected_root["errors"])
    evidence = root.get("sourceEvidence") if isinstance(root.get("sourceEvidence"), dict) else {}
    sphere_path = resolve_repo_path(repo_root, evidence.get("csharp")) or paths["sphereSdf"]
    defaults_path = resolve_repo_path(repo_root, evidence.get("defaults"))
    source_path = resolve_repo_path(repo_root, shadergraph_section.get("source"))
    example_path = resolve_repo_path(repo_root, evidence.get("example"))
    sphere_sdf = read_text(sphere_path)
    sphere_defaults = read_text(defaults_path) if defaults_path is not None else None
    shadergraph_source = read_text(source_path) if source_path is not None else None
    shader_tests = read_text(example_path) if example_path is not None else None

    resources_hook_found = template is not None and "/*{RESOURCES(t8)}*/" in template
    base_resources = extract_base_shader_resources(template or "")
    generator_valid = has_all(generator, [
        "public readonly Slot<Object> Resources = new();",
        "AssembleResources();",
        "Resources.Value = _resourceViews;",
        "InjectResourcesCode(ref templateCode);",
        "_graphNode.CollectResources(_resourceReferences",
        "const string resourcesStartHook = \"/*{RESOURCES(\";",
        "_resourceDefinitionsBuilder.AppendLine($\"{rr.Definition}:register({t}{index});\");",
    ])
    recursive_collect_valid = has_all(shader_graph_node, [
        "public void CollectResources(List<SrvBufferReference> buffers, int frameNumber, int graphId)",
        "foreach (var inputNode in InputNodes)",
        "inputNode?.CollectResources(buffers, frameNumber, graphId);",
        "_nodeOp.AppendShaderResources(ref buffers);",
        "public record SrvBufferReference(string Definition, ShaderResourceView Srv);",
    ])
    graph_node_op_empty = graph_node_op is not None and re.search(r"void\s+AppendShaderResources\s*\(\s*ref\s+List<ShaderGraphNode\.SrvBufferReference>\s+list\s*\)\s*\{\s*\}", graph_node_op, re.S) is not None
    sphere_sources_valid = has_all(sphere_sdf, [
        "[GraphParam]",
        "public readonly InputSlot<Vector3> Center",
        "public readonly InputSlot<float> Radius",
        "ShaderNode = new ShaderGraphNode(this);",
        "public void GetPreShaderCode",
    ])
    sphere_defaults_valid = has_all(sphere_defaults, [
        '"Id": "fc2a33fc-d957-4113-8096-92d4dcbe14b5"/*SphereSDF*/',
    ])
    shader_tests_valid = has_all(shader_tests, [
        '"Id": "04426d9c-b039-4a92-9b1f-61186b4df2e5"/*SphereSDF*/',
        '"SourceParentOrChildId": "04426d9c-b039-4a92-9b1f-61186b4df2e5"',
    ])
    shadergraph_source_valid = has_all(shadergraph_source, [
        '"Id": "04426d9c-b039-4a92-9b1f-61186b4df2e5"/*SphereSDF*/',
        '"SourceParentOrChildId": "04426d9c-b039-4a92-9b1f-61186b4df2e5"',
    ])
    sphere_sources_valid = sphere_sources_valid and sphere_defaults_valid and shader_tests_valid and shadergraph_source_valid and expected_root["valid"]
    sphere_has_resource_override = sphere_sdf is not None and "AppendShaderResources" in sphere_sdf
    sphere_sdf_append_empty = graph_node_op_empty and sphere_sources_valid and not sphere_has_resource_override
    stage_behavior = validate_stage_append_behavior(generator, draw_mesh, stage)

    if not resources_hook_found:
        errors.append({"field": "mesh-Draw.hlsl.RESOURCES(t8)", "expected": "/*{RESOURCES(t8)}*/", "actual": "missing"})
    if len(base_resources) != 8 or [item["register"] for item in base_resources] != [f"t{i}" for i in range(8)]:
        errors.append({"field": "mesh-Draw.hlsl.baseShaderResources", "expected": "t0-t7 count 8", "actual": base_resources})
    if not generator_valid:
        errors.append({"field": "GenerateShaderGraphCode.cs.CollectResources", "expected": "Resources output, AssembleResources, CollectResources, and InjectResourcesCode path", "actual": "missing"})
    if not recursive_collect_valid:
        errors.append({"field": "ShaderGraphNode.cs.CollectResources", "expected": "recursive InputNodes collection followed by AppendShaderResources", "actual": "missing"})
    if not graph_node_op_empty:
        errors.append({"field": "IGraphNodeOp.cs.AppendShaderResources", "expected": "empty default implementation", "actual": "missing_or_non_empty"})
    if not sphere_sources_valid:
        errors.append({"field": "SphereSDF.sourceEvidence", "expected": "C# GraphParam source plus SphereSDF defaults and ShaderTests instance evidence", "actual": "missing_or_mismatched"})
    if sphere_has_resource_override:
        errors.append({"field": "SphereSDF.cs.AppendShaderResources", "expected": "no override for current fixture", "actual": "present"})
    if not stage_behavior["validated"]:
        errors.append({"field": "SetPixelAndVertexShaderStage.Resources", "expected": "GenerateShaderGraphCode.Resources appended after ShaderResources through VariousResources", "actual": "missing"})

    collect_resources_path_validated = generator_valid and recursive_collect_valid
    current_fixture_t8_resources_empty = resources_hook_found and collect_resources_path_validated and sphere_sdf_append_empty
    source_files_validated = current_fixture_t8_resources_empty and len(base_resources) == 8 and stage_behavior["validated"]
    claims = bounded_claims(
        source_files_validated=source_files_validated,
        b5_expansion_artifact_consumed=True,
        resources_hook_found=resources_hook_found,
        collect_resources_path_validated=collect_resources_path_validated,
        sphere_sdf_append_resources_empty=sphere_sdf_append_empty,
        current_fixture_t8_resources_empty=current_fixture_t8_resources_empty,
        shadergraph_resources_expansion_proven=False,
        stage_append_behavior_source_validated=stage_behavior["validated"],
    )

    return {
        "errors": errors,
        "claims": claims,
        "sourceFacts": source_fact_summary(paths, sphere_path, defaults_path, source_path, example_path, repo_root),
        "resourceExpansion": resource_expansion(base_resources, current_fixture_t8_resources_empty, expected_root["prefix"]),
        "stageAppendBehavior": stage_behavior,
    }


def validate_root_fixture(root: dict[str, Any]) -> dict[str, Any]:
    expected = {
        "type": "tixl.field.generate.sdf.SphereSDF",
        "title": "my_SphereSDF",
        "tixlSymbolChildId": "04426d9c-b039-4a92-9b1f-61186b4df2e5",
        "prefix": "SphereSDF_nG1CBDm",
    }
    errors: list[dict[str, Any]] = []
    for field, expected_value in expected.items():
        actual = root.get(field)
        if actual != expected_value:
            errors.append({
                "field": f"shadergraphResourceExpansion.rootNode.{field}",
                "expected": expected_value,
                "actual": actual,
            })
    return {
        "valid": not errors,
        "errors": errors,
        "prefix": root.get("prefix") if isinstance(root.get("prefix"), str) and root.get("prefix") else expected["prefix"],
    }


def validate_stage_append_behavior(generator: str | None, draw_mesh: str | None, stage: str | None) -> dict[str, Any]:
    output_guid = "adf247cd-79cc-4d4e-b3c1-6a8b2d54683d"
    target_guid = "cc866663-5bfa-4a17-9efc-e2f381767317"
    valid = has_all(generator, ["public readonly Slot<Object> Resources = new();", output_guid.upper()])
    connected = has_all(draw_mesh, [
        '"SourceParentOrChildId": "151209ef-f52b-4c88-9d9c-5a8882fb0e1d"',
        f'"SourceSlotId": "{output_guid}"',
        '"TargetParentOrChildId": "69767494-70bc-4ac8-aa51-a552037edf79"',
        f'"TargetSlotId": "{target_guid}"',
    ])
    appended = has_all(stage, [
        "ShaderResources.GetValues(ref _shaderResourceViews, context);",
        "GetAdditionalResources(context);",
        "VariousResources.GetCollectedTypedInputs();",
        "vsStage.SetShaderResources(0, _shaderResourceViews.Length, _shaderResourceViews);",
        "vsStage.SetShaderResources(_shaderResourceViews.Length, _additionalSrvs.Length, _additionalSrvs);",
        "psStage.SetShaderResources(0, _shaderResourceViews.Length, _shaderResourceViews);",
        "psStage.SetShaderResources(_shaderResourceViews.Length, _additionalSrvs.Length, _additionalSrvs);",
    ])
    return {
        "validated": bool(valid and connected and appended),
        "sourceOutput": "GenerateShaderGraphCode.Resources",
        "targetInput": "SetPixelAndVertexShaderStage.VariousResources",
        "ordinaryShaderResourcesInput": "SetPixelAndVertexShaderStage.ShaderResources",
        "appendedAfterOrdinaryShaderResources": bool(appended),
        "evidence": {
            "resourcesOutputGuid": output_guid,
            "variousResourcesInputGuid": target_guid,
            "drawMeshConnectionFound": bool(connected),
            "setStageAppendsAdditionalSrvsAtShaderResourcesLength": bool(appended),
        },
        "boundary": "Source-validated stage append behavior only; no real SRV creation or renderer integration is proven.",
    }


def validate_b5_artifact(artifact: dict[str, Any]) -> list[dict[str, Any]]:
    mismatches: list[dict[str, Any]] = []
    if artifact.get("kind") != "TixlMeshDrawB5ShadergraphParamsExpansionVerdict":
        mismatches.append({"field": "b5.kind", "expected": "TixlMeshDrawB5ShadergraphParamsExpansionVerdict", "actual": artifact.get("kind")})
    if artifact.get("ok") is not True:
        mismatches.append({"field": "b5.ok", "expected": True, "actual": artifact.get("ok")})
    if artifact.get("status") != "expanded_b5_shadergraph_params_source_backed":
        mismatches.append({"field": "b5.status", "expected": "expanded_b5_shadergraph_params_source_backed", "actual": artifact.get("status")})
    expansion = artifact.get("expansion") if isinstance(artifact.get("expansion"), dict) else {}
    root = expansion.get("rootNode") if isinstance(expansion.get("rootNode"), dict) else {}
    if root.get("prefix") != "SphereSDF_nG1CBDm_":
        mismatches.append({"field": "b5.expansion.rootNode.prefix", "expected": "SphereSDF_nG1CBDm_", "actual": root.get("prefix")})
    field_names = [field.get("name") for field in expansion.get("fields", []) if isinstance(field, dict)]
    if field_names != ["SphereSDF_nG1CBDm_Center", "SphereSDF_nG1CBDm_Radius"]:
        mismatches.append({"field": "b5.expansion.fields", "expected": ["SphereSDF_nG1CBDm_Center", "SphereSDF_nG1CBDm_Radius"], "actual": field_names})
    claims = artifact.get("claims") if isinstance(artifact.get("claims"), dict) else {}
    for field in B5_FORBIDDEN_TRUE_CLAIMS:
        if claims.get(field) is True:
            mismatches.append({"field": f"b5.claims.{field}", "expected": False, "actual": True})
    return mismatches


def extract_base_shader_resources(source: str) -> list[dict[str, Any]]:
    resources: list[dict[str, Any]] = []
    pattern = re.compile(r"^\s*(?P<type>(?:StructuredBuffer|Texture2D|TextureCube)<[^>]+>)\s+(?P<name>\w+)\s*:\s*register\((?P<register>t\d+)\);", re.M)
    for match in pattern.finditer(source):
        register = match.group("register")
        if register in {f"t{i}" for i in range(8)}:
            resources.append({
                "register": register,
                "name": match.group("name"),
                "hlslType": match.group("type"),
            })
    return sorted(resources, key=lambda item: int(item["register"][1:]))


def resource_expansion(base_resources: list[dict[str, Any]], empty: bool, root_prefix: str) -> dict[str, Any]:
    return {
        "registerStart": "t8",
        "hook": "RESOURCES(t8)",
        "baseShaderResources": base_resources,
        "baseShaderResourcesCount": len(base_resources),
        "visitedShaderGraphNodes": [root_prefix],
        "resourceTypes": [],
        "resourceReferences": [] if empty else ["blocked"],
        "resourceDefinitions": [],
        "resourceViewsCount": 0 if empty else None,
        "generatedResourceHlsl": "" if empty else None,
        "currentFixtureT8ResourcesEmpty": empty,
    }


def result_payload(
    graph_id: Any,
    status: str,
    input_summary: dict[str, Any],
    source_facts: dict[str, Any],
    expansion: dict[str, Any],
    stage_append_behavior: dict[str, Any],
    b5_consumed: bool,
    ok: bool = False,
    claims_override: dict[str, bool] | None = None,
) -> dict[str, Any]:
    claims = claims_override or bounded_claims(b5_expansion_artifact_consumed=b5_consumed)
    if ok:
        claims = dict(EXPECTED_CLAIMS)
    elif claims_override is not None and not b5_consumed:
        claims = dict(claims_override)
        claims["b5ExpansionArtifactConsumed"] = False
        claims["shadergraphResourcesExpansionProven"] = False
    return {
        "kind": "TixlMeshDrawShadergraphResourcesExpansionProof",
        "graphId": graph_id,
        "ok": ok,
        "status": status,
        "inputArtifacts": input_summary,
        "sourceFacts": source_facts,
        "resourceExpansion": expansion,
        "stageAppendBehavior": stage_append_behavior,
        "claims": claims,
    }


def bounded_claims(
    source_files_validated: bool = False,
    b5_expansion_artifact_consumed: bool = False,
    resources_hook_found: bool = False,
    collect_resources_path_validated: bool = False,
    sphere_sdf_append_resources_empty: bool = False,
    current_fixture_t8_resources_empty: bool = False,
    shadergraph_resources_expansion_proven: bool = False,
    stage_append_behavior_source_validated: bool = False,
) -> dict[str, bool]:
    return {
        "sourceFilesValidated": source_files_validated,
        "b5ExpansionArtifactConsumed": b5_expansion_artifact_consumed,
        "resourcesHookFound": resources_hook_found,
        "collectResourcesPathValidated": collect_resources_path_validated,
        "sphereSdfAppendResourcesEmpty": sphere_sdf_append_resources_empty,
        "currentFixtureT8ResourcesEmpty": current_fixture_t8_resources_empty,
        "shadergraphResourcesExpansionProven": shadergraph_resources_expansion_proven,
        "stageAppendBehaviorSourceValidated": stage_append_behavior_source_validated,
        "nonEmptyT8ResourcesProven": False,
        "realSrvCreationProven": False,
        "constantBufferAdapterComplete": False,
        "textureSamplerMapping": False,
        "nativeCompileParity": False,
        "hlslToMslTranslationProven": False,
        "fullPbrResourceBinding": False,
        "backendReplacementReady": False,
        "hlslToMslTranslation": False,
        "tixlRuntimeParity": False,
        "pbrVisualCorrectness": False,
        "rendererIntegrationComplete": False,
    }


def source_fact_summary(paths: dict[str, Path], sphere_path: Path, defaults_path: Path | None, source_path: Path | None, example_path: Path | None, repo_root: Path) -> dict[str, Any]:
    return {
        "meshDrawTemplate": {
            "path": display_path(paths["meshDrawTemplate"], repo_root),
            "resourcesHook": "/*{RESOURCES(t8)}*/",
            "baseResources": "t0-t7",
        },
        "generator": {
            "path": display_path(paths["generator"], repo_root),
            "collectsResourcesWith": "ShaderGraphNode.CollectResources",
            "injectsResourcesWith": "InjectResourcesCode",
            "resourcesOutput": "GenerateShaderGraphCode.Resources",
        },
        "shaderGraphNode": {
            "path": display_path(paths["shaderGraphNode"], repo_root),
            "recursiveCollection": "InputNodes -> CollectResources -> AppendShaderResources",
        },
        "graphNodeOp": {
            "path": display_path(paths["graphNodeOp"], repo_root),
            "defaultAppendShaderResources": "empty",
        },
        "sphereSdf": {
            "path": display_path(sphere_path, repo_root),
            "defaults": display_path(defaults_path, repo_root) if defaults_path is not None else None,
            "shadergraphSource": display_path(source_path, repo_root) if source_path is not None else None,
            "example": display_path(example_path, repo_root) if example_path is not None else None,
            "graphParams": ["Center", "Radius"],
            "appendShaderResourcesOverride": False,
        },
        "stage": {
            "drawMeshComposition": display_path(paths["drawMeshComposition"], repo_root),
            "setPixelAndVertexShaderStage": display_path(paths["setPixelAndVertexShaderStage"], repo_root),
        },
    }


def summarize_artifact(path: Path, artifact: Any, repo_root: Path) -> dict[str, Any]:
    summary: dict[str, Any] = {"path": display_path(path, repo_root)}
    if isinstance(artifact, dict):
        for key in ("kind", "ok", "status"):
            if key in artifact:
                summary[key] = artifact[key]
    return summary


def resolve_inputs(repo_root: Path, fixture_path: Path, fixture: dict[str, Any]) -> dict[str, Path]:
    root = fixture.get("shadergraphResourceExpansion", {}).get("rootNode", {}) if isinstance(fixture.get("shadergraphResourceExpansion"), dict) else {}
    evidence = root.get("sourceEvidence") if isinstance(root.get("sourceEvidence"), dict) else {}
    return {
        "b5Artifact": resolve_path(repo_root, fixture_path, fixture.get("b5ShadergraphParamsExpansionArtifact"), DEFAULT_B5_ARTIFACT),
        "meshDrawTemplate": resolve_path(repo_root, fixture_path, fixture.get("meshDrawTemplateSource"), DEFAULT_TEMPLATE),
        "generator": resolve_path(repo_root, fixture_path, fixture.get("generateShaderGraphCodeSource"), DEFAULT_GENERATOR),
        "shaderGraphNode": resolve_path(repo_root, fixture_path, fixture.get("shaderGraphNodeSource"), DEFAULT_SHADER_GRAPH_NODE),
        "graphNodeOp": resolve_path(repo_root, fixture_path, fixture.get("graphNodeOpSource"), DEFAULT_GRAPH_NODE_OP),
        "sphereSdf": resolve_path(repo_root, fixture_path, evidence.get("csharp"), DEFAULT_SPHERE_SDF),
        "drawMeshComposition": resolve_path(repo_root, fixture_path, fixture.get("drawMeshCompositionSource"), DEFAULT_DRAW_MESH),
        "setPixelAndVertexShaderStage": resolve_path(repo_root, fixture_path, fixture.get("setPixelAndVertexShaderStageSource"), DEFAULT_STAGE),
    }


def resolve_path(repo_root: Path, fixture_path: Path, value: Any, default: str) -> Path:
    raw = value if isinstance(value, str) and value else default
    candidate = Path(raw).expanduser()
    if candidate.is_absolute():
        return candidate
    repo_candidate = (repo_root / candidate).resolve()
    if repo_candidate.exists():
        return repo_candidate
    return (fixture_path.parent / candidate).resolve()


def resolve_repo_path(repo_root: Path, value: Any) -> Path | None:
    if not isinstance(value, str) or not value:
        return None
    path = Path(value).expanduser()
    if path.is_absolute():
        return path
    return (repo_root / path).resolve()


def has_all(source: str | None, needles: list[str]) -> bool:
    return source is not None and all(needle in source for needle in needles)


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

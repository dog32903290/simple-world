#!/usr/bin/env python3
"""
Run a minimal Metal texture/sampler binding probe for TiXL mesh Draw resources.

This lane proves only t2 BaseColorMap, t7 BRDFLookup, s0 WrappedSampler, and
s1 ClampedSampler for the handwritten explicit MSL adapter. It consumes the
source audit and the prior resource binding ledger before compiling Metal.
"""

from __future__ import annotations

import json
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path
from typing import Any


DEFAULT_SOURCE_AUDIT_ARTIFACT = "docs/runtime/artifacts/tixl_mesh_draw_shader_source_audit/tixl_mesh_draw_shader_source_audit_result.json"
DEFAULT_PRIOR_RESOURCE_BINDING_ARTIFACT = "docs/runtime/artifacts/tixl_mesh_draw_resource_binding/tixl_mesh_draw_resource_binding_result.json"
DEFAULT_STRATEGY_ARTIFACT = "docs/runtime/artifacts/tixl_mesh_draw_explicit_translation_strategy/tixl_mesh_draw_explicit_translation_strategy_result.json"
RESULT_NAME = "tixl_mesh_draw_texture_sampler_binding_result.json"
TRACE_NAME = "tixl_mesh_draw_texture_sampler_binding_trace.json"
ERRORS_NAME = "tixl_mesh_draw_texture_sampler_binding_errors.json"
GENERATED_MSL_NAME = "generated_texture_sampler_probe.metal"

BOUND_SUBSET = ["t2", "t7", "s0", "s1"]
TEXTURE_SAMPLER_MAPPING = [
    ("t2", "BaseColorMap", "Texture2D<float4>", "texture(2)", "0x11223344"),
    ("t7", "BRDFLookup", "Texture2D<float4>", "texture(7)", "0xa1b2c3d4"),
    ("s0", "WrappedSampler", "sampler", "sampler(0)", "0x778899aa"),
    ("s1", "ClampedSampler", "sampler", "sampler(1)", "0x55667788"),
]
EXPECTED_WORDS = [0x11223344, 0xA1B2C3D4, 0x778899AA, 0x55667788]
FORBIDDEN_TRUE_CLAIMS = [
    "fullPbrResourceBinding",
    "t8ShadergraphResourcesExpanded",
    "backendReplacementReady",
    "hlslToMslTranslation",
    "tixlRuntimeParity",
    "pbrVisualCorrectness",
    "rendererIntegrationComplete",
    "constantBufferAdapterComplete",
]


def main() -> int:
    if len(sys.argv) != 3:
        print("usage: tixl_mesh_draw_texture_sampler_binding_shell.py <fixture.graph.json> <out_dir>", file=sys.stderr)
        return 2

    fixture_path = Path(sys.argv[1]).expanduser().resolve()
    out_dir = Path(sys.argv[2]).expanduser().resolve()
    out_dir.mkdir(parents=True, exist_ok=True)
    clear_optional_artifacts(out_dir)

    repo_root = Path(__file__).resolve().parents[3]
    trace: list[dict[str, Any]] = [{
        "op": "loadTixlMeshDrawTextureSamplerBindingFixture",
        "fixture": display_path(fixture_path, repo_root),
    }]
    errors: list[dict[str, Any]] = []

    fixture = read_json(fixture_path, errors, "tixl_mesh_draw_texture_sampler_binding.fixture_read_failed", repo_root)
    if fixture is None:
        result = default_result(None, "blocked_missing_fixture", {}, {}, {})
        publish(out_dir, result, trace, errors)
        return 1

    result, run_trace, run_errors, generated_msl = run_proof(repo_root, fixture_path, fixture)
    trace.extend(run_trace)
    errors.extend(run_errors)
    trace.append({
        "op": "publishTixlMeshDrawTextureSamplerBindingArtifacts",
        "ok": result.get("ok") is True and not errors,
    })
    publish(out_dir, result, trace, errors, generated_msl if result.get("ok") is True and not errors else None)
    return 0 if result.get("ok") is True and not errors else 1


def run_proof(
    repo_root: Path,
    fixture_path: Path,
    fixture: dict[str, Any],
) -> tuple[dict[str, Any], list[dict[str, Any]], list[dict[str, Any]], str | None]:
    trace: list[dict[str, Any]] = []
    errors: list[dict[str, Any]] = []
    graph_id = fixture.get("graphId")

    source_path = resolve_path(repo_root, fixture_path, fixture.get("sourceAuditArtifact"), DEFAULT_SOURCE_AUDIT_ARTIFACT)
    prior_path = resolve_path(repo_root, fixture_path, fixture.get("priorResourceBindingArtifact"), DEFAULT_PRIOR_RESOURCE_BINDING_ARTIFACT)
    strategy_path = resolve_path(repo_root, fixture_path, fixture.get("strategyArtifact"), DEFAULT_STRATEGY_ARTIFACT)
    trace.append({
        "op": "resolveInputArtifacts",
        "sourceAuditArtifact": display_path(source_path, repo_root),
        "priorResourceBindingArtifact": display_path(prior_path, repo_root),
        "strategyArtifact": display_path(strategy_path, repo_root),
    })

    source = read_json(source_path, errors, "tixl_mesh_draw_texture_sampler_binding.source_audit_read_failed", repo_root)
    prior = read_json(prior_path, errors, "tixl_mesh_draw_texture_sampler_binding.prior_resource_binding_read_failed", repo_root)
    strategy = read_json(strategy_path, errors, "tixl_mesh_draw_texture_sampler_binding.strategy_read_failed", repo_root)
    artifacts = {
        "sourceAudit": summarize_artifact(source_path, source, repo_root),
        "priorResourceBinding": summarize_artifact(prior_path, prior, repo_root),
        "explicitTranslationStrategy": summarize_artifact(strategy_path, strategy, repo_root),
    }
    trace.append({
        "op": "readInputArtifacts",
        "sourceAuditRead": source is not None,
        "priorResourceBindingRead": prior is not None,
        "strategyRead": strategy is not None,
    })
    if source is None or prior is None or strategy is None:
        return default_result(graph_id, "blocked_missing_input_artifact", artifacts["sourceAudit"], artifacts["priorResourceBinding"], artifacts["explicitTranslationStrategy"]), trace, errors, None

    source_errors = validate_source_audit(source)
    prior_errors = validate_prior_resource_binding(prior)
    strategy_errors = validate_strategy(strategy)
    trace.append({
        "op": "validateInputArtifacts",
        "sourceAuditValid": not source_errors,
        "priorResourceBindingValid": not prior_errors,
        "strategyValid": not strategy_errors,
    })
    if source_errors:
        errors.append({
            "code": "tixl_mesh_draw_texture_sampler_binding.invalid_source_audit_artifact",
            "message": "TiXL mesh draw source audit artifact does not contain the required t2/t7/s0/s1 slots.",
            "mismatches": source_errors,
        })
    if prior_errors:
        errors.append({
            "code": "tixl_mesh_draw_texture_sampler_binding.invalid_prior_resource_binding_artifact",
            "message": "Prior resource binding artifact is not the expected partial/unbound boundary.",
            "mismatches": prior_errors,
        })
    if strategy_errors:
        errors.append({
            "code": "tixl_mesh_draw_texture_sampler_binding.invalid_strategy_artifact",
            "message": "Explicit translation strategy artifact no longer selects the bounded handwritten adapter.",
            "mismatches": strategy_errors,
        })
    if source_errors or prior_errors or strategy_errors:
        return default_result(graph_id, "blocked_invalid_input_artifact", artifacts["sourceAudit"], artifacts["priorResourceBinding"], artifacts["explicitTranslationStrategy"]), trace, errors, None

    fixture_errors = validate_fixture_expectations(fixture)
    trace.append({
        "op": "validateFixtureExpectations",
        "valid": not fixture_errors,
        "boundSubset": fixture.get("adapterMapping", {}).get("boundSubset") if isinstance(fixture.get("adapterMapping"), dict) else None,
    })
    if fixture_errors:
        errors.append({
            "code": "tixl_mesh_draw_texture_sampler_binding.invalid_fixture_expectations",
            "message": "Fixture expected claims widened outside the four-slot texture/sampler lane.",
            "mismatches": fixture_errors,
        })
        return default_result(graph_id, "blocked_invalid_fixture_expectations", artifacts["sourceAudit"], artifacts["priorResourceBinding"], artifacts["explicitTranslationStrategy"]), trace, errors, None

    probe_payload, probe_trace, probe_errors, generated_msl = run_metal_probe(repo_root)
    trace.extend(probe_trace)
    if probe_errors:
        errors.extend(probe_errors)
        status = str(probe_payload.get("status") if isinstance(probe_payload, dict) else "probe_failed")
        return default_result(graph_id, status, artifacts["sourceAudit"], artifacts["priorResourceBinding"], artifacts["explicitTranslationStrategy"]), trace, errors, None

    result = build_success_result(graph_id, artifacts, probe_payload)
    trace.append({
        "op": "buildTextureSamplerBindingLedger",
        "boundSubset": BOUND_SUBSET,
        "actualWords": probe_payload.get("actualWords"),
    })
    return result, trace, errors, generated_msl


def validate_source_audit(source: dict[str, Any]) -> list[dict[str, Any]]:
    mismatches: list[dict[str, Any]] = []
    if source.get("kind") != "TixlMeshDrawShaderSourceAudit":
        mismatches.append({"field": "kind", "expected": "TixlMeshDrawShaderSourceAudit", "actual": source.get("kind")})
    if source.get("ok") is not True:
        mismatches.append({"field": "ok", "expected": True, "actual": source.get("ok")})
    if source.get("status") != "audited_tixl_mesh_draw_source":
        mismatches.append({"field": "status", "expected": "audited_tixl_mesh_draw_source", "actual": source.get("status")})

    resources = {(item.get("register"), item.get("name"), item.get("kind"), item.get("elementType")) for item in list_items(source.get("resources"))}
    for register, name in (("t2", "BaseColorMap"), ("t7", "BRDFLookup")):
        expected = (register, name, "Texture2D", "float4")
        if expected not in resources:
            mismatches.append({"field": "resources", "expected": {"register": register, "name": name, "kind": "Texture2D", "elementType": "float4"}})

    samplers = {(item.get("register"), item.get("name")) for item in list_items(source.get("samplers"))}
    for register, name in (("s0", "WrappedSampler"), ("s1", "ClampedSampler")):
        if (register, name) not in samplers:
            mismatches.append({"field": "samplers", "expected": {"register": register, "name": name}})

    claims = source.get("claims") if isinstance(source.get("claims"), dict) else {}
    for field, actual in claims.items():
        if field in ("fullPbrResourceBinding", "backendReplacementReady", "hlslToMslTranslation", "tixlRuntimeParity") and actual is True:
            mismatches.append({"field": f"claims.{field}", "expected": False, "actual": actual})
    return mismatches


def validate_prior_resource_binding(prior: dict[str, Any]) -> list[dict[str, Any]]:
    mismatches: list[dict[str, Any]] = []
    if prior.get("kind") != "TixlMeshDrawResourceBindingProof":
        mismatches.append({"field": "kind", "expected": "TixlMeshDrawResourceBindingProof", "actual": prior.get("kind")})
    if prior.get("ok") is not True:
        mismatches.append({"field": "ok", "expected": True, "actual": prior.get("ok")})
    if prior.get("status") != "summarized_tixl_mesh_draw_resource_binding":
        mismatches.append({"field": "status", "expected": "summarized_tixl_mesh_draw_resource_binding", "actual": prior.get("status")})

    claims = prior.get("claims") if isinstance(prior.get("claims"), dict) else {}
    expected_false_claims = [
        "fullPbrResourceBinding",
        "backendReplacementReady",
        "hlslToMslTranslation",
        "tixlRuntimeParity",
        "boundedTextureSamplerMappingProven",
        "textureSamplerMapping",
    ]
    for field in expected_false_claims:
        if claims.get(field) is True:
            mismatches.append({"field": f"claims.{field}", "expected": False, "actual": True})

    ledger = prior.get("bindingLedger") if isinstance(prior.get("bindingLedger"), dict) else {}
    bound_registers = {item.get("sourceRegister") for item in list_items(ledger.get("boundNow"))}
    for register in BOUND_SUBSET:
        if register in bound_registers:
            mismatches.append({"field": "bindingLedger.boundNow", "expected": f"{register} unbound in prior artifact", "actual": register})

    unbound = {(item.get("sourceRegister"), item.get("sourceName"), item.get("sourceKind")) for item in list_items(ledger.get("declaredButUnbound"))}
    for register, name, kind, _metal, _sentinel in TEXTURE_SAMPLER_MAPPING:
        if (register, name, kind) not in unbound:
            mismatches.append({"field": "bindingLedger.declaredButUnbound", "expected": {"sourceRegister": register, "sourceName": name, "sourceKind": kind}})
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
        "selectedStrategy": "handwritten_explicit_msl_adapter",
        "fullPbrResourceBinding": False,
        "backendReplacementReady": False,
        "tixlRuntimeParity": False,
        "hlslToMslTranslation": False,
        "pbrVisualCorrectness": False,
    }
    for field, expected in expected_claims.items():
        if claims.get(field) != expected:
            mismatches.append({"field": f"claims.{field}", "expected": expected, "actual": claims.get(field)})
    gates = strategy.get("nextAdapterGates") if isinstance(strategy.get("nextAdapterGates"), list) else []
    if "texture/sampler mapping t2-t7/s0-s1" not in gates:
        mismatches.append({"field": "nextAdapterGates", "expected": "texture/sampler mapping t2-t7/s0-s1"})
    return mismatches


def validate_fixture_expectations(fixture: dict[str, Any]) -> list[dict[str, Any]]:
    mismatches: list[dict[str, Any]] = []
    mapping = fixture.get("adapterMapping") if isinstance(fixture.get("adapterMapping"), dict) else {}
    if mapping.get("boundSubset") != BOUND_SUBSET:
        mismatches.append({"field": "adapterMapping.boundSubset", "expected": BOUND_SUBSET, "actual": mapping.get("boundSubset")})
    expected_bindings = {
        "t2": "texture(2)",
        "t7": "texture(7)",
        "s0": "sampler(0)",
        "s1": "sampler(1)",
    }
    bindings = mapping.get("metalBindings") if isinstance(mapping.get("metalBindings"), dict) else {}
    for field, expected in expected_bindings.items():
        if bindings.get(field) != expected:
            mismatches.append({"field": f"adapterMapping.metalBindings.{field}", "expected": expected, "actual": bindings.get(field)})

    expected = fixture.get("expected") if isinstance(fixture.get("expected"), dict) else {}
    claims = expected.get("claims") if isinstance(expected.get("claims"), dict) else {}
    for field, expected_value in claim_flags(True).items():
        if claims.get(field) is not expected_value:
            mismatches.append({"field": f"expected.claims.{field}", "expected": expected_value, "actual": claims.get(field)})
    if expected.get("boundedTextureSamplerMappingSubset") != BOUND_SUBSET:
        mismatches.append({"field": "expected.boundedTextureSamplerMappingSubset", "expected": BOUND_SUBSET, "actual": expected.get("boundedTextureSamplerMappingSubset")})
    return mismatches


def run_metal_probe(repo_root: Path) -> tuple[dict[str, Any], list[dict[str, Any]], list[dict[str, Any]], str | None]:
    trace: list[dict[str, Any]] = []
    errors: list[dict[str, Any]] = []
    generated_msl = generated_msl_source()
    build_dir = Path(tempfile.mkdtemp(prefix="tixl-mesh-draw-texture-sampler-build-"))
    run_payload: dict[str, Any] | None = None
    try:
        msl_path = build_dir / GENERATED_MSL_NAME
        probe_bin = build_dir / "tixl_mesh_draw_texture_sampler_binding_probe"
        msl_path.write_text(generated_msl, encoding="utf8")

        probe_source = repo_root / "docs/runtime/native/tixl_mesh_draw_texture_sampler_binding_probe.mm"
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
            "op": "buildMetalTextureSamplerProbe",
            "compiler": "xcrun clang++",
            "probe": display_path(probe_source, repo_root),
            "exitCode": build.returncode,
        })
        if build.returncode != 0:
            errors.append({
                "code": "tixl_mesh_draw_texture_sampler_binding.probe_build_failed",
                "message": clean_message(build.stderr or build.stdout or "probe build failed", repo_root),
            })
            return {"status": "probe_build_failed"}, trace, errors, None

        run = subprocess.run([str(probe_bin), str(msl_path)], cwd=repo_root, text=True, capture_output=True)
        trace.append({
            "op": "runMetalTextureSamplerProbe",
            "exitCode": run.returncode,
        })
        run_payload = parse_probe_payload(run.stdout)
        if run_payload is None:
            errors.append({
                "code": "tixl_mesh_draw_texture_sampler_binding.probe_output_invalid",
                "message": clean_message(run.stderr or run.stdout or "probe did not emit JSON", repo_root),
            })
            return {"status": "probe_output_invalid"}, trace, errors, None
        if run.returncode != 0 or run_payload.get("ok") is not True:
            errors.append(error_from_probe(run_payload))
            return run_payload, trace, errors, None
    finally:
        shutil.rmtree(build_dir, ignore_errors=True)

    if not validate_probe_payload(run_payload):
        errors.append({
            "code": "tixl_mesh_draw_texture_sampler_binding.probe_sentinel_mismatch",
            "message": "probe payload did not report the exact expected sentinel readback",
            "actualWords": run_payload.get("actualWords") if isinstance(run_payload, dict) else None,
            "expectedWords": EXPECTED_WORDS,
        })
        return run_payload or {"status": "sentinel_mismatch"}, trace, errors, None
    return run_payload, trace, errors, generated_msl


def generated_msl_source() -> str:
    return """#include <metal_stdlib>
using namespace metal;

static uint pack_rgba8(float4 color)
{
    uint4 bytes = uint4(round(clamp(color, 0.0, 1.0) * 255.0));
    return (bytes.r << 24) | (bytes.g << 16) | (bytes.b << 8) | bytes.a;
}

kernel void texture_sampler_probe(
    texture2d<float, access::sample> BaseColorMap [[texture(2)]],
    texture2d<float, access::sample> BRDFLookup [[texture(7)]],
    sampler WrappedSampler [[sampler(0)]],
    sampler ClampedSampler [[sampler(1)]],
    device uint* outWords [[buffer(0)]])
{
    outWords[0] = pack_rgba8(BaseColorMap.sample(WrappedSampler, float2(0.25, 0.25)));
    outWords[1] = pack_rgba8(BRDFLookup.sample(ClampedSampler, float2(0.75, 0.25)));
    outWords[2] = pack_rgba8(BaseColorMap.sample(WrappedSampler, float2(-0.25, 0.25)));
    outWords[3] = pack_rgba8(BRDFLookup.sample(ClampedSampler, float2(-0.25, 0.25)));
}
"""


def build_success_result(graph_id: Any, artifacts: dict[str, Any], probe: dict[str, Any]) -> dict[str, Any]:
    return {
        "kind": "TixlMeshDrawTextureSamplerBindingProof",
        "graphId": graph_id,
        "ok": True,
        "status": "proven_tixl_mesh_draw_texture_sampler_binding",
        "message": "proved a bounded handwritten MSL adapter mapping for t2/t7 textures and s0/s1 samplers with a real Metal sentinel probe",
        "inputArtifacts": artifacts,
        "bindingLedger": {
            "boundNow": bound_now(),
            "boundedTextureSamplerMappingSubset": BOUND_SUBSET,
            "notProven": [
                "t3 EmissiveColorMap",
                "t4 RSMOMap",
                "t5 NormalMap",
                "t6 PrefilteredSpecular TextureCube",
                "t8+ shadergraph resources",
                "full texture/sampler set",
            ],
        },
        "evidence": {
            "metalProbeStatus": probe.get("status"),
            "textureBindings": probe.get("textureBindings"),
            "samplerBindings": probe.get("samplerBindings"),
            "sentinelReadback": {
                "expectedWords": probe.get("expectedWords"),
                "actualWords": probe.get("actualWords"),
            },
        },
        "claims": claim_flags(True),
    }


def default_result(
    graph_id: Any,
    status: str,
    source_summary: dict[str, Any],
    prior_summary: dict[str, Any],
    strategy_summary: dict[str, Any],
) -> dict[str, Any]:
    return {
        "kind": "TixlMeshDrawTextureSamplerBindingProof",
        "graphId": graph_id,
        "ok": False,
        "status": status,
        "inputArtifacts": {
            "sourceAudit": source_summary,
            "priorResourceBinding": prior_summary,
            "explicitTranslationStrategy": strategy_summary,
        },
        "bindingLedger": {
            "boundNow": [],
            "boundedTextureSamplerMappingSubset": [],
        },
        "claims": claim_flags(False),
    }


def claim_flags(proven: bool) -> dict[str, bool]:
    return {
        "sourceAuditArtifactConsumed": proven,
        "priorResourceBindingArtifactConsumed": proven,
        "actualMetalTextureSamplerProbeRan": proven,
        "t2BaseColorMapBindingProven": proven,
        "t7BrdfLookupBindingProven": proven,
        "s0WrappedSamplerBindingProven": proven,
        "s1ClampedSamplerBindingProven": proven,
        "boundedTextureSamplerMappingProven": proven,
        "fullPbrResourceBinding": False,
        "t8ShadergraphResourcesExpanded": False,
        "backendReplacementReady": False,
        "hlslToMslTranslation": False,
        "tixlRuntimeParity": False,
        "pbrVisualCorrectness": False,
        "rendererIntegrationComplete": False,
        "constantBufferAdapterComplete": False,
    }


def bound_now() -> list[dict[str, Any]]:
    return [
        {
            "sourceRegister": register,
            "sourceName": name,
            "sourceKind": kind,
            "metalBinding": metal,
            "sentinelWord": sentinel,
            "observedIn": "actual Metal texture/sampler compute probe",
        }
        for register, name, kind, metal, sentinel in TEXTURE_SAMPLER_MAPPING
    ]


def validate_probe_payload(payload: dict[str, Any] | None) -> bool:
    if not isinstance(payload, dict):
        return False
    return (
        payload.get("status") == "proven"
        and payload.get("ok") is True
        and payload.get("actualCompilerRan") is True
        and payload.get("actualMetalRan") is True
        and payload.get("expectedWords") == EXPECTED_WORDS
        and payload.get("actualWords") == EXPECTED_WORDS
        and payload.get("textureBindings") == {"BaseColorMap": 2, "BRDFLookup": 7}
        and payload.get("samplerBindings") == {"WrappedSampler": 0, "ClampedSampler": 1}
    )


def error_from_probe(probe: dict[str, Any]) -> dict[str, Any]:
    status = str(probe.get("status") or "probe_failed")
    if status == "blocked_metal_device_unavailable":
        code = "tixl_mesh_draw_texture_sampler_binding.device_unavailable"
    elif status == "compile_failed":
        code = "tixl_mesh_draw_texture_sampler_binding.compile_failed"
    elif status == "pipeline_failed":
        code = "tixl_mesh_draw_texture_sampler_binding.pipeline_failed"
    elif status == "sentinel_mismatch":
        code = "tixl_mesh_draw_texture_sampler_binding.sentinel_mismatch"
    else:
        code = "tixl_mesh_draw_texture_sampler_binding.probe_failed"
    error: dict[str, Any] = {"code": code, "message": clean_text(str(probe.get("message") or status))}
    if probe.get("compilerDiagnostic"):
        error["compilerDiagnostic"] = clean_text(str(probe["compilerDiagnostic"]))
    if probe.get("actualWords"):
        error["actualWords"] = probe["actualWords"]
    return error


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


def parse_probe_payload(stdout: str) -> dict[str, Any] | None:
    text = stdout.strip()
    if not text:
        return None
    try:
        return json.loads(text.splitlines()[-1])
    except json.JSONDecodeError:
        return None


def publish(
    out_dir: Path,
    result: dict[str, Any],
    trace: list[dict[str, Any]],
    errors: list[dict[str, Any]],
    generated_msl: str | None = None,
) -> None:
    write_json(out_dir / RESULT_NAME, result)
    write_json(out_dir / TRACE_NAME, trace)
    write_json(out_dir / ERRORS_NAME, errors)
    if generated_msl is not None:
        (out_dir / GENERATED_MSL_NAME).write_text(generated_msl, encoding="utf8")


def write_json(path: Path, payload: Any) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload, indent=2, ensure_ascii=False) + "\n", encoding="utf8")


def clear_optional_artifacts(out_dir: Path) -> None:
    target = out_dir / GENERATED_MSL_NAME
    if target.exists():
        target.unlink()


def clean_text(text: str) -> str:
    return " ".join(text.split())


def clean_message(text: str, repo_root: Path) -> str:
    cleaned = clean_text(text.replace(str(repo_root), "."))
    try:
        cleaned = cleaned.replace(str(Path.home()), "~")
    except RuntimeError:
        pass
    return cleaned


def display_path(path: Path | None, repo_root: Path) -> str | None:
    if path is None:
        return None
    try:
        return str(path.resolve().relative_to(repo_root))
    except ValueError:
        return f"outside_repo/{path.name}"


if __name__ == "__main__":
    raise SystemExit(main())

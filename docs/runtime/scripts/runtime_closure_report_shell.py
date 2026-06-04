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
    "prove_native_mesh_resource_binding_against_pbrvertex_faceindices_layout",
    "prove_or_reject_hlsl_to_msl_translation_for_mesh_draw",
    "replace_bounded_backend_interface_after_resource_binding_and_hlsl_to_msl_proof",
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
        report = build_report({}, {}, {}, {}, {}, [], errors, {}, repo_root)
        write_artifacts(out_dir, report, trace, errors)
        return 1

    artifact_dir = resolve_repo_path(repo_root, fixture_path, fixture.get("nativeRenderPipelineArtifacts"))
    if artifact_dir is None:
        errors.append({"code": "runtime_closure.missing_native_render_pipeline_artifacts"})
        artifact_dir = Path("")

    artifacts = read_pipeline_artifacts(artifact_dir, repo_root, errors)
    trace.append({"op": "readNativeRenderPipelineArtifacts", "artifactDir": display_path(artifact_dir, repo_root)})
    report = build_report(
        artifacts.get("pipelineSummary", {}),
        artifacts.get("commandStreamSummary", {}),
        artifacts.get("shaderProgramPackage", {}),
        artifacts.get("nativeBackendInterface", {}),
        artifacts.get("backendStatus", {}),
        artifacts.get("pipelineErrors", []),
        errors,
        artifacts.get("evidence", {}),
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
        required_next.extend(REQUIRED_NEXT_FOR_BOUNDED_NATIVE_BACKEND)
    elif native_draw_shader_status not in (None, "supported"):
        broken.append("native_hlsl_metal_compile")

    native_compile_is_bounded = "native_hlsl_metal_compile" in bounded
    ok = (
        not errors
        and not broken
        and "core_headless_pipeline" in proven
        and native_compile_is_bounded
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
        },
        "evidence": evidence,
    }


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


def read_json(path: Path, errors: list[dict[str, Any]], code: str, fallback: Any | None = None) -> Any:
    try:
        return json.loads(path.read_text(encoding="utf8"))
    except Exception as exc:
        errors.append({"code": code, "path": str(path), "message": str(exc)})
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

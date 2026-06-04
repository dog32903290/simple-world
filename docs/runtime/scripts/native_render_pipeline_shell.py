#!/usr/bin/env python3
"""
Connect ResourceLifetime, ShaderUniformBinding, ShaderProgram, and
NativeRendererBackend shells.

This runner is glue only. It proves the existing layer artifacts can be wired
into one headless frame without hidden UI state.
"""

from __future__ import annotations

import json
import subprocess
import sys
from pathlib import Path
from typing import Any


def main() -> int:
    if len(sys.argv) != 3:
        print("usage: native_render_pipeline_shell.py <native_render_pipeline.graph.json> <out_dir>", file=sys.stderr)
        return 2

    fixture_path = Path(sys.argv[1]).expanduser().resolve()
    out_dir = Path(sys.argv[2]).expanduser().resolve()
    out_dir.mkdir(parents=True, exist_ok=True)

    errors: list[dict[str, Any]] = []
    fixture = read_json(fixture_path, errors, "native_render_pipeline.fixture_read_failed")
    if fixture is None:
        write_artifacts(out_dir, {}, [], {}, {}, {}, {}, {}, {}, {}, {}, errors)
        return 1

    summary, trace, render_pass_plan, resource_access_ledger, resource_registry, invalidation_ledger, command_summary, command_result, frame_input, captured_frame, run_errors = run_pipeline(fixture, fixture_path, out_dir)
    errors.extend(run_errors)
    write_artifacts(out_dir, summary, trace, render_pass_plan, resource_access_ledger, resource_registry, invalidation_ledger, command_summary, command_result, frame_input, captured_frame, errors)
    return 0 if not errors else 1


def run_pipeline(
    fixture: dict[str, Any],
    fixture_path: Path,
    out_dir: Path,
) -> tuple[dict[str, Any], list[dict[str, Any]], dict[str, Any], dict[str, Any], dict[str, Any], dict[str, Any], dict[str, Any], dict[str, Any], dict[str, Any], dict[str, Any], list[dict[str, Any]]]:
    repo_root = Path(__file__).resolve().parents[3]
    errors: list[dict[str, Any]] = []
    trace: list[dict[str, Any]] = [{
        "op": "loadNativeRenderPipeline",
        "graphId": fixture.get("graphId"),
    }]

    uniform_fixture = resolve_repo_path(repo_root, fixture_path, fixture.get("shaderUniformBindingFixture"))
    program_fixture = resolve_repo_path(repo_root, fixture_path, fixture.get("shaderProgramFixture"))
    render_graph_fixture = resolve_repo_path(repo_root, fixture_path, fixture.get("renderGraphFixture"))
    resource_fixture = resolve_repo_path(repo_root, fixture_path, fixture.get("resourceLifetimeFixture"))
    command_fixture = resolve_repo_path(repo_root, fixture_path, fixture.get("commandStreamFixture"))
    backend_fixture = resolve_repo_path(repo_root, fixture_path, fixture.get("nativeBackendFixture"))

    render_graph_dir = out_dir / "render_graph"
    resource_dir = out_dir / "resource_lifetime"
    command_dir = out_dir / "command_stream"
    uniform_dir = out_dir / "shader_uniform_binding"
    program_dir = out_dir / "shader_program"
    backend_dir = out_dir / "native_backend"

    run_shell(repo_root, "docs/runtime/scripts/render_graph_shell.py", render_graph_fixture, render_graph_dir, "renderGraph", trace, errors)
    render_pass_plan = read_json_file(render_graph_dir / "render_pass_plan.json", {})
    resource_access_ledger = read_json_file(render_graph_dir / "resource_access_ledger.json", {})
    render_graph_errors = read_json_file(render_graph_dir / "render_graph_errors.json", [])

    run_resource_lifetime_shell(repo_root, resource_fixture, resource_access_ledger_path=render_graph_dir / "resource_access_ledger.json", out_dir=resource_dir, trace=trace, errors=errors)
    resource_registry = read_json_file(resource_dir / "resource_registry.json", {})
    invalidation_ledger = read_json_file(resource_dir / "view_invalidation_ledger.json", {})
    resource_errors = read_json_file(resource_dir / "resource_lifetime_errors.json", [])

    command_summary: dict[str, Any] = {}
    command_result: dict[str, Any] = {}
    run_command_stream_shell(repo_root, command_fixture, resource_dir / "resource_registry.json", render_graph_dir / "render_pass_plan.json", command_dir, trace, errors)
    command_summary = read_json_file(command_dir / "command_stream_summary.json", {})
    command_result = read_json_file(command_dir / "command_stream_result.json", {})
    command_errors = read_json_file(command_dir / "command_stream_pipeline_errors.json", [])

    run_shell(repo_root, "docs/runtime/scripts/shader_uniform_binding_shell.py", uniform_fixture, uniform_dir, "uniform", trace, errors)
    uniform_snapshot = read_json_file(uniform_dir / "shader_uniform_snapshot.json", {})
    uniform_bindings = read_json_file(uniform_dir / "shader_uniform_bindings.json", {})
    frame_input = read_json_file(uniform_dir / "render_frame_input.json", {})

    run_shell(repo_root, "docs/runtime/scripts/shader_program_shell.py", program_fixture, program_dir, "shaderProgram", trace, errors)
    shader_package = read_json_file(program_dir / "shader_program_package.json", {})

    backend_input_fixture = write_backend_fixture_with_frame_input(backend_fixture, frame_input, program_fixture, out_dir, errors)
    run_shell(repo_root, "docs/runtime/scripts/native_renderer_backend_interface_shell.py", backend_input_fixture, backend_dir, "nativeBackend", trace, errors)
    native_interface = read_json_file(backend_dir / "native_backend_interface.json", {})
    captured_frame = read_json_file(backend_dir / "captured_frame_contract.json", {})
    backend_status = read_json_file(backend_dir / "backend_status.json", {})

    compatibility_errors = validate_compatibility(
        resource_registry,
        render_graph_errors,
        resource_errors,
        command_summary,
        command_result,
        command_errors,
        uniform_snapshot,
        uniform_bindings,
        frame_input,
        shader_package,
        native_interface,
        captured_frame,
    )
    errors.extend(compatibility_errors)
    for error in compatibility_errors:
        trace.append({"op": "pipeline.compatibilityError", **error})

    summary = {
        "kind": "NativeRenderPipelineProof",
        "graphId": fixture.get("graphId"),
        "ok": not errors,
        "layers": {
            "shaderUniformBinding": uniform_snapshot.get("status"),
            "shaderProgram": shader_package.get("status"),
            "resourceLifetime": "ok" if not resource_errors else "error",
            "renderGraph": "ok" if not render_graph_errors else "error",
            "commandStream": "ok" if command_summary.get("ok") is True else "error",
            "nativeBackend": backend_status.get("status"),
            "capturedFrame": captured_frame.get("status"),
        },
        "liveTextureCount": count_live_textures(resource_registry),
        "renderGraphPassOrder": render_pass_plan.get("passOrder", []),
        "renderGraphBarrierCount": resource_access_ledger.get("barrierCount", 0),
        "invalidatedViewCount": invalidation_ledger.get("count"),
        "drawCalls": command_summary.get("drawCalls", 0),
        "targetProgramId": shader_package.get("programId"),
        "loudness": frame_input.get("loudness"),
        "frameIndex": frame_input.get("frameIndex"),
        "importsOldUi": native_interface.get("importsOldUi"),
        "nonBlackSample": captured_frame.get("nonBlackSample"),
    }
    trace.append({
        "op": "publishNativeRenderPipelineArtifacts",
        "ok": not errors,
    })
    return summary, trace, render_pass_plan, resource_access_ledger, resource_registry, invalidation_ledger, command_summary, command_result, frame_input, captured_frame, errors


def run_shell(
    repo_root: Path,
    script_relative: str,
    fixture_path: Path | None,
    out_dir: Path,
    label: str,
    trace: list[dict[str, Any]],
    errors: list[dict[str, Any]],
) -> None:
    if fixture_path is None:
        errors.append({"code": f"native_render_pipeline.missing_{label}_fixture"})
        return
    script_path = repo_root / script_relative
    trace.append({"op": f"{label}.begin", "fixture": str(fixture_path)})
    result = subprocess.run(
        ["python3", str(script_path), str(fixture_path), str(out_dir)],
        cwd=repo_root,
        text=True,
        capture_output=True,
        check=False,
    )
    trace.append({"op": f"{label}.end", "status": result.returncode})
    if result.returncode != 0:
        errors.append({
            "code": f"native_render_pipeline.{label}_failed",
            "status": result.returncode,
            "stdout": result.stdout,
            "stderr": result.stderr,
        })


def run_command_stream_shell(
    repo_root: Path,
    fixture_path: Path | None,
    resource_registry_path: Path,
    render_pass_plan_path: Path,
    out_dir: Path,
    trace: list[dict[str, Any]],
    errors: list[dict[str, Any]],
) -> None:
    if fixture_path is None:
        errors.append({"code": "native_render_pipeline.missing_commandStream_fixture"})
        return
    script_path = repo_root / "docs/runtime/scripts/command_stream_pipeline_shell.py"
    trace.append({"op": "commandStream.begin", "fixture": str(fixture_path)})
    result = subprocess.run(
        ["python3", str(script_path), str(fixture_path), str(resource_registry_path), str(out_dir), str(render_pass_plan_path)],
        cwd=repo_root,
        text=True,
        capture_output=True,
        check=False,
    )
    trace.append({"op": "commandStream.end", "status": result.returncode})
    if result.returncode != 0:
        errors.append({
            "code": "native_render_pipeline.commandStream_failed",
            "status": result.returncode,
            "stdout": result.stdout,
            "stderr": result.stderr,
        })


def run_resource_lifetime_shell(
    repo_root: Path,
    fixture_path: Path | None,
    resource_access_ledger_path: Path,
    out_dir: Path,
    trace: list[dict[str, Any]],
    errors: list[dict[str, Any]],
) -> None:
    if fixture_path is None:
        errors.append({"code": "native_render_pipeline.missing_resourceLifetime_fixture"})
        return
    script_path = repo_root / "docs/runtime/scripts/resource_lifetime_shell.py"
    trace.append({"op": "resourceLifetime.begin", "fixture": str(fixture_path), "resourceAccessLedger": str(resource_access_ledger_path)})
    result = subprocess.run(
        ["python3", str(script_path), str(fixture_path), str(out_dir), str(resource_access_ledger_path)],
        cwd=repo_root,
        text=True,
        capture_output=True,
        check=False,
    )
    trace.append({"op": "resourceLifetime.end", "status": result.returncode})
    if result.returncode != 0:
        errors.append({
            "code": "native_render_pipeline.resourceLifetime_failed",
            "status": result.returncode,
            "stdout": result.stdout,
            "stderr": result.stderr,
        })


def write_backend_fixture_with_frame_input(
    backend_fixture_path: Path | None,
    frame_input: dict[str, Any],
    shader_program_fixture: Path | None,
    out_dir: Path,
    errors: list[dict[str, Any]],
) -> Path | None:
    if backend_fixture_path is None:
        errors.append({"code": "native_render_pipeline.missing_backend_fixture"})
        return None
    try:
        backend_fixture = json.loads(backend_fixture_path.read_text(encoding="utf8"))
    except Exception as exc:
        errors.append({"code": "native_render_pipeline.backend_fixture_read_failed", "message": str(exc)})
        return None
    backend_fixture["shaderProgramFixture"] = str(shader_program_fixture) if shader_program_fixture is not None else ""
    backend_fixture["requestedFrame"] = {
        "resolution": frame_input.get("resolution"),
        "viewportScale": frame_input.get("viewportScale", 1),
        "timeSeconds": frame_input.get("timeSeconds", 0.0),
        "frameIndex": frame_input.get("frameIndex", 0),
        "loudness": frame_input.get("loudness", 0.0),
    }
    generated_path = out_dir / "native_backend_input.graph.json"
    generated_path.write_text(json.dumps(backend_fixture, indent=2, ensure_ascii=False) + "\n", encoding="utf8")
    return generated_path


def validate_compatibility(
    resource_registry: dict[str, Any],
    render_graph_errors: list[dict[str, Any]],
    resource_errors: list[dict[str, Any]],
    command_summary: dict[str, Any],
    command_result: dict[str, Any],
    command_errors: list[dict[str, Any]],
    uniform_snapshot: dict[str, Any],
    uniform_bindings: dict[str, Any],
    frame_input: dict[str, Any],
    shader_package: dict[str, Any],
    native_interface: dict[str, Any],
    captured_frame: dict[str, Any],
) -> list[dict[str, Any]]:
    errors = []
    if render_graph_errors:
        errors.append({
            "code": "native_render_pipeline.render_graph_failed",
            "renderGraphErrors": render_graph_errors,
        })
    if resource_errors:
        errors.append({
            "code": "native_render_pipeline.resource_lifetime_failed",
            "resourceErrors": resource_errors,
        })
    if count_live_textures(resource_registry) < 1:
        errors.append({"code": "native_render_pipeline.no_live_texture_resources"})
    if command_errors:
        errors.append({
            "code": "native_render_pipeline.command_stream_failed",
            "commandErrors": command_errors,
        })
    if command_summary.get("ok") is not True:
        errors.append({"code": "native_render_pipeline.command_stream_not_ok"})
    if int(command_summary.get("drawCalls", 0)) < 1:
        errors.append({"code": "native_render_pipeline.no_draw_calls"})
    if command_result.get("ok") is not True:
        errors.append({"code": "native_render_pipeline.command_result_not_ok"})
    target_program_id = uniform_bindings.get("targetProgramId")
    shader_program_id = shader_package.get("programId")
    if target_program_id != shader_program_id:
        errors.append({
            "code": "native_render_pipeline.program_mismatch",
            "uniformTargetProgramId": target_program_id,
            "shaderProgramId": shader_program_id,
        })
    if uniform_snapshot.get("ok") is not True:
        errors.append({"code": "native_render_pipeline.uniform_snapshot_not_ok"})
    if frame_input.get("sourceSnapshotOk") is not True:
        errors.append({"code": "native_render_pipeline.frame_input_not_from_valid_snapshot"})
    if shader_package.get("status") != "ok":
        errors.append({"code": "native_render_pipeline.shader_program_not_ok"})
    if native_interface.get("importsOldUi") is not False:
        errors.append({"code": "native_render_pipeline.old_ui_imported"})
    if captured_frame.get("nonBlackSample") is not True:
        errors.append({"code": "native_render_pipeline.black_or_missing_frame_sample"})
    return errors


def count_live_textures(resource_registry: dict[str, Any]) -> int:
    return sum(
        1
        for texture in resource_registry.get("resources", {}).values()
        if texture.get("kind") == "Texture2D" and texture.get("disposed") is not True
    )


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


def read_json(path: Path, errors: list[dict[str, Any]], code: str) -> Any | None:
    try:
        return json.loads(path.read_text(encoding="utf8"))
    except Exception as exc:
        errors.append({"code": code, "path": str(path), "message": str(exc)})
        return None


def read_json_file(path: Path, fallback: Any) -> Any:
    try:
        return json.loads(path.read_text(encoding="utf8"))
    except Exception:
        return fallback


def write_artifacts(
    out_dir: Path,
    summary: dict[str, Any],
    trace: list[dict[str, Any]],
    render_pass_plan: dict[str, Any],
    resource_access_ledger: dict[str, Any],
    resource_registry: dict[str, Any],
    invalidation_ledger: dict[str, Any],
    command_summary: dict[str, Any],
    command_result: dict[str, Any],
    frame_input: dict[str, Any],
    captured_frame: dict[str, Any],
    errors: list[dict[str, Any]],
) -> None:
    write_json(out_dir / "pipeline_summary.json", summary)
    write_json(out_dir / "pipeline_trace.json", trace)
    write_json(out_dir / "render_pass_plan.json", render_pass_plan)
    write_json(out_dir / "resource_access_ledger.json", resource_access_ledger)
    write_json(out_dir / "resource_registry.json", resource_registry)
    write_json(out_dir / "view_invalidation_ledger.json", invalidation_ledger)
    write_json(out_dir / "command_stream_summary.json", command_summary)
    write_json(out_dir / "command_stream_result.json", command_result)
    write_json(out_dir / "render_frame_input.json", frame_input)
    write_json(out_dir / "captured_frame_contract.json", captured_frame)
    write_json(out_dir / "native_render_pipeline_errors.json", errors)


def write_json(path: Path, payload: Any) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload, indent=2, ensure_ascii=False) + "\n", encoding="utf8")


if __name__ == "__main__":
    raise SystemExit(main())

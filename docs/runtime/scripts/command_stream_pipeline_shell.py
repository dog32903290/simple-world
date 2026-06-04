#!/usr/bin/env python3
"""
Execute a CommandStream against ResourceLifetime-produced TextureView identity.

This is command validation for the native pipeline, not a GPU draw.
"""

from __future__ import annotations

import json
import sys
from pathlib import Path
from typing import Any

from native_command_stream_api import (
    CommandStream,
    RenderState,
    make_draw_command,
    make_input_assembler_command,
    make_output_merger_command,
    make_rasterizer_command,
    make_shader_stage_command,
)
from native_resource_api import TextureViewHandle


def main() -> int:
    if len(sys.argv) not in {4, 5}:
        print("usage: command_stream_pipeline_shell.py <command_stream_pipeline.graph.json> <resource_registry.json> <out_dir> [render_pass_plan.json]", file=sys.stderr)
        return 2

    fixture_path = Path(sys.argv[1]).expanduser().resolve()
    registry_path = Path(sys.argv[2]).expanduser().resolve()
    out_dir = Path(sys.argv[3]).expanduser().resolve()
    render_pass_plan_path = Path(sys.argv[4]).expanduser().resolve() if len(sys.argv) == 5 else None
    out_dir.mkdir(parents=True, exist_ok=True)

    errors: list[dict[str, Any]] = []
    fixture = read_json(fixture_path, errors, "command_stream_pipeline.fixture_read_failed")
    registry = read_json(registry_path, errors, "command_stream_pipeline.registry_read_failed")
    render_pass_plan = read_json(render_pass_plan_path, errors, "command_stream_pipeline.render_pass_plan_read_failed") if render_pass_plan_path else None
    if fixture is None or registry is None or errors:
        write_artifacts(out_dir, {}, [], {}, errors)
        return 1

    result, trace, summary, run_errors = run_command_stream(fixture, registry, render_pass_plan)
    errors.extend(run_errors)
    write_artifacts(out_dir, result, trace, summary, errors)
    return 0 if not errors else 1


def run_command_stream(
    fixture: dict[str, Any],
    registry: dict[str, Any],
    render_pass_plan: dict[str, Any] | None = None,
) -> tuple[dict[str, Any], list[dict[str, Any]], dict[str, Any], list[dict[str, Any]]]:
    errors: list[dict[str, Any]] = []
    trace = [{"op": "loadCommandStreamPipeline", "graphId": fixture.get("graphId")}]
    command = fixture.get("command", {})
    views = registry.get("views", {})
    render_target = fixture.get("renderTarget", {})
    render_graph_binding = resolve_render_graph_binding(fixture, render_pass_plan, errors)
    if render_graph_binding:
        trace.append({"op": "resolveRenderGraphTarget", **render_graph_binding})
    color_view_id = render_graph_binding.get("colorViewId") if render_graph_binding else render_target.get("colorViewId")
    color_view = view_from_registry(views, color_view_id, errors)
    depth_view = view_from_registry(views, render_target.get("depthViewId"), errors) if render_target.get("depthViewId") else None

    if color_view is None:
        errors.append({"code": "command_stream_pipeline.missing_color_rtv"})
        result = {"ok": False, "trace": [], "errors": errors, "stats": {}, "finalState": {}}
    else:
        stream = CommandStream([
            make_input_assembler_command(command),
            make_shader_stage_command(command),
            make_rasterizer_command(command),
            make_output_merger_command(
                renderTargetViews=[color_view],
                depthStencilView=depth_view,
                outputMergerState=command.get("outputMergerState"),
            ),
            make_draw_command(command),
        ])
        result = stream.execute(RenderState())
        errors.extend(result.get("errors", []))

    trace.append({"op": "commandStream.execute", "result": result})
    summary = {
        "kind": "CommandStreamPipelineProof",
        "ok": not errors and result.get("ok") is True,
        "renderGraphPassId": render_graph_binding.get("passId") if render_graph_binding else None,
        "colorViewId": color_view_id,
        "drawCalls": result.get("stats", {}).get("drawCalls", 0),
        "triangles": result.get("stats", {}).get("triangles", 0),
        "resourceBarriers": result.get("stats", {}).get("resourceBarriers", 0),
        "renderTargetViews": result.get("trace", [{}])[-1].get("renderTargetViews", []) if result.get("trace") else [],
    }
    trace.append({"op": "publishCommandStreamPipelineArtifacts", "ok": summary["ok"]})
    return result, trace, summary, errors


def resolve_render_graph_binding(
    fixture: dict[str, Any],
    render_pass_plan: dict[str, Any] | None,
    errors: list[dict[str, Any]],
) -> dict[str, Any] | None:
    if render_pass_plan is None:
        return None
    binding = fixture.get("renderGraphBinding", {})
    pass_id = binding.get("passId")
    command_name = binding.get("command", "commandStream")
    color_access = binding.get("colorAccess", "RenderTargetWrite")
    color_view_type = binding.get("colorViewType", "RTV")
    passes = render_pass_plan.get("passes", [])
    matched_pass = next((row for row in passes if row.get("passId") == pass_id), None)
    if matched_pass is None:
        errors.append({"code": "command_stream_pipeline.render_graph_pass_missing", "passId": pass_id})
        return None
    if command_name not in matched_pass.get("commands", []):
        errors.append({
            "code": "command_stream_pipeline.render_graph_command_missing",
            "passId": pass_id,
            "command": command_name,
        })
        return None
    color_write = next(
        (
            write
            for write in matched_pass.get("writes", [])
            if write.get("access") == color_access
        ),
        None,
    )
    if color_write is None:
        errors.append({
            "code": "command_stream_pipeline.render_graph_color_write_missing",
            "passId": pass_id,
            "access": color_access,
        })
        return None
    resource_id = color_write.get("resource")
    return {
        "passId": pass_id,
        "command": command_name,
        "colorResourceId": resource_id,
        "colorViewId": f"{resource_id}.{color_view_type.lower()}",
    }


def view_from_registry(views: dict[str, Any], view_id: str | None, errors: list[dict[str, Any]]) -> TextureViewHandle | None:
    if not view_id:
        return None
    payload = views.get(view_id)
    if payload is None:
        errors.append({"code": "command_stream_pipeline.missing_view", "viewId": view_id})
        return None
    return TextureViewHandle(
        ok=bool(payload.get("ok")),
        textureId=payload.get("textureId"),
        type=payload.get("type"),
        reason=payload.get("reason"),
        format=payload.get("format"),
        dimension=payload.get("dimension"),
        firstArraySlice=payload.get("firstArraySlice"),
        arraySize=payload.get("arraySize"),
    )


def read_json(path: Path, errors: list[dict[str, Any]], code: str) -> Any | None:
    try:
        return json.loads(path.read_text(encoding="utf8"))
    except Exception as exc:
        errors.append({"code": code, "path": str(path), "message": str(exc)})
        return None


def write_artifacts(
    out_dir: Path,
    result: dict[str, Any],
    trace: list[dict[str, Any]],
    summary: dict[str, Any],
    errors: list[dict[str, Any]],
) -> None:
    write_json(out_dir / "command_stream_result.json", result)
    write_json(out_dir / "command_stream_trace.json", trace)
    write_json(out_dir / "command_stream_summary.json", summary)
    write_json(out_dir / "command_stream_pipeline_errors.json", errors)


def write_json(path: Path, payload: Any) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload, indent=2, ensure_ascii=False) + "\n", encoding="utf8")


if __name__ == "__main__":
    raise SystemExit(main())

#!/usr/bin/env python3
"""
Run the first replayable native runtime lane.

This orchestrates existing Material/PBR command generation and the software
native renderer proof, then adds a resource registry and TextureView identity
report. It is not a GPU backend.
"""

from __future__ import annotations

import json
import subprocess
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
from native_resource_api import allocate_render_target_resources, create_default_texture_views

REPO = Path(__file__).resolve().parents[3]
MATERIAL_SCRIPT = REPO / "docs/runtime/scripts/material_pbr_scope_shell.py"
RENDER_SCRIPT = REPO / "docs/runtime/scripts/native_render_shell.py"


def main() -> int:
    if len(sys.argv) != 3:
        print("usage: native_runtime_lane.py <native_runtime_lane.graph.json> <out_dir>", file=sys.stderr)
        return 2

    graph_path = Path(sys.argv[1]).expanduser().resolve()
    out_dir = Path(sys.argv[2]).expanduser().resolve()
    out_dir.mkdir(parents=True, exist_ok=True)

    trace: list[dict[str, Any]] = []
    errors: list[dict[str, Any]] = []
    registry = {"resources": {}}
    texture_views = {"views": {}}

    graph = read_json(graph_path, errors, "native_lane.graph_read_failed")
    trace.append({"op": "loadGraph", "path": str(graph_path), "ok": graph is not None})
    if graph is None:
        write_lane_artifacts(out_dir, trace, errors, registry, texture_views)
        return 1

    material_out = out_dir / "material_pbr_scope"
    material_graph = REPO / graph.get("inputs", {}).get("materialGraph", "")
    material_result = run_stage([sys.executable, str(MATERIAL_SCRIPT), str(material_graph), str(material_out)])
    material_errors = read_json(material_out / "pbr_binding_errors.json", [], "native_lane.material_errors_read_failed") or []
    trace.append({
        "op": "runMaterialPbrScope",
        "ok": material_result.returncode == 0,
        "graph": str(material_graph),
        "outDir": str(material_out),
        "diagnostics": material_errors,
    })
    if material_result.returncode != 0:
        errors.append({
            "code": "native_lane.material_scope_failed",
            "stderr": material_result.stderr,
            "stdout": material_result.stdout,
            "stageErrors": material_errors,
        })
        write_lane_artifacts(out_dir, trace, errors, registry, texture_views)
        return 1

    command_path = material_out / "mesh_pbr_draw_command.json"
    command = read_json(command_path, errors, "native_lane.command_read_failed")
    if command is None or command.get("ok") is not True:
        errors.append({
            "code": "native_lane.material_scope_failed",
            "message": "Material/PBR stage did not publish an ok command.",
            "stageErrors": material_errors,
            "command": command,
        })
        write_lane_artifacts(out_dir, trace, errors, registry, texture_views)
        return 1

    render_target = graph.get("renderTarget", {})
    command = with_render_target_viewport(command, render_target)
    trace.append({"op": "commandStream.accept", "command": command})

    resource_registry = allocate_render_target_resources(render_target)
    registry = {"resources": resource_registry.to_json()["resources"]}
    trace.append({
        "op": "renderTarget.allocate",
        "resourceId": render_target.get("id"),
        "resources": list(registry["resources"].keys()),
    })

    texture_views = create_default_texture_views(resource_registry)
    trace.append({
        "op": "textureView.create",
        "views": list(texture_views["views"].keys()),
        "failedViews": [
            view_id for view_id, view in texture_views["views"].items()
            if view.get("ok") is not True
        ],
    })

    command_stream = CommandStream([
        make_input_assembler_command(command),
        make_shader_stage_command(command),
        make_rasterizer_command(command),
        make_output_merger_command(
            renderTargetViews=[resource_registry.views["rt.color.rtv"]],
            depthStencilView=resource_registry.views["rt.depth.dsv"],
        ),
        make_draw_command(command),
    ])
    command_result = command_stream.execute(RenderState())
    trace.append({
        "op": "commandStream.execute",
        "result": command_result,
    })
    if not command_result["ok"]:
        errors.extend(command_result["errors"])
        write_lane_artifacts(out_dir, trace, errors, registry, texture_views)
        return 1

    native_out = out_dir / "native_renderer"
    render_result = run_stage([sys.executable, str(RENDER_SCRIPT), str(command_path), str(native_out)])
    frame_stats = read_json(native_out / "frame_stats.json", errors, "native_lane.frame_stats_read_failed")
    trace.append({
        "op": "runNativeRenderer",
        "ok": render_result.returncode == 0,
        "outDir": str(native_out),
        "frameStats": frame_stats or {},
    })
    if render_result.returncode != 0:
        errors.append({
            "code": "native_lane.native_renderer_failed",
            "stderr": render_result.stderr,
            "stdout": render_result.stdout,
        })

    trace.append({
        "op": "publishArtifacts",
        "ok": not errors,
        "artifacts": [
            "native_runtime_lane_trace.json",
            "native_runtime_lane_errors.json",
            "resource_registry.json",
            "texture_views.json",
            "material_pbr_scope/mesh_pbr_draw_command.json",
            "native_renderer/frame.ppm",
        ],
    })
    write_lane_artifacts(out_dir, trace, errors, registry, texture_views)
    return 0 if not errors else 1


def run_stage(command: list[str]) -> subprocess.CompletedProcess[str]:
    return subprocess.run(command, cwd=str(REPO), text=True, capture_output=True, check=False)


def with_render_target_viewport(command: dict[str, Any], render_target: dict[str, Any]) -> dict[str, Any]:
    resolution = render_target.get("resolution", {})
    enriched = dict(command)
    enriched.setdefault("rasterizerState", {
        "fillMode": 3,
        "culling": "Back",
        "enableZTest": True,
        "enableZWrite": True,
    })
    enriched["viewports"] = [{
        "x": 0,
        "y": 0,
        "width": resolution.get("width"),
        "height": resolution.get("height"),
        "minDepth": 0,
        "maxDepth": 1,
    }]
    return enriched


def read_json(path: Path, errors: list[dict[str, Any]], code: str) -> Any | None:
    try:
        return json.loads(path.read_text(encoding="utf8"))
    except Exception as exc:
        errors.append({"code": code, "path": str(path), "message": str(exc)})
        return None


def write_lane_artifacts(
    out_dir: Path,
    trace: list[dict[str, Any]],
    errors: list[dict[str, Any]],
    registry: dict[str, Any],
    texture_views: dict[str, Any],
) -> None:
    out_dir.mkdir(parents=True, exist_ok=True)
    write_json(out_dir / "native_runtime_lane_trace.json", trace)
    write_json(out_dir / "native_runtime_lane_errors.json", errors)
    write_json(out_dir / "resource_registry.json", registry)
    write_json(out_dir / "texture_views.json", texture_views)


def write_json(path: Path, payload: Any) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload, indent=2, ensure_ascii=False) + "\n", encoding="utf8")


if __name__ == "__main__":
    raise SystemExit(main())

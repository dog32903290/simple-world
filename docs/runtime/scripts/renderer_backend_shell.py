#!/usr/bin/env python3
"""
Validate the RendererBackend contract fixture and emit backend artifacts.

This shell does not render. It proves backend capability selection, pass
planning, resource lifetime, and frame output boundaries.
"""

from __future__ import annotations

import json
import sys
from pathlib import Path
from typing import Any


def main() -> int:
    if len(sys.argv) != 3:
        print("usage: renderer_backend_shell.py <renderer_backend_contract.graph.json> <out_dir>", file=sys.stderr)
        return 2

    graph_path = Path(sys.argv[1]).expanduser().resolve()
    out_dir = Path(sys.argv[2]).expanduser().resolve()
    out_dir.mkdir(parents=True, exist_ok=True)

    errors: list[dict[str, Any]] = []
    graph = read_json(graph_path, errors, "renderer_backend.graph_read_failed")
    if graph is None:
        write_artifacts(out_dir, {}, {}, {}, {}, {}, errors)
        return 1

    capabilities = build_capability_report(graph)
    selection = select_backend(graph, capabilities)
    pass_plan = build_pass_plan(graph, selection)
    resource_lifetime = build_resource_lifetime_plan(graph, selection)
    frame_output = build_frame_output_contract(graph, selection)

    errors.extend(selection.get("errors", []))
    errors.extend(pass_plan.get("errors", []))
    errors.extend(resource_lifetime.get("errors", []))
    errors.extend(frame_output.get("errors", []))

    write_artifacts(out_dir, capabilities, selection, pass_plan, resource_lifetime, frame_output, errors)
    return 0 if not errors else 1


def build_capability_report(graph: dict[str, Any]) -> dict[str, Any]:
    return {
        "backends": {
            backend["id"]: {
                "role": backend["role"],
                "status": backend["status"],
                "capabilities": backend["capabilities"],
            }
            for backend in graph.get("backends", [])
        }
    }


def select_backend(graph: dict[str, Any], capability_report: dict[str, Any]) -> dict[str, Any]:
    request = graph["requestedRenderContract"]
    required = request.get("requiredCapabilities", [])
    resolution = request["resolution"]
    candidates: list[dict[str, Any]] = []

    for backend_id, backend in capability_report["backends"].items():
        missing = []
        capabilities = backend["capabilities"]
        if backend["status"] != "available":
            missing.append("backend not available")
        for capability in required:
            if capabilities.get(capability) is not True:
                missing.append(capability)
        max_texture_size = capabilities.get("maxTextureSize")
        if isinstance(max_texture_size, int):
            if resolution["width"] > max_texture_size or resolution["height"] > max_texture_size:
                missing.append("maxTextureSize")
        elif max_texture_size is None:
            missing.append("maxTextureSize")

        candidates.append({
            "backendId": backend_id,
            "ok": not missing,
            "missing": missing,
            "role": backend["role"],
        })

    selected = next((candidate for candidate in candidates if candidate["ok"]), None)
    errors = []
    if selected is None:
        errors.append({
            "code": "renderer_backend.no_backend_satisfies_required_capabilities",
            "requiredCapabilities": required,
        })
    return {
        "requestedResolution": resolution,
        "requiredCapabilities": required,
        "selectedBackend": None if selected is None else selected["backendId"],
        "candidates": candidates,
        "errors": errors,
    }


def build_pass_plan(graph: dict[str, Any], selection: dict[str, Any]) -> dict[str, Any]:
    passes = graph.get("renderGraph", {}).get("passes", [])
    errors = []
    if not passes:
        errors.append({"code": "renderer_backend.missing_render_passes"})
    if selection.get("selectedBackend") is None:
        errors.append({"code": "renderer_backend.pass_plan_without_backend"})
    return {
        "backendId": selection.get("selectedBackend"),
        "passes": passes,
        "passOrder": [render_pass.get("id") for render_pass in passes],
        "errors": errors,
    }


def build_resource_lifetime_plan(graph: dict[str, Any], selection: dict[str, Any]) -> dict[str, Any]:
    request_resolution = graph["requestedRenderContract"]["resolution"]
    resources = graph.get("resources", [])
    errors = []
    planned = []
    for resource in resources:
        resolution = resource.get("resolution", {})
        if resolution != request_resolution:
            errors.append({
                "code": "renderer_backend.resource_resolution_mismatch",
                "resourceId": resource.get("id"),
                "expected": request_resolution,
                "actual": resolution,
            })
        planned.append({
            "resourceId": resource["id"],
            "kind": resource["kind"],
            "role": resource.get("role"),
            "format": resource["format"],
            "resolution": resolution,
            "bindFlags": resource.get("bindFlags", []),
            "ownerPass": resource.get("ownerPass"),
            "lifetime": resource.get("lifetime"),
            "resizePolicy": resource.get("resizePolicy"),
            "disposePolicy": resource.get("disposePolicy"),
            "backendId": selection.get("selectedBackend"),
        })
    return {
        "resources": planned,
        "errors": errors,
    }


def build_frame_output_contract(graph: dict[str, Any], selection: dict[str, Any]) -> dict[str, Any]:
    frame_output = graph["requestedRenderContract"]["frameOutput"]
    backend_id = selection.get("selectedBackend")
    backend = next((item for item in graph.get("backends", []) if item["id"] == backend_id), None)
    errors = []
    if backend is None:
        errors.append({"code": "renderer_backend.frame_output_without_backend"})
        supported = []
    else:
        supported = backend["capabilities"].get("frameOutputKinds", [])
        if frame_output["kind"] not in supported:
            errors.append({
                "code": "renderer_backend.unsupported_frame_output",
                "backendId": backend_id,
                "frameOutput": frame_output["kind"],
                "supported": supported,
            })
    return {
        "backendId": backend_id,
        "frameOutput": frame_output,
        "supportedFrameOutputKinds": supported,
        "publishBoundary": "FrameOutput",
        "errors": errors,
    }


def read_json(path: Path, errors: list[dict[str, Any]], code: str) -> Any | None:
    try:
        return json.loads(path.read_text(encoding="utf8"))
    except Exception as exc:
        errors.append({"code": code, "path": str(path), "message": str(exc)})
        return None


def write_artifacts(
    out_dir: Path,
    capabilities: dict[str, Any],
    selection: dict[str, Any],
    pass_plan: dict[str, Any],
    resource_lifetime: dict[str, Any],
    frame_output: dict[str, Any],
    errors: list[dict[str, Any]],
) -> None:
    write_json(out_dir / "backend_capabilities.json", capabilities)
    write_json(out_dir / "backend_selection.json", selection)
    write_json(out_dir / "render_pass_plan.json", pass_plan)
    write_json(out_dir / "resource_lifetime_plan.json", resource_lifetime)
    write_json(out_dir / "frame_output_contract.json", frame_output)
    write_json(out_dir / "renderer_backend_errors.json", errors)


def write_json(path: Path, payload: Any) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload, indent=2, ensure_ascii=False) + "\n", encoding="utf8")


if __name__ == "__main__":
    raise SystemExit(main())

#!/usr/bin/env python3
"""
Validate a RenderGraph pass plan and emit pass/resource-access artifacts.

This shell does not render. It proves pass order and hazard visibility.
"""

from __future__ import annotations

import json
import sys
from pathlib import Path
from typing import Any

WRITE_ACCESSES = {"RenderTargetWrite", "DepthStencilWrite", "UAVWrite", "UnorderedAccessWrite"}
READ_ACCESSES = {"ShaderResourceRead", "FrameOutputRead"}


def main() -> int:
    if len(sys.argv) != 3:
        print("usage: render_graph_shell.py <render_graph_passes.graph.json> <out_dir>", file=sys.stderr)
        return 2

    graph_path = Path(sys.argv[1]).expanduser().resolve()
    out_dir = Path(sys.argv[2]).expanduser().resolve()
    out_dir.mkdir(parents=True, exist_ok=True)

    errors: list[dict[str, Any]] = []
    graph = read_json(graph_path, errors, "render_graph.graph_read_failed")
    if graph is None:
        write_artifacts(out_dir, [], {}, [], errors)
        return 1

    trace, pass_plan, ledger, run_errors = run_render_graph(graph)
    errors.extend(run_errors)
    write_artifacts(out_dir, trace, pass_plan, ledger, errors)
    return 0 if not errors else 1


def run_render_graph(graph: dict[str, Any]) -> tuple[list[dict[str, Any]], dict[str, Any], dict[str, Any], list[dict[str, Any]]]:
    errors: list[dict[str, Any]] = []
    passes = {render_pass["id"]: render_pass for render_pass in graph.get("passes", [])}
    resources = {resource["id"]: resource for resource in graph.get("resources", [])}
    pass_order = topological_pass_order(passes)
    trace: list[dict[str, Any]] = [{
        "op": "loadRenderGraph",
        "graphId": graph.get("graphId"),
        "frame": graph.get("frame"),
        "passOrder": pass_order,
    }]
    latest_access: dict[str, str] = {}
    barriers: list[dict[str, Any]] = []
    pass_rows: list[dict[str, Any]] = []

    for pass_id in pass_order:
        render_pass = passes[pass_id]
        pass_errors = validate_pass(render_pass, resources)
        errors.extend(pass_errors)
        trace.append({
            "op": "pass.begin",
            "passId": pass_id,
            "reads": render_pass.get("reads", []),
            "writes": render_pass.get("writes", []),
        })

        for access in render_pass.get("reads", []):
            update_access(trace, barriers, latest_access, access["resource"], access["access"])
        for access in render_pass.get("writes", []):
            update_access(trace, barriers, latest_access, access["resource"], access["access"])

        pass_rows.append({
            "passId": pass_id,
            "domain": render_pass.get("domain"),
            "dependsOn": render_pass.get("dependsOn", []),
            "reads": render_pass.get("reads", []),
            "writes": render_pass.get("writes", []),
            "commands": render_pass.get("commands", []),
            "clearPolicy": render_pass.get("clearPolicy"),
            "publish": render_pass.get("publish"),
        })
        trace.append({"op": "pass.end", "passId": pass_id, "ok": not pass_errors})

    pass_plan = {
        "frame": graph.get("frame"),
        "passOrder": pass_order,
        "passes": pass_rows,
    }
    ledger = {
        "resources": resources,
        "latestAccess": latest_access,
        "resourceBarriers": barriers,
        "barrierCount": len(barriers),
    }
    trace.append({
        "op": "publishArtifacts",
        "ok": not errors,
        "barrierCount": len(barriers),
    })
    return trace, pass_plan, ledger, errors


def update_access(
    trace: list[dict[str, Any]],
    barriers: list[dict[str, Any]],
    latest_access: dict[str, str],
    resource_id: str,
    access: str,
) -> None:
    before = latest_access.get(resource_id)
    if before and should_barrier(before, access):
        barrier = {
            "op": "resourceBarrier",
            "resource": resource_id,
            "before": before,
            "after": access,
        }
        trace.append(barrier)
        barriers.append({key: barrier[key] for key in ["resource", "before", "after"]})
    latest_access[resource_id] = access


def should_barrier(before: str, after: str) -> bool:
    if before == after:
        return False
    return before in WRITE_ACCESSES and after in READ_ACCESSES.union(WRITE_ACCESSES)


def validate_pass(render_pass: dict[str, Any], resources: dict[str, Any]) -> list[dict[str, Any]]:
    errors = []
    for direction in ["reads", "writes"]:
        for access in render_pass.get(direction, []):
            if access.get("resource") not in resources:
                errors.append({
                    "code": "render_graph.missing_resource",
                    "passId": render_pass.get("id"),
                    "resource": access.get("resource"),
                })
            if direction == "reads" and access.get("access") not in READ_ACCESSES:
                errors.append({
                    "code": "render_graph.invalid_read_access",
                    "passId": render_pass.get("id"),
                    "access": access.get("access"),
                })
            if direction == "writes" and access.get("access") not in WRITE_ACCESSES:
                errors.append({
                    "code": "render_graph.invalid_write_access",
                    "passId": render_pass.get("id"),
                    "access": access.get("access"),
                })
    return errors


def topological_pass_order(passes: dict[str, dict[str, Any]]) -> list[str]:
    incoming = {
        pass_id: set(render_pass.get("dependsOn", []))
        for pass_id, render_pass in passes.items()
    }
    ready = sorted(pass_id for pass_id, deps in incoming.items() if not deps)
    order: list[str] = []
    while ready:
        pass_id = ready.pop(0)
        order.append(pass_id)
        for candidate, deps in incoming.items():
            if pass_id in deps:
                deps.discard(pass_id)
                if not deps and candidate not in order and candidate not in ready:
                    ready.append(candidate)
    if len(order) != len(passes):
        raise ValueError("render graph pass cycle detected")
    return order


def read_json(path: Path, errors: list[dict[str, Any]], code: str) -> Any | None:
    try:
        return json.loads(path.read_text(encoding="utf8"))
    except Exception as exc:
        errors.append({"code": code, "path": str(path), "message": str(exc)})
        return None


def write_artifacts(
    out_dir: Path,
    trace: list[dict[str, Any]],
    pass_plan: dict[str, Any],
    ledger: dict[str, Any],
    errors: list[dict[str, Any]],
) -> None:
    write_json(out_dir / "render_graph_trace.json", trace)
    write_json(out_dir / "render_pass_plan.json", pass_plan)
    write_json(out_dir / "resource_access_ledger.json", ledger)
    write_json(out_dir / "render_graph_errors.json", errors)


def write_json(path: Path, payload: Any) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload, indent=2, ensure_ascii=False) + "\n", encoding="utf8")


if __name__ == "__main__":
    raise SystemExit(main())

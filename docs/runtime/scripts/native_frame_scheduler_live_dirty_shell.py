#!/usr/bin/env python3
"""Run the native FrameScheduler live dirty propagation proof."""

from __future__ import annotations

import json
import sys
from pathlib import Path
from typing import Any


RESULT_NAME = "native_frame_scheduler_live_dirty_result.json"
ERRORS_NAME = "native_frame_scheduler_live_dirty_errors.json"


def main() -> int:
    if len(sys.argv) != 3:
        print("usage: native_frame_scheduler_live_dirty_shell.py <fixture> <out_dir>", file=sys.stderr)
        return 2

    fixture_path = Path(sys.argv[1]).expanduser().resolve()
    out_dir = Path(sys.argv[2]).expanduser().resolve()
    out_dir.mkdir(parents=True, exist_ok=True)
    clear_previous(out_dir)

    errors: list[dict[str, Any]] = []
    fixture = read_json(fixture_path, errors, "native_frame_scheduler.fixture_read_failed")
    if fixture is None:
        publish(out_dir, None, False, "fixture_read_failed", {}, [], {}, {}, errors)
        return 1

    frame_uniform_errors = validate_frame_uniform_ownership(fixture)
    if frame_uniform_errors:
        errors.extend(frame_uniform_errors)
        publish(out_dir, fixture.get("graphId"), False, "frame_uniform_contract_failed", {}, [], {}, {}, errors)
        return 1

    try:
        runtime_graph = build_runtime_graph(fixture)
        command_log = [{"index": index, "source": "fixture", "command": command} for index, command in enumerate(fixture.get("commands", []))]
        dirty_trace, frame_artifacts = run_scheduler(fixture, runtime_graph, command_log)
    except Exception as exc:
        errors.append({"code": "native_frame_scheduler.run_failed", "message": str(exc)})
        publish(out_dir, fixture.get("graphId"), False, "run_failed", {}, [], {}, {}, errors)
        return 1

    expected_errors = validate_expected(fixture, dirty_trace)
    errors.extend(expected_errors)
    ok = not errors
    publish(out_dir, fixture.get("graphId"), ok, "live_dirty_ready" if ok else "live_dirty_mismatch", runtime_graph, command_log, dirty_trace, frame_artifacts, errors)
    return 0 if ok else 1


def validate_frame_uniform_ownership(fixture: dict[str, Any]) -> list[dict[str, Any]]:
    errors: list[dict[str, Any]] = []
    if fixture.get("scheduler", {}).get("clockOwner") != "graph":
        errors.append({"code": "native_frame_scheduler.invalid_clock_owner"})
    for node in fixture.get("nodes", []):
        if node.get("dirtyPolicy") == "frameUniform" and node.get("clockOwner") not in (None, "graph"):
            errors.append({"code": "native_frame_scheduler.node_owned_frame_uniform", "nodeId": node.get("id")})
            break
    return errors


def build_runtime_graph(fixture: dict[str, Any]) -> dict[str, Any]:
    nodes = {node["id"]: node for node in fixture.get("nodes", [])}
    cook_order = fixture.get("expected", {}).get("runtimeCookOrder") or topological_order(nodes, fixture.get("edges", []))
    return {
        "kind": "RuntimeGraph",
        "graphId": fixture.get("graphId"),
        "cookOrder": cook_order,
        "nodes": [
            {
                "id": node_id,
                "type": nodes[node_id]["type"],
                "dirtyPolicy": nodes[node_id].get("dirtyPolicy"),
                "params": nodes[node_id].get("params", {}),
            }
            for node_id in cook_order
        ],
        "edges": fixture.get("edges", []),
        "scheduler": fixture.get("scheduler", {}),
    }


def run_scheduler(
    fixture: dict[str, Any],
    runtime_graph: dict[str, Any],
    command_log: list[dict[str, Any]],
) -> tuple[dict[str, Any], dict[str, Any]]:
    outgoing = build_outgoing(runtime_graph.get("edges", []))
    cook_order = runtime_graph.get("cookOrder", [])
    frame_uniform_nodes = [
        node["id"]
        for node in runtime_graph.get("nodes", [])
        if node.get("dirtyPolicy") == "frameUniform"
    ]
    frames_trace: list[dict[str, Any]] = []
    frame_artifacts: list[dict[str, Any]] = []

    for frame in fixture.get("scheduler", {}).get("frames", []):
        frame_index = frame["frameIndex"]
        dirty_roots: list[str]
        if frame_index == 0:
            dirty_roots = ["startup"]
            cooked = cook_order[:]
        else:
            dirty_roots = frame_uniform_nodes[:]
            for framed_command in frame.get("commands", []):
                command = framed_command["command"]
                apply_command_to_runtime_graph(runtime_graph, command)
                command_log.append({"index": len(command_log), "source": framed_command.get("source", f"runtime.frame.{frame_index}"), "command": command})
                if command.get("id") not in dirty_roots:
                    dirty_roots.append(command["id"])
            cooked = cook_closure(dirty_roots, outgoing, cook_order)

        skipped = [node_id for node_id in cook_order if node_id not in cooked]
        frame_uniforms = {"u_frame": frame_index, "u_time": frame["time"], "u_deltaTime": frame["deltaTime"]}
        frames_trace.append({
            "frameIndex": frame_index,
            "frameUniforms": frame_uniforms,
            "dirtyRoots": dirty_roots,
            "cookedNodes": cooked,
            "skippedCleanNodes": skipped,
        })
        frame_artifacts.append({
            "kind": "RuntimeFrameArtifact",
            "frameIndex": frame_index,
            "frameUniforms": frame_uniforms,
            "publishedNode": cook_order[-1] if cook_order else None,
            "cookedNodes": cooked,
            "skippedCleanNodes": skipped,
        })

    return {"kind": "LiveDirtyTrace", "frames": frames_trace}, {"kind": "FrameArtifacts", "frames": frame_artifacts}


def apply_command_to_runtime_graph(runtime_graph: dict[str, Any], command: dict[str, Any]) -> None:
    if command.get("op") != "setParam":
        raise ValueError(f"unsupported frame command op: {command.get('op')}")
    for node in runtime_graph["nodes"]:
        if node["id"] == command["id"]:
            node.setdefault("params", {})[command["param"]] = command["value"]
            return
    raise ValueError(f"unknown command node: {command.get('id')}")


def cook_closure(roots: list[str], outgoing: dict[str, set[str]], cook_order: list[str]) -> list[str]:
    dirty = set(roots)
    queue = [root for root in roots if root != "startup"]
    while queue:
        node_id = queue.pop(0)
        for dst in sorted(outgoing.get(node_id, set())):
            if dst not in dirty:
                dirty.add(dst)
                queue.append(dst)
    return [node_id for node_id in cook_order if node_id in dirty]


def build_outgoing(edges: list[dict[str, Any]]) -> dict[str, set[str]]:
    outgoing: dict[str, set[str]] = {}
    for edge in edges:
        src = edge["from"][0]
        dst = edge["to"][0]
        outgoing.setdefault(src, set()).add(dst)
    return outgoing


def validate_expected(fixture: dict[str, Any], dirty_trace: dict[str, Any]) -> list[dict[str, Any]]:
    errors: list[dict[str, Any]] = []
    expected = fixture.get("expected", {}).get("frameCookSets", [])
    observed = dirty_trace.get("frames", [])
    for expected_frame, observed_frame in zip(expected, observed):
        if expected_frame.get("dirtyRoots") != observed_frame.get("dirtyRoots"):
            errors.append({"code": "native_frame_scheduler.dirty_roots_mismatch", "frameIndex": expected_frame.get("frameIndex"), "expected": expected_frame.get("dirtyRoots"), "observed": observed_frame.get("dirtyRoots")})
        if expected_frame.get("cookedNodes") != observed_frame.get("cookedNodes"):
            errors.append({"code": "native_frame_scheduler.cooked_nodes_mismatch", "frameIndex": expected_frame.get("frameIndex"), "expected": expected_frame.get("cookedNodes"), "observed": observed_frame.get("cookedNodes")})
    return errors


def publish(
    out_dir: Path,
    graph_id: str | None,
    ok: bool,
    status: str,
    runtime_graph: dict[str, Any],
    command_log: list[dict[str, Any]],
    dirty_trace: dict[str, Any],
    frame_artifacts: dict[str, Any],
    errors: list[dict[str, Any]],
) -> None:
    frames = dirty_trace.get("frames", [])
    static_skipped = any("blob_1" in frame.get("skippedCleanNodes", []) for frame in frames)
    result = {
        "kind": "NativeFrameSchedulerLiveDirtyProof",
        "graphId": graph_id,
        "ok": ok,
        "status": status,
        "claims": {
            "schedulerOwnsFrameUniforms": ok and bool(frames),
            "commandDirtyPropagation": ok and any("gradient_1" in frame.get("dirtyRoots", []) for frame in frames),
            "staticUnchangedNodeSkipped": ok and static_skipped,
            "runtimeFrameLinked": ok and bool(frame_artifacts.get("frames")),
        },
    }
    write_json(out_dir / RESULT_NAME, result)
    write_json(out_dir / "runtime_graph.json", runtime_graph)
    write_json(out_dir / "command_log.json", command_log)
    write_json(out_dir / "live_dirty_trace.json", dirty_trace)
    write_json(out_dir / "frame_artifacts.json", frame_artifacts)
    write_json(out_dir / ERRORS_NAME, errors)


def topological_order(nodes: dict[str, Any], edges: list[dict[str, Any]]) -> list[str]:
    incoming = {node_id: set() for node_id in nodes}
    outgoing = {node_id: set() for node_id in nodes}
    for edge in edges:
        src = edge["from"][0]
        dst = edge["to"][0]
        incoming[dst].add(src)
        outgoing[src].add(dst)
    ready = sorted(node_id for node_id, deps in incoming.items() if not deps)
    order: list[str] = []
    while ready:
        node_id = ready.pop(0)
        order.append(node_id)
        for dst in sorted(outgoing[node_id]):
            incoming[dst].discard(node_id)
            if not incoming[dst] and dst not in order and dst not in ready:
                ready.append(dst)
    if len(order) != len(nodes):
        raise ValueError("cycle detected")
    return order


def clear_previous(out_dir: Path) -> None:
    for name in [
        RESULT_NAME,
        ERRORS_NAME,
        "runtime_graph.json",
        "command_log.json",
        "live_dirty_trace.json",
        "frame_artifacts.json",
    ]:
        target = out_dir / name
        if target.exists():
            target.unlink()


def read_json(path: Path, errors: list[dict[str, Any]], code: str) -> Any | None:
    try:
        return json.loads(path.read_text(encoding="utf8"))
    except Exception as exc:
        errors.append({"code": code, "message": str(exc)})
        return None


def write_json(path: Path, payload: Any) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload, indent=2, ensure_ascii=True) + "\n", encoding="utf8")


if __name__ == "__main__":
    raise SystemExit(main())

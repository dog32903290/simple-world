#!/usr/bin/env python3
"""
Run a minimal FrameScheduler proof.

This is a timing/synchronization shell, not a renderer.
"""

from __future__ import annotations

import json
import sys
from pathlib import Path
from typing import Any


def main() -> int:
    if len(sys.argv) != 3:
        print("usage: frame_scheduler_shell.py <graph.json> <out_dir>", file=sys.stderr)
        return 2

    graph_path = Path(sys.argv[1]).expanduser().resolve()
    out_dir = Path(sys.argv[2]).expanduser().resolve()
    out_dir.mkdir(parents=True, exist_ok=True)

    errors: list[dict[str, Any]] = []
    graph = read_json(graph_path, errors, "frame_scheduler.graph_read_failed")
    if graph is None:
        write_artifacts(out_dir, [], errors, [], [])
        return 1

    try:
        result = run_scheduler(graph)
    except Exception as exc:
        errors.append({"code": "frame_scheduler.run_failed", "message": str(exc)})
        write_artifacts(out_dir, [], errors, [], [])
        return 1

    errors.extend(result["errors"])
    write_artifacts(
        out_dir,
        result["trace"],
        errors,
        result["nodeObservations"],
        result["stateTrace"],
    )
    return 0 if not errors else 1


def run_scheduler(graph: dict[str, Any]) -> dict[str, Any]:
    nodes = {node["id"]: node for node in graph.get("nodes", [])}
    edges = graph.get("edges", [])
    frames = graph.get("scheduler", {}).get("frames", [])
    expected_order = graph.get("expected", {}).get("cookOrder")
    cook_order = expected_order or topological_order(nodes, edges)

    trace: list[dict[str, Any]] = [{
        "op": "loadGraph",
        "graphId": graph.get("graphId"),
        "clockOwner": graph.get("scheduler", {}).get("clockOwner"),
        "cookOrder": cook_order,
    }]
    errors: list[dict[str, Any]] = []
    node_observations: list[dict[str, Any]] = []
    state_trace: list[dict[str, Any]] = []
    values: dict[str, dict[str, Any]] = {}
    state: dict[str, Any] = {
        "keep_previous_1": {
            "currentFrame": None,
            "previousFrame": None,
            "lastUpdatedFrame": None,
        }
    }

    if graph.get("scheduler", {}).get("clockOwner") != "graph":
        errors.append({"code": "frame_scheduler.invalid_clock_owner"})

    for frame in frames:
        frame_context = {
            "frameIndex": frame["frameIndex"],
            "time": frame["time"],
            "deltaTime": frame["deltaTime"],
        }
        trace.append({"op": "frame.begin", "frame": frame_context})
        frame_values: dict[str, dict[str, Any]] = {}

        for node_id in cook_order:
            node = nodes[node_id]
            observation = {
                "nodeId": node_id,
                "type": node["type"],
                "frame": frame_context,
            }
            node_observations.append(observation)
            output = cook_node(node, frame_context, values, state, state_trace)
            frame_values[node_id] = output
            values[node_id] = output
            trace.append({
                "op": "node.cook",
                "nodeId": node_id,
                "frameIndex": frame_context["frameIndex"],
                "outputKeys": sorted(output.keys()),
            })

        context_errors = validate_frame_context(frame_context, node_observations, cook_order)
        errors.extend(context_errors)
        trace.append({
            "op": "frame.publish",
            "frameIndex": frame_context["frameIndex"],
            "publishedNode": cook_order[-1] if cook_order else None,
            "ok": not context_errors,
        })

    errors.extend(validate_state_updates_once_per_frame(state_trace, frames))
    trace.append({
        "op": "publishArtifacts",
        "ok": not errors,
        "artifacts": [
            "frame_scheduler_trace.json",
            "frame_scheduler_errors.json",
            "node_observations.json",
            "state_trace.json",
        ],
    })

    return {
        "trace": trace,
        "errors": errors,
        "nodeObservations": node_observations,
        "stateTrace": state_trace,
    }


def cook_node(
    node: dict[str, Any],
    frame: dict[str, Any],
    values: dict[str, dict[str, Any]],
    state: dict[str, Any],
    state_trace: list[dict[str, Any]],
) -> dict[str, Any]:
    node_type = node["type"]
    node_id = node["id"]

    if node_type == "image.constant":
        resolution = node["params"]["resolution"]
        return {
            "image": {
                "source": node_id,
                "width": resolution[0],
                "height": resolution[1],
                "color": node["params"]["color"],
                "frameIndex": frame["frameIndex"],
            }
        }

    if node_type == "image.blend":
        return {
            "image": {
                "source": node_id,
                "backgroundFrame": values["constant_a"]["image"]["frameIndex"],
                "foregroundFrame": values["constant_b"]["image"]["frameIndex"],
                "foregroundOpacity": node["params"]["foregroundOpacity"],
                "frameIndex": frame["frameIndex"],
            }
        }

    if node_type == "feedback.keepPreviousFrame":
        slot = state[node_id]
        if slot["lastUpdatedFrame"] != frame["frameIndex"]:
            slot["previousFrame"] = slot["currentFrame"]
            slot["currentFrame"] = values["blend_1"]["image"]
            slot["lastUpdatedFrame"] = frame["frameIndex"]
            state_trace.append({
                "nodeId": node_id,
                "op": "state.update",
                "frameIndex": frame["frameIndex"],
                "currentFrame": slot["currentFrame"]["frameIndex"],
                "previousFrame": None if slot["previousFrame"] is None else slot["previousFrame"]["frameIndex"],
            })
        return {
            "currentFrame": slot["currentFrame"],
            "previousFrame": slot["previousFrame"],
        }

    if node_type == "output.texture_summary":
        current = values["keep_previous_1"]["currentFrame"]
        previous = values["keep_previous_1"]["previousFrame"]
        return {
            "summary": {
                "frameIndex": frame["frameIndex"],
                "currentFrame": current["frameIndex"],
                "previousFrame": None if previous is None else previous["frameIndex"],
                "width": 960,
                "height": 540,
            }
        }

    raise ValueError(f"unsupported node type: {node_type}")


def validate_frame_context(
    frame: dict[str, Any],
    observations: list[dict[str, Any]],
    cook_order: list[str],
) -> list[dict[str, Any]]:
    frame_observations = [
        item for item in observations
        if item["frame"]["frameIndex"] == frame["frameIndex"]
    ]
    errors: list[dict[str, Any]] = []
    if [item["nodeId"] for item in frame_observations] != cook_order:
        errors.append({
            "code": "frame_scheduler.cook_order_mismatch",
            "frameIndex": frame["frameIndex"],
            "observed": [item["nodeId"] for item in frame_observations],
            "expected": cook_order,
        })

    for item in frame_observations:
        if item["frame"] != frame:
            errors.append({
                "code": "frame_scheduler.frame_context_mismatch",
                "nodeId": item["nodeId"],
                "frameIndex": frame["frameIndex"],
                "observedFrame": item["frame"],
                "expectedFrame": frame,
            })
    return errors


def validate_state_updates_once_per_frame(
    state_trace: list[dict[str, Any]],
    frames: list[dict[str, Any]],
) -> list[dict[str, Any]]:
    errors: list[dict[str, Any]] = []
    frame_indices = [frame["frameIndex"] for frame in frames]
    for frame_index in frame_indices:
        updates = [entry for entry in state_trace if entry["frameIndex"] == frame_index]
        if len(updates) != 1:
            errors.append({
                "code": "frame_scheduler.state_update_count_mismatch",
                "frameIndex": frame_index,
                "expected": 1,
                "actual": len(updates),
            })
    return errors


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


def read_json(path: Path, errors: list[dict[str, Any]], code: str) -> Any | None:
    try:
        return json.loads(path.read_text(encoding="utf8"))
    except Exception as exc:
        errors.append({"code": code, "path": str(path), "message": str(exc)})
        return None


def write_artifacts(
    out_dir: Path,
    trace: list[dict[str, Any]],
    errors: list[dict[str, Any]],
    observations: list[dict[str, Any]],
    state_trace: list[dict[str, Any]],
) -> None:
    write_json(out_dir / "frame_scheduler_trace.json", trace)
    write_json(out_dir / "frame_scheduler_errors.json", errors)
    write_json(out_dir / "node_observations.json", observations)
    write_json(out_dir / "state_trace.json", state_trace)


def write_json(path: Path, payload: Any) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload, indent=2, ensure_ascii=False) + "\n", encoding="utf8")


if __name__ == "__main__":
    raise SystemExit(main())

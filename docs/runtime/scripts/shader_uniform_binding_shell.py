#!/usr/bin/env python3
"""
Validate live control -> shader uniform evidence -> RenderFrameInput.

This shell mirrors the useful my-world ShaderPreviewInputBridge semantics in a
text-replayable simple_world contract.
"""

from __future__ import annotations

import json
import math
import sys
from pathlib import Path
from typing import Any

UNIFORM_FLOAT = "my.shader.uniformFloat"
CONSTANT_FLOAT = "my.value.constantFloat"
REMAP_FLOAT = "tixl.numbers.float.adjust.Remap"


def main() -> int:
    if len(sys.argv) != 3:
        print("usage: shader_uniform_binding_shell.py <shader_uniform_binding.graph.json> <out_dir>", file=sys.stderr)
        return 2

    graph_path = Path(sys.argv[1]).expanduser().resolve()
    out_dir = Path(sys.argv[2]).expanduser().resolve()
    out_dir.mkdir(parents=True, exist_ok=True)

    errors: list[dict[str, Any]] = []
    graph = read_json(graph_path, errors, "shader_uniform_binding.graph_read_failed")
    if graph is None:
        write_artifacts(out_dir, {}, {}, {}, [], errors)
        return 1

    snapshot, bindings, frame_input, trace, run_errors = run_uniform_binding(graph)
    errors.extend(run_errors)
    write_artifacts(out_dir, snapshot, bindings, frame_input, trace, errors)
    return 0 if not errors else 1


def run_uniform_binding(graph: dict[str, Any]) -> tuple[dict[str, Any], dict[str, Any], dict[str, Any], list[dict[str, Any]], list[dict[str, Any]]]:
    errors: list[dict[str, Any]] = []
    trace: list[dict[str, Any]] = [{
        "op": "loadShaderUniformBindingGraph",
        "graphId": graph.get("graphId"),
    }]
    nodes = {node.get("id"): node for node in graph.get("nodes", [])}
    edges = graph.get("edges", [])
    target_program = graph.get("targetProgram", {})
    uniform_nodes = [node for node in nodes.values() if node.get("type") == UNIFORM_FLOAT]

    if not uniform_nodes:
        errors.append({"code": "shader_uniform_binding.missing_uniform_node"})

    uniforms = []
    for uniform_node in uniform_nodes:
        uniform = build_uniform_evidence(uniform_node, nodes, edges, target_program, errors, trace)
        if uniform is not None:
            uniforms.append(uniform)

    snapshot_ok = not errors
    snapshot = {
        "kind": "shaderUniformBindingSnapshot",
        "ok": snapshot_ok,
        "status": "ready" if snapshot_ok else "failed",
        "message": "shader_uniform_binding_ready" if snapshot_ok else "shader_uniform_binding_failed",
        "sampleCounter": max([uniform.get("sampleCounter", 0) for uniform in uniforms], default=0),
        "uniforms": uniforms if snapshot_ok else [],
        "errors": errors,
    }
    bindings = {
        "kind": "shaderUniformBindings",
        "targetProgramId": target_program.get("programId"),
        "uniforms": uniforms if snapshot_ok else [],
    }
    frame_input = build_render_frame_input(graph.get("frame", {}), snapshot)
    trace.append({
        "op": "publishShaderUniformBindingArtifacts",
        "ok": snapshot_ok,
        "uniformCount": len(snapshot.get("uniforms", [])),
    })
    return snapshot, bindings, frame_input, trace, errors


def build_uniform_evidence(
    uniform_node: dict[str, Any],
    nodes: dict[str, dict[str, Any]],
    edges: list[dict[str, Any]],
    target_program: dict[str, Any],
    errors: list[dict[str, Any]],
    trace: list[dict[str, Any]],
) -> dict[str, Any] | None:
    params = uniform_node.get("params", {})
    binding_id = params.get("bindingId")
    uniform_name = params.get("uniformName") or params.get("name")

    if not binding_id:
        errors.append({
            "code": "shader_uniform_binding.missing_binding_id",
            "nodeId": uniform_node.get("id"),
        })
    if not uniform_name:
        errors.append({
            "code": "shader_uniform_binding.missing_uniform_name",
            "nodeId": uniform_node.get("id"),
        })

    source_edge = find_input_edge(edges, uniform_node.get("id"), "value")
    if source_edge is None:
        errors.append({
            "code": "shader_uniform_binding.missing_source_value",
            "nodeId": uniform_node.get("id"),
            "port": "value",
        })
        return None

    source_node = nodes.get(source_edge.get("from", {}).get("nodeId"))
    if source_node is None:
        errors.append({
            "code": "shader_uniform_binding.missing_source_node",
            "nodeId": source_edge.get("from", {}).get("nodeId"),
        })
        return None

    value = evaluate_float_node(source_node, nodes, edges, errors, trace)
    if value is None or not math.isfinite(value):
        errors.append({
            "code": "shader_uniform_binding.invalid_numeric_value",
            "nodeId": source_node.get("id"),
        })
        return None

    sample_counter = int(source_node.get("params", {}).get("sampleCounter", params.get("sampleCounter", 0)))
    uniform = {
        "bindingId": binding_id,
        "uniformName": uniform_name,
        "value": value,
        "sampleCounter": sample_counter,
        "sourceNodeId": source_node.get("id"),
        "sourcePort": source_edge.get("from", {}).get("port"),
        "targetProgramId": target_program.get("programId"),
    }
    trace.append({
        "op": "bindUniformFloat",
        "nodeId": uniform_node.get("id"),
        "bindingId": binding_id,
        "uniformName": uniform_name,
        "value": value,
    })
    return uniform


def evaluate_float_node(
    node: dict[str, Any],
    nodes: dict[str, dict[str, Any]],
    edges: list[dict[str, Any]],
    errors: list[dict[str, Any]],
    trace: list[dict[str, Any]],
) -> float | None:
    node_type = node.get("type")
    params = node.get("params", {})
    if node_type == CONSTANT_FLOAT:
        trace.append({"op": "constantFloat", "nodeId": node.get("id"), "value": params.get("value")})
        return float(params.get("value"))

    if node_type == REMAP_FLOAT:
        input_edge = find_input_edge(edges, node.get("id"), "value")
        if input_edge is None:
            errors.append({"code": "shader_uniform_binding.remap_missing_input", "nodeId": node.get("id")})
            return None
        source_node = nodes.get(input_edge.get("from", {}).get("nodeId"))
        if source_node is None:
            errors.append({"code": "shader_uniform_binding.remap_missing_source_node", "nodeId": node.get("id")})
            return None
        value = evaluate_float_node(source_node, nodes, edges, errors, trace)
        if value is None:
            return None
        in_min = float(params.get("rangeInMin", 0.0))
        in_max = float(params.get("rangeInMax", 1.0))
        out_min = float(params.get("rangeOutMin", 0.0))
        out_max = float(params.get("rangeOutMax", 1.0))
        if in_max == in_min:
            errors.append({"code": "shader_uniform_binding.remap_zero_input_range", "nodeId": node.get("id")})
            return None
        normalized = (value - in_min) / (in_max - in_min)
        result = out_min + normalized * (out_max - out_min)
        trace.append({"op": "remapFloat", "nodeId": node.get("id"), "input": value, "value": result})
        return result

    errors.append({
        "code": "shader_uniform_binding.unsupported_value_node",
        "nodeId": node.get("id"),
        "type": node_type,
    })
    return None


def build_render_frame_input(frame: dict[str, Any], snapshot: dict[str, Any]) -> dict[str, Any]:
    loudness = float(frame.get("fallbackLoudness", 0.0))
    if snapshot.get("ok"):
        for uniform in snapshot.get("uniforms", []):
            if uniform.get("uniformName") == "u_loudness":
                loudness = float(uniform.get("value", loudness))
                break
    return {
        "kind": "RenderFrameInput",
        "timeSeconds": frame.get("timeSeconds", 0.0),
        "frameIndex": frame.get("frameIndex", 0),
        "loudness": loudness,
        "resolution": frame.get("resolution"),
        "viewportScale": frame.get("viewportScale", 1),
        "sourceSnapshotOk": bool(snapshot.get("ok")),
    }


def find_input_edge(edges: list[dict[str, Any]], node_id: str | None, port: str) -> dict[str, Any] | None:
    for edge in edges:
        target = edge.get("to", {})
        if target.get("nodeId") == node_id and target.get("port") == port:
            return edge
    return None


def read_json(path: Path, errors: list[dict[str, Any]], code: str) -> Any | None:
    try:
        return json.loads(path.read_text(encoding="utf8"))
    except Exception as exc:
        errors.append({"code": code, "path": str(path), "message": str(exc)})
        return None


def write_artifacts(
    out_dir: Path,
    snapshot: dict[str, Any],
    bindings: dict[str, Any],
    frame_input: dict[str, Any],
    trace: list[dict[str, Any]],
    errors: list[dict[str, Any]],
) -> None:
    write_json(out_dir / "shader_uniform_snapshot.json", snapshot)
    write_json(out_dir / "shader_uniform_bindings.json", bindings)
    write_json(out_dir / "render_frame_input.json", frame_input)
    write_json(out_dir / "uniform_binding_trace.json", trace)
    write_json(out_dir / "shader_uniform_binding_errors.json", errors)


def write_json(path: Path, payload: Any) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload, indent=2, ensure_ascii=False) + "\n", encoding="utf8")


if __name__ == "__main__":
    raise SystemExit(main())

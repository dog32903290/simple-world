#!/usr/bin/env python3
"""
Compile the E1 ShaderGraphNode shell fixture into inspectable artifacts.

This is deliberately not a renderer. It proves that dangerous TiXL field nodes
can cross the My World runtime contract as structured graph data, produce shader
source text, and report failures without touching DX11/HLSL runtime state.
"""

from __future__ import annotations

import json
import sys
from pathlib import Path
from typing import Any


SPHERE_SDF = "tixl.field.generate.sdf.SphereSDF"
RAYMARCH_FIELD = "tixl.field.render.RaymarchField"


def main() -> int:
    if len(sys.argv) != 3:
        print("usage: compile_shadergraph_shell.py <graph.json> <out_dir>", file=sys.stderr)
        return 2

    graph_path = Path(sys.argv[1])
    out_dir = Path(sys.argv[2])
    out_dir.mkdir(parents=True, exist_ok=True)

    errors: list[dict[str, Any]] = []
    cook_order: list[dict[str, Any]] = []

    try:
        graph = json.loads(graph_path.read_text(encoding="utf8"))
    except Exception as exc:
        write_json(out_dir / "errors.json", [{"code": "graph.read_failed", "message": str(exc)}])
        write_json(out_dir / "cook_order.json", [])
        (out_dir / "shader_source.glsl").write_text("", encoding="utf8")
        return 1

    nodes = {node["id"]: node for node in graph.get("nodes", []) if "id" in node}
    edges = graph.get("edges", [])
    raymarch_nodes = [node for node in nodes.values() if node.get("type") == RAYMARCH_FIELD]

    if len(raymarch_nodes) != 1:
        errors.append({
            "code": "shadergraph.raymarch_count",
            "message": "E1 shell expects exactly one RaymarchField node.",
            "count": len(raymarch_nodes),
        })
        return write_artifacts(out_dir, "", errors, cook_order)

    raymarch = raymarch_nodes[0]
    sdf_edge = find_input_edge(edges, raymarch["id"], "sdfField")
    if sdf_edge is None:
        errors.append({
            "code": "shadergraph.missing_sdf_field",
            "nodeId": raymarch["id"],
            "port": "sdfField",
        })
        return write_artifacts(out_dir, "", errors, cook_order)

    sdf_node = nodes.get(sdf_edge["from"]["nodeId"])
    if sdf_node is None:
        errors.append({
            "code": "shadergraph.missing_source_node",
            "edgeId": sdf_edge.get("id"),
            "nodeId": sdf_edge["from"]["nodeId"],
        })
        return write_artifacts(out_dir, "", errors, cook_order)

    if sdf_node.get("type") != SPHERE_SDF:
        errors.append({
            "code": "shadergraph.unsupported_sdf_node",
            "nodeId": sdf_node.get("id"),
            "type": sdf_node.get("type"),
            "supported": [SPHERE_SDF],
        })
        return write_artifacts(out_dir, "", errors, cook_order)

    cook_order.append({"node_id": sdf_node["id"], "type": sdf_node["type"], "artifact": "field_expr"})
    cook_order.append({"node_id": raymarch["id"], "type": raymarch["type"], "artifact": "shader_source.glsl"})

    shader = assemble_shader(sdf_node, raymarch)
    return write_artifacts(out_dir, shader, errors, cook_order)


def find_input_edge(edges: list[dict[str, Any]], node_id: str, port: str) -> dict[str, Any] | None:
    for edge in edges:
        target = edge.get("to", {})
        if target.get("nodeId") == node_id and target.get("port") == port:
            return edge
    return None


def assemble_shader(sdf_node: dict[str, Any], raymarch_node: dict[str, Any]) -> str:
    params = sdf_node.get("params", {})
    center = params.get("center", {"x": 0, "y": 0, "z": 0})
    radius = params.get("radius", 0.5)
    center_expr = f"vec3({fmt_float(center.get('x', 0))}, {fmt_float(center.get('y', 0))}, {fmt_float(center.get('z', 0))})"

    ray_params = raymarch_node.get("params", {})
    max_steps = int(ray_params.get("maxSteps", 100))
    min_distance = fmt_float(ray_params.get("minDistance", 0.002))
    max_distance = fmt_float(ray_params.get("maxDistance", 300))

    return f"""// My World E1 ShaderGraphNode shell
// Source: TiXL SphereSDF -> RaymarchField
// This GLSL-like artifact is for contract inspection, not final renderer output.

struct MyWorldField {{
    vec3 xyz;
    float w;
}};

float sdSphere(vec3 p, vec3 center, float radius) {{
    return length(p - center) - radius;
}}

MyWorldField myworld_field(vec3 p) {{
    MyWorldField f;
    f.w = sdSphere(p, {center_expr}, {fmt_float(radius)});
    f.xyz = p;
    return f;
}}

float raymarch_field_1(vec3 rayOrigin, vec3 rayDirection) {{
    float totalDistance = 0.0;
    for (int i = 0; i < {max_steps}; ++i) {{
        vec3 p = rayOrigin + rayDirection * totalDistance;
        float distanceToField = myworld_field(p).w;
        if (distanceToField < {min_distance}) {{
            return totalDistance;
        }}
        totalDistance += distanceToField;
        if (totalDistance > {max_distance}) {{
            break;
        }}
    }}
    return -1.0;
}}
"""


def fmt(value: Any) -> str:
    number = float(value)
    if number.is_integer():
        return str(int(number))
    return f"{number:.8g}"


def fmt_float(value: Any) -> str:
    number = float(value)
    text = f"{number:.8g}"
    if "e" in text.lower() or "." in text:
        return text
    return f"{text}.0"


def write_artifacts(
    out_dir: Path,
    shader: str,
    errors: list[dict[str, Any]],
    cook_order: list[dict[str, Any]],
) -> int:
    (out_dir / "shader_source.glsl").write_text(shader, encoding="utf8")
    write_json(out_dir / "errors.json", errors)
    write_json(out_dir / "cook_order.json", cook_order)
    return 0 if not errors else 1


def write_json(path: Path, payload: Any) -> None:
    path.write_text(json.dumps(payload, indent=2, ensure_ascii=False) + "\n", encoding="utf8")


if __name__ == "__main__":
    raise SystemExit(main())

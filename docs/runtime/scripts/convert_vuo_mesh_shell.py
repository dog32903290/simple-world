#!/usr/bin/env python3
"""
Convert a VuoMesh CPU snapshot into the first My World Mesh contract artifact.

This is an adapter shell, not a Vuo runtime node. It fixes the data contract
that a future Vuo custom node must satisfy when exporting mesh data.
"""

from __future__ import annotations

import json
import sys
from pathlib import Path
from typing import Any


TOPOLOGY_MAP = {
    "VuoMesh_IndividualTriangles": "triangles",
    "VuoMesh_IndividualLines": "lines",
    "VuoMesh_Points": "points",
}


def main() -> int:
    if len(sys.argv) != 3:
        print("usage: convert_vuo_mesh_shell.py <vuo_mesh.snapshot.json> <out_dir>", file=sys.stderr)
        return 2

    input_path = Path(sys.argv[1])
    out_dir = Path(sys.argv[2])
    out_dir.mkdir(parents=True, exist_ok=True)

    try:
        snapshot = json.loads(input_path.read_text(encoding="utf8"))
    except Exception as exc:
        return write_artifacts(out_dir, None, None, [{"code": "mesh.read_failed", "message": str(exc)}])

    errors = validate_snapshot(snapshot)
    if errors:
        return write_artifacts(out_dir, None, None, errors)

    mesh = build_mesh_contract(snapshot)
    stats = build_mesh_stats(mesh)
    return write_artifacts(out_dir, mesh, stats, [])


def validate_snapshot(snapshot: dict[str, Any]) -> list[dict[str, Any]]:
    errors: list[dict[str, Any]] = []
    method = snapshot.get("elementAssemblyMethod")
    if method not in TOPOLOGY_MAP:
        errors.append({
            "code": "mesh.unsupported_topology",
            "elementAssemblyMethod": method,
            "supported": sorted(TOPOLOGY_MAP),
        })
        return errors

    vertex_count = snapshot.get("vertexCount")
    positions = snapshot.get("positions")
    elements = snapshot.get("elements")
    if not isinstance(vertex_count, int) or vertex_count < 0:
        errors.append({"code": "mesh.invalid_vertex_count", "value": vertex_count})
    if not isinstance(positions, list) or len(positions) != vertex_count:
        errors.append({"code": "mesh.positions_count_mismatch", "vertexCount": vertex_count})
    if not isinstance(elements, list):
        errors.append({"code": "mesh.missing_elements"})

    for key, size in [("positions", 3), ("normals", 3), ("textureCoordinates", 2), ("colors", 4)]:
        values = snapshot.get(key)
        if values is None:
            continue
        if not isinstance(values, list) or len(values) != vertex_count:
            errors.append({"code": f"mesh.{key}_count_mismatch", "vertexCount": vertex_count})
            continue
        for index, value in enumerate(values):
            if not isinstance(value, list) or len(value) != size:
                errors.append({"code": f"mesh.{key}_arity_mismatch", "index": index, "expected": size})

    if isinstance(elements, list):
        for index in elements:
            if not isinstance(index, int) or index < 0 or index >= vertex_count:
                errors.append({"code": "mesh.index_out_of_range", "index": index, "vertexCount": vertex_count})

    if method == "VuoMesh_IndividualTriangles" and isinstance(elements, list) and len(elements) % 3 != 0:
        errors.append({"code": "mesh.triangle_element_count_not_multiple_of_three", "elementCount": len(elements)})
    if method == "VuoMesh_IndividualLines" and isinstance(elements, list) and len(elements) % 2 != 0:
        errors.append({"code": "mesh.line_element_count_not_multiple_of_two", "elementCount": len(elements)})

    return errors


def build_mesh_contract(snapshot: dict[str, Any]) -> dict[str, Any]:
    attributes = {
        "position": {
            "semantic": "position",
            "type": "Vec3",
            "data": snapshot["positions"],
        }
    }
    if snapshot.get("normals") is not None:
        attributes["normal"] = {"semantic": "normal", "type": "Vec3", "data": snapshot["normals"]}
    if snapshot.get("textureCoordinates") is not None:
        attributes["uv"] = {"semantic": "uv", "type": "Vec2", "data": snapshot["textureCoordinates"]}
    if snapshot.get("colors") is not None:
        attributes["color"] = {"semantic": "color", "type": "Color", "data": snapshot["colors"]}

    return {
        "version": "0.1",
        "type": "Mesh",
        "topology": TOPOLOGY_MAP[snapshot["elementAssemblyMethod"]],
        "source": snapshot.get("source", {"host": "vuo", "type": "VuoMesh"}),
        "ownership": {
            "cpu": "copied",
            "gpu": "notShared",
            "sourcePolicy": snapshot.get("ownership", {}).get("adapterPolicy", "copy"),
        },
        "coordinateSystem": {
            "handedness": "unknown",
            "upAxis": "unknown",
            "source": "vuo",
        },
        "faceCulling": snapshot.get("faceCulling", "VuoMesh_CullBackfaces"),
        "primitiveSize": snapshot.get("primitiveSize", 1.0),
        "vertexCount": snapshot["vertexCount"],
        "indices": snapshot["elements"],
        "attributes": attributes,
    }


def build_mesh_stats(mesh: dict[str, Any]) -> dict[str, Any]:
    positions = mesh["attributes"]["position"]["data"]
    mins = [min(vertex[i] for vertex in positions) for i in range(3)]
    maxs = [max(vertex[i] for vertex in positions) for i in range(3)]
    return {
        "vertexCount": mesh["vertexCount"],
        "indexCount": len(mesh["indices"]),
        "topology": mesh["topology"],
        "attributes": list(mesh["attributes"].keys()),
        "bounds": {
            "min": mins,
            "max": maxs,
        },
    }


def write_artifacts(
    out_dir: Path,
    mesh: dict[str, Any] | None,
    stats: dict[str, Any] | None,
    errors: list[dict[str, Any]],
) -> int:
    write_json(out_dir / "mesh_contract.json", mesh or {})
    write_json(out_dir / "mesh_stats.json", stats or {})
    write_json(out_dir / "errors.json", errors)
    return 0 if not errors else 1


def write_json(path: Path, payload: Any) -> None:
    path.write_text(json.dumps(payload, indent=2, ensure_ascii=False) + "\n", encoding="utf8")


if __name__ == "__main__":
    raise SystemExit(main())

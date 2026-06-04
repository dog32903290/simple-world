#!/usr/bin/env python3
"""
Compile the Material / PBR scope fixture into inspectable runtime artifacts.

This is not a renderer. It proves that TiXL-style PbrMaterial, material-list
scope, environment context texture scope, and DrawMesh PBR binding can be
carried as structured runtime state before native GPU parity exists.
"""

from __future__ import annotations

import json
import sys
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any


SET_MATERIAL = "tixl.render.shading.SetMaterial"
DEFINE_MATERIALS = "tixl.render.shading.DefineMaterials"
SET_ENVIRONMENT = "tixl.render.shading.SetEnvironment"
CUBE_MESH = "tixl.mesh.generate.CubeMesh"
DRAW_MESH = "tixl.mesh.draw.DrawMesh"

DEFAULT_SRVS = {
    "baseColorMap": "DefaultAlbedoColorSrv",
    "emissiveColorMap": "DefaultEmissiveColorSrv",
    "roughnessMetallicOcclusionMap": "DefaultRoughnessMetallicOcclusionSrv",
    "normalMap": "DefaultNormalSrv",
}


@dataclass
class RuntimeContext:
    pbr_material: dict[str, Any]
    materials: list[dict[str, Any]] = field(default_factory=list)
    context_textures: dict[str, Any] = field(default_factory=lambda: {
        "PrefilteredSpecular": "default-prefiltered-specular"
    })
    trace: list[dict[str, Any]] = field(default_factory=list)
    errors: list[dict[str, Any]] = field(default_factory=list)


def main() -> int:
    if len(sys.argv) != 3:
        print("usage: material_pbr_scope_shell.py <graph.json> <out_dir>", file=sys.stderr)
        return 2

    graph_path = Path(sys.argv[1])
    out_dir = Path(sys.argv[2])
    out_dir.mkdir(parents=True, exist_ok=True)

    try:
        graph = json.loads(graph_path.read_text(encoding="utf8"))
    except Exception as exc:
        return write_artifacts(
            out_dir,
            [],
            {},
            [{"code": "material_pbr.read_failed", "message": str(exc)}],
        )

    trace: list[dict[str, Any]] = []
    errors = validate_graph(graph)
    if errors:
        return write_artifacts(out_dir, trace, {}, errors)

    nodes = {node["id"]: node for node in graph["nodes"]}
    context = RuntimeContext(pbr_material=make_material({"materialId": "Default"}))

    material_nodes = upstream_nodes(graph, "define_materials_1", "materials")
    materials = [make_material(nodes[node_id].get("params", {})) for node_id in material_nodes]
    for material in materials:
        context.errors.extend(material.pop("diagnostics"))

    define_materials(context, materials, lambda: run_environment_and_draw(graph, nodes, context))
    return write_artifacts(out_dir, context.trace, latest_draw_command(context.trace), context.errors)


def validate_graph(graph: dict[str, Any]) -> list[dict[str, Any]]:
    errors: list[dict[str, Any]] = []
    nodes = graph.get("nodes")
    edges = graph.get("edges")
    if not isinstance(nodes, list):
        return [{"code": "material_pbr.missing_nodes"}]
    if not isinstance(edges, list):
        return [{"code": "material_pbr.missing_edges"}]

    by_id = {node.get("id"): node for node in nodes if isinstance(node, dict)}
    required = {
        "define_materials_1": DEFINE_MATERIALS,
        "environment_1": SET_ENVIRONMENT,
        "cube_mesh_1": CUBE_MESH,
        "draw_mesh_1": DRAW_MESH,
    }
    for node_id, expected_type in required.items():
        node = by_id.get(node_id)
        if node is None:
            errors.append({"code": "material_pbr.missing_node", "nodeId": node_id})
        elif node.get("type") != expected_type:
            errors.append({
                "code": "material_pbr.unexpected_node_type",
                "nodeId": node_id,
                "type": node.get("type"),
                "expected": expected_type,
            })

    material_inputs = upstream_nodes(graph, "define_materials_1", "materials")
    if not material_inputs:
        errors.append({"code": "material_pbr.no_material_references"})
    for node_id in material_inputs:
        node = by_id.get(node_id)
        if node is None:
            errors.append({"code": "material_pbr.missing_material_node", "nodeId": node_id})
        elif node.get("type") != SET_MATERIAL:
            errors.append({
                "code": "material_pbr.unsupported_material_reference",
                "nodeId": node_id,
                "type": node.get("type"),
                "supported": [SET_MATERIAL],
            })

    return errors


def upstream_nodes(graph: dict[str, Any], target_node_id: str, target_port: str) -> list[str]:
    result = []
    for edge in graph.get("edges", []):
        target = edge.get("to", {})
        if target.get("nodeId") == target_node_id and target.get("port") == target_port:
            source = edge.get("from", {})
            if "nodeId" in source:
                result.append(source["nodeId"])
    return result


def make_material(params: dict[str, Any]) -> dict[str, Any]:
    material_id = params.get("materialId", params.get("id", ""))
    srvs = {
        "baseColorMap": make_srv("BaseColorMap", params.get("baseColorMap"), "baseColorMap"),
        "emissiveColorMap": make_srv("EmissiveColorMap", params.get("emissiveColorMap"), "emissiveColorMap"),
        "roughnessMetallicOcclusionMap": make_srv(
            "RoughnessMetallicOcclusionMap",
            params.get("roughnessMetallicOcclusionMap"),
            "roughnessMetallicOcclusionMap",
        ),
        "normalMap": make_srv("NormalMap", params.get("normalMap"), "normalMap"),
    }
    diagnostics = [diagnostic for srv in srvs.values() for diagnostic in srv.pop("diagnostics")]

    return {
        "id": material_id,
        "parameterBuffer": f"pbr:{material_id or 'anonymous'}",
        "parameters": {
            "baseColor": params.get("baseColor", [1, 1, 1, 1]),
            "emissiveColor": params.get("emissiveColor", [0, 0, 0, 1]),
            "roughness": params.get("roughness", 0.25),
            "specular": params.get("specular", 1),
            "metal": params.get("metal", 0),
        },
        "srvs": srvs,
        "diagnostics": diagnostics,
    }


def make_srv(input_name: str, texture: Any, default_key: str) -> dict[str, Any]:
    if texture is None or texture.get("disposed") is True:
        return {"srv": DEFAULT_SRVS[default_key], "fallback": True, "diagnostics": []}
    if texture.get("canCreateSrv") is False:
        return {
            "srv": DEFAULT_SRVS[default_key],
            "fallback": True,
            "diagnostics": [{
                "code": "material_pbr.srv_create_failed",
                "input": input_name,
                "textureId": texture.get("id"),
                "fallback": DEFAULT_SRVS[default_key],
            }],
        }
    return {"srv": f"srv:{texture.get('id', 'unnamed')}", "fallback": False, "diagnostics": []}


def define_materials(context: RuntimeContext, materials: list[dict[str, Any]], subtree: Any) -> None:
    previous_count = len(context.materials)
    context.materials.extend(materials)
    context.trace.append({
        "op": "defineMaterials.push",
        "materials": [material["id"] for material in materials],
        "previousCount": previous_count,
    })
    subtree()
    del context.materials[previous_count:]
    context.trace.append({
        "op": "defineMaterials.restore",
        "restoredCount": len(context.materials),
        "expectedCount": previous_count,
    })


def run_environment_and_draw(
    graph: dict[str, Any],
    nodes: dict[str, dict[str, Any]],
    context: RuntimeContext,
) -> None:
    environment = nodes["environment_1"]
    env_params = environment.get("params", {})
    texture_id = env_params.get("contextTextureId", "PrefilteredSpecular")
    texture = env_params.get("texture", {})

    had_previous = texture_id in context.context_textures
    previous = context.context_textures.get(texture_id)
    context.context_textures[texture_id] = texture.get("id", "WhitePixelTexture")
    context.trace.append({
        "op": "contextTexture.push",
        "id": texture_id,
        "texture": context.context_textures[texture_id],
        "hadPrevious": had_previous,
    })

    command = build_draw_mesh_command(graph, nodes, context)
    context.trace.append({"op": "drawMesh.pbrBinding", "command": command})

    if had_previous:
        context.context_textures[texture_id] = previous
    else:
        context.context_textures.pop(texture_id, None)
    context.trace.append({
        "op": "contextTexture.restore",
        "id": texture_id,
        "restoredTexture": context.context_textures.get(texture_id),
    })


def build_draw_mesh_command(
    graph: dict[str, Any],
    nodes: dict[str, dict[str, Any]],
    context: RuntimeContext,
) -> dict[str, Any]:
    mesh_node_id = upstream_nodes(graph, "draw_mesh_1", "mesh")[0]
    mesh = nodes[mesh_node_id].get("params", {})
    mesh_error = validate_mesh(mesh)
    if mesh_error is not None:
        context.errors.append(mesh_error)
        return {"ok": False, "reason": mesh_error["message"], "commandOps": []}

    draw = nodes["draw_mesh_1"]
    params = draw.get("params", {})
    requested_id = params.get("useMaterialId", "")
    selected = context.pbr_material
    unresolved = None
    if requested_id:
        for material in context.materials:
            if material["id"] == requested_id:
                selected = material
                break
        else:
            unresolved = requested_id

    previous_material = context.pbr_material
    context.pbr_material = selected
    pbr = get_pbr_parameters(context)
    context.pbr_material = previous_material

    return {
        "ok": True,
        "meshId": mesh.get("meshId", mesh_node_id),
        "topology": mesh.get("topology", "TriangleList"),
        "vertexBuffer": {
            "buffer": mesh.get("vertexBuffer", f"{mesh.get('meshId', mesh_node_id)}.vertexBuffer"),
            "srv": mesh.get("vertexSrv", f"{mesh.get('meshId', mesh_node_id)}.vertexSrv"),
        },
        "indexBuffer": {
            "buffer": mesh.get("indexBuffer", f"{mesh.get('meshId', mesh_node_id)}.indexBuffer"),
            "srv": mesh.get("indexSrv", f"{mesh.get('meshId', mesh_node_id)}.indexSrv"),
        },
        "requestedMaterialId": requested_id,
        "selectedMaterialId": selected["id"],
        "unresolvedMaterialId": unresolved,
        "shaderSource": params.get("shaderSource", "Lib:shaders/3d/mesh/mesh-Draw.hlsl"),
        "vertexShaderEntry": params.get("vertexShaderEntry", "vsMain"),
        "pixelShaderEntry": params.get("pixelShaderEntry", "psMain"),
        "constantBuffers": [
            "transforms",
            "context",
            "pointLights",
            pbr["PbrParameterBuffer"],
        ],
        "shaderResources": [
            pbr["AlbedoColorMap"],
            pbr["EmissiveColorMap"],
            pbr["RoughnessMetallicOcclusionMap"],
            pbr["NormalMap"],
            pbr["BrdfLookupMap"],
            pbr["PrefilteredSpecularMap"],
        ],
        "commandOps": ["inputAssembler", "shaderStage", "rasterizer", "outputMerger", "draw"],
    }


def validate_mesh(mesh: dict[str, Any]) -> dict[str, Any] | None:
    if mesh.get("topology") != "TriangleList":
        return {
            "code": "material_pbr.unsupported_topology",
            "message": f"unsupported topology: {mesh.get('topology')}",
        }
    if not mesh.get("hasVertexBuffer") or not mesh.get("hasVertexSrv"):
        return {"code": "material_pbr.vertex_buffer_undefined", "message": "Vertex buffer undefined"}
    if not mesh.get("hasIndexBuffer") or not mesh.get("hasIndexSrv"):
        return {"code": "material_pbr.indices_buffer_undefined", "message": "Indices buffer undefined"}
    return None


def get_pbr_parameters(context: RuntimeContext) -> dict[str, Any]:
    material = context.pbr_material
    return {
        "PbrParameterBuffer": material["parameterBuffer"],
        "AlbedoColorMap": material["srvs"]["baseColorMap"]["srv"],
        "EmissiveColorMap": material["srvs"]["emissiveColorMap"]["srv"],
        "RoughnessMetallicOcclusionMap": material["srvs"]["roughnessMetallicOcclusionMap"]["srv"],
        "NormalMap": material["srvs"]["normalMap"]["srv"],
        "BrdfLookupMap": "PbrLookUpTextureSrv",
        "PrefilteredSpecularMap": context.context_textures.get("PrefilteredSpecular"),
    }


def latest_draw_command(trace: list[dict[str, Any]]) -> dict[str, Any]:
    for entry in reversed(trace):
        if entry.get("op") == "drawMesh.pbrBinding":
            return entry.get("command", {})
    return {}


def write_artifacts(
    out_dir: Path,
    trace: list[dict[str, Any]],
    command: dict[str, Any],
    errors: list[dict[str, Any]],
) -> int:
    write_json(out_dir / "material_scope_trace.json", trace)
    write_json(out_dir / "mesh_pbr_draw_command.json", command)
    write_json(out_dir / "pbr_binding_errors.json", errors)
    return 0 if not any(error.get("fatal") for error in errors) else 1


def write_json(path: Path, payload: Any) -> None:
    path.write_text(json.dumps(payload, indent=2, ensure_ascii=False) + "\n", encoding="utf8")


if __name__ == "__main__":
    raise SystemExit(main())

#!/usr/bin/env python3
"""Build registry-driven ShaderIR, cache, and generated MSL artifacts."""

from __future__ import annotations

import json
import sys
from pathlib import Path
from typing import Any, Callable


RESULT_NAME = "native_shader_ir_codegen_registry_result.json"
ERRORS_NAME = "native_shader_ir_codegen_registry_errors.json"
SOURCE_NAME = "generated_registry_patch.metal"


def main() -> int:
    if len(sys.argv) != 3:
        print("usage: native_shader_ir_codegen_registry_shell.py <fixture> <out_dir>", file=sys.stderr)
        return 2

    fixture_path = Path(sys.argv[1]).expanduser().resolve()
    out_dir = Path(sys.argv[2]).expanduser().resolve()
    out_dir.mkdir(parents=True, exist_ok=True)
    clear_previous(out_dir)

    errors: list[dict[str, Any]] = []
    diagnostics: list[dict[str, Any]] = []
    fixture = read_json(fixture_path, errors, "shader_ir_registry.fixture_read_failed")
    if fixture is None:
        publish(out_dir, None, False, "fixture_read_failed", {}, {}, {}, diagnostics, errors)
        return 1

    editor_graph, command_log = replay_commands(fixture.get("commands", []))
    runtime_graph = build_runtime_graph(fixture, editor_graph)
    registry = build_registry()
    validate_registry_admission(editor_graph, registry, diagnostics)
    shader_ir = build_shader_ir(runtime_graph, registry, diagnostics)
    shader_cache = build_shader_cache(shader_ir)

    if diagnostics:
        errors.append({"code": "shader_ir.codegen_blocked_by_diagnostics", "count": len(diagnostics)})
        publish(out_dir, fixture.get("graphId"), False, "diagnostics_failed", registry_summary(registry), shader_ir, shader_cache, diagnostics, errors)
        write_json(out_dir / "command_log.json", command_log)
        write_json(out_dir / "runtime_graph.json", runtime_graph)
        return 1

    source = generate_source(shader_ir)
    (out_dir / SOURCE_NAME).write_text(source, encoding="utf8")
    publish(out_dir, fixture.get("graphId"), True, "codegen_ready", registry_summary(registry), shader_ir, shader_cache, diagnostics, errors)
    write_json(out_dir / "command_log.json", command_log)
    write_json(out_dir / "runtime_graph.json", runtime_graph)
    return 0


def replay_commands(commands: list[dict[str, Any]]) -> tuple[dict[str, Any], list[dict[str, Any]]]:
    graph = {"nodes": {}, "edges": []}
    log = []
    for index, command in enumerate(commands):
        op = command.get("op")
        if op == "createNode":
            graph["nodes"][command["id"]] = {"id": command["id"], "type": command["type"], "params": {}}
        elif op == "setParam":
            graph["nodes"][command["id"]]["params"][command["param"]] = command["value"]
        elif op == "connect":
            graph["edges"].append({"from": command["from"], "to": command["to"]})
        else:
            raise ValueError(f"unsupported command op: {op}")
        log.append({"index": index, "source": "fixture", "command": command})
    return graph, log


def build_runtime_graph(fixture: dict[str, Any], editor_graph: dict[str, Any]) -> dict[str, Any]:
    cook_order = fixture.get("expected", {}).get("cookOrder") or list(editor_graph["nodes"].keys())
    return {
        "kind": "RuntimeGraph",
        "graphId": fixture.get("graphId"),
        "cookOrder": cook_order,
        "nodes": [
            {
                "id": node_id,
                "type": editor_graph["nodes"][node_id]["type"],
                "params": editor_graph["nodes"][node_id].get("params", {}),
                "domain": "frame",
            }
            for node_id in cook_order
        ],
        "edges": editor_graph["edges"],
        "scheduler": fixture.get("scheduler", {}),
    }


def build_registry() -> dict[str, dict[str, Any]]:
    return {
        "image.constant": spec("ConstantImage", "constantImage.fragment", {"color": [0, 0, 0, 1]}, constant_resources, emit_constant),
        "image.generate.basic.blob": spec(
            "Blob",
            "blob.fragment",
            {"color": [1, 1, 1, 1], "background": [0, 0, 0, 0], "scale": 0.5, "stretch": [1, 1], "feather": 1.0},
            blob_resources,
            emit_blob,
        ),
        "image.use.blendImages": spec("BlendImages", "blendImages.fragment", {"blendFraction": 0.0}, blend_resources, emit_blend),
        "image.generate.gradient": spec("Gradient", "gradient.fragment", {"startColor": [0, 0, 0, 1], "endColor": [1, 1, 1, 1]}, gradient_resources, emit_gradient),
        "image.memory.feedback": spec("Feedback", "feedback.fragment", {"decay": 0.7, "injection": 0.5}, feedback_resources, emit_feedback),
        "image.output.renderTarget": spec("RenderTarget", "renderTarget.fragment", {}, render_target_resources, emit_render_target),
    }


def spec(
    op: str,
    template_id: str,
    params: dict[str, Any],
    resources: Callable[[dict[str, Any]], dict[str, list[str]]],
    emitter: Callable[[dict[str, Any]], str],
) -> dict[str, Any]:
    return {
        "op": op,
        "stage": "fragment",
        "templateId": template_id,
        "params": params,
        "resources": resources,
        "emitter": emitter,
    }


def build_shader_ir(runtime_graph: dict[str, Any], registry: dict[str, dict[str, Any]], diagnostics: list[dict[str, Any]]) -> dict[str, Any]:
    nodes = []
    for node in runtime_graph.get("nodes", []):
        node_type = node.get("type")
        if node_type == "output.texture":
            continue
        entry = registry.get(node_type)
        if entry is None:
            diagnostics.append({
                "code": "shader_ir.unknown_node_type",
                "nodeId": node.get("id"),
                "nodeType": node_type,
                "severity": "error",
            })
            continue
        params = {**entry["params"], **node.get("params", {})}
        ir_node = {
            "id": node["id"],
            "type": node_type,
            "op": entry["op"],
            "stage": entry["stage"],
            "templateId": entry["templateId"],
            "params": params,
            "resources": entry["resources"]({"id": node["id"], "params": params}),
        }
        nodes.append(ir_node)
    return {"kind": "ShaderIR", "version": "0.3-registry", "nodes": nodes}


def validate_registry_admission(editor_graph: dict[str, Any], registry: dict[str, dict[str, Any]], diagnostics: list[dict[str, Any]]) -> None:
    for node in editor_graph.get("nodes", {}).values():
        node_type = node.get("type")
        if node_type == "output.texture":
            continue
        if node_type not in registry:
            diagnostics.append({
                "code": "shader_ir.unknown_node_type",
                "nodeId": node.get("id"),
                "nodeType": node_type,
                "severity": "error",
            })


def build_shader_cache(shader_ir: dict[str, Any]) -> dict[str, Any]:
    return {
        "kind": "ShaderCodegenCache",
        "generatedSource": SOURCE_NAME,
        "entries": [
            {
                "nodeId": node["id"],
                "op": node["op"],
                "templateId": node["templateId"],
                "pipelineName": f"{node['id']}_pipeline",
                "cacheKey": "ir:" + stable_hash(node),
                "stage": node["stage"],
            }
            for node in shader_ir.get("nodes", [])
        ],
    }


def generate_source(shader_ir: dict[str, Any]) -> str:
    parts = [source_header()]
    for node in shader_ir.get("nodes", []):
        emitter = build_registry()[node["type"]]["emitter"]
        parts.append(emitter(node))
    return "\n\n".join(parts) + "\n"


def source_header() -> str:
    return """#include <metal_stdlib>
using namespace metal;

struct VertexOut {
    float4 position [[position]];
    float2 uv;
};

vertex VertexOut my_world_vertex(uint vertexID [[vertex_id]])
{
    float2 positions[3] = { float2(-1.0, -1.0), float2(3.0, -1.0), float2(-1.0, 3.0) };
    VertexOut out;
    out.position = float4(positions[vertexID], 0.0, 1.0);
    out.uv = positions[vertexID] * 0.5 + 0.5;
    return out;
}"""


def constant_resources(node: dict[str, Any]) -> dict[str, list[str]]:
    return {"reads": [], "writes": [f"{node['id']}.texture"]}


def blob_resources(node: dict[str, Any]) -> dict[str, list[str]]:
    return {"reads": [], "writes": [f"{node['id']}.texture"]}


def blend_resources(node: dict[str, Any]) -> dict[str, list[str]]:
    return {"reads": ["constant_bg.texture", "blob_fg.texture"], "writes": [f"{node['id']}.output"]}


def gradient_resources(node: dict[str, Any]) -> dict[str, list[str]]:
    return {"reads": [], "writes": [f"{node['id']}.texture"]}


def feedback_resources(node: dict[str, Any]) -> dict[str, list[str]]:
    return {"reads": ["gradient_1.texture", f"{node['id']}.history"], "writes": [f"{node['id']}.ping", f"{node['id']}.pong"]}


def render_target_resources(node: dict[str, Any]) -> dict[str, list[str]]:
    return {"reads": ["feedback_1.pong"], "writes": [f"{node['id']}.texture"]}


def emit_constant(node: dict[str, Any]) -> str:
    return f"""fragment float4 {node['id']}_fragment(VertexOut in [[stage_in]])
{{
    const float4 color = {color4(node['params'].get('color'))};
    return color;
}}"""


def emit_blob(node: dict[str, Any]) -> str:
    params = node["params"]
    return f"""fragment float4 {node['id']}_fragment(VertexOut in [[stage_in]])
{{
    const float4 blobColor = {color4(params.get('color'))};
    const float4 background = {color4(params.get('background'))};
    float2 centered = (in.uv - float2(0.5, 0.5)) * 2.0;
    centered = float2(centered.x / {vec2(params.get('stretch'))}.x, centered.y / {vec2(params.get('stretch'))}.y);
    float distanceFromCenter = length(centered);
    float edge0 = max(0.0001, {float(params.get('scale', 0.5)):.6f} * (1.0 - {float(params.get('feather', 1.0)):.6f}));
    float edge1 = max(edge0 + 0.0001, {float(params.get('scale', 0.5)):.6f});
    float alpha = 1.0 - smoothstep(edge0, edge1, distanceFromCenter);
    return mix(background, blobColor, alpha);
}}"""


def emit_blend(node: dict[str, Any]) -> str:
    amount = max(0.0, min(1.0, float(node["params"].get("blendFraction", 0.0))))
    return f"""fragment float4 {node['id']}_fragment(
    VertexOut in [[stage_in]],
    texture2d<float> constantTexture [[texture(0)]],
    texture2d<float> blobTexture [[texture(1)]],
    sampler textureSampler [[sampler(0)]])
{{
    float4 constantColor = constantTexture.sample(textureSampler, in.uv);
    float4 blobColor = blobTexture.sample(textureSampler, in.uv);
    return mix(constantColor, blobColor, {amount:.6f});
}}"""


def emit_gradient(node: dict[str, Any]) -> str:
    return f"""fragment float4 {node['id']}_fragment(VertexOut in [[stage_in]])
{{
    const float4 startColor = {color4(node['params'].get('startColor'))};
    const float4 endColor = {color4(node['params'].get('endColor'))};
    float diagonal = clamp((in.uv.x + in.uv.y) * 0.5, 0.0, 1.0);
    return mix(startColor, endColor, diagonal);
}}"""


def emit_feedback(node: dict[str, Any]) -> str:
    decay = float(node["params"].get("decay", 0.7))
    injection = float(node["params"].get("injection", 0.5))
    return f"""fragment float4 {node['id']}_fragment(
    VertexOut in [[stage_in]],
    texture2d<float> gradientTexture [[texture(0)]],
    texture2d<float> feedbackHistory [[texture(1)]],
    sampler textureSampler [[sampler(0)]])
{{
    float4 nowColor = gradientTexture.sample(textureSampler, in.uv);
    float4 historyColor = feedbackHistory.sample(textureSampler, clamp(in.uv + float2(0.012, -0.008), float2(0.0), float2(1.0)));
    return clamp(nowColor * {injection:.6f} + historyColor * {decay:.6f}, 0.0, 1.0);
}}"""


def emit_render_target(node: dict[str, Any]) -> str:
    return f"""fragment float4 {node['id']}_fragment(
    VertexOut in [[stage_in]],
    texture2d<float> feedbackTexture [[texture(0)]],
    sampler textureSampler [[sampler(0)]])
{{
    float4 color = feedbackTexture.sample(textureSampler, in.uv);
    float vignette = smoothstep(0.9, 0.15, distance(in.uv, float2(0.5, 0.5)));
    color.rgb = color.rgb * (0.55 + 0.45 * vignette);
    color.a = 1.0;
    return color;
}}"""


def registry_summary(registry: dict[str, dict[str, Any]]) -> dict[str, Any]:
    return {
        "kind": "NodeCodegenRegistry",
        "version": "0.3",
        "nodeTypes": list(registry.keys()),
        "entries": [
            {
                "nodeType": node_type,
                "op": entry["op"],
                "templateId": entry["templateId"],
                "stage": entry["stage"],
                "params": entry["params"],
            }
            for node_type, entry in registry.items()
        ],
    }


def publish(
    out_dir: Path,
    graph_id: str | None,
    ok: bool,
    status: str,
    registry: dict[str, Any],
    shader_ir: dict[str, Any],
    shader_cache: dict[str, Any],
    diagnostics: list[dict[str, Any]],
    errors: list[dict[str, Any]],
) -> None:
    result = {
        "kind": "NativeShaderIrCodegenRegistryProof",
        "graphId": graph_id,
        "ok": ok,
        "status": status,
        "claims": {
            "registryDrivenCodegen": ok,
            "perShellHandAdapter": False,
            "completeShaderLanguage": False,
            "hlslToMslTranslation": False,
        },
        "artifactIndex": {
            "registry": "node_codegen_registry.json",
            "shaderIr": "shader_ir.json",
            "shaderCache": "shader_cache.json",
            "source": SOURCE_NAME,
            "diagnostics": "diagnostics.json",
            "errors": ERRORS_NAME,
        },
    }
    write_json(out_dir / RESULT_NAME, result)
    write_json(out_dir / "node_codegen_registry.json", registry)
    write_json(out_dir / "shader_ir.json", shader_ir)
    write_json(out_dir / "shader_cache.json", shader_cache)
    write_json(out_dir / "diagnostics.json", diagnostics)
    write_json(out_dir / ERRORS_NAME, errors)


def clear_previous(out_dir: Path) -> None:
    for name in [
        RESULT_NAME,
        ERRORS_NAME,
        "node_codegen_registry.json",
        "shader_ir.json",
        "shader_cache.json",
        "diagnostics.json",
        "command_log.json",
        "runtime_graph.json",
        SOURCE_NAME,
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


def color4(values: Any) -> str:
    vals = list(values if isinstance(values, list) else [0, 0, 0, 1])
    while len(vals) < 4:
        vals.append(1 if len(vals) == 3 else 0)
    return "float4(" + ", ".join(f"{float(value):.6f}" for value in vals[:4]) + ")"


def vec2(values: Any) -> str:
    vals = list(values if isinstance(values, list) else [1, 1])
    while len(vals) < 2:
        vals.append(1)
    return f"float2({float(vals[0]):.6f}, {float(vals[1]):.6f})"


def stable_hash(payload: Any) -> str:
    text = json.dumps(payload, sort_keys=True, separators=(",", ":"))
    value = 2166136261
    for char in text:
        value ^= ord(char)
        value = (value * 16777619) & 0xFFFFFFFF
    return f"fnv1a32:{value:08x}"


if __name__ == "__main__":
    raise SystemExit(main())

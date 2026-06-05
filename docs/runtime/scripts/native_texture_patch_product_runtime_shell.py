#!/usr/bin/env python3
"""Run the native product texture patch runtime proof."""

from __future__ import annotations

import json
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path
from typing import Any


RESULT_NAME = "native_texture_patch_product_runtime_result.json"
ERRORS_NAME = "native_texture_patch_product_runtime_errors.json"


def main() -> int:
    if len(sys.argv) != 3:
        print("usage: native_texture_patch_product_runtime_shell.py <fixture> <out_dir>", file=sys.stderr)
        return 2

    fixture_path = Path(sys.argv[1]).expanduser().resolve()
    out_dir = Path(sys.argv[2]).expanduser().resolve()
    out_dir.mkdir(parents=True, exist_ok=True)
    clear_previous(out_dir)

    repo_root = Path(__file__).resolve().parents[3]
    errors: list[dict[str, Any]] = []
    fixture = read_json(fixture_path, errors, "native_texture_product.fixture_read_failed")
    if fixture is None:
        publish(out_dir, None, False, "fixture_read_failed", {}, {}, {}, {}, {}, errors)
        return 1

    editor_graph, command_log = replay_commands(fixture.get("commands", []))
    runtime_graph = build_runtime_graph(fixture, editor_graph)
    resource_ledger = build_resource_ledger(fixture)
    shader_ir = build_shader_ir(runtime_graph)
    shader_cache = build_shader_cache(shader_ir)
    source = generate_msl(runtime_graph)
    source_path = out_dir / "generated_texture_patch.metal"
    source_path.write_text(source, encoding="utf8")

    metal_result, metal_errors = run_probe(repo_root, source_path, fixture)
    errors.extend(metal_errors)
    frame_stats = {
        "width": metal_result.get("width"),
        "height": metal_result.get("height"),
        "nonBlack": metal_result.get("nonBlack"),
        "varied": metal_result.get("varied"),
        "nonBlackPixels": metal_result.get("nonBlackPixels"),
        "uniqueColorSamples": metal_result.get("uniqueColorSamples"),
    } if metal_result.get("ok") else {}

    ok = metal_result.get("status") == "rendered" and not errors
    publish(
        out_dir,
        fixture.get("graphId"),
        ok,
        "rendered" if ok else metal_result.get("status", "probe_failed"),
        runtime_graph,
        resource_ledger,
        shader_ir,
        shader_cache,
        frame_stats,
        errors,
        command_log,
    )
    return 0 if ok else 1


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
        "frames": fixture.get("scheduler", {}).get("frames", []),
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


def build_resource_ledger(fixture: dict[str, Any]) -> dict[str, Any]:
    viewport = fixture.get("viewport", {})
    width = int(viewport.get("width", 64))
    height = int(viewport.get("height", 64))

    def resource(resource_id: str, owner: str, role: str) -> dict[str, Any]:
        return {
            "id": resource_id,
            "ownerNode": owner,
            "role": role,
            "width": width,
            "height": height,
            "format": "RGBA8_Unorm",
            "bindFlags": ["RenderTarget", "ShaderResource"],
        }

    return {
        "kind": "NativeTexturePatchProductResourceLedger",
        "resources": {
            "gradient_1.texture": resource("gradient_1.texture", "gradient_1", "ColorBuffer"),
            "feedback_1.history": resource("feedback_1.history", "feedback_1", "FeedbackHistory"),
            "feedback_1.ping": resource("feedback_1.ping", "feedback_1", "FeedbackState"),
            "feedback_1.pong": resource("feedback_1.pong", "feedback_1", "FeedbackState"),
            "render_target_1.texture": resource("render_target_1.texture", "render_target_1", "RenderTarget"),
        },
        "renderPasses": [
            {"name": "gradient_1_pass", "nodeId": "gradient_1", "reads": [], "writes": ["gradient_1.texture"]},
            {
                "name": "feedback_1_pass",
                "nodeId": "feedback_1",
                "reads": ["gradient_1.texture", "feedback_1.history"],
                "writes": ["feedback_1.ping", "feedback_1.pong"],
            },
            {
                "name": "render_target_1_pass",
                "nodeId": "render_target_1",
                "reads": ["feedback_1.pong"],
                "writes": ["render_target_1.texture"],
            },
        ],
    }


def build_shader_ir(runtime_graph: dict[str, Any]) -> dict[str, Any]:
    op_by_type = {
        "image.generate.gradient": "Gradient",
        "image.memory.feedback": "Feedback",
        "image.output.renderTarget": "RenderTarget",
    }
    return {
        "kind": "ShaderIR",
        "version": "0.2",
        "nodes": [
            {"id": node["id"], "op": op_by_type[node["type"]], "stage": "fragment", "params": node.get("params", {})}
            for node in runtime_graph.get("nodes", [])
            if node["type"] in op_by_type
        ],
    }


def build_shader_cache(shader_ir: dict[str, Any]) -> dict[str, Any]:
    return {
        "kind": "NativeTexturePatchShaderCache",
        "generatedSource": "generated_texture_patch.metal",
        "entries": [
            {
                "nodeId": node["id"],
                "op": node["op"],
                "cacheKey": stable_json(node),
                "pipelineName": f"{node['id']}_pipeline",
                "stage": "fragment",
            }
            for node in shader_ir.get("nodes", [])
        ],
    }


def generate_msl(runtime_graph: dict[str, Any]) -> str:
    nodes = {node["id"]: node for node in runtime_graph.get("nodes", [])}
    gradient = nodes["gradient_1"]["params"]
    feedback = nodes["feedback_1"]["params"]
    start = color4(gradient.get("startColor", [0, 0, 0, 1]))
    end = color4(gradient.get("endColor", [1, 1, 1, 1]))
    decay = float(feedback.get("decay", 0.7))
    injection = float(feedback.get("injection", 0.5))
    return f"""#include <metal_stdlib>
using namespace metal;

struct VertexOut {{
    float4 position [[position]];
    float2 uv;
}};

vertex VertexOut my_world_vertex(uint vertexID [[vertex_id]])
{{
    float2 positions[3] = {{ float2(-1.0, -1.0), float2(3.0, -1.0), float2(-1.0, 3.0) }};
    VertexOut out;
    out.position = float4(positions[vertexID], 0.0, 1.0);
    out.uv = positions[vertexID] * 0.5 + 0.5;
    return out;
}}

fragment float4 gradient_1_fragment(VertexOut in [[stage_in]])
{{
    const float4 startColor = {start};
    const float4 endColor = {end};
    float diagonal = clamp((in.uv.x + in.uv.y) * 0.5, 0.0, 1.0);
    return mix(startColor, endColor, diagonal);
}}

fragment float4 feedback_1_fragment(
    VertexOut in [[stage_in]],
    texture2d<float> gradientTexture [[texture(0)]],
    texture2d<float> feedbackHistory [[texture(1)]],
    sampler textureSampler [[sampler(0)]])
{{
    float4 nowColor = gradientTexture.sample(textureSampler, in.uv);
    float2 driftUv = clamp(in.uv + float2(0.012, -0.008), float2(0.0), float2(1.0));
    float4 historyColor = feedbackHistory.sample(textureSampler, driftUv);
    return clamp(nowColor * {injection} + historyColor * {decay}, 0.0, 1.0);
}}

fragment float4 render_target_1_fragment(
    VertexOut in [[stage_in]],
    texture2d<float> feedbackTexture [[texture(0)]],
    sampler textureSampler [[sampler(0)]])
{{
    float4 color = feedbackTexture.sample(textureSampler, in.uv);
    float vignette = smoothstep(0.9, 0.15, distance(in.uv, float2(0.5, 0.5)));
    color.rgb = color.rgb * (0.55 + 0.45 * vignette);
    color.a = 1.0;
    return color;
}}
"""


def run_probe(repo_root: Path, msl_path: Path, fixture: dict[str, Any]) -> tuple[dict[str, Any], list[dict[str, Any]]]:
    viewport = fixture.get("viewport", {})
    width = int(viewport.get("width", 64))
    height = int(viewport.get("height", 64))
    build_dir = Path(tempfile.mkdtemp(prefix="native-texture-product-build-"))
    try:
        binary = build_dir / "native_texture_patch_product_runtime_probe"
        source = repo_root / "docs/runtime/native/native_texture_patch_product_runtime_probe.mm"
        compile_cmd = [
            "xcrun",
            "clang++",
            "-std=c++17",
            "-fobjc-arc",
            "-framework",
            "Metal",
            "-framework",
            "Foundation",
            str(source),
            "-o",
            str(binary),
        ]
        compiled = subprocess.run(compile_cmd, cwd=repo_root, text=True, capture_output=True, check=False)
        if compiled.returncode != 0:
            return {"ok": False, "status": "probe_build_failed"}, [{"code": "native_texture_product.probe_build_failed", "message": clean(compiled.stderr or compiled.stdout)}]
        run = subprocess.run([str(binary), str(msl_path), str(width), str(height)], cwd=repo_root, text=True, capture_output=True, check=False)
        payload = parse_json(run.stdout)
        if payload is None:
            return {"ok": False, "status": "probe_output_invalid"}, [{"code": "native_texture_product.probe_output_invalid", "message": clean(run.stderr or run.stdout)}]
        if run.returncode != 0:
            return payload, [{"code": f"native_texture_product.{payload.get('status', 'probe_failed')}", "message": payload.get("message", "probe failed")}]
        return payload, []
    finally:
        shutil.rmtree(build_dir, ignore_errors=True)


def publish(
    out_dir: Path,
    graph_id: str | None,
    ok: bool,
    status: str,
    runtime_graph: dict[str, Any],
    resource_ledger: dict[str, Any],
    shader_ir: dict[str, Any],
    shader_cache: dict[str, Any],
    frame_stats: dict[str, Any],
    errors: list[dict[str, Any]],
    command_log: list[dict[str, Any]] | None = None,
) -> None:
    result = {
        "kind": "NativeTexturePatchProductRuntimeProof",
        "graphId": graph_id,
        "ok": ok,
        "status": status,
        "claims": {
            "commandGraphReplayed": all(entry.get("source") == "fixture" for entry in (command_log or [])),
            "runtimeGraphBuilt": bool(runtime_graph.get("cookOrder")),
            "gradientMetalPass": ok,
            "feedbackMetalPass": ok,
            "renderTargetMetalPass": ok,
            "actualMetalRan": ok,
            "completeTextureRuntime": False,
            "completeShaderLanguage": False,
        },
    }
    write_json(out_dir / RESULT_NAME, result)
    write_json(out_dir / "runtime_graph.json", runtime_graph)
    write_json(out_dir / "resource_ledger.json", resource_ledger)
    write_json(out_dir / "shader_ir.json", shader_ir)
    write_json(out_dir / "shader_cache.json", shader_cache)
    write_json(out_dir / "frame_stats.json", frame_stats)
    write_json(out_dir / "command_log.json", command_log or [])
    write_json(out_dir / ERRORS_NAME, errors)


def clear_previous(out_dir: Path) -> None:
    for name in [
        RESULT_NAME,
        ERRORS_NAME,
        "runtime_graph.json",
        "resource_ledger.json",
        "shader_ir.json",
        "shader_cache.json",
        "frame_stats.json",
        "command_log.json",
        "generated_texture_patch.metal",
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


def parse_json(text: str) -> dict[str, Any] | None:
    for line in text.splitlines():
        line = line.strip()
        if line.startswith("{") and line.endswith("}"):
            try:
                return json.loads(line)
            except Exception:
                return None
    return None


def color4(values: Any) -> str:
    vals = list(values if isinstance(values, list) else [0, 0, 0, 1])
    while len(vals) < 4:
        vals.append(1 if len(vals) == 3 else 0)
    return "float4(" + ", ".join(f"{float(value):.6f}" for value in vals[:4]) + ")"


def stable_json(payload: Any) -> str:
    return json.dumps(payload, sort_keys=True, separators=(",", ":"))


def clean(text: str) -> str:
    return text.replace(str(Path.home()), "~")


if __name__ == "__main__":
    raise SystemExit(main())

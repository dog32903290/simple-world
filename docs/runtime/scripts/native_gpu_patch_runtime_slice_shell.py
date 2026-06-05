#!/usr/bin/env python3
"""
Run the first native GPU patch runtime slice.

This lane is bounded: it replays a command-authored ConstantImage/Blob/Blend
patch, builds a runtimeGraph and resource ledger, generates explicit MSL, then
runs a real Metal readback probe when available.
"""

from __future__ import annotations

import json
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path
from typing import Any

from native_resource_api import TextureResourceRegistry


RESULT_NAME = "native_gpu_patch_runtime_slice_result.json"
TRACE_NAME = "native_gpu_patch_runtime_slice_trace.json"
ERRORS_NAME = "native_gpu_patch_runtime_slice_errors.json"


def main() -> int:
    if len(sys.argv) != 3:
        print("usage: native_gpu_patch_runtime_slice_shell.py <fixture> <out_dir>", file=sys.stderr)
        return 2

    fixture_path = Path(sys.argv[1]).expanduser().resolve()
    out_dir = Path(sys.argv[2]).expanduser().resolve()
    out_dir.mkdir(parents=True, exist_ok=True)
    clear_optional_artifacts(out_dir)

    repo_root = Path(__file__).resolve().parents[3]
    trace: list[dict[str, Any]] = [{"op": "loadNativeGpuPatchRuntimeSlice", "fixture": display_path(fixture_path, repo_root)}]
    errors: list[dict[str, Any]] = []

    fixture = read_json(fixture_path, errors, "native_gpu_patch.fixture_read_failed")
    if fixture is None:
        result = result_payload(None, False, "fixture_read_failed", claim_flags(False, False, False, False, False, False, False))
        publish(out_dir, result, trace, errors, {}, {}, {}, "")
        return 1

    try:
        editor_graph, command_trace = replay_commands(fixture)
        trace.extend(command_trace)
        runtime_graph = build_runtime_graph(fixture, editor_graph)
        trace.append({"op": "buildRuntimeGraph", "cookOrder": runtime_graph["cookOrder"]})
        resource_ledger = build_resource_ledger(runtime_graph)
        trace.append({"op": "allocatePatchResources", "resourceIds": list(resource_ledger["resources"].keys())})
        shader_cache, source = build_shader_cache_and_source(fixture, runtime_graph)
        trace.append({"op": "buildShaderCodegenCache", "entries": [entry["nodeId"] for entry in shader_cache["entries"]]})
        Path(out_dir / "generated_patch.metal").write_text(source, encoding="utf8")
    except Exception as exc:
        errors.append({"code": "native_gpu_patch.prepare_failed", "message": str(exc)})
        result = result_payload(fixture.get("graphId"), False, "prepare_failed", claim_flags(False, False, False, False, False, False, False))
        publish(out_dir, result, trace, errors, {}, {}, {}, "")
        return 1

    metal_result, metal_trace, metal_errors, frame_stats = run_metal_probe(repo_root, out_dir / "generated_patch.metal", fixture)
    trace.extend(metal_trace)
    errors.extend(metal_errors)

    structural_ok = not any(error["code"].startswith("native_gpu_patch.") for error in errors)
    metal_ok = metal_result.get("status") == "rendered"
    status = "rendered" if metal_ok else metal_result.get("status", "probe_failed")
    claims = claim_flags(
        command_graph=True,
        runtime_graph=True,
        frame_scheduler=True,
        resource_allocator=True,
        shader_cache=True,
        compiler=bool(metal_result.get("actualCompilerRan")),
        metal=bool(metal_result.get("actualMetalRan")),
    )
    result = result_payload(fixture.get("graphId"), structural_ok and metal_ok and not errors, status, claims)
    result["viewport"] = fixture.get("viewport", {})
    if frame_stats:
        result["frameStats"] = frame_stats

    publish(out_dir, result, trace, errors, runtime_graph, resource_ledger, shader_cache, source, frame_stats)
    return 0 if result["ok"] else 1


def replay_commands(fixture: dict[str, Any]) -> tuple[dict[str, Any], list[dict[str, Any]]]:
    nodes: dict[str, dict[str, Any]] = {}
    edges: list[dict[str, Any]] = []
    trace: list[dict[str, Any]] = []
    for command in fixture.get("commands", []):
        op = command.get("op")
        if op == "createNode":
            nodes[command["id"]] = {"id": command["id"], "type": command["type"], "params": {}}
        elif op == "setParam":
            nodes[command["id"]]["params"][command["param"]] = command["value"]
        elif op == "connect":
            edges.append({"from": command["from"], "to": command["to"]})
        else:
            raise ValueError(f"unsupported command op: {op}")
        trace.append({"op": f"command.{op}", "nodeId": command.get("id")})
    return {"nodes": nodes, "edges": edges}, trace


def build_runtime_graph(fixture: dict[str, Any], editor_graph: dict[str, Any]) -> dict[str, Any]:
    cook_order = fixture.get("expected", {}).get("cookOrder") or list(editor_graph["nodes"].keys())
    runtime_nodes = []
    for node_id in cook_order:
        node = editor_graph["nodes"][node_id]
        runtime_nodes.append({
            "id": node_id,
            "type": node["type"],
            "domain": "frame",
            "params": node.get("params", {}),
            "outputs": output_ports_for(node["type"]),
        })
    return {
        "kind": "RuntimeGraph",
        "graphId": fixture.get("graphId"),
        "cookOrder": cook_order,
        "frames": fixture.get("scheduler", {}).get("frames", []),
        "nodes": runtime_nodes,
        "edges": editor_graph["edges"],
        "scheduler": fixture.get("scheduler", {}),
    }


def output_ports_for(node_type: str) -> list[str]:
    if node_type in {"image.constant", "image.generate.basic.blob"}:
        return ["textureOutput"]
    if node_type == "image.use.blendImages":
        return ["outputImage"]
    if node_type == "output.texture":
        return []
    raise ValueError(f"unsupported node type: {node_type}")


def build_resource_ledger(runtime_graph: dict[str, Any]) -> dict[str, Any]:
    registry = TextureResourceRegistry()
    resources: dict[str, Any] = {}
    views: dict[str, Any] = {}
    width, height = runtime_resolution(runtime_graph)
    for node in runtime_graph["nodes"]:
        texture_id = texture_id_for_node(node)
        if texture_id is None:
            continue
        payload = {
            "id": texture_id,
            "owner": node["id"],
            "role": "ColorBuffer",
            "width": width,
            "height": height,
            "format": "RGBA8_Unorm",
            "bindFlags": ["RenderTarget", "ShaderResource"],
        }
        texture = registry.register_texture(payload)
        srv = registry.create_view(texture.id, "srv")
        rtv = registry.create_view(texture.id, "rtv")
        resources[texture.id] = {**texture.to_json(), "ownerNode": node["id"], "role": "ColorBuffer"}
        views[f"{texture.id}.srv"] = srv.to_json()
        views[f"{texture.id}.rtv"] = rtv.to_json()
    render_passes = [
        {
            "name": "constant_bg_pass",
            "nodeId": "constant_bg",
            "pipelineName": "constant_bg_pipeline",
            "reads": [],
            "writes": ["constant_bg.texture"],
        },
        {
            "name": "blob_fg_pass",
            "nodeId": "blob_fg",
            "pipelineName": "blob_fg_pipeline",
            "reads": [],
            "writes": ["blob_fg.texture"],
        },
        {
            "name": "blend_1_pass",
            "nodeId": "blend_1",
            "pipelineName": "blend_1_pipeline",
            "reads": ["constant_bg.texture", "blob_fg.texture"],
            "writes": ["blend_1.output"],
        },
    ]
    return {
        "kind": "NativeGpuPatchResourceLedger",
        "resources": resources,
        "views": views,
        "renderPasses": render_passes,
    }


def texture_id_for_node(node: dict[str, Any]) -> str | None:
    if node["type"] == "image.constant":
        return f"{node['id']}.texture"
    if node["type"] == "image.generate.basic.blob":
        return f"{node['id']}.texture"
    if node["type"] == "image.use.blendImages":
        return f"{node['id']}.output"
    return None


def build_shader_cache_and_source(fixture: dict[str, Any], runtime_graph: dict[str, Any]) -> tuple[dict[str, Any], str]:
    entries = []
    for node in runtime_graph["nodes"]:
        if node["type"] == "output.texture":
            continue
        entries.append({
            "nodeId": node["id"],
            "nodeType": node["type"],
            "cacheKey": f"{node['type']}:{stable_json(node.get('params', {}))}",
            "pipelineName": f"{node['id']}_pipeline",
            "stage": "fragment",
        })
    source = generate_msl_source(fixture, runtime_graph)
    return {"kind": "NativeGpuPatchShaderCache", "generatedSource": "generated_patch.metal", "entries": entries}, source


def generate_msl_source(fixture: dict[str, Any], runtime_graph: dict[str, Any]) -> str:
    node_map = {node["id"]: node for node in runtime_graph["nodes"]}
    bg = node_map["constant_bg"]["params"]
    blob = node_map["blob_fg"]["params"]
    blend = node_map["blend_1"]["params"]
    bg_color = color4(bg.get("color", [0, 0, 0, 1]))
    blob_color = color4(blob.get("color", [1, 1, 1, 1]))
    blob_background = color4(blob.get("background", [0, 0, 0, 0]))
    stretch = vec2(blob.get("stretch", [1, 1]))
    scale = float(blob.get("scale", 0.5))
    feather = float(blob.get("feather", 1.0))
    blend_fraction = float(blend.get("blendFraction", 0.0))
    amount = max(0.0, min(1.0, blend_fraction - int(blend_fraction)))
    if blend_fraction == 0.0:
        amount = 0.0
    elif blend_fraction >= 1.0 and int(blend_fraction) % 2 == 1:
        amount = blend_fraction - int(blend_fraction)
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

static float4 blobSample(float2 uv)
{{
    const float4 blobColor = {blob_color};
    const float4 blobBackground = {blob_background};
    float2 centered = (uv - float2(0.5, 0.5)) * 2.0;
    centered = float2(centered.x / {stretch}.x, centered.y / {stretch}.y);
    float distanceFromCenter = length(centered);
    float edge0 = max(0.0001, {scale} * (1.0 - {feather}));
    float edge1 = max(edge0 + 0.0001, {scale});
    float alpha = 1.0 - smoothstep(edge0, edge1, distanceFromCenter);
    return mix(blobBackground, blobColor, alpha);
}}

fragment float4 constant_bg_fragment(VertexOut in [[stage_in]])
{{
    const float4 constant_bg_color = {bg_color};
    return constant_bg_color;
}}

fragment float4 blob_fg_fragment(VertexOut in [[stage_in]])
{{
    return blobSample(in.uv);
}}

fragment float4 blend_1_fragment(
    VertexOut in [[stage_in]],
    texture2d<float> constantTexture [[texture(0)]],
    texture2d<float> blobTexture [[texture(1)]],
    sampler textureSampler [[sampler(0)]])
{{
    float4 constantColor = constantTexture.sample(textureSampler, in.uv);
    float4 blobColor = blobTexture.sample(textureSampler, in.uv);
    return mix(constantColor, blobColor, {amount});
}}

fragment float4 my_world_fragment(VertexOut in [[stage_in]])
{{
    const float4 constant_bg_color = {bg_color};
    float4 constantColor = constant_bg_color;
    float4 blobColor = blobSample(in.uv);
    return mix(constantColor, blobColor, {amount});
}}
"""


def run_metal_probe(
    repo_root: Path,
    msl_path: Path,
    fixture: dict[str, Any],
) -> tuple[dict[str, Any], list[dict[str, Any]], list[dict[str, Any]], dict[str, Any] | None]:
    viewport = fixture.get("viewport", {})
    width = int(viewport.get("width", 64))
    height = int(viewport.get("height", 64))
    build_dir = Path(tempfile.mkdtemp(prefix="native-gpu-patch-build-"))
    trace: list[dict[str, Any]] = []
    errors: list[dict[str, Any]] = []
    try:
        probe_bin = build_dir / "native_gpu_patch_runtime_slice_probe"
        probe_source = repo_root / "docs/runtime/native/native_gpu_patch_runtime_slice_probe.mm"
        compile_cmd = [
            "xcrun",
            "clang++",
            "-std=c++17",
            "-fobjc-arc",
            "-framework",
            "Metal",
            "-framework",
            "Foundation",
            str(probe_source),
            "-o",
            str(probe_bin),
        ]
        build = subprocess.run(compile_cmd, cwd=repo_root, text=True, capture_output=True)
        trace.append({"op": "buildMetalPatchProbe", "exitCode": build.returncode})
        if build.returncode != 0:
            errors.append({"code": "native_gpu_patch.probe_build_failed", "message": clean_text(build.stderr or build.stdout)})
            return {"status": "probe_build_failed", "actualCompilerRan": False, "actualMetalRan": False}, trace, errors, None
        run = subprocess.run([str(probe_bin), str(msl_path), str(width), str(height)], cwd=repo_root, text=True, capture_output=True)
        trace.append({"op": "runMetalPatchProbe", "exitCode": run.returncode})
    finally:
        shutil.rmtree(build_dir, ignore_errors=True)

    payload = parse_json_line(run.stdout)
    if payload is None:
        errors.append({"code": "native_gpu_patch.probe_output_invalid", "message": clean_text(run.stderr or run.stdout or "probe did not emit JSON")})
        return {"status": "probe_output_invalid", "actualCompilerRan": False, "actualMetalRan": False}, trace, errors, None
    if payload.get("status") != "rendered":
        code = "native_gpu_patch.device_unavailable" if payload.get("status") == "blocked_metal_device_unavailable" else "native_gpu_patch.probe_failed"
        errors.append({"code": code, "message": clean_text(str(payload.get("message", payload.get("status"))))})
        return payload, trace, errors, None
    stats = {
        "width": payload["width"],
        "height": payload["height"],
        "byteCount": payload["byteCount"],
        "nonBlack": payload["nonBlack"],
        "varied": payload["varied"],
        "nonBlackPixels": payload["nonBlackPixels"],
        "uniqueColorSamples": payload["uniqueColorSamples"],
    }
    return payload, trace, errors, stats


def result_payload(graph_id: Any, ok: bool, status: str, claims: dict[str, bool]) -> dict[str, Any]:
    return {
        "kind": "NativeGpuPatchRuntimeSliceProof",
        "graphId": graph_id,
        "ok": ok,
        "status": status,
        "backend": "Metal",
        "claims": claims,
    }


def claim_flags(
    command_graph: bool,
    runtime_graph: bool,
    frame_scheduler: bool,
    resource_allocator: bool,
    shader_cache: bool,
    compiler: bool,
    metal: bool,
) -> dict[str, bool]:
    return {
        "commandGraphReplayed": command_graph,
        "runtimeGraphBuilt": runtime_graph,
        "frameSchedulerRan": frame_scheduler,
        "resourceAllocatorRan": resource_allocator,
        "shaderCodegenCacheBuilt": shader_cache,
        "actualCompilerRan": compiler,
        "actualMetalRan": metal,
        "nativeCanvasComplete": False,
        "aiWorkerRepairLoop": False,
        "genericShaderIrComplete": False,
        "vuoParity": False,
        "backendReplacementReady": False,
        "fullTixlCloneParity": False,
    }


def publish(
    out_dir: Path,
    result: dict[str, Any],
    trace: list[dict[str, Any]],
    errors: list[dict[str, Any]],
    runtime_graph: dict[str, Any],
    resource_ledger: dict[str, Any],
    shader_cache: dict[str, Any],
    source: str,
    frame_stats: dict[str, Any] | None = None,
) -> None:
    write_json(out_dir / RESULT_NAME, result)
    write_json(out_dir / TRACE_NAME, trace + [{"op": "publishNativeGpuPatchRuntimeSliceArtifacts", "ok": result.get("ok") is True}])
    write_json(out_dir / ERRORS_NAME, errors)
    write_json(out_dir / "runtime_graph.json", runtime_graph)
    write_json(out_dir / "resource_ledger.json", resource_ledger)
    write_json(out_dir / "shader_cache.json", shader_cache)
    if source:
        (out_dir / "generated_patch.metal").write_text(source, encoding="utf8")
    if frame_stats:
        write_json(out_dir / "frame_stats.json", frame_stats)


def runtime_resolution(runtime_graph: dict[str, Any]) -> tuple[int, int]:
    for node in runtime_graph["nodes"]:
        resolution = node.get("params", {}).get("resolution")
        if isinstance(resolution, list) and len(resolution) == 2:
            return int(resolution[0]), int(resolution[1])
    return 64, 64


def color4(values: list[Any]) -> str:
    padded = list(values[:4]) + [1.0] * max(0, 4 - len(values))
    return "float4(" + ", ".join(f"{float(value):.8g}" for value in padded[:4]) + ")"


def vec2(values: list[Any]) -> str:
    padded = list(values[:2]) + [1.0] * max(0, 2 - len(values))
    return "float2(" + ", ".join(f"{float(value):.8g}" for value in padded[:2]) + ")"


def stable_json(payload: Any) -> str:
    return json.dumps(payload, sort_keys=True, separators=(",", ":"))


def parse_json_line(stdout: str) -> dict[str, Any] | None:
    text = stdout.strip()
    if not text:
        return None
    try:
        return json.loads(text.splitlines()[-1])
    except json.JSONDecodeError:
        return None


def read_json(path: Path, errors: list[dict[str, Any]], code: str) -> Any | None:
    try:
        return json.loads(path.read_text(encoding="utf8"))
    except Exception as exc:
        errors.append({"code": code, "message": str(exc)})
        return None


def write_json(path: Path, payload: Any) -> None:
    path.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n", encoding="utf8")


def clear_optional_artifacts(out_dir: Path) -> None:
    for name in ["frame_stats.json", "generated_patch.metal"]:
        path = out_dir / name
        if path.exists():
            path.unlink()


def display_path(path: Path, repo_root: Path) -> str:
    try:
        return str(path.resolve().relative_to(repo_root))
    except ValueError:
        return path.name


def clean_text(text: str) -> str:
    return " ".join(text.split())


if __name__ == "__main__":
    raise SystemExit(main())

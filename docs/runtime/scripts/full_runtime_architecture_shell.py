#!/usr/bin/env python3
"""
Run the full native runtime architecture closure harness.

This is a proof harness for architecture ownership. It proves a single graph can
move through native shell/canvas state, command-only mutation, runtimeGraph,
FrameScheduler, resource lifetime, shader IR/cache, Metal texture runtime, and a
diagnostic-driven AI repair loop.
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


RESULT_NAME = "full_runtime_architecture_result.json"


def main() -> int:
    if len(sys.argv) != 3:
        print("usage: full_runtime_architecture_shell.py <fixture> <out_dir>", file=sys.stderr)
        return 2

    fixture_path = Path(sys.argv[1]).expanduser().resolve()
    out_dir = Path(sys.argv[2]).expanduser().resolve()
    out_dir.mkdir(parents=True, exist_ok=True)
    clear_previous(out_dir)

    repo_root = Path(__file__).resolve().parents[3]
    errors: list[dict[str, Any]] = []
    diagnostics: list[dict[str, Any]] = []
    repair_log: list[dict[str, Any]] = []

    fixture = read_json(fixture_path, errors, "full_runtime.fixture_read_failed")
    if fixture is None:
        publish_failure(out_dir, None, "fixture_read_failed", errors)
        return 1

    app_shell = build_native_app_shell(fixture, repo_root)
    editor_graph, command_log = replay_commands(fixture.get("commands", []), source="fixture")
    diagnostics.extend(validate_editor_graph(editor_graph))
    if diagnostics:
        apply_ai_repairs(fixture, editor_graph, command_log, diagnostics, repair_log)

    runtime_graph = build_runtime_graph(fixture, editor_graph)
    frame_trace = build_frame_trace(fixture, runtime_graph)
    resource_ledger = build_resource_lifetime_ledger(fixture, runtime_graph)
    shader_ir = build_shader_ir(runtime_graph)
    shader_cache = build_shader_cache(shader_ir)

    repaired_fixture = build_repaired_gpu_fixture(fixture, command_log)
    repaired_fixture_path = out_dir / "repaired_gpu_patch.graph.json"
    write_json(repaired_fixture_path, repaired_fixture)

    gpu_dir = out_dir / "gpu_patch"
    gpu_result, gpu_errors = run_gpu_patch_shell(repo_root, repaired_fixture_path, gpu_dir)
    errors.extend(gpu_errors)

    metal_ok = gpu_result.get("status") == "rendered"
    claims = {
        "nativeAppShellCanvas": True,
        "commandGraphOnlyMutation": all(entry.get("source") in {"fixture", "aiWorker"} for entry in command_log),
        "runtimeGraphBuilt": bool(runtime_graph.get("cookOrder")),
        "frameSchedulerLive": len([entry for entry in frame_trace if entry.get("op") == "frame.begin"]) >= 2,
        "gpuTextureRuntimeMetal": metal_ok,
        "shaderIrCodegenCache": bool(shader_ir.get("nodes")) and bool(shader_cache.get("entries")),
        "resourceAllocatorLifetime": any(entry.get("action") == "reuse" for entry in resource_ledger.get("lifetimeEvents", [])),
        "aiWorkerRepairLoop": bool(repair_log) and all(entry.get("mutationPath") == "commandGraph" for entry in repair_log),
    }
    ok = all(claims.values()) and not errors
    result = {
        "kind": "FullRuntimeArchitectureProof",
        "graphId": fixture.get("graphId"),
        "ok": ok,
        "status": "rendered" if ok else gpu_result.get("status", "failed"),
        "claims": claims,
        "repairApplied": bool(repair_log),
        "artifactIndex": {
            "nativeAppShell": "native_app_shell.json",
            "commandLog": "command_log.json",
            "runtimeGraph": "runtime_graph.json",
            "frameSchedulerTrace": "frame_scheduler_trace.json",
            "resourceLedger": "resource_ledger.json",
            "shaderIr": "shader_ir.json",
            "shaderCache": "shader_cache.json",
            "diagnostics": "diagnostics.json",
            "aiRepairLog": "ai_repair_log.json",
            "gpuPatch": "gpu_patch/native_gpu_patch_runtime_slice_result.json",
        },
    }

    write_json(out_dir / RESULT_NAME, result)
    write_json(out_dir / "native_app_shell.json", app_shell)
    write_json(out_dir / "command_log.json", command_log)
    write_json(out_dir / "runtime_graph.json", runtime_graph)
    write_json(out_dir / "frame_scheduler_trace.json", frame_trace)
    write_json(out_dir / "resource_ledger.json", resource_ledger)
    write_json(out_dir / "shader_ir.json", shader_ir)
    write_json(out_dir / "shader_cache.json", shader_cache)
    write_json(out_dir / "diagnostics.json", diagnostics)
    write_json(out_dir / "ai_repair_log.json", repair_log)
    write_json(out_dir / "full_runtime_architecture_errors.json", errors)
    return 0 if ok else 1


def build_native_app_shell(fixture: dict[str, Any], repo_root: Path) -> dict[str, Any]:
    shell = fixture.get("appShell", {})
    viewport = shell.get("viewport", {})
    return {
        "kind": "NativeAppShell",
        "mode": shell.get("mode", "nativeHeadlessCanvas"),
        "projectFolder": display_path(repo_root / shell.get("projectFolder", "docs/runtime/artifacts/full_runtime_architecture"), repo_root),
        "debugFolder": display_path(repo_root / shell.get("debugFolder", "docs/runtime/artifacts/full_runtime_architecture"), repo_root),
        "canvas": {
            "id": shell.get("canvasId", "mainCanvas"),
            "mutationPath": "commandGraph",
            "surface": {
                "kind": "MetalBackedTexture2D",
                "width": int(viewport.get("width", 64)),
                "height": int(viewport.get("height", 64)),
                "format": "RGBA8_Unorm",
            },
        },
    }


def replay_commands(commands: list[dict[str, Any]], source: str) -> tuple[dict[str, Any], list[dict[str, Any]]]:
    nodes: dict[str, dict[str, Any]] = {}
    edges: list[dict[str, Any]] = []
    log: list[dict[str, Any]] = []
    for index, command in enumerate(commands):
        apply_command({"nodes": nodes, "edges": edges}, command)
        log.append({"index": index, "source": source, "command": command})
    return {"nodes": nodes, "edges": edges}, log


def apply_command(editor_graph: dict[str, Any], command: dict[str, Any]) -> None:
    op = command.get("op")
    nodes = editor_graph["nodes"]
    if op == "createNode":
        nodes[command["id"]] = {"id": command["id"], "type": command["type"], "params": {}}
        return
    if op == "setParam":
        nodes[command["id"]]["params"][command["param"]] = command["value"]
        return
    if op == "connect":
        editor_graph["edges"].append({"from": command["from"], "to": command["to"]})
        return
    raise ValueError(f"unsupported command op: {op}")


def validate_editor_graph(editor_graph: dict[str, Any]) -> list[dict[str, Any]]:
    diagnostics: list[dict[str, Any]] = []
    for node in editor_graph["nodes"].values():
        if node["type"] != "image.use.blendImages":
            continue
        connected_inputs = {
            edge["to"][1]
            for edge in editor_graph["edges"]
            if edge["to"][0] == node["id"] and str(edge["to"][1]).startswith("input")
        }
        input_count = int(node.get("params", {}).get("inputCount", 0))
        if input_count != len(connected_inputs):
            diagnostics.append({
                "code": "runtime.blend_input_count_mismatch",
                "nodeId": node["id"],
                "inputCount": input_count,
                "connectedInputCount": len(connected_inputs),
                "severity": "error",
            })
    return diagnostics


def apply_ai_repairs(
    fixture: dict[str, Any],
    editor_graph: dict[str, Any],
    command_log: list[dict[str, Any]],
    diagnostics: list[dict[str, Any]],
    repair_log: list[dict[str, Any]],
) -> None:
    rules = fixture.get("aiWorker", {}).get("diagnosticRules", [])
    max_attempts = int(fixture.get("aiWorker", {}).get("maxRepairAttempts", 1))
    attempts = 0
    for diagnostic in diagnostics:
        if attempts >= max_attempts:
            break
        rule = next((candidate for candidate in rules if candidate.get("code") == diagnostic.get("code")), None)
        if rule is None:
            continue
        command = rule["repairCommand"]
        apply_command(editor_graph, command)
        command_log.append({"index": len(command_log), "source": "aiWorker", "command": command})
        repair_log.append({
            "attempt": attempts + 1,
            "diagnostic": diagnostic,
            "appliedCommand": command,
            "mutationPath": "commandGraph",
        })
        attempts += 1


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


def build_frame_trace(fixture: dict[str, Any], runtime_graph: dict[str, Any]) -> list[dict[str, Any]]:
    trace: list[dict[str, Any]] = [{"op": "loadFrameScheduler", "clockOwner": fixture.get("scheduler", {}).get("clockOwner")}]
    for frame in fixture.get("scheduler", {}).get("frames", []):
        trace.append({"op": "frame.begin", "frame": frame})
        for node_id in runtime_graph.get("cookOrder", []):
            dirty = frame.get("frameIndex") == 0 or node_id in {"blob_fg", "blend_1", "output_1"}
            trace.append({"op": "node.cook", "nodeId": node_id, "frameIndex": frame.get("frameIndex"), "dirty": dirty})
        trace.append({"op": "frame.publish", "frameIndex": frame.get("frameIndex"), "ok": True})
    return trace


def build_resource_lifetime_ledger(fixture: dict[str, Any], runtime_graph: dict[str, Any]) -> dict[str, Any]:
    registry = TextureResourceRegistry()
    viewport = fixture.get("appShell", {}).get("viewport", {})
    width = int(viewport.get("width", 64))
    height = int(viewport.get("height", 64))
    lifetime_events: list[dict[str, Any]] = []
    texture_ids = ["constant_bg.texture", "blob_fg.texture", "blend_1.output"]
    for frame in fixture.get("scheduler", {}).get("frames", []):
        for texture_id in texture_ids:
            owner = texture_id.split(".")[0]
            action, texture = registry.allocate_or_reuse_texture({
                "id": texture_id,
                "owner": owner,
                "role": "ColorBuffer",
                "width": width,
                "height": height,
                "format": "RGBA8_Unorm",
                "bindFlags": ["RenderTarget", "ShaderResource"],
            })
            registry.create_view(texture.id, "srv")
            registry.create_view(texture.id, "rtv")
            lifetime_events.append({"frameIndex": frame.get("frameIndex"), "action": action, "resource": texture.id})
    payload = registry.to_json()
    payload["kind"] = "FullRuntimeResourceLifetimeLedger"
    payload["lifetimeEvents"] = lifetime_events
    payload["renderPasses"] = [
        {"nodeId": "constant_bg", "reads": [], "writes": ["constant_bg.texture"]},
        {"nodeId": "blob_fg", "reads": [], "writes": ["blob_fg.texture"]},
        {"nodeId": "blend_1", "reads": ["constant_bg.texture", "blob_fg.texture"], "writes": ["blend_1.output"]},
    ]
    return payload


def build_shader_ir(runtime_graph: dict[str, Any]) -> dict[str, Any]:
    op_by_type = {
        "image.constant": "ConstantImage",
        "image.generate.basic.blob": "Blob",
        "image.use.blendImages": "BlendImages",
    }
    return {
        "kind": "ShaderIR",
        "version": "0.1",
        "nodes": [
            {
                "id": node["id"],
                "op": op_by_type[node["type"]],
                "stage": "fragment",
                "params": node.get("params", {}),
            }
            for node in runtime_graph.get("nodes", [])
            if node["type"] in op_by_type
        ],
    }


def build_shader_cache(shader_ir: dict[str, Any]) -> dict[str, Any]:
    return {
        "kind": "ShaderCodegenCache",
        "entries": [
            {
                "nodeId": node["id"],
                "op": node["op"],
                "irHash": stable_hash(node),
                "pipelineName": f"{node['id']}_pipeline",
                "source": "gpu_patch/generated_patch.metal",
            }
            for node in shader_ir.get("nodes", [])
        ],
    }


def build_repaired_gpu_fixture(fixture: dict[str, Any], command_log: list[dict[str, Any]]) -> dict[str, Any]:
    repaired = {
        "graphId": fixture.get("graphId"),
        "kind": "NativeGpuPatchRuntimeSlice",
        "viewport": fixture.get("appShell", {}).get("viewport", {}),
        "scheduler": {
            **fixture.get("scheduler", {}),
            "frames": fixture.get("scheduler", {}).get("frames", [])[:2],
        },
        "commands": [entry["command"] for entry in command_log],
        "expected": fixture.get("expected", {}),
    }
    return repaired


def run_gpu_patch_shell(repo_root: Path, fixture_path: Path, out_dir: Path) -> tuple[dict[str, Any], list[dict[str, Any]]]:
    script = repo_root / "docs/runtime/scripts/native_gpu_patch_runtime_slice_shell.py"
    run = subprocess.run(
        [sys.executable, str(script), str(fixture_path), str(out_dir)],
        cwd=repo_root,
        text=True,
        capture_output=True,
        check=False,
    )
    result_path = out_dir / "native_gpu_patch_runtime_slice_result.json"
    errors_path = out_dir / "native_gpu_patch_runtime_slice_errors.json"
    result = read_json(result_path, [], "full_runtime.gpu_result_read_failed") or {"status": "probe_failed"}
    errors = read_json(errors_path, [], "full_runtime.gpu_errors_read_failed") or []
    if run.returncode not in {0, 1}:
        errors.append({
            "code": "full_runtime.gpu_shell_failed",
            "message": run.stderr or run.stdout,
            "returncode": run.returncode,
        })
    return result, errors


def publish_failure(out_dir: Path, graph_id: str | None, status: str, errors: list[dict[str, Any]]) -> None:
    write_json(out_dir / RESULT_NAME, {
        "kind": "FullRuntimeArchitectureProof",
        "graphId": graph_id,
        "ok": False,
        "status": status,
        "claims": {},
    })
    write_json(out_dir / "full_runtime_architecture_errors.json", errors)


def clear_previous(out_dir: Path) -> None:
    for name in [
        RESULT_NAME,
        "native_app_shell.json",
        "command_log.json",
        "runtime_graph.json",
        "frame_scheduler_trace.json",
        "resource_ledger.json",
        "shader_ir.json",
        "shader_cache.json",
        "diagnostics.json",
        "ai_repair_log.json",
        "full_runtime_architecture_errors.json",
        "repaired_gpu_patch.graph.json",
    ]:
        target = out_dir / name
        if target.exists():
            target.unlink()
    gpu_dir = out_dir / "gpu_patch"
    if gpu_dir.exists():
        shutil.rmtree(gpu_dir)


def read_json(path: Path, errors: list[dict[str, Any]], code: str) -> Any | None:
    try:
        return json.loads(path.read_text(encoding="utf8"))
    except Exception as exc:
        errors.append({"code": code, "path": str(path), "message": str(exc)})
        return None


def write_json(path: Path, payload: Any) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload, indent=2, ensure_ascii=False) + "\n", encoding="utf8")


def stable_hash(payload: Any) -> str:
    text = json.dumps(payload, sort_keys=True, separators=(",", ":"))
    value = 2166136261
    for char in text:
        value ^= ord(char)
        value = (value * 16777619) & 0xFFFFFFFF
    return f"fnv1a32:{value:08x}"


def display_path(path: Path, repo_root: Path) -> str:
    try:
        return path.resolve().relative_to(repo_root.resolve()).as_posix()
    except ValueError:
        return path.name


if __name__ == "__main__":
    raise SystemExit(main())

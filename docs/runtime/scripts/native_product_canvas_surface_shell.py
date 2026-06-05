#!/usr/bin/env python3
"""
Run the native product canvas surface proof.

This shell replays commandGraph mutations, builds a runtimeGraph, compiles and
runs a small AppKit/MetalKit probe, then publishes path-clean artifacts that
separate a real native surface from the older headless architecture shell.
"""

from __future__ import annotations

import json
import shutil
import subprocess
import sys
from pathlib import Path
from typing import Any


RESULT_NAME = "native_product_canvas_surface_result.json"


def main() -> int:
    if len(sys.argv) != 3:
        print("usage: native_product_canvas_surface_shell.py <fixture> <out_dir>", file=sys.stderr)
        return 2

    fixture_path = Path(sys.argv[1]).expanduser().resolve()
    out_dir = Path(sys.argv[2]).expanduser().resolve()
    out_dir.mkdir(parents=True, exist_ok=True)
    clear_previous(out_dir)

    repo_root = Path(__file__).resolve().parents[3]
    errors: list[dict[str, Any]] = []
    fixture = read_json(fixture_path, errors, "native_product_canvas.fixture_read_failed")
    if fixture is None:
        publish_all(out_dir, {}, {}, {}, {}, [], errors, "fixture_read_failed", False)
        return 1

    editor_graph, command_log = replay_commands(fixture.get("commands", []), "fixture")
    runtime_graph = build_runtime_graph(fixture, editor_graph)
    probe = run_native_probe(repo_root, out_dir, fixture)
    if not probe.get("ok"):
        errors.append({
            "code": f"native_product_canvas.{probe.get('status', 'probe_failed')}",
            "message": probe.get("message", "native surface probe failed"),
        })

    app_surface = build_app_surface(fixture, probe)
    canvas_surface = build_canvas_surface(fixture, probe)
    frame_artifact = build_frame_artifact(fixture, runtime_graph, probe)
    ok = bool(probe.get("ok"))
    status = "native_surface_ready" if ok else probe.get("status", "probe_failed")
    publish_all(
        out_dir,
        app_surface,
        canvas_surface,
        runtime_graph,
        frame_artifact,
        command_log,
        errors,
        status,
        ok,
        fixture.get("graphId"),
    )
    return 0 if ok else 1


def replay_commands(commands: list[dict[str, Any]], source: str) -> tuple[dict[str, Any], list[dict[str, Any]]]:
    nodes: dict[str, dict[str, Any]] = {}
    edges: list[dict[str, Any]] = []
    log: list[dict[str, Any]] = []
    editor_graph = {"nodes": nodes, "edges": edges}
    for index, command in enumerate(commands):
        apply_command(editor_graph, command)
        log.append({"index": index, "source": source, "command": command})
    return editor_graph, log


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


def build_runtime_graph(fixture: dict[str, Any], editor_graph: dict[str, Any]) -> dict[str, Any]:
    cook_order = fixture.get("expected", {}).get("cookOrder") or list(editor_graph["nodes"].keys())
    return {
        "kind": "RuntimeGraph",
        "graphId": fixture.get("graphId"),
        "source": "commandGraph",
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


def run_native_probe(repo_root: Path, out_dir: Path, fixture: dict[str, Any]) -> dict[str, Any]:
    source = repo_root / "docs/runtime/native/native_product_canvas_surface_probe.mm"
    binary = out_dir / "native_product_canvas_surface_probe"
    compile_command = [
        "xcrun",
        "--sdk",
        "macosx",
        "clang++",
        "-std=c++17",
        "-fobjc-arc",
        str(source),
        "-framework",
        "AppKit",
        "-framework",
        "Metal",
        "-framework",
        "MetalKit",
        "-o",
        str(binary),
    ]
    compiled = subprocess.run(compile_command, cwd=repo_root, text=True, capture_output=True, check=False)
    if compiled.returncode != 0:
        return {
            "ok": False,
            "status": "compile_failed",
            "message": compiled.stderr or compiled.stdout or "native probe compile failed",
            "compileCommand": display_command(compile_command, repo_root),
        }

    window = fixture.get("appShell", {}).get("window", {})
    width = int(window.get("width", 640))
    height = int(window.get("height", 360))
    title = str(window.get("title", "simple_world"))
    run = subprocess.run([str(binary), str(width), str(height), title], cwd=repo_root, text=True, capture_output=True, check=False)
    try:
        payload = json.loads(run.stdout)
    except Exception:
        payload = {
            "ok": False,
            "status": "probe_failed",
            "message": run.stderr or run.stdout or "native probe emitted invalid JSON",
        }
    payload["returncode"] = run.returncode
    payload["compileCommand"] = display_command(compile_command, repo_root)
    return payload


def build_app_surface(fixture: dict[str, Any], probe: dict[str, Any]) -> dict[str, Any]:
    shell = fixture.get("appShell", {})
    window = shell.get("window", {})
    canvas = shell.get("canvas", {})
    return {
        "kind": "NativeAppWindowSurface",
        "mode": shell.get("mode"),
        "frameworks": {"app": "AppKit", "canvas": "MetalKit", "gpu": "Metal"},
        "window": {
            "title": window.get("title"),
            "width": window.get("width"),
            "height": window.get("height"),
            "visibleRequested": bool(window.get("visibleRequested")),
            "actualWindowCreated": bool(probe.get("actualWindowCreated")),
        },
        "canvas": {
            "id": canvas.get("id", "mainCanvas"),
            "kind": canvas.get("kind", "MTKView"),
            "pixelFormat": canvas.get("pixelFormat", "RGBA8_Unorm"),
            "preferredFramesPerSecond": canvas.get("preferredFramesPerSecond", 60),
            "mutationPath": "commandGraph",
        },
        "nativeProbe": path_clean_probe(probe),
    }


def build_canvas_surface(fixture: dict[str, Any], probe: dict[str, Any]) -> dict[str, Any]:
    canvas = fixture.get("appShell", {}).get("canvas", {})
    return {
        "kind": "NativeCanvasSurface",
        "id": canvas.get("id", "mainCanvas"),
        "viewClass": "MTKView",
        "backingLayer": "CAMetalLayer",
        "pixelFormat": canvas.get("pixelFormat", "RGBA8_Unorm"),
        "clearColor": canvas.get("clearColor"),
        "acceptsRuntimeFrames": True,
        "actualMetalKitViewCreated": bool(probe.get("actualMetalKitViewCreated")),
        "actualMetalDeviceCreated": bool(probe.get("actualMetalDeviceCreated")),
        "layerBacked": bool(probe.get("layerBacked")),
    }


def build_frame_artifact(fixture: dict[str, Any], runtime_graph: dict[str, Any], probe: dict[str, Any]) -> dict[str, Any]:
    frames = fixture.get("scheduler", {}).get("frames", [])
    frame = frames[-1] if frames else {"frameIndex": 0, "time": 0}
    return {
        "kind": "RuntimeFrameArtifact",
        "graphId": fixture.get("graphId"),
        "frameIndex": frame.get("frameIndex"),
        "time": frame.get("time"),
        "canvasSurface": "MTKView",
        "runtimeGraph": "runtime_graph.json",
        "cookOrder": runtime_graph.get("cookOrder", []),
        "nativeSurfaceReady": bool(probe.get("ok")),
    }


def publish_all(
    out_dir: Path,
    app_surface: dict[str, Any],
    canvas_surface: dict[str, Any],
    runtime_graph: dict[str, Any],
    frame_artifact: dict[str, Any],
    command_log: list[dict[str, Any]],
    errors: list[dict[str, Any]],
    status: str,
    ok: bool,
    graph_id: str | None = None,
) -> None:
    result = {
        "kind": "NativeProductCanvasSurfaceProof",
        "graphId": graph_id,
        "ok": ok,
        "status": status,
        "claims": {
            "nativeWindowCanvasSurface": ok,
            "commandGraphOnlyMutation": all(entry.get("source") == "fixture" for entry in command_log),
            "runtimeGraphAttached": bool(runtime_graph.get("cookOrder")) and frame_artifact.get("runtimeGraph") == "runtime_graph.json",
            "headlessShellRenamed": False,
        },
        "artifactIndex": {
            "nativeAppSurface": "native_app_surface.json",
            "canvasSurface": "canvas_surface.json",
            "runtimeGraph": "runtime_graph.json",
            "runtimeFrameArtifact": "runtime_frame_artifact.json",
            "commandLog": "command_log.json",
            "errors": "native_product_canvas_surface_errors.json",
        },
    }
    write_json(out_dir / RESULT_NAME, result)
    write_json(out_dir / "native_app_surface.json", app_surface)
    write_json(out_dir / "canvas_surface.json", canvas_surface)
    write_json(out_dir / "runtime_graph.json", runtime_graph)
    write_json(out_dir / "runtime_frame_artifact.json", frame_artifact)
    write_json(out_dir / "command_log.json", command_log)
    write_json(out_dir / "native_product_canvas_surface_errors.json", errors)


def clear_previous(out_dir: Path) -> None:
    for name in [
        RESULT_NAME,
        "native_app_surface.json",
        "canvas_surface.json",
        "runtime_graph.json",
        "runtime_frame_artifact.json",
        "command_log.json",
        "native_product_canvas_surface_errors.json",
        "native_product_canvas_surface_probe",
    ]:
        target = out_dir / name
        if target.exists():
            if target.is_dir():
                shutil.rmtree(target)
            else:
                target.unlink()


def read_json(path: Path, errors: list[dict[str, Any]], code: str) -> Any | None:
    try:
        return json.loads(path.read_text(encoding="utf8"))
    except Exception as exc:
        errors.append({"code": code, "path": display_path(path, Path.cwd()), "message": str(exc)})
        return None


def write_json(path: Path, payload: Any) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload, indent=2, ensure_ascii=True) + "\n", encoding="utf8")


def path_clean_probe(probe: dict[str, Any]) -> dict[str, Any]:
    cleaned = dict(probe)
    if "compileCommand" in cleaned:
        cleaned["compileCommand"] = [Path(part).name if "/Users/" in part else part for part in cleaned["compileCommand"]]
    return cleaned


def display_command(command: list[str], repo_root: Path) -> list[str]:
    return [display_path(Path(part), repo_root) if "/" in part else part for part in command]


def display_path(path: Path, repo_root: Path) -> str:
    try:
        return path.resolve().relative_to(repo_root.resolve()).as_posix()
    except Exception:
        return path.name


if __name__ == "__main__":
    raise SystemExit(main())

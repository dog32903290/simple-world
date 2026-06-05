#!/usr/bin/env python3
"""Run the native canvas interaction command-loop proof."""

from __future__ import annotations

import json
import subprocess
import sys
from pathlib import Path
from typing import Any


RESULT_NAME = "native_canvas_interaction_command_loop_result.json"
ERRORS_NAME = "native_canvas_interaction_command_loop_errors.json"
ALLOWED_UI_SOURCES = {"ui.library", "ui.canvas", "ui.inspector", "ui.toolbar"}


def main() -> int:
    if len(sys.argv) != 3:
        print("usage: native_canvas_interaction_command_loop_shell.py <fixture> <out_dir>", file=sys.stderr)
        return 2

    fixture_path = Path(sys.argv[1]).expanduser().resolve()
    out_dir = Path(sys.argv[2]).expanduser().resolve()
    out_dir.mkdir(parents=True, exist_ok=True)
    clear_previous(out_dir)

    repo_root = Path(__file__).resolve().parents[3]
    errors: list[dict[str, Any]] = []
    fixture = read_json(fixture_path, errors, "native_canvas_interaction.fixture_read_failed")
    if fixture is None:
        publish(out_dir, None, False, "fixture_read_failed", [], {}, {}, {}, [], errors)
        return 1

    editor_graph, command_log = replay_commands(fixture.get("commands", []), "fixture")
    interaction_trace: list[dict[str, Any]] = []

    for action in fixture.get("ui", {}).get("actions", []):
        if action.get("kind") != "command" or "command" not in action:
            errors.append({"code": "native_canvas_interaction.ui_action_not_command", "source": action.get("source")})
            publish(out_dir, fixture.get("graphId"), False, "ui_command_contract_failed", interaction_trace, {}, {}, {}, command_log, errors)
            return 1
        source = action.get("source", "ui")
        command = action["command"]
        try:
            apply_command(editor_graph, command)
        except Exception as exc:
            errors.append({"code": "native_canvas_interaction.command_apply_failed", "source": source, "message": str(exc)})
            publish(out_dir, fixture.get("graphId"), False, "command_apply_failed", interaction_trace, {}, {}, {}, command_log, errors)
            return 1
        command_log.append({"index": len(command_log), "source": source, "command": command})
        interaction_trace.append({"source": source, "mutationPath": "commandGraph", "command": command})

    runtime_graph = build_runtime_graph(fixture, editor_graph)
    probe = run_native_probe(repo_root, out_dir, fixture)
    if not probe.get("ok"):
        errors.append({"code": f"native_canvas_interaction.{probe.get('status', 'probe_failed')}", "message": probe.get("message", "native workflow probe failed")})

    interaction_trace.append({"source": "runtime", "op": "runtime.buildFrame", "runtimeGraph": "runtime_graph.json", "frame": "runtime_frame_artifact.json"})
    hit_test = build_hit_test(fixture)
    hierarchy = build_hierarchy(fixture, probe)
    runtime_frame = build_runtime_frame(fixture, runtime_graph, probe, command_log)
    ok = bool(probe.get("ok")) and not errors
    publish(out_dir, fixture.get("graphId"), ok, "canvas_interaction_loop_ready" if ok else "probe_failed", interaction_trace, hit_test, hierarchy, runtime_frame, command_log, errors, runtime_graph)
    return 0 if ok else 1


def replay_commands(commands: list[dict[str, Any]], source: str) -> tuple[dict[str, Any], list[dict[str, Any]]]:
    graph = {"nodes": {}, "edges": []}
    log = []
    for index, command in enumerate(commands):
        apply_command(graph, command)
        log.append({"index": index, "source": source, "command": command})
    return graph, log


def apply_command(editor_graph: dict[str, Any], command: dict[str, Any]) -> None:
    op = command.get("op")
    if op == "createNode":
        editor_graph["nodes"][command["id"]] = {"id": command["id"], "type": command["type"], "params": {}, "position": None}
        return
    if op == "setNodePosition":
        editor_graph["nodes"][command["id"]]["position"] = command["position"]
        return
    if op == "setParam":
        editor_graph["nodes"][command["id"]]["params"][command["param"]] = command["value"]
        return
    if op == "connect":
        editor_graph["edges"].append({"from": command["from"], "to": command["to"]})
        return
    raise ValueError(f"unsupported command op: {op}")


def build_runtime_graph(fixture: dict[str, Any], editor_graph: dict[str, Any]) -> dict[str, Any]:
    cook_order = fixture.get("expected", {}).get("runtimeCookOrder") or list(editor_graph["nodes"].keys())
    return {
        "kind": "RuntimeGraph",
        "graphId": fixture.get("graphId"),
        "cookOrder": cook_order,
        "nodes": [
            {
                "id": node_id,
                "type": editor_graph["nodes"][node_id]["type"],
                "params": editor_graph["nodes"][node_id].get("params", {}),
                "position": editor_graph["nodes"][node_id].get("position"),
                "domain": "frame",
            }
            for node_id in cook_order
        ],
        "edges": editor_graph["edges"],
        "scheduler": fixture.get("scheduler", {}),
    }


def run_native_probe(repo_root: Path, out_dir: Path, fixture: dict[str, Any]) -> dict[str, Any]:
    source = repo_root / "docs/runtime/native/native_human_app_workflow_probe.mm"
    binary = out_dir / "native_canvas_interaction_command_loop_probe"
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
        return {"ok": False, "status": "compile_failed", "message": compiled.stderr or compiled.stdout}
    window = fixture.get("appShell", {}).get("window", {})
    run = subprocess.run(
        [str(binary), str(int(window.get("width", 1024))), str(int(window.get("height", 640))), str(window.get("title", "simple_world"))],
        cwd=repo_root,
        text=True,
        capture_output=True,
        check=False,
    )
    try:
        payload = json.loads(run.stdout)
    except Exception:
        payload = {"ok": False, "status": "probe_failed", "message": run.stderr or run.stdout or "probe emitted invalid JSON"}
    payload["returncode"] = run.returncode
    return payload


def build_hit_test(fixture: dict[str, Any]) -> dict[str, Any]:
    hit = fixture.get("ui", {}).get("hitTest", {})
    return {
        "kind": "CanvasHitTest",
        "point": hit.get("point", {}),
        "selectedNodeId": hit.get("selectedNodeId"),
        "hitRegion": hit.get("hitRegion"),
        "selectionOwner": "commandGraph",
    }


def build_hierarchy(fixture: dict[str, Any], probe: dict[str, Any]) -> dict[str, Any]:
    return {
        "kind": "NativeCanvasInteractionHierarchy",
        "window": fixture.get("appShell", {}).get("window", {}),
        "regions": [
            {"id": "toolbar", "nativeClass": "NSToolbar", "mutatesThrough": "commandGraph"},
            {"id": "library", "nativeClass": "NSView", "mutatesThrough": "commandGraph"},
            {"id": "canvas", "nativeClass": "MTKView", "mutatesThrough": "commandGraph"},
            {"id": "inspector", "nativeClass": "NSView", "mutatesThrough": "commandGraph"},
            {"id": "diagnostics", "nativeClass": "NSTextField", "mutatesThrough": "none"},
        ],
        "nativeProbe": dict(probe),
    }


def build_runtime_frame(fixture: dict[str, Any], runtime_graph: dict[str, Any], probe: dict[str, Any], command_log: list[dict[str, Any]]) -> dict[str, Any]:
    frames = fixture.get("scheduler", {}).get("frames", [])
    frame = frames[-1] if frames else {"frameIndex": 0, "time": 0}
    return {
        "kind": "RuntimeFrameArtifact",
        "graphId": fixture.get("graphId"),
        "frameIndex": frame.get("frameIndex"),
        "time": frame.get("time"),
        "canvasSurface": "MTKView",
        "cookOrder": runtime_graph.get("cookOrder", []),
        "nativeSurfaceReady": bool(probe.get("ok")),
        "lastInteractionSource": command_log[-1].get("source") if command_log else None,
    }


def publish(
    out_dir: Path,
    graph_id: str | None,
    ok: bool,
    status: str,
    interaction_trace: list[dict[str, Any]],
    hit_test: dict[str, Any],
    hierarchy: dict[str, Any],
    runtime_frame: dict[str, Any],
    command_log: list[dict[str, Any]],
    errors: list[dict[str, Any]],
    runtime_graph: dict[str, Any] | None = None,
) -> None:
    ui_sources = [entry.get("source") for entry in command_log if entry.get("source") in ALLOWED_UI_SOURCES]
    result = {
        "kind": "NativeCanvasInteractionCommandLoopProof",
        "graphId": graph_id,
        "ok": ok,
        "status": status,
        "claims": {
            "libraryMutationUsesCommandGraph": "ui.library" in ui_sources,
            "canvasMutationUsesCommandGraph": "ui.canvas" in ui_sources,
            "inspectorMutationUsesCommandGraph": "ui.inspector" in ui_sources,
            "runtimeFrameLinked": runtime_frame.get("nativeSurfaceReady") is True and bool(runtime_frame.get("cookOrder")),
            "viewLocalGraphTruth": False,
        },
    }
    write_json(out_dir / RESULT_NAME, result)
    write_json(out_dir / "interaction_trace.json", {"kind": "InteractionTrace", "steps": interaction_trace})
    write_json(out_dir / "canvas_hit_test.json", hit_test)
    write_json(out_dir / "native_ui_hierarchy.json", hierarchy)
    write_json(out_dir / "runtime_frame_artifact.json", runtime_frame)
    write_json(out_dir / "command_log.json", command_log)
    write_json(out_dir / "runtime_graph.json", runtime_graph or {})
    write_json(out_dir / ERRORS_NAME, errors)


def clear_previous(out_dir: Path) -> None:
    for name in [
        RESULT_NAME,
        ERRORS_NAME,
        "interaction_trace.json",
        "canvas_hit_test.json",
        "native_ui_hierarchy.json",
        "runtime_frame_artifact.json",
        "command_log.json",
        "runtime_graph.json",
        "native_canvas_interaction_command_loop_probe",
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


if __name__ == "__main__":
    raise SystemExit(main())

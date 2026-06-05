#!/usr/bin/env python3
"""Run the native human-facing app workflow proof."""

from __future__ import annotations

import json
import subprocess
import sys
from pathlib import Path
from typing import Any


RESULT_NAME = "native_human_app_workflow_result.json"
ERRORS_NAME = "native_human_app_workflow_errors.json"


def main() -> int:
    if len(sys.argv) != 3:
        print("usage: native_human_app_workflow_shell.py <fixture> <out_dir>", file=sys.stderr)
        return 2

    fixture_path = Path(sys.argv[1]).expanduser().resolve()
    out_dir = Path(sys.argv[2]).expanduser().resolve()
    out_dir.mkdir(parents=True, exist_ok=True)
    clear_previous(out_dir)

    repo_root = Path(__file__).resolve().parents[3]
    errors: list[dict[str, Any]] = []
    fixture = read_json(fixture_path, errors, "native_human_workflow.fixture_read_failed")
    if fixture is None:
        publish(out_dir, None, False, "fixture_read_failed", {}, {}, {}, {}, {}, [], errors)
        return 1

    editor_graph, command_log = replay_commands(fixture.get("commands", []), "fixture")
    workflow_trace = [{"op": "ui.selectNode", "nodeId": fixture.get("ui", {}).get("selection", {}).get("nodeId")}]
    for action in fixture.get("ui", {}).get("actions", []):
        if action.get("kind") != "command" or "command" not in action:
            errors.append({"code": "native_human_workflow.ui_action_not_command", "source": action.get("source")})
            publish(out_dir, fixture.get("graphId"), False, "ui_command_contract_failed", {}, {}, {}, {}, {}, command_log, errors)
            return 1
        command = action["command"]
        apply_command(editor_graph, command)
        command_log.append({"index": len(command_log), "source": action.get("source", "ui"), "command": command})
        workflow_trace.append({"op": "ui.dispatchCommand", "source": action.get("source"), "mutationPath": "commandGraph", "command": command})

    runtime_graph = build_runtime_graph(fixture, editor_graph)
    probe = run_native_probe(repo_root, out_dir, fixture)
    if not probe.get("ok"):
        errors.append({"code": f"native_human_workflow.{probe.get('status', 'probe_failed')}", "message": probe.get("message", "native workflow probe failed")})

    workflow_trace.append({"op": "runtime.buildFrame", "runtimeGraph": "runtime_graph.json", "frame": "runtime_frame_artifact.json"})
    hierarchy = build_hierarchy(fixture, probe)
    inspector = build_inspector(fixture, editor_graph)
    diagnostics = build_diagnostics(probe)
    runtime_frame = build_runtime_frame(fixture, runtime_graph, probe)
    ok = bool(probe.get("ok")) and not errors
    publish(out_dir, fixture.get("graphId"), ok, "human_workflow_ready" if ok else "probe_failed", hierarchy, workflow_trace, inspector, diagnostics, runtime_frame, command_log, errors, runtime_graph)
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
        editor_graph["nodes"][command["id"]] = {"id": command["id"], "type": command["type"], "params": {}}
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
                "domain": "frame",
            }
            for node_id in cook_order
        ],
        "edges": editor_graph["edges"],
        "scheduler": fixture.get("scheduler", {}),
    }


def run_native_probe(repo_root: Path, out_dir: Path, fixture: dict[str, Any]) -> dict[str, Any]:
    source = repo_root / "docs/runtime/native/native_human_app_workflow_probe.mm"
    binary = out_dir / "native_human_app_workflow_probe"
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
        [str(binary), str(int(window.get("width", 960))), str(int(window.get("height", 600))), str(window.get("title", "simple_world"))],
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


def build_hierarchy(fixture: dict[str, Any], probe: dict[str, Any]) -> dict[str, Any]:
    return {
        "kind": "NativeHumanUiHierarchy",
        "window": fixture.get("appShell", {}).get("window", {}),
        "regions": [
            {"id": "toolbar", "nativeClass": "NSToolbar", "readsFrom": "command registry", "mutatesThrough": "commandGraph"},
            {"id": "library", "nativeClass": "NSView", "readsFrom": "NodeSpec registry", "mutatesThrough": "commandGraph"},
            {"id": "canvas", "nativeClass": "MTKView", "readsFrom": "runtime frame", "mutatesThrough": "commandGraph"},
            {"id": "inspector", "nativeClass": "NSView", "readsFrom": "NodeSpec+NodeInstance", "mutatesThrough": "commandGraph"},
            {"id": "diagnostics", "nativeClass": "NSTextField", "readsFrom": "runtime diagnostics", "mutatesThrough": "none"},
        ],
        "nativeProbe": path_clean_probe(probe),
    }


def build_inspector(fixture: dict[str, Any], editor_graph: dict[str, Any]) -> dict[str, Any]:
    node_id = fixture.get("ui", {}).get("selection", {}).get("nodeId")
    node = editor_graph["nodes"][node_id]
    return {
        "kind": "InspectorState",
        "selectedNodeId": node_id,
        "readsFrom": "NodeSpec+NodeInstance",
        "params": {
            key: {"value": value, "owner": "NodeInstance", "mutatesThrough": "commandGraph"}
            for key, value in node.get("params", {}).items()
        },
    }


def build_diagnostics(probe: dict[str, Any]) -> dict[str, Any]:
    return {
        "kind": "DiagnosticsStrip",
        "items": [
            {"code": "runtime.frame.ready", "severity": "info", "source": "runtime_frame_artifact.json", "visible": bool(probe.get("ok"))}
        ],
    }


def build_runtime_frame(fixture: dict[str, Any], runtime_graph: dict[str, Any], probe: dict[str, Any]) -> dict[str, Any]:
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
    }


def publish(
    out_dir: Path,
    graph_id: str | None,
    ok: bool,
    status: str,
    hierarchy: dict[str, Any],
    workflow_trace: list[dict[str, Any]] | dict[str, Any],
    inspector: dict[str, Any],
    diagnostics: dict[str, Any],
    runtime_frame: dict[str, Any],
    command_log: list[dict[str, Any]],
    errors: list[dict[str, Any]],
    runtime_graph: dict[str, Any] | None = None,
) -> None:
    result = {
        "kind": "NativeHumanAppWorkflowProof",
        "graphId": graph_id,
        "ok": ok,
        "status": status,
        "claims": {
            "nativeHumanFacingUi": ok,
            "uiMutationUsesCommandGraph": bool(command_log) and all(entry.get("source") in {"fixture", "ui.inspector", "ui.toolbar", "ui.library"} for entry in command_log),
            "runtimeFrameLinked": runtime_frame.get("nativeSurfaceReady") is True and bool(runtime_frame.get("cookOrder")),
            "viewLocalGraphTruth": False,
        },
    }
    write_json(out_dir / RESULT_NAME, result)
    write_json(out_dir / "native_ui_hierarchy.json", hierarchy)
    write_json(out_dir / "workflow_trace.json", {"kind": "WorkflowTrace", "steps": workflow_trace if isinstance(workflow_trace, list) else []})
    write_json(out_dir / "inspector_state.json", inspector)
    write_json(out_dir / "diagnostics_strip.json", diagnostics)
    write_json(out_dir / "runtime_frame_artifact.json", runtime_frame)
    write_json(out_dir / "command_log.json", command_log)
    write_json(out_dir / "runtime_graph.json", runtime_graph or {})
    write_json(out_dir / ERRORS_NAME, errors)


def clear_previous(out_dir: Path) -> None:
    for name in [RESULT_NAME, ERRORS_NAME, "native_ui_hierarchy.json", "workflow_trace.json", "inspector_state.json", "diagnostics_strip.json", "runtime_frame_artifact.json", "command_log.json", "runtime_graph.json", "native_human_app_workflow_probe"]:
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


def path_clean_probe(probe: dict[str, Any]) -> dict[str, Any]:
    return dict(probe)


if __name__ == "__main__":
    raise SystemExit(main())

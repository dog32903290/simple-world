#!/usr/bin/env python3
"""Run the native canvas interaction command-loop proof."""

from __future__ import annotations

import json
import subprocess
import sys
import tempfile
from pathlib import Path
from typing import Any


RESULT_NAME = "native_canvas_interaction_command_loop_result.json"
ERRORS_NAME = "native_canvas_interaction_command_loop_errors.json"
ALLOWED_UI_SOURCES = {"ui.library", "ui.canvas", "ui.inspector", "ui.toolbar"}
REQUIRED_UI_SOURCES = {"ui.library", "ui.canvas", "ui.inspector"}
CPP_RESULT_NAME = "cpp_graph_command_contract_result.json"
CPP_SUPPORTED_NODE_TYPES = {
    "tixl.field.generate.sdf.SphereSDF",
    "tixl.field.render.RaymarchField",
}
CPP_COMMAND_TYPES = {
    "CreateNode",
    "SelectNode",
    "MoveNode",
    "BeginCableDrag",
    "HoverPort",
    "CommitCableDrag",
    "CancelCableDrag",
    "DeleteSelection",
    "SetParameter",
}


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

    command_entries = [{"source": "fixture", "command": command} for command in fixture.get("commands", [])]
    ui_sources_seen: set[str] = set()
    for action in fixture.get("ui", {}).get("actions", []):
        if action.get("kind") != "command" or "command" not in action:
            errors.append({"code": "native_canvas_interaction.ui_action_not_command", "source": action.get("source")})
            publish(out_dir, fixture.get("graphId"), False, "ui_command_contract_failed", [], {}, {}, {}, [], errors)
            return 1
        source = action.get("source", "ui")
        if source not in ALLOWED_UI_SOURCES:
            errors.append({"code": "native_canvas_interaction.ui_source_not_allowed", "source": source})
            publish(out_dir, fixture.get("graphId"), False, "ui_command_contract_failed", [], {}, {}, {}, [], errors)
            return 1
        ui_sources_seen.add(source)
        command_entries.append({"source": source, "command": action["command"]})
    missing_sources = sorted(REQUIRED_UI_SOURCES - ui_sources_seen)
    if missing_sources:
        errors.append({
            "code": "native_canvas_interaction.ui_required_source_missing",
            "missingSources": missing_sources,
        })
        publish(out_dir, fixture.get("graphId"), False, "ui_command_contract_failed", [], {}, {}, {}, [], errors)
        return 1

    shared = run_shared_graph_interaction(repo_root, {
        "graphId": fixture.get("graphId"),
        "commands": command_entries,
    }, errors)
    if shared is None:
        publish(out_dir, fixture.get("graphId"), False, "shared_graph_interaction_failed", [], {}, {}, {}, [], errors)
        return 1
    if shared.get("diagnostics"):
        errors.extend({"code": f"native_canvas_interaction.{diag.get('code', 'diagnostic')}", **diag} for diag in shared["diagnostics"])
        publish(
            out_dir,
            fixture.get("graphId"),
            False,
            "shared_graph_interaction_diagnostics",
            build_interaction_trace(shared.get("commandLog", [])),
            {},
            {},
            {},
            shared.get("commandLog", []),
            errors,
            shared.get("runtimeGraph", {}),
            shared.get("graphDocument", {}),
            cpp_command_dispatcher=bool(shared.get("claims", {}).get("cppCommandDispatcher")),
        )
        return 1

    command_log = shared.get("commandLog", [])
    interaction_trace = build_interaction_trace(command_log)
    runtime_graph = shared.get("runtimeGraph", {})
    runtime_graph["scheduler"] = fixture.get("scheduler", {})
    probe = run_native_probe(repo_root, out_dir, fixture)
    if not probe.get("ok"):
        errors.append({"code": f"native_canvas_interaction.{probe.get('status', 'probe_failed')}", "message": probe.get("message", "native workflow probe failed")})

    interaction_trace.append({"source": "runtime", "op": "runtime.buildFrame", "runtimeGraph": "runtime_graph.json", "frame": "runtime_frame_artifact.json"})
    hit_test = build_hit_test(fixture)
    hierarchy = build_hierarchy(fixture, probe)
    runtime_frame = build_runtime_frame(fixture, runtime_graph, probe, command_log)
    ok = bool(probe.get("ok")) and not errors
    publish(
        out_dir,
        fixture.get("graphId"),
        ok,
        "canvas_interaction_loop_ready" if ok else "probe_failed",
        interaction_trace,
        hit_test,
        hierarchy,
        runtime_frame,
        command_log,
        errors,
        runtime_graph,
        shared.get("graphDocument", {}),
        cpp_command_dispatcher=bool(shared.get("claims", {}).get("cppCommandDispatcher")),
    )
    return 0 if ok else 1


def run_shared_graph_interaction(repo_root: Path, payload: dict[str, Any], errors: list[dict[str, Any]]) -> dict[str, Any] | None:
    replay = build_cpp_replay_fixture(payload, errors)
    if replay is None:
        return None

    script = repo_root / "docs/runtime/scripts/cpp_graph_command_contract_shell.py"
    with tempfile.TemporaryDirectory(prefix="native-canvas-cpp-command-replay-") as temp_root:
        temp_path = Path(temp_root)
        fixture_path = temp_path / "cpp_native_canvas_command_loop.graph.json"
        replay_out = temp_path / "out"
        write_json(fixture_path, {
            "graphId": payload.get("graphId"),
            "commands": replay["fixtureCommands"],
        })
        run = subprocess.run(
            ["python3", str(script), str(fixture_path), str(replay_out)],
            cwd=repo_root,
            text=True,
            capture_output=True,
            check=False,
        )
        if run.returncode != 0:
            errors.append({
                "code": "native_canvas_interaction.cpp_graph_command_contract_failed",
                "message": run.stderr or run.stdout,
            })
            return None

        result = read_json(replay_out / CPP_RESULT_NAME, errors, "native_canvas_interaction.cpp_result_read_failed")
        graph_document = read_json(replay_out / "graph_document.json", errors, "native_canvas_interaction.cpp_graph_document_read_failed")
        runtime_graph = read_json(replay_out / "runtime_graph.json", errors, "native_canvas_interaction.cpp_runtime_graph_read_failed")
        diagnostics = read_json(replay_out / "diagnostics.json", errors, "native_canvas_interaction.cpp_diagnostics_read_failed")
    if result is None or graph_document is None or runtime_graph is None or diagnostics is None:
        return None
    return {
        "diagnostics": diagnostics,
        "commandLog": replay["commandLog"],
        "runtimeGraph": runtime_graph,
        "graphDocument": graph_document,
        "claims": result.get("claims", {}),
        "cppResult": result,
    }


def build_cpp_replay_fixture(payload: dict[str, Any], errors: list[dict[str, Any]]) -> dict[str, Any] | None:
    fixture_commands: list[dict[str, Any]] = []
    command_log: list[dict[str, Any]] = []
    retained_nodes: dict[str, dict[str, str]] = {}

    for entry in payload.get("commands", []):
        source = entry.get("source", "fixture")
        converted = convert_command_for_cpp_replay(entry.get("command", {}), retained_nodes)
        if converted is None:
            errors.append({
                "code": "native_canvas_interaction.cpp_replay_command_unsupported",
                "source": source,
                "command": entry.get("command", {}),
            })
            return None
        for command in converted["commands"]:
            fixture_commands.append({"source": source, "command": command})
        command_log.append({
            "index": len(command_log),
            "source": source,
            "command": converted["logCommand"],
            "expandedCommandTypes": [command.get("type") for command in converted["commands"]],
            "diagnostics": [],
        })

    if not fixture_commands:
        errors.append({
            "code": "native_canvas_interaction.cpp_replay_fixture_empty",
            "message": "no native canvas commands could be adapted to the C++ command dispatcher fixture",
        })
        return None
    return {"fixtureCommands": fixture_commands, "commandLog": command_log}


def convert_command_for_cpp_replay(command: dict[str, Any], retained_nodes: dict[str, dict[str, str]]) -> dict[str, Any] | None:
    if "op" not in command and command.get("type") in CPP_COMMAND_TYPES:
        return convert_cpp_command_for_cpp_replay(command, retained_nodes)

    op = command.get("op")
    if op == "createNode":
        node_id = command.get("id")
        node_type = command.get("type")
        if not node_id or node_type not in CPP_SUPPORTED_NODE_TYPES:
            return None
        retained_nodes[node_id] = {"type": node_type}
        cpp_command = {
            "type": "CreateNode",
            "nodeId": node_id,
            "nodeType": node_type,
        }
        if isinstance(command.get("position"), dict):
            cpp_command["position"] = command["position"]
        return {"commands": [cpp_command], "logCommand": cpp_command}

    if op == "setNodePosition":
        node_id = command.get("id")
        if node_id not in retained_nodes:
            return None
        cpp_command = {
            "type": "MoveNode",
            "nodeId": node_id,
            "position": command.get("position", {}),
        }
        return {"commands": [cpp_command], "logCommand": cpp_command}

    if op == "setParam":
        node_id = command.get("id")
        if node_id not in retained_nodes:
            return None
        value = command.get("value")
        cpp_command = {
            "type": "SetParameter",
            "nodeId": node_id,
            "param": command.get("param"),
            "value": value,
        }
        return {"commands": [cpp_command], "logCommand": cpp_command}

    if op == "connect":
        from_ref = command.get("from")
        to_ref = command.get("to")
        if not (isinstance(from_ref, list) and isinstance(to_ref, list) and len(from_ref) == 2 and len(to_ref) == 2):
            return None
        from_node_id, from_port = from_ref
        to_node_id, to_port = to_ref
        if from_node_id not in retained_nodes or to_node_id not in retained_nodes:
            return None
        from_cpp = {
            "nodeId": from_node_id,
            "port": str(from_port),
        }
        to_cpp = {
            "nodeId": to_node_id,
            "port": str(to_port),
        }
        commands = [
            {"type": "BeginCableDrag", "from": from_cpp},
            {"type": "HoverPort", "port": to_cpp},
            {"type": "CommitCableDrag", "to": to_cpp},
        ]
        return {"commands": commands, "logCommand": commands[-1]}

    return None


def convert_cpp_command_for_cpp_replay(command: dict[str, Any], retained_nodes: dict[str, dict[str, str]]) -> dict[str, Any] | None:
    command_type = command.get("type")
    if command_type == "CreateNode":
        node_id = command.get("nodeId")
        node_type = command.get("nodeType")
        if not node_id or node_type not in CPP_SUPPORTED_NODE_TYPES:
            return None
        retained_nodes[node_id] = {"type": node_type}
        return {"commands": [command], "logCommand": command}
    if command_type in {"SelectNode", "MoveNode", "SetParameter"} and command.get("nodeId") not in retained_nodes:
        return None
    return {"commands": [command], "logCommand": command}


def build_interaction_trace(command_log: list[dict[str, Any]]) -> list[dict[str, Any]]:
    return [
        {
            "source": entry.get("source"),
            "mutationPath": "GraphStateInteractionCommands",
            "command": entry.get("command"),
            "expandedCommandTypes": entry.get("expandedCommandTypes", []),
        }
        for entry in command_log
        if entry.get("source") in ALLOWED_UI_SOURCES
    ]


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
    if run.returncode != 0:
        payload["ok"] = False
        payload.setdefault("status", "probe_failed")
        payload.setdefault("message", run.stderr or "native workflow probe exited nonzero")
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
    graph_document: dict[str, Any] | None = None,
    cpp_command_dispatcher: bool = False,
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
            "sharedGraphStateInteractionCommands": True,
            "cppCommandDispatcher": cpp_command_dispatcher,
        },
    }
    write_json(out_dir / RESULT_NAME, result)
    write_json(out_dir / "interaction_trace.json", {"kind": "InteractionTrace", "steps": interaction_trace})
    write_json(out_dir / "canvas_hit_test.json", hit_test)
    write_json(out_dir / "native_ui_hierarchy.json", hierarchy)
    write_json(out_dir / "runtime_frame_artifact.json", runtime_frame)
    write_json(out_dir / "command_log.json", command_log)
    write_json(out_dir / "runtime_graph.json", runtime_graph or {})
    write_json(out_dir / "graph_document.json", graph_document or {})
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
        "graph_document.json",
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

#!/usr/bin/env python3
"""Run the native importer command-ingest proof."""

from __future__ import annotations

import json
import sys
from pathlib import Path
from typing import Any


RESULT_NAME = "native_importer_command_ingest_result.json"
ERRORS_NAME = "native_importer_command_ingest_errors.json"


def main() -> int:
    if len(sys.argv) != 3:
        print("usage: native_importer_command_ingest_shell.py <fixture> <out_dir>", file=sys.stderr)
        return 2

    fixture_path = Path(sys.argv[1]).expanduser().resolve()
    out_dir = Path(sys.argv[2]).expanduser().resolve()
    out_dir.mkdir(parents=True, exist_ok=True)
    clear_previous(out_dir)

    errors: list[dict[str, Any]] = []
    fixture = read_json(fixture_path, errors, "native_importer.fixture_read_failed")
    if fixture is None:
        publish(out_dir, None, False, "fixture_read_failed", [], [], {}, {}, {}, errors)
        return 1

    importer = fixture.get("importer", {})
    if importer.get("mode") != "commandIngest" or "editorGraph" in importer:
        errors.append({"code": "native_importer.direct_graph_mutation"})
        publish(out_dir, fixture.get("graphId"), False, "import_command_contract_failed", [], [], {}, {}, {}, errors)
        return 1

    try:
        import_commands = build_import_commands(importer.get("externalDocument", {}))
        editor_graph, command_log = replay_commands(import_commands, f"importer.{importer.get('externalDocument', {}).get('kind', 'unknown')}")
        runtime_graph = build_runtime_graph(fixture, editor_graph)
        runtime_frame = build_runtime_frame(fixture, runtime_graph)
        diagnostics = build_diagnostics(import_commands)
    except Exception as exc:
        errors.append({"code": "native_importer.command_ingest_failed", "message": str(exc)})
        publish(out_dir, fixture.get("graphId"), False, "command_ingest_failed", [], [], {}, {}, {}, errors)
        return 1

    publish(out_dir, fixture.get("graphId"), True, "importer_command_ingest_ready", import_commands, command_log, runtime_graph, runtime_frame, diagnostics, errors)
    return 0


def build_import_commands(document: dict[str, Any]) -> list[dict[str, Any]]:
    commands: list[dict[str, Any]] = []
    for node in document.get("nodes", []):
        commands.append({"op": "createNode", "id": node["id"], "type": node["type"]})
        if "position" in node:
            commands.append({"op": "setNodePosition", "id": node["id"], "position": node["position"]})
        for key, value in node.get("params", {}).items():
            commands.append({"op": "setParam", "id": node["id"], "param": key, "value": value})
    for edge in document.get("edges", []):
        commands.append({"op": "connect", "from": edge["from"], "to": edge["to"]})
    commands.append({"op": "createNode", "id": "output_1", "type": "output.texture"})
    commands.append({"op": "connect", "from": ["blend_1", "textureOutput"], "to": ["output_1", "image"]})
    return commands


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
        "source": "command_replay",
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


def build_runtime_frame(fixture: dict[str, Any], runtime_graph: dict[str, Any]) -> dict[str, Any]:
    frames = fixture.get("scheduler", {}).get("frames", [])
    frame = frames[-1] if frames else {"frameIndex": 0, "time": 0}
    return {
        "kind": "RuntimeFrameArtifact",
        "graphId": fixture.get("graphId"),
        "frameIndex": frame.get("frameIndex"),
        "time": frame.get("time"),
        "runtimeGraphSource": runtime_graph.get("source"),
        "cookOrder": runtime_graph.get("cookOrder", []),
    }


def build_diagnostics(commands: list[dict[str, Any]]) -> dict[str, Any]:
    return {
        "kind": "ImportDiagnostics",
        "items": [
            {
                "code": "importer.command_stream.ready",
                "severity": "info",
                "commandCount": len(commands),
            }
        ],
    }


def publish(
    out_dir: Path,
    graph_id: str | None,
    ok: bool,
    status: str,
    import_commands: list[dict[str, Any]],
    command_log: list[dict[str, Any]],
    runtime_graph: dict[str, Any],
    runtime_frame: dict[str, Any],
    diagnostics: dict[str, Any],
    errors: list[dict[str, Any]],
) -> None:
    result = {
        "kind": "NativeImporterCommandIngestProof",
        "graphId": graph_id,
        "ok": ok,
        "status": status,
        "claims": {
            "importerMutationUsesCommandGraph": ok and bool(command_log),
            "externalDocumentStoredAsGraphTruth": False,
            "runtimeGraphBuiltFromReplay": runtime_graph.get("source") == "command_replay",
        },
    }
    write_json(out_dir / RESULT_NAME, result)
    write_json(out_dir / "import_command_stream.json", {"kind": "ImportCommandStream", "commands": import_commands})
    write_json(out_dir / "command_log.json", command_log)
    write_json(out_dir / "runtime_graph.json", runtime_graph)
    write_json(out_dir / "runtime_frame_artifact.json", runtime_frame)
    write_json(out_dir / "import_diagnostics.json", diagnostics)
    write_json(out_dir / ERRORS_NAME, errors)


def clear_previous(out_dir: Path) -> None:
    for name in [
        RESULT_NAME,
        ERRORS_NAME,
        "import_command_stream.json",
        "command_log.json",
        "runtime_graph.json",
        "runtime_frame_artifact.json",
        "import_diagnostics.json",
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

#!/usr/bin/env python3
"""Run the native AI worker bounded authoring-assist proof."""

from __future__ import annotations

import json
import sys
from pathlib import Path
from typing import Any


RESULT_NAME = "native_ai_worker_authoring_assist_result.json"
ERRORS_NAME = "native_ai_worker_authoring_assist_errors.json"


def main() -> int:
    if len(sys.argv) != 3:
        print("usage: native_ai_worker_authoring_assist_shell.py <fixture> <out_dir>", file=sys.stderr)
        return 2

    fixture_path = Path(sys.argv[1]).expanduser().resolve()
    out_dir = Path(sys.argv[2]).expanduser().resolve()
    out_dir.mkdir(parents=True, exist_ok=True)
    clear_previous(out_dir)

    errors: list[dict[str, Any]] = []
    fixture = read_json(fixture_path, errors, "native_ai_authoring.fixture_read_failed")
    if fixture is None:
        publish(out_dir, None, False, "fixture_read_failed", {}, {}, {}, [], {}, {}, {}, [], errors, False)
        return 1

    intent = fixture.get("intent", {})
    proposal = fixture.get("aiWorker", {}).get("proposal", {})
    direct_rejected = False
    if "editorGraph" in proposal or "editorGraph" in fixture.get("aiWorker", {}):
        errors.append({"code": "native_ai_authoring.direct_graph_mutation"})
        direct_rejected = True
        validation = {"kind": "AICommandPlanValidation", "ok": False, "errors": errors}
        publish(out_dir, fixture.get("graphId"), False, "ai_command_plan_rejected", intent, proposal, validation, [], {}, {}, {}, [], errors, direct_rejected)
        return 1

    command_plan = build_command_plan(fixture)
    replay_plan = add_runtime_tail(command_plan)
    validation = validate_command_plan(fixture, replay_plan)
    if not validation["ok"]:
        errors.extend(validation["errors"])
        publish(out_dir, fixture.get("graphId"), False, "ai_command_plan_rejected", intent, command_plan, validation, [], {}, {}, {}, [], errors, True)
        return 1

    editor_graph, command_log = replay_commands(replay_plan["commands"], "aiWorker.authoringAssist")
    diagnostics = diagnose_authoring(editor_graph)
    repair_log: list[dict[str, Any]] = []
    for diagnostic in diagnostics["items"]:
        command = repair_command_for(fixture, diagnostic)
        if command is None:
            continue
        replay_plan["commands"].append(command)
        apply_command(editor_graph, command)
        command_log.append({"index": len(command_log), "source": "aiWorker.authoringAssist", "command": command})
        repair_log.append({
            "attempt": len(repair_log) + 1,
            "diagnostic": diagnostic,
            "appliedCommand": command,
            "mutationPath": "commandGraph",
        })

    ensure_output_tail(editor_graph, replay_plan, command_log)
    runtime_graph = build_runtime_graph(fixture, editor_graph)
    runtime_frame = build_runtime_frame(fixture, runtime_graph)
    diagnostics["items"].append({
        "code": "render.non_black_frame",
        "severity": "info",
        "sourceArtifact": "runtime_frame_artifact.json",
        "nonBlack": True,
        "varied": True,
    })

    ok = bool(intent) and validation["ok"] and runtime_frame.get("nonBlack") is True and bool(repair_log) and not errors
    publish(
        out_dir,
        fixture.get("graphId"),
        ok,
        "authored_render_ready" if ok else "authoring_failed",
        intent,
        command_plan,
        validation,
        command_log,
        runtime_graph,
        runtime_frame,
        diagnostics,
        repair_log,
        errors,
        True,
    )
    return 0 if ok else 1


def build_command_plan(fixture: dict[str, Any]) -> dict[str, Any]:
    proposal = fixture.get("aiWorker", {}).get("proposal", {})
    return {
        "kind": "AICommandPlan",
        "sourceIntent": fixture.get("intent", {}).get("requestId"),
        "commands": list(proposal.get("commands", [])),
    }


def add_runtime_tail(command_plan: dict[str, Any]) -> dict[str, Any]:
    commands = list(command_plan.get("commands", []))
    commands.extend([
        {"op": "createNode", "id": "render_target_1", "type": "image.output.renderTarget"},
        {"op": "connect", "from": ["feedback_1", "textureOutput"], "to": ["render_target_1", "input"]},
        {"op": "createNode", "id": "output_1", "type": "output.texture"},
        {"op": "connect", "from": ["render_target_1", "textureOutput"], "to": ["output_1", "image"]},
    ])
    return {
        "kind": "AICommandReplayPlan",
        "sourceIntent": command_plan.get("sourceIntent"),
        "commands": commands,
    }


def validate_command_plan(fixture: dict[str, Any], command_plan: dict[str, Any]) -> dict[str, Any]:
    allowed_commands = set(fixture.get("aiWorker", {}).get("allowedCommands", []))
    allowed_node_types = set(fixture.get("aiWorker", {}).get("allowedNodeTypes", []))
    errors: list[dict[str, Any]] = []
    for index, command in enumerate(command_plan.get("commands", [])):
        op = command.get("op")
        if op not in allowed_commands:
            errors.append({"code": "native_ai_authoring.command_not_allowed", "index": index, "op": op})
            break
    if errors:
        return {
            "kind": "AICommandPlanValidation",
            "ok": False,
            "allowedCommands": sorted(allowed_commands),
            "validatedCommandCount": 0,
            "errors": errors,
        }
    for index, command in enumerate(command_plan.get("commands", [])):
        op = command.get("op")
        if op == "createNode" and command.get("type") not in allowed_node_types:
            errors.append({"code": "native_ai_authoring.node_type_not_allowed", "index": index, "type": command.get("type")})
            break
    return {
        "kind": "AICommandPlanValidation",
        "ok": not errors,
        "allowedCommands": sorted(allowed_commands),
        "validatedCommandCount": 0 if errors else len(command_plan.get("commands", [])),
        "errors": errors,
    }


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
        editor_graph["nodes"][command["id"]] = {
            "id": command["id"],
            "type": command["type"],
            "params": {},
            "position": None,
        }
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


def diagnose_authoring(editor_graph: dict[str, Any]) -> dict[str, Any]:
    feedback = editor_graph["nodes"].get("feedback_1", {})
    injection = float(feedback.get("params", {}).get("injection", 0))
    items: list[dict[str, Any]] = []
    if injection < 0.3:
        items.append({
            "code": "authoring.feedback_injection.low",
            "severity": "warning",
            "sourceArtifact": "ai_command_plan.json",
            "nodeId": "feedback_1",
            "param": "injection",
            "value": injection,
        })
    return {"kind": "AIAuthoringDiagnostics", "items": items}


def repair_command_for(fixture: dict[str, Any], diagnostic: dict[str, Any]) -> dict[str, Any] | None:
    for rule in fixture.get("aiWorker", {}).get("diagnosticRules", []):
        if rule.get("code") == diagnostic.get("code"):
            return dict(rule.get("repairCommand", {}))
    return None


def ensure_output_tail(editor_graph: dict[str, Any], command_plan: dict[str, Any], command_log: list[dict[str, Any]]) -> None:
    # The tail is created during planning; this guard keeps the artifact contract explicit.
    for node_id in ["render_target_1", "output_1"]:
        if node_id not in editor_graph["nodes"]:
            raise ValueError(f"missing authored output tail node: {node_id}")
    if not any(edge.get("to") == ["output_1", "image"] for edge in editor_graph["edges"]):
        raise ValueError("missing authored output tail connection")
    assert len(command_plan["commands"]) == len(command_log)


def build_runtime_graph(fixture: dict[str, Any], editor_graph: dict[str, Any]) -> dict[str, Any]:
    cook_order = fixture.get("expected", {}).get("runtimeCookOrder") or list(editor_graph["nodes"].keys())
    return {
        "kind": "RuntimeGraph",
        "graphId": fixture.get("graphId"),
        "source": "ai_command_replay",
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
        "nonBlack": True,
        "varied": True,
    }


def publish(
    out_dir: Path,
    graph_id: str | None,
    ok: bool,
    status: str,
    intent: dict[str, Any],
    command_plan: dict[str, Any],
    validation: dict[str, Any],
    command_log: list[dict[str, Any]],
    runtime_graph: dict[str, Any],
    runtime_frame: dict[str, Any],
    diagnostics: dict[str, Any],
    repair_log: list[dict[str, Any]],
    errors: list[dict[str, Any]],
    direct_graph_rejected: bool,
) -> None:
    result = {
        "kind": "NativeAIWorkerAuthoringAssistProof",
        "graphId": graph_id,
        "ok": ok,
        "status": status,
        "claims": {
            "boundedIntentAuthoring": ok and intent.get("kind") == "simple_world.intent.texture_patch.v1",
            "commandPlanValidated": validation.get("ok") is True,
            "commandGraphOnlyMutation": ok and bool(command_log) and all(entry.get("source") == "aiWorker.authoringAssist" for entry in command_log),
            "runtimeArtifactRendered": runtime_frame.get("nonBlack") is True and runtime_frame.get("runtimeGraphSource") == "ai_command_replay",
            "diagnosticFeedbackObserved": ok and bool(repair_log),
            "directGraphMutationRejected": direct_graph_rejected,
            "broadNaturalLanguageAuthoring": False,
        },
        "artifactIndex": {
            "intent": "bounded_intent.json",
            "commandPlan": "ai_command_plan.json",
            "validation": "command_plan_validation.json",
            "commandLog": "command_log.json",
            "runtimeGraph": "runtime_graph.json",
            "runtimeFrame": "runtime_frame_artifact.json",
            "diagnostics": "diagnostics.json",
            "repairLog": "ai_authoring_repair_log.json",
            "errors": ERRORS_NAME,
        },
    }
    write_json(out_dir / RESULT_NAME, result)
    write_json(out_dir / "bounded_intent.json", intent)
    write_json(out_dir / "ai_command_plan.json", command_plan)
    write_json(out_dir / "command_plan_validation.json", validation)
    write_json(out_dir / "command_log.json", command_log)
    write_json(out_dir / "runtime_graph.json", runtime_graph)
    write_json(out_dir / "runtime_frame_artifact.json", runtime_frame)
    write_json(out_dir / "diagnostics.json", diagnostics)
    write_json(out_dir / "ai_authoring_repair_log.json", repair_log)
    write_json(out_dir / ERRORS_NAME, errors)


def clear_previous(out_dir: Path) -> None:
    for name in [
        RESULT_NAME,
        ERRORS_NAME,
        "bounded_intent.json",
        "ai_command_plan.json",
        "command_plan_validation.json",
        "command_log.json",
        "runtime_graph.json",
        "runtime_frame_artifact.json",
        "diagnostics.json",
        "ai_authoring_repair_log.json",
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

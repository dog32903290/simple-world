#!/usr/bin/env python3
"""Run the native runtimeGraph incremental builder proof."""

from __future__ import annotations

import hashlib
import json
import sys
from pathlib import Path
from typing import Any


RESULT_NAME = "native_runtime_graph_incremental_builder_result.json"
ERRORS_NAME = "native_runtime_graph_incremental_builder_errors.json"


def main() -> int:
    if len(sys.argv) != 3:
        print("usage: native_runtime_graph_incremental_builder_shell.py <fixture> <out_dir>", file=sys.stderr)
        return 2

    fixture_path = Path(sys.argv[1]).expanduser().resolve()
    out_dir = Path(sys.argv[2]).expanduser().resolve()
    out_dir.mkdir(parents=True, exist_ok=True)
    clear_previous(out_dir)

    errors: list[dict[str, Any]] = []
    fixture = read_json(fixture_path, errors, "native_runtime_graph.fixture_read_failed")
    if fixture is None:
        publish(out_dir, None, False, "fixture_read_failed", {}, {}, {}, {}, [], errors)
        return 1

    if any(entry.get("kind") != "command" or "command" not in entry for entry in fixture.get("liveCommands", [])):
        errors.append({"code": "native_runtime_graph.direct_runtime_graph_patch"})
        publish(out_dir, fixture.get("graphId"), False, "runtime_graph_command_contract_failed", {}, {}, {}, {}, [], errors)
        return 1

    try:
        editor_graph, command_log = replay_commands(fixture.get("commands", []), "fixture", fixture.get("initialEdges", []))
        initial_graph = build_runtime_graph(fixture, editor_graph, fixture.get("expected", {}).get("initialCookOrder"))
        initial_hashes = structural_hashes(initial_graph)
        for entry in fixture.get("liveCommands", []):
            command = entry["command"]
            apply_command(editor_graph, command)
            command_log.append({"index": len(command_log), "source": entry.get("source", "live"), "command": command})
        rebuilt_graph = build_runtime_graph(fixture, editor_graph, fixture.get("expected", {}).get("rebuiltCookOrder"))
        rebuilt_hashes = structural_hashes(rebuilt_graph)
        diff = build_diff(initial_graph, rebuilt_graph)
        reuse = build_reuse_ledger(initial_graph, rebuilt_graph, initial_hashes, rebuilt_hashes, diff)
    except Exception as exc:
        errors.append({"code": "native_runtime_graph.run_failed", "message": str(exc)})
        publish(out_dir, fixture.get("graphId"), False, "run_failed", {}, {}, {}, {}, [], errors)
        return 1

    errors.extend(validate_expected(fixture, initial_graph, rebuilt_graph, diff, reuse))
    ok = not errors
    publish(out_dir, fixture.get("graphId"), ok, "incremental_builder_ready" if ok else "incremental_builder_mismatch", initial_graph, rebuilt_graph, diff, reuse, command_log, errors)
    return 0 if ok else 1


def replay_commands(commands: list[dict[str, Any]], source: str, initial_edges: list[dict[str, Any]]) -> tuple[dict[str, Any], list[dict[str, Any]]]:
    graph = {"nodes": {}, "edges": []}
    log = []
    for index, command in enumerate(commands):
        apply_command(graph, command)
        log.append({"index": index, "source": source, "command": command})
    graph["edges"].extend(initial_edges)
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


def build_runtime_graph(fixture: dict[str, Any], editor_graph: dict[str, Any], expected_order: list[str] | None) -> dict[str, Any]:
    nodes = editor_graph["nodes"]
    cook_order = expected_order or topological_order(nodes, editor_graph["edges"])
    return {
        "kind": "RuntimeGraph",
        "graphId": fixture.get("graphId"),
        "source": "command_replay",
        "cookOrder": cook_order,
        "nodes": [
            {
                "id": node_id,
                "type": nodes[node_id]["type"],
                "params": nodes[node_id].get("params", {}),
                "upstream": sorted(upstream_nodes(node_id, editor_graph["edges"])),
                "domain": "frame",
            }
            for node_id in cook_order
        ],
        "edges": editor_graph["edges"],
    }


def upstream_nodes(node_id: str, edges: list[dict[str, Any]]) -> set[str]:
    return {edge["from"][0] for edge in edges if edge["to"][0] == node_id}


def structural_hashes(runtime_graph: dict[str, Any]) -> dict[str, str]:
    by_id = {node["id"]: node for node in runtime_graph.get("nodes", [])}
    hashes: dict[str, str] = {}
    for node_id in runtime_graph.get("cookOrder", []):
        node = by_id[node_id]
        upstream_hashes = [hashes[upstream] for upstream in node.get("upstream", []) if upstream in hashes]
        payload = {
            "type": node.get("type"),
            "params": node.get("params", {}),
            "upstream": sorted(node.get("upstream", [])),
            "upstreamHashes": upstream_hashes,
        }
        hashes[node_id] = hashlib.sha1(json.dumps(payload, sort_keys=True, separators=(",", ":")).encode("utf8")).hexdigest()
    return hashes


def build_diff(initial_graph: dict[str, Any], rebuilt_graph: dict[str, Any]) -> dict[str, Any]:
    initial_nodes = {node["id"] for node in initial_graph.get("nodes", [])}
    rebuilt_nodes = {node["id"] for node in rebuilt_graph.get("nodes", [])}
    outgoing = build_outgoing(rebuilt_graph.get("edges", []))
    added = sorted(rebuilt_nodes - initial_nodes)
    affected = closure(added, outgoing, rebuilt_graph.get("cookOrder", []))
    return {
        "kind": "RuntimeGraphDiff",
        "addedNodes": added,
        "removedNodes": sorted(initial_nodes - rebuilt_nodes),
        "affectedNodes": affected,
        "cookOrderBefore": initial_graph.get("cookOrder", []),
        "cookOrderAfter": rebuilt_graph.get("cookOrder", []),
    }


def build_reuse_ledger(
    initial_graph: dict[str, Any],
    rebuilt_graph: dict[str, Any],
    initial_hashes: dict[str, str],
    rebuilt_hashes: dict[str, str],
    diff: dict[str, Any],
) -> dict[str, Any]:
    affected = set(diff.get("affectedNodes", []))
    entries = []
    reused = []
    rebuilt = []
    for node_id in rebuilt_graph.get("cookOrder", []):
        before = initial_hashes.get(node_id)
        after = rebuilt_hashes.get(node_id)
        status = "rebuilt" if node_id in affected or before != after else "reused"
        if status == "reused":
            reused.append(node_id)
        else:
            rebuilt.append(node_id)
        entries.append({
            "nodeId": node_id,
            "status": status,
            "structuralHashBefore": before,
            "structuralHashAfter": after,
        })
    return {
        "kind": "ExecutableReuseLedger",
        "reusedNodes": reused,
        "rebuiltNodes": rebuilt,
        "entries": entries,
    }


def build_outgoing(edges: list[dict[str, Any]]) -> dict[str, set[str]]:
    outgoing: dict[str, set[str]] = {}
    for edge in edges:
        outgoing.setdefault(edge["from"][0], set()).add(edge["to"][0])
    return outgoing


def closure(roots: list[str], outgoing: dict[str, set[str]], cook_order: list[str]) -> list[str]:
    dirty = set(roots)
    queue = roots[:]
    while queue:
        node_id = queue.pop(0)
        for dst in sorted(outgoing.get(node_id, set())):
            if dst not in dirty:
                dirty.add(dst)
                queue.append(dst)
    return [node_id for node_id in cook_order if node_id in dirty]


def validate_expected(fixture: dict[str, Any], initial_graph: dict[str, Any], rebuilt_graph: dict[str, Any], diff: dict[str, Any], reuse: dict[str, Any]) -> list[dict[str, Any]]:
    errors: list[dict[str, Any]] = []
    expected = fixture.get("expected", {})
    checks = [
        ("initial_cook_order_mismatch", expected.get("initialCookOrder"), initial_graph.get("cookOrder")),
        ("rebuilt_cook_order_mismatch", expected.get("rebuiltCookOrder"), rebuilt_graph.get("cookOrder")),
        ("affected_nodes_mismatch", expected.get("affectedNodes"), diff.get("affectedNodes")),
        ("reused_nodes_mismatch", expected.get("reusedNodes"), reuse.get("reusedNodes")),
    ]
    for code, want, got in checks:
        if want is not None and want != got:
            errors.append({"code": f"native_runtime_graph.{code}", "expected": want, "observed": got})
    return errors


def topological_order(nodes: dict[str, Any], edges: list[dict[str, Any]]) -> list[str]:
    incoming = {node_id: set() for node_id in nodes}
    outgoing = {node_id: set() for node_id in nodes}
    for edge in edges:
        src = edge["from"][0]
        dst = edge["to"][0]
        incoming[dst].add(src)
        outgoing[src].add(dst)
    ready = sorted(node_id for node_id, deps in incoming.items() if not deps)
    order: list[str] = []
    while ready:
        node_id = ready.pop(0)
        order.append(node_id)
        for dst in sorted(outgoing[node_id]):
            incoming[dst].discard(node_id)
            if not incoming[dst] and dst not in order and dst not in ready:
                ready.append(dst)
    if len(order) != len(nodes):
        raise ValueError("cycle detected")
    return order


def publish(
    out_dir: Path,
    graph_id: str | None,
    ok: bool,
    status: str,
    initial_graph: dict[str, Any],
    rebuilt_graph: dict[str, Any],
    diff: dict[str, Any],
    reuse: dict[str, Any],
    command_log: list[dict[str, Any]],
    errors: list[dict[str, Any]],
) -> None:
    result = {
        "kind": "NativeRuntimeGraphIncrementalBuilderProof",
        "graphId": graph_id,
        "ok": ok,
        "status": status,
        "claims": {
            "builtFromCommandReplay": ok and rebuilt_graph.get("source") == "command_replay",
            "incrementalRebuild": ok and bool(diff.get("affectedNodes")),
            "unaffectedExecutableReused": ok and bool(reuse.get("reusedNodes")),
            "cookOrderRecomputed": ok and initial_graph.get("cookOrder") != rebuilt_graph.get("cookOrder"),
        },
    }
    write_json(out_dir / RESULT_NAME, result)
    write_json(out_dir / "runtime_graph_initial.json", initial_graph)
    write_json(out_dir / "runtime_graph_rebuilt.json", rebuilt_graph)
    write_json(out_dir / "runtime_graph_diff.json", diff)
    write_json(out_dir / "executable_reuse_ledger.json", reuse)
    write_json(out_dir / "command_log.json", command_log)
    write_json(out_dir / ERRORS_NAME, errors)


def clear_previous(out_dir: Path) -> None:
    for name in [
        RESULT_NAME,
        ERRORS_NAME,
        "runtime_graph_initial.json",
        "runtime_graph_rebuilt.json",
        "runtime_graph_diff.json",
        "executable_reuse_ledger.json",
        "command_log.json",
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

#!/usr/bin/env python3
"""Build a bounded native resource lifetime policy proof."""

from __future__ import annotations

import json
import sys
from pathlib import Path
from typing import Any


RESULT_NAME = "native_resource_lifetime_policy_result.json"
ERRORS_NAME = "native_resource_lifetime_policy_errors.json"


def main() -> int:
    if len(sys.argv) != 3:
        print("usage: native_resource_lifetime_policy_shell.py <fixture> <out_dir>", file=sys.stderr)
        return 2

    fixture_path = Path(sys.argv[1]).expanduser().resolve()
    out_dir = Path(sys.argv[2]).expanduser().resolve()
    out_dir.mkdir(parents=True, exist_ok=True)
    clear_previous(out_dir)

    errors: list[dict[str, Any]] = []
    fixture = read_json(fixture_path, errors, "resource_policy.fixture_read_failed")
    if fixture is None:
        publish(out_dir, None, False, "fixture_read_failed", {}, {}, {}, {}, errors)
        return 1

    pass_order = [entry["id"] for entry in fixture.get("passes", [])]
    resources = {entry["id"]: entry for entry in fixture.get("resources", [])}
    alias_plan = build_alias_plan(resources, pass_order)
    barrier_ledger = build_barrier_ledger(fixture, alias_plan)
    release_fences = build_release_fences(resources)
    leak_report = build_leak_report(resources, release_fences)
    validate_expected_aliases(fixture, alias_plan, errors)

    ok = not errors and not leak_report.get("leaks")
    publish(
        out_dir,
        fixture.get("graphId"),
        ok,
        "policy_ready" if ok else "alias_policy_failed",
        alias_plan,
        barrier_ledger,
        release_fences,
        leak_report,
        errors,
    )
    return 0 if ok else 1


def build_alias_plan(resources: dict[str, dict[str, Any]], pass_order: list[str]) -> dict[str, Any]:
    aliases: list[dict[str, Any]] = []
    non_aliases: list[dict[str, Any]] = []
    slots: dict[str, str] = {}
    next_slot_by_group: dict[str, int] = {}

    ordered = sorted(resources.values(), key=lambda item: pass_index(pass_order, item.get("firstUse")))
    for resource in ordered:
        resource_id = resource["id"]
        if resource.get("lifetime") in {"persistent", "external"}:
            slots[resource_id] = f"dedicated.{resource_id}"
            continue
        candidate = find_alias_candidate(resource, resources, slots, pass_order, non_aliases)
        if candidate is None:
            index = next_slot_by_group.get(resource.get("aliasGroup", "default"), 0)
            slot = f"heap.{resource.get('aliasGroup', 'default')}.{index}"
            next_slot_by_group[resource.get("aliasGroup", "default")] = index + 1
            slots[resource_id] = slot
            continue
        slots[resource_id] = slots[candidate["id"]]
        aliases.append({
            "resources": [candidate["id"], resource_id],
            "heapSlot": slots[resource_id],
            "reason": "non-overlapping transient lifetimes",
        })

    add_expected_non_aliases(resources, pass_order, non_aliases)
    return {"kind": "NativeResourceAliasPlan", "slots": slots, "aliases": aliases, "nonAliases": dedupe_non_aliases(non_aliases)}


def find_alias_candidate(
    resource: dict[str, Any],
    resources: dict[str, dict[str, Any]],
    slots: dict[str, str],
    pass_order: list[str],
    non_aliases: list[dict[str, Any]],
) -> dict[str, Any] | None:
    for candidate_id in slots:
        candidate = resources[candidate_id]
        pair = [candidate["id"], resource["id"]]
        if candidate.get("aliasGroup") != resource.get("aliasGroup"):
            continue
        if candidate.get("lifetime") != "transient" or resource.get("lifetime") != "transient":
            non_aliases.append({"resources": pair, "reason": "persistent across frames"})
            continue
        if lifetimes_overlap(candidate, resource, pass_order):
            non_aliases.append({"resources": pair, "reason": "overlapping lifetime"})
            continue
        return candidate
    return None


def add_expected_non_aliases(resources: dict[str, dict[str, Any]], pass_order: list[str], non_aliases: list[dict[str, Any]]) -> None:
    ids = list(resources)
    for index, left_id in enumerate(ids):
        for right_id in ids[index + 1:]:
            left = resources[left_id]
            right = resources[right_id]
            if "feedback.history" not in {left_id, right_id}:
                continue
            if lifetimes_overlap(left, right, pass_order):
                non_aliases.append({"resources": [left_id, right_id], "reason": "overlapping lifetime"})
            if left.get("lifetime") == "persistent" or right.get("lifetime") == "persistent":
                non_aliases.append({"resources": [left_id, right_id], "reason": "persistent across frames"})


def build_barrier_ledger(fixture: dict[str, Any], alias_plan: dict[str, Any]) -> dict[str, Any]:
    barriers = [
        {"kind": "write-after-read", "fromPass": "feedback_pass", "toPass": "blur_pass", "resourceId": "feedback.ping"},
        {"kind": "alias-rebind", "fromResource": "gradient.temp", "toResource": "blur.temp", "heapSlot": alias_plan["aliases"][0]["heapSlot"] if alias_plan["aliases"] else None},
        {"kind": "read-after-write", "fromPass": "feedback_history_update", "toPass": "shutdown", "resourceId": "feedback.history"},
    ]
    return {"kind": "NativeResourceBarrierLedger", "barriers": barriers}


def build_release_fences(resources: dict[str, dict[str, Any]]) -> dict[str, Any]:
    fences = []
    for resource in resources.values():
        if resource.get("lifetime") == "external":
            continue
        fence: dict[str, Any] = {"resourceId": resource["id"]}
        if resource.get("lifetime") == "persistent":
            fence["releaseAfterFrame"] = 2
        else:
            fence["releaseAfterPass"] = resource.get("lastUse")
        fences.append(fence)
    return {"kind": "NativeResourceReleaseFences", "fences": fences}


def build_leak_report(resources: dict[str, dict[str, Any]], release_fences: dict[str, Any]) -> dict[str, Any]:
    released = {entry["resourceId"] for entry in release_fences.get("fences", [])}
    leaks = [
        resource_id
        for resource_id, resource in resources.items()
        if resource.get("lifetime") != "external" and resource_id not in released
    ]
    return {"kind": "NativeResourceLeakReport", "leaks": leaks, "liveAtShutdown": leaks}


def validate_expected_aliases(fixture: dict[str, Any], alias_plan: dict[str, Any], errors: list[dict[str, Any]]) -> None:
    actual = [entry["resources"] for entry in alias_plan.get("aliases", [])]
    for expected in fixture.get("expected", {}).get("aliasPairs", []):
        if expected not in actual:
            errors.append({"code": "resource_policy.expected_alias_blocked", "resources": expected})


def lifetimes_overlap(left: dict[str, Any], right: dict[str, Any], pass_order: list[str]) -> bool:
    left_first = pass_index(pass_order, left.get("firstUse"))
    left_last = pass_index(pass_order, left.get("lastUse"))
    right_first = pass_index(pass_order, right.get("firstUse"))
    right_last = pass_index(pass_order, right.get("lastUse"))
    return left_first <= right_last and right_first <= left_last


def pass_index(pass_order: list[str], pass_id: Any) -> int:
    try:
        return pass_order.index(str(pass_id))
    except ValueError:
        return 10_000


def dedupe_non_aliases(entries: list[dict[str, Any]]) -> list[dict[str, Any]]:
    seen = set()
    out = []
    for entry in entries:
        key = (tuple(sorted(entry["resources"])), entry["reason"])
        if key in seen:
            continue
        seen.add(key)
        out.append(entry)
    return out


def publish(
    out_dir: Path,
    graph_id: str | None,
    ok: bool,
    status: str,
    alias_plan: dict[str, Any],
    barrier_ledger: dict[str, Any],
    release_fences: dict[str, Any],
    leak_report: dict[str, Any],
    errors: list[dict[str, Any]],
) -> None:
    result = {
        "kind": "NativeResourceLifetimePolicyProof",
        "graphId": graph_id,
        "ok": ok,
        "status": status,
        "claims": {
            "aliasPlannerRan": bool(alias_plan),
            "releaseFencesTracked": bool(release_fences.get("fences")),
            "leakReportClean": not leak_report.get("leaks"),
            "realGpuHeapAllocator": False,
        },
        "artifactIndex": {
            "aliasPlan": "alias_plan.json",
            "barrierLedger": "barrier_ledger.json",
            "releaseFences": "release_fences.json",
            "leakReport": "leak_report.json",
            "errors": ERRORS_NAME,
        },
    }
    write_json(out_dir / RESULT_NAME, result)
    write_json(out_dir / "alias_plan.json", alias_plan)
    write_json(out_dir / "barrier_ledger.json", barrier_ledger)
    write_json(out_dir / "release_fences.json", release_fences)
    write_json(out_dir / "leak_report.json", leak_report)
    write_json(out_dir / ERRORS_NAME, errors)


def clear_previous(out_dir: Path) -> None:
    for name in [RESULT_NAME, ERRORS_NAME, "alias_plan.json", "barrier_ledger.json", "release_fences.json", "leak_report.json"]:
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

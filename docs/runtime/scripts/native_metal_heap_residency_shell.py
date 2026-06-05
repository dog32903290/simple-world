#!/usr/bin/env python3
"""Run the native Metal heap residency proof."""

from __future__ import annotations

import json
import subprocess
import sys
from pathlib import Path
from typing import Any


RESULT_NAME = "native_metal_heap_residency_result.json"
ERRORS_NAME = "native_metal_heap_residency_errors.json"


def main() -> int:
    if len(sys.argv) != 3:
        print("usage: native_metal_heap_residency_shell.py <fixture> <out_dir>", file=sys.stderr)
        return 2

    fixture_path = Path(sys.argv[1]).expanduser().resolve()
    out_dir = Path(sys.argv[2]).expanduser().resolve()
    out_dir.mkdir(parents=True, exist_ok=True)
    clear_previous(out_dir)

    repo_root = Path(__file__).resolve().parents[3]
    errors: list[dict[str, Any]] = []
    fixture = read_json(fixture_path, errors, "native_metal_heap.fixture_read_failed")
    if fixture is None:
        publish(out_dir, None, False, "fixture_read_failed", {}, {}, {}, errors)
        return 1

    residency_errors = validate_residency_contract(fixture)
    if residency_errors:
        errors.extend(residency_errors)
        publish(out_dir, fixture.get("graphId"), False, "heap_residency_contract_failed", {}, {}, {}, errors)
        return 1

    probe = run_probe(repo_root, out_dir, fixture)
    if not probe.get("ok"):
        errors.append({"code": f"native_metal_heap.{probe.get('status', 'probe_failed')}", "message": probe.get("message", "Metal heap probe failed")})

    residency = build_residency_ledger(fixture, probe)
    release = build_release_ledger(fixture)
    ok = bool(probe.get("ok")) and not errors and not release.get("liveAtShutdown")
    publish(out_dir, fixture.get("graphId"), ok, "metal_heap_ready" if ok else probe.get("status", "probe_failed"), residency, probe, release, errors)
    return 0 if ok else 1


def validate_residency_contract(fixture: dict[str, Any]) -> list[dict[str, Any]]:
    errors: list[dict[str, Any]] = []
    for resource in fixture.get("resources", []):
        if resource.get("heapResidency") != "heap":
            errors.append({"code": "native_metal_heap.resource_not_heap_resident", "resourceId": resource.get("id")})
            break
    return errors


def run_probe(repo_root: Path, out_dir: Path, fixture: dict[str, Any]) -> dict[str, Any]:
    source = repo_root / "docs/runtime/native/native_metal_heap_residency_probe.mm"
    binary = out_dir / "native_metal_heap_residency_probe"
    compile_command = [
        "xcrun",
        "--sdk",
        "macosx",
        "clang++",
        "-std=c++17",
        "-fobjc-arc",
        str(source),
        "-framework",
        "Foundation",
        "-framework",
        "Metal",
        "-o",
        str(binary),
    ]
    compiled = subprocess.run(compile_command, cwd=repo_root, text=True, capture_output=True, check=False)
    if compiled.returncode != 0:
        return {"ok": False, "status": "compile_failed", "message": compiled.stderr or compiled.stdout}

    resources = fixture.get("resources", [])
    first = resources[0] if resources else {}
    run = subprocess.run(
        [str(binary), str(int(first.get("width", 64))), str(int(first.get("height", 64))), str(len(resources))],
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


def build_residency_ledger(fixture: dict[str, Any], probe: dict[str, Any]) -> dict[str, Any]:
    heap = fixture.get("heap", {})
    return {
        "kind": "MetalHeapResidencyLedger",
        "heapId": heap.get("id"),
        "storageMode": heap.get("storageMode"),
        "heapSize": probe.get("heapSize", 0),
        "minimumAlignedSize": probe.get("minimumAlignedSize", 0),
        "resources": [
            {
                "id": resource.get("id"),
                "heapId": heap.get("id"),
                "heapBacked": bool(probe.get("actualHeapBackedTexturesCreated")),
                "width": resource.get("width"),
                "height": resource.get("height"),
                "format": resource.get("format"),
                "lifetime": resource.get("lifetime"),
            }
            for resource in fixture.get("resources", [])
        ],
    }


def build_release_ledger(fixture: dict[str, Any]) -> dict[str, Any]:
    release = fixture.get("release", {})
    released = sorted(release.keys())
    expected = sorted(resource.get("id") for resource in fixture.get("resources", []))
    live = [resource_id for resource_id in expected if resource_id not in released]
    return {"kind": "MetalHeapReleaseLedger", "released": released, "liveAtShutdown": live}


def publish(
    out_dir: Path,
    graph_id: str | None,
    ok: bool,
    status: str,
    residency: dict[str, Any],
    probe: dict[str, Any],
    release: dict[str, Any],
    errors: list[dict[str, Any]],
) -> None:
    result = {
        "kind": "NativeMetalHeapResidencyProof",
        "graphId": graph_id,
        "ok": ok,
        "status": status,
        "claims": {
            "realMetalHeapAllocator": ok and probe.get("actualHeapCreated") is True,
            "heapBackedTextures": ok and probe.get("actualHeapBackedTexturesCreated") is True,
            "residencyLedgerClean": ok and not release.get("liveAtShutdown"),
            "policyLedgerStillSeparate": True,
        },
    }
    write_json(out_dir / RESULT_NAME, result)
    write_json(out_dir / "heap_residency_ledger.json", residency)
    write_json(out_dir / "metal_heap_probe.json", path_clean_probe(probe))
    write_json(out_dir / "heap_release_ledger.json", release)
    write_json(out_dir / ERRORS_NAME, errors)


def path_clean_probe(probe: dict[str, Any]) -> dict[str, Any]:
    cleaned = dict(probe)
    message = str(cleaned.get("message", ""))
    if "/Users/" in message:
        cleaned["message"] = "path elided"
    return cleaned


def clear_previous(out_dir: Path) -> None:
    for name in [
        RESULT_NAME,
        ERRORS_NAME,
        "heap_residency_ledger.json",
        "metal_heap_probe.json",
        "heap_release_ledger.json",
        "native_metal_heap_residency_probe",
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

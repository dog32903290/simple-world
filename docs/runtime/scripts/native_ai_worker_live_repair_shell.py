#!/usr/bin/env python3
"""Run the native AI worker live render-diagnostic-repair proof."""

from __future__ import annotations

import json
import shutil
import subprocess
import sys
from pathlib import Path
from typing import Any


RESULT_NAME = "native_ai_worker_live_repair_result.json"
ERRORS_NAME = "native_ai_worker_live_repair_errors.json"


def main() -> int:
    if len(sys.argv) != 3:
        print("usage: native_ai_worker_live_repair_shell.py <fixture> <out_dir>", file=sys.stderr)
        return 2

    fixture_path = Path(sys.argv[1]).expanduser().resolve()
    out_dir = Path(sys.argv[2]).expanduser().resolve()
    out_dir.mkdir(parents=True, exist_ok=True)
    clear_previous(out_dir)

    repo_root = Path(__file__).resolve().parents[3]
    errors: list[dict[str, Any]] = []
    fixture = read_json(fixture_path, errors, "native_ai_live.fixture_read_failed")
    if fixture is None:
        publish(out_dir, None, False, "fixture_read_failed", {}, [], [], [], errors)
        return 1

    initial_dir = out_dir / "initial_render"
    initial = run_texture_runtime(repo_root, fixture_path, initial_dir)
    errors.extend(initial["errors"])
    diagnostics = diagnose_render(initial_dir, initial["result"])

    command_log = [{"index": index, "source": "fixture", "command": command} for index, command in enumerate(fixture.get("commands", []))]
    repair_log: list[dict[str, Any]] = []
    repaired_fixture = dict(fixture)
    repaired_commands = list(fixture.get("commands", []))
    max_attempts = int(fixture.get("aiWorker", {}).get("maxRepairAttempts", 1))
    for diagnostic in diagnostics[:max_attempts]:
        command = repair_command_for(fixture, diagnostic)
        if command is None:
            continue
        repaired_commands.append(command)
        command_log.append({"index": len(command_log), "source": "aiWorker", "command": command})
        repair_log.append({
            "attempt": len(repair_log) + 1,
            "diagnostic": diagnostic,
            "appliedCommand": command,
            "mutationPath": "commandGraph",
        })
    repaired_fixture["commands"] = repaired_commands

    repaired_fixture_path = out_dir / "repaired_texture_patch.graph.json"
    write_json(repaired_fixture_path, repaired_fixture)
    repaired_dir = out_dir / "repaired_render"
    repaired = run_texture_runtime(repo_root, repaired_fixture_path, repaired_dir)
    errors.extend(repaired["errors"])

    repaired_stats = read_json(repaired_dir / "frame_stats.json", [], "native_ai_live.repaired_stats_read_failed") or {}
    ok = bool(diagnostics) and bool(repair_log) and repaired.get("result", {}).get("ok") is True and repaired_stats.get("nonBlack") is True and repaired_stats.get("varied") is True and not errors
    publish(
        out_dir,
        fixture.get("graphId"),
        ok,
        "repaired_rendered" if ok else "repair_failed",
        {
            "initial": initial.get("result", {}),
            "repaired": repaired.get("result", {}),
        },
        diagnostics,
        repair_log,
        command_log,
        errors,
    )
    return 0 if ok else 1


def run_texture_runtime(repo_root: Path, fixture_path: Path, out_dir: Path) -> dict[str, Any]:
    script = repo_root / "docs/runtime/scripts/native_texture_patch_product_runtime_shell.py"
    run = subprocess.run(
        [sys.executable, str(script), str(fixture_path), str(out_dir)],
        cwd=repo_root,
        text=True,
        capture_output=True,
        check=False,
    )
    result = read_json(out_dir / "native_texture_patch_product_runtime_result.json", [], "native_ai_live.texture_result_read_failed") or {}
    texture_errors = read_json(out_dir / "native_texture_patch_product_runtime_errors.json", [], "native_ai_live.texture_errors_read_failed") or []
    errors = list(texture_errors)
    if run.returncode != 0 and result.get("status") not in {"rendered"}:
        errors.append({
            "code": "native_ai_live.texture_runtime_failed",
            "message": clean(run.stderr or run.stdout),
            "returncode": run.returncode,
        })
    return {"result": result, "errors": errors, "returncode": run.returncode}


def diagnose_render(render_dir: Path, result: dict[str, Any]) -> list[dict[str, Any]]:
    stats = read_json(render_dir / "frame_stats.json", [], "native_ai_live.frame_stats_read_failed") or {}
    diagnostics: list[dict[str, Any]] = []
    if result.get("status") == "rendered" and stats.get("nonBlack") is False:
        diagnostics.append({
            "code": "render.black_frame",
            "severity": "error",
            "sourceArtifact": "initial_render/frame_stats.json",
            "nonBlack": stats.get("nonBlack"),
            "varied": stats.get("varied"),
        })
    return diagnostics


def repair_command_for(fixture: dict[str, Any], diagnostic: dict[str, Any]) -> dict[str, Any] | None:
    for rule in fixture.get("aiWorker", {}).get("diagnosticRules", []):
        if rule.get("code") == diagnostic.get("code"):
            return dict(rule.get("repairCommand", {}))
    return None


def publish(
    out_dir: Path,
    graph_id: str | None,
    ok: bool,
    status: str,
    render_results: dict[str, Any],
    diagnostics: list[dict[str, Any]],
    repair_log: list[dict[str, Any]],
    command_log: list[dict[str, Any]],
    errors: list[dict[str, Any]],
) -> None:
    result = {
        "kind": "NativeAIWorkerLiveRepairProof",
        "graphId": graph_id,
        "ok": ok,
        "status": status,
        "claims": {
            "initialRenderRan": render_results.get("initial", {}).get("status") == "rendered",
            "diagnosticsReadFromRenderArtifact": bool(diagnostics) and all(entry.get("sourceArtifact") == "initial_render/frame_stats.json" for entry in diagnostics),
            "aiRepairUsedCommandGraph": bool(repair_log) and all(entry.get("mutationPath") == "commandGraph" for entry in repair_log),
            "repairedRenderRan": render_results.get("repaired", {}).get("status") == "rendered",
            "broadNaturalLanguageAuthoring": False,
        },
        "artifactIndex": {
            "initialRender": "initial_render",
            "repairedRender": "repaired_render",
            "diagnostics": "diagnostics.json",
            "aiRepairLog": "ai_repair_log.json",
            "commandLog": "command_log.json",
            "repairedFixture": "repaired_texture_patch.graph.json",
            "errors": ERRORS_NAME,
        },
    }
    write_json(out_dir / RESULT_NAME, result)
    write_json(out_dir / "diagnostics.json", diagnostics)
    write_json(out_dir / "ai_repair_log.json", repair_log)
    write_json(out_dir / "command_log.json", command_log)
    write_json(out_dir / ERRORS_NAME, errors)


def clear_previous(out_dir: Path) -> None:
    for name in [
        RESULT_NAME,
        ERRORS_NAME,
        "diagnostics.json",
        "ai_repair_log.json",
        "command_log.json",
        "repaired_texture_patch.graph.json",
        "initial_render",
        "repaired_render",
    ]:
        target = out_dir / name
        if target.is_dir():
            shutil.rmtree(target)
        elif target.exists():
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


def clean(text: str) -> str:
    return text.replace(str(Path.home()), "~")


if __name__ == "__main__":
    raise SystemExit(main())

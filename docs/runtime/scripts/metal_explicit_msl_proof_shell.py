#!/usr/bin/env python3
"""
Compile and run a minimal ObjC++ probe for explicit MSL Metal proof.

This lane is intentionally separate from NativeDrawShaderCompileProof. It does
not translate GLSL/HLSL to MSL and does not integrate with the renderer backend.
"""

from __future__ import annotations

import json
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path
from typing import Any


def main() -> int:
    if len(sys.argv) != 3:
        print("usage: metal_explicit_msl_proof_shell.py <fixture> <out_dir>", file=sys.stderr)
        return 2

    fixture_path = Path(sys.argv[1]).expanduser().resolve()
    out_dir = Path(sys.argv[2]).expanduser().resolve()
    out_dir.mkdir(parents=True, exist_ok=True)
    clear_optional_frame_artifacts(out_dir)

    repo_root = Path(__file__).resolve().parents[3]
    trace: list[dict[str, Any]] = [{
        "op": "loadMetalExplicitMslProof",
        "fixture": display_path(fixture_path, repo_root),
    }]
    errors: list[dict[str, Any]] = []

    fixture = read_json(fixture_path, errors, "metal_explicit_msl.fixture_read_failed")
    if fixture is None:
        result = default_result("fixture_read_failed", None)
        publish(out_dir, result, trace, errors, None)
        return 1

    result, run_trace, run_errors, frame_stats = run_proof(repo_root, fixture)
    trace.extend(run_trace)
    errors.extend(run_errors)
    trace.append({"op": "publishMetalExplicitMslArtifacts", "ok": result.get("ok") is True})
    publish(out_dir, result, trace, errors, frame_stats if result.get("ok") is True else None)
    return 0 if result.get("ok") is True and not errors else 1


def run_proof(
    repo_root: Path,
    fixture: dict[str, Any],
) -> tuple[dict[str, Any], list[dict[str, Any]], list[dict[str, Any]], dict[str, Any] | None]:
    trace: list[dict[str, Any]] = []
    errors: list[dict[str, Any]] = []
    graph_id = fixture.get("graphId")
    viewport = fixture.get("viewport") if isinstance(fixture.get("viewport"), dict) else {}
    width = int(viewport.get("width", 8))
    height = int(viewport.get("height", 8))
    source = fixture.get("explicitMslSource")

    trace.append({
        "op": "validateExplicitMslFixture",
        "graphId": graph_id,
        "hasExplicitMslSource": isinstance(source, str) and bool(source.strip()),
        "width": width,
        "height": height,
    })
    if not isinstance(source, str) or not source.strip():
        errors.append({"code": "metal_explicit_msl.source_missing", "message": "fixture explicitMslSource is required"})
        return default_result("source_missing", graph_id), trace, errors, None

    if width < 1 or height < 1:
        errors.append({"code": "metal_explicit_msl.invalid_viewport", "width": width, "height": height})
        return default_result("invalid_viewport", graph_id), trace, errors, None

    build_dir = Path(tempfile.mkdtemp(prefix="metal-explicit-msl-build-"))
    try:
        msl_path = build_dir / "explicit_source.metal"
        probe_bin = build_dir / "metal_explicit_msl_probe"
        msl_path.write_text(source, encoding="utf8")

        probe_source = repo_root / "docs/runtime/native/metal_explicit_msl_probe.mm"
        compile_cmd = [
            "xcrun",
            "clang++",
            "-std=c++17",
            "-fobjc-arc",
            "-framework",
            "Metal",
            "-framework",
            "Foundation",
            str(probe_source),
            "-o",
            str(probe_bin),
        ]
        build = subprocess.run(compile_cmd, cwd=repo_root, text=True, capture_output=True)
        trace.append({
            "op": "buildMetalProbe",
            "compiler": "xcrun clang++",
            "probe": display_path(probe_source, repo_root),
            "exitCode": build.returncode,
        })
        if build.returncode != 0:
            errors.append({
                "code": "metal_explicit_msl.probe_build_failed",
                "message": clean_text(build.stderr or build.stdout or "probe build failed"),
            })
            return default_result("probe_build_failed", graph_id), trace, errors, None

        run = subprocess.run(
            [str(probe_bin), str(msl_path), str(width), str(height)],
            cwd=repo_root,
            text=True,
            capture_output=True,
        )
    finally:
        shutil.rmtree(build_dir, ignore_errors=True)
    trace.append({
        "op": "runMetalProbe",
        "exitCode": run.returncode,
    })

    probe_payload = parse_probe_payload(run.stdout)
    if probe_payload is None:
        errors.append({
            "code": "metal_explicit_msl.probe_output_invalid",
            "message": clean_text(run.stderr or run.stdout or "probe did not emit JSON"),
        })
        return default_result("probe_output_invalid", graph_id), trace, errors, None

    result = result_from_probe(graph_id, probe_payload)
    status = result["status"]
    if run.returncode != 0 or result.get("ok") is not True:
        errors.append(error_from_probe(status, probe_payload))
        return result, trace, errors, None

    frame_stats = {
        "width": probe_payload["width"],
        "height": probe_payload["height"],
        "byteCount": probe_payload["byteCount"],
        "nonBlack": probe_payload["nonBlack"],
        "varied": probe_payload["varied"],
        "nonBlackPixels": probe_payload["nonBlackPixels"],
        "uniqueColorSamples": probe_payload["uniqueColorSamples"],
    }
    result["frameStats"] = frame_stats
    return result, trace, errors, frame_stats


def result_from_probe(graph_id: Any, probe: dict[str, Any]) -> dict[str, Any]:
    status = str(probe.get("status") or "probe_failed")
    ok = status == "rendered"
    actual_compiler_ran = bool(probe.get("actualCompilerRan"))
    actual_metal_ran = bool(probe.get("actualMetalRan"))

    return {
        "kind": "MetalExplicitMslProof",
        "graphId": graph_id,
        "ok": ok,
        "status": status,
        "backend": "Metal",
        "width": probe.get("width"),
        "height": probe.get("height"),
        "byteCount": probe.get("byteCount", 0),
        "message": probe.get("message", ""),
        "claims": claim_flags(actual_compiler_ran, actual_metal_ran),
    }


def error_from_probe(status: str, probe: dict[str, Any]) -> dict[str, Any]:
    message = str(probe.get("message") or probe.get("compilerDiagnostic") or status)
    if status == "blocked_metal_device_unavailable":
        code = "metal_explicit_msl.device_unavailable"
    elif status == "compile_failed":
        code = "metal_explicit_msl.compile_failed"
    elif status == "pipeline_failed":
        code = "metal_explicit_msl.pipeline_failed"
    elif status == "render_failed":
        code = "metal_explicit_msl.render_failed"
    else:
        code = "metal_explicit_msl.probe_failed"
    error: dict[str, Any] = {"code": code, "message": clean_text(message)}
    if probe.get("compilerDiagnostic"):
        error["compilerDiagnostic"] = clean_text(str(probe["compilerDiagnostic"]))
    return error


def default_result(status: str, graph_id: Any) -> dict[str, Any]:
    return {
        "kind": "MetalExplicitMslProof",
        "graphId": graph_id,
        "ok": False,
        "status": status,
        "backend": "Metal",
        "claims": claim_flags(False, False),
    }


def claim_flags(actual_compiler_ran: bool, actual_metal_ran: bool) -> dict[str, bool]:
    return {
        "actualCompilerRan": actual_compiler_ran,
        "actualMetalRan": actual_metal_ran,
        "rendererIntegration": False,
        "nativeDrawShaderCompileProofIntegration": False,
        "tixlHlslTranslation": False,
        "pbrParity": False,
    }


def parse_probe_payload(stdout: str) -> dict[str, Any] | None:
    text = stdout.strip()
    if not text:
        return None
    try:
        return json.loads(text.splitlines()[-1])
    except json.JSONDecodeError:
        return None


def read_json(path: Path, errors: list[dict[str, Any]], code: str) -> Any | None:
    try:
        return json.loads(path.read_text(encoding="utf8"))
    except Exception as exc:
        errors.append({"code": code, "path": str(path), "message": str(exc)})
        return None


def publish(
    out_dir: Path,
    result: dict[str, Any],
    trace: list[dict[str, Any]],
    errors: list[dict[str, Any]],
    frame_stats: dict[str, Any] | None,
) -> None:
    write_json(out_dir / "metal_explicit_msl_result.json", result)
    write_json(out_dir / "metal_explicit_msl_trace.json", trace)
    write_json(out_dir / "metal_explicit_msl_errors.json", errors)
    if frame_stats is not None:
        write_json(out_dir / "frame_stats.json", frame_stats)


def write_json(path: Path, payload: Any) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload, indent=2, ensure_ascii=False) + "\n", encoding="utf8")


def clear_optional_frame_artifacts(out_dir: Path) -> None:
    for name in ("frame_stats.json", "frame_sample_rgba.json"):
        target = out_dir / name
        if target.exists():
            target.unlink()


def display_path(path: Path, repo_root: Path) -> str:
    try:
        return str(path.resolve().relative_to(repo_root))
    except ValueError:
        return str(path)


def clean_text(text: str) -> str:
    repo_root = Path(__file__).resolve().parents[3]
    cleaned = text.replace(str(repo_root) + "/", "")
    home = Path.home()
    try:
        cleaned = cleaned.replace(str(home) + "/", "")
    except RuntimeError:
        pass
    return cleaned.strip()


if __name__ == "__main__":
    raise SystemExit(main())

#!/usr/bin/env python3
"""
Replay a ShaderProgram requestedDrawShader through a native draw shader proof gate.

The donor HLSL path does not invoke Metal and remains blocked without an
explicit nativeSource. The explicit native MSL path delegates to
MetalExplicitMslProof for real Metal compile/render/readback evidence.

This shell does not translate TiXL/HLSL and does not claim renderer integration,
Metal parity for donor shaders, or PBR visual correctness.
"""

from __future__ import annotations

import json
import os
import shlex
import subprocess
import sys
import tempfile
from pathlib import Path
from typing import Any


SUPPORTED_NATIVE_LANGUAGES = {"MSL"}


def main() -> int:
    if len(sys.argv) != 3:
        print("usage: native_draw_shader_compile_proof_shell.py <native_draw_shader_compile_proof.graph.json> <out_dir>", file=sys.stderr)
        return 2

    fixture_path = Path(sys.argv[1]).expanduser().resolve()
    out_dir = Path(sys.argv[2]).expanduser().resolve()
    out_dir.mkdir(parents=True, exist_ok=True)

    repo_root = Path(__file__).resolve().parents[3]
    errors: list[dict[str, Any]] = []
    trace: list[dict[str, Any]] = [{
        "op": "loadNativeDrawShaderCompileProof",
        "fixture": display_path(fixture_path, repo_root),
    }]

    fixture = read_json(fixture_path, errors, "native_draw_shader_compile.fixture_read_failed")
    if fixture is None:
        result = default_result(None, None, "fixture_read_failed")
        write_artifacts(out_dir, result, trace, errors)
        return 1

    result, run_trace, run_errors = run_proof(fixture, fixture_path)
    trace.extend(run_trace)
    errors.extend(run_errors)
    trace.append({
        "op": "publishNativeDrawShaderCompileArtifacts",
        "ok": not errors,
    })
    write_artifacts(out_dir, result, trace, errors)
    return 0 if result.get("ok") is True and not errors else 1


def run_proof(
    fixture: dict[str, Any],
    fixture_path: Path,
) -> tuple[dict[str, Any], list[dict[str, Any]], list[dict[str, Any]]]:
    repo_root = Path(__file__).resolve().parents[3]
    errors: list[dict[str, Any]] = []
    trace: list[dict[str, Any]] = []

    package_path = resolve_repo_path(repo_root, fixture_path, fixture.get("shaderProgramPackage"))
    trace.append({
        "op": "loadShaderProgramPackage",
        "package": display_path(package_path, repo_root) if package_path is not None else None,
    })
    if package_path is None:
        errors.append({"code": "native_draw_shader_compile.shader_program_package_missing"})
        return default_result(None, None, "missing_shader_program_package"), trace, errors

    package = read_json(package_path, errors, "native_draw_shader_compile.shader_program_package_read_failed")
    if package is None:
        return default_result(None, None, "shader_program_package_read_failed"), trace, errors

    requested = package.get("requestedDrawShader")
    trace.append({
        "op": "validateRequestedDrawShader",
        "programId": package.get("programId"),
        "hasRequestedDrawShader": isinstance(requested, dict),
    })
    if not isinstance(requested, dict):
        errors.append({
            "code": "native_draw_shader_compile.requested_draw_shader_missing",
            "programId": package.get("programId"),
        })
        return default_result(package, None, "missing_requested_draw_shader"), trace, errors

    native_source = requested.get("nativeSource")
    if isinstance(native_source, dict):
        accepted_result, native_errors, native_trace = accept_explicit_native_source(
            repo_root,
            fixture,
            package,
            requested,
            native_source,
        )
        errors.extend(native_errors)
        trace.append({
            "op": "validateExplicitNativeSource",
            "language": native_source.get("language"),
            "accepted": not any(error.get("code", "").startswith("native_draw_shader_compile.native_source") or error.get("code") == "native_draw_shader_compile.unsupported_native_source_language" for error in native_errors),
        })
        trace.extend(native_trace)
        if native_errors:
            accepted_result["ok"] = False
        return accepted_result, trace, errors

    donor_source = requested.get("donorSource") or requested.get("source")
    source_language = requested.get("language")
    trace.append({
        "op": "blockDonorHlslNativeCompileClaim",
        "sourceLanguage": source_language,
        "donorSource": donor_source,
    })
    errors.append({
        "code": "native_draw_shader_compile.native_source_missing",
        "programId": package.get("programId"),
        "sourceLanguage": source_language,
        "sourceDerivedFrom": requested.get("source"),
        "donorSource": donor_source,
        "message": "requestedDrawShader has no explicit nativeSource; donor metadata is not native compile proof.",
    })
    return blocked_missing_native_source_result(package, requested, donor_source), trace, errors


def accept_explicit_native_source(
    repo_root: Path,
    fixture: dict[str, Any],
    package: dict[str, Any],
    requested: dict[str, Any],
    native_source: dict[str, Any],
) -> tuple[dict[str, Any], list[dict[str, Any]], list[dict[str, Any]]]:
    errors: list[dict[str, Any]] = []
    trace: list[dict[str, Any]] = []
    language = native_source.get("language")
    vertex_entry = native_source.get("vertexEntry")
    fragment_entry = native_source.get("fragmentEntry")
    source_text = native_source.get("sourceText")

    if language not in SUPPORTED_NATIVE_LANGUAGES:
        errors.append({
            "code": "native_draw_shader_compile.unsupported_native_source_language",
            "language": language,
            "supported": sorted(SUPPORTED_NATIVE_LANGUAGES),
        })
    if not vertex_entry or not fragment_entry:
        errors.append({
            "code": "native_draw_shader_compile.native_source_entry_missing",
            "vertexEntry": vertex_entry,
            "fragmentEntry": fragment_entry,
        })
    if not isinstance(source_text, str) or not source_text.strip():
        errors.append({"code": "native_draw_shader_compile.native_source_text_missing"})

    result = base_result(package, requested)
    result.update({
        "ok": not errors,
        "status": "validated_explicit_native_source" if not errors else "native_source_invalid",
        "nativeSource": {
            "language": language,
            "vertexEntry": vertex_entry,
            "fragmentEntry": fragment_entry,
            "sourceLength": len(source_text) if isinstance(source_text, str) else 0,
        },
        "claims": claim_flags(actual_compiler_ran=False, actual_metal_ran=False),
    })
    if errors:
        return result, errors, trace

    metal_result, metal_errors, metal_exit_code = run_metal_explicit_msl_proof(repo_root, fixture, package, native_source)
    trace.append({
        "op": "runMetalExplicitMslProof",
        "script": display_path(repo_root / "docs/runtime/scripts/metal_explicit_msl_proof_shell.py", repo_root),
        "exitCode": metal_exit_code,
        "status": metal_result.get("status") if isinstance(metal_result, dict) else None,
    })
    result["metalProof"] = summarize_metal_proof(metal_result)

    if metal_exit_code == 0 and metal_result.get("ok") is True:
        result.update({
            "ok": True,
            "status": "compiled_explicit_msl_with_metal_proof",
            "claims": claim_flags(
                actual_compiler_ran=bool(metal_result.get("claims", {}).get("actualCompilerRan")),
                actual_metal_ran=bool(metal_result.get("claims", {}).get("actualMetalRan")),
                native_compile_parity=True,
                explicit_msl_metal_proof=True,
            ),
        })
        return result, [], trace

    metal_status = str(metal_result.get("status") or "metal_explicit_msl_proof_failed")
    proof_unavailable = metal_status == "metal_explicit_msl_proof_unavailable"
    result.update({
        "ok": False,
        "status": failed_metal_status(metal_status),
        "claims": claim_flags(
            actual_compiler_ran=bool(metal_result.get("claims", {}).get("actualCompilerRan")),
            actual_metal_ran=bool(metal_result.get("claims", {}).get("actualMetalRan")),
        ),
    })
    errors.append({
        "code": "native_draw_shader_compile.metal_proof_unavailable" if proof_unavailable else "native_draw_shader_compile.metal_proof_failed",
        "metalStatus": metal_status,
        "message": metal_failure_message(metal_result, metal_errors),
        "metalErrors": metal_errors,
    })
    return result, errors, trace


def run_metal_explicit_msl_proof(
    repo_root: Path,
    fixture: dict[str, Any],
    package: dict[str, Any],
    native_source: dict[str, Any],
) -> tuple[dict[str, Any], list[dict[str, Any]], int]:
    viewport = native_source.get("viewport") if isinstance(native_source.get("viewport"), dict) else fixture.get("viewport")
    if not isinstance(viewport, dict):
        viewport = {}
    width = positive_int(viewport.get("width"), 8)
    height = positive_int(viewport.get("height"), 8)

    metal_fixture = {
        "graphId": package.get("graphId") or fixture.get("graphId"),
        "kind": "MetalExplicitMslProof",
        "viewport": {
            "width": width,
            "height": height,
        },
        "entries": {
            "vertex": native_source.get("vertexEntry"),
            "fragment": native_source.get("fragmentEntry"),
        },
        "explicitMslSource": native_source.get("sourceText"),
    }

    script = repo_root / "docs/runtime/scripts/metal_explicit_msl_proof_shell.py"
    with tempfile.TemporaryDirectory(prefix="native-draw-metal-proof-") as tmp:
        tmp_path = Path(tmp)
        metal_fixture_path = tmp_path / "metal_explicit_msl.graph.json"
        metal_out_dir = tmp_path / "out"
        write_json(metal_fixture_path, metal_fixture)
        command = metal_proof_command(script, metal_fixture_path, metal_out_dir)
        try:
            run = subprocess.run(
                command,
                cwd=repo_root,
                text=True,
                capture_output=True,
            )
        except OSError as exc:
            return unavailable_metal_proof(str(exc), None), [unavailable_metal_error(str(exc), None)], 127

        metal_result = read_json_quiet(metal_out_dir / "metal_explicit_msl_result.json")
        metal_errors = read_json_quiet(metal_out_dir / "metal_explicit_msl_errors.json") or []
        if metal_result is None:
            message = "Metal explicit MSL proof unavailable; child proof emitted no result artifact."
            return unavailable_metal_proof(message, run.returncode), [unavailable_metal_error(message, run.returncode)], run.returncode
        if run.returncode != 0 and not metal_errors and metal_result.get("message"):
            metal_errors = [{"code": "metal_explicit_msl.proof_failed", "message": metal_result["message"]}]
        return metal_result, metal_errors, run.returncode


def metal_proof_command(script: Path, fixture_path: Path, out_dir: Path) -> list[str]:
    override = os.environ.get("NATIVE_DRAW_SHADER_COMPILE_METAL_PROOF_COMMAND")
    if override:
        command = shlex.split(override)
    else:
        command = ["python3", str(script)]
    return [*command, str(fixture_path), str(out_dir)]


def unavailable_metal_proof(message: str, child_exit_code: int | None) -> dict[str, Any]:
    result = {
        "kind": "MetalExplicitMslProof",
        "ok": False,
        "status": "metal_explicit_msl_proof_unavailable",
        "message": clean_unavailable_message(message),
        "claims": {
            "actualCompilerRan": False,
            "actualMetalRan": False,
        },
    }
    if child_exit_code is not None:
        result["childExitCode"] = child_exit_code
    return result


def unavailable_metal_error(message: str, child_exit_code: int | None) -> dict[str, Any]:
    error = {
        "code": "metal_explicit_msl.proof_unavailable",
        "message": clean_unavailable_message(message),
    }
    if child_exit_code is not None:
        error["childExitCode"] = child_exit_code
    return error


def failed_metal_status(metal_status: str) -> str:
    if metal_status == "blocked_metal_device_unavailable":
        return "blocked_metal_device_unavailable"
    if metal_status == "metal_explicit_msl_proof_unavailable":
        return "metal_explicit_msl_proof_unavailable"
    return "metal_explicit_msl_proof_failed"


def summarize_metal_proof(metal_result: dict[str, Any]) -> dict[str, Any]:
    frame_stats = metal_result.get("frameStats") if isinstance(metal_result.get("frameStats"), dict) else {}
    return {
        "kind": metal_result.get("kind", "MetalExplicitMslProof"),
        "status": metal_result.get("status"),
        "width": metal_result.get("width") or frame_stats.get("width"),
        "height": metal_result.get("height") or frame_stats.get("height"),
        "nonBlack": frame_stats.get("nonBlack"),
        "varied": frame_stats.get("varied"),
        "actualCompilerRan": bool(metal_result.get("claims", {}).get("actualCompilerRan")),
        "actualMetalRan": bool(metal_result.get("claims", {}).get("actualMetalRan")),
    }


def blocked_missing_native_source_result(
    package: dict[str, Any],
    requested: dict[str, Any],
    donor_source: Any,
) -> dict[str, Any]:
    result = base_result(package, requested)
    result.update({
        "ok": False,
        "status": "blocked_missing_native_source",
        "requestedDrawShader": normalize_requested_draw_shader(requested, donor_source),
        "claims": claim_flags(actual_compiler_ran=False, actual_metal_ran=False),
    })
    return result


def base_result(package: dict[str, Any], requested: dict[str, Any]) -> dict[str, Any]:
    donor_source = requested.get("donorSource") or requested.get("source")
    return {
        "kind": "NativeDrawShaderCompileProof",
        "programId": package.get("programId"),
        "graphId": package.get("graphId"),
        "shaderProgramStatus": package.get("status"),
        "requestedDrawShader": normalize_requested_draw_shader(requested, donor_source),
    }


def default_result(
    package: dict[str, Any] | None,
    requested: dict[str, Any] | None,
    status: str,
) -> dict[str, Any]:
    return {
        "kind": "NativeDrawShaderCompileProof",
        "ok": False,
        "status": status,
        "programId": package.get("programId") if package else None,
        "graphId": package.get("graphId") if package else None,
        "requestedDrawShader": normalize_requested_draw_shader(requested, None) if requested else None,
        "claims": claim_flags(actual_compiler_ran=False, actual_metal_ran=False),
    }


def normalize_requested_draw_shader(
    requested: dict[str, Any],
    donor_source: Any,
) -> dict[str, Any]:
    source = requested.get("source")
    return {
        "source": source,
        "sourceLanguage": requested.get("language"),
        "vertexEntry": requested.get("vertexShaderEntry") or requested.get("vertexEntry"),
        "fragmentEntry": requested.get("pixelShaderEntry") or requested.get("fragmentEntry"),
        "sourceDerivedFrom": requested.get("sourceDerivedFrom") or source,
        "donorSource": donor_source,
        "compileParity": requested.get("compileParity"),
    }


def claim_flags(
    actual_compiler_ran: bool,
    actual_metal_ran: bool,
    native_compile_parity: bool = False,
    explicit_msl_metal_proof: bool = False,
) -> dict[str, bool]:
    return {
        "actualCompilerRan": actual_compiler_ran,
        "actualMetalRan": actual_metal_ran,
        "nativeCompileParity": native_compile_parity,
        "explicitMslMetalProof": explicit_msl_metal_proof,
        "metalParity": False,
        "tixlParity": False,
        "pbrVisualCorrectness": False,
    }


def positive_int(value: Any, fallback: int) -> int:
    try:
        parsed = int(value)
    except (TypeError, ValueError):
        return fallback
    return parsed if parsed > 0 else fallback


def metal_failure_message(metal_result: dict[str, Any], metal_errors: list[dict[str, Any]]) -> str:
    if metal_errors:
        message = metal_errors[0].get("message")
        if message:
            return str(message)
    return str(metal_result.get("message") or metal_result.get("status") or "Metal explicit MSL proof failed")


def resolve_repo_path(repo_root: Path, fixture_path: Path, maybe_path: Any) -> Path | None:
    if not isinstance(maybe_path, str) or not maybe_path:
        return None
    path = Path(maybe_path).expanduser()
    if path.is_absolute():
        return path
    repo_candidate = repo_root / path
    if repo_candidate.exists():
        return repo_candidate.resolve()
    return (fixture_path.parent / path).resolve()


def read_json(path: Path, errors: list[dict[str, Any]], code: str) -> Any | None:
    try:
        return json.loads(path.read_text(encoding="utf8"))
    except Exception as exc:
        errors.append({"code": code, "path": str(path), "message": str(exc)})
        return None


def read_json_quiet(path: Path) -> Any | None:
    try:
        return json.loads(path.read_text(encoding="utf8"))
    except Exception:
        return None


def write_artifacts(
    out_dir: Path,
    result: dict[str, Any],
    trace: list[dict[str, Any]],
    errors: list[dict[str, Any]],
) -> None:
    write_json(out_dir / "native_draw_shader_compile_result.json", result)
    write_json(out_dir / "native_draw_shader_compile_trace.json", trace)
    write_json(out_dir / "native_draw_shader_compile_errors.json", errors)


def write_json(path: Path, payload: Any) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload, indent=2, ensure_ascii=False) + "\n", encoding="utf8")


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


def clean_unavailable_message(text: str) -> str:
    cleaned = clean_text(text)
    if "Traceback" in cleaned:
        return "Metal explicit MSL proof unavailable."
    return cleaned or "Metal explicit MSL proof unavailable."


if __name__ == "__main__":
    raise SystemExit(main())

#!/usr/bin/env python3
"""
Replay a ShaderProgram requestedDrawShader through a compile-only native proof gate.

This shell does not invoke Metal or render. Without an explicit nativeSource it
must block donor HLSL metadata from being mistaken for native compile parity.
"""

from __future__ import annotations

import json
import sys
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
        accepted_result, native_errors = accept_explicit_native_source(package, requested, native_source)
        errors.extend(native_errors)
        trace.append({
            "op": "validateExplicitNativeSource",
            "language": native_source.get("language"),
            "accepted": not native_errors,
        })
        if native_errors:
            accepted_result["ok"] = False
            accepted_result["status"] = "native_source_invalid"
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
    package: dict[str, Any],
    requested: dict[str, Any],
    native_source: dict[str, Any],
) -> tuple[dict[str, Any], list[dict[str, Any]]]:
    errors: list[dict[str, Any]] = []
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
        "status": "accepted_explicit_native_source" if not errors else "native_source_invalid",
        "nativeSource": {
            "language": language,
            "vertexEntry": vertex_entry,
            "fragmentEntry": fragment_entry,
            "sourceLength": len(source_text) if isinstance(source_text, str) else 0,
        },
        "claims": claim_flags(actual_compiler_ran=False),
    })
    return result, errors


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
        "claims": claim_flags(actual_compiler_ran=False),
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
        "claims": claim_flags(actual_compiler_ran=False),
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


def claim_flags(actual_compiler_ran: bool) -> dict[str, bool]:
    return {
        "actualCompilerRan": actual_compiler_ran,
        "nativeCompileParity": False,
        "metalParity": False,
        "tixlParity": False,
        "pbrVisualCorrectness": False,
    }


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


if __name__ == "__main__":
    raise SystemExit(main())

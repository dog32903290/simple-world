#!/usr/bin/env python3
"""
Package a ShaderGraph source artifact into a ShaderProgram contract.

This shell does not render. It proves the boundary between generated shader
source and backend compile/validation inputs.
"""

from __future__ import annotations

import hashlib
import json
import subprocess
import sys
from pathlib import Path
from typing import Any

SUPPORTED_LANGUAGES = {"GLSL_ES_300"}
SUPPORTED_BACKENDS = {"webgl2ShaderProbe"}
SUPPORTED_STAGES = {"fragment"}
BINDING_KINDS = ["uniforms", "samplers", "textures", "storageBuffers", "constantBuffers"]


def main() -> int:
    if len(sys.argv) not in {3, 4}:
        print("usage: shader_program_shell.py <shader_program_contract.graph.json> <out_dir> [draw_command.json]", file=sys.stderr)
        return 2

    fixture_path = Path(sys.argv[1]).expanduser().resolve()
    out_dir = Path(sys.argv[2]).expanduser().resolve()
    draw_command_path = Path(sys.argv[3]).expanduser().resolve() if len(sys.argv) == 4 else None
    out_dir.mkdir(parents=True, exist_ok=True)

    errors: list[dict[str, Any]] = []
    fixture = read_json(fixture_path, errors, "shader_program.fixture_read_failed")
    draw_command = read_json(draw_command_path, errors, "shader_program.draw_command_read_failed") if draw_command_path else None
    if fixture is None or errors:
        write_artifacts(out_dir, "", {}, {}, {}, default_last_valid_policy(), errors)
        return 1

    package, bindings, compile_request, last_valid_policy, shader_source, run_errors = package_shader_program(
        fixture,
        fixture_path,
        out_dir,
        draw_command,
    )
    errors.extend(run_errors)
    write_artifacts(out_dir, shader_source, package, bindings, compile_request, last_valid_policy, errors)
    return 0 if not errors else 1


def package_shader_program(
    fixture: dict[str, Any],
    fixture_path: Path,
    out_dir: Path,
    draw_command: dict[str, Any] | None = None,
) -> tuple[dict[str, Any], dict[str, Any], dict[str, Any], dict[str, Any], str, list[dict[str, Any]]]:
    errors: list[dict[str, Any]] = []
    repo_root = Path(__file__).resolve().parents[3]
    program = fixture.get("shaderProgram", {})
    source_dir = out_dir / "source"

    language = program.get("language")
    compile_backend = program.get("compileBackend")
    if language not in SUPPORTED_LANGUAGES:
        errors.append({
            "code": "shader_program.unsupported_language",
            "language": language,
            "supported": sorted(SUPPORTED_LANGUAGES),
        })
    if compile_backend not in SUPPORTED_BACKENDS:
        errors.append({
            "code": "shader_program.unsupported_backend",
            "compileBackend": compile_backend,
            "supported": sorted(SUPPORTED_BACKENDS),
        })

    source_graph = resolve_repo_path(repo_root, fixture_path, program.get("sourceGraph"))
    source_compiler = resolve_repo_path(repo_root, fixture_path, program.get("sourceCompiler"))
    shader_source = ""
    source_hash = ""

    if source_graph is None:
        errors.append({"code": "shader_program.missing_source_graph"})
    if source_compiler is None:
        errors.append({"code": "shader_program.missing_source_compiler"})

    if source_graph is not None and source_compiler is not None:
        compile_result = subprocess.run(
            ["python3", str(source_compiler), str(source_graph), str(source_dir)],
            cwd=repo_root,
            text=True,
            capture_output=True,
            check=False,
        )
        if compile_result.returncode != 0:
            errors.append({
                "code": "shader_program.source_compile_failed",
                "status": compile_result.returncode,
                "stderr": compile_result.stderr,
                "stdout": compile_result.stdout,
            })
        shader_path = source_dir / program.get("sourceArtifact", "shader_source.glsl")
        if shader_path.exists():
            shader_source = shader_path.read_text(encoding="utf8")
            source_hash = hashlib.sha256(shader_source.encode("utf8")).hexdigest()
        else:
            errors.append({
                "code": "shader_program.missing_source_artifact",
                "artifact": str(shader_path),
            })

    stages = program.get("stages", [])
    for stage in stages:
        stage_name = stage.get("stage")
        entry_point = stage.get("entryPoint")
        if stage_name not in SUPPORTED_STAGES:
            errors.append({
                "code": "shader_program.unsupported_stage",
                "stage": stage_name,
                "supported": sorted(SUPPORTED_STAGES),
            })
        if entry_point and entry_point not in shader_source:
            errors.append({
                "code": "shader_program.missing_entry_point",
                "stage": stage_name,
                "entryPoint": entry_point,
            })

    for symbol in program.get("entrySymbols", []):
        if symbol not in shader_source:
            errors.append({
                "code": "shader_program.missing_entry_symbol",
                "symbol": symbol,
            })

    raw_bindings = program.get("bindings", {})
    bindings = normalize_bindings(raw_bindings, errors)
    requested_draw_shader = normalize_draw_shader_request(draw_command, errors)
    last_valid_policy = program.get("lastValidPolicy") or default_last_valid_policy()

    package = {
        "programId": program.get("programId"),
        "graphId": fixture.get("graphId"),
        "sourceGraph": program.get("sourceGraph"),
        "sourceArtifact": program.get("sourceArtifact"),
        "sourceHash": source_hash,
        "language": language,
        "compileBackend": compile_backend,
        "compileMode": program.get("compileMode"),
        "stages": stages,
        "requestedDrawShader": requested_draw_shader,
        "entrySymbols": program.get("entrySymbols", []),
        "status": "ok" if not errors else "error",
    }
    compile_request = {
        "programId": program.get("programId"),
        "backendRole": compile_backend,
        "language": language,
        "sourceHash": source_hash,
        "stages": stages,
        "requestedDrawShader": requested_draw_shader,
        "bindings": bindings,
        "mode": program.get("compileMode"),
        "actualCompileProof": "docs/runtime/scripts/check_shader_webgl.js",
    }
    return package, bindings, compile_request, last_valid_policy, shader_source, errors


def normalize_draw_shader_request(
    draw_command: dict[str, Any] | None,
    errors: list[dict[str, Any]],
) -> dict[str, Any] | None:
    if draw_command is None:
        return None
    if draw_command.get("ok") is not True:
        errors.append({
            "code": "shader_program.draw_command_not_ok",
            "reason": draw_command.get("reason"),
        })
        return None
    shader_source = draw_command.get("shaderSource")
    vertex_entry = draw_command.get("vertexShaderEntry")
    pixel_entry = draw_command.get("pixelShaderEntry")
    if not shader_source:
        errors.append({"code": "shader_program.draw_shader_source_missing"})
    if not vertex_entry or not pixel_entry:
        errors.append({
            "code": "shader_program.draw_shader_stage_missing",
            "vertexShaderEntry": vertex_entry,
            "pixelShaderEntry": pixel_entry,
        })
    return {
        "source": shader_source,
        "language": "HLSL_TIXL_DONOR",
        "vertexShaderEntry": vertex_entry,
        "pixelShaderEntry": pixel_entry,
        "selectedMaterialId": draw_command.get("selectedMaterialId"),
        "constantBuffers": list(draw_command.get("constantBuffers", [])),
        "shaderResources": list(draw_command.get("shaderResources", [])),
        "compileParity": "notClaimed",
    }


def normalize_bindings(raw_bindings: dict[str, Any], errors: list[dict[str, Any]]) -> dict[str, Any]:
    bindings = {}
    for kind in BINDING_KINDS:
        values = raw_bindings.get(kind, [])
        if not isinstance(values, list):
            errors.append({
                "code": "shader_program.invalid_binding_list",
                "kind": kind,
                "actualType": type(values).__name__,
            })
            values = []
        seen_names = set()
        normalized = []
        for index, binding in enumerate(values):
            if not isinstance(binding, dict):
                errors.append({
                    "code": "shader_program.invalid_binding",
                    "kind": kind,
                    "index": index,
                })
                continue
            name = binding.get("name") or binding.get("id")
            if not name:
                errors.append({
                    "code": "shader_program.binding_missing_name",
                    "kind": kind,
                    "index": index,
                })
                continue
            if name in seen_names:
                errors.append({
                    "code": "shader_program.duplicate_binding",
                    "kind": kind,
                    "name": name,
                })
            seen_names.add(name)
            normalized.append(binding)
        bindings[kind] = normalized
    return bindings


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


def default_last_valid_policy() -> dict[str, Any]:
    return {
        "onCompileError": "keepLastValidProgram",
        "whenNoPreviousProgram": "publishNoProgram",
        "diagnostics": "publishErrors",
        "replaceLiveProgram": False,
    }


def read_json(path: Path, errors: list[dict[str, Any]], code: str) -> Any | None:
    try:
        return json.loads(path.read_text(encoding="utf8"))
    except Exception as exc:
        errors.append({"code": code, "path": str(path), "message": str(exc)})
        return None


def write_artifacts(
    out_dir: Path,
    shader_source: str,
    package: dict[str, Any],
    bindings: dict[str, Any],
    compile_request: dict[str, Any],
    last_valid_policy: dict[str, Any],
    errors: list[dict[str, Any]],
) -> None:
    write_text(out_dir / "shader_source.glsl", shader_source)
    write_json(out_dir / "shader_program_package.json", package)
    write_json(out_dir / "shader_program_bindings.json", bindings)
    write_json(out_dir / "shader_program_compile_request.json", compile_request)
    write_json(out_dir / "shader_program_last_valid_policy.json", last_valid_policy)
    write_json(out_dir / "shader_program_errors.json", errors)


def write_text(path: Path, payload: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(payload, encoding="utf8")


def write_json(path: Path, payload: Any) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload, indent=2, ensure_ascii=False) + "\n", encoding="utf8")


if __name__ == "__main__":
    raise SystemExit(main())

#!/usr/bin/env python3
"""
Run the NativeRendererBackend interface proof.

This shell does not render with Metal. It packages a ShaderProgram, applies the
my-world-style backend lifecycle, and emits inspectable status artifacts.
"""

from __future__ import annotations

import json
import subprocess
import sys
from pathlib import Path
from typing import Any


def main() -> int:
    if len(sys.argv) != 3:
        print("usage: native_renderer_backend_interface_shell.py <native_renderer_backend_interface.graph.json> <out_dir>", file=sys.stderr)
        return 2

    fixture_path = Path(sys.argv[1]).expanduser().resolve()
    out_dir = Path(sys.argv[2]).expanduser().resolve()
    out_dir.mkdir(parents=True, exist_ok=True)

    errors: list[dict[str, Any]] = []
    fixture = read_json(fixture_path, errors, "native_backend.fixture_read_failed")
    if fixture is None:
        write_artifacts(out_dir, {}, {}, {}, {}, {}, errors)
        return 1

    artifacts, run_errors = run_interface_proof(fixture, fixture_path, out_dir)
    errors.extend(run_errors)
    write_artifacts(
        out_dir,
        artifacts["interface"],
        artifacts["compile_result"],
        artifacts["backend_status"],
        artifacts["frame_input"],
        artifacts["captured_frame"],
        errors,
    )
    return 0 if not errors else 1


def run_interface_proof(fixture: dict[str, Any], fixture_path: Path, out_dir: Path) -> tuple[dict[str, Any], list[dict[str, Any]]]:
    repo_root = Path(__file__).resolve().parents[3]
    shader_program_fixture = resolve_repo_path(repo_root, fixture_path, fixture.get("shaderProgramFixture"))
    draw_command_artifact = resolve_repo_path(repo_root, fixture_path, fixture.get("drawCommandArtifact"))
    shader_program_dir = out_dir / "shader_program"
    errors: list[dict[str, Any]] = []
    shader_package: dict[str, Any] = {}
    shader_errors: list[dict[str, Any]] = []

    if shader_program_fixture is None:
        errors.append({"code": "native_backend.missing_shader_program_fixture"})
    else:
        shader_shell = repo_root / "docs/runtime/scripts/shader_program_shell.py"
        shader_args = ["python3", str(shader_shell), str(shader_program_fixture), str(shader_program_dir)]
        if draw_command_artifact is not None:
            shader_args.append(str(draw_command_artifact))
        result = subprocess.run(
            shader_args,
            cwd=repo_root,
            text=True,
            capture_output=True,
            check=False,
        )
        shader_package = read_json_file(shader_program_dir / "shader_program_package.json", {})
        shader_errors = read_json_file(shader_program_dir / "shader_program_errors.json", [])
        if result.returncode != 0:
            errors.append({
                "code": "native_backend.shader_program_package_failed",
                "status": result.returncode,
                "shaderErrors": shader_errors,
            })

    backend = fixture.get("backend", {})
    requested_frame = fixture.get("requestedFrame", {})
    initial_state = backend.get("initialState", {})
    has_previous_valid = bool(initial_state.get("previousValidProgram"))
    package_ok = shader_package.get("status") == "ok" and not shader_errors and not errors
    compile_result = compile_shader_program(backend, shader_package, package_ok, has_previous_valid, shader_errors)
    native_draw_boundary = evaluate_native_draw_boundary(backend, shader_package)
    frame_input = build_frame_input(requested_frame)
    backend_status = build_backend_status(backend, compile_result, requested_frame, native_draw_boundary)
    captured_frame = build_captured_frame(backend, compile_result, frame_input)

    interface = {
        "kind": "NativeRendererBackendInterface",
        "sourceDonor": "my-world RenderBackend.h",
        "importsOldUi": False,
        "methods": [
            "compileShader(shaderProgramPackage)",
            "resize(width,height,scale)",
            "renderFrame(frameInput)",
            "captureFrame()",
            "release()",
            "backendStatus()",
        ],
        "acceptedProgramShape": [
            "sourceHash",
            "language",
            "stages",
            "entrySymbols",
            "bindings",
            "lastValidPolicy",
            "requestedDrawShader",
        ],
        "operationsRun": [] if not package_ok else ["compileShader", "resize", "renderFrame", "captureFrame"],
        "shaderProgramId": shader_package.get("programId"),
        "nativeDrawBoundary": native_draw_boundary,
    }

    if not compile_result["ok"]:
        captured_frame["ok"] = False
        captured_frame["status"] = "not_captured"
        captured_frame["message"] = "compile failed; no new frame captured"

    return {
        "interface": interface,
        "compile_result": compile_result,
        "backend_status": backend_status,
        "frame_input": frame_input,
        "captured_frame": captured_frame,
    }, errors


def evaluate_native_draw_boundary(
    backend: dict[str, Any],
    shader_package: dict[str, Any],
) -> dict[str, Any]:
    requested = shader_package.get("requestedDrawShader")
    supported_languages = set(backend.get("capabilities", {}).get("shaderLanguages", []))
    future_languages = set(backend.get("capabilities", {}).get("futureNativeLanguages", []))
    if requested is None:
        return {
            "kind": "NativeDrawShaderBoundary",
            "present": False,
            "status": "notRequested",
        }
    language = requested.get("language")
    native_source = requested.get("source")
    native_supported = language in supported_languages
    future_supported = language in future_languages or language == "HLSL_TIXL_DONOR"
    return {
        "kind": "NativeDrawShaderBoundary",
        "present": True,
        "status": "compileParityNotClaimed" if not native_supported else "supported",
        "source": native_source,
        "language": language,
        "vertexShaderEntry": requested.get("vertexShaderEntry"),
        "pixelShaderEntry": requested.get("pixelShaderEntry"),
        "selectedMaterialId": requested.get("selectedMaterialId"),
        "compileParity": requested.get("compileParity"),
        "backendCanCompileNow": native_supported,
        "futureNativeCandidate": future_supported,
    }


def compile_shader_program(
    backend: dict[str, Any],
    shader_package: dict[str, Any],
    package_ok: bool,
    has_previous_valid: bool,
    shader_errors: list[dict[str, Any]],
) -> dict[str, Any]:
    backend_name = backend.get("backendName", backend.get("id"))
    if package_ok:
        return {
            "ok": True,
            "message": "compiled",
            "status": "compiled",
            "preservesLastValidFrame": False,
            "backendName": backend_name,
            "programId": shader_package.get("programId"),
            "sourceHash": shader_package.get("sourceHash"),
            "language": shader_package.get("language"),
        }
    return {
        "ok": False,
        "message": "compile failed; keeping last valid frame" if has_previous_valid else "compile failed; no valid program",
        "status": "compile_failed",
        "preservesLastValidFrame": has_previous_valid,
        "backendName": backend_name,
        "programId": shader_package.get("programId"),
        "sourceHash": shader_package.get("sourceHash"),
        "language": shader_package.get("language"),
        "diagnostics": shader_errors,
    }


def build_frame_input(requested_frame: dict[str, Any]) -> dict[str, Any]:
    return {
        "kind": "RenderFrameInput",
        "timeSeconds": requested_frame.get("timeSeconds", 0.0),
        "frameIndex": requested_frame.get("frameIndex", 0),
        "loudness": requested_frame.get("loudness", 0.0),
        "resolution": requested_frame.get("resolution"),
        "viewportScale": requested_frame.get("viewportScale", 1),
    }


def build_backend_status(
    backend: dict[str, Any],
    compile_result: dict[str, Any],
    requested_frame: dict[str, Any],
    native_draw_boundary: dict[str, Any],
) -> dict[str, Any]:
    resolution = requested_frame.get("resolution", {})
    return {
        "kind": "RenderBackendStatus",
        "backendName": compile_result.get("backendName"),
        "lastOperation": "render" if compile_result.get("ok") else "compile",
        "status": "rendered" if compile_result.get("ok") else compile_result.get("status"),
        "lastSuccessfulCompileMessage": "compiled" if compile_result.get("ok") else "",
        "lastError": "" if compile_result.get("ok") else compile_result.get("message"),
        "hasRenderableProgram": bool(compile_result.get("ok")) or bool(backend.get("initialState", {}).get("previousValidProgram")),
        "preservesLastValidFrame": bool(compile_result.get("preservesLastValidFrame")),
        "viewportWidth": resolution.get("width", 0),
        "viewportHeight": resolution.get("height", 0),
        "viewportScale": requested_frame.get("viewportScale", 1),
        "nativeDrawShaderStatus": native_draw_boundary.get("status"),
        "nativeDrawShaderSource": native_draw_boundary.get("source"),
        "nativeDrawShaderCanCompileNow": native_draw_boundary.get("backendCanCompileNow"),
    }


def build_captured_frame(backend: dict[str, Any], compile_result: dict[str, Any], frame_input: dict[str, Any]) -> dict[str, Any]:
    sample = build_sample_pixels(float(frame_input.get("loudness", 0.0)), int(frame_input.get("frameIndex", 0)))
    return {
        "kind": "CapturedFrame",
        "ok": bool(compile_result.get("ok")),
        "status": "captured" if compile_result.get("ok") else "not_captured",
        "message": "deterministic interface sample, not native GPU readback",
        "backendName": backend.get("backendName", backend.get("id")),
        "requestedResolution": frame_input.get("resolution"),
        "sampleWidth": 4,
        "sampleHeight": 4,
        "rgbaSample": sample,
        "nonBlackSample": any(pixel[:3] != [0, 0, 0] for pixel in sample),
    }


def build_sample_pixels(loudness: float, frame_index: int) -> list[list[int]]:
    base = max(8, min(255, int(48 + loudness * 160)))
    return [
        [base, (frame_index * 7 + index * 13) % 255, (base + index * 17) % 255, 255]
        for index in range(16)
    ]


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


def read_json_file(path: Path, fallback: Any) -> Any:
    try:
        return json.loads(path.read_text(encoding="utf8"))
    except Exception:
        return fallback


def write_artifacts(
    out_dir: Path,
    interface: dict[str, Any],
    compile_result: dict[str, Any],
    backend_status: dict[str, Any],
    frame_input: dict[str, Any],
    captured_frame: dict[str, Any],
    errors: list[dict[str, Any]],
) -> None:
    write_json(out_dir / "native_backend_interface.json", interface)
    write_json(out_dir / "shader_compile_result.json", compile_result)
    write_json(out_dir / "backend_status.json", backend_status)
    write_json(out_dir / "render_frame_input.json", frame_input)
    write_json(out_dir / "captured_frame_contract.json", captured_frame)
    write_json(out_dir / "native_renderer_backend_errors.json", errors)


def write_json(path: Path, payload: Any) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload, indent=2, ensure_ascii=False) + "\n", encoding="utf8")


if __name__ == "__main__":
    raise SystemExit(main())

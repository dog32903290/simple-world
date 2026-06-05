#!/usr/bin/env python3
"""Build and compile the native ShaderIR expression core proof."""

from __future__ import annotations

import hashlib
import json
import subprocess
import sys
from pathlib import Path
from typing import Any


RESULT_NAME = "native_shader_ir_expression_core_result.json"
ERRORS_NAME = "native_shader_ir_expression_core_errors.json"
SOURCE_NAME = "generated_expression_core.metal"
ALLOWED_OPS = {"uniform", "uv", "swizzle", "const", "sin", "mul", "add", "smoothstep", "mix", "vec4"}


def main() -> int:
    if len(sys.argv) != 3:
        print("usage: native_shader_ir_expression_core_shell.py <fixture> <out_dir>", file=sys.stderr)
        return 2

    fixture_path = Path(sys.argv[1]).expanduser().resolve()
    out_dir = Path(sys.argv[2]).expanduser().resolve()
    out_dir.mkdir(parents=True, exist_ok=True)
    clear_previous(out_dir)

    errors: list[dict[str, Any]] = []
    diagnostics: list[dict[str, Any]] = []
    fixture = read_json(fixture_path, errors, "shader_expression.fixture_read_failed")
    if fixture is None:
        publish(out_dir, None, False, "fixture_read_failed", {}, {}, {}, diagnostics, errors)
        return 1

    expression = fixture.get("shaderExpression", {})
    validate_expression(expression.get("root", {}), expression.get("uniforms", []), diagnostics)
    expression_ir = build_expression_ir(fixture, diagnostics)
    shader_cache = build_shader_cache(expression_ir)

    if diagnostics:
        errors.append({"code": "shader_expression.codegen_blocked_by_diagnostics", "count": len(diagnostics)})
        publish(out_dir, fixture.get("graphId"), False, "diagnostics_failed", expression_ir, shader_cache, {}, diagnostics, errors)
        return 1

    source = generate_source(expression_ir)
    source_path = out_dir / SOURCE_NAME
    source_path.write_text(source, encoding="utf8")
    compile_artifact = compile_metal(source_path, out_dir)
    if compile_artifact.get("status") != "compiled":
        errors.append({"code": "shader_expression.metal_compile_failed", "message": compile_artifact.get("stderr", "")})

    ok = not errors
    publish(out_dir, fixture.get("graphId"), ok, "expression_core_ready" if ok else "compile_failed", expression_ir, shader_cache, compile_artifact, diagnostics, errors)
    return 0 if ok else 1


def validate_expression(expr: dict[str, Any], uniforms: list[dict[str, Any]], diagnostics: list[dict[str, Any]]) -> None:
    uniform_names = {uniform.get("name") for uniform in uniforms}
    walk_expression(expr, diagnostics, uniform_names)


def walk_expression(expr: dict[str, Any], diagnostics: list[dict[str, Any]], uniform_names: set[str]) -> None:
    op = expr.get("op")
    if op not in ALLOWED_OPS:
        diagnostics.append({"code": "shader_expression.unsupported_op", "op": op, "severity": "error"})
        return
    if op == "uniform" and expr.get("name") not in uniform_names:
        diagnostics.append({"code": "shader_expression.unknown_uniform", "name": expr.get("name"), "severity": "error"})
    if op == "swizzle":
        if expr.get("field") not in {"x", "y", "z", "w", "r", "g", "b", "a"}:
            diagnostics.append({"code": "shader_expression.unsupported_swizzle", "field": expr.get("field"), "severity": "error"})
        walk_expression(expr.get("value", {}), diagnostics, uniform_names)
        return
    for arg in expr.get("args", []):
        if isinstance(arg, dict):
            walk_expression(arg, diagnostics, uniform_names)


def build_expression_ir(fixture: dict[str, Any], diagnostics: list[dict[str, Any]]) -> dict[str, Any]:
    expression = fixture.get("shaderExpression", {})
    return {
        "kind": "ShaderExpressionIR",
        "version": "0.1-expression-core",
        "name": expression.get("name", "expression_core_fragment"),
        "allowedOps": sorted(ALLOWED_OPS),
        "uniforms": expression.get("uniforms", []),
        "root": expression.get("root", {}),
        "diagnosticCount": len(diagnostics),
    }


def build_shader_cache(expression_ir: dict[str, Any]) -> dict[str, Any]:
    payload = json.dumps(expression_ir, sort_keys=True, separators=(",", ":")).encode("utf8")
    return {
        "kind": "ShaderExpressionCache",
        "generatedSource": SOURCE_NAME,
        "entries": [
            {
                "name": expression_ir.get("name"),
                "cacheKey": "expr:" + hashlib.sha1(payload).hexdigest(),
                "stage": "fragment",
                "source": SOURCE_NAME,
            }
        ],
    }


def generate_source(expression_ir: dict[str, Any]) -> str:
    uniforms = expression_ir.get("uniforms", [])
    uniform_fields = "\n".join(f"    {uniform_type(uniform)} {uniform['name']};" for uniform in uniforms)
    expr_source = emit_expr(expression_ir.get("root", {}))
    return f"""#include <metal_stdlib>
using namespace metal;

struct VertexOut {{
    float4 position [[position]];
    float2 uv;
}};

struct ExpressionUniforms {{
{uniform_fields}
}};

vertex VertexOut expression_core_vertex(uint vertexID [[vertex_id]])
{{
    float2 positions[3] = {{ float2(-1.0, -1.0), float2(3.0, -1.0), float2(-1.0, 3.0) }};
    VertexOut out;
    out.position = float4(positions[vertexID], 0.0, 1.0);
    out.uv = positions[vertexID] * 0.5 + 0.5;
    return out;
}}

fragment float4 expression_core_fragment(VertexOut in [[stage_in]], constant ExpressionUniforms& uniforms [[buffer(0)]])
{{
    return {expr_source};
}}
"""


def uniform_type(uniform: dict[str, Any]) -> str:
    if uniform.get("type") in {"float", "float2", "float3", "float4"}:
        return str(uniform["type"])
    return "float"


def emit_expr(expr: dict[str, Any]) -> str:
    op = expr.get("op")
    if op == "uniform":
        return f"uniforms.{expr['name']}"
    if op == "uv":
        return "in.uv"
    if op == "swizzle":
        return f"({emit_expr(expr['value'])}).{expr['field']}"
    if op == "const":
        return emit_const(expr)
    if op == "sin":
        return f"sin({emit_expr(expr['args'][0])})"
    if op == "mul":
        return f"({emit_expr(expr['args'][0])} * {emit_expr(expr['args'][1])})"
    if op == "add":
        return f"({emit_expr(expr['args'][0])} + {emit_expr(expr['args'][1])})"
    if op == "smoothstep":
        return f"smoothstep({emit_expr(expr['args'][0])}, {emit_expr(expr['args'][1])}, {emit_expr(expr['args'][2])})"
    if op == "mix":
        return f"mix({emit_expr(expr['args'][0])}, {emit_expr(expr['args'][1])}, {emit_expr(expr['args'][2])})"
    if op == "vec4":
        return f"float4({emit_expr(expr['args'][0])}, {emit_expr(expr['args'][1])})"
    raise ValueError(f"unsupported expression op: {op}")


def emit_const(expr: dict[str, Any]) -> str:
    value = expr.get("value")
    value_type = expr.get("type")
    if value_type in {"float2", "float3", "float4"}:
        return f"{value_type}({', '.join(format_float(item) for item in value)})"
    return format_float(value)


def format_float(value: Any) -> str:
    return f"{float(value):.7g}"


def compile_metal(source_path: Path, out_dir: Path) -> dict[str, Any]:
    repo_root = Path(__file__).resolve().parents[3]
    probe_source = repo_root / "docs/runtime/native/native_shader_ir_expression_core_compile_probe.mm"
    probe_binary = out_dir / "native_shader_ir_expression_core_compile_probe"
    command = [
        "xcrun",
        "--sdk",
        "macosx",
        "clang++",
        "-std=c++17",
        "-fobjc-arc",
        str(probe_source),
        "-framework",
        "Foundation",
        "-framework",
        "Metal",
        "-o",
        str(probe_binary),
    ]
    compiled = subprocess.run(command, text=True, capture_output=True, check=False)
    if compiled.returncode != 0:
        return {
            "kind": "MetalCompileArtifact",
            "status": "compile_probe_build_failed",
            "returncode": compiled.returncode,
            "output": None,
            "stderr": sanitize(compiled.stderr or compiled.stdout),
        }

    run = subprocess.run([str(probe_binary), str(source_path)], text=True, capture_output=True, check=False)
    try:
        payload = json.loads(run.stdout)
    except Exception:
        payload = {"status": "compile_failed", "message": run.stderr or run.stdout or "probe emitted invalid JSON"}
    return {
        "kind": "MetalCompileArtifact",
        "status": payload.get("status"),
        "returncode": run.returncode,
        "output": "native Metal library" if payload.get("status") == "compiled" else None,
        "actualMetalDeviceCreated": payload.get("actualMetalDeviceCreated", False),
        "actualLibraryCreated": payload.get("actualLibraryCreated", False),
        "stderr": sanitize(payload.get("message", "")),
    }


def sanitize(text: str) -> str:
    return text.replace(str(Path.home()), "~")


def publish(
    out_dir: Path,
    graph_id: str | None,
    ok: bool,
    status: str,
    expression_ir: dict[str, Any],
    shader_cache: dict[str, Any],
    compile_artifact: dict[str, Any],
    diagnostics: list[dict[str, Any]],
    errors: list[dict[str, Any]],
) -> None:
    result = {
        "kind": "NativeShaderIrExpressionCoreProof",
        "graphId": graph_id,
        "ok": ok,
        "status": status,
        "claims": {
            "coreExpressionLanguage": ok,
            "metalCompiled": ok and compile_artifact.get("status") == "compiled",
            "completeShaderLanguage": False,
            "unsafeExpressionBlocked": True,
        },
    }
    write_json(out_dir / RESULT_NAME, result)
    write_json(out_dir / "shader_expression_ir.json", expression_ir)
    write_json(out_dir / "shader_expression_cache.json", shader_cache)
    write_json(out_dir / "metal_compile.json", compile_artifact)
    write_json(out_dir / "diagnostics.json", diagnostics)
    write_json(out_dir / ERRORS_NAME, errors)


def clear_previous(out_dir: Path) -> None:
    for name in [
        RESULT_NAME,
        ERRORS_NAME,
        SOURCE_NAME,
        "native_shader_ir_expression_core_compile_probe",
        "shader_expression_ir.json",
        "shader_expression_cache.json",
        "metal_compile.json",
        "diagnostics.json",
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

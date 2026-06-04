#!/usr/bin/env python3
"""
Turn a MeshPbrDrawCommand artifact into the first inspectable frame artifact.

This is a software proof shell, not a GPU renderer. It proves that the command
artifact emitted by the Material/PBR lane can be consumed by a renderer-shaped
pipeline and produce nonblack output plus diagnostics.
"""

from __future__ import annotations

import json
import sys
from pathlib import Path
from typing import Any


EXPECTED_OPS = ["inputAssembler", "shaderStage", "rasterizer", "outputMerger", "draw"]
EXPECTED_CONSTANT_PREFIX = ["transforms", "context", "pointLights"]
EXPECTED_SHADER_RESOURCE_COUNT = 6


def main() -> int:
    if len(sys.argv) != 3:
        print("usage: native_render_shell.py <mesh_pbr_draw_command.json> <out_dir>", file=sys.stderr)
        return 2

    command_path = Path(sys.argv[1])
    out_dir = Path(sys.argv[2])
    out_dir.mkdir(parents=True, exist_ok=True)

    trace: list[dict[str, Any]] = []
    errors: list[dict[str, Any]] = []

    command = read_command(command_path, errors)
    trace.append({"op": "loadCommand", "path": str(command_path), "ok": command is not None})

    if command is None:
        return write_artifacts(out_dir, trace, errors, None, None)

    errors.extend(validate_command(command))
    trace.append({
        "op": "validateCommand",
        "ok": not errors,
        "meshId": command.get("meshId"),
        "selectedMaterialId": command.get("selectedMaterialId"),
    })

    if errors:
        return write_artifacts(out_dir, trace, errors, None, None)

    trace.extend([
        {"op": "bindInputAssembler", "meshId": command["meshId"]},
        {
            "op": "bindShaderStage",
            "shaderSource": command["shaderSource"],
            "vertexShaderEntry": command["vertexShaderEntry"],
            "pixelShaderEntry": command["pixelShaderEntry"],
            "constantBuffers": command["constantBuffers"],
            "shaderResources": command["shaderResources"],
        },
        {"op": "bindRasterizer", "mode": "solid"},
        {"op": "bindOutputMerger", "target": "frame.ppm"},
        {"op": "draw", "primitive": "triangle", "material": command["selectedMaterialId"]},
    ])

    pixels = render_frame(command, width=320, height=180)
    stats = measure_frame(pixels, width=320, height=180)
    if stats["nonblackPixels"] == 0:
        errors.append({"code": "native_render.black_frame", "message": "Generated frame has no nonblack pixels."})

    trace.append({"op": "writeFrame", "path": "frame.ppm", "stats": stats})
    trace.append({"op": "measureFrame", "ok": stats["nonblackPixels"] > 0})
    return write_artifacts(out_dir, trace, errors, pixels, stats)


def read_command(path: Path, errors: list[dict[str, Any]]) -> dict[str, Any] | None:
    try:
        payload = json.loads(path.read_text(encoding="utf8"))
    except Exception as exc:
        errors.append({"code": "native_render.command_read_failed", "message": str(exc)})
        return None

    if not isinstance(payload, dict):
        errors.append({"code": "native_render.command_not_object"})
        return None
    return payload


def validate_command(command: dict[str, Any]) -> list[dict[str, Any]]:
    errors: list[dict[str, Any]] = []
    if command.get("ok") is not True:
        errors.append({
            "code": "native_render.command_not_ok",
            "message": command.get("reason", "MeshPbrDrawCommand ok flag is not true."),
        })

    for field in [
        "meshId",
        "selectedMaterialId",
        "shaderSource",
        "vertexShaderEntry",
        "pixelShaderEntry",
        "constantBuffers",
        "shaderResources",
        "commandOps",
    ]:
        if field not in command:
            errors.append({"code": "native_render.missing_field", "field": field})

    if command.get("vertexShaderEntry") != "vsMain" or command.get("pixelShaderEntry") != "psMain":
        errors.append({
            "code": "native_render.invalid_shader_stage",
            "vertexShaderEntry": command.get("vertexShaderEntry"),
            "pixelShaderEntry": command.get("pixelShaderEntry"),
        })

    if command.get("commandOps") != EXPECTED_OPS:
        errors.append({
            "code": "native_render.invalid_command_ops",
            "expected": EXPECTED_OPS,
            "actual": command.get("commandOps"),
        })

    constant_buffers = command.get("constantBuffers", [])
    shader_resources = command.get("shaderResources", [])
    if (
        not isinstance(constant_buffers, list)
        or constant_buffers[:3] != EXPECTED_CONSTANT_PREFIX
        or len(constant_buffers) < 4
        or not str(constant_buffers[3]).startswith("pbr:")
    ):
        errors.append({
            "code": "native_render.incomplete_pbr_binding",
            "slot": "constantBuffers",
            "value": constant_buffers,
        })

    if (
        not isinstance(shader_resources, list)
        or len(shader_resources) != EXPECTED_SHADER_RESOURCE_COUNT
        or any(resource is None for resource in shader_resources)
    ):
        errors.append({
            "code": "native_render.incomplete_pbr_binding",
            "slot": "shaderResources",
            "value": shader_resources,
        })

    return errors


def render_frame(command: dict[str, Any], width: int, height: int) -> list[tuple[int, int, int]]:
    material = command["selectedMaterialId"]
    material_color = color_from_material(material)
    background = (0, 0, 0)
    pixels: list[tuple[int, int, int]] = []
    cx = width / 2
    cy = height / 2
    radius = min(width, height) * 0.32

    for y in range(height):
        for x in range(width):
            dx = abs(x - cx) / radius
            dy = abs(y - cy) / radius
            if dx + dy < 1.0:
                shade = 1.0 - 0.35 * dy + 0.15 * (x / max(width - 1, 1))
                pixels.append(tuple(clamp_channel(channel * shade) for channel in material_color))
            else:
                pixels.append(background)
    return pixels


def color_from_material(material: str) -> tuple[int, int, int]:
    if material == "glass":
        return (96, 184, 255)
    if material == "copper":
        return (255, 122, 56)
    digest = sum(ord(char) for char in material)
    return (80 + digest % 120, 80 + (digest // 3) % 120, 80 + (digest // 7) % 120)


def clamp_channel(value: float) -> int:
    return max(0, min(255, int(round(value))))


def measure_frame(
    pixels: list[tuple[int, int, int]],
    width: int,
    height: int,
) -> dict[str, Any]:
    nonblack = sum(1 for pixel in pixels if pixel != (0, 0, 0))
    bright = sum(1 for pixel in pixels if max(pixel) > 32)
    avg = [
        round(sum(pixel[index] for pixel in pixels) / len(pixels), 4)
        for index in range(3)
    ]
    return {
        "width": width,
        "height": height,
        "pixelCount": len(pixels),
        "nonblackPixels": nonblack,
        "brightPixels": bright,
        "averageRgb": avg,
    }


def write_artifacts(
    out_dir: Path,
    trace: list[dict[str, Any]],
    errors: list[dict[str, Any]],
    pixels: list[tuple[int, int, int]] | None,
    stats: dict[str, Any] | None,
) -> int:
    write_json(out_dir / "native_render_trace.json", trace)
    write_json(out_dir / "native_render_errors.json", errors)
    write_json(out_dir / "frame_stats.json", stats or {})
    if pixels is not None and stats is not None:
        write_ppm(out_dir / "frame.ppm", pixels, stats["width"], stats["height"])
    else:
        (out_dir / "frame.ppm").write_text("", encoding="utf8")
    return 0 if not errors else 1


def write_ppm(path: Path, pixels: list[tuple[int, int, int]], width: int, height: int) -> None:
    header = f"P3\n{width} {height}\n255\n"
    rows = []
    for y in range(height):
        start = y * width
        row = pixels[start:start + width]
        rows.append(" ".join(f"{r} {g} {b}" for r, g, b in row))
    path.write_text(header + "\n".join(rows) + "\n", encoding="ascii")


def write_json(path: Path, payload: Any) -> None:
    path.write_text(json.dumps(payload, indent=2, ensure_ascii=False) + "\n", encoding="utf8")


if __name__ == "__main__":
    raise SystemExit(main())

#!/usr/bin/env python3
"""
Audit the ignored TiXL mesh draw HLSL donor source without compiling it.

The artifacts intentionally contain only path-clean source metadata, hashes,
line counts, include edges, entry/resource summaries, template holes, and
semantic blockers. They do not copy donor source text and do not claim native
translation, renderer integration, TiXL parity, or PBR visual correctness.
"""

from __future__ import annotations

import hashlib
import json
import re
import sys
from pathlib import Path
from typing import Any


DEFAULT_DONOR_SOURCE = "external/tixl/Operators/Lib/Assets/shaders/3d/mesh/mesh-Draw.hlsl"
RESULT_NAME = "tixl_mesh_draw_shader_source_audit_result.json"
TRACE_NAME = "tixl_mesh_draw_shader_source_audit_trace.json"
ERRORS_NAME = "tixl_mesh_draw_shader_source_audit_errors.json"

INCLUDE_RE = re.compile(r'^[ \t]*#include[ \t]+"([^"]+)"', re.MULTILINE)
ENTRY_RE = re.compile(r"\b([A-Za-z_][\w<>]*)\s+(vsMain|psMain)\s*\(")
CBUFFER_RE = re.compile(
    r"\bcbuffer\s+([A-Za-z_]\w*)\s*:\s*register\((b\d+)\)\s*\{(?P<body>.*?)\}\s*;?",
    re.DOTALL,
)
RESOURCE_RE = re.compile(
    r"^\s*((?:RW)?StructuredBuffer|(?:RW)?Buffer|Texture(?:1D|2D|3D|Cube))"
    r"(?:<([^>]+)>)?\s+([A-Za-z_]\w*)\s*:\s*register\((t\d+|u\d+)\)",
    re.MULTILINE,
)
SAMPLER_RE = re.compile(r"^\s*sampler\w*\s+([A-Za-z_]\w*)\s*:\s*register\((s\d+)\)", re.MULTILINE)
TEMPLATE_HOLE_RE = re.compile(r"/\*\{([^}]+)\}\*/")
STRUCT_RE = re.compile(r"\bstruct\s+([A-Za-z_]\w*)\b")
FUNCTION_RE = re.compile(
    r"^\s*(?:inline\s+)?([A-Za-z_][\w]*(?:<[^>]+>)?)\s+([A-Za-z_]\w*)"
    r"\s*\([^;{}]*\)\s*(?::\s*[A-Za-z_]\w*)?\s*\{",
    re.MULTILINE,
)
FUNCTION_NAME_BLOCKLIST = {
    "for",
    "if",
    "return",
    "while",
}


def main() -> int:
    if len(sys.argv) != 3:
        print("usage: tixl_mesh_draw_shader_source_audit_shell.py <fixture.graph.json> <out_dir>", file=sys.stderr)
        return 2

    fixture_path = Path(sys.argv[1]).expanduser().resolve()
    out_dir = Path(sys.argv[2]).expanduser().resolve()
    out_dir.mkdir(parents=True, exist_ok=True)

    repo_root = Path(__file__).resolve().parents[3]
    trace: list[dict[str, Any]] = [{
        "op": "loadTixlMeshDrawShaderSourceAuditFixture",
        "fixture": display_path(fixture_path, repo_root),
    }]
    errors: list[dict[str, Any]] = []
    fixture = read_json(fixture_path, errors, "tixl_mesh_draw_shader_source_audit.fixture_read_failed")
    if fixture is None:
        result = blocked_result(None, repo_root, "fixture_read_failed")
        write_artifacts(out_dir, result, trace, errors)
        return 1

    result, run_trace, run_errors = run_audit(fixture, fixture_path, repo_root)
    trace.extend(run_trace)
    errors.extend(run_errors)
    trace.append({
        "op": "publishTixlMeshDrawShaderSourceAuditArtifacts",
        "ok": result.get("ok") is True and not errors,
    })
    write_artifacts(out_dir, result, trace, errors)
    return 0 if result.get("ok") is True and not errors else 1


def run_audit(
    fixture: dict[str, Any],
    fixture_path: Path,
    repo_root: Path,
) -> tuple[dict[str, Any], list[dict[str, Any]], list[dict[str, Any]]]:
    errors: list[dict[str, Any]] = []
    trace: list[dict[str, Any]] = []
    donor_path = resolve_path(repo_root, fixture_path, fixture.get("donorSource") or DEFAULT_DONOR_SOURCE)
    include_roots = [
        path for path in (
            resolve_path(repo_root, fixture_path, value)
            for value in fixture.get("includeSearchRoots", [])
        )
        if path is not None
    ]
    if donor_path is None:
        donor_path = repo_root / DEFAULT_DONOR_SOURCE
    trace.append({
        "op": "resolveDonorSource",
        "donorSource": display_path(donor_path, repo_root),
        "exists": donor_path.exists(),
        "includeSearchRoots": [display_path(path, repo_root) for path in include_roots],
    })

    if not donor_path.exists():
        errors.append({
            "code": "tixl_mesh_draw_shader_source_audit.donor_source_missing",
            "donorSource": display_path(donor_path, repo_root),
            "message": "TiXL mesh draw donor source is not available in this checkout.",
        })
        return blocked_result(donor_path, repo_root, "blocked_missing_donor_source"), trace, errors

    files: dict[Path, dict[str, Any]] = {}
    edges: list[dict[str, Any]] = []
    include_errors: list[dict[str, Any]] = []
    audit_file(donor_path, include_roots, repo_root, files, edges, include_errors, set())
    trace.append({
        "op": "parseIncludeGraph",
        "fileCount": len(files),
        "edgeCount": len(edges),
        "unresolvedIncludeCount": len(include_errors),
    })

    summaries = [files[path] for path in sorted(files, key=lambda path: display_path(path, repo_root))]
    donor_summary = files[donor_path.resolve()]
    entry_points = entry_points_from(donor_summary)
    requirements = merge_requirements(summaries)
    missing_entries = [
        name for name, entry in entry_points.items()
        if entry.get("found") is not True
    ]
    blockers = semantic_blockers(donor_summary, summaries, include_errors, missing_entries)
    if missing_entries:
        errors.append({
            "code": "tixl_mesh_draw_shader_source_audit.entry_points_missing",
            "missing": missing_entries,
            "message": "Required TiXL mesh draw shader entry points were not found in the donor source.",
        })
    status = "audited_tixl_mesh_draw_source"
    if include_errors:
        status = "blocked_unresolved_includes"
    elif missing_entries:
        status = "blocked_missing_entry_points"

    result = {
        "kind": "TixlMeshDrawShaderSourceAudit",
        "graphId": fixture.get("graphId"),
        "ok": len(include_errors) == 0 and not missing_entries,
        "status": status,
        "donorSource": donor_summary["source"],
        "includeGraph": {
            "directIncludes": donor_summary["directIncludes"],
            "edges": edges,
            "files": summaries,
        },
        "entryPoints": entry_points,
        "requiredBuffers": requirements["buffers"],
        "resources": requirements["resources"],
        "samplers": requirements["samplers"],
        "constants": requirements["constants"],
        "templateHoles": requirements["templateHoles"],
        "symbolSummary": requirements["symbols"],
        "semanticBlockers": blockers,
        "claims": claim_flags(),
    }
    errors.extend(include_errors)
    return result, trace, errors


def audit_file(
    source_path: Path,
    include_roots: list[Path],
    repo_root: Path,
    files: dict[Path, dict[str, Any]],
    edges: list[dict[str, Any]],
    errors: list[dict[str, Any]],
    stack: set[Path],
) -> None:
    resolved = source_path.resolve()
    if resolved in files:
        return
    if resolved in stack:
        errors.append({
            "code": "tixl_mesh_draw_shader_source_audit.include_cycle",
            "source": display_path(resolved, repo_root),
        })
        return

    stack.add(resolved)
    text = source_path.read_text(encoding="utf8")
    includes = list(INCLUDE_RE.finditer(text))
    direct_includes: list[dict[str, Any]] = []
    summary = summarize_source(source_path, text, repo_root)
    files[resolved] = summary

    for include_match in includes:
        include_name = include_match.group(1)
        include_path = resolve_include(source_path, include_name, include_roots)
        edge = {
            "from": display_path(source_path, repo_root),
            "include": include_name,
            "line": line_number(text, include_match.start()),
            "to": display_path(include_path, repo_root) if include_path is not None else None,
            "resolved": include_path is not None,
        }
        edges.append(edge)
        direct_includes.append(edge)
        if include_path is None:
            errors.append({
                "code": "tixl_mesh_draw_shader_source_audit.include_unresolved",
                "source": display_path(source_path, repo_root),
                "include": include_name,
                "line": edge["line"],
            })
            continue
        audit_file(include_path, include_roots, repo_root, files, edges, errors, stack)

    summary["directIncludes"] = direct_includes
    stack.remove(resolved)


def summarize_source(source_path: Path, text: str, repo_root: Path) -> dict[str, Any]:
    return {
        "source": {
            "path": display_path(source_path, repo_root),
            "sha256": hashlib.sha256(text.encode("utf8")).hexdigest(),
            "lineCount": len(text.splitlines()),
        },
        "directIncludes": [],
        "entries": [
            {"name": match.group(2), "returnType": match.group(1), "line": line_number(text, match.start())}
            for match in ENTRY_RE.finditer(text)
        ],
        "buffers": parse_buffers(text),
        "resources": parse_resources(text),
        "samplers": parse_samplers(text),
        "templateHoles": [
            {"name": match.group(1), "line": line_number(text, match.start())}
            for match in TEMPLATE_HOLE_RE.finditer(text)
        ],
        "symbols": {
            "structs": sorted(set(STRUCT_RE.findall(text))),
            "functions": function_names_from(text),
        },
    }


def parse_buffers(text: str) -> list[dict[str, Any]]:
    buffers = []
    for match in CBUFFER_RE.finditer(text):
        fields = []
        body = strip_comments(match.group("body"))
        for field_match in re.finditer(r"\b([A-Za-z_][\w<>]*(?:\s*x\s*\d+)?)\s+([A-Za-z_]\w*)(\[[^\]]+\])?\s*;", body):
            fields.append({
                "type": re.sub(r"\s+", "", field_match.group(1)),
                "name": field_match.group(2),
                "array": field_match.group(3) or None,
            })
        buffers.append({
            "name": match.group(1),
            "register": match.group(2),
            "fields": fields,
        })
    return buffers


def parse_resources(text: str) -> list[dict[str, Any]]:
    return [
        {
            "kind": match.group(1),
            "elementType": match.group(2),
            "name": match.group(3),
            "register": match.group(4),
        }
        for match in RESOURCE_RE.finditer(text)
    ]


def parse_samplers(text: str) -> list[dict[str, Any]]:
    return [
        {"name": match.group(1), "register": match.group(2)}
        for match in SAMPLER_RE.finditer(text)
    ]


def merge_requirements(summaries: list[dict[str, Any]]) -> dict[str, Any]:
    buffers = unique_by(flatten(summary["buffers"] for summary in summaries), ("name", "register"))
    resources = unique_by(flatten(summary["resources"] for summary in summaries), ("name", "register"))
    samplers = unique_by(flatten(summary["samplers"] for summary in summaries), ("name", "register"))
    template_holes = unique_by(
        flatten(
            {
                "source": summary["source"]["path"],
                "name": hole["name"],
                "line": hole["line"],
            }
            for summary in summaries
            for hole in summary["templateHoles"]
        ),
        ("source", "name", "line"),
    )
    constants = [
        {
            "buffer": buffer["name"],
            "register": buffer["register"],
            "name": field["name"],
            "type": field["type"],
            "array": field["array"],
        }
        for buffer in buffers
        for field in buffer["fields"]
    ]
    symbols = {
        "structs": sorted(set(flatten(summary["symbols"]["structs"] for summary in summaries))),
        "functions": sorted(set(flatten(summary["symbols"]["functions"] for summary in summaries))),
    }
    return {
        "buffers": buffers,
        "resources": resources,
        "samplers": samplers,
        "constants": constants,
        "templateHoles": template_holes,
        "symbols": symbols,
    }


def entry_points_from(summary: dict[str, Any]) -> dict[str, Any]:
    found = {entry["name"]: entry for entry in summary["entries"]}
    return {
        "vsMain": {
            "found": "vsMain" in found,
            "line": found.get("vsMain", {}).get("line"),
            "returnType": found.get("vsMain", {}).get("returnType"),
        },
        "psMain": {
            "found": "psMain" in found,
            "line": found.get("psMain", {}).get("line"),
            "returnType": found.get("psMain", {}).get("returnType"),
        },
    }


def semantic_blockers(
    donor_summary: dict[str, Any],
    summaries: list[dict[str, Any]],
    include_errors: list[dict[str, Any]],
    missing_entries: list[str],
) -> list[dict[str, Any]]:
    blockers = [
        {
            "code": "requires_hlsl_to_msl_translation_lane",
            "message": "Audit records TiXL HLSL source requirements only; no HLSL-to-MSL translator has run.",
        },
        {
            "code": "requires_native_mesh_resource_binding",
            "message": "PbrVertices, FaceIndices, material textures, environment maps, and samplers need a native binding contract before compile parity.",
        },
        {
            "code": "requires_pbr_visual_reference",
            "message": "PBR lighting and IBL resources are summarized but not rendered or visually compared.",
        },
    ]
    if include_errors:
        blockers.append({
            "code": "unresolved_include_graph",
            "count": len(include_errors),
        })
    if missing_entries:
        blockers.append({
            "code": "missing_shader_entry_points",
            "missing": missing_entries,
        })
    if donor_summary["templateHoles"]:
        blockers.append({
            "code": "shader_template_holes_require_tixl_expansion",
            "holes": [hole["name"] for hole in donor_summary["templateHoles"]],
        })
    buffer_names = [buffer["name"] for summary in summaries for buffer in summary["buffers"]]
    duplicates = sorted({name for name in buffer_names if buffer_names.count(name) > 1})
    if duplicates:
        blockers.append({
            "code": "duplicate_buffer_names_need_binding_policy",
            "names": duplicates,
        })
    return blockers


def function_names_from(text: str) -> list[str]:
    names = set()
    for _, function_name in FUNCTION_RE.findall(text):
        if function_name in FUNCTION_NAME_BLOCKLIST:
            continue
        names.add(function_name)
    return sorted(names)


def blocked_result(donor_path: Path | None, repo_root: Path, status: str) -> dict[str, Any]:
    return {
        "kind": "TixlMeshDrawShaderSourceAudit",
        "ok": False,
        "status": status,
        "donorSource": {
            "path": display_path(donor_path, repo_root) if donor_path is not None else None,
        },
        "includeGraph": {"directIncludes": [], "edges": [], "files": []},
        "entryPoints": {
            "vsMain": {"found": False, "line": None, "returnType": None},
            "psMain": {"found": False, "line": None, "returnType": None},
        },
        "requiredBuffers": [],
        "resources": [],
        "samplers": [],
        "constants": [],
        "templateHoles": [],
        "symbolSummary": {"structs": [], "functions": []},
        "semanticBlockers": [{
            "code": "missing_donor_source",
            "message": "Donor source must exist before this source audit can summarize dependencies.",
        }],
        "claims": claim_flags(),
    }


def claim_flags() -> dict[str, bool]:
    return {
        "hlslToMslTranslationProven": False,
        "tixlParity": False,
        "nativeCompileParity": False,
        "pbrVisualCorrectness": False,
    }


def resolve_include(source_path: Path, include_name: str, include_roots: list[Path]) -> Path | None:
    candidates = [source_path.parent / include_name]
    candidates.extend(root / include_name for root in include_roots)
    for candidate in candidates:
        if candidate.exists():
            return candidate.resolve()
    return None


def resolve_path(repo_root: Path, fixture_path: Path, maybe_path: Any) -> Path | None:
    if not isinstance(maybe_path, str) or not maybe_path:
        return None
    path = Path(maybe_path).expanduser()
    if path.is_absolute():
        return path
    repo_candidate = repo_root / path
    if repo_candidate.exists() or str(maybe_path).startswith("external/"):
        return repo_candidate.resolve()
    return (fixture_path.parent / path).resolve()


def read_json(path: Path, errors: list[dict[str, Any]], code: str) -> Any:
    try:
        return json.loads(path.read_text(encoding="utf8"))
    except Exception as exc:
        errors.append({"code": code, "path": display_path(path, Path(__file__).resolve().parents[3]), "message": str(exc)})
        return None


def write_artifacts(out_dir: Path, result: dict[str, Any], trace: list[dict[str, Any]], errors: list[dict[str, Any]]) -> None:
    write_json(out_dir / RESULT_NAME, result)
    write_json(out_dir / TRACE_NAME, trace)
    write_json(out_dir / ERRORS_NAME, errors)


def write_json(path: Path, payload: Any) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload, indent=2, ensure_ascii=False) + "\n", encoding="utf8")


def display_path(path: Path | None, repo_root: Path) -> str | None:
    if path is None:
        return None
    try:
        return str(path.resolve().relative_to(repo_root))
    except ValueError:
        return f"outside_repo/{path.name}"


def line_number(text: str, offset: int) -> int:
    return text.count("\n", 0, offset) + 1


def strip_comments(text: str) -> str:
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.DOTALL)
    return re.sub(r"//.*", "", text)


def flatten(values: Any) -> list[Any]:
    output = []
    for value in values:
        if isinstance(value, list):
            output.extend(value)
        else:
            output.append(value)
    return output


def unique_by(values: list[dict[str, Any]], keys: tuple[str, ...]) -> list[dict[str, Any]]:
    seen = set()
    output = []
    for value in values:
        key = tuple(value.get(name) for name in keys)
        if key in seen:
            continue
        seen.add(key)
        output.append(value)
    return output


if __name__ == "__main__":
    raise SystemExit(main())

#!/usr/bin/env python3
"""Compile and run the C++ graph command contract probe."""

from __future__ import annotations

import json
import subprocess
import sys
import tempfile
from pathlib import Path
from typing import Any


RESULT_NAME = "cpp_graph_command_contract_result.json"
DIAGNOSTICS_NAME = "diagnostics.json"
ARTIFACT_NAMES = {
    RESULT_NAME,
    "graph_document.json",
    "runtime_graph.json",
    DIAGNOSTICS_NAME,
    "reloaded_graph_document.json",
}
LEGACY_BINARY_NAME = "graph_command_contract_probe"


def main() -> int:
    if len(sys.argv) != 3:
        print("usage: cpp_graph_command_contract_shell.py <fixture> <out_dir>", file=sys.stderr)
        return 2

    fixture_path = Path(sys.argv[1]).expanduser().resolve()
    out_dir = Path(sys.argv[2]).expanduser().resolve()
    out_dir.mkdir(parents=True, exist_ok=True)
    clear_output_dir(out_dir)

    repo_root = Path(__file__).resolve().parents[3]
    with tempfile.TemporaryDirectory(prefix="cpp-graph-command-contract-build-") as build_dir:
        binary = Path(build_dir) / "graph_command_contract_probe"
        compile_result = compile_probe(repo_root, binary)
        if compile_result.returncode != 0:
            diagnostics = [{
                "code": "cpp_graph_command_contract.compile_failed",
                "message": compile_result.stderr or compile_result.stdout,
                "severity": "error",
            }]
            write_json(out_dir / RESULT_NAME, {
                "kind": "CppGraphCommandContractResult",
                "graphId": None,
                "status": "compile_failed",
                "claims": {"cppCommandDispatcher": False, "runtimeDirty": False},
                "diagnostics": diagnostics,
            })
            write_json(out_dir / DIAGNOSTICS_NAME, diagnostics)
            sys.stderr.write(compile_result.stderr or compile_result.stdout)
            return compile_result.returncode

        run = subprocess.run(
            [str(binary), str(fixture_path), str(out_dir)],
            cwd=repo_root,
            text=True,
            capture_output=True,
            check=False,
        )
    if run.returncode != 0:
        diagnostics = [{
            "code": "cpp_graph_command_contract.probe_failed",
            "message": run.stderr or run.stdout,
            "severity": "error",
        }]
        if not (out_dir / RESULT_NAME).exists():
            write_json(out_dir / RESULT_NAME, {
                "kind": "CppGraphCommandContractResult",
                "graphId": None,
                "status": "probe_failed",
                "claims": {"cppCommandDispatcher": False, "runtimeDirty": False},
                "diagnostics": diagnostics,
            })
        if not (out_dir / DIAGNOSTICS_NAME).exists():
            write_json(out_dir / DIAGNOSTICS_NAME, diagnostics)
        sys.stderr.write(run.stderr or run.stdout)
    return run.returncode


def compile_probe(repo_root: Path, binary: Path) -> subprocess.CompletedProcess[str]:
    sources = [
        "docs/runtime/native/graph/GraphDocument.cpp",
        "docs/runtime/native/graph/GraphCommandDispatcher.cpp",
        "docs/runtime/native/graph/GraphValidator.cpp",
        "docs/runtime/native/graph/GraphDirtyPolicy.cpp",
        "docs/runtime/native/nodes/NodeManifestMaturity.cpp",
        "docs/runtime/native/nodes/NodeSpecRegistry.cpp",
        "docs/runtime/native/runtime/RuntimeGraphBuilder.cpp",
        "docs/runtime/native/tools/graph_command_contract_probe.cpp",
    ]
    command = [
        "clang++",
        "-std=c++17",
        "-I",
        "docs/runtime/native",
        *sources,
        "-o",
        str(binary),
    ]
    return subprocess.run(command, cwd=repo_root, text=True, capture_output=True, check=False)


def clear_output_dir(out_dir: Path) -> None:
    for name in ARTIFACT_NAMES:
        artifact = out_dir / name
        if artifact.is_file() or artifact.is_symlink():
            artifact.unlink()
    legacy_binary = out_dir / LEGACY_BINARY_NAME
    if legacy_binary.is_file() or legacy_binary.is_symlink():
        legacy_binary.unlink()


def write_json(path: Path, payload: Any) -> None:
    path.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n", encoding="utf8")


if __name__ == "__main__":
    raise SystemExit(main())

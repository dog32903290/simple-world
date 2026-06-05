#!/usr/bin/env python3
"""Generate the TiXL -> Vuo port status board from current repo evidence."""

from __future__ import annotations

import argparse
import re
import sys
from dataclasses import dataclass
from pathlib import Path


BOARD_PATH = Path("docs/tixl-porting/PORT_STATUS_BOARD.md")
INDEX_PATH = Path("docs/tixl-porting/TIXL_TO_VUO_PORTING.md")
INVENTORY_PATH = Path("docs/tixl-porting/reports/source_inventory.md")


@dataclass(frozen=True)
class VuoNode:
    title: str
    category: str
    source_path: Path
    status: str


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--check", action="store_true", help="fail if the checked-in board is stale")
    parser.add_argument("--write", action="store_true", help="write the generated board")
    args = parser.parse_args()

    repo_root = Path.cwd()
    generated = build_board(repo_root)
    board_path = repo_root / BOARD_PATH

    if args.write:
        board_path.write_text(generated, encoding="utf8")
        return 0

    if args.check:
        if not board_path.exists():
            print(f"missing {BOARD_PATH}", file=sys.stderr)
            return 1
        existing = board_path.read_text(encoding="utf8")
        if existing != generated:
            print(f"stale {BOARD_PATH}; run python3 docs/tixl-porting/scripts/generate_port_status_board.py --write", file=sys.stderr)
            return 1
        return 0

    print(generated)
    return 0


def build_board(repo_root: Path) -> str:
    indexed_nodes = read_inventory_count(repo_root)
    vuo_node_paths = sorted((repo_root / "vuo-nodes").glob("*.c"))
    composition_paths = sorted((repo_root / "vuo-compositions").glob("*.vuo")) + sorted((repo_root / "vuo-compositions/generated").glob("*.vuo"))
    test_paths = sorted((repo_root / "tests").glob("*.test.js"))
    composition_text = "\n".join(path.read_text(encoding="utf8", errors="ignore") for path in composition_paths)
    generated_composition_text = "\n".join(path.read_text(encoding="utf8", errors="ignore") for path in sorted((repo_root / "vuo-compositions/generated").glob("*.vuo")))
    test_text = "\n".join(path.read_text(encoding="utf8", errors="ignore") for path in test_paths)

    nodes = [
        parse_vuo_node(path, repo_root, composition_text, generated_composition_text, test_text)
        for path in vuo_node_paths
    ]

    lines = [
        "# TiXL -> Vuo Port Status Board",
        "",
        "Generated from current repo evidence. Do not hand-edit table rows; run:",
        "",
        "```bash",
        "python3 docs/tixl-porting/scripts/generate_port_status_board.py --write",
        "```",
        "",
        "## Summary",
        "",
        f"- TiXL indexed nodes: {indexed_nodes}",
        f"- Vuo custom node sources: {len(vuo_node_paths)}",
        f"- Vuo proof compositions: {len(composition_paths)}",
        "- Source inventory: `docs/tixl-porting/reports/source_inventory.md`",
        "- Grade rules: `docs/tixl-porting/reports/porting_grade_rules.md`",
        "",
        "## Current Vuo Node Evidence",
        "",
        "| visible node | category | status | source |",
        "|---|---|---|---|",
    ]
    for node in sorted(nodes, key=lambda item: (item.category, item.title)):
        lines.append(f"| {node.title} | {node.category} | {node.status} | `{display_path(node.source_path, repo_root)}` |")

    lines.extend([
        "",
        "## Read",
        "",
        "This board says what exists in Vuo today. It does not upgrade a body-layer adapter into native parity.",
        "Use the grade rules before turning any unbuilt TiXL node into code.",
        "",
        "Next batch should start from Grade A value/control nodes, then move outward only when the harness catches drift.",
        "",
    ])
    return "\n".join(lines)


def read_inventory_count(repo_root: Path) -> int:
    inventory = (repo_root / INVENTORY_PATH).read_text(encoding="utf8")
    match = re.search(r"TiXL nodes in `nodes_index\.csv`\s*\|\s*([0-9,]+)", inventory)
    if match:
        return int(match.group(1).replace(",", ""))
    index = (repo_root / INDEX_PATH).read_text(encoding="utf8")
    match = re.search(r"Global inventory:\s*\n\s*-\s*([0-9,]+) TiXL nodes", index)
    if not match:
        raise RuntimeError("could not read TiXL inventory count")
    return int(match.group(1).replace(",", ""))


def parse_vuo_node(path: Path, repo_root: Path, composition_text: str, generated_composition_text: str, test_text: str) -> VuoNode:
    source = path.read_text(encoding="utf8", errors="ignore")
    title = first_match(source, r'"title"\s*:\s*"([^"]+)"') or path.stem
    category = extract_category(title, source)
    status = infer_status(title, path.name, composition_text, generated_composition_text, test_text)
    return VuoNode(title=title, category=category, source_path=path, status=status)


def extract_category(title: str, source: str) -> str:
    if title == "my_MainClock":
        return "My World runtime adapter"
    if title.endswith("Proof"):
        return "proof-adapter"
    category = first_match(source, r"Category:\s*(Operators/Lib/[A-Za-z0-9_./-]+)")
    if category:
        return category.rstrip(".")
    category = first_match(source, r"mirrors TiXL's Lib\.([A-Za-z0-9_.]+)\.[A-Za-z0-9_]+ operator")
    if category:
        return "Operators/Lib/" + category.replace(".", "/")
    category = first_match(source, r"TiXL\s+(?:Operators/Lib|source:\s*external/tixl/Operators/Lib)/([A-Za-z0-9_./-]+)")
    if category:
        return "Operators/Lib/" + category.rsplit("/", 1)[0]
    return "unknown"


def infer_status(title: str, file_name: str, composition_text: str, generated_composition_text: str, test_text: str) -> str:
    if title == "my_MainClock" and title in generated_composition_text:
        return "Vuo node + generated composition proof"
    if title in composition_text:
        return "Vuo node + composition proof"
    if title in test_text or file_name in test_text:
        return "Vuo node + source contract test"
    return "Vuo node source only"


def first_match(source: str, pattern: str) -> str | None:
    match = re.search(pattern, source)
    return match.group(1) if match else None


def display_path(path: Path, repo_root: Path) -> str:
    return str(path.resolve().relative_to(repo_root))


if __name__ == "__main__":
    raise SystemExit(main())

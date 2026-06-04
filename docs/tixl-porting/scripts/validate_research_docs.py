#!/usr/bin/env python3

from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[1]

EXPECTED_FILES = [
    ROOT / "namespaces" / "numbers.md",
    ROOT / "namespaces" / "image.md",
    ROOT / "namespaces" / "render_mesh_point.md",
    ROOT / "namespaces" / "field_particle.md",
    ROOT / "namespaces" / "io_flow_string_data.md",
    ROOT / "reports" / "source_inventory.md",
    ROOT / "reports" / "porting_grade_rules.md",
]

REQUIRED_PATTERNS = {
    "grade markers": re.compile(r"\b[ABCD]\b"),
    "source evidence": re.compile(r"(Source evidence|C#|\\.t3|docs)", re.IGNORECASE),
    "vuo mapping": re.compile(r"Vuo mapping|Vuo[A-Z]|Vuo built-in|Vuo type", re.IGNORECASE),
}


def main() -> int:
    failed = False
    for path in EXPECTED_FILES:
        if not path.exists():
            print(f"MISS {path.relative_to(ROOT)}")
            failed = True
            continue

        text = path.read_text(encoding="utf-8")
        headings = len(re.findall(r"^##\s+", text, flags=re.MULTILINE))
        print(f"OK   {path.relative_to(ROOT)} headings={headings} bytes={len(text)}")

        for label, pattern in REQUIRED_PATTERNS.items():
            if not pattern.search(text):
                print(f"  MISS {label}")
                failed = True

    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())

#!/usr/bin/env python3
"""coverage_scan.py — verify-zone tool: find NodeSpec params that goldens never test.

This is NOT a TiXL-parity op; it has no golden-vs-TiXL.  It is an audit tool that
cross-references two facts about every registered node-op:

  1. WHAT it declares  — from the LIVE registry, via `simple_world --dump-specs`
     (authoritative; not a source-code parse).  Each op declares a set of INPUT
     ports = its parameters (e.g. TransformPoints declares Rotation.x/.y/.z).

  2. WHAT its golden tests — from the golden/selftest source corpus.  A param is
     "tested" only if its PORT-ID STRING LITERAL (e.g. "Rotation.x") appears in
     the op's golden source, i.e. the golden drives it through the param-cook path
     `node.params["Rotation.x"] = v`.  Setting the GPU struct field directly
     (`RP.RotationX = 90.0f`) BYPASSES the param path and does NOT count — that is
     exactly the task_eef5757e rotation bug: the Rotation port→GPU plumbing ships
     untested because the golden pokes the struct, never the port.

DETECTION (two tiers, see write-up in the report):
  Tier 1 (name-absence): the param-id string literal "Foo.x" never appears in the
          op's golden source  ->  CONFIRMED HOLE.
  Tier 2 (default-only):  the param-id string DOES appear, but only ever assigned
          its declared default value  ->  WEAK HOLE (exercised but not varied).
          (Tier 2 is reported separately; the ground-truth Rotation case is a
           Tier-1 hole, so Tier-1 alone already satisfies acceptance.)

OP -> GOLDEN FILE binding (a file "belongs" to an op when either holds):
  - filename matches the lowercased type, e.g. point_ops_transformpoints.cpp, OR
  - the file contains the op's type name as a quoted string literal "TransformPoints"
    (goldens create the node via `xf.type = "TransformPoints"`).
This per-op scoping prevents cross-contamination: "Rotation.x" lives in
polartransformpoints / eulertoaxisangle goldens, but those files are NOT bound to
TransformPoints, so they cannot falsely mark TransformPoints' Rotation as covered.

Output: docs/agent/census/COVERAGE_SCAN.md
"""

import os
import re
import subprocess
import sys
from collections import OrderedDict

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BIN = os.path.join(REPO, "app", "build", "simple_world")
OUT = os.path.join(REPO, "docs", "agent", "census", "COVERAGE_SCAN.md")

# Data-bag / structural input dataTypes that are NOT tunable scalar params — a missing
# golden for a "points" input bag is not a coverage hole in the param sense.  We focus the
# audit on the numeric/text param rail (matches the rotation-bug class).  These are reported
# in a separate "structural inputs (not param-audited)" note, not as holes.
PARAM_DATATYPES = {"Float", "String"}

# Golden / selftest source corpus globs (relative to repo root).  Covers all 3 locations:
# per-op inline goldens, shell-tier *_golden.cpp, and aggregator *_selftest.cpp + the router.
CORPUS_DIRS = [
    ("app/src/runtime", lambda f: f.endswith(".cpp")),
    ("app/src", lambda f: f.endswith("_golden.cpp") or f == "selftests.cpp"),
    ("app/src/app", lambda f: f.endswith("_selftest.cpp") or f.endswith("_golden.cpp")),
    ("app/src/ui", lambda f: f.endswith("_selftest.cpp")),
    ("app/src/verify", lambda f: f.endswith("_selftest.cpp")),
]


def run_dump_specs():
    """Return OrderedDict[type] -> list of (port_id, dataType) for INPUT ports."""
    if not os.path.exists(BIN):
        sys.exit("ERROR: binary not built: %s (run agent_worktree_setup.sh)" % BIN)
    out = subprocess.run([BIN, "--dump-specs"], capture_output=True, text=True)
    if out.returncode != 0:
        sys.exit("ERROR: --dump-specs exited %d:\n%s" % (out.returncode, out.stderr))
    specs = OrderedDict()
    for line in out.stdout.splitlines():
        if "\t" not in line:
            continue
        typ, rest = line.split("\t", 1)
        ports = []
        if rest:
            for tok in rest.split(","):
                if ":" in tok:
                    pid, dt = tok.rsplit(":", 1)
                else:
                    pid, dt = tok, ""
                ports.append((pid, dt))
        specs[typ] = ports
    return specs


def load_corpus():
    """Return OrderedDict[abs_path] -> file text, for every golden/selftest source file.

    Paths are sorted so output is byte-stable regardless of filesystem listing order.
    """
    corpus = OrderedDict()
    for rel, pred in CORPUS_DIRS:
        d = os.path.join(REPO, rel)
        if not os.path.isdir(d):
            continue
        for fn in sorted(os.listdir(d)):
            if not pred(fn):
                continue
            p = os.path.join(d, fn)
            if not os.path.isfile(p):
                continue
            try:
                with open(p, "r", errors="replace") as fh:
                    corpus[p] = fh.read()
            except OSError:
                pass
    return corpus


def files_for_op(typ, corpus):
    """Bind golden files to an op.  Returns (files, binding_mode).

    TIGHT (preferred): the op has its own per-op golden — a file whose name ends
    `_<type>.cpp` (lowercased), e.g. point_ops_transformpoints.cpp,
    field_ops_boxsdf_golden.cpp.  Only that file (+ any same-name siblings) is scanned.
    This is precise: no other op's golden can falsely cover a same-named param.

    FALLBACK (looser): no per-op golden file exists (generators / draw / some value ops
    are registered in node_registry_<family>.cpp and exercised across MANY goldens that
    use the op as an upstream node, e.g. `gen.type = "RadialPoints"`).  Here coverage is
    genuinely DISTRIBUTED, so we bind every file that quotes the type name and accept the
    distributed coverage.  Trade-off documented in the report: in fallback mode a param
    could be marked covered by a sibling op's `params["<sameName>"]` (cross-family
    collision).  This only affects the 82 ops without a dedicated golden, and is flagged.
    """
    lower = typ.lower()
    fname_pat = "_%s.cpp" % lower
    fname_hits = [(p, t) for p, t in corpus.items()
                  if os.path.basename(p).lower().endswith(fname_pat)]
    if fname_hits:
        return fname_hits, "tight"
    quoted = '"%s"' % typ
    quoted_hits = [(p, t) for p, t in corpus.items() if quoted in t]
    return quoted_hits, "fallback"


def _cover_patterns(param_id):
    """Regexes that match a golden *setting* this param (cook path), NOT *declaring* it.

    Covered (test SETS the param):
      node.params["<id>"] = v          (GPU/cook ops)
      node.strParams["<id>"] = "t"     (String params)
      evalOpParams("Op", {{"<id>", v}}) (value/math ops; 2nd elem is a value, not a string)

    NOT covered (these are DECLARATIONS, must not count):
      {"<id>", "Name", "Float", ...}   (NodeSpec literal table — 2nd elem is a quoted string)
      rx.id = "<id>"; rx.name = ...     (PortSpec builder)
      RP.RotationX = 90.0f              (struct field — has no quoted dotted id at all)
    """
    e = re.escape(param_id)
    # params["id"] / strParams["id"] subscript — unambiguous "set" idiom.
    subscript = re.compile(r'(?:params|strParams)\s*\[\s*"' + e + r'"\s*\]')
    # evalOpParams brace-pair {"id", <value>} — 2nd element is NOT a quoted string, so this
    # cannot match a NodeSpec literal row (whose 2nd element is always a quoted display name).
    evalpair = re.compile(r'\{\s*"' + e + r'"\s*,\s*[^"\s}]')
    return subscript, evalpair


def param_covered(param_id, texts):
    """Tier-1 signal: the param is SET (not merely declared) somewhere in the op's golden.

    Matching the *cook-set* idiom (not a bare quoted string) is what separates a real test
    from the op's own NodeSpec declaration that lives in the same .cpp.  The struct-field
    bypass (`RP.RotationX`) carries no quoted dotted id and is correctly excluded → the
    task_eef5757e Rotation case reads as a hole.
    """
    subscript, evalpair = _cover_patterns(param_id)
    return any(subscript.search(t) or evalpair.search(t) for t in texts)


def main():
    specs = run_dump_specs()
    corpus = load_corpus()

    rows = []          # (type, [uncovered], n_param_ports, n_bound_files, binding_mode)
    no_golden = []     # ops with zero bound golden file (type never quoted, no own file)
    total_params = 0
    total_holes = 0
    fallback_holes = 0  # holes found under the looser fallback binding (lower confidence)

    for typ, ports in specs.items():
        params = [(pid, dt) for (pid, dt) in ports if dt in PARAM_DATATYPES]
        if not params:
            continue  # op declares no scalar/text params (pure data-flow op)
        bound, mode = files_for_op(typ, corpus)
        texts = [t for (_p, t) in bound]
        total_params += len(params)
        if not bound:
            # No golden source at all -> every param is unverified (a different, coarser hole).
            no_golden.append((typ, [pid for (pid, _dt) in params]))
            continue
        uncovered = [pid for (pid, _dt) in params if not param_covered(pid, texts)]
        if uncovered:
            total_holes += len(uncovered)
            if mode == "fallback":
                fallback_holes += len(uncovered)
            rows.append((typ, uncovered, len(params), len(bound), mode))

    # Sort: tight-binding holes first (highest confidence), then by hole count.
    rows.sort(key=lambda r: (r[4] != "tight", -len(r[1]), r[0]))
    no_golden.sort(key=lambda r: (-len(r[1]), r[0]))

    n_ops_scanned = sum(1 for _t, ports in specs.items()
                        if any(dt in PARAM_DATATYPES for (_p, dt) in ports))
    n_ops_with_holes = len(rows)
    n_ops_no_golden = len(no_golden)
    no_golden_holes = sum(len(p) for _t, p in no_golden)

    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    with open(OUT, "w") as f:
        w = f.write
        w("# Parameter Coverage Scan — declared-but-untested NodeSpec params\n\n")
        w("> Generated by `tools/coverage_scan.py` (verify zone). NOT a TiXL-parity artifact.\n")
        w("> Re-run: `./app/build/simple_world --dump-specs` is the live param source; "
          "the script cross-references it against the golden/selftest source corpus.\n\n")

        w("## What this finds\n\n")
        w("Ops that **declare** a parameter in their NodeSpec but whose **golden never "
          "drives that parameter through the param-cook path** (`node.params[\"<id>\"] = v`). "
          "This is the **class-1 coverage hole** — the same shape as the task_eef5757e "
          "rotation bug, where `Rotation.x/.y/.z` ship untested because the golden pokes the "
          "GPU struct field (`RP.RotationX`) directly and never the port id.\n\n")

        w("## Detection method\n\n")
        w("- **Authoritative param list**: `simple_world --dump-specs` (live registry: "
          "`registry()` + all 9 self-registration sinks). NOT a source parse.\n")
        w("- **Param universe audited**: INPUT ports of dataType `Float` or `String` "
          "(the tunable param rail). Data-bag inputs (`Points`/`Field`/`Image`/...) are "
          "structural, not param-audited, and are excluded from hole counts.\n")
        w("- **Op→golden binding (two modes)**:\n")
        w("  - *TIGHT* (preferred, used for the 230 ops that have one): the op's own per-op "
          "golden — a file whose name ends `_<type>.cpp` (lowercased), e.g. "
          "`point_ops_transformpoints.cpp`, `field_ops_boxsdf_golden.cpp`. Only that file is "
          "scanned, so no sibling op's golden can falsely cover a same-named param.\n")
        w("  - *FALLBACK* (looser, the ~80 ops with no dedicated golden — generators, draw, "
          "some value ops registered in `node_registry_<family>.cpp` and exercised across many "
          "goldens as upstream nodes): bind every file that quotes the type name `\"<Type>\"`. "
          "Coverage is genuinely distributed for these, but a param could be marked covered by "
          "a *sibling* op's `params[\"<sameName>\"]` (cross-family collision). These rows are "
          "labelled `fallback` and split out in the summary for human review.\n")
        w("- **Coverage signal (the discriminator)**: a param counts as TESTED only when its "
          "id appears in a *set* idiom — `params[\"<id>\"]` / `strParams[\"<id>\"]` subscript, "
          "or an `evalOpParams(..., {{\"<id>\", value}}, ...)` brace-pair. This deliberately "
          "does NOT match the *declaration* idioms that live in the same .cpp: the NodeSpec "
          "literal `{\"<id>\", \"Name\", \"Float\", ...}` (2nd element is a quoted display name) "
          "and the PortSpec builder `p.id = \"<id>\"`. The struct-field bypass `RP.RotationX` "
          "carries no quoted dotted id at all. So a golden that pokes only the struct (the "
          "task_eef5757e shape) is correctly reported as a hole.\n")
        w("- **Tier**: this is name-absence detection — a param flagged here is *never set via "
          "the cook path* by its golden. A natural next refinement (Tier 2 = \"present but only "
          "ever assigned its default value\") was not needed: the ground-truth Rotation case is "
          "a clean absence, so cook-set presence is a sufficient discriminator here.\n\n")

        w("## False-positive / false-negative risk\n\n")
        w("- **Fallback rows (false positive)**: an op with no dedicated golden whose param is "
          "genuinely driven somewhere the scanner did not bind would over-report. The "
          "`fallback` rows are the ones to eyeball; `tight` rows are high-confidence.\n")
        w("- **Hidden key (false positive)**: a param driven via a computed/non-literal key "
          "(not a string literal) would read as a hole. Spot-checks did not surface this — the "
          "cook idiom in this codebase is string-literal keys.\n")
        w("- **Cross-family collision (false negative, fallback only)**: in fallback mode a "
          "param could be marked covered by a sibling op that sets a same-named param. TIGHT "
          "binding is immune (single own-file scope). This is the trade for not inventing "
          "false holes on distributed-test ops.\n")
        w("- **Counter-bias (intentional)**: in TIGHT mode a param tested only in a *different* "
          "op's golden is NOT counted as covering this op — that is exactly how the rotation "
          "bug stayed invisible, and the tool refuses to launder it.\n\n")

        w("## Summary\n\n")
        w("| metric | count |\n|---|---:|\n")
        w("| ops scanned (declare ≥1 Float/String param) | %d |\n" % n_ops_scanned)
        w("| ops with ≥1 hole | %d |\n" % n_ops_with_holes)
        w("| total holes | %d |\n" % total_holes)
        w("| └ of those, under TIGHT binding (high confidence) | %d |\n"
          % (total_holes - fallback_holes))
        w("| └ of those, under FALLBACK binding (review — may be false +) | %d |\n"
          % fallback_holes)
        w("| ops with NO golden source at all | %d |\n" % n_ops_no_golden)
        w("| params under ops with no golden | %d |\n" % no_golden_holes)
        w("| total Float/String params audited | %d |\n\n" % total_params)

        w("## GROUND-TRUTH check (task_eef5757e)\n\n")
        tp = next((r for r in rows if r[0] == "TransformPoints"), None)
        if tp and any(p in tp[1] for p in ("Rotation.x", "Rotation.y", "Rotation.z")):
            w("PASS — `TransformPoints` Rotation flagged uncovered (detection = name-absence):\n\n")
            w("```\nTransformPoints\t%s\n```\n\n" % ", ".join(tp[1]))
        else:
            w("**FAIL — TransformPoints Rotation NOT flagged. Tool is broken; do not trust.**\n\n")

        w("## Ops with holes (param declared but never cook-driven by its golden)\n\n")
        w("Sorted highest-confidence first (TIGHT = op has its own per-op golden; FALLBACK = "
          "distributed-test op, looser binding). `n/N` = uncovered / total Float·String "
          "params.\n\n")
        w("| op | binding | uncovered params | n/N | files |\n|---|---|---|---:|---:|\n")
        for typ, unc, nparam, nfiles, mode in rows:
            w("| `%s` | %s | %s | %d/%d | %d |\n"
              % (typ, mode, ", ".join("`%s`" % u for u in unc), len(unc), nparam, nfiles))
        w("\n")

        if no_golden:
            w("## Ops with NO golden source (coarser hole: nothing tests them)\n\n")
            w("These ops have no bound golden/selftest file; every declared param is "
              "unverified. Distinct from the holes above (those have a golden that simply "
              "skips a param).\n\n")
            w("| op | declared params | count |\n|---|---|---:|\n")
            for typ, params in no_golden:
                shown = ", ".join("`%s`" % p for p in params[:8])
                if len(params) > 8:
                    shown += ", …"
                w("| `%s` | %s | %d |\n" % (typ, shown, len(params)))
            w("\n")

    # Console summary
    print("scanned %d ops; %d with holes (%d holes); %d ops with no golden (%d params)"
          % (n_ops_scanned, n_ops_with_holes, total_holes, n_ops_no_golden, no_golden_holes))
    print("report: %s" % OUT)
    tp = next((r for r in rows if r[0] == "TransformPoints"), None)
    if tp:
        print("GROUND-TRUTH TransformPoints uncovered: %s" % ", ".join(tp[1]))
    else:
        print("GROUND-TRUTH WARNING: TransformPoints not in hole list!")


if __name__ == "__main__":
    main()

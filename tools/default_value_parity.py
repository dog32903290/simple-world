#!/usr/bin/env python3
# tools/default_value_parity.py — per-node DEFAULT-VALUE comparator for the value-parity gate.
#
# Called by tools/default_value_parity.sh (one invocation per node). Reads the sw side (the DEF lines
# from `simple_world --dump-nodespec <Type>`) on stdin and the TiXL side (`<Type>.t3`) from argv, joins
# by parameter name, applies the normalization layers, and prints one MISMATCH line per drifting param:
#
#     <Node>\t<Param>\t<sw>\t<TiXL>\t<kind>
#
# NORMALIZATION (see task spec):
#   1. vector expand  — TiXL DefaultValue {X,Y,Z(,W)} → three/four param rows name.x/.y/.z(/.w),
#                       matching sw's per-component ports.
#   2. enum name→index — TiXL DefaultValue "MirrorOnce" → the index of that label in sw's enum labels.
#   3. bool           — TiXL true/false → 1.0 / 0.0 (sw stores the bool as a float in .def).
#   4. type-zero      — a sw param NOT present in the .t3 Inputs[] compares against the type-zero default
#                       (0.0 numeric / "" string). Only surfaced when sw itself is non-zero (real drift).
#
# Only Float/String-typed sw input ports are compared (connection inputs — Points/Image/Command/… —
# carry no default and TiXL serializes them as null). Float epsilon = abs(a-b) <= 1e-4 + 1e-4*abs(b).
import json, re, sys

FLOAT_EPS_ABS = 1e-4
FLOAT_EPS_REL = 1e-4


def strip_block_comments(text: str) -> str:
    # Remove /* ... */ comments so the .t3 becomes valid JSON. (.t3 has no // line comments.)
    return re.sub(r"/\*.*?\*/", "", text, flags=re.DOTALL)


def parse_t3_inputs(path: str):
    """Return {param_name: default_value} where default_value is python float/int/bool/str/dict/None."""
    raw = open(path, "r", encoding="utf-8").read()
    # guid -> name from the inline /*Name*/ comment attached to each "Id".
    guid_name = {}
    for m in re.finditer(r'"([0-9a-fA-F-]{36})"\s*/\*([^*]*)\*/', raw):
        guid_name[m.group(1).lower()] = m.group(2)
    doc = json.loads(strip_block_comments(raw))
    out = {}
    for inp in doc.get("Inputs", []):
        guid = str(inp.get("Id", "")).lower()
        name = guid_name.get(guid)
        if not name:
            continue
        if "DefaultValue" not in inp:
            continue
        out[name] = inp["DefaultValue"]
    return out


def expand_tixl(t3map):
    """Expand vectors to component rows; drop null/list; keep numbers/bools/strings.
    Returns {param_name: value} with value being float or str."""
    exp = {}
    for name, v in t3map.items():
        if v is None:
            continue
        if isinstance(v, bool):
            exp[name] = 1.0 if v else 0.0
        elif isinstance(v, (int, float)):
            exp[name] = float(v)
        elif isinstance(v, str):
            exp[name] = v  # enum-name or string default
        elif isinstance(v, dict):
            for comp, key in (("x", "X"), ("y", "Y"), ("z", "Z"), ("w", "W")):
                if key in v and isinstance(v[key], (int, float, bool)):
                    exp[f"{name}.{comp}"] = float(v[key])
        elif isinstance(v, list):
            # A 16-element list is a row-major 4x4 matrix; sw spells it name.m11..m44. Expand
            # positionally: index (r-1)*4+(c-1) → name.m{r}{c}. Other-length lists are not modeled.
            if len(v) == 16 and all(isinstance(x, (int, float, bool)) for x in v):
                for r in range(1, 5):
                    for c in range(1, 5):
                        exp[f"{name}.m{r}{c}"] = float(v[(r - 1) * 4 + (c - 1)])
            continue
    return exp


def parse_sw(lines):
    """Parse DEF lines → list of dicts {id,dataType,widget,def,strDef,labels}."""
    ports = []
    for ln in lines:
        if not ln.startswith("DEF\t"):
            continue
        parts = ln.rstrip("\n").split("\t")
        # DEF, id, dataType, widget, def, strDef, labels
        while len(parts) < 7:
            parts.append("")
        ports.append({
            "id": parts[1],
            "dataType": parts[2],
            "widget": parts[3],
            "def": float(parts[4]) if parts[4] not in ("", "nan") else 0.0,
            "strDef": parts[5],
            "labels": parts[6].split(",") if parts[6] else [],
        })
    return ports


def floats_equal(a, b):
    return abs(a - b) <= FLOAT_EPS_ABS + FLOAT_EPS_REL * abs(b)


# Positional component-suffix aliases: sw spells a vector's components as .x/.y/.z/.w OR .r/.g/.b/.a
# (colors), while a TiXL vector DefaultValue always expands to .x/.y/.z/.w (from its {X,Y,Z,W}). Map an
# rgba-suffixed sw port name to its positional xyzw twin so the join lands.
_RGBA = {".r": ".x", ".g": ".y", ".b": ".z", ".a": ".w"}


def tixl_lookup(tixl, key):
    """Return (value, present) for a sw port name, trying the rgba→xyzw positional alias."""
    if key in tixl:
        return tixl[key], True
    for suf, alias in _RGBA.items():
        if key.endswith(suf):
            ak = key[: -len(suf)] + alias
            if ak in tixl:
                return tixl[ak], True
            break
    return None, False


def fmt(x):
    if isinstance(x, float):
        return f"{x:g}"
    return str(x)


def main():
    node = sys.argv[1]
    t3_path = sys.argv[2]
    sw_ports = parse_sw(sys.stdin.readlines())
    tixl = expand_tixl(parse_t3_inputs(t3_path))

    mism = []       # (param, sw, tixl, kind)
    matched_keys = set()

    for p in sw_ports:
        dt = p["dataType"]
        if dt not in ("Float", "String"):
            continue  # connection input — no default parity
        key = p["id"]

        # ---- String-typed port: compare strDef against TiXL string default ----
        if dt == "String":
            tv, present = tixl_lookup(tixl, key)
            if present:
                matched_keys.add(key)
            else:
                tv = ""  # type-zero fallback
            if not isinstance(tv, str):
                tv = str(tv)
            if p["strDef"] != tv:
                # only report sw-only strings when sw is non-empty (real drift, not empty==empty)
                if present or p["strDef"] != "":
                    kind = "string" if present else "string-vs-zero"
                    mism.append((key, repr(p["strDef"]), repr(tv), kind))
            continue

        # ---- Float-typed port (scalar / enum / bool / vec component) ----
        tv, present = tixl_lookup(tixl, key)
        if present:
            matched_keys.add(key)
        else:
            tv = 0.0  # type-zero fallback (item 4)

        if p["widget"] == "Enum":
            sw_idx = int(round(p["def"]))
            if isinstance(tv, str):
                # enum name → index via sw labels
                if tv in p["labels"]:
                    ti = p["labels"].index(tv)
                    if sw_idx != ti:
                        mism.append((key, str(sw_idx), f"{ti}({tv})", "enum"))
                else:
                    mism.append((key, f"{sw_idx}({p['labels'][sw_idx] if sw_idx < len(p['labels']) else '?'})",
                                 f'"{tv}"', "enum-name-unresolved"))
            else:
                ti = int(round(tv))
                if sw_idx != ti and (present or sw_idx != 0):
                    mism.append((key, str(sw_idx), str(ti), "enum"))
        else:
            # numeric (slider / bool / vec)
            if isinstance(tv, str):
                # sw numeric but TiXL string — cannot normalize; flag
                mism.append((key, fmt(p["def"]), f'"{tv}"', "type-mismatch"))
                continue
            if not floats_equal(p["def"], float(tv)):
                # type-zero fallbacks only reported when sw is non-zero (real drift)
                if present or abs(p["def"]) > FLOAT_EPS_ABS:
                    kind = "float" if present else "float-vs-zero"
                    mism.append((key, fmt(p["def"]), fmt(float(tv)), kind))

    # tixl-only inputs (sw missing the param entirely) — a COUNT concern, reported as a tally line only.
    tixl_only = [k for k in tixl.keys() if k not in matched_keys]

    for (param, swv, tv, kind) in mism:
        print(f"{node}\t{param}\t{swv}\t{tv}\t{kind}")
    # machine tally on stderr (sh aggregates): MISMATCH=<n> TIXL_ONLY=<n>
    sys.stderr.write(f"TALLY\t{node}\t{len(mism)}\t{len(tixl_only)}\n")


if __name__ == "__main__":
    main()

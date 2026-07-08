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
#   5. name alias     — a per-node {sw_name: tixl_name} table for confirmed renames/typos where the join
#                       key differs but the underlying param (and value) is the same. Every entry below
#                       was hand-verified against the live .t3 (value matches) — see _ALIASES.
#   6. case-insensitive join — param-name join falls back to a case-insensitive match (handles TiXL
#                       vectors expanding to name.x/.y/.z/.w while sw spells a port name.X/.Y, or scalar
#                       casing like sw "a"/"b" vs TiXL "A"/"B"). Tried after the exact and rgba-alias
#                       lookups, and again after a name-alias substitution.
#
# WHITELIST (_WHITELIST): sw-internal ports with NO top-level TiXL Input to join against at all (a
# pinless kernel discriminator, or a value flattened out of an internal compound child) — excluded from
# the gate entirely, not "matched". See the table below for the citation per entry.
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

# Per-node {sw_param: tixl_param} rename/typo table. Keyed by (node, sw_base_name) — sw_base_name is the
# port name WITHOUT a vector-component suffix (".r"/".x"/…), so one entry covers every component row.
# Every entry hand-verified against the live .t3 default value (scout audit, 2026-07-08/09):
_ALIASES = {
    ("CollapseVertices", "Strength"): "Amount",              # CollapseVertices.t3: Amount=1.0
    ("SetRequestedResolution", "Multiply"): "ScaleResolution",  # .t3: ScaleResolution=1.0
    ("ShardNoise", "Sharpness"): "Sharpen",                   # ShardNoise.t3: Sharpen=1.0
    ("SubdivideLinePoints", "InsertCount"): "Count",          # SubdivideLinePoints.t3: Count=100
    ("Blob", "Fill"): "Color",                                # Blob.t3: Color={1,1,1,1}
    ("KeyColor", "KeyColor"): "Key",                          # KeyColor.t3: Key={1,1,1,1}
    ("RgbTV", "Contrast"): "ImageContrast",                   # RgbTV.t3: ImageContrast=1.0 (sw abbreviates)
    ("RgbTV", "ImageBrightness"): "ImageBrightess",           # RgbTV.t3 TiXL typo (missing "n")
    ("Rings", "Contrast"): "Constrast",                       # Rings.t3 TiXL typo
    ("RyojiPattern1", "ForegroundRatio"): "ForgroundRatio",   # RyojiPattern1.t3 TiXL typo
    ("RyojiPattern2", "ForegroundRatio"): "ForgroundRatio",   # RyojiPattern2.t3 TiXL typo
    ("TriggerAnim", "VariableName"): "UseTriggerVar",         # TriggerAnim.t3: UseTriggerVar="__Trigger";
                                                               # sw source (node_registry_math_anim2.cpp:69)
                                                               # names it explicitly: "UseTriggerVar rides
                                                               # the String rail (VariableName-style)".
}

# sw-internal ports with NO top-level TiXL Input to join against — excluded from the gate, not "matched".
_WHITELIST = {
    # _ForceKind: pinless kernel discriminator (ForceKind enum, force_params.h:~251) that tells
    # cookParticleSim which force kernel to dispatch for a wired ParticleForce node. TiXL has no such
    # pin — the concept doesn't exist on that side. One entry per Force node the census flagged.
    ("AxisStepForce", "_ForceKind"),
    ("DirectionalForce", "_ForceKind"),
    ("FieldDistanceForce", "_ForceKind"),
    ("FieldVolumeForce", "_ForceKind"),
    ("RandomJumpForce", "_ForceKind"),
    ("SnapToAnglesForce", "_ForceKind"),
    ("VectorFieldForce", "_ForceKind"),
    ("VelocityForce", "_ForceKind"),
    # TimeDisplace.ArraySize: TiXL's TimeDisplace.t3 is a COMPOUND whose internal KeepInTextureArray
    # child carries ArrayLength=128 — an inline default inside the compound graph, not a top-level Input
    # on TimeDisplace itself (point_ops_timedisplace.cpp:9,69,157,164). sw flattened the compound and
    # exposed the child's value as a real port; there is nothing in TimeDisplace.t3 Inputs[] to join it
    # against with this tool's model (which only reads the top-level .t3).
    ("TimeDisplace", "ArraySize"),
}


def tixl_lookup(tixl, tixl_ci, node, key):
    """Return (value, present) for a sw port name.

    Resolution order: exact → rgba positional alias → case-insensitive → (if a name-alias exists for
    this node+base-name) repeat exact/rgba/case-insensitive against the aliased name.
    """
    def _try(k):
        if k in tixl:
            return tixl[k], True
        for suf, alias in _RGBA.items():
            if k.endswith(suf):
                ak = k[: -len(suf)] + alias
                if ak in tixl:
                    return tixl[ak], True
                break
        lk = k.lower()
        if lk in tixl_ci:
            return tixl[tixl_ci[lk]], True
        return None, False

    v, present = _try(key)
    if present:
        return v, True

    if "." in key:
        base, suf = key.split(".", 1)
        suf = "." + suf
    else:
        base, suf = key, ""
    alias_base = _ALIASES.get((node, base))
    if alias_base:
        v, present = _try(alias_base + suf)
        if present:
            return v, True

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
    tixl_ci = {k.lower(): k for k in tixl}

    mism = []       # (param, sw, tixl, kind)
    matched_keys = set()

    for p in sw_ports:
        dt = p["dataType"]
        if dt not in ("Float", "String"):
            continue  # connection input — no default parity
        key = p["id"]
        if (node, key) in _WHITELIST:
            continue  # sw-internal, no TiXL top-level Input to join against — see _WHITELIST comment

        # ---- String-typed port: compare strDef against TiXL string default ----
        if dt == "String":
            tv, present = tixl_lookup(tixl, tixl_ci, node, key)
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
        tv, present = tixl_lookup(tixl, tixl_ci, node, key)
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

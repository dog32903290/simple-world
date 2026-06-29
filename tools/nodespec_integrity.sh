#!/usr/bin/env bash
# tools/nodespec_integrity.sh — param-completion INTEGRITY GATE for point generators.
#
# WHY: the param-completion fan-out exposes every TiXL [Input] of a generator as a real inspector
# knob. The failure mode is SILENT: miss one baked param and the node looks done but diverges from
# TiXL. This gate makes the omission LOUD by comparing two independently-derived counts:
#
#   sw side  : app/build/simple_world --dump-nodespec <Type>  →  FOLDED_LOGICAL_COUNT
#              (a VectorN param folds to 1; the sw output port + grid-capacity "Count" are excluded —
#               see dumpNodeSpec in app/src/selftests.cpp for the exact rule.)
#   TiXL side: grep the node's .cs for `[Input(` on non-comment lines (each InputSlot<VectorN> is
#              ONE [Input] line → already folded, no expansion needed).
#
# PASS when sw + known-forks == TiXL. FAIL (nonzero) with a diff when they differ — a mismatch means
# either a missing sw param (sw < TiXL) or an extra/forked one (sw > TiXL); the implementer reads the
# dump + the .cs to decide which inputs to add (or to record as a named FORK).
#
# KNOWN FORKS: some TiXL [Input]s are intentionally NOT exposed as sw params (faithful-dead) — e.g.
# PointsOnMesh's IsEnabled, which forwards into the generic flow/Execute disable-wrapper (not a
# node-specific knob). These are listed in known_fork_count() with a justification, and ADDED to the
# sw side before the compare. Stuffing a meaningless param would lie to the inspector; the fork list
# keeps the gate green WITHOUT that lie. The list is small and each entry cites its op-source header.
#
# Usage:
#   tools/nodespec_integrity.sh <NodeType>        # gate one generator
#   tools/nodespec_integrity.sh --all-generators  # gate the whole known generator set
#
# Read-only on external/tixl. Requires a built app/build/simple_world (run cmake --build first).
set -u

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BIN="${SW_BIN:-$REPO_ROOT/app/build/simple_world}"
# external/tixl normally lives in the running checkout; SW_TIXL_LIB overrides the root (e.g. a worktree
# whose external/ is only present in the main checkout — point it at the main repo's Operators/Lib).
TIXL_LIB="${SW_TIXL_LIB:-$REPO_ROOT/external/tixl/Operators/Lib}"
TIXL_GEN="$TIXL_LIB/point/generate"

if [ ! -x "$BIN" ]; then
  echo "✗ nodespec_integrity: binary not found/executable: $BIN" >&2
  echo "  build first: cmake --build app/build -j8" >&2
  exit 3
fi

# SW source roots (read the leaf op's header authority — workitem B, stale-proof fork rename).
SW_RT="$REPO_ROOT/app/src/runtime"
SW_APP="$REPO_ROOT/app/src"

# ---- fork-rename authority map (workitem B, stale-proof, PRECOMPUTED ONCE) ----
# A handful of sw leaf ops fork the TiXL filename/op-id (DoyleSpiralPoints→DoyleSpiralPoints2,
# chromab→ChromaticAbberation…). The authoritative TiXL .cs name is DECLARED in the leaf's LEADING
# comment header as `// @tixl: <OpName>` or `// TiXL authority: .../<OpName>.cs` — the SAME census
# source#4 convention (op_census.sh:45-62), so the gate and census read one SSOT and never drift.
# We scan ONLY the few files that actually carry such a header (grep -l up front) and build a
# TYPE<TAB><OpName>.cs map FILE once (bash 3.2 has no associative arrays), so cs_for_type is a cheap
# grep lookup instead of a per-type tree grep (the per-type grep made the image-island sweep of
# ~hundreds of types unusably slow).
FORK_MAP="$(mktemp -t nodespec_fork.XXXXXX)"
NONSPEC_SET="$(mktemp -t nodespec_nonspec.XXXXXX)"
# VEC-RUN-SHORT flag passed ACROSS the command-substitution subshell boundary. sw_folded_count runs
# inside `sw="$(...)"`, so a shell variable it sets is lost when that subshell exits — the structural
# invariant would be silently dead (image-island bug: VEC-SHORT never tripped, ops mis-reported as
# EXTRA). A file write survives the subshell, so the function stamps 1/0 here and the parent reads it
# back via sw_vec_run_short after each `sw=$(...)`.
VEC_SHORT_FLAG="$(mktemp -t nodespec_vecshort.XXXXXX)"
printf '0' > "$VEC_SHORT_FLAG"
# Build the TYPE<TAB><OpName>.cs fork map. ONE awk pass per authority-carrying file (no nested process
# substitution — bash 3.2 segfaults on deep `< <()` nesting): the awk emits a line per (declared type ×
# authority .cs) found in that file's leading header + body type= decls.
_build_fork_map() {
  local f flist; flist="$(mktemp -t nodespec_flist.XXXXXX)"
  grep -rlE '@tixl:|TiXL authority:' "$SW_RT" "$SW_APP" --include='*.cpp' 2>/dev/null > "$flist"
  while IFS= read -r f; do
    [ -z "$f" ] && continue
    awk '
      # Phase 1: accumulate the LEADING // comment block to find the authority .cs.
      NR==1 {inhdr=1}
      inhdr && /^[[:space:]]*\/\// {
        if (match($0, /@tixl:[[:space:]]*[A-Za-z0-9_]+/)) {
          a=substr($0,RSTART,RLENGTH); sub(/^@tixl:[[:space:]]*/,"",a); auth=a".cs"
        }
        if (index($0,"TiXL authority:")>0) grab=3
        if (grab>0) { abuf=abuf " " $0; grab-- }
        next
      }
      inhdr && /^[[:space:]]*$/ && hdrseen==0 { next }
      inhdr { inhdr=0
        if (auth=="" && match(abuf, /[A-Za-z0-9_]+\.(cs|\{cs)/)) {
          t=substr(abuf,RSTART,RLENGTH); sub(/\.(cs|\{cs)$/,"",t); auth=t".cs"
        }
      }
      # Phase 2: scan the whole file for NodeSpec type = "X" declarations.
      /type[[:space:]]*=[[:space:]]*"[A-Za-z0-9]+"/ {
        s=$0
        while (match(s, /type[[:space:]]*=[[:space:]]*"[A-Za-z0-9]+"/)) {
          tok=substr(s,RSTART,RLENGTH)
          sub(/^type[[:space:]]*=[[:space:]]*"/,"",tok); sub(/"$/,"",tok)
          if (tok!="") types[tok]=1
          s=substr(s,RSTART+RLENGTH)
        }
      }
      END{
        if (auth=="") exit
        for (k in types) print k "\t" auth
      }
    ' "$f" >> "$FORK_MAP"
  done < "$flist"
  rm -f "$flist"
}
_build_fork_map

# type → .cs basename map. Default is "<Type>.cs". A fork-map entry (workitem B header authority) is
# honored ONLY when it is a GENUINE rename: the authority basename differs from "<Type>.cs" AND no
# literal "<Type>.cs" exists anywhere under Lib. This rejects FALSE map entries — an authority-carrying
# leaf (e.g. point_ops_blur.cpp, header @tixl: Blur) often builds OTHER nodes in its golden/test body
# (`type = "RadialPoints"`), which would otherwise mis-map RadialPoints→Blur.cs. RadialPoints.cs exists,
# so its own name wins; DoyleSpiralPoints.cs does NOT exist (TiXL renamed it to DoyleSpiralPoints2.cs),
# so the fork map's DoyleSpiralPoints2.cs is honored. No hand-maintained rename table.
cs_for_type() {
  local h; h="$(awk -F'\t' -v k="$1" '$1==k{print $2; exit}' "$FORK_MAP")"
  if [ -n "$h" ] && [ "$h" != "$1.cs" ]; then
    # genuine rename only if the literal <Type>.cs is absent under Lib
    if [ -z "$(find "$TIXL_LIB" -name "$1.cs" 2>/dev/null | grep -vE '_obsolete|/_' | head -1)" ]; then
      echo "$h"; return
    fi
  fi
  echo "$1.cs"
}

# cs_path <Type> [island] — resolve the .cs full path. Generator types live in point/generate (the
# original gate scope). When an <island> is given (field/mesh/image…), search ONLY that island subtree
# (workitem A: islands' .cs scatter under sub-dirs, e.g. field/generate/sdf), excluding _obsolete and
# leading-underscore helper dirs. Falls back to a Lib-wide find so flow/other islands still resolve.
# Echoes the path, or "" if not found.
cs_path() {
  local base; base="$(cs_for_type "$1")"
  local island="${2:-}"
  if [ -f "$TIXL_GEN/$base" ]; then echo "$TIXL_GEN/$base"; return; fi
  if [ -n "$island" ] && [ -d "$TIXL_LIB/$island" ]; then
    local hit; hit="$(find "$TIXL_LIB/$island" -name "$base" 2>/dev/null | grep -vE '_obsolete|/_' | head -1)"
    if [ -n "$hit" ]; then echo "$hit"; return; fi
  fi
  # flow / other Lib islands: find the first non-_obsolete match under Lib.
  find "$TIXL_LIB" -name "$base" 2>/dev/null | grep -vE '_obsolete|/_' | head -1
}

# known_fork_count <Type> — number of TiXL [Input] lines that sw intentionally does NOT expose as a
# param, recorded as named FORKs (faithful-dead). These are added to the sw side before the compare,
# so the gate reads "sw_folded + known_forks == TiXL". Each entry MUST be justified in the op's source
# header (the gate stays loud about genuine omissions; only listed, documented forks are absolved).
#   • PointsOnMesh: IsEnabled (PointsOnMesh.cs:28-29) routes into the generic flow/Execute wrapper's
#     IsEnabled slot (guid d68b5569-…, flow/Execute.t3ui) — the generic "skip this op's GPU pass"
#     toggle, not a node-specific knob. Same shape as CombineMeshes' deferred IsEnabled.
#     See point_ops_pointsonmesh.cpp header (NAMED TiXL forks).
#   • flow context-var Set*Var: TiXL's ONE Set{Int,Float,Vec3,Bool}Var node carries a SubGraph(Command)
#     input + (Int/Float) a ClearAfterExecution. sw's two-rail model can't put a Float/Vec output AND a
#     Command output on one node-spec, so the no-SubGraph VALUE write stayed the value-rail "Set*Var"
#     (node_registry_math_contextvar.cpp / stateful_value_ops_context_vars.cpp) and the SubGraph-scoped half
#     is the separate Command type "Set*VarCmd" (point_ops_setvarcmd.cpp), which DOES carry SubGraph +
#     ClearAfterExecution. So on the value-rail node those inputs are intentional forks (behaviour-faithful,
#     spelling-forked). SetIntVar/SetFloatVar fork 2 (SubGraph + ClearAfterExecution); SetVec3Var/SetBoolVar
#     fork 1 (SubGraph; their .cs has no ClearAfterExecution). See point_ops_setvarcmd.cpp / .h headers.
known_fork_count() {
  case "$1" in
    PointsOnMesh) echo 1 ;;   # IsEnabled = generic flow/Execute graph-wrapper toggle
    SetIntVar)    echo 2 ;;   # SubGraph + ClearAfterExecution → SetIntVarCmd (Command rail)
    SetFloatVar)  echo 2 ;;   # SubGraph + ClearAfterExecution → SetFloatVarCmd
    SetVec3Var)   echo 1 ;;   # SubGraph → SetVec3VarCmd (no ClearAfterExecution in .cs)
    SetBoolVar)   echo 1 ;;   # SubGraph → SetBoolVarCmd (no ClearAfterExecution in .cs)
    *)            echo 0 ;;
  esac
}

# The flow-island type set the gate sweeps for --all-flow. = the sw context-var + log NodeSpec types whose
# TiXL [Input] count the param-completion fan-out closed (value-rail Set*/Get*Var carry their forks above).
ALL_FLOW=(
  SetIntVar SetFloatVar SetVec3Var SetBoolVar GetIntVar LogMessage
)

# The generator set the gate sweeps for --all-generators. = the sw NodeSpec generator types.
ALL_GENERATORS=(
  RadialPoints LinePoints GridPoints SpherePoints
  HexGridPoints DoyleSpiralPoints RepetitionPoints CommonPointSets
  BoundingBoxPoints MeshVerticesToPoints PointsOnMesh PointTrailFast PointTrail
)

# tixl_input_count <Type> [island] — folded TiXL [Input] count (comment lines stripped so commented-out
# inputs like RepetitionPoints' Vector3 Scale don't inflate the count). Echoes the count, or "?"
# if the .cs is missing (so the caller can flag a missing authority file).
tixl_input_count() {
  local cs; cs="$(cs_path "$1" "${2:-}")"
  if [ -z "$cs" ] || [ ! -f "$cs" ]; then echo "?"; return; fi
  grep -vE '^[[:space:]]*//' "$cs" | grep -c '\[Input('
}

# ---- non-NodeSpec cook-path set (texReg/cmdReg, PRECOMPUTED ONCE) ----
# Types registered via registerTexOp / registerCmdOp. A type in this set that ALSO lacks a NodeSpec
# (--dump-nodespec empty) cooks on a non-NodeSpec path → N/A, not a param缺口. (A type with BOTH a
# NodeSpec AND a cmdReg hook — e.g. LogMessage — is still NodeSpec-driven; the empty-dump test gates
# that.) Precomputed to a set FILE (bash 3.2 = no assoc arrays) so the island sweep doesn't re-grep
# the tree per type.
grep -rhoE 'register(Tex|Cmd)Op\("[A-Za-z0-9]+"' "$SW_RT" --include='*.cpp' 2>/dev/null \
  | grep -oE '"[A-Za-z0-9]+"' | tr -d '"' | sort -u > "$NONSPEC_SET"

# is_non_nodespec_path <Type> — true (0) if this sw type cooks via a NON-NodeSpec registry.
is_non_nodespec_path() {
  grep -qxF "$1" "$NONSPEC_SET"
}

# sw_folded_count <Type> — the FOLDED_LOGICAL_COUNT line from --dump-nodespec, or "?" if unknown.
# Stamps VEC_SHORT_FLAG with 1 when the dump tripped its structural invariant (exit code 4 = a Vec
# head declared arity N but didn't lay down N-1 component ports → author bug, must be loud). We write
# a FILE, not a shell var: this function is always called as `sw="$(sw_folded_count ...)"`, and a var
# set inside that subshell would not survive — the parent reads the flag back via sw_vec_run_short.
sw_folded_count() {
  local out rc short=0
  out="$("$BIN" --dump-nodespec "$1" 2>/dev/null)"; rc=$?
  [ "$rc" -eq 4 ] && short=1
  printf '%s' "$short" > "$VEC_SHORT_FLAG"
  if [ -z "$out" ]; then echo "?"; return; fi
  echo "$out" | sed -n 's/^FOLDED_LOGICAL_COUNT: //p'
}

# sw_vec_run_short — true (0) if the LAST sw_folded_count call tripped VEC-RUN-SHORT. Reads the flag
# file the function stamped (survives the `sw=$(...)` subshell, unlike a plain variable).
sw_vec_run_short() {
  [ "$(cat "$VEC_SHORT_FLAG" 2>/dev/null)" = "1" ]
}

# gate_one <Type> [island] — returns 0 if sw==TiXL, 1 otherwise. Prints a one-line verdict. When an
# <island> is given, the .cs is resolved island-scoped (workitem A). A type that cooks via a non-NodeSpec
# registry (texReg/cmdReg) is marked N/A (not a缺口) and returns 0.
gate_one() {
  local t="$1" island="${2:-}"
  local sw tixl forks eff
  sw="$(sw_folded_count "$t")"
  tixl="$(tixl_input_count "$t" "$island")"
  forks="$(known_fork_count "$t")"
  # Cook-path classification: a type with NO NodeSpec (--dump-nodespec empty → sw="?") that cooks via a
  # texReg/cmdReg registry is a non-NodeSpec path → N/A, not a real缺口. (A type that has BOTH a NodeSpec
  # AND a cmdReg hook — e.g. LogMessage, a Command-rail op WITH params — is still NodeSpec-driven: the
  # dump succeeds, so it is gated normally. The N/A only catches the spec-less texReg/cmdReg ops.)
  if [ "$sw" = "?" ] && is_non_nodespec_path "$t"; then
    printf '  · %-22s N/A (non-NodeSpec path: texReg/cmdReg, no param spec)\n' "$t"
    return 0
  fi
  if [ "$sw" = "?" ]; then
    printf '  ✗ %-22s sw=UNKNOWN (node type not registered?)  TiXL=%s\n' "$t" "$tixl"
    return 1
  fi
  # Structural invariant: a short Vec run is an author bug, surface it LOUD even if counts happen to align.
  if sw_vec_run_short; then
    printf '  ✗ %-22s VEC-RUN-SHORT (a Vec head lacks its component ports — see --dump-nodespec %s)\n' "$t" "$t"
    return 1
  fi
  if [ "$tixl" = "?" ]; then
    printf '  ✗ %-22s sw=%s  TiXL=MISSING .cs (%s, not found under Lib)\n' "$t" "$sw" "$(cs_for_type "$t")"
    return 1
  fi
  # effective sw count = exposed params + intentionally-deferred named forks
  eff=$(( sw + forks ))
  if [ "$eff" = "$tixl" ]; then
    if [ "$forks" -gt 0 ]; then
      printf '  ✓ %-22s sw=%s + %s fork == TiXL=%s\n' "$t" "$sw" "$forks" "$tixl"
    else
      printf '  ✓ %-22s sw=%s == TiXL=%s\n' "$t" "$sw" "$tixl"
    fi
    return 0
  fi
  local delta=$(( eff - tixl ))
  local dir
  if [ "$delta" -lt 0 ]; then dir="sw MISSING $(( -delta )) param(s)"; else dir="sw has $delta EXTRA param(s)"; fi
  if [ "$forks" -gt 0 ]; then
    printf '  ✗ %-22s sw=%s + %s fork != TiXL=%s  (%s)\n' "$t" "$sw" "$forks" "$tixl" "$dir"
  else
    printf '  ✗ %-22s sw=%s != TiXL=%s  (%s)\n' "$t" "$sw" "$tixl" "$dir"
  fi
  return 1
}

rc=0
if [ "${1:-}" = "--all-generators" ]; then
  echo "[nodespec_integrity] sweeping ${#ALL_GENERATORS[@]} generators (sw folded vs TiXL [Input])..."
  for t in "${ALL_GENERATORS[@]}"; do
    gate_one "$t" || rc=1
  done
  if [ "$rc" -eq 0 ]; then echo "[nodespec_integrity] OK — every generator's param set matches TiXL."
  else echo "[nodespec_integrity] FAIL — one or more generators diverge from TiXL (see ✗ rows)."; fi
elif [ "${1:-}" = "--all-flow" ]; then
  echo "[nodespec_integrity] sweeping ${#ALL_FLOW[@]} flow-island context-var/log ops (sw folded + forks vs TiXL [Input])..."
  for t in "${ALL_FLOW[@]}"; do
    gate_one "$t" || rc=1
  done
  if [ "$rc" -eq 0 ]; then echo "[nodespec_integrity] OK — every flow op's param set matches TiXL."
  else echo "[nodespec_integrity] FAIL — one or more flow ops diverge from TiXL (see ✗ rows)."; fi
elif [ "${1:-}" = "--island" ]; then
  # --island <name>: sweep every REGISTERED sw type whose authoritative .cs resolves under Lib/<name>/.
  # The type set is DERIVED (--dump-nodespec-types ∩ island subtree), never hardcoded — adding an op
  # auto-enters the sweep, deleting one auto-leaves. Prints a per-island缺口 panorama (MATCH / MISSING /
  # EXTRA / N-A non-NodeSpec). Returns 0 if no real param缺口 (MISSING+EXTRA, after forks); else 1.
  island="${2:-}"
  if [ -z "$island" ]; then echo "usage: $0 --island <name>" >&2; exit 2; fi
  if [ ! -d "$TIXL_LIB/$island" ]; then
    echo "✗ nodespec_integrity: island subtree not found: $TIXL_LIB/$island" >&2; exit 3
  fi
  echo "[nodespec_integrity] sweeping island '$island' (registered types whose .cs lives under Lib/$island/)..."
  # All registered NodeSpec types from the binary (portable read loop — no mapfile dependency).
  ALL_TYPES_RAW="$("$BIN" --dump-nodespec-types 2>/dev/null | sort -u)"
  swept=0; match=0; missing=0; extra=0; na=0; short=0
  while IFS= read -r t; do
    [ -z "$t" ] && continue
    # Belongs to this island iff its authoritative .cs resolves UNDER Lib/<island>/.
    csp="$(cs_path "$t" "$island")"
    case "$csp" in
      "$TIXL_LIB/$island"/*) : ;;   # in island
      *) continue ;;                  # not this island — skip
    esac
    swept=$((swept+1))
    sw="$(sw_folded_count "$t")"; tixl="$(tixl_input_count "$t" "$island")"; forks="$(known_fork_count "$t")"
    # Non-NodeSpec path (spec-less texReg/cmdReg) → N/A, not a real缺口.
    if [ "$sw" = "?" ] && is_non_nodespec_path "$t"; then
      gate_one "$t" "$island"; na=$((na+1)); continue
    fi
    if sw_vec_run_short; then
      gate_one "$t" "$island"; short=$((short+1)); rc=1; continue
    fi
    if [ "$sw" = "?" ] || [ "$tixl" = "?" ]; then gate_one "$t" "$island"; rc=1; continue; fi
    eff=$(( sw + forks )); delta=$(( eff - tixl ))
    if [ "$delta" -eq 0 ]; then match=$((match+1)); gate_one "$t" "$island"
    elif [ "$delta" -lt 0 ]; then missing=$((missing+1)); rc=1; gate_one "$t" "$island"
    else extra=$((extra+1)); rc=1; gate_one "$t" "$island"; fi
  done <<< "$ALL_TYPES_RAW"
  echo "[nodespec_integrity] island '$island': swept=$swept  MATCH=$match  MISSING=$missing  EXTRA=$extra  N/A=$na  VEC-SHORT=$short"
elif [ -n "${1:-}" ]; then
  gate_one "$1" || rc=1
else
  rm -f "$FORK_MAP" "$NONSPEC_SET" "$VEC_SHORT_FLAG"
  echo "usage: $0 <NodeType> | --all-generators | --all-flow | --island <name>" >&2
  exit 2
fi
rm -f "$FORK_MAP" "$NONSPEC_SET" "$VEC_SHORT_FLAG"
exit "$rc"

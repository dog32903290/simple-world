#!/usr/bin/env bash
# op_census.sh — TiXL ⇄ simple_world 節點 port 進度查詢引擎（標準化檢索）
#
# 為什麼存在：手刻 set-diff 一再踩坑（工具漏數 numbers 子目錄、sw 葉檔命名 value_op_ 單數
# vs *_ops_ 複數混用、TiXL 用 .cs 檔數含 helper 而非真節點）。把方法論固化成一個可重複跑的
# 真相來源，攻縫前後各跑一次就知道「解鎖了哪些 op」。
#
# 方法論（三個定義，全部可驗）：
#   1. TiXL 真節點 = 繼承 `: Instance<>` 的 class（能放進 graph），扣 _Old/_obsolete/_前綴/__OBSOLETE。
#      ——不是數 .cs 檔（那含 helper/util，會膨脹到 931）。真節點 = 749。
#   2. sw 已 port = 三個來源的 union（大小寫無視）：
#        (a) 葉檔 stem：runtime/<prefix>_<name>.cpp，prefix ∈ {point_ops value_op field_ops
#            string_ops mesh_ops pointlist_ops stateful_value_ops floatlist_ops host_scalar_ops
#            colorlist_ops gradient_ops stringlist_ops}
#        (b) register*Op("Name") 呼叫
#        (c) registry / node_registry 表裡的 PascalCase 字串字面量
#   3. 比對 = 小寫化後 set-diff（sw 有命名 fork 時可能略低估 → 標 ⚠）。
#
# 用法：
#   tools/op_census.sh                 → 按島總表
#   tools/op_census.sh <island>        → 該島逐顆 [x]已做 / [ ]未做
#   tools/op_census.sh --undone <isl>  → 只列未做
#   tools/op_census.sh --tsv           → 機器可讀 TSV（island\top\tstatus）給後續工具/grep
#
# 島名：numbers image point render io field mesh string flow particle data
set -euo pipefail
cd "$(dirname "$0")/.."

TIXL=external/tixl/Operators/Lib
SWRT=app/src/runtime
SWAPP=app/src/app
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

# ---- build sw-ported set (lowercase) ----
{
  # (a) 葉檔 stem（所有已知前綴）
  find "$SWRT" -name '*.cpp' | sed -E 's#.*/##; s#\.cpp$##' \
    | sed -E 's#^(point_ops|value_op|field_ops|string_ops|mesh_ops|pointlist_ops|stateful_value_ops|floatlist_ops|host_scalar_ops|colorlist_ops|gradient_ops|stringlist_ops)_##' \
    | grep -ivE 'registry|_params$|golden|selftest|slots|^register|templates|cases|decls|^node_|^point_graph|^resident|^compound$|^field$|^graph$|^bpm|^curve$|^audio$|^variation|^point$|^field_graph'
  # (b) register*Op("Name")
  grep -rhoE 'register[A-Za-z]*\(\s*"[A-Za-z0-9_]+"' "$SWRT" "$SWAPP" 2>/dev/null \
    | grep -oE '"[A-Za-z0-9_]+"' | tr -d '"'
  # (c) registry / node_registry 表的 PascalCase 字串
  find "$SWRT" \( -name '*_op_registry.*' -o -name '*_ops_registry.*' -o -name 'node_registry_*.cpp' \) \
    | xargs grep -hoE '"[A-Z][A-Za-z0-9]+"' 2>/dev/null | tr -d '"'
} | tr 'A-Z' 'a-z' | sort -u > "$TMP/sw.txt"

# ---- build TiXL real-op list per island (file: island<TAB>OpName) ----
grep -rlE ': *Instance<' "$TIXL" --include='*.cs' 2>/dev/null \
  | grep -viE '_Old|_obsolete|/_|__OBSOLETE' \
  | sed -E "s#^$TIXL/([^/]+)/.*/([^/]+)\.cs#\1\t\2#; s#^$TIXL/([^/]+)/([^/]+)\.cs#\1\t\2#" \
  | sort -u > "$TMP/tixl.txt"

ISLANDS="numbers image point render io field mesh string flow particle data"

emit_status() { # island -> prints "op<TAB>x|space" lines
  local isl="$1"
  awk -F'\t' -v i="$isl" '$1==i{print $2}' "$TMP/tixl.txt" | sort -u | while read -r op; do
    lc=$(printf '%s' "$op" | tr 'A-Z' 'a-z')
    if grep -qxF "$lc" "$TMP/sw.txt"; then echo -e "$op\tx"; else echo -e "$op\t "; fi
  done
}

case "${1:-}" in
  --tsv)
    for isl in $ISLANDS; do
      emit_status "$isl" | while IFS=$'\t' read -r op st; do
        [ "$st" = x ] && s=done || s=todo; echo -e "$isl\t$op\t$s"; done
    done ;;
  --undone)
    isl="${2:?island name required}"
    echo "== $isl 未做 =="
    emit_status "$isl" | awk -F'\t' '$2!="x"{print "  "$1}' ;;
  "")
    printf "%-10s %6s %6s %6s  %s\n" "island" "total" "done" "todo" "todo%"
    gt=0; gd=0
    for isl in $ISLANDS; do
      out=$(emit_status "$isl"); [ -z "$out" ] && continue
      t=$(printf '%s\n' "$out" | grep -c .)
      d=$(printf '%s\n' "$out" | awk -F'\t' '$2=="x"{c++}END{print c+0}')
      td=$((t-d))
      pct=$(awk -v t="$t" -v td="$td" 'BEGIN{if(t)printf "%d%%",td*100/t}')
      printf "%-10s %6s %6s %6s  %s\n" "$isl" "$t" "$d" "$td" "$pct"
      gt=$((gt+t)); gd=$((gd+d))
    done
    echo "-----------------------------------------------"
    gpct=$(awk -v t="$gt" -v td="$((gt-gd))" 'BEGIN{if(t)printf "%d%%",td*100/t}')
    printf "%-10s %6s %6s %6s  %s\n" "TOTAL" "$gt" "$gd" "$((gt-gd))" "$gpct"
    echo "(⚠ sw 命名 fork 未追蹤時 done 可能略低估；逐顆查 tools/op_census.sh <island>)" ;;
  *)
    isl="$1"
    echo "== $isl ([x]=已 port  [ ]=未做) =="
    emit_status "$isl" | while IFS=$'\t' read -r op st; do
      [ "$st" = x ] && echo "  [x] $op" || echo "  [ ] $op"; done ;;
esac
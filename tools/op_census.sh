#!/usr/bin/env bash
# op_census.sh — TiXL ⇄ simple_world 克隆進度查詢引擎（標準化檢索，消滅「看進度就幻覺」）
#
# 為什麼存在：手寫 census（OP_BACKLOG 數字 / SEAM_STATE / MASTER_PLAN）會 stale、會互相打架，
# 導致「看一下進度」要耗大量 token 逐檔對、還產生幻覺（實證：SEAM_STATE 曾把 Render2dField 誤報
# 成 BUILT，本工具一跑就抓出 0 命中）。把真相從工具 derive，幾個命令就知道克隆到哪、剩幾顆、哪條縫先做。
#
# === 三個命令回答柏為的三個問題 ===
#   tools/op_census.sh --overview   → 克隆 TiXL 到哪了？(總進度 + 每島 + 縫地圖摘要) ★先跑這個
#   tools/op_census.sh --seams      → 哪幾條縫工作量大、該先做哪條？(縫地圖,按解鎖量排序)
#   tools/op_census.sh <island>     → 某島逐顆 [x]做了/[ ]沒做
#
# 其他：
#   tools/op_census.sh              → 逐島 done/todo 總表
#   tools/op_census.sh --undone <isl>  只列某島未做
#   tools/op_census.sh --tsv        機器可讀 island<TAB>op<TAB>done|todo
#
# === 方法論（三個定義，全可驗）===
#   1. TiXL 真節點 = 繼承 `: Instance<>` 的 class，扣 _Old/_obsolete/_前綴/__OBSOLETE = 749。
#      不是數 .cs 檔（含 helper 會膨脹到 931 → 這是 570/749 幻覺的根源）。
#   2. sw 已 port = 葉檔 stem(含 value_op_ 等所有前綴) + register*Op("X") + registry 表 PascalCase 字串
#      + 葉檔 header 的 `// TiXL authority: .../<OpName>.cs` 權威宣告(第四源,authoritative 不猜),
#      大小寫無視 set-diff。第四源治 sw 命名 fork(例 chromab→ChromaticAbberation,int2tovec2→
#      Int2ToVector2):filename≠TiXL-name 時靠 header 宣告對上,消滅 false-todo 誤派重 port。
#   3. 縫歸類 = tools/seam_map.tsv（權威 SSOT）的 path_regex 套未做 op 的 TiXL 路徑。
set -uo pipefail  # 不用 -e：報表腳本裡 grep 無匹配的 return 1 是正常,不該中止
cd "$(dirname "$0")/.."
TIXL=external/tixl/Operators/Lib
SWRT=app/src/runtime; SWAPP=app/src/app
SEAMMAP=tools/seam_map.tsv
TMP=$(mktemp -d); trap 'rm -rf "$TMP"' EXIT

# ---- sw-ported set (lowercase) ----
{
  find "$SWRT" -name '*.cpp' | sed -E 's#.*/##; s#\.cpp$##' \
    | sed -E 's#^(point_ops|value_op|field_ops|string_ops|mesh_ops|pointlist_ops|stateful_value_ops|floatlist_ops|host_scalar_ops|colorlist_ops|gradient_ops|stringlist_ops)_##' \
    | grep -ivE 'registry|_params$|golden|selftest|slots|^register|templates|cases|decls|^node_|^point_graph|^resident|^compound$|^field$|^graph$|^bpm|^curve$|^audio$|^variation|^point$|^field_graph'
  grep -rhoE 'register[A-Za-z]*\(\s*"[A-Za-z0-9_]+"' "$SWRT" "$SWAPP" 2>/dev/null | grep -oE '"[A-Za-z0-9_]+"' | tr -d '"'
  find "$SWRT" \( -name '*_op_registry.*' -o -name '*_ops_registry.*' -o -name 'node_registry_*.cpp' \) \
    | xargs grep -hoE '"[A-Z][A-Za-z0-9]+"' 2>/dev/null | tr -d '"'
  # 4th source: authoritative `// TiXL authority: .../<OpName>.cs` declaration in leaf headers.
  # Catches sw命名 fork (filename≠TiXL-name). Scoped to the LEADING comment header block only
  # (so body cross-refs like "mirrors value_op_addint2.cpp" don't leak); reads the authority line
  # + up to 2 wrapped continuation lines; accepts both `Name.cs` and `Name.{cs,hlsl,t3}` forms.
  find "$SWRT" "$SWAPP" -name '*.cpp' | while read -r f; do
    awk '
      /^[[:space:]]*\/\// {hdr=hdr $0 "\n"; next}
      /^[[:space:]]*$/ && hdr=="" {next}
      {exit}
      END{printf "%s", hdr}
    ' "$f" | awk '
      # bare canonical declaration: `// @tixl: <OpName>` (highest-friction-free key)
      /@tixl:/ { if (match($0, /@tixl:[[:space:]]*[A-Za-z0-9_]+/)) {
                   t=substr($0,RSTART,RLENGTH); sub(/^@tixl:[[:space:]]*/,"",t); print t } }
      /TiXL authority:/ {grab=3}
      grab>0 {buf=buf " " $0; grab--}
      END{
        while (match(buf, /[A-Za-z0-9_]+\.(cs|\{cs)/)) {
          tok=substr(buf, RSTART, RLENGTH); sub(/\.(cs|\{cs)$/,"",tok); print tok;
          buf=substr(buf, RSTART+RLENGTH)
        }
      }'
  done
} | tr 'A-Z' 'a-z' | sort -u > "$TMP/sw.txt"

# ---- TiXL real ops: island<TAB>op<TAB>relpath(island/sub/op) ----
grep -rlE ': *Instance<' "$TIXL" --include='*.cs' 2>/dev/null \
  | grep -viE '_Old|_obsolete|/_|__OBSOLETE' \
  | sed -E "s#^$TIXL/##; s#\.cs\$##" \
  | awk -F'/' '{print $1"\t"$NF"\t"$0}' | sort -u > "$TMP/tixl.txt"

# ---- split done/todo (preserve relpath) ----
awk -F'\t' 'NR==FNR{sw[$1]=1;next}{print (sw[tolower($2)]?"done":"todo")"\t"$1"\t"$2"\t"$3}' \
  "$TMP/sw.txt" "$TMP/tixl.txt" > "$TMP/status.txt"

ISLANDS="numbers image point render io field mesh string flow particle data"

# classify every todo relpath into a seam (first matching rule wins)
build_seam_assign() { # -> $TMP/assign.txt : seam<TAB>relpath
  awk -F'\t' '$1=="todo"{print $4}' "$TMP/status.txt" | sort -u > "$TMP/remain.txt"
  : > "$TMP/assign.txt"
  while IFS=$'\t' read -r sid kind blast rx desc; do
    case "$sid" in ''|\#*) continue;; esac
    grep -E "$rx" "$TMP/remain.txt" 2>/dev/null | sed "s/^/$sid\t/" >> "$TMP/assign.txt" || true
    grep -vE "$rx" "$TMP/remain.txt" 2>/dev/null > "$TMP/remain2.txt" || true
    mv "$TMP/remain2.txt" "$TMP/remain.txt"
  done < "$SEAMMAP"
  # leftover = unclassified
  sed "s/^/__unclassified\t/" "$TMP/remain.txt" >> "$TMP/assign.txt"
}

seam_meta() { awk -F'\t' -v s="$1" '$1==s{print $2"\t"$3"\t"$5; exit}' "$SEAMMAP"; }

case "${1:-}" in
  --tsv)
    awk -F'\t' '{print $2"\t"$3"\t"$1}' "$TMP/status.txt" ;;

  --undone)
    isl="${2:?island name required}"; echo "== $isl 未做 =="
    awk -F'\t' -v i="$isl" '$1=="todo"&&$2==i{print "  "$3}' "$TMP/status.txt" | sort ;;

  --seams)
    build_seam_assign
    echo "════ 縫地圖 — 每條縫卡幾顆未做節點 (tools/seam_map.tsv 為準) ════"
    echo ""
    for grp in seam-build leaf-ready domain-blocked; do
      case "$grp" in
        seam-build)    title="◆ seam-build  先蓋縫→解鎖一批 (柏為要的「先做完能大量產出」,按解鎖量排序)";;
        leaf-ready)    title="○ leaf-ready   縫已建,直接採節點 (sw-batch 自走首選,零等待)";;
        domain-blocked)title="△ domain-blocked  需全新大島/裝置 (晚做或柏為域)";;
      esac
      echo "$title"
      # 該 kind 的 seam,按 count 降序
      awk -F'\t' '{print $1}' "$TMP/assign.txt" | sort | uniq -c \
        | while read -r cnt sid; do
            meta=$(seam_meta "$sid"); k=$(echo "$meta"|cut -f1); b=$(echo "$meta"|cut -f2); d=$(echo "$meta"|cut -f3)
            [ "$k" = "$grp" ] && printf '%s\t%s\t%s\t%s\n' "$cnt" "$sid" "$b" "$d"
          done | sort -rn \
        | while IFS=$'\t' read -r cnt sid b d; do printf "   %3d  %-16s [%s] %s\n" "$cnt" "$sid" "$b" "$d"; done
      echo ""
    done
    un=$(awk -F'\t' '$1=="__unclassified"' "$TMP/assign.txt" | wc -l | tr -d ' ')
    if [ "$un" -gt 0 ]; then
      echo "   ⚠ $un 顆未歸類 (seam_map.tsv 規則沒涵蓋→補規則): $(awk -F'\t' '$1=="__unclassified"{print $2}' "$TMP/assign.txt" | head -8 | tr '\n' ' ')"
    fi
    exit 0 ;;

  --overview)
    gt=$(wc -l < "$TMP/status.txt" | tr -d ' ')
    gd=$(awk -F'\t' '$1=="done"' "$TMP/status.txt" | wc -l | tr -d ' ')
    gtd=$((gt-gd))
    gpct=$(awk -v d="$gd" -v t="$gt" 'BEGIN{printf "%d",d*100/t}')
    echo "════════════════════════════════════════════════════"
    echo " 克隆 TiXL 進度 — $gd / $gt 節點已 port  (${gpct}% done,  剩 $gtd)"
    echo "════════════════════════════════════════════════════"
    printf " %-9s %5s %5s %5s\n" "島" "總" "做" "剩"
    for isl in $ISLANDS; do
      t=$(awk -F'\t' -v i="$isl" '$2==i' "$TMP/status.txt" | wc -l | tr -d ' '); [ "$t" -eq 0 ] && continue
      d=$(awk -F'\t' -v i="$isl" '$2==i&&$1=="done"' "$TMP/status.txt" | wc -l | tr -d ' ')
      bar=""; [ "$((t-d))" -gt 0 ] && bar="←剩 $((t-d))"
      printf " %-9s %5s %5s %5s  %s\n" "$isl" "$t" "$d" "$((t-d))" "$bar"
    done
    echo ""
    echo " 縫地圖摘要 (詳: tools/op_census.sh --seams):"
    build_seam_assign
    echo "  ◆ 先蓋縫解鎖最多的 3 條:"
    awk -F'\t' '{print $1}' "$TMP/assign.txt" | sort | uniq -c \
      | while read -r cnt sid; do meta=$(seam_meta "$sid"); [ "$(echo "$meta"|cut -f1)" = seam-build ] && echo "$cnt $sid $(echo "$meta"|cut -f3)"; done \
      | sort -rn | head -3 | while read -r cnt sid d; do printf "     %3d 顆 ← %s (%s)\n" "$cnt" "$sid" "$d"; done
    lr=$(awk -F'\t' '{print $1}' "$TMP/assign.txt" | sort | uniq -c | while read -r cnt sid; do meta=$(seam_meta "$sid"); [ "$(echo "$meta"|cut -f1)" = leaf-ready ] && echo "$cnt"; done | awk '{s+=$1}END{print s+0}')
    echo "  ○ leaf-ready (縫已建可直接採): $lr 顆散在各島"
    echo "  (⚠ done 可能略低估 sw 命名 fork;逐顆複查 tools/op_census.sh <island>)" ;;

  "")
    printf "%-10s %6s %6s %6s  %s\n" "island" "total" "done" "todo" "todo%"
    gt=0; gd=0
    for isl in $ISLANDS; do
      t=$(awk -F'\t' -v i="$isl" '$2==i' "$TMP/status.txt" | wc -l | tr -d ' '); [ "$t" -eq 0 ] && continue
      d=$(awk -F'\t' -v i="$isl" '$2==i&&$1=="done"' "$TMP/status.txt" | wc -l | tr -d ' '); td=$((t-d))
      printf "%-10s %6s %6s %6s  %s\n" "$isl" "$t" "$d" "$td" "$(awk "BEGIN{if($t)printf \"%d%%\",$td*100/$t}")"
      gt=$((gt+t)); gd=$((gd+d))
    done
    echo "-----------------------------------------------"
    printf "%-10s %6s %6s %6s  %s\n" "TOTAL" "$gt" "$gd" "$((gt-gd))" "$(awk "BEGIN{printf \"%d%%\",($gt-$gd)*100/$gt}")"
    echo "(三命令: --overview 全局 / --seams 縫地圖 / <island> 逐顆)" ;;

  *)
    isl="$1"; echo "== $isl ([x]=已 port  [ ]=未做) =="
    awk -F'\t' -v i="$isl" '$2==i{print ($1=="done"?"  [x] ":"  [ ] ")$3}' "$TMP/status.txt" | sort -k1.5 ;;
esac
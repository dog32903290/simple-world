#!/usr/bin/env bash
# tools/node_health.sh — NODE HEALTH LEDGER (可信的「節點健康帳」)
#
# 為什麼存在（2026-06-29 柏為血證）：op_census 的 done-check 系統性 stale，orchestrator 一晚讀 code
# 校正 6-8 次「以為沒做其實做了」+ 反向假陽性（enum-label 裸字串 "Steps" 被當成已 port 的節點）。
# 根因兩條：① done-check 只看「單一處」（漏中央表 node_registry_math_*、漏各 registry）→ 假陰性；
# ② done-set 用鬆散 `grep '"[A-Z]..."'` 撈 registry 檔的每個引號字串 → port 名/enum-label 汙染 →
# 假陽性（image op `Steps` 因 AnimValue 的 "Steps" 內插模式 enum-label 被誤判 done）。
#
# 本工具治本：
#   • 帳以 TiXL 算子為列（925 = ground-truth universe，逐顆一行），不靠任何文件數字。
#   • done-check 多處 grep（任一命中 = done），且 ONLY 從結構錨定的位置抽節點名 —— 不撈裸引號字串：
#       1. 葉檔 stem（檔案系統真相：point_ops_*/field_ops_*/value_op_*/…）
#       2. register*Op("X") / NodeSpec type 錨定行 `{"X","X",`（中央表，非任意引號）
#       3. 葉檔 leading-header 的 `// @tixl: X` / `// TiXL authority: …/X.cs`（fork 改名權威，census source#4）
#       4. 已 build 的 binary `--dump-nodespec-types`（NodeSpec 路徑的權威已註冊集）
#       5. texReg/cmdReg cook-flow 註冊（非 NodeSpec 路徑）
#     enum-label 裸字串陣列（"Steps"/"Random"/"PerlinNoise" 等）絕不進 done-set（錨定行只認 `{"X","X",`，
#     不認 `{"a","b","c"}` 的多元素陣列）。
#   • 節點身份分類（柏為 pivot：原子手刻 + 複合 .t3 重放）：
#       真原子        = sw 做了 ∧ TiXL `.t3` Children 空（原子）→ 留（真積木）
#       壓平複合      = sw 做了 ∧ TiXL `.t3` Children 非空（複合）→ 廢棄候選（等 .t3 重放取代）
#       未做          = TiXL 有 ∧ sw 多處 done-check 全不命中
#       非NodeSpec    = sw 做了，但走 texReg/cmdReg（param 閘 N/A）
#       無header      = sw 做了某顆但對不上 TiXL 名（filename fork 又無 @tixl header）→ 盤點待補
#   • param-gap 欄：呼叫 nodespec_integrity 逐顆求值（fold-bug 已修 c0251ea → 全島可信）。
#     gate 自己分類 cook 路徑（NodeSpec → 真比對；texReg/cmdReg → N/A）+ 自動解析島（Lib-wide find）。
#     本工具不重修 fold，只接 nodespec_integrity 現有閘。
#   • 廢棄候選 + 依賴掃描：壓平複合逐顆 grep 其他節點/scenario/.scn 是否引用此 type →
#     標「可安全廢棄(無依賴) / 有依賴(挪會壞)」。只標不動 code（柏為定：先標記別動）。
#
# 用法：
#   tools/node_health.sh                 → 健康帳總表（每身份分區 + 計數 + 逐顆名稱清單）★主視圖
#   tools/node_health.sh --summary       → 只印分類統計 + 廢棄候選數 + 無-header 數
#   tools/node_health.sh --class <id>    → 只列某身份的逐顆清單 (atom|flattened|undone|nonspec|noheader)
#   tools/node_health.sh --discard       → 廢棄候選 + 依賴標記（壓平複合）
#   tools/node_health.sh --tsv           → 機器可讀：tixl_op<TAB>island<TAB>identity<TAB>done<TAB>param_gap<TAB>discard
#   tools/node_health.sh --html [file]   → 產視覺 HTML 報告（預設 docs/agent/census/node_health.html）
#   tools/node_health.sh --verify        → 抽樣對已知案例驗準（Steps/Cos/Atan2/DefineGradient/…）
#
# Read-only on external/tixl 與 source。param-gap 欄需要已 build 的 app/build/simple_world（沒有則標 no-bin）。
set -uo pipefail
cd "$(dirname "$0")/.."
REPO_ROOT="$(pwd)"

# ---- TiXL Lib root（worktree 無 external/ → 退到 main checkout）----
if [ -n "${SW_TIXL_LIB:-}" ]; then TIXL_LIB="$SW_TIXL_LIB"
elif [ -d "$REPO_ROOT/external/tixl/Operators/Lib" ]; then TIXL_LIB="$REPO_ROOT/external/tixl/Operators/Lib"
elif [ -d "$REPO_ROOT/../../../external/tixl/Operators/Lib" ]; then TIXL_LIB="$REPO_ROOT/../../../external/tixl/Operators/Lib"
else
  echo "✗ node_health: external/tixl/Operators/Lib not found (set SW_TIXL_LIB)" >&2; exit 3
fi
SWRT="app/src/runtime"; SWAPP="app/src"
BIN="${SW_BIN:-$REPO_ROOT/app/build/simple_world}"
INTEGRITY="$REPO_ROOT/tools/nodespec_integrity.sh"
TMP="$(mktemp -d)"; trap 'rm -rf "$TMP"' EXIT

# op-leaf family prefixes (真節點葉檔；非前綴檔 = 子系統基建，非節點)
LEAF_PREFIX='point_ops|value_op|field_ops|string_ops|mesh_ops|pointlist_ops|stateful_value_ops|floatlist_ops|host_scalar_ops|colorlist_ops|gradient_ops|stringlist_ops|intlist_ops'
# op-leaf stems = family-prefixed *.cpp, prefix stripped, infra/test/wrap files removed.
LEAF_STEMS="$TMP/leaf_stems.txt"
find "$SWRT" -name '*.cpp' | sed -E 's#.*/##; s#\.cpp$##' \
  | grep -E "^($LEAF_PREFIX)_" \
  | sed -E "s#^($LEAF_PREFIX)_##" \
  | grep -ivE 'registry|_params$|golden|selftest|slots|_wrap$|_decls$|_cases$|_templates$|_ext$' \
  | sort -u > "$LEAF_STEMS"

# ============================================================================
# 1. sw done-set —— ONLY 結構錨定來源（lowercase，治假陽性）
# ============================================================================
{
  # --- source 1: op-leaf 檔 stem（檔案系統真相）。ONLY 家族前綴檔（真 op 葉），剝前綴。 ---
  # 非前綴檔 = 子系統基建（audio_ingest/particle_system/compound_load…），不是節點 → 不進 done-set。
  cat "$LEAF_STEMS"

  # --- source 2a: register*Op("X") 呼叫點 ---
  grep -rhoE 'register[A-Za-z]*\(\s*"[A-Za-z0-9_]+"' "$SWRT" "$SWAPP" 2>/dev/null \
    | grep -oE '"[A-Za-z0-9_]+"' | tr -d '"'

  # --- source 2b: 中央表 NodeSpec 錨定行 `{"X", "X",` ONLY（id==displayname 重複 + 行尾，排除 ports/enum-label）---
  # port = {"id","name","Type",...}（同行更多元素）；enum-label = {"a","b","c"}（多元素無重複）。
  # 只有 NodeSpec entry 才是 `{"X","X",` 開頭、行尾僅逗號。
  find "$SWRT" \( -name 'node_registry_*.cpp' \) \
    | xargs awk '
        /^[[:space:]]*\{"[A-Za-z0-9]+",[[:space:]]*"[^"]*",[[:space:]]*$/ {
          if (match($0, /"[A-Za-z0-9]+"/)) { t=substr($0,RSTART+1,RLENGTH-2); print t }
        }' 2>/dev/null

  # --- source 3: 葉檔 leading-header 的 @tixl: / TiXL authority:（fork 改名權威；census source#4 同邏輯）---
  # 只掃 leading // 註解塊（body cross-ref "mirrors X.cs" 不洩漏）。
  find "$SWRT" "$SWAPP" -name '*.cpp' | while read -r f; do
    awk '
      /^[[:space:]]*\/\// {hdr=hdr $0 "\n"; next}
      /^[[:space:]]*$/ && hdr=="" {next}
      {exit}
      END{printf "%s", hdr}
    ' "$f" | awk '
      /@tixl:/ { if (match($0, /@tixl:[[:space:]]*[A-Za-z0-9_]+/)) {
                   t=substr($0,RSTART,RLENGTH); sub(/^@tixl:[[:space:]]*/,"",t); print t } }
      /TiXL authority:/ {grab=3}
      grab>0 {buf=buf " " $0; grab--}
      END{
        while (match(buf, /[A-Za-z0-9_]+\.(cs|\{cs|t3)/)) {
          tok=substr(buf, RSTART, RLENGTH); sub(/\.(cs|\{cs|t3)$/,"",tok); print tok;
          buf=substr(buf, RSTART+RLENGTH)
        }
      }'
  done

  # --- source 4: binary --dump-nodespec-types（NodeSpec 路徑權威已註冊集）---
  if [ -x "$BIN" ]; then "$BIN" --dump-nodespec-types 2>/dev/null; fi
} | tr 'A-Z' 'a-z' | sort -u > "$TMP/sw_done.txt"

# ---- 非 NodeSpec 路徑集（texReg/cmdReg，lowercase）----
grep -rhoE 'register(Tex|Cmd)Op\(\s*"[A-Za-z0-9_]+"' "$SWRT" --include='*.cpp' 2>/dev/null \
  | grep -oE '"[A-Za-z0-9_]+"' | tr -d '"' | tr 'A-Z' 'a-z' | sort -u > "$TMP/nonspec.txt"

# ============================================================================
# 2. TiXL universe —— 每顆 .t3：op<TAB>island<TAB>identity(atom/compound)<TAB>relpath
# ============================================================================
# 原子 = "Children": []（空）；複合 = Children 非空。
find "$TIXL_LIB" -name '*.t3' 2>/dev/null | grep -viE '/_obsolete|/[^/]*_[Oo]ld\.t3' | while read -r f; do
  rel="${f#$TIXL_LIB/}"
  op="$(basename "$f" .t3)"
  island="$(echo "$rel" | cut -d/ -f1)"
  if grep -q '"Children": \[\]' "$f"; then kind="atom"; else kind="compound"; fi
  printf '%s\t%s\t%s\t%s\n' "$op" "$island" "$kind" "$rel"
done | sort -u > "$TMP/tixl.txt"

# ============================================================================
# 3. classify every TiXL op → identity ledger
#    tixl_op<TAB>island<TAB>identity<TAB>done(y/n)<TAB>tixl_kind(atom/compound)<TAB>relpath
# ============================================================================
awk -F'\t' '
  FILENAME==DONE { done[$1]=1; next }
  FILENAME==NONSPEC { nonspec[$1]=1; next }
  {
    op=$1; island=$2; kind=$3; rel=$4
    lop=tolower(op)
    isdone = (lop in done)
    if (isdone) {
      if (lop in nonspec) ident="nonspec"
      else if (kind=="atom") ident="atom"
      else ident="flattened"
    } else ident="undone"
    print op "\t" island "\t" ident "\t" (isdone?"y":"n") "\t" kind "\t" rel
  }
' DONE="$TMP/sw_done.txt" NONSPEC="$TMP/nonspec.txt" "$TMP/sw_done.txt" "$TMP/nonspec.txt" "$TMP/tixl.txt" > "$TMP/ledger.txt"

# ---- no-header bucket (FILE-level): op-leaf FILES that (a) carry NO @tixl:/authority header AND
# (b) whose stem doesn't equal a TiXL op name AND (c) hold no register*Op("X") (single-op leaf, not a
# multi-op registrar). Such a file ports a TiXL op under a forked name with no recoverable mapping →
# 標「待補 @tixl header」。一個有 header 的檔（即使 stem≠TiXL名，如 doylespiralpoints→@tixl:DoyleSpiralPoints2）
# 已自證對應，NOT no-header。多-op registrar 檔（change_detectors/context_vars…，內含 register*Op）也排除。
awk -F'\t' '{print tolower($1)}' "$TMP/tixl.txt" | sort -u > "$TMP/tixl_names.txt"
: > "$TMP/sw_unmatched.txt"
find "$SWRT" -name '*.cpp' | grep -E "/($LEAF_PREFIX)_" | while read -r f; do
  base="$(basename "$f" .cpp)"
  stem="$(echo "$base" | sed -E "s#^($LEAF_PREFIX)_##")"
  case "$stem" in
    *registry*|*_params|*golden*|*selftest*|*slots*|*_wrap|*_decls|*_cases|*_templates|*_ext) continue;;
    register_*|*_register_*) continue;;  # per-family registrar split, not an op
  esac
  # cook-fn split (its op is registered in a sibling file) → header lives there, not a forked op here.
  head -6 "$f" | grep -qiE 'Registered via|cook \(split|split out of|split from' && continue
  lstem="$(echo "$stem" | tr 'A-Z' 'a-z')"
  # (a) has a leading @tixl/authority header? → self-declares mapping, resolved, skip.
  head -25 "$f" | grep -qE '^[[:space:]]*//.*(@tixl:|TiXL authority:)' && continue
  # (c) multi-op file: holds register*Op("X") OR static `_reg_OpName{` registrars → ops resolve by their
  #     own names (central-table anchor / binary), the file stem is not an op name → skip.
  grep -qE 'register[A-Za-z]*\(\s*"|_reg_[A-Za-z0-9]+\s*\{' "$f" && continue
  # (b) stem equals a TiXL op name? → resolved by filename, skip.
  grep -qxF "$lstem" "$TMP/tixl_names.txt" && continue
  # (d) pure helper (defines no node at all — no NodeSpec/registrar/register) → not a forked op, skip.
  grep -qE 'NodeSpec|register[A-Za-z]*\(|_reg_' "$f" || continue
  echo "$stem"
done | sort -u > "$TMP/sw_unmatched.txt"

# ============================================================================
# 4. param-gap (defer to nodespec_integrity; fold-bug FIXED c0251ea → all islands trustworthy)
# ============================================================================
# fold-bug 已修（c0251ea：dumpNodeSpec 鏡像權威盲目位置消費 + VEC-SHORT 跨 subshell 傳遞）→ 全島可信。
# 本欄逐顆 spawn `nodespec_integrity <Op>`：gate 自己分類 cook 路徑（NodeSpec → sw-folded vs TiXL [Input]
# 真比對；texReg/cmdReg spec-less → N/A）+ 自動解析島（generator 在 point/generate；其餘 Lib-wide find）。
# 預設不跑（每呼叫 ~2.4s spawn binary）；--tsv --params 才逐顆求值。SW_TIXL_LIB 透傳（worktree 無 external/）。
param_gap_for() { # <Op> [island] -> string  (island arg kept for callsite symmetry; gate resolves it)
  local op="$1"
  if [ ! -x "$BIN" ]; then echo "no-bin"; return; fi
  local line; line="$(SW_TIXL_LIB="$TIXL_LIB" SW_BIN="$BIN" "$INTEGRITY" "$op" 2>/dev/null | tail -1)"
  # Gate verdict text (gate_one): "sw MISSING N param(s)" / "sw has N EXTRA param(s)" — count precedes
  # the keyword, so extract "<N> (MISSING|EXTRA)" and re-emit as "MISSING N param" / "EXTRA N param".
  case "$line" in
    *"=="*) echo "match" ;;
    *"VEC-RUN-SHORT"*) echo "vec-short(author bug)" ;;
    *"MISSING .cs"*) echo "no-cs" ;;
    *"sw MISSING"*) echo "MISSING $(echo "$line" | grep -oE '[0-9]+ param' | head -1 | grep -oE '[0-9]+') param" ;;
    *"EXTRA"*) echo "EXTRA $(echo "$line" | grep -oE '[0-9]+ EXTRA' | head -1 | grep -oE '[0-9]+') param" ;;
    *"N/A"*) echo "n/a" ;;
    *"UNKNOWN"*) echo "unregistered" ;;
    *) echo "?" ;;
  esac
}

# ============================================================================
# 5. discard-candidate dependency scan (flattened compounds only)
# ============================================================================
# For a flattened-compound type T: does any OTHER sw node / scenario / .scn reference "T" by name?
# Reference = the type string appears in a .scn or in a non-leaf-defining source spot.
# Build the CONSUMER CORPUS once (fast): the set of files where a reference to a node TYPE would mean
# "something downstream uses this node, retiring it would break that". = project/scene graphs (.swproj
# instantiate nodes by "name") + source files that are NOT a node's own definition/registration/test
# site. Definition-class files (leaf ops, node_registry_* tables, selftest/golden) are EXCLUDED: they
# disappear WITH the node, so a hit there is not a downstream dependency. Computed lazily, cached.
_DEP_CORPUS=""
build_dep_corpus() {
  [ -n "$_DEP_CORPUS" ] && return
  _DEP_CORPUS="$TMP/dep_corpus.txt"
  {
    # scene / project graphs (real instantiation references)
    find "$REPO_ROOT/app" "$REPO_ROOT/assets" \( -name '*.swproj' -o -name '*.scn' \) 2>/dev/null
    # source consumers: all .cpp/.h MINUS definition-class files
    find "$SWAPP" \( -name '*.cpp' -o -name '*.h' \) 2>/dev/null \
      | grep -ivE '/(point_ops|value_op|field_ops|string_ops|mesh_ops|pointlist_ops|stateful_value_ops|floatlist_ops|intlist_ops|host_scalar_ops|colorlist_ops|gradient_ops|stringlist_ops)_' \
      | grep -ivE '/node_registry_' \
      | grep -ivE 'golden|selftest|_slots\.|_params\.'
  } | sort -u > "$_DEP_CORPUS"
}

dep_scan() { # <Type> -> "safe" | "deps:<n>"  (n = consumer files referencing "Type" as a quoted string)
  build_dep_corpus
  local t="$1" n
  # whole quoted-string match "Type" inside the consumer corpus only.
  n="$(grep -lF -- "\"$t\"" $(cat "$_DEP_CORPUS") 2>/dev/null | sort -u | grep -c .)"
  if [ "${n:-0}" -eq 0 ]; then echo "safe"; else echo "deps:$n"; fi
}

# ============================================================================
# REPORT MODES
# ============================================================================
count_ident() { awk -F'\t' -v i="$1" '$3==i' "$TMP/ledger.txt" | wc -l | tr -d ' '; }

print_class_list() { # <ident> [withkind]
  awk -F'\t' -v i="$1" '$3==i{print $1"\t"$2"\t"$6}' "$TMP/ledger.txt" | sort
}

emit_summary() {
  local atom flat undone nonspec total noheader
  atom=$(count_ident atom); flat=$(count_ident flattened); undone=$(count_ident undone); nonspec=$(count_ident nonspec)
  total=$(wc -l < "$TMP/ledger.txt" | tr -d ' ')
  noheader=$(wc -l < "$TMP/sw_unmatched.txt" | tr -d ' ')
  # undone split by TiXL kind: undone-atom = 手刻 backlog；undone-compound = .t3 重放 backlog
  local ua uc
  ua=$(awk -F'\t' '$3=="undone"&&$5=="atom"' "$TMP/ledger.txt" | wc -l | tr -d ' ')
  uc=$(awk -F'\t' '$3=="undone"&&$5=="compound"' "$TMP/ledger.txt" | wc -l | tr -d ' ')
  echo "════════════════════════════════════════════════════════════════"
  echo " 節點健康帳 — TiXL $total 算子 (ground-truth universe, 親數 .t3，扣 _obsolete/_Old)"
  echo "════════════════════════════════════════════════════════════════"
  printf "  真原子       %5s   sw做了 ∧ TiXL原子        → 留 (真積木)\n" "$atom"
  printf "  壓平複合     %5s   sw做了 ∧ TiXL複合        → 廢棄候選 (等 .t3 重放)\n" "$flat"
  printf "  非NodeSpec   %5s   sw做了 ∧ texReg/cmdReg    → param 閘 N/A\n" "$nonspec"
  printf "  未做         %5s   TiXL有 ∧ sw多處全不命中\n" "$undone"
  printf "      ├ 未做·原子   %4s   → 原子手刻 backlog\n" "$ua"
  printf "      └ 未做·複合   %4s   → .t3 重放 backlog\n" "$uc"
  echo   "  ────────────────────────────────────────────"
  printf "  無@tixl-header %3s   sw葉檔 stem 對不上任何 TiXL 名 (fork 待補 header)\n" "$noheader"
  echo ""
}

case "${1:-}" in
  --summary)
    emit_summary ;;

  --tsv)
    # tixl_op  island  identity  done  param_gap  discard
    # param_gap 欄預設 "-"（接口預留，--params 才逐顆跑 nodespec_integrity；非 point/flow 標 pending）。
    want_params=0; [ "${2:-}" = "--params" ] && want_params=1
    while IFS=$'\t' read -r op island ident done kind rel; do
      pg="-"; disc="-"
      [ "$ident" = "flattened" ] && disc="$(dep_scan "$op")"
      if [ "$want_params" = "1" ] && [ "$done" = "y" ]; then pg="$(param_gap_for "$op" "$island")"; fi
      printf '%s\t%s\t%s\t%s\t%s\t%s\n' "$op" "$island" "$ident" "$done" "$pg" "$disc"
    done < "$TMP/ledger.txt" ;;

  --class)
    cls="${2:?usage: --class atom|flattened|undone|nonspec|noheader}"
    if [ "$cls" = "noheader" ]; then
      echo "== 無 @tixl-header 的 sw 葉檔 stem（對不上 TiXL 名，待補 header）=="
      sed 's/^/  /' "$TMP/sw_unmatched.txt"
    else
      echo "== identity=$cls 逐顆 =="
      print_class_list "$cls" | sed -E 's/^/  /'
    fi ;;

  --discard)
    echo "== 壓平複合 = 廢棄候選 + 依賴掃描（只標不動 code）=="
    echo "   safe = 無下游依賴可安全廢棄；deps:N = 有 N 處引用，挪會壞"
    echo ""
    awk -F'\t' '$3=="flattened"{print $1"\t"$2}' "$TMP/ledger.txt" | sort | while IFS=$'\t' read -r op island; do
      d="$(dep_scan "$op")"
      printf "  %-32s [%-8s] %s\n" "$op" "$island" "$d"
    done ;;

  --verify)
    echo "== 抽樣對已知案例驗準（今天血證的 6-8 次 stale）=="
    chk() { # <Op> <expect ident or done-state desc>
      local op="$1" want="$2"
      local row; row="$(awk -F'\t' -v o="$op" '$1==o{print; exit}' "$TMP/ledger.txt")"
      if [ -z "$row" ]; then printf "  %-18s NOT a TiXL op (no .t3)\n" "$op"; return; fi
      local ident; ident="$(echo "$row" | cut -f3)"; local done; done="$(echo "$row" | cut -f4)"
      local mark="✓"; [ "$ident" = "$want" ] || mark="✗"
      printf "  %s %-18s identity=%-10s done=%s   (expect %s)\n" "$mark" "$op" "$ident" "$done" "$want"
    }
    echo "  --- 假陽性殺手：Steps 是真 TiXL image op 但 sw 未 port，名字撞 AnimValue 的 enum-label \"Steps\" ---"
    echo "      （舊 loose-grep census 因此誤判 done；錨定後正確 = undone）---"
    chk Steps undone
    echo "  --- 對照：Random/PerlinNoise 名字也撞 enum-label，但 sw 真有 value_op leaf → 正確 atom（非假陽性）---"
    chk Random atom
    chk PerlinNoise atom
    echo "  --- 假陰性（中央表 node_registry_math_* 已做，舊 census 漏看）應 = atom ---"
    chk Cos atom
    chk Atan2 atom
    echo "  --- gradient_ops 已做（census 假陽性剔除）---"
    chk DefineGradient atom
    echo "  --- 真原子已做 ---"
    chk TransformVec3 atom
    chk Add atom
    echo "  --- 壓平複合範例（sw 手刻單體 / TiXL 是 .t3 複合）---"
    chk BlendPoints flattened
    chk PairPointsForLines flattened
    ;;

  --html)
    OUT="${2:-$REPO_ROOT/docs/agent/census/node_health.html}"
    # PARAM_TSV (env): a precomputed `--tsv --params` snapshot. When set, the HTML reads the param-gap
    # column per atom from it instead of re-spawning the binary 307×. param_gap_lookup echoes the cached
    # value (or "" when no cache / not present). Keeps --html fast; the slow sweep runs once into the cache.
    PARAM_TSV="${PARAM_TSV:-}"
    param_gap_lookup() { # <Op> -> cached param-gap string or ""
      [ -n "$PARAM_TSV" ] && [ -f "$PARAM_TSV" ] || { echo ""; return; }
      awk -F'\t' -v o="$1" '$1==o{print $5; exit}' "$PARAM_TSV"
    }
    {
      atom=$(count_ident atom); flat=$(count_ident flattened); undone=$(count_ident undone); nonspec=$(count_ident nonspec)
      total=$(wc -l < "$TMP/ledger.txt" | tr -d ' '); noheader=$(wc -l < "$TMP/sw_unmatched.txt" | tr -d ' ')
      # 真原子旋鈕不全 = atoms whose cached param-gap reports MISSING (baked param not exposed as a knob).
      pggap=0
      if [ -n "$PARAM_TSV" ] && [ -f "$PARAM_TSV" ]; then
        pggap=$(awk -F'\t' '$3=="atom" && $5 ~ /MISSING/' "$PARAM_TSV" | wc -l | tr -d ' ')
      fi
      cat <<HTMLHEAD
<!DOCTYPE html>
<html lang="zh-Hant"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>節點健康帳 — simple_world ⇄ TiXL</title>
<style>
  :root{--bg:#0f1115;--card:#181b22;--ink:#e6e8ec;--mut:#8a93a3;--line:#262b35;
    --atom:#4ade80;--flat:#fbbf24;--undone:#60a5fa;--nonspec:#a78bfa;--nohdr:#f87171;}
  *{box-sizing:border-box} body{margin:0;background:var(--bg);color:var(--ink);
    font:14px/1.5 -apple-system,BlinkMacSystemFont,"Segoe UI",sans-serif;padding:28px}
  h1{font-size:22px;margin:0 0 4px} .sub{color:var(--mut);margin:0 0 22px;font-size:13px}
  .kpis{display:flex;flex-wrap:wrap;gap:10px;margin-bottom:26px}
  .kpi{background:var(--card);border:1px solid var(--line);border-radius:10px;
    padding:12px 16px;min-width:130px} .kpi .n{font-size:26px;font-weight:700}
  .kpi .l{color:var(--mut);font-size:12px;margin-top:2px}
  .kpi.atom .n{color:var(--atom)} .kpi.flat .n{color:var(--flat)}
  .kpi.undone .n{color:var(--undone)} .kpi.nonspec .n{color:var(--nonspec)}
  .kpi.nohdr .n{color:var(--nohdr)}
  section{margin-bottom:30px}
  .sechead{display:flex;align-items:baseline;gap:10px;border-bottom:1px solid var(--line);
    padding-bottom:8px;margin-bottom:14px}
  .sechead h2{font-size:17px;margin:0} .sechead .cnt{color:var(--mut);font-size:13px}
  .sechead .desc{color:var(--mut);font-size:12px;margin-left:auto;text-align:right}
  .grid{display:flex;flex-wrap:wrap;gap:7px}
  .chip{background:var(--card);border:1px solid var(--line);border-left-width:3px;
    border-radius:7px;padding:6px 10px;font-size:12.5px;display:flex;flex-direction:column;min-width:0}
  .chip .nm{font-weight:600;white-space:nowrap} .chip .meta{color:var(--mut);font-size:11px;margin-top:1px}
  .s-atom .chip{border-left-color:var(--atom)} .s-flat .chip{border-left-color:var(--flat)}
  .s-undone .chip{border-left-color:var(--undone)} .s-nonspec .chip{border-left-color:var(--nonspec)}
  .s-nohdr .chip{border-left-color:var(--nohdr)}
  .chip .tag{display:inline-block;font-size:10px;padding:1px 5px;border-radius:4px;margin-top:3px}
  .tag.safe{background:#14361f;color:var(--atom)} .tag.deps{background:#3a2410;color:var(--flat)}
  .tag.pg{background:#1c2a3f;color:var(--undone)}
  .tag.pg-match{background:#14361f;color:var(--atom)} .tag.pg-miss{background:#3a1414;color:var(--nohdr)}
  .tag.pg-na{background:#23262e;color:var(--mut)} .tag.pg-short{background:#3a2410;color:var(--flat)}
  details>summary{cursor:pointer;color:var(--mut);font-size:12px;margin-bottom:8px}
</style></head><body>
<h1>節點健康帳 — simple_world ⇄ TiXL</h1>
<p class="sub">TiXL $total 算子 (親數 .t3 = ground-truth universe) · done-check 多處錨定來源 · 身份 = sw做了 × TiXL原子/複合 · 真原子 param-gap 由 nodespec_integrity 逐顆對 TiXL [Input]（fold-bug c0251ea 修後全島可信）· 由 tools/node_health.sh --html [PARAM_TSV=快照] 產（跟 code 走不 stale）</p>
<div class="kpis">
  <div class="kpi atom"><div class="n">$atom</div><div class="l">真原子 (留·真積木)</div></div>
  <div class="kpi flat"><div class="n">$flat</div><div class="l">壓平複合 (廢棄候選)</div></div>
  <div class="kpi undone"><div class="n">$undone</div><div class="l">未做</div></div>
  <div class="kpi nonspec"><div class="n">$nonspec</div><div class="l">非NodeSpec (param N/A)</div></div>
  <div class="kpi nohdr"><div class="n">$noheader</div><div class="l">無@tixl-header</div></div>
  <div class="kpi nohdr"><div class="n">$pggap</div><div class="l">真原子旋鈕不全 (baked MISSING)</div></div>
</div>
HTMLHEAD

      html_esc() { sed -e 's/&/\&amp;/g' -e 's/</\&lt;/g' -e 's/>/\&gt;/g'; }

      emit_section() { # <ident> <cssclass> <title> <desc>
        local ident="$1" css="$2" title="$3" desc="$4" cnt
        cnt=$(count_ident "$ident")
        echo "<section class=\"s-$css\"><div class=\"sechead\"><h2>$title</h2><span class=\"cnt\">$cnt 顆</span><span class=\"desc\">$desc</span></div><div class=\"grid\">"
        if [ "$ident" = "flattened" ]; then
          awk -F'\t' '$3=="flattened"{print $1"\t"$2}' "$TMP/ledger.txt" | sort | while IFS=$'\t' read -r op island; do
            local d; d="$(dep_scan "$op")"
            local tagcls="safe" tagtxt="可安全廢棄"
            case "$d" in deps:*) tagcls="deps"; tagtxt="有依賴 ${d#deps:} 處";; esac
            printf '<div class="chip"><span class="nm">%s</span><span class="meta">%s</span><span class="tag %s">%s</span></div>\n' \
              "$(echo "$op"|html_esc)" "$(echo "$island"|html_esc)" "$tagcls" "$tagtxt"
          done
        else
          awk -F'\t' -v i="$ident" '$3==i{print $1"\t"$2}' "$TMP/ledger.txt" | sort | while IFS=$'\t' read -r op island; do
            # param-gap tag (atoms only, from cached snapshot): match / MISSING N / N/A / vec-short.
            pgtag=""
            if [ "$ident" = "atom" ]; then
              local pg; pg="$(param_gap_lookup "$op")"
              case "$pg" in
                match)        pgtag='<span class="tag pg-match">旋鈕齊</span>' ;;
                MISSING*)     pgtag="<span class=\"tag pg-miss\">缺 $(echo "$pg"|grep -oE '[0-9]+') 旋鈕</span>" ;;
                EXTRA*)       pgtag="<span class=\"tag pg-short\">EXTRA $(echo "$pg"|grep -oE '[0-9]+')</span>" ;;
                vec-short*)   pgtag='<span class="tag pg-short">VEC-SHORT</span>' ;;
                n/a)          pgtag='<span class="tag pg-na">param N/A</span>' ;;
                no-cs|unregistered) pgtag="<span class=\"tag pg-na\">$pg</span>" ;;
                "") : ;;  # no cache → no tag
                *)            pgtag='<span class="tag pg-na">?</span>' ;;
              esac
            fi
            printf '<div class="chip"><span class="nm">%s</span><span class="meta">%s</span>%s</div>\n' \
              "$(echo "$op"|html_esc)" "$(echo "$island"|html_esc)" "$pgtag"
          done
        fi
        echo "</div></section>"
      }

      emit_section atom      atom    "真原子 — 留 (真積木)"          "sw做了 ∧ TiXL .t3 Children 空"
      emit_section flattened flat    "壓平複合 — 廢棄候選"            "sw 手刻單體 ∧ TiXL 是 .t3 複合 → 等 .t3 重放取代；標依賴別動 code"
      emit_section undone    undone  "未做"                          "TiXL 有 ∧ sw 多處 done-check 全不命中"
      emit_section nonspec   nonspec "非 NodeSpec 路徑"               "sw做了，走 texReg/cmdReg → param 閘 N/A"

      # no-header section
      echo "<section class=\"s-nohdr\"><div class=\"sechead\"><h2>無 @tixl-header</h2><span class=\"cnt\">$noheader 顆</span><span class=\"desc\">sw 葉檔 stem 對不上任何 TiXL 名 (fork 待補 // @tixl: header)</span></div><div class=\"grid\">"
      while IFS= read -r stem; do
        [ -z "$stem" ] && continue
        printf '<div class="chip"><span class="nm">%s</span><span class="meta">leaf stem</span></div>\n' "$(echo "$stem"|html_esc)"
      done < "$TMP/sw_unmatched.txt"
      echo "</div></section>"

      echo "</body></html>"
    } > "$OUT"
    echo "[node_health] HTML 寫到: $OUT"
    ;;

  ""|--ledger)
    emit_summary
    for spec in "atom:真原子 (留·真積木)" "flattened:壓平複合 (廢棄候選, 等 .t3 重放)" "undone:未做" "nonspec:非NodeSpec (param N/A)"; do
      ident="${spec%%:*}"; title="${spec#*:}"
      cnt=$(count_ident "$ident")
      echo "──── $title — $cnt 顆 ────"
      print_class_list "$ident" | awk -F'\t' '{printf "  %-34s %s\n",$1,$2}' | column -x 2>/dev/null || print_class_list "$ident" | awk -F'\t' '{printf "  %-34s %s\n",$1,$2}'
      echo ""
    done
    noheader=$(wc -l < "$TMP/sw_unmatched.txt" | tr -d ' ')
    echo "──── 無 @tixl-header — $noheader 顆 (sw 葉檔 stem 對不上 TiXL 名) ────"
    sed 's/^/  /' "$TMP/sw_unmatched.txt"
    echo ""
    echo "(逐身份清單: --class <id> / 廢棄依賴: --discard / 驗準: --verify / 視覺: --html / 機器: --tsv)"
    ;;

  *)
    echo "usage: $0 [--summary|--class <id>|--discard|--tsv|--html [file]|--verify]" >&2
    exit 2 ;;
esac

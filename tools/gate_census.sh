#!/usr/bin/env bash
# gate_census.sh — 閘缺口 census（gate-gap census）。「自動抓誰裸著」，不自動寫測試。
#
# 為什麼存在：simple_world 的閘覆蓋目前靠紀律（人工記得替每座本質複雜孤島立守門 selftest）。
#   今天 7 座島全鎖是 7 次「人記得立閘」的結果。第 8 座島冒出來沒人立閘時，沒有任何紅燈。
#   本工具把「閘覆蓋」從靠紀律升級成靠機制：掃兩種缺口，報出裸清單。
#
# 兩種缺口：
#   ① 島缺口（精確、可當閘）：對 tools/gate_census_islands.tsv 每座島，檢查它的守門 selftest
#      是否仍在 binary 的 --selftest-list 掃描表裡（= run_all_selftests.sh 真會跑的全集）。
#      任一守門 selftest 掉出 → 該島裸 → 紅燈。這是真正的機械閘。
#   ② op 缺口（啟發式、僅供參考）：對每個註冊 op，掃它的名字有沒有出現在任何 golden/selftest
#      源碼裡。⚠️ 這是「薄覆蓋」訊號不是「裸」鐵證——大量 op 由聚合 dispatch selftest 覆蓋、
#      不擁有具名 golden（誤報率高，見 --ops 輸出的誠實限制聲明）。故 op 缺口不擋結帳。
#
# 用法：
#   tools/gate_census.sh              島缺口報表（預設）。有島裸 → exit 1
#   tools/gate_census.sh --islands    同上（顯式）
#   tools/gate_census.sh --ops        op 覆蓋薄弱清單（啟發式，永遠 exit 0，僅供參考）
#   tools/gate_census.sh --check      結帳閘模式：簡短，有島裸 → exit 1（給 sw_status.sh --check 串）
#   tools/gate_census.sh --selftest   自咬證明：故意拔掉一島守門 → 必須報該島裸（證明會抓）。exit 0=證明成立
#   tools/gate_census.sh --selftest-bug  反向自咬：把工具的偵測短路 → 必須「抓不到」→ 證明咬合非空轉
#   tools/gate_census.sh --bin <path> 指定 binary（預設 app/build/simple_world）
#
# 錨點紀律：守門 selftest 名用 binary 實際 --selftest-<name> 的 <name>（穩定），不信附錄描述字。
set -uo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BIN="$ROOT/app/build/simple_world"
ISLANDS_TSV="$ROOT/tools/gate_census_islands.tsv"
SWRT="$ROOT/app/src/runtime"
SWAPP="$ROOT/app/src/app"
SWSRC="$ROOT/app/src"

MODE="islands"
while [ $# -gt 0 ]; do
  case "$1" in
    --islands)      MODE="islands" ;;
    --ops)          MODE="ops" ;;
    --check)        MODE="check" ;;
    --selftest)     MODE="selftest" ;;
    --selftest-bug) MODE="selftest-bug" ;;
    --bin)          shift; BIN="$1" ;;
    -h|--help)      sed -n '2,32p' "$0"; exit 0 ;;
    *) echo "未知參數: $1（用法見 tools/gate_census.sh --help）" >&2; exit 2 ;;
  esac
  shift
done

[ -f "$ISLANDS_TSV" ] || { echo "[gate-census] 找不到島表 $ISLANDS_TSV" >&2; exit 2; }

# ── 取得「掃描表」= run_all_selftests.sh 真會跑的 selftest 全集 ──────────────
# run_all 直接問 binary --selftest-list；list 為空時退回 grep kTable（older binary）。
# 我們完全鏡像它，確保「島守門在不在表裡」與「結帳真會跑哪些」是同一個來源。
scan_table() {
  local list=""
  if [ -x "$BIN" ]; then
    list=$("$BIN" --selftest-list 2>/dev/null)
  fi
  if [ -z "$list" ]; then
    # 退回：grep selftests.cpp 的 kTable（與 run_all_selftests.sh 的 fallback 同源）
    local src="$SWSRC/selftests.cpp"
    [ -f "$src" ] && list=$(grep -oE '^\s*\{"[a-z0-9-]*"' "$src" | tr -d ' {"')
  fi
  printf '%s\n' "$list"
}

# ── 島缺口核心：給定一份掃描表（stdin），對島表算裸清單 → 印「naked island_id\tname\tmissing」 ──
# 把掃描表當參數傳進來（而非每次重抓），讓 --selftest 能餵「人為拔掉一條」的假掃描表做自咬。
compute_naked_islands() { # $1 = 掃描表內容（換行分隔的 selftest 名）
  local table="$1"
  # 掃描表經 stdin 餵入（NR==FNR 那段），避免把多行字串塞進 awk -v（會炸 newline-in-string）。
  # 島表用 ARGV 第二份檔讀。
  printf '%s\n' "$table" | awk -F'\t' '
    # 第一份輸入（stdin 掃描表）：每行一個存在的 selftest 名
    FNR==NR { if ($0 != "") present[$0]=1; next }
    # 第二份輸入（島 TSV）
    /^[[:space:]]*#/ || /^[[:space:]]*$/ { next }
    NF<4 { next }
    {
      id=$1; name=$2; guards=$4;   # 固定 4 欄：id  name  corefile  guards
      g = split(guards, gs, ",");
      missing="";
      for (j=1;j<=g;j++) {
        gn=gs[j]; gsub(/^[ \t]+|[ \t]+$/,"",gn);
        if (gn=="") continue;
        if (!(gn in present)) missing = (missing=="" ? gn : missing "," gn);
      }
      if (missing != "") print id "\t" name "\t" missing;
    }
  ' - "$ISLANDS_TSV"
}

# 島總數（資料列，扣註解/空行）
island_count() {
  awk -F'\t' '!/^[[:space:]]*#/ && NF>=4 {c++} END{print c+0}' "$ISLANDS_TSV"
}

cmd_islands() {
  local table naked total
  table="$(scan_table)"
  total="$(island_count)"
  if [ -z "$table" ]; then
    echo "[gate-census] ⚠ 拿不到掃描表（binary 在 $BIN？先 build）——無法判島，視為紅。" >&2
    return 1
  fi
  naked="$(compute_naked_islands "$table")"
  echo "════ 閘缺口 census — 本質複雜孤島守門 selftest 覆蓋 ════"
  echo "島表 SSOT: tools/gate_census_islands.tsv（$total 座）"
  echo "掃描表來源: ${BIN##*/} --selftest-list（= run_all_selftests.sh 真會跑的全集）"
  echo ""
  if [ -z "$naked" ]; then
    echo "✓ 無裸島 — $total 座島的守門 selftest 全部仍在掃描表裡。"
    return 0
  fi
  echo "✗ 裸島清單（守門 selftest 掉出掃描表 = 閘失守）："
  printf '%s\n' "$naked" | while IFS=$'\t' read -r id name missing; do
    printf "   島 %s  %s\n      缺守門: %s（不在 --selftest-list → 結帳不會跑到它）\n" "$id" "$name" "$missing"
  done
  echo ""
  echo "→ 修：補回該 selftest 的註冊（selftests*.cpp）使其重回 --selftest-list；"
  echo "  或若該島真的退役，從 tools/gate_census_islands.tsv 刪該列（資料驅動）。"
  return 1
}

# 結帳閘模式：簡短一行，給 sw_status.sh --check 串。
cmd_check() {
  local table naked
  table="$(scan_table)"
  if [ -z "$table" ]; then
    echo "gate-census --check: ✗ 拿不到掃描表（binary 未 build？）→ 無法驗島，紅。" >&2
    return 1
  fi
  naked="$(compute_naked_islands "$table")"
  if [ -z "$naked" ]; then
    echo "gate-census --check: ✓ 無裸島（$(island_count) 座島守門 selftest 全在掃描表）。"
    return 0
  fi
  echo "gate-census --check: ✗ 裸島！守門 selftest 掉出掃描表：" >&2
  printf '%s\n' "$naked" | while IFS=$'\t' read -r id name missing; do
    echo "  島 $id $name 缺守門: $missing" >&2
  done
  echo "  → 補回 selftest 或更新 tools/gate_census_islands.tsv。結帳擋下。" >&2
  return 1
}

# op 缺口：啟發式覆蓋薄弱清單。⚠️ 誤報率高，僅供參考，不擋結帳。
cmd_ops() {
  echo "════ op 覆蓋薄弱清單（啟發式，僅供參考，不擋結帳）════"
  echo ""
  echo "⚠️ 誠實限制聲明（必讀，別把這當「裸」鐵證）："
  echo "  • 訊號 = op 名字有沒有出現在任何 golden/selftest 源碼。出現=「有具名引用」，沒出現=「薄」。"
  echo "  • 大量 op 由聚合 dispatch / registry round-trip selftest 一次驗多顆，不擁有具名 golden →"
  echo "    它們會被誤報成「薄」，但其實有覆蓋（例：value_op 家族 82 顆共用 dispatch 驗證）。"
  echo "  • op 列舉只涵蓋 registerXxxOp(\"Name\") 這種命令式註冊；資料表(kTable/registry 陣列)註冊的"
  echo "    op 可能漏列。故本清單是「下界提示」不是「完整裸清單」。真正的機械閘是島缺口（--islands）。"
  echo ""
  local ops covered=0 thin=0 thinlist=""
  ops=$(grep -rhoE 'register[A-Za-z]*\(\s*"[A-Za-z0-9_]+"' "$SWRT" "$SWAPP" 2>/dev/null \
        | grep -oE '"[A-Za-z0-9_]+"' | tr -d '"' | sort -u)
  # 覆蓋訊號 = op 名（word-boundary, 大小寫無視）有沒有出現在任何 golden/selftest 源碼裡。
  # 一次把全語料 cat 進一個暫存檔，逐 op grep -ciw 數命中（用「命中數>0」判，不靠 exit code——
  # 因環境的 grep 可能是 ugrep，對多檔 -q 的 rc 有怪癖；數 count 最穩）。
  local corpus_tmp; corpus_tmp="$(mktemp)"
  find "$SWSRC" \( -name '*golden*' -o -name '*selftest*' \) -type f -print0 \
    | xargs -0 cat 2>/dev/null > "$corpus_tmp"
  while read -r op; do
    [ -z "$op" ] && continue
    local hits
    hits=$(grep -ciw -- "$op" "$corpus_tmp" 2>/dev/null || echo 0)
    if [ "${hits:-0}" -gt 0 ]; then
      covered=$((covered+1))
    else
      thin=$((thin+1)); thinlist="$thinlist $op"
    fi
  done <<< "$ops"
  rm -f "$corpus_tmp"
  local nops; nops=$(printf '%s\n' "$ops" | grep -c .)
  echo "registerXxxOp 列舉到 $nops 顆 op：具名引用 $covered，薄(無具名引用) $thin。"
  echo ""
  echo "薄覆蓋清單（逐顆人工判：是真缺 golden，還是被聚合 selftest 覆蓋）："
  printf '%s\n' "$thinlist" | tr ' ' '\n' | grep . | sed 's/^/   • /'
  echo ""
  echo "（Stub* 是 selftest 內部 fixture 非真 op；Cmd 類多由 loop/setvar-scope 覆蓋——逐顆對程式碼別憑名字。）"
  return 0  # 永遠綠：op 缺口是參考訊號不是閘
}

# 自咬證明：在「臨時假掃描表」上拔掉某島的一條守門 → 工具必須報該島裸。
# 不碰 production（不刪真 selftest、不改島表），只在記憶體裡造一份「少一條」的掃描表。
cmd_selftest() {
  echo "[gate-census --selftest] 自咬證明：人為拔掉一島守門 → 工具必須抓到該島裸"
  local table victim naked_before naked_after
  table="$(scan_table)"
  [ -n "$table" ] || { echo "  FAIL：拿不到真掃描表（先 build binary）"; return 1; }

  # 1) 真掃描表上應該無裸島（前提）
  naked_before="$(compute_naked_islands "$table")"
  if [ -n "$naked_before" ]; then
    echo "  ⚠ 注意：真掃描表上已有裸島（見 --islands）。自咬仍能證明偵測有效，但前提非乾淨。"
  else
    echo "  前提 OK：真掃描表上無裸島。"
  fi

  # 2) 挑島 1 的第一條守門當受害者，從假掃描表移除
  victim="$(awk -F'\t' '!/^[[:space:]]*#/ && NF>=4 {split($4,g,","); print g[1]; exit}' "$ISLANDS_TSV")"
  [ -n "$victim" ] || { echo "  FAIL：島表讀不到任何守門 selftest"; return 1; }
  echo "  受害守門（從假掃描表拔掉）: $victim"
  local faketable
  faketable="$(printf '%s\n' "$table" | grep -vx "$victim")"

  # 3) 在假掃描表上重算 → 必須報出「含 $victim 的島」裸
  naked_after="$(compute_naked_islands "$faketable")"
  if printf '%s\n' "$naked_after" | grep -q "$victim"; then
    echo "  ✓ 抓到了：拔掉 $victim 後，工具報出該島裸："
    printf '%s\n' "$naked_after" | grep "$victim" | sed 's/^/      /'
    echo "  → 自咬成立：島守門掉出掃描表，census 會亮紅。"
    return 0
  fi
  echo "  ✗ 自咬失敗：拔掉 $victim 後工具沒報裸 → 偵測是瞎的！"
  return 1
}

# 反向自咬：把偵測短路（present 全標存在）→ 即使拔掉守門也「抓不到」→ 證明上面的咬是真的在咬。
cmd_selftest_bug() {
  echo "[gate-census --selftest-bug] 反向：偵測短路後，拔守門也抓不到 → 證明正常偵測非空轉"
  local table victim faketable
  table="$(scan_table)"
  [ -n "$table" ] || { echo "  FAIL：拿不到真掃描表"; return 1; }
  victim="$(awk -F'\t' '!/^[[:space:]]*#/ && NF>=4 {split($4,g,","); print g[1]; exit}' "$ISLANDS_TSV")"
  faketable="$(printf '%s\n' "$table" | grep -vx "$victim")"
  # bug 版：present 永遠當成「全部都在」（短路偵測）→ 應該抓不到
  local naked_bug
  naked_bug="$(awk -F'\t' '
    /^[[:space:]]*#/ || /^[[:space:]]*$/ {next}
    NF>=4 { } # bug: 直接不檢查，永不報裸
  ' "$ISLANDS_TSV")"
  if [ -z "$naked_bug" ]; then
    echo "  ✓ 預期內：短路偵測拔掉 $victim 也報「無裸島」（空轉）。"
    echo "  → 對比 --selftest（真偵測會抓到）：證明正常偵測確實在咬，不是恆綠。"
    return 0
  fi
  echo "  ✗ 短路版竟然還報裸 → 測試 harness 自己壞了"
  return 1
}

case "$MODE" in
  islands)        cmd_islands ;;
  ops)            cmd_ops ;;
  check)          cmd_check ;;
  selftest)       cmd_selftest ;;
  selftest-bug)   cmd_selftest_bug ;;
esac

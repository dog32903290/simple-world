#!/usr/bin/env bash
# sw_status.sh — /sw-batch 定位 + 結帳的「單一狀態出口」
# ───────────────────────────────────────────────────────────────────────────
# 為什麼存在：進度真相散在三套會漂的手寫源（MASTER_PLAN snapshot / memory head /
#   Cut）→ 每次 /sw-batch 開頭要逐檔對、數字 stale、互相打架。此工具把真相收斂成
#   一支命令、三個可信度分層，殺掉「看進度就幻覺」。
#
# 三區（出力分層 = 反幻覺機制，永不混淆）：
#   ① LIVE        現在量到的（git + op_census）——HEAD/乾淨度/克隆進度/縫。可信。
#   ② STAMPED@結帳 上次收尾蓋章的（--bite PASS 數）——標年齡，是「記憶」不是「量測」。
#   ③ HAND 手寫接力 MASTER_PLAN 頂的三句（Active Lane / Next / Conflict）——機器驗不了，
#                  唯一防線是它們在單一 snapshot 裡夠刺眼。
#
# 用法：
#   tools/sw_status.sh                開頭定位：印三區（唯讀，零副作用，不彈窗）
#   tools/sw_status.sh --stamp P [F] [N]
#                                     結帳：把 HEAD+census(現測) + 你剛跑的 --bite
#                                     PASS=P (FAILED=F / NO-BITE=N) 寫進 MASTER_PLAN 機器塊。
#                                     只改 <!-- sw_status:begin/end --> 圍欄內，prose 不碰。
#   tools/sw_status.sh --check        結帳閘：最後一個 commit 晚於上次 stamp → exit 1
#                                     （逼「收尾前一定蓋章」，不碰 mid-batch commit）。
# ───────────────────────────────────────────────────────────────────────────
set -uo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PLAN="$ROOT/docs/agent/MASTER_PLAN.md"
CENSUS="$ROOT/tools/op_census.sh"
UICENSUS="$ROOT/tools/ui_census.sh"

BEGIN_RE='<!-- sw_status:begin'
END_RE='<!-- sw_status:end'

# 從機器塊讀一個 fielded 欄位（例：HEAD / BITE / STAMP_AT）
block_field() {
  [ -f "$PLAN" ] || return 0
  awk -v f="$1:" '
    /sw_status:begin/ {b=1; next}
    /sw_status:end/   {b=0}
    b && $1==f { $1=""; sub(/^[ \t]+/,""); print; exit }
  ' "$PLAN"
}

# 從手寫段讀一個 markdown header 下的內容行（停在下個 ## 或空行）
hand_section() {
  [ -f "$PLAN" ] || return 0
  awk -v h="## $1" '
    $0==h {grab=1; next}
    grab && /^## / {exit}
    grab && /<!-- sw_status/ {next}
    grab && NF {print; got=1}
    grab && !NF && got {exit}
  ' "$PLAN"
}

cmd_status() {
  local head dirty dirtyn census_done seams
  head="$(cd "$ROOT" && git rev-parse --short HEAD 2>/dev/null || echo '?')"
  dirtyn="$(cd "$ROOT" && git status --porcelain 2>/dev/null | wc -l | tr -d ' ')"
  if [ "$dirtyn" -eq 0 ]; then dirty="clean"; else dirty="$dirtyn files dirty"; fi

  echo "── ① LIVE（git + census，現在量到的）──────────────────"
  printf "HEAD      %s  (%s)\n" "$head" "$dirty"
  if [ "$dirtyn" -ne 0 ]; then
    (cd "$ROOT" && git status --porcelain 2>/dev/null | sed 's/^/          /' | head -12)
  fi
  if [ -x "$CENSUS" ]; then
    census_done="$(bash "$CENSUS" --overview 2>/dev/null | grep -E '克隆 TiXL 進度' | sed -E 's/^[^0-9]*//')"
    printf "CENSUS    %s  [floor — fork 命名可能略低估；逐顆複查 op_census <island>]\n" "${census_done:-?}"
    echo   "SEAMS     （tools/op_census.sh --seams 看全圖；seam-build 解鎖最多先做）"
    bash "$CENSUS" --seams 2>/dev/null | grep -A3 'seam-build' | grep -E '^[[:space:]]+[0-9]' | head -3 | sed 's/^/          /'
  else
    echo "CENSUS    (tools/op_census.sh 不可執行)"
  fi
  if [ -x "$UICENSUS" ]; then
    bash "$UICENSUS" --overview 2>/dev/null | sed 's/^/          /'
    echo "          （非節點 UI/skin parity；tools/ui_census.sh 看全表，--gaps 選工作，規格在 docs/agent/alignment/）"
  fi

  echo ""
  echo "── ② STAMPED @ 上次結帳（非現測，是記憶）────────────────"
  local bite stamp
  bite="$(block_field BITE)"
  stamp="$(block_field STAMP_AT)"
  if [ -n "$bite" ]; then
    printf "BITE      %s   (蓋章於 %s；未重跑——要刷新跑 tools/run_all_selftests.sh --bite)\n" \
      "$bite" "${stamp:-?}"
  else
    echo "BITE      (尚無蓋章；結帳時 tools/sw_status.sh --stamp <PASS> 寫入)"
  fi

  echo ""
  echo "── ③ HAND 手寫接力（MASTER_PLAN 頂，機器驗不了，自己讀清楚）──"
  local al nx cf
  al="$(hand_section 'Active Lane')"
  nx="$(hand_section 'Next Handoff Sentence')"
  cf="$(hand_section 'Conflict Register')"
  printf "ACTIVE    %s\n" "${al:-（未填——危險，不知接哪條 lane）}"
  printf "NEXT      %s\n" "${nx:-（未填——危險，不知下一步）}"
  printf "CONFLICT  %s\n" "${cf:-none}"
}

cmd_stamp() {
  local pass="${1:-?}" failed="${2:-}" nobite="${3:-}"
  [ -f "$PLAN" ] || { echo "sw_status: 找不到 $PLAN" >&2; return 1; }
  grep -q "$BEGIN_RE" "$PLAN" || { echo "sw_status: MASTER_PLAN 缺機器塊圍欄（<!-- sw_status:begin -->），先補上" >&2; return 1; }

  local head census_done dirtyn dirty stamp_at
  head="$(cd "$ROOT" && git rev-parse --short HEAD 2>/dev/null || echo '?')"
  dirtyn="$(cd "$ROOT" && git status --porcelain 2>/dev/null | wc -l | tr -d ' ')"
  if [ "$dirtyn" -eq 0 ]; then dirty="clean"; else dirty="$dirtyn files"; fi
  census_done="$(bash "$CENSUS" --overview 2>/dev/null | grep -E '克隆 TiXL 進度' | sed -E 's/^[^0-9]*([0-9]+ \/ [0-9]+).*/\1/')"
  stamp_at="$(date '+%Y-%m-%dT%H:%M')"

  local bite_line="BITE: ${pass} PASS"
  [ -n "$failed" ] && bite_line="$bite_line | FAILED=[$failed]"
  [ -n "$nobite" ] && bite_line="$bite_line | NO-BITE=[$nobite]"

  local tmp; tmp="$(mktemp)"
  awk -v head="$head" -v dirty="$dirty" -v census="$census_done" \
      -v bite="$bite_line" -v at="$stamp_at" '
    /sw_status:begin/ { print; ins=1;
      print "HEAD: " head;
      print "DIRTY: " dirty;
      print "CENSUS: " census " done";
      print bite;
      print "STAMP_AT: " at;
      skip=1; next }
    /sw_status:end/ { skip=0; print; ins=0; next }
    skip { next }
    { print }
  ' "$PLAN" > "$tmp" && mv "$tmp" "$PLAN"
  echo "sw_status: 蓋章 HEAD=$head CENSUS=$census_done $bite_line @ $stamp_at"
}

cmd_check() {
  [ -f "$PLAN" ] || { echo "sw_status --check: 找不到 $PLAN" >&2; return 1; }

  # ── 閘缺口 census：本質複雜孤島的守門 selftest 掉出掃描表 = 裸島 = 結帳紅 ──────
  # 加在最前面：島裸是「機器站不住」的硬紅，比 stamp 新鮮度更根本。島乾淨才往下走原有檢查。
  # 注意：只有 census 工具存在＋判得出來時才當閘；工具缺席不無中生有擋既有結帳（absent-safe）。
  local GATECENSUS="$ROOT/tools/gate_census.sh"
  if [ -x "$GATECENSUS" ]; then
    if ! bash "$GATECENSUS" --check >/dev/null 2>&1; then
      echo "sw_status --check: ✗ 閘缺口 census 報裸島（守門 selftest 掉出掃描表）：" >&2
      bash "$GATECENSUS" --check 2>&1 | sed 's/^/  /' >&2
      echo "  → 補回守門 selftest 或更新 tools/gate_census_islands.tsv，再結帳。" >&2
      return 1
    fi
  fi

  local stamp_at stamp_epoch last_epoch
  stamp_at="$(block_field STAMP_AT)"
  [ -n "$stamp_at" ] || { echo "sw_status --check: 機器塊無 STAMP_AT（從未結帳蓋章）→ 紅" >&2; return 1; }
  # macOS BSD date 解析 ISO8601-min
  stamp_epoch="$(date -j -f '%Y-%m-%dT%H:%M' "$stamp_at" '+%s' 2>/dev/null || echo 0)"
  last_epoch="$(cd "$ROOT" && git log -1 --format=%ct 2>/dev/null || echo 0)"
  # 寬限 GRACE 秒：蓋章→隨即 commit(蓋章本身) 是正常收尾，commit 必比 STAMP_AT 晚幾秒~幾分；
  # 真正的 stale = 「commit 落地後 batch 沒蓋章就結束」→ 最後 commit 比蓋章晚「很多」。
  local GRACE=600
  if [ $((last_epoch - stamp_epoch)) -gt "$GRACE" ]; then
    echo "sw_status --check: ✗ 最後 commit($(date -r "$last_epoch" '+%H:%M')) 比上次蓋章($stamp_at)晚 >$((GRACE/60))min" >&2
    echo "  → 結帳未收尾：跑 tools/sw_status.sh --stamp <bite PASS> 蓋章後再結束本批。" >&2
    return 1
  fi
  echo "sw_status --check: ✓ 蓋章($stamp_at) 與最後 commit 同窗（≤$((GRACE/60))min）。handoff 新鮮。"
}

# --nag：給 Stop hook 用。只在「樹乾淨 + 有 commit 比蓋章新 >GRACE」時吐提醒 JSON。
# 樹乾淨=收尾點(該結帳了);樹髒=還在做事→不吵。永遠 exit 0(非阻塞)。
cmd_nag() {
  [ -f "$PLAN" ] || exit 0
  local dirtyn; dirtyn="$(cd "$ROOT" && git status --porcelain 2>/dev/null | wc -l | tr -d ' ')"
  [ "${dirtyn:-0}" -eq 0 ] || exit 0          # 樹髒=還在做=不吵
  local stamp_at stamp_epoch last_epoch GRACE=600
  stamp_at="$(block_field STAMP_AT)"
  [ -n "$stamp_at" ] || exit 0
  stamp_epoch="$(date -j -f '%Y-%m-%dT%H:%M' "$stamp_at" '+%s' 2>/dev/null || echo 0)"
  last_epoch="$(cd "$ROOT" && git log -1 --format=%ct 2>/dev/null || echo 0)"
  if [ $((last_epoch - stamp_epoch)) -gt "$GRACE" ]; then
    local msg="⚠️ 結帳未蓋章：最後 commit 比 handoff 蓋章($stamp_at)新，下個 session 會接到 stale。收尾請跑 tools/sw_status.sh --stamp <bite PASS>（+手寫更新 MASTER_PLAN 的 Active Lane/Next/Conflict）。"
    jq -n --arg m "$msg" '{systemMessage:$m, suppressOutput:true}' 2>/dev/null \
      || printf '{"systemMessage":"%s","suppressOutput":true}\n' "$msg"
  fi
  exit 0
}

case "${1:-}" in
  --stamp) shift; cmd_stamp "$@";;
  --check) cmd_check;;
  --nag)   cmd_nag;;
  ''|status) cmd_status;;
  -h|--help) sed -n '2,30p' "$0";;
  *) echo "未知參數: $1（用法見 tools/sw_status.sh --help）" >&2; exit 2;;
esac

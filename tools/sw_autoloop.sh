#!/usr/bin/env bash
# sw_autoloop.sh — sw-batch 自我恢復看門狗（柏為選「全自動·免手動」, 2026-06-19）
#
# 目的：連線中途斷掉時, 迴圈不要停死等人手動點 Try again。
# 作法：「一批一啟動」——launchd 每 N 分鐘戳一次本腳本(tick)；確認沒人在跑才
#       啟動一個 headless `claude -p` 跑「一個 sw-batch 批次」就退, 下一批等下次戳。
#       每批原子 + 靠磁碟結帳(Cut/memory)接續 => 斷線最多丟一批尾巴, 已 commit 不丟。
#
# ★防雙開(柏為雙開血債 [[sw-batch-no-parallel-launch]]): 任一中即 skip
#   A. lock 檔: 上一批 headless 還活著 → skip
#   B. transcript 近期活動(<WARM_MIN 分鐘): 任何 session 在跑都寫 transcript, 冷了才是真停 → skip
#   + 最後防線: headless 跑 sw-batch 步驟1 的 git-status 雙開自檢(prompt 已內建)
#   (註: 不用進程偵測 — Claude Desktop app 常駐開著 ≠ session 活躍, 會永遠誤擋)
#
# 柏為只需要記三個: sw_autoloop.sh on | off | status
set -uo pipefail

PROJECT="/Users/chenbaiwei/Desktop/vibe coding/simple_world"
TX_DIR="$HOME/.claude/projects/-Users-chenbaiwei-Desktop-vibe-coding-simple-world"
LOCK="$HOME/.claude/sw_autoloop.lock"
LOG="$HOME/.claude/sw_autoloop.log"
RUNLOG="$HOME/.claude/sw_autoloop.run.log"
PLIST="$HOME/Library/LaunchAgents/com.baiwei.sw-autoloop.plist"
LABEL="com.baiwei.sw-autoloop"
CLAUDE="/Users/chenbaiwei/.npm-global/bin/claude"
WARM_MIN=40          # transcript 在 N 分鐘內動過 = 有人在跑 → skip
                     # (40 而非 15:前景 driver 與 autoloop 共存時,前景長 agent 跑著主 transcript
                     #  會靜默到 ~34min[Cut73 implementer],須 >它才不被 autoloop 誤判插入=雙開血債)
SELF="$(basename "$0")"

log(){ echo "[$(date '+%Y-%m-%d %H:%M:%S')] $*" >> "$LOG"; }

# ───────────────────── tick: 看門狗本體(launchd 呼叫這個) ─────────────────────
# 用法: tick [--dry]   --dry 只跑 gate 印出判定, 不真啟動 headless
do_tick(){
  local dry=0; [ "${1:-}" = "--dry" ] && dry=1

  # ── Gate A: lock(上一批還在跑?) ──
  if [ -f "$LOCK" ]; then
    local lpid; lpid="$(sed -n '1p' "$LOCK" 2>/dev/null || true)"
    if [ -n "$lpid" ] && kill -0 "$lpid" 2>/dev/null; then
      log "SKIP[A]: autoloop 批次 pid=$lpid 仍在跑"; echo "SKIP[A] lock pid=$lpid alive"; return 0
    fi
    log "清除 stale lock (pid=${lpid:-?} 已死)"; rm -f "$LOCK"
  fi

  # ── Gate B: transcript 近期活動 ──
  local newest; newest="$(ls -t "$TX_DIR"/*.jsonl 2>/dev/null | head -1 || true)"
  if [ -n "$newest" ]; then
    local age_min=$(( ( $(date +%s) - $(stat -f %m "$newest") ) / 60 ))
    if [ "$age_min" -lt "$WARM_MIN" ]; then
      log "SKIP[B]: 最新 transcript ${age_min}min 前動過 (<${WARM_MIN}min)"; echo "SKIP[B] transcript ${age_min}min ago"; return 0
    fi
  fi

  # ── (原 Gate C「進程偵測」已移除: Claude Desktop app 常駐開著 ≠ session 活躍,
  #     用進程存在判定會永遠誤擋 autoloop。活躍偵測一律靠 Gate B(transcript mtime),
  #     冷了才是真停。最後防線 = headless 跑 sw-batch 步驟1 時的 git-status 雙開自檢。) ──

  if [ "$dry" = "1" ]; then
    log "DRY: 三 gate 全通過 → 真跑時會啟動一批"; echo "WOULD-START (all gates clear)"; return 0
  fi

  # ── 通過 gate: 啟動一批 ──
  { echo "$$"; date '+%s'; } > "$LOCK"
  log "START: 啟動 headless sw-batch 一批 (watchdog pid=$$)"
  cd "$PROJECT" || { log "ERR: cd PROJECT 失敗"; rm -f "$LOCK"; return 1; }

  local PROMPT='/sw-batch
（這是外部自動恢復觸發。本次只跑「一個批次」：照 /sw-batch 步驟 1→7 完整跑一輪，結帳寫 Cut+memory 後就結束本次 turn。**絕不 ScheduleWakeup、絕不自走第二批**——下一批由外部 launchd 觸發。啟動定位(步驟1)若偵測到另一個 live session、或 git status 有符合 Resume 下一步的未 commit 改動=立刻停、不碰檔、直接退出。）'

  "$CLAUDE" -p "$PROMPT" --model opus --output-format stream-json --verbose < /dev/null >> "$RUNLOG" 2>&1
  local rc=$?
  [ "$rc" -ne 0 ] && log "WARN: headless 批次退出碼 $rc"
  rm -f "$LOCK"
  log "DONE: 批次結束 (rc=$rc), lock 已清"
}

# ───────────────────── on / off / status ─────────────────────
write_plist(){
  mkdir -p "$(dirname "$PLIST")"
  cat > "$PLIST" <<PL
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>Label</key><string>$LABEL</string>
  <key>ProgramArguments</key>
  <array>
    <string>/bin/bash</string>
    <string>$PROJECT/tools/sw_autoloop.sh</string>
    <string>tick</string>
  </array>
  <key>StartInterval</key><integer>600</integer>
  <key>RunAtLoad</key><true/>
  <key>StandardOutPath</key><string>$HOME/.claude/sw_autoloop.launchd.log</string>
  <key>StandardErrorPath</key><string>$HOME/.claude/sw_autoloop.launchd.err</string>
</dict>
</plist>
PL
}

do_on(){
  write_plist
  launchctl unload "$PLIST" 2>/dev/null || true
  launchctl load "$PLIST" && log "ON: launchd 已載入 (每 600s 戳一次)" \
    && echo "✅ 自動恢復已開啟 (每 10 分鐘檢查一次, 沒人在跑才接一批)"
}

do_off(){
  launchctl unload "$PLIST" 2>/dev/null && log "OFF: launchd 已卸載" \
    && echo "⏹ 自動恢復已關閉" || echo "(本來就沒在跑)"
}

do_status(){
  echo "── sw_autoloop 狀態 ──"
  if launchctl list 2>/dev/null | grep -q "$LABEL"; then echo "launchd: ✅ 載入中 (每 600s tick)"; else echo "launchd: ⏹ 未載入"; fi
  if [ -f "$LOCK" ]; then
    local lpid; lpid="$(sed -n '1p' "$LOCK" 2>/dev/null)"
    if kill -0 "$lpid" 2>/dev/null; then echo "lock:    🔒 一批跑中 (pid=$lpid)"; else echo "lock:    ⚠ stale (pid=$lpid 已死, 下次 tick 自清)"; fi
  else echo "lock:    (無, 閒置)"; fi
  echo "── 最近 8 行 log ──"; tail -8 "$LOG" 2>/dev/null || echo "(無 log)"
}

case "${1:-}" in
  tick)   do_tick "${2:-}";;
  on)     do_on;;
  off)    do_off;;
  status) do_status;;
  *) echo "用法: $SELF {on|off|status|tick [--dry]}"; exit 2;;
esac

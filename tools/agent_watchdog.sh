#!/bin/bash
# agent_watchdog.sh — 背景看門狗：盯 subagent 的 transcript 檔 mtime，停寫超過閾值=疑似死亡。
#
# 為什麼存在（2026-06-13 早上的血）：批次15 兩個背景 agent 在 05:28-05:34 無聲死亡（session
# 閒置時被收走），沒 dossier、沒通知；orchestrator 等通知等了三小時，柏為人肉發現。
# 檢測訊號 = agent 的 output transcript（JSONL）工作中持續長大；mtime 停走 >閾值 = 死/卡。
#
# 用法（orchestrator 派工後立刻背景啟動，run_in_background）：
#   tools/agent_watchdog.sh <stale_minutes> <output_file...>
#   例: tools/agent_watchdog.sh 30 /private/tmp/claude-501/.../tasks/aXXX.output ...
#
# 行為：每 5 分鐘掃一輪。某檔 mtime 停滯 ≥ stale_minutes → 印報告並 exit 1（背景命令退出
# = orchestrator 被叫醒 → 按 WORKFLOW §六 接力程序處理）。全部檔案消失（任務收割完）→ exit 0。
# 注意：這支自己也是背景命令——它退出才有訊號，所以「全活著」時它一直默默跑。
set -u
STALE_MIN="${1:?usage: agent_watchdog.sh <stale_minutes> <output_file...>}"
shift
[ $# -ge 1 ] || { echo "[watchdog] no output files given" >&2; exit 2; }

while :; do
  now=$(date +%s)
  alive=0
  for f in "$@"; do
    [ -f "$f" ] || continue          # 收割完被清掉=正常
    alive=1
    mt=$(stat -f %m "$f" 2>/dev/null) || continue
    age_min=$(( (now - mt) / 60 ))
    if [ "$age_min" -ge "$STALE_MIN" ]; then
      echo "[watchdog] STALE: $f"
      echo "[watchdog]   last write ${age_min}min ago (threshold ${STALE_MIN}min) — agent 疑似死亡/卡死"
      echo "[watchdog]   → WORKFLOW §六: 查 worktree git status 盤半成品, 派接力 agent 進同一 worktree"
      exit 1
    fi
  done
  [ "$alive" -eq 0 ] && { echo "[watchdog] all agents harvested — exiting clean"; exit 0; }
  sleep 300
done

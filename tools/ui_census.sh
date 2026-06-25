#!/usr/bin/env bash
# ui_census.sh — B 軌(非節點 UI/skin)parity 現算防漏網。op_census 的近親。
# ───────────────────────────────────────────────────────────────────────────
# 為什麼存在:docs/agent/alignment/ 是 6/22-23 workflow 跑出的高質量 UI gap 快照
#   (100% 覆蓋驗證 + 規格庫),但快照會 stale(已落後 HEAD 200+ commit:搜尋/modes/
#   bpm/asset 等大批 6/22 列「缺」的條目其實三天內做掉了)。本工具把每條 gap 自帶的
#   verify 證據抽成「現跑的 grep 斷言」→ 狀態不存檔、每次現算 → 永不 stale。
#   施工規格仍留在 alignment/*.md(那層描述 TiXL,不變)。
#
#   三態:
#     GAP   = greppable 且 0 命中 = 那條缺口還活著(確定還沒做)
#     DONE  = greppable 且有命中 = 很可能做了(對 6/22 快照=stale,建議掃一眼確認)
#     CHECK = 範式級/語義級,純 grep 判不準 → 派 agent 讀碼複驗,命中數僅供線索
#
# 用法:
#   tools/ui_census.sh              逐條 + 總計(掛進 sw_status ① LIVE 用這個)
#   tools/ui_census.sh --overview   只印一行總計(給 sw_status 嵌入)
#   tools/ui_census.sh --tsv        機器可讀 id<TAB>sev<TAB>status
#   tools/ui_census.sh --gaps       只列還活著的 GAP(派 UI lane 選工作用)
#
# 規格 SSOT = docs/agent/alignment/<area>.md(每條的對齊規格+TiXL 源碼依據+行號)。
# 本表 52 條涵蓋 6 分區;node-coverage(節點)歸 op_census,不在此。
# ───────────────────────────────────────────────────────────────────────────
set -uo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"
MODE="${1:-}"

done_n=0 gap_n=0 check_n=0
declare -a ROWS

# check <id> <sev> <area> <greppable yes|no> <paths> <pattern> <desc>
#   paths 無引號展開(可含 glob/多檔/目錄);廣搜過濾 golden/selftest/verify 假命中。
check() {
  local id="$1" sev="$2" area="$3" grpble="$4" paths="$5" pat="$6" desc="$7" hits status
  hits=$(grep -rIE "$pat" $paths --include='*.cpp' --include='*.h' --include='*.mm' 2>/dev/null \
         | grep -vE 'golden|selftest|_test|/verify/' | grep -c . || true)
  if [ "$grpble" = no ]; then status=CHECK; check_n=$((check_n+1))
  elif [ "${hits:-0}" -gt 0 ]; then status=DONE; done_n=$((done_n+1))
  else status=GAP; gap_n=$((gap_n+1)); fi
  ROWS+=("$status|$sev|$area|$id|$desc|$hits")
}

# ══ ui-surface ════════════════════════════════════════════════════════════
check maggraph-layout       core      ui-surface no  "app/src/ui/node_draw.cpp" \
  'SetNodeSize|MagGraphItem|Width *= *140' "畫布磁吸網格(140寬/35列高/共邊吸附) vs 自由擺放 [6/25複驗:真缺,仍純 ed 自由擺放,core 範式級]"
check required-indicator    polish    ui-surface yes "app/src/runtime/graph.h" \
  'bool required' "必填輸入(Required)旗標+紅色閃爍指示器"
check reset-to-default      important ui-surface yes "app/src/ui/inspector_param_menu.cpp" \
  'Reset to default|ResetOverrideCommand' "參數列:重置為預設值 affordance [6/25複驗:✓已做 inspector_param_menu.cpp:26 Reset to default 選單項(gated anyOverride,跑 ResetOverrideCommand 可undo),inspector.cpp:175/289 接線]"
check undo-show-name        polish    ui-surface yes "app/src/ui/combine_dialog.cpp" \
  'Undo \(|peekUndoTitle|peekRedoTitle' "右鍵/選單 Undo 顯示下一步動作名稱"
check boundary-pentagon     polish    ui-surface yes "app/src/ui/node_draw.cpp" \
  'AddConvexPolyFilled' "符號邊界 Input/Output 節點箭頭凸角五邊形"
check menu-bar              polish    ui-surface yes "app/src/ui" \
  'BeginMainMenuBar' "頂部選單列(vs 單浮動 Toolbar)"
check node-thumb-preview    core      ui-surface yes "app/src/ui/node_faces.cpp" \
  'drawTexturePreviewFace' "節點本體內嵌輸出縮圖預覽(Texture2D) [6/25複驗:✓已做 node_faces.cpp:147 通用 face,預覽已搬回節點本體]"

# ══ node-classification ═══════════════════════════════════════════════════
check namespace-category-axis core    node-classification yes "app/src/runtime/graph.h" \
  'std::string category|categoryOf' "NodeSpec 加 namespace/category 分類軸欄位"
check search-relevancy-rank   core    node-classification yes "app/src/ui/quick_add.cpp app/src/ui/quick_add_tree.cpp" \
  'computeRelevancy' "搜尋結果依 relevancy 分數降冪排序(取代註冊順序)"
check scatter-subseq-filter   important node-classification yes "app/src/ui/quick_add.cpp app/src/ui/quick_add_tree.cpp" \
  'scatterMatch' "過濾升級為散射子序列(字元依序可不連續)"
check namespace-browse-tree   important node-classification yes "app/src/ui/quick_add.cpp app/src/ui/quick_add_tree.cpp" \
  'buildNamespaceTree|namespaceTree' "空搜尋畫 namespace 折疊樹(取代扁平列)"
check port-drag-type-filter   important node-classification yes "app/src/ui/editor_ui.cpp" \
  'QueryNewNode|inputFilter|outputFilter' "從 port 拖線開 quick-add 帶 dataType 預過濾"
check usage-count-weighting   polish  node-classification yes "app/src/ui/quick_add.cpp app/src/ui/quick_add_tree.cpp" \
  'usageCount|usageBoost' "relevancy 乘 op 被引用次數加權"
check symbol-tags             polish  node-classification yes "app/src/runtime/graph.h app/src/runtime/compound_graph.h" \
  'Essential|symbolTags' "Symbol 加 tags(Essential/Obsolete)供精選與沉底"
check result-row-type-color   polish  node-classification yes "app/src/ui/quick_add_tree.cpp app/src/ui/node_style.cpp" \
  'drawResultRow\(|labelColor\(outType' "結果列型別色+型別後綴+hover tooltip(三特徵) [6/25複驗:✓三項核心已做 e427d55,quick_add_tree.cpp:64 drawResultRow=labelColor(outType=firstOutputType()) 標題型別色 tint + TextDisabled 型別後綴 gated showType + portSummaryTooltip hover,node_style.cpp:69 labelColor;scenario quick_add_type_color.scn PASS。描述文字加值版 tooltip 仍 deferred(sub-detail)]"

# ══ render-output-page ════════════════════════════════════════════════════
check out-resolution-selector core    render-output yes "app/src/ui/toolbar.cpp app/src/ui/output_window.cpp" \
  'ComputeResolution|resolutions\.json|resolutionCombo' "Output 解析度下拉(Fill/720p/1080p/4k)+資料表 [6/25複驗:RequestedResolution 承重已做,缺 UI 選擇器層]"
check out-pan-zoom-viewmode   important render-output yes "app/src/ui/output_window.cpp" \
  'ViewMode' "Fit/1:1/Custom + 滑鼠 pan/滾輪 zoom 預覽 canvas"
check out-eval-start-instance important render-output yes "app/src/ui/editor_ui.h app/src/ui/editor_ui.cpp" \
  'g_evalStartNode|evalStartNode' "view-instance vs evaluation-start-instance 拆分"
check out-multi-window        polish  render-output yes "app/src/ui/output_window.cpp" \
  'AllowMultipleInstances|outputWindowId' "多 Output 視窗實例(各自 pin/解析度/view) [6/25複驗:單例 g_canvas/g_pinnedNode]"
check out-snapshot-png        important render-output yes "app/src/ui/toolbar.cpp" \
  'CGImageDestination|saveSnapshotPng' "toolbar Snapshot 按鈕→存 PNG(產品路徑非測試)"
check out-video-export        important render-output yes "app/src/ui/output_window.cpp app/src/ui/toolbar.cpp" \
  'RenderAnimation|exportVideo|offlineRender' "影片/序列匯出(離線逐格 cook+FPS/範圍/進度) [6/25複驗:無離線cook,framecook 僅即時逐frame]"
check out-window-persistence  polish  render-output yes "app/src/runtime/compound_save.cpp" \
  'OutputWindowState' "輸出視窗狀態(pin/解析度/相機)寫進 .swproj"
check out-show-output-slot    polish  render-output yes "app/src/ui/output_window.cpp" \
  'selectedOutput|showOutputSlot' "多 output op 選看哪個 output slot"
check out-background-color    polish  render-output yes "app/src/ui/output_window.cpp" \
  'backgroundColor|bgColorPicker' "Command 輸出的背景色 picker(現寫死黑)"

# ══ modes ═════════════════════════════════════════════════════════════════
check modes-player           core      modes yes "app/src/main.cpp" \
  'g_playerMode|--play' "獨立演出/播放(Player)模式:全螢幕乾淨輸出"
check modes-focus            important  modes yes "app/src/ui/view_modes.cpp app/src/ui/view_modes.h" \
  'g_focusMode|focusMode' "Focus Mode(F12):可逆隱藏浮動視窗全螢幕畫輸出"
check modes-toggle-all-ui    important  modes yes "app/src/ui/view_modes.cpp app/src/ui/view_modes.h" \
  'g_showChrome|toggleChrome' "Toggle-All-UI(Shift+Esc):一鍵收起所有 chrome"
check modes-os-fullscreen    polish     modes yes "app/src/app/menu.cpp" \
  'toggleFullScreen' "OS 視窗全螢幕切換(F11)+View>Fullscreen menu"
check modes-window-layouts   polish     modes yes "app/src/app/user_settings.h app/src/app/user_settings.cpp" \
  'SaveIniSettingsToMemory|LoadIniSettingsFromMemory|namedLayout' "Window Layout 存/載(F1-F10 10 組)"
check modes-graph-over-content polish   modes no  "app/src/ui/editor_ui.cpp" \
  'g_backgroundOutputNode|backgroundOutput|graphFade' "背景輸出層+graph 漸隱+邊界焦點切換 [6/25複驗:三子機制全缺]"

# ══ file-management ═══════════════════════════════════════════════════════
check folder-package-save    important file-management yes "app/src/runtime/compound_save.cpp" \
  'packageDir|perCompoundFile|\.swpkg|folderPackage' "一檔一 symbol 資料夾式 package(vs 單巨檔)"
check guid-child-id          important file-management no  "app/src/runtime/compound_save.cpp app/src/runtime/compound_graph.h" \
  'childGuid|toGuidString|Guid id' "child/slot/symbol id 改 Guid 字串(範式遷移) [6/25複驗:真缺但刻意fork—int-id 對 native 單機合理,header 具名]"
check child-ui-fields        polish    file-management yes "app/src/runtime/compound_save.cpp" \
  'snapshotGroupIndex|connectionStyleOverride|collapsedInto' ".t3ui child UI 欄位(Comment/Style/SnapshotGroup)"
check symbol-metadata        polish    file-management yes "app/src/runtime/compound_graph.h" \
  'symbolDescription|symbolTags|tourPoint' "per-symbol Description/Tags/Links/TourPoints"
check composition-path-persist polish  file-management yes "app/src/runtime/compound_save.cpp" \
  'compositionPath' "compositionPath(breadcrumb 檢視位置)持久化"

# ══ missing-subsystems ════════════════════════════════════════════════════
check variation-system       core      missing-subsystems yes "app/src/ui app/src/runtime" \
  'variation_crossfader|variation_mix|variation_snapshot_actions|VariationCrossfader' "Variation/Snapshot/Blend VJ 核心 [6/25複驗:PARTIAL 4/5—資料/crossfader/Nway-mix/actions 全接線,差 enabledForSnapshots filter+pool 持久化未 wire(JSON 存在沒人呼叫,document swap 清掉)]"
check gradient-inspector      important missing-subsystems yes "app/src/ui/inspector.cpp" \
  'gradientStop|drawGradientBar|GradientWidget|editGradient' "inspector Gradient 色帶編輯 widget [6/25複驗:真缺—gradient_ops_definegradient.cpp 存在但只是 op 的 Vec/Slider 逐參數列(走通用 inspector),無色帶條編輯 widget。op/資料型別有≠editing widget,別誤判]"
check io-editor-interaction   important missing-subsystems yes "app/src" \
  'SpaceMouse|sendLed|LedFeedback|ioEventsWindow|noteToLed' "IO 編輯器互動層(LED 回饋/SpaceMouse/錄製窗)"
check audio-export-record     important missing-subsystems yes "app/src" \
  'mixdown|recordWav|writeWav|exportAudio|waveformThumb' "音訊輸出半:離線 mixdown/錄 WAV/波形縮圖"
check perf-observability      important missing-subsystems yes "app/src" \
  'FrameTimeGrader|p99Grade|vsyncToggle|perfMiniGraph|crashReport' "FrameTime P99/perf graph/VSync/console/crash"
check external-beat-sync      important missing-subsystems yes "app/src" \
  'TriggerSyncTap|TriggerResync|beatTimer|oscBeat|tapProvider' "外部節拍同步:Tap/OSC beatTimer/sync-mode"
check bpm-auto-detect         important missing-subsystems yes "app/src/runtime/bpm_detection.h app/src/runtime/bpm_detection.cpp" \
  'BpmDetection|detectBpm|FocusFactor' "BPM 自動偵測(25s 自相關)"
check value-snap-interface    important missing-subsystems yes "app/src" \
  'ISnapAttractor|registerSnapAttractor|class SnapHandler|struct SnapHandler' "通用可註冊 snapping 介面(現 inline 在 timeline) [6/25複驗:真缺—snap 仍 inline 在 timeline_canvas.cpp:88 snapDragTime+collectSnapAnchors(timeline_snap.scn 綠),這條要的是抽成通用可註冊介面,抽取未做]"
check slider-ladder           important missing-subsystems yes "app/src/ui" \
  'SliderLadder|RadialSlider|precisionLadder' "SliderLadder 精度梯子 + RadialSlider 圓撥盤"
check asset-library-browser   polish    missing-subsystems yes "app/src/ui/asset_browser.cpp app/src/ui/asset_library.cpp" \
  'AssetLibrary|assetBrowser|drawAssetBrowser' "資源瀏覽器(拖/點建 LoadImage op+path 改寫)"
check skillquest-training     polish    missing-subsystems yes "app/src" \
  'SkillQuest|TrainingMode|PlayModeQuest|snapshotScoring' "SkillQuest 教學 play-mode + snapshot 評分"
check color-theme-system      polish    missing-subsystems yes "app/src" \
  'NamedTheme|colorTheme|ColorVariationDict|applyTheme' "命名主題系統(themes+per-field 變化+熱套用)"
check scalable-canvas-scope   polish    missing-subsystems yes "app/src" \
  'zoomToFit|jumpInOut|fitArea|scopeTransition' "通用 fit-area/zoom-to-fit/jump 過場導航"
check animcanvas-vsnap        polish    missing-subsystems yes "app/src/ui/timeline_curve_editor.cpp" \
  'snapForV|altInsertKey|insertKeyWithTangent' "曲線 V 軸 snap + Alt-hover 插 key"
check sliding-average-helper  polish    missing-subsystems yes "app/src/runtime" \
  'SlidingAverage|ringAverage|circularAverage' "環形平均 helper(beat/tap 平滑)"
check startup-lock-conform    polish    missing-subsystems yes "app/src" \
  'ConformAssetPaths|startupLock|crashRecoveryLock' "StartUp 鎖檔 crash-recovery + 路徑遷移 [6/25複驗:真缺但有未接線葉子—path 遷移=asset_relink.cpp:6 relinkAsset()、crash-recovery=auto_backup_restore.cpp:120 restoreLatestBackup(),兩者皆只有 selftest 呼叫、無 live UI/startup caller;startup 鎖檔本身不存在。leaf 已寫但 subsystem 未活]"

# ══ 輸出 ══════════════════════════════════════════════════════════════════
gtrep_total=$((done_n+gap_n))
if [ "$MODE" = "--tsv" ]; then
  for r in "${ROWS[@]}"; do IFS='|' read -r st sv ar id de hc <<<"$r"; printf "%s\t%s\t%s\n" "$id" "$sv" "$st"; done
  exit 0
fi
if [ "$MODE" = "--overview" ]; then
  printf "UI-CENSUS  B軌 %d/%d done · %d gap · %d 需複驗(範式級)  [規格:docs/agent/alignment/]\n" \
    "$done_n" "$gtrep_total" "$gap_n" "$check_n"
  exit 0
fi

echo "════ B 軌 UI/skin parity 現算 (vs alignment 6/22 快照) ════"
printf "greppable: DONE %d / GAP %d (共 %d)  ·  範式級需複驗(CHECK): %d  ·  全 52 條\n" \
  "$done_n" "$gap_n" "$gtrep_total" "$check_n"
echo "規格 SSOT: docs/agent/alignment/<area>.md  ·  DONE=6/22 說缺現有命中=快照 stale,掃一眼確認"
echo
last_area=""
for r in "${ROWS[@]}"; do
  IFS='|' read -r st sv ar id de hc <<<"$r"
  [ "$MODE" = "--gaps" ] && [ "$st" != GAP ] && continue
  if [ "$ar" != "$last_area" ]; then echo "── [$ar] ──"; last_area="$ar"; fi
  case "$st" in
    GAP)   tag="GAP  " ;;
    DONE)  tag="DONE✓" ;;
    CHECK) tag="CHECK?" ;;
  esac
  printf "  %-6s %-10s %-26s %s" "$tag" "$sv" "$id" "$de"
  [ "$st" = DONE ]  && printf "  (命中%s→stale?)" "$hc"
  [ "$st" = CHECK ] && printf "  [需agent讀碼,線索命中%s]" "$hc"
  echo
done
[ -z "$MODE" ] && { echo; echo "下一步:GAP=確定還缺(派 UI lane 選這些);DONE=校準掉的 stale(可回寫 alignment 標 done);CHECK=派輕量 agent 讀碼複驗。"; }

# 不同的模式 — TiXL parity gap + 規格

> 來源:tixl-parity-gap-audit workflow(2026-06-22,6 區 12 agent 唯讀清查;每條 gap 對 simple_world 實際碼 verify 過真偽)。
> 計數:真 gap 6 · 部分 2 · stale已做 0。

## 真 gap(已驗證確實未做)

### [core] 缺獨立的演出/播放程式 (Player)

- **對齊規格**:新增『演出模式』入口(可先做成同一 binary 的 --play / --present CLI flag,不必拆 binary)。進入後:(1)隱藏全部 editor chrome(toolbar/canvas/inspector/timeline),(2)把當前 root 或指定 pinned 節點的終端輸出 texture 全螢幕 blit 滿整個視窗,(3)cursor 隱藏,(4)Space/Esc 退出回 editor。Texture 選擇規則照 TiXL:取終端節點的第一個 texture 輸出。可接受先用既有的 previewTexture() seam + 一個 g_presentMode bool gate main.cpp 的 draw 呼叫。CLI flag 預設全螢幕、可加 --windowed/--width/--height。
- **驗證證據**:main.cpp:80-136 是唯一入口。CLI 只認 --help/-h (l.91)、--version/-v (l.112)、--open <file> (l.123-124)、以及 runSelftestFromArgs (l.81)。--help 的 usage 字串 (l.92-109) 完整列出所有支援 flag,沒有 --play/--present/--windowed/--width/--height/--loop。grep present/player/export 全 app/src/ 命中的全是 GPU 'fullscreen pass'/presentDrawable (l.272) 等無關字。draw() (l.184-313) 是單一 editor 迴圈,無任何 export/player 分支。

### [important] 缺 Focus Mode (F12: 圖隱、輸出獨佔全畫面、可隨時切回編輯)

- **對齊規格**:加一個 g_focusMode bool + F12 toggle。進入: 記下當前各浮動視窗 visible 狀態,隱藏 toolbar/inspector/timeline/output 視窗,把 previewTexture() 全螢幕畫在 canvas 背後(或直接全螢幕)。離開: 還原記下的 visible 狀態。與 modes-01 演出模式的差別在於 Focus Mode 是 editor 內可逆中間態(保留鍵盤編輯/導覽),演出模式是給觀眾的乾淨輸出。實作上兩者可共用『隱藏 chrome + 全螢幕輸出』的底層,差在是否仍跑 canvas 互動。
- **驗證證據**:keymap.cpp:415-416 自註 '"no-FocusMode": TiXL P in FocusMode sets the BACKGROUND output instead ... We have no FocusMode, so P always pins.';keymap.h:16 重述。kKeyTable (keymap.cpp:596-629) 完整列出所有綁定,沒有 F12,也沒有任何 ImGuiKey_F1..F12 (grep ImGuiKey_F 在 keymap.cpp 只命中 ImGuiKey_F 字母鍵 l.148/167)。沒有 g_focusMode 變數,grep focusMode/focus_mode 在業務碼零命中(只有 keymap 的否定註解)。

### [important] 缺 Toggle-All-UI (Shift+Esc: 一鍵收起/展開所有 UI chrome)

- **對齊規格**:加 g_showChrome bool(預設 true)+ Shift+Esc toggle,gate main.cpp:258-262 的 drawToolbar/drawInspector/drawTimelineWindow/drawOutputWindow 呼叫(canvas 永遠畫)。關掉時只留 canvas + 背景輸出。比 Focus Mode 簡單,不碰 layout/背景 instance,適合先做。注意:行號從上一棒的 258-262 已對齊現碼(main.cpp:258-262 確為這五個 draw 呼叫)。
- **驗證證據**:main.cpp:258-262 無條件每幀呼叫 drawToolbar/drawNodeCanvas/drawInspector/drawTimelineWindow/drawOutputWindow,沒有任何 bool gate。grep showChrome/show_chrome/toggleAllUi 在 app/src/ 零命中。keymap kKeyTable 無 Shift+Esc;Escape 只在 annotation_interact.cpp:200 與 quick_add.cpp:208(各自取消手勢/對話框)用到,非全域 toggle-UI。沒有 g_showChrome 變數。

### [polish] 缺 OS 視窗全螢幕切換 (F11)

- **對齊規格**:macOS 上呼叫 _pWindow->toggleFullScreen(nullptr) (NSWindow),綁 F11 + 在 menu.cpp 加 View 選單一個 'Fullscreen' item(Ctrl+Cmd+F 或 F11)。純 platform 層,不影響 UI 佈局。注意目前 menu.cpp 連 View 選單都不存在,要先新增一個 submenu 表。
- **驗證證據**:grep toggleFullScreen 全 app/src/ 零命中(只命中 GPU 'fullscreen pass' 註解)。app_delegate.cpp 不處理 toggleFullScreen。keymap kKeyTable 無 F11。menu.cpp 只有三個 submenu:kAppMenu(Quit l.54-59)/kFileMenu(New/Open/Save/SaveAs l.61-66)/kWindowMenu(Close Window l.68-)，buildMainMenu (l.78-90) 只 addSubmenu 這三個——沒有 View 選單,沒有 Fullscreen item。

### [polish] 缺 Window Layout 存/載系統 (F1-F10 / Ctrl+F1-F10, 10 組)

- **對齊規格**:若要 parity 此功能: (1) 啟用 io.IniFilename 或手動 SaveIniSettingsToMemory/LoadIniSettingsFromMemory, (2) 加 1-10 組 named layout 存到 settings 目錄(每組 = ImGui ini 字串 + 各浮動視窗 visible 旗標的 JSON), (3) F1-F10 載 / Cmd+F1-F10 存, (4) menu.cpp 加 'View > Layouts' 子選單。注意我們目前只有 ~4 個浮動視窗,layout 價值低於 TiXL(17 視窗);建議先做 modes-03(toggle-all)取得 80% 效益,layout 系統列為較後。
- **驗證證據**:grep Layout/layout 在 keymap.cpp 零命中;kKeyTable 無 F1-F10。沒有 io.IniFilename / SaveIniSettingsToMemory / LoadIniSettingsFromMemory 的使用(grep 零命中)。menu.cpp 無 'Load layout'/'Save layouts' item。各浮動視窗(Toolbar/Inspector/Timeline/Output)都是每幀無條件 Begin,visible 狀態未持久化。document.cpp 不存任何視窗配置。

### [polish] 缺 Graph-over-content 背景輸出 + 邊界漸隱互動

- **對齊規格**:(進階,依賴 modes-02 的背景 instance 概念) 在 drawNodeCanvas 的 ed::Begin 之前、grid 之後,若有 g_backgroundOutputNode 就把 previewTexture() 全螢幕 ImGui::Image 畫成底層;再依滑鼠到右邊界距離(RemapAndClamp 50→150)算 graph alpha,把 node/wire 整體用該 alpha 畫。邊界點擊 toggle 互動焦點。這塊承重大(要 canvas draw 支援整體 opacity + 背景 texture 層),建議在 modes-01/02/03 之後再評估;柏為的 3D/camera 互動若還沒上,這個的價值會打折。可複用既有 remapClamp (editor_ui.cpp:89)。
- **驗證證據**:輸出只在 output_window.cpp 的浮動 'Output' 視窗(l.48 ImGui::Begin("Output");l.104-105 previewTexture() -> ImGui::Image(tex, avail) 畫進該視窗內)。drawNodeCanvas (editor_ui.cpp:165-171) 在 ed::Begin 前只畫 drawCanvasBackgroundGrids() + 純色填充(StyleColor_Bg l.164),背後沒有 output texture 層,grep g_backgroundOutputNode/background image 零命中。RemapAndClamp 工具(editor_ui.cpp:89-90 remapClamp)雖已存在,但只用於 grid opacity (l.115/123),無『graph 整體 opacity 隨滑鼠到右邊界距離漸隱』+『邊界點擊切互動焦點給背景』的招牌互動。

## 部分完成(做了一半)

### [important] Timeline 永遠顯示, 缺『可隱藏 + 綁 sync mode』與 VJ(Tapping) 模式

- **剩什麼**:(1) 短期: 加 g_showTimeline bool 讓 timeline 可關(配合 modes-03 toggle-all 一起收)。(2) 若要 VJ 模式 parity: 加一個 syncMode (Timeline/Tapping) 設定,Tapping 時 timeline 視窗改顯示 BPM 編輯 + Sync tap 按鈕(左鍵=trigger tap、右鍵=resync measure、Ctrl 點=圓整 BPM)。BPM tap 的底層計時可後補;先做 visible gate 即可消除『timeline 無法隱藏』的最明顯落差。
- **現況證據**:Timeline 永遠顯示已證:timeline_window.cpp:162-167 drawTimelineWindow() 無條件 ImGui::Begin("Timeline");無 visible gate,grep showTimeline/show_timeline 零命中,main.cpp:261 每幀無條件呼叫。VJ/Tapping 已證缺:toolbar.cpp:140-143 只有單一 BPM DragScalar,沒有 tap-sync 按鈕;grep Tapping/tap/SyncMode/sync mode 在業務碼零命中。已做的部分:transport 本身存在(transportBpm/transportSetBpm/transportPlaying 等,toolbar.cpp:128-165),timeline lane/playhead/scrub 都運作。剩:(a) g_showTimeline 可隱藏 gate,(b) syncMode(Timeline/Tapping)+ BPM tap-sync 按鈕底層計時。

### [polish] 缺 Time display 四模式循環 (Bars/Secs/F30/F60) 與 loop/keyframe 導覽按鈕

- **剩什麼**:(1) Pos 顯示加一個格式循環按鈕: Bars / Secs(mm:ss:ff) / 幀@30 / 幀@60,點一下循環(換 DragScalar 的顯示字串)。(2) toolbar 加 Loop toggle 按鈕(目前無 transport loop range 概念,見 keymap.cpp:463-464 no-loop-range fork,需 frame_cook 支援 loop range)。(3) 加 jump-to-start / prev-key / next-key 三個按鈕(prev/next 已有鍵盤 . ,,接 keymap 同一路徑即可),依是否有前/後 keyframe 變灰。這些是 transport parity 的細修,非結構性。
- **現況證據**:toolbar.cpp:124-173 確有 Play/Pause(l.129)、Pos drag(l.135 標籤永遠 '(bars)')、BPM(l.142)、Speed(l.162)。已做:鍵盤跳 keyframe . , (keymap.cpp:251 handleJumpToNextKeyframe / l.279 handleJumpToPrevKeyframe)、J/K/L playback(handlePlaybackBackwards/Stop/Forward)、Home=JumpToStartTime(keymap.cpp:466-469)。缺:(a) time-format 循環按鈕——Pos 永遠 bars 格式,grep TimeDisplay/Secs/F30/F60 零命中;(b) toolbar 無 Loop toggle 按鈕,grep Loop 在 toolbar 零命中,keymap.cpp:463-464 還自註 'no-loop-range: we have no LoopRange concept';(c) toolbar 無 jump-to-start / prev-key / next-key 三個圖示鈕(鍵盤路徑有,toolbar UI 沒有),也無『依有無前/後 keyframe 變灰』。

## verify 戳破的 stale(survey 說沒做、其實做了)

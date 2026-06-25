# 第一次 6 區 audit 整塊漏掉的子系統(普查 + 補漏挖出，grounded)

> 來源:`tixl-nonnode-coverage-sweep`(覆蓋 838 檔 100%)的 completeness-critic + 22 漏檔補讀。
> 這些是**抽樣 audit 完全沒看到**的子系統。每條讀出來的、附 TiXL 源碼依據。嚴重度排序。

---

## ★ CORE:Variation / Snapshot / Blend 系統（simple_world 完全不存在）

TiXL 對 VJ 現場演出**最核心的工作流之一**:把一組參數狀態存成 snapshot，現場用 MIDI/滑鼠/OSC 在 snapshot 之間**加權 morph**。grep `VariationExplorer/SnapshotGroup/presetCanvas` 全 0。跟 Player 同等承重。建議**當一整個 seam 規劃**。

### 1. 資料模型（`Variation.cs`）
- `Variation` = 一筆參數快照:`Guid Id / string Title / int ActivationIndex（MIDI/鍵格位）/ bool IsPreset / Vector2 PosOnCanvas,Size`，核心 = `Dictionary<Guid childId, Dictionary<Guid inputId, InputValue>> ParameterSetsForChildIds`（外層 key=被影響子節點，`Guid.Empty`=composition 本身；內層=input→覆蓋值）。
- **Preset vs Snapshot 只差一個 bool**:`IsSnapshot => !IsPreset`。Preset 套**單一選中節點**;Snapshot 套**整個 composition 的多個啟用子節點**（VJ「整場狀態」）。
- 序列化 `ToJson/TryLoadVariationFromJson`:讀檔逐一驗 input 仍存在+型別仍對，跳過 `ExcludedFromPresets` 的 input，缺失/型別不符不爆。
- **sw 規格**:`sw::Variation{id,title,activationIndex,isPreset,posOnCanvas, std::map<NodeId,std::map<InputId,Value>> parameterSets}`；per-symbol/per-document variation pool 檔；讀檔容錯（缺 input 跳過）。

### 2. Snapshot 抓取 / 過濾（`VariationHandling.cs`）
- `CreateOrUpdateSnapshotVariation(index)`:已有先刪→只收 `EnabledForSnapshots==true` 的子節點→從當前值建 Variation→`FindFreePositionForNewThumbnail` 自動排位→立刻落盤。
- **active pool 由 UI 焦點決定**:選單一節點→該 symbol=preset pool、parent=snapshot pool;沒選→composition=snapshot pool。**`Lib.` namespace 的 op 禁止 snapshot**（會污染所有 instance）。
- **sw 規格**:每節點 `enabledForSnapshots` 旗標;抓 snapshot=掃 composition 子節點只收 enabled 的;active pool 跟 UI 焦點;擋共用 op。

### 3. 2-way crossfader 加權混合（`BlendActions.cs`，VJ 核心觸發）
- 模型:`_snapshotLeft(0)/_snapshotRight(127)/_activeIsLeft`;blend target 永遠是 active 對側。
- `UpdateBlendingTowardsProgress(index, midiValue)`:`pos=midi/127`，≥0.99 收右/≤0.01 收左，中間 `blendAmount=activeIsLeft?pos:1-pos`。
- **平滑器**:`SpringDamp(target,damped,ref vel,20f,1/60f)` 把 127 階粗 MIDI 磨平;`|vel|<0.0005` 視為收斂→`ApplyCurrentBlend()` 燒成新 active + 翻 `_activeIsLeft`。端點不立即完成，等阻尼穩。
- **sw 規格**:兩格 snapshot + 0..1 fader→per-param 線性插值;fader 輸入（MIDI/滑鼠/OSC）先過 spring-damp（係數 20，frame 1/60）;到端點等阻尼穩才 commit。

### 4. N-way 加權混合插值公式（`ExplorationVariation.Mix`）
- 每參數依型別（float/vec2/vec3/vec4）:`value=Σ(neighbourValue*weight)` 然後 `*=1/sumWeight`（正規化加權平均）;缺值 neighbour fallback 當前值（不當 0）。
- 加 scatter:`value += random(-scatter,scatter)*ParameterScale*ScatterStrength`;per-param `ScatterStrength/ParameterScale/Min/Max/ClampMin/Max`。
- 套用走 `MacroCommand of ChangeInputValueCommand`（可 undo）。
- **sw 規格**:Mix=per-param 正規化加權平均按型別分支;套用走 command/undo（sw 已有 ChangeInputValueCommand 類可掛）。

### 5. 觸發動作 / 格位語意（`SnapshotActions.cs`）
- `ActivateOrCreateSnapshotAtIndex(i)`:已存在→Apply+設 active;不存在→建新+active。**一鍵雙語意**（有就叫出，沒有就存當前）。`SaveSnapshotAtIndex`=強制覆蓋;`RemoveSnapshotAtIndex`=刪。直接套用時設 active 格+清 blend 對側。
- **sw 規格**:格位陣列（數字鍵/MIDI pad）;每格「有→套，沒→存」，修飾鍵覆蓋，另鍵刪。

---

## IMPORTANT:其他漏掉的承重子系統

### Gradient 編輯 authoring（critic）
runtime 有 `sw_gradient.h` + value-op SampleGradient，但 **inspector 對 Gradient input 零編輯 widget**（grep Gradient in inspector=0）。圖裡可有 gradient 值在跑，但柏為**無法在 UI 畫色帶**。柏為明確 authoring 域，卻不在任何區。規格:inspector 色帶 widget（stop 增刪拖 + presets）。

### IO 編輯器互動層（critic，依賴 Variation 系統）
node-coverage 盤了 73 個 io **節點**，但 TiXL 的 IO 還有**編輯器互動層**:8 種 MIDI 裝置 note/LED map（按硬體鍵觸發 snapshot、燈號回饋當前狀態）、SpaceMouse HID 驅動 3D 相機、IO Events 視窗顯示錄製中的 DataSet。綁在 Variation/Snapshot 上，op 計數抓不到。柏為硬體 VJ 控制的真實工作流。

### Audio 匯出 / 錄製 / 波形縮圖（critic）
音訊「輸入/分析/playback」半已大量做。缺的是「輸出」半:離線 audio mixdown（配影片匯出）、live recording 到 WAV、FFT→waveform PNG 縮圖（grep 全 0）。跟 render-output 的影片匯出同一個演出輸出半身——影片匯出沒對齊音軌=匯出無聲。

### 效能/可觀測性 + 維運（critic）
FrameTimeGrader P99 評分 + perf mini-graph + VSync toggle + CSV、Console log 視窗、auto-backup 排程、crash report（四項 grep 全 0）。fps 評分/VSync 對即時 60fps 演出直接相關（卡頓即翻車）;auto-backup 對長 session 防丟檔。6 區把編輯器當只有 canvas+inspector+timeline+output，漏了維運層。

### 外部節拍同步（補漏 `PlaybackUtils/OscBeatTiming/BeatTimingPlayback`）
sw 只有**手動 BPM 旋鈕**（toolbar DragScalar）。TiXL:`AudioSource==ExternalDevice && Syncing==Tapping`→時間軸交給節拍（`FxTimeInBars=BeatTime`）;Tap 同步（`TriggerSyncTap/TriggerResyncMeasure`）;OSC `/beatTimer` listener（UDP 12345，每拍一訊號）。規格:tapProvider+bpmProvider 抽象 + sync mode 切換（Timeline vs Tapping）。

### BPM 自動偵測（補漏 `BpmDetection.cs`）
25 秒低頻能量滑動 buffer→smooth（減平滑去音量起伏）→對 80..180 BPM 自相關能量差掃描 + FocusFactor（偏好當前值防跳）。「別每 frame 算」。sw 有 audio_monitor 但無 BPM 偵測。

### 通用值-snapping 介面（補漏 `ValueSnapHandler/SnapResult`）
**sw 已把這套數學 inline 進 timeline**（`timeline_canvas.cpp` 注解逐條對 TiXL:threshold=5px/scale、force=threshold−dist、max-force-wins、<1e-5 reject）。**但沒抽成通用介面**——只服務時間軸，inspector 數值/曲線 V 軸都沒共用。Gap=升級成可註冊 `ISnapAttractor`+`SnapHandler`。

### 精密數值編輯 SliderLadder / RadialSlider（補漏）
sw inspector 用裸 `ImGui::DragScalarN`（固定 step 0.01）。TiXL:**SliderLadder**=拖數值時彈梯子，垂直分 7 精度檔（1000…0.001）、水平拖×檔位、Alt×0.01/Shift×10/Ctrl 對齊;**RadialSlider**=圓撥盤，半徑→值域、自動刻度。手感層明顯落差。

---

## POLISH（列出供完整，不建議現在做）

- **AssetLibrary 資源瀏覽器**（critic）:拖資產到畫布自動建 load-op、點相容資產改寫 op FilePath（undoable）、OS 檔案操作。file-management.md 盤的是存檔格式，不是資產瀏覽綁定。sw 載資產靠手打路徑。
- **SkillQuest 教學 play-mode**（critic）:劫持 layout、live 對 snapshot 評分（Untouched/Required/Correct/Warm/Forbidden）。modes.md 完全漏了 training mode。
- **色彩主題系統**（critic）:named themes + per-field ColorVariation dict + 存檔 + 熱套用。sw 只有硬編單一配色。
- **ScalableCanvas scope/transition**（補漏）:通用 fit-area/zoom-to-fit/jump-in-out 過場;sw 各畫布各自為政無通用導航。**[6/25複驗] 核心已對齊 TiXL（原生名,故 census string-grep false-neg）**：fit-selection/fit-content=F→`ed::NavigateToSelection/Content`（keymap.cpp:143）+ 進出 compound 自動 fit（editor_ui.cpp:450）；timeline 垂直 fit=`SetVerticalScopeToCanvasArea` 1:1（timeline_window.cpp:230,含 0.15 padding+damp）。**殘餘兩塊皆 NON-pure-ui（deferred,別當純皮 Tier1）**：① JumpIn/JumpOut overshoot 過場（imgui-node-editor 只有單段 ease 無 overshoot API→需改 vendored `third_party/imgui-node-editor/`）② 通用跨畫布 scope 抽象（需 runtime/app 共用層）。
- **AnimationCanvas U/V 雙 snap + Alt 插 key**（補漏）:曲線畫布 V 軸 snap、Alt-hover 插 key 用前 key tangent;sw U 軸已 port，V 軸/Alt-插 待交叉比對 `timeline_curve_editor.cpp`。
- **SlidingAverage**（補漏）:~20 行環形平均 helper，beat/tap 平滑用。
- **StartUp 鎖檔 crash-recovery + ConformAssetPaths 路徑遷移**（補漏）:需先有 auto-backup/asset 系統，優先低。

---

## 已 PORT（補漏確認，非 gap）

- **ColorVariation**（`ColorVariation.cs`）= `node_style.cpp:46 variation(c,bri,sat,op)` 已忠實 port。
- **timeline snapping/raster/scalable-canvas 數學**（S6 批次）已 port，只是未抽通用。
- **animation 曲線引擎 + audio 分析鏈**已做得很深（見 [_COVERAGE.md](_COVERAGE.md) 校準）。

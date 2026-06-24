# EXPERIENCE_SCOPE_GAPS — 體驗軸分母補遺（系統普查）

> 柏為 2026-06-24 下令「把分母一次釘死」。連續 3 次人肉抓漏（SliderLadder/Player/Output 視窗）暴露根因：[EXPERIENCE_PARITY_PLAN](EXPERIENCE_PARITY_PLAN.md) 寫的時候主要靠 `ui-surface.md` 一份，沒系統納入 `alignment/` 其他普查檔。
> 本檔 = **體驗軸 1:1 完整分母的補遺清單**（sub-ledger，非 dashboard）。逐條對 `app/src` ground-truth 過、排除已做的 stale。SSOT 仍是各 `alignment/` 檔。
> 排程權威仍是 EXPERIENCE_PARITY_PLAN（演出脊椎 + 編輯 lane + 獨立施工圖）+ MASTER_PLAN。本檔的項要 pull 進批次時，照那邊的「主檔避撞 + 驗證閘分流」規則。

## 普查方法
讀施工圖全文（分子）+ 7 份體驗 alignment 檔（`ui-surface` / `render-output-page` / `modes` / `missing-subsystems` / `file-management` / `_sweep-gaps-full` / `_COVERAGE`，分母）+ `git log --grep experience`（已做）。節點軸（`node-classification`/`node-coverage` ~800 op）排除。

---

## ★ 最系統的漏：新增「檔/資產/設定軸」獨立施工圖

施工圖焦點放在「演出脊椎 + 編輯畫布」，但 TiXL 使用體驗的**進場（開專案/找資產）+ 跨 session 記憶（recent/layout/keymap）整塊缺席**。這幾條互相牽連（拖檔依賴 asset-binding 表、layout 依賴 user-settings 層），建議收成一條新獨立施工圖：

| # | 嚴重 | 名稱 | 現況證據 | SSOT |
|---|---|---|---|---|
| 2 | [important] | **AssetLibrary 資源瀏覽器 + 拖檔建 load-op**（拖資產→自動建 op、點相容資產→改寫 FilePath、undoable）| `grep AssetLibrary` 只有 selftest 樁，業務碼 0 | missing-subsystems §AssetLibrary / file-management |
| 3 | [important] | **Drag-and-drop 進畫布**（symbol/file/asset 三類）| `grep NSDragging/handleDrop` app/src=0 | _sweep:64,475 |
| 11 | [important] | **Keymap 持久化/換鍵**（user JSON 蓋 factory）| `grep keymap.*json`=0；硬編 kKeyTable | _sweep:472,151 |
| 12 | [important] | **Editor user-settings 持久化**（recent files / window layout / view-area per-symbol；`io.IniFilename` 目前刻意關）| `grep recent`=0；只有 audio device UID prefs | file-management §FILE-11 / modes |
| 17 | [polish] | **ProjectHub 啟動著陸頁**（無專案時專案卡片 + Add Project；SkillQuest 引導）| `grep ProjectHub/landingPage`=0 | _sweep:469 |

---

## 演出脊椎深度缺口（P6 已基本版，深度未補）

| # | 嚴重 | 名稱 | 現況 | SSOT |
|---|---|---|---|---|
| 5 | [important]⚠️ | **Player 深度**：ScreenManager 多螢幕選 target / output spanning / `--loop`/`--windowed`/`--width` flag | P6（`53edf5f`）只做基本全螢幕 blit | modes §Player / render-output |
| 6 | [important]⚠️ | **Focus 深度**：pin output 當 graph 背景（graph-over-content）+ 邊界漸隱互動 | P6 只收 chrome；`grep g_backgroundOutputNode`=0 | modes §Focus / Graph-over-content |
| 16 | [polish] | **F11 OS 視窗全螢幕 toggle**（NSWindow toggleFullScreen + View 選單 item）| `grep toggleFullScreen`=0 | modes §F11 |

---

## 編輯器 UI 缺口

| # | 嚴重 | 名稱 | 現況 | SSOT |
|---|---|---|---|---|
| 1 | **[core]** | **Required relevancy 完整概念**（不只紅指示=L-G 已做；是 input/output line 依 relevancy 顯隱 + 排序權重）| L-G 只做紅下三角；relevancy line visibility 未做，PortSpec 只有 bool required | ui-surface §required / _sweep:196 |
| 4 | [important] | **In-window menu bar + View 選單五 toggle**（ShowMainMenu/Toolbar/Timeline/Minimap/Title）| `BeginMainMenuBar` app/src=0；menu.cpp 是 macOS 原生 NSMenu 非 in-window | ui-surface §工具列 / modes §F11 |
| 9 | [important] | **Color picker 進階**（HSV wheel + HDR value + alpha + used-colors palette）| `grep ColorEdit/HSV` inspector.cpp=0 | _sweep:157 |
| 10 | [important]⚠️ | **節點本體 multi-input 群組框 + 標題隨寬度縮放**（值字串+zoom gating 已做 `e2cee23`）| multi-input 框 / title downscale 未見 | ui-surface §即時值字串 partial |
| 13 | [important] | **節點 thumbnail 預覽 face**（Texture2D 輸出貼本體；柏為已下注做）| `node_faces.cpp` kFaces 只有 AudioReaction | ui-surface §節點縮圖（施工圖 Tier3 已列未做）|
| 18 | [polish] | **ScalableCanvas 通用導航**（fit-area/zoom-to-fit/jump 過場，跨畫布共用）| timeline 有自己的數學未抽通用 | missing-subsystems §ScalableCanvas |
| 19 | [polish] | **通用值-snapping 介面**（ISnapAttractor/SnapHandler；目前 inline timeline）| 未抽通用 | missing-subsystems §值-snapping |
| 20 | [polish] | **節點 indicator badges + idle-fade + Disabled/Bypassed X overlay**（本體狀態視覺語彙）| 施工圖只列「邊界凸角」未含這些 | _sweep node-render 群 |

---

## Timeline 深度（施工圖已掛指標，未開工）

| # | 嚴重 | 名稱 | 現況 | SSOT |
|---|---|---|---|---|
| 7 | [important] | **Timeline VJ / Tapping sync mode** | `grep Tapping/SyncMode` 業務碼=0 | modes §Timeline / missing-subsystems §外部節拍 |
| 8 | [important] | **AnimationCanvas keyframe V 軸 snap + Alt-hover 插 key（用前 key tangent）** | curve_editor 有 double-click 插 key，無 V snap/Alt-hover | missing-subsystems §AnimationCanvas |

---

## 維運 / 主題（polish，可後置）

| # | 嚴重 | 名稱 | 現況 | SSOT |
|---|---|---|---|---|
| 14 | [polish] | **效能/可觀測性 overlay**（P99 frame grader + fps graph + VSync toggle + CSV + Console log + crash report）| `grep FrameTimeGrader/p99`=0 | missing-subsystems §效能可觀測性 |
| 15 | [polish] | **色彩主題系統**（named themes + per-field ColorVariation + 存檔 + 熱套用）| `grep colorTheme`=0；硬編單一配色 | missing-subsystems §色彩主題 |

## 建議 out-of-scope（記回收條件，不主動採）
- #21 **SkillQuest 教學 play-mode**（劫持 layout + live 對 snapshot 評分）—— 與核心演出/編輯體驗無關，柏為單機工具不需。回收條件=要做教學內容時。

## stale 排除（alignment 寫了 gap 但其實已做，勿重做）
- **節點本體即時值字串 + zoom gating**：`e2cee23` 已做（commit 在 alignment 普查之後）。
- **Annotation frames draw/resize/drag/rename**：`annotation_draw.cpp` + `annotation_interact.cpp` 已存在。
- **Exit/unsaved 確認對話框**：`document.cpp askUnsaved()` 三按鈕已做。
- **框選矩形顏色 / .t3 可讀名**：alignment 自己已標 stale。

---

## 分母總結（補齊後）
體驗軸**約還漏 21 項**：**1 [core]**（#1 relevancy 完整）+ **約 11 [important]** + **約 9 [polish]**（含 #5/#6 P6 深度=⚠️部分）。
- **最嚴重最系統**：「檔/資產/設定軸」整條缺席（#2/#3/#11/#12/#17）→ 建議升獨立施工圖。
- **次嚴重**：in-window menu bar + View 選單（#4）= 多條 polish 的共同 UI 入口。
- 這些 + 施工圖既有的演出脊椎/編輯 lane/MagGraph/Output/Timeline/Gradient/Audio/SliderLadder = **體驗軸 1:1 完整分母**。

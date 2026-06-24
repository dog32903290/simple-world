# EXPERIENCE_PARITY_PLAN — TiXL 使用體驗 1:1 復刻施工圖

> 柏為 2026-06-24 定：**1:1 完整復刻 TiXL 使用體驗**。演出迴路為脊椎、編輯視覺並行穿插。
> **併入 sw-batch 統一並行池**（與引擎節點承重同一張地圖，orchestrator 統一選批避撞）。
> 本檔 = **體驗軸 sub-plan**。頂層路由權威仍是 [MASTER_PLAN](MASTER_PLAN.md)（唯一 dashboard）。
> UI gap 規格 SSOT = [alignment/ui-surface.md](alignment/ui-surface.md) + [alignment/modes.md](alignment/modes.md) + [alignment/missing-subsystems.md](alignment/missing-subsystems.md)。對齊基準 = `external/tixl/Editor/Gui` 源碼 + `artifacts/sw_tixl_*.png`。**事實以 git/碼為準。**

---

## 為什麼有這份檔（垂直切片方法論）

復刻 TiXL 不只是港節點。一個功能要能用 = **三層都到**：①功能（引擎算得出）②水管（driver tick 它 + 輸出接回 graph）③UI/皮（手碰得到、看起來像 TiXL）。

2026-06-24 eye-hand 實證（`artifacts/` 開場截圖）：
- **常規水管全通**：節點 cook、畫面顯示（Output 窗有真實粒子雲）、滑鼠選取、拖參數改畫面、command/undo、存載 —— 親眼驗過活的。
- **斷的只有「現場演出控制」三條 live 橫切水管**：Variation crossfader（引擎 100% 綠但零 frame-loop 驅動 + 輸出寫 LiveParams 沒回 graph）、live MIDI/OSC→graph、BPM 自動同步。
- **演出 UI 零入口**：map.json widget 掃描，Variation/snapshot/crossfader UI = 完全不存在。

→ 結論：**機器的腦做得很遠（428 節點 + 粒子系統當場跑），但手（現場控制）是斷的。** 這份施工圖補的是「手」+「皮」，用垂直切片（一段水管 + 一塊 UI + 觸發，端到端通一次）逐條接，每條通了就成樣板讓後面順著長。

---

## 完成定義（1:1，全進）

逐項對齊 TiXL（基準＝源碼＋截圖），下列全部達成才算完成：

**A. 演出迴路（脊椎）**
- Variation crossfader 真驅動 graph + 完整面板（對 TiXL VariationCanvas / BlendActions）
- live MIDI/OSC → graph 參數綁定 + MIDI learn UI
- snapshot 觸發 + LED 硬體回饋 + 鍵位映射
- BPM 自動偵測驅動 transport
- **Player / 演出輸出模式**（`--play` 全螢幕 blit，modes.md [core]，與 Variation 同等承重）
- **Focus Mode / 收起全部 UI**（modes.md，F12 / Shift+Esc）

**B. 編輯器 UI（並行，ui-surface 9 真 gap + 4 半成品全做）**
- 加節點面板：scatter 搜尋 + 相關度排序 + 命名空間樹
- inspector：參數重置 + default/override 字色 + jog-dial + **SliderLadder/RadialSlider 精密數值編輯**（★壓測補：MASTER_PLAN L2 + missing-subsystems 列承重 critic 級，repo grep=0，**不可用 jog-dial 偷換**，是獨立的精密拖曳階梯）
- 節點右鍵選單：Disable/Duplicate/Delete/Align/Select connected/Display as/Add Comment
- 節點本體：即時值字串 + 縮放 gating + 縮圖預覽 + 邊界節點凸角形狀
- 連線：箭頭三角 + hover-to-split
- 必填輸入紅指示 + Undo/Redo 顯示下一步標題 + 工具列範式

**C. 獨立施工圖（各自成序列脊椎，本檔只掛指標）**
- **MagGraph 磁吸畫布範式重寫**（[core] 最大；內部：座標模型重寫→比例對齊→磁吸貼齊→退化三角連線）
- **Timeline 編輯**（`ui/timeline_*` 已大量存在；補 sync mode + VJ tapping，見 modes.md；**+ AnimationCanvas keyframe V 軸 snap + Alt-插 key**，壓測補，missing-subsystems §75 待交叉比對 `timeline_curve_editor.cpp`）
- **Gradient authoring widget**（柏為 authoring 域；inspector 零 gradient widget）
- **Audio 匯出 / 錄製 / mixdown**（missing-subsystems；影片匯出需對齊音軌否則匯出無聲）

> 註：節點庫 ~800 顆對齊是**另一條軸**，由 MASTER_PLAN / OP_BACKLOG 管，不在本檔。

---

## 與 sw-batch / MASTER_PLAN 的整合（柏為 2026-06-24 想法 + 壓測修正）

**想法（柏為）**：把體驗施工圖併進 sw-batch 統一並行池，跟引擎承重排同一張地圖 → 引擎 lane 被 cook-core 序列鎖住時，多一批不撞檔的 UI 活可選，並行度變高。

**壓測修正（2026-06-24，subagent 對碼 ground-truth）**：「體驗 vs 引擎彼此不互斥核心承重」只對一半。三條鐵律：

1. **核心承重共用同一條序列 owner-lock。** 體驗軸裡動核心檔的 lane —— S0（`graph.h`）、演出脊椎 P1-P5（`app/frame_cook.cpp`）、Tier3（碰 cook/resident）—— 跟引擎 cook-core 脊椎 **S1-S4 + `point_graph.cpp` 同一條鎖**，不可並行。orchestrator 選批時這些算「序列脊椎」不算「並行 lane」。
2. **驗證閘分流。** 節點 lane 走 `golden + refuter + --bite`；體驗 lane 走 `eye-hand 截圖比對 + 每日檢查點`。orchestrator 依 lane 類型選對的閘 —— **不可拿 golden 套 UI 工作**（會卡），也不可拿 eye-hand 放行碰 cook 的改動（要補 golden）。
3. **單 loop 單 driver。** 柏為新 loop 跑工程；體驗軸與引擎軸由**同一個 orchestrator 序列選批**，不雙開 session（血證 [[sw-batch-no-parallel-launch]]：雙 driver 撞 main checkout 雙寫）。

---

## 檔域衝突矩陣（orchestrator 選批避撞依據）

> **★ 壓測 BLOCK 修正（2026-06-24）：Tier 是「驗證閘維度」（碰不碰 command/cook），不是「避撞維度」（撞不撞同一檔）。** 兩條 Tier1 不必然能並行 —— 它們可能撞同一個 `ui/` 檔。**選批避撞看「主檔」欄，不看 Tier。**

| lane | **主檔（避撞看這欄）** | 撞核心？ | 排程類 | 驗證閘 |
|---|---|---|---|---|
| **S0** header schema | `runtime/graph.h` | ✅ graph.h（全體讀 NodeSpec/PortSpec）| **序列前置（最先，獨佔 graph.h；P1/Tier1-不碰-graph.h 可同時跑）** | 編譯綠 + `--bite` 不回歸 |
| **P1-P5** 演出水管 | `app/frame_cook.cpp` + `app/variation_apply.*` | ✅ frame_cook（cook 心跳）| **序列脊椎（與 S1-S4 同鎖）；不碰 graph.h 故不等 S0** | eye-hand + golden（回寫數值）|
| **P6** Player 模式 | `app/main.cpp` present + `platform/`（全螢幕 blit）| ⚠️ present path（與 frame_cook 心跳相鄰）| 半序列 | eye-hand + **`--bite` present 不回歸**（壓測補）|
| **Tier1** 純皮 | **`ui/node_draw.cpp` / `ui/editor_ui.cpp`（★序列瓶頸，見下）** | ❌ | 並行**但同主檔者序列** | eye-hand 截圖比對 |
| **Tier2** 碰 command | `ui/inspector.cpp` / `ui/editor_ui.cpp` + `app/command` | ⚠️ command（序列化進 undo+.swproj）| 並行但顧 undo | eye-hand + undo round-trip + **.swproj round-trip**（壓測補）|
| **Tier3** 碰 cook | `ui/node_draw.cpp` / `ui/quick_add.cpp` + `runtime/`（讀 effectiveInput/resident 貼圖）| ✅ | **與 cook-core 協調** | eye-hand + golden |
| **MagGraph** | `ui/editor_ui.cpp` + `app/command` + .swproj | ⚠️ command + .swproj 序列化 | **獨立施工圖（自成脊椎）** | eye-hand 手感（大量柏為驗）|
| **Timeline** | `ui/timeline_*.cpp` | ❌（大多 ui，獨立檔群）| 獨立施工圖 | eye-hand + round-trip |
| **Gradient** | `ui/inspector.cpp` + 新 widget 檔 | ⚠️ widget 資料模型 | 獨立施工圖 | eye-hand + 柏為 authoring 簽收 |
| **Audio 匯出** | `platform/audio` | ❌（platform）| 並行 lane | round-trip + codex-ears |

### ★ Tier 內序列瓶頸（壓測抓到的真避撞依據）
- **`ui/node_draw.cpp`（~212 行，單一入口 `drawChild`/`drawBoundaryDef`）= 多條 lane 的共撞點**：Tier1 的「邊界節點凸角」「必填紅指示 blinkValue」+ Tier3 的「即時值字串/縮放 gating」「縮圖預覽」**全落這一檔**。→ **這幾條彼此序列，不可並行**（即使分屬不同 Tier）。
- **`ui/editor_ui.cpp` = 連線繪製共撞點**：Tier1 連線箭頭 + Tier2 hover-to-split + MagGraph 都改連線/Link 路徑。→ **序列**。
- **真正能自由並行的**：主檔互斥的組合，例：`node_draw.cpp`（一條）∥ `inspector.cpp`（Tier2 重置）∥ `quick_add.cpp`（搜尋）∥ `timeline_*.cpp` ∥ `platform/audio`。orchestrator 選批 = **每個主檔同時只派一條 lane**。

---

## S0 — 共享 header 前置 batch（序列脊椎，最先做完）

**為何最先**：壓測抓到的並行撞車根因。`NodeSpec`/`PortSpec` 在 `runtime/graph.h`，L-B（category）/ L-G（required）/ Tier3 / 節點 lane 全讀它。不先一次加完，多條 lane 會同時改 graph.h。

**可動工 spec**：
- `graph.h` `struct NodeSpec`（約 :47）加 `std::string category;`（命名空間，對 TiXL Symbol.Namespace）。
- `graph.h` `struct PortSpec`（約 :27-46）加 `bool required = false;`（對 TiXL relevancy/Required）。
- **★ 壓測地雷（必遵）**：`PortSpec` 是 **positional aggregate-init**，現任最後 member 是 `strDef`（:45，註解自承「LAST member + defaulted 才讓數百筆 `{...,multiInput,vecArity}` positional init 維持有效」）。**`required` 必須加在 `strDef` 之後當新的最後 member**，插中間會破壞數百筆聚合初始化、S0 第一步就全線編譯紅。
- 兩欄位 **additive、defaulted**（category 空字串 / required false）→ 既有 ~428 節點註冊不必改、不回歸。
- 在少數示範 op 上填值（需 Points 輸入的 modify 家族標 required；幾顆 op 標 category）證明欄位活。
- **閘**：編譯綠 + check-arch + `--bite` 不回歸（純加 defaulted 欄位，golden 應全綠）。
- **owner-lock（量化，壓測修正）**：S0 只加兩個 defaulted 欄位 = **小且快（預期 < 1 次 build cycle）**。它凍結的是「所有改 graph.h schema 的寫入」——**含 MASTER_PLAN 的 L4 節點 lane**（它們註冊 NodeSpec/PortSpec）。因為短，凍結代價低；但 orchestrator 要知道 **S0 跑時整條引擎 L4 暫停寫 graph.h**（讀不受影響）。**S0 一個 batch 一次合，合完立即解凍。**
- **★ S0 不阻塞 P1 與純 ui/ lane**：P1（動 frame_cook/variation_apply）、Tier1 不碰 graph.h 的純皮（連線箭頭/右鍵選單）**都不碰 graph.h，可與 S0 同時跑**。只有「靠 category/required 的 lane」（L-B 搜尋樹、L-G 必填指示）要等 S0。

---

## 脊椎 — 演出迴路垂直切片（序列，與 cook-core owner-lock 協調）

### P1 — Variation 最薄垂直切片（第一片，詳細）

**這是樣板片：第一條端到端通的演出水管。後面 P2-P5 複製它的 pattern。**

真卡點（壓測修正）：**回寫已建好，缺的是 frame-loop tick hook**。
- 已存在：`app/src/app/variation_apply.h:83 buildBlendTowardsVariationCommand` + `:107 buildNWayMixCommand`（已實作、已 selftest，把 blend 變 undo-able SetOverrideCommand）。
- 缺：①`frame_cook.cpp` 沒有任何 variation 引用（grep 0）②crossfader 的 per-frame `UpdateBlend`（spring-damp 需逐幀推進，`variation_crossfader.h` 自述 once-per-frame）沒被呼叫。

**可動工步驟**：
1. **水管 — driver hook**：在 `frame_cook.cpp` 的 per-frame cook 序列（仿 `cookStatefulValueOp` / `cookDetectBpmNodes` 的掛點，約 run() 主體）每幀呼叫 crossfader 的 `tick(LiveParams&)`（`variation_crossfader.h:120`），settle 時 commit。**★ 壓測修正**：`tick` 內部 spring 用**固定 `kTimeStep=1/60`**（忠實 TiXL BlendActions.cs:244），**不吃實 dt**。`measureDeltaSeconds()` 的 dt 只用來決定「這一幀要不要 tick」（loop 心跳閘），**絕不餵進 springDamp 當步長**（餵了會偏離 parity）。
2. **水管 — 回寫**：crossfader 的 blended 值不再只寫 LiveParams side-map，而是經**已建的** `buildBlendTowardsVariationCommand` 套用回 graph override（或 P1 先窄到一個參數證明通路）。解掉 `fork-crossfader-direct-apply`。
3. **UI — 一根推桿**：toolbar 或新 Variation 窗畫一根 crossfader 推桿（對 TiXL BlendActions UI；0-127 fader 慣例）。拖它 → `updateFader(midiValue)`。
4. **觸發**：拖推桿（eye-hand `drag` 可驗）。
5. **驗（自驗）**：①推桿拖到一端 → 目標參數的 graph override 值改變（side-effect，讀 override map）②clean.png before/after：推桿動 → 畫面參數即時變（仿本次 Amount 滑桿驗法，PNG 結構性差異）③golden：crossfader spring-damp 數值對 TiXL BlendActions.cs 常數（kSpringConstant 20 / kTimeStep 1/60 / kSettleVelocity 0.0005）。
6. **驗（柏為檢查點）**：推桿的 spring 滑順度「磨平 127 階 MIDI」的手感 —— 只有柏為能簽。

### P2 — Variation 完整面板
snapshot pool 格子（抓取/啟用/刪除）+ N-way weighted mix 接線 + 完整 2-way crossfader。對 TiXL VariationCanvas 佈局。引擎全綠（`variation_pool.h`/`variation_mix.h`/`variation_snapshot_actions.h`），P2 = 接線 + UI。

### P3 — MIDI/OSC → graph 綁定
缺 `registerIoLiveSources` app 層 hook（loopback + SourceRegistry 已建，grep 確認 hook 未實作）。+ MIDI learn UI（點參數→動控制器→綁定）。對 TiXL MIDI 綁定。

### P4 — snapshot 觸發 + 硬體回饋
硬體鍵位觸發 snapshot activate + LED feedback 輸出線 + 鍵位映射 UI。依賴 P2（snapshot）+ P3（MIDI 雙向）。

### P5 — BPM 自動驅動 transport
DetectBpm 引擎已綠但孤兒（`fork-bpm-not-live-driving-transport`）。接 detect→transport.bpm + UI 顯示。

### P6 — Player / 演出輸出模式
`--play` 全螢幕 blit 輸出（modes.md [core]）+ Focus Mode/收 UI。確認 present path 不撞 frame_cook 心跳後可半並行。

---

## 並行 lane — 編輯視覺（S0 後開跑，按 Tier 選閘）

**Tier 1 純皮（`ui/` only，自由並行，eye-hand 截圖閘）**
- 節點右鍵選單（command 現成：Duplicate=copy+paste、Delete=現有鍵盤路徑包成選單）
- 邊界節點凸角五邊形（`node_draw.cpp:174` drawBoundaryDef 改 AddConvexPolyFilled）
- 連線箭頭三角（自繪 overlay）、Undo/Redo 顯示標題（CommandStack 補 peek*Title）、工具列範式、必填紅指示（靠 S0 required + 重用 blinkValue）

**Tier 2 碰 command（`ui/`+command，並行但顧 undo round-trip）**
- inspector 參數重置（SetOverrideCommand erase 路徑已現成）+ default/override 字色 + jog-dial（SliderFloat→DragFloat）
- 連線 hover-to-split（插節點 = AddWire/AddChild command + 命中測試）

**Tier 3 碰 cook（`ui/`+`runtime/`，與 cook-core 協調）**
- 節點本體即時值字串（讀 `sw::effectiveInput`）+ 縮放 gating（scale<0.25 隱 label/<0.4 隱值）
- 節點縮圖預覽（取 child resident 輸出貼圖→ImGui::Image）—— **柏為下注：做。與 view⊥graph 共存**（Output 窗=大預覽 / 節點本體=小預覽，TiXL 兩者皆有）
- 加節點面板 scatter 搜尋 + 命名空間樹（靠 S0 category）

---

## 驗收 — 每日檢查點（柏為 2026-06-24 定，非阻塞）

- **loop 絕不為檢查點停。** 持續自走選批。
- **每片完成：orchestrator 即時自驗**（第一道即時閘）= eye-hand side-effect / before-after diff / **對 TiXL 源碼+截圖客觀比對**。證據存 `artifacts/`。能客觀化的全自驗掉。
- **每天攢一份檢查點包**（`artifacts/checkpoints/YYYY-MM-DD/`）：當天完成、需手感驗的項 + 證據截圖對比 + 對 TiXL 截圖並排。**一天最多一份**（柏為定）。
- **★ 冪等守門機制（壓測 BLOCK 修正 — 否則「每天一次」只是願望）**：loop 自走、柏為全程 ABSENT（[[never-infer-baiwei-presence-from-loop]]），不能靠人觸發。落地規格 = **每批結尾，orchestrator `stat artifacts/checkpoints/$(date +%F)/`：不存在才產出今日包、存在則只 append 條目不另開**（date-stamp 目錄即天然冪等鎖，無需 cron）。**此機制必須寫進 `.claude/commands/sw-batch.md` 的迴圈收尾步驟才算落地**（現 grep checkpoint=0，目錄 `artifacts/checkpoints/` 未建）→ 這是交付新 loop 前的前置 todo（見 Next）。
- **柏為驗不驗、何時驗，不阻塞 loop。** 手感回饋進下一輪修，不卡當前。
- 自驗=即時閘、柏為=滯後低頻閘 → **把手感主觀項壓到最短**（磁吸力道/jog 速度曲線/crossfader 滑順度/MIDI LED 這類本質只有柏為能簽的，明確標進柏為 checklist；其餘全客觀化）。

---

## 風險（壓測登記）

1. **「1:1 + 低頻驗收」= 水平分層風險**：視覺/手感方向早期歪、累積到很晚才發現。緩解=每片對源碼/截圖即時比對自驗 + 每日檢查點累積（不堆到最後）。
2. **MagGraph 黑洞**：[core] 最大，自身內部依賴鏈長（座標模型→命中→連線退化三角）+ 碰 .swproj 序列化。獨立施工圖、獨立 owner-lock，不讓它吃並行頻寬。手感項最多，柏為 checklist 主要來源。
3. **驗證閘錯配**：orchestrator 拿 golden 套 UI 會卡 / 拿 eye-hand 放行碰 cook 的改動會漏。靠衝突矩陣的「驗證閘」欄強制分流。

---

## Plan Inventory（本檔定位）
- 本檔 = **體驗軸 sub-plan**（lane 規格 + 排程 + 驗收）。**唯一 dashboard 仍是 MASTER_PLAN。**
- 規格肉 SSOT：`alignment/ui-surface.md`（編輯 gap）+ `alignment/modes.md`（Player/Focus）+ `alignment/missing-subsystems.md`（Gradient/Audio/Timeline）。
- 對齊基準：`external/tixl/Editor/Gui` 源碼 + `artifacts/sw_tixl_*.png` 截圖。

## Next（第一批可動工）

**交付新 loop 前的前置 todo（壓測補，否則檢查點/避撞失效）：**
0a. `.claude/commands/sw-batch.md` 迴圈收尾加**檢查點冪等守門**（`stat artifacts/checkpoints/$(date +%F)/` 不存在才產出）+ 建 `artifacts/checkpoints/` 目錄。
0b. sw-batch orchestrator 選批改讀本檔**「主檔」欄**避撞（不是讀 Tier）—— 每個主檔同時只派一條 lane。

**第一批 ✅ 完成（2026-06-24）：**
- **S0 ✅**（`ae90f61`）：graph.h `category`+`required` 兩 defaulted 欄位，各加在 struct 最後 member（避過 PortSpec **與 NodeSpec 兩處** aggregate-init 地雷）。NO-BITE==clean base。**schema freeze 已解凍。**
- **P1 ✅**（`4108ae9`）：Variation crossfader→graph 活管接通（`variation_live.{h,cpp}` + frame_cook tick + toolbar fader）。FIXED 1/60 golden + eye-hand 親驗（拖 fader→ring 變稀疏 4540px）。**演出脊椎樣板片立下，P2-P5 照抄。** 柏為手感簽收=spring 滑順度（非阻塞）。
- **Tier1 連線箭頭 ✅**（`1c74d53`，in `node_draw.cpp`+`editor_ui.cpp`）：對 MagGraphCanvas.DrawConnection.cs，eye-hand 親驗紅/青箭頭 discriminating。

**第二批 ✅ 完成（2026-06-24 16:40，驗 S0 兩欄位 payoff）：**
- **L-B ✅**（`c8f5f50`）：Cmd+F quick-add palette scatter-match + relevance（對 `SymbolFilter.cs`）。驗 S0 `category`（match+subtitle）。**DEFERRED 命名空間樹**=需 category repo-wide fill。
- **L-G ✅**（`d04b5f4`）：必填輸入磁紅下三角（對 `MagGraphCanvas.DrawNode.cs` StatusAttention #CB1371）。驗 S0 `required`。
- **★S0 兩欄位 payoff 雙證活**=schema 鍵石價值兌現。

**第三批 ✅ 完成（2026-06-24 17:42，cook-core 脊椎 ∥ UI lane）：**
- **P2 ✅**（`cc8b331`）：完整 Variation 面板（`ui/variation_panel.{cpp,h}` 新窗 + pool/N-way mix/crossfader），對 TiXL VariationCanvas。golden GREEN（--bite 432）。★crossfade pixel-diff 改 golden 釘（harness gap task_bf656ae9）。**演出脊椎第二片，P1+P2=Variation 演出面完整。**
- **Tier1-B ✅**（`f853634`）：節點右鍵選單對 `GraphContextMenu.cs` 12-item，無 hit-test touch。greyed 誠實（無 backing field）。
- **★harness gap（task_bf656ae9）**：eye-hand combo-popup 不入 map + node-selection flaky→UI lane 暫以 .scn+golden 為主閘，raw-hand pixel-diff 不可靠。

**第四批 ✅ 完成（2026-06-24 19:18，柏為 18:06 現身核可 harness 修）：**
- **P3 ✅**（`9c0c554`）：MIDI/OSC→graph 綁定（registerIoLiveSources hook+cook-side wire frame_cook tick+MIDI learn）。loopback golden CC64/127→0.503937 達 graph param。**演出脊椎第三片＝三條 live wire 第二條（Variation✅+MIDI✅+BPM 待）。**
- **inspector ✅**（`6a46bb6`）：ResetOverrideCommand（undoable）+TiXL 字色+DragFloat jog。
- **harness-fix ✅**（`b08bb24`+triage `3b9f670`）：gap-1 combo-popup 入 map + gap-2 selectnode（triage 修 bad-id-wipe bug + enqueueClick hover-settle）。**★限制誠實記：selectnode 在 scenario/compound_smoke context 可靠（=UI 驗證載入的 doc），raw 任意 doc 取決 childId==ed-id。**
- **★合流血證**：inspector∥P3 撞 inspector.cpp、P3∥harness 撞 hand.cpp→手動 merge 保兩邊+重驗。**多 lane 動同 UI/verify 檔要預期 conflict。**

**第五批（下次 /sw-batch 從這 pull，避撞看主檔欄）：**
- **P5 BPM→transport**（DetectBpm engine 已綠孤兒 `fork-bpm-not-live-driving-transport`→接 detect→transport.bpm+UI 顯示，cook-core-adjacent，solo focus）。對 TiXL BPM。**P4(snapshot 觸發+LED) 排後因需 P3 MIDI 雙向+LED 硬體=部分柏為域。**
- **∥ 編輯 UI lane**（擇一，主檔互斥）：即時值字串 in `node_draw.cpp`（Tier3 靠 effectiveInput）／工具列範式／hover-to-split in `editor_ui.cpp`（Tier2）。
- **∥ L-B-follow（category repo fill→命名空間樹）**：機械 pass 填 `NodeSpec.category` 跨 node_registry_*.cpp。可 solo。
- ★避撞硬化：`node_draw.cpp`/`editor_ui.cpp`/`inspector.cpp`/`hand.cpp`/`frame_cook.cpp` 是多 lane 共撞點，每批每檔只一 lane。P5(frame_cook/transport) 與動 frame_cook 的 lane 互斥。後續 P4/P6(Player 模式)。

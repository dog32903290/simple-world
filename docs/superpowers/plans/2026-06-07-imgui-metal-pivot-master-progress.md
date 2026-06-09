# simple_world Master Progress — imgui + Metal Pivot

> **這是 simple_world 目前唯一的進度進入點（single source of truth）。**
> 新 session 必須先讀這份。舊的
> `docs/superpowers/plans/2026-06-05-native-runtime-master-progress.md`（那份宣稱
> 自建 C++ native runtime「100/100」）已被本檔取代，只當歷史與底層知識來源，
> **不要再照它往「繼續蓋自建 graph runtime」的方向走。**

## Current Snapshot

- 日期：2026-06-09。分支：`codex/js-to-cpp-contract-migration`（內容已與新方向脫節）。最新：`39f2d98`（TiXL AudioReaction 對齊）。
- 階段：**step 0 殼 → step 1 粒子線 → step 2 照 TiXL re-arch → step 3 graph 資料模型 → B0 編輯器基本可操作層
  全部 ✅（柏為親手測過）→ B2「命令層/undo-redo」Phase 1 ✅ closed（柏為 7/7 驗收）。**
  北極星 = Mac 版 TiXL（柏為選 B，視覺也追）；完成定義 = 柏為親手測得到（見下〈完成定義〉）；照〈Roadmap〉順序走。
- **節點編輯命令層**：Phase 1（命令核心 + add/link/delete/move + Cmd+Z）= **closed，柏為 7/7**。
  Phase 2（reconnect / replace-on-input）= **實戰驗證通過**（值脊椎建鏈時 Sine→Speed 那一拖觸發 reconnect 自動取代舊線，已不再 parked）。Phase 3–5（插入節點 / 框選 / 複製貼上）= queued。
- **值脊椎（2026-06-08 ✅ 端到端打通）**：param = 帶預設的 Float input port（schema 合一，TiXL InputSlot 查證）；
  `evalFloat` 拉動求值器 + 5 值節點（Time/Const/Multiply/Sine/Remap）；接縫 main.cpp evalParam（連線→求值/否則常數，GPU buffer 流不碰）；
  Inspector 被驅動變灰 + 即時拖動。`--selftest-valuecook` RED→GREEN。**全自動驗證**：加 Time/Sine 節點 + 拖線建出
  `Time→Sine→Speed`，state.json 證實鏈接，粒子由 sin(time) 驅動脈動。柏為眼球驗收（看螢幕脈動）= 唯一剩項。
- **眼手 harness 擴充（2026-06-08 ✅）**：map.json 含 canvas pin/node 螢幕座標（`ed::CanvasToScreen`）+ popup menu 項 +
  `state.json`（graph+selection 機器可讀）。RED→GREEN 證過。**agent 現在能全自主驅動編輯器**（加節點/拖線/驗證/重啟 app）。
- **audio-ingest 已整合進主線（2026-06-08 merge）**：另一條 session（`claude/runtime-workflow-approach-i4BrT`）的
  audio ingest 引擎/fixture/selftest/replay/Bespoke poller 已 merge 進本線；衝突只在 main.cpp/CMakeLists（各加各的行，已解）。
  **現在單一工作線 = `codex/js-to-cpp-contract-migration`**。全 8 selftest 綠（graph/save/command/valuecook/hand/eye/flow/audioingest）。
- **（2026-06-09）runtime 合約與柏為 13 輪重對齊（時間/聲音/交互層）**：`MY_WORLD_RUNTIME_CONTRACT` 升 **v0.2**、
  `docs/runtime/CONTRACT_ALIGNMENT_LEDGER.md`(L1–L14) 落檔、TiXL 時間/動畫設計抽取已驗證（L8/L9/L10/L12 與 TiXL 同構）。
  **開工 lane = runtime 時間/交互層建造**（Transport 兩鐘 / scoreGraph 第五張圖 / automation / 值解析堆疊；見 Active Lane）。
  地基已**親證在跑**（eye 抓真 frame：Metal 粒子 + node canvas，68.8fps）。
- **（2026-06-09）audio 子線 → TiXL AudioReaction 完整對齊（commit `39f2d98`，柏為定「一模一樣」）**：
  ①裝置路由 bug 修復（選裝置收 0 blocks 真根因＝`AudioUnitSetProperty` 換 AVAudioEngine 輸入裝置會 desync tap → 改 **raw AUHAL** 輸入單元，接縫 AudioCapture 不變）；
  ②新 `runtime/spectrum_analyzer`（**vDSP** 2048-pt FFT → 32 log-octave band 55–15kHz + peaks/attacks/onsets，移植 TiXL `AudioAnalysisContext`）；
  ③新 `runtime/audio_reaction`（狀態 cook，移植 `AudioReaction.cs`：5 InputModes / window 加權 Sum / threshold+去抖 hit / 5 OutputModes / Reset）；
  ④AudioReaction 節點 = **3 輸出**(Level/WasHit/HitCount) + **10 參數**(InputBand/Output enum 下拉 + Reset checkbox)，`PortSpec` 加 widget/labels/pinless，**Inspector 渲染 combo/checkbox/slider**，`Node.outCache` 帶狀態節點輸出（main 每幀 cook→outCache，evalFloat 讀它），頻譜直方圖畫在節點臉。
  13 selftest 綠（+spectrum/audioreaction）、save/load 含新參數、inspector+節點 eye 驗、裝置路由 `--audio-capture-smoke` 驗（內建/2i2）。**逐項細節見 active plan `2026-06-09-runtime-time-interaction-build.md` 的「TiXL AudioReaction 完整對齊」段。**
- 與柏為長談後確立的核心判斷（四個皮選項都已逐一壓過、由他拍板）：
  - **核心資產 = 自己的 Metal 粒子/3D 引擎。皮是可換外殼。**
  - 放棄整批克隆 TiXL / Vuo runtime（兩者 runtime 都搬不動，證據見 Conflict Register）。
  - **下注：`imgui-node-editor`（C++）當皮 + 自建 Metal runtime。**

## 北極星（2026-06-07 柏為拍板，最高指導原則）

**最終成品 = Mac 版的 TiXL（Tooll3）。** 工程導向：系統性地把 TiXL 搬到 Mac/Metal，**不自創、不即興**。

- **TiXL 是唯一設計權威。** 任何不確定（節點語意 / port / 參數 / 運動 / 型別 / 失敗行為 / 圖的邏輯）→
  **直接照 TiXL 邏輯**，依序查：
  1. 本地 `external/tixl/`（**完整 C# 源碼** + `.help/docs/operators` 官方文件）— 離線、最快、含實際 compute 邏輯。
  2. `external/tixl-spec/`（935 節點報告書，已結構化）。
  3. 線上 `https://github.com/tixl3d/tixl.git`（branch `dev`）— 最新、補本地缺漏。
- **「Mac 版 TiXL」的精確定義 = 功能/邏輯/節點體系/體驗對等，技術棧重寫。**
  邏輯、port、語意、節點分類照搬 TiXL；**實作換 metal-cpp**（TiXL 的 DX11/HLSL/C# runtime 焊死 Windows、搬不動）。
  **不是**逐行翻譯 C#。皮維持 imgui-node-editor（TiXL 本身就是 ImGui 寫的，`external/tixl/ImguiWindows`，手感同源）。
- **視覺也追 TiXL（柏為 2026-06-07 選 B，「我喜歡 TiXL 的邏輯」）：** editor 版面 / 操作手感 / 節點外觀 / 配色盡量 1:1 像 TiXL。
  可行性高——TiXL editor 是 ImGui-based（`external/tixl/Editor`、`ImguiWindows`、`ScalableCanvas` 等），節點繪製 /
  canvas 縮放平移 / 連線手感 / 配色程式碼可直接讀照搬到 imgui-node-editor；焊死的只有 DX11 render backend + WinForms 外殼。
  工具：`tooll3-interaction-compatibility` skill。**順序**：UI 視覺皮要附著在能動的 runtime 上才驗得了
  （`ui-skin-pressure-gate`），故 **runtime 母線先行、UI 隨之向 TiXL 收斂**，不先停 runtime 去做空皮。
- **節點工作流（每個節點都照此）**：先讀 TiXL 對應 `.cs` + `.help/docs` 的 port/語意/行為 → 照搬成我們的
  node 契約（`node-contract-architect`）→ metal-cpp/MSL 重實作 → selftest 掛 TiXL 行為當 golden。不確定問 TiXL，不猜。

## 完成定義（2026-06-07 柏為拍板，凌駕一切）

**一件事「完成」= 柏為能在介面親手按到 / 操作 / 確認它 work，不是 selftest 綠。**
selftest 綠是必要不充分。每個功能都要有「non-technical handle」（按鈕 / 選單 / 可操作的節點），
否則對柏為等於沒做（例：save roundtrip 綠但他按不到儲存 = 未完成）。違反這條的「完成」一律退回。

## Roadmap（工作順序，2026-06-07 與柏為定；解「怕漏 / 怕即興」）

**規則**：照此順序走，不再每次即興丟 a/b/c。每做完標 ✅、指下一項。柏為是導演可隨時插隊；插隊不讓別項
消失（回 `queued`）。**完成 = 柏為親手測得到**（見上）。本清單是「未來順序」，搭配下方〈進度〉的「已發生」。

**順序（柏為定）：基本可操作編輯器層先 → runtime fan-out → 視覺/進階。** 因為沒有可操作層，runtime 做再多柏為都碰不到。

- **B0 基本可操作層（✅ v1 裝好，待柏為親手測 = 真完成）**
  - ✅ 浮動 Toolbar：**Save / Load** 按鈕（寫/讀 `~/Desktop/simple_world_project.json`，status 列回報結果）
    + **Add Node** 按鈕（popup 列 registry 全型別，未來 fan-out 的節點自動出現）。拖曳位置每幀 sync 回 graph，Save 收得到。
  - ✅ 連線（拖 pin→pin，驗一進一出不同節點）/ 刪連線 / 刪節點（選+Delete，連帶清 dangling 連線）。
  - 踩雷修正：`ed::EndCreate()`/`EndDelete()` 必須在 `if(ed::BeginCreate/Delete())` **內**（Begin 回 false 時不設
    m_InActive，在外無條件呼叫 End→assert 崩）。`ed::Begin` 回傳 void、要用 host window 的 ImGui::Begin bool 守。
  - ⬜（park）連線真的驅動任意拓撲求值 → 等 B2 通用 evaluator；現在 cook 仍是「按 type 找節點套參數」，
    所以加/連節點能畫能存，但「連線改變求值」還沒（誠實標記，柏為測時會發現加的力還沒作用）。
- **A runtime fan-out（B0 後，每個節點自動能從選單叫出 + 存檔）**：其餘 16 力 / 點生成 / 點操作 / 渲染 / mesh / image / audio
- **B2 編輯器進階**：
  - ✅ **undo/redo command 層 = Phase 1 closed（2026-06-08，柏為 7/7 驗收）**：Command/CommandStack/MacroCommand
    在 `app/`；add/link/delete/move 全走命令；Cmd+Z/Cmd+Shift+Z（注意 macOS Cmd 落在 `io.KeyCtrl`，見踩雷）。
  - ⏸ **reconnect（replace-on-input）= Phase 2 parked**：碼+selftest 完成，GUI 驗收待節點變多再做。
  - ⬜ queued：Phase 3 插入節點（TiXL 線上浮圓點）/ Phase 4 框選多選刪移 / Phase 5 複製貼上；compound 群組、通用 topological evaluator、型別/port 檢查。
- **C 視覺（北極星 B）**：TiXL 配色/節點外觀/canvas 手感、時間軸+keyframe、中文字型
- **D 基建（持續）**：效能（非阻塞/PSO 快取）、檔案格式版本

## 語言/後端（2026-06-07 拍板，已壓過，不要重議）

**C++ + metal-cpp。** 柏為拍板「Metal 是必須的、必須親手調底層 GPU」+ 先前拍板「皮 =
imgui-node-editor」。兩個約束的交集只剩 C++：imgui-node-editor 是 C++ 一等公民、metal-cpp
是 Apple 官方 C++ Metal binding（連 .mm 都幾乎不用寫）。已逐一壓掉 Swift（要反向 bridge C++
node editor）、Rust（wgpu 是 WebGPU 抽象非親手 Metal + 對柏為崖最陡）、C#（TiXL 複用是
DX11/WinForms 焊死的陷阱）、Web/TS（wgpu→Metal 非親手 + 丟 imgui-node-editor 手感）。
柏為的戰場 = MSL shader（≈他在學的 GLSL）+ 節點接線；C++/build/Metal 骨架由 Claude 扛。

## 環境前提（試壓過）

- `clang` 21 ✅ / `cmake` 4.3.2 ✅ / `git` ✅。
- **`metal` shader compiler ✅ 就緒**（2026-06-07）：完整 Xcode + `xcodebuild
  -downloadComponent MetalToolchain` 都裝好，`metal` v32023.883。注意：裝 Xcode ≠ 有
  metal compiler，Metal Toolchain 是 Xcode 16+ 的可選下載元件，要另跑 downloadComponent。
- metal-cpp headers 來源：`/tmp/metal-cpp-cmake/metal-cmake/metal-cpp/`（LeeTeng2001 vendored）。
  之後整進專案 `app/third_party/`。

## Active Lane

**runtime 時間/交互層建造（2026-06-09 柏為拍板開工）** = `plans/2026-06-09-runtime-time-interaction-build.md`。
設計來源 = `docs/runtime/CONTRACT_ALIGNMENT_LEDGER.md`(L1–L14) + `MY_WORLD_RUNTIME_CONTRACT.md` **v0.2**
（2026-06-09 13 輪對齊重訂：Transport 兩鐘、scoreGraph 第五張圖、值解析堆疊+arm、override 照 Ableton、
驗收三層、Vuo 捨棄、audio 兩世界+延遲閘門）。
- **11 段脊椎、4 個體感關卡(▣)。2026-06-09 壓測重排 = sound-first**：S0 預清 header 雷 → S1=**解析模型（來源註冊+binding/override，拱心石）** → S2=**▣ 聲音→參數**。S0/S1/S2 已詳到可執行。
- 節律 = **牽繩 B**：水管工夜裡自主(`/goal "段 selftest 綠"`) + 每段 selftest 派 subagent 對抗審查、▣ 體感邊界停給柏為摸。
- 4 個早上：**M1 聲音→參數**（你彈/放聲音、粒子跟著動；零 transport、夜1=S0–S2）→ **M2** automation 播放+scrub(夜2=S3–S6) →
  **M3** 錄製+override 黏放(夜3=S7–S8) → **M4** 兩個聲音世界(夜4=S9–S11,批)。
- 接在「值脊椎」後面（已端到端打通）；值脊椎的連線驅動 = 解析模型的 `binding=connection`，零改自動進模型。Transport 退到 S5（automation 才需要 playhead）。

> 上一個 lane（值脊椎）= ✅ 端到端打通；唯一懸著=柏為眼球確認粒子脈動（機器證據已齊）。
> 其餘 queued（照 Roadmap，本 lane 後）：A runtime fan-out / Phase 3 插入節點 / Phase 4 框選 / C 視覺向 TiXL 收斂。

> step 0→3 的起手細節（clone starter、第一條粒子線）已歸檔到 `2026-06-08-progress-history.md`（grep 用）。

## 進度（逐條史已歸檔）

> 逐條歷史（step0→今天，含各 RED→GREEN 證據/檔案/踩雷）移到 `2026-06-08-progress-history.md`（grep 用，非必讀）。當前狀態見上方〈Current Snapshot〉。

## 工作方式（2026-06-07 定案 = gemini research〔標準 Metal 工程實踐〕+ 舊契約資產 + 現有進度，三者收斂）

**核心切線：契約層「順序鎖」、葉子層「並行 fan-out」。** 並行的單位不是「節點 vs 節點」，是「契約定義層 vs 葉子實作層」。

- **必須順序（Opus 自己做、禁 fan-out）= 契約層**：定義 host/device binding slots、PSO/pipeline descriptor、
  memory sync、write offsets、共享 struct 的東西。改這些就是動承重結構。
- **可並行 fan-out（sub-agent）= 葉子層**：封裝在單一執行單元的東西——一個 MSL 數學/物理函式、一個只讀寫
  「已配置好的 buffer slot」的 node widget、一個 offscreen 驗證 harness。

**兩階段（順序不可反）：**
1. **鎖契約骨幹（順序，現在）**：把舊契約規格翻成 C++ 共同地基——`SharedTypes.h`(Particle/PointBuffer 型別)
   + `BufferIndices` enum(binding slots 單一來源) + `EvaluationContext`(= Cook Contract + FrameScheduler 的
   frameIndex/time/deltaTime) + resource ownership 規則。**這層鎖死、agent 禁改（除非專屬指令）。**
2. **葉子 fan-out（並行，骨幹定後）**：每個節點 = 一個 agent + 它的 selftest，讀寫已配置 slot，互不踩。

**驗證三模式（人不逐步人工審）：** A `compute readback assertion`（固定 seed→readback buffer→assert 物理/數學
定律，= 已做的 `--selftest-radial`）；B `offscreen golden frame`（render→getBytes→MSE vs golden，設容差）；
C `Metal validation layer`（`MTL_DEBUG_LAYER`/`MTL_SHADER_VALIDATION`，已開）。

**回報顆粒度：** 一個「會動、能 demo」的切片做完才回報柏為（畫面 + 全綠報告 + 本檔更新）；中間逐線 selftest
由 Opus 自己爬，不要他陪看中間態。

**並行會死在哪（全源自「共享契約沒先鎖」）：** struct padding 分歧、vertex descriptor 不同步、render pass
load/store 覆蓋、語義合併衝突（git 行比對不報、memory layout 靜默壞）。→ **階段一鎖契約是不可跳的前提。**

## Spine（皮選項的最終判決，已壓過，不要重議）

| 選項 | 判決 |
| --- | --- |
| `imgui-node-editor` + 自建 Metal runtime | ✅ **走這個**。跨平台、直接 Metal、一個調度者、無 event model；TiXL 本身就是 ImGui(326 檔)，手感免費繼承，借 TiXL editor 的 .cs(如 `ScalableCanvas.cs`)當互動設計參考而非克隆。 |
| 克隆 TiXL editor(C#) | ❌ 假路：567 檔中 453 焊死 T3.Core + SharpDX(DX11) 28 + WinForms 12 + net*-windows。 |
| 魔改 Vuo GL→Metal | ❌ GL 焊進 VuoImage 型別系統，等於重寫 Vuo 一半。 |
| 兩個調度者共用 Vuo 畫布 | ❌ 要開膛 Vuo Editor，繼承雙倍複雜度，最貴。 |
| Vuo 當皮(粒子寫成 Vuo node) | ⚠️ 次選，僅當「大量用 Vuo 的 image/audio 生態」才考慮；要吃 event model + Metal↔GL 橋接。 |
| 自己從零做編輯器 | ⚠️ 幾個月全職前端，柏為不寫程式，最後才考慮。 |

## 第一批節點（柏為實際要的，重心是 GPU 粒子，不是 image）

- **核心(必自建 Metal)**：RadialPoints / LinePoints / RandomizePoints / FilterPoints /
  TransformPoints / RepeatAtPoints / MapPointAttributes / LinearSamplePointAttributes /
  CombineBuffers / DrawPoints / DrawLines / **DrawMeshAtPoints** / Raymarch Field。
- 周邊(Vuo 有、可晚點借資料)：mesh/camera/image filter/audio/輸出。
- 全清單見柏為訊息與 [[simple-world-architecture-pivot]]。

## Plan Inventory

- 本檔：**現行 master progress，唯一 dashboard。**
- `specs/2026-06-08-node-editing-commands-design.md`：**節點編輯命令層 spec**（五階段：命令層→reconnect→插入→框選→複製貼上）。active 設計契約。
- `specs/2026-06-08-value-spine-design.md`：**值脊椎 spec（current active lane 的設計契約）**：param=帶預設的 Float input port（schema 合一，TiXL InputSlot 已查證）、5 承重柱、5 起手節點（Time/Const/Multiply/Sine/Remap）、重用 Phase1/2 對照、接縫 main.cpp:322-325。schema 已釘死。
- `2026-06-08-value-spine.md`：**值脊椎實作計畫（✅ closed，端到端打通）**：5 task bite-sized TDD（schema 合一→求值引擎→接縫→UI→眼手驗），標重用 Phase1/2 vs 新碼。本 lane 接在它後面。
- **`2026-06-09-runtime-time-interaction-build.md`：runtime 時間/交互層建造計畫（= current active lane）。** 11 段脊椎（S1 詳到可執行、S2–S11 條目）、4 個體感關卡、夜→晨節律、每段血緣（搬/擴充/全新）+ TiXL 源。設計來源 = 下方 CONTRACT_ALIGNMENT_LEDGER。
- **`docs/runtime/CONTRACT_ALIGNMENT_LEDGER.md`：時間/聲音/交互層對齊底稿（L1–L14，active lane 的設計契約）。** 2026-06-09 與柏為 13 輪壓測合約含混處的逐項鎖定 + 停車 + Decision Log；TiXL 抽取證據。改約/實作前先讀。
- `2026-06-08-node-editing-commands-phase1.md`：**Phase 1 實作計畫 = closed**（命令層+undo/redo，柏為 7/7 驗收，checkbox 已勾）。closure evidence。
- `2026-06-08-node-editing-commands-phase2.md`：**Phase 2 實作計畫 = parked**（reconnect；頂部有 ⏸ 狀態 banner 說明擱置原因 + 待釐清方向）。
- `2026-06-08-architecture-constitution.md` / `specs/2026-06-08-architecture-constitution-design.md`：架構憲法（五區/7 鐵律）= `ARCHITECTURE.md` 的來源。
- `2026-06-07-reusable-resources.md`：**可用資源清單**（不重造的輪子：starter、引擎輪子、
  求值核心、設計藍圖、LLM/metal-cpp 陷阱 checklist、待驗清單）。source material。
- `2026-06-05-native-runtime-master-progress.md`：**歷史（JS/Python 實作不搬）**，但它指向的契約資產**不是**廢的（見下）。
- **`docs/runtime/*_CONTRACT.md`（19 個）+ `tests/*_contract.test.js`（21 個）= 可複用契約資產（2026-06-07 修正判定）。**
  尤其 `MY_WORLD_RUNTIME_CONTRACT.md`（總綱）的**規格層是語言中立的引擎設計法**：Runtime Type System
  （含 `PointBuffer`）、Cook Contract（7 求值階段）、FrameScheduler/Main Clock（frameIndex/time/deltaTime
  = 柏為說的 timeline）、Node/Port/Resource/Failure/Command Contract、Four Graphs（editorGraph/runtimeGraph/
  commandGraph）、`P1: Point Buffer Shell` fixture（= 現行粒子線的設計藍圖）。**新方向不重新發明這些規格，
  讀契約當 C++ 設計法 + 把 golden 驗收案例移植成 C++ selftest。** JS/Python 實作與 Vuo body-layer/proof-gate 不搬。
  **契約盤點完成（2026-06-07）。Top 5 NOW（現行粒子線該立刻複用當驗收標準）：**
  1. `MY_WORLD_RUNTIME_CONTRACT`（總綱，唯一純 SPEC）— `PointBuffer` 型別 / Cook 7 階求值 / `P1` 設計藍圖 /
     `EvaluationContext` 別重新發明。
  2. `FRAME_SCHEDULER` — TransformPoints 要 time/deltaTime；golden：一幀一個 context、`previousFrame=[null,0,1]`、
     非法 clockOwner→報錯 exit 1。
  3. `RESOURCE_LIFETIME` — buffer/texture ownership + realloc 時舊 view 失效；golden：`["allocate","reuse","reallocate",200]`。
  4. `COMMAND_STREAM`（只取 dispatch 數學）— `calcDispatchCount` 真值表 `64,16→5 / 63,16→4 / 960×540,16→61×34`。
  5. `TEXTURE_VIEW` — DrawPoints 畫進 texture 要 RTV/SRV；`createTextureView` 真值表擋「buffer 看似有效但沒綁對 view」的假成功。
  其餘契約多為 MIXED（規格/gate 邏輯可移植，Vuo 組合 / `.py` shell / `.graph.json` / DX11 名詞不搬）；4 個 IMPL/SKIP 綁死 Vuo/Python。
  **最有移植價值的不是契約散文，是 `tests/*_contract.test.js` 裡那幾組行為斷言（真值表 / dispatch 數學 /
  scheduler previousFrame 序列 / lifetime allocate-reuse-reallocate 序列）= 直接改寫成 C++ selftest 的 golden 素材。**
- `external/tixl-spec/TIXL_CLONE_SPEC_20260604/`：柏為的節點報告書（935 specs/915 C# classes），當**設計藍圖**，非程式碼來源。
- `docs/tixl-porting/`：TiXL→Vuo porting 的歷史批次，旁支。
- 354 個 `vuo-nodes/*.c`：柏為說「在錯誤時間做的」、runtime 不承重的**廢旁支**，不要再往上加東西。

## Conflict Register

- 舊 master plan 宣稱 100/100 ↔ 實際沒有能打開的 app。解決：本檔取代它；100/100 只是 headless 證明。
- **（2026-06-07，柏為提醒觸發）本檔前一版把舊 native-runtime 的「graph/command/runtimeGraph 層」整批判成
  「新方向用不到」= 判太狠的錯。** 把「TiXL/Vuo **runtime 實作**搬不動」過度推廣成「**契約規格**也用不到」。
  解決：區分**契約規格（語言中立，可複用，是資產）vs JS/Vuo 實作（不搬，是負債）**。`docs/runtime/*_CONTRACT.md`
  改判為可複用契約資產（見 Plan Inventory）。剩餘風險：契約裡夾雜的 Vuo/`.py`/`.graph.json` 細節要逐契約篩
  （SPEC/MIXED/IMPL），background agent 盤點中。**Session Safety 那條「不要照舊 100/100 蓋 graph/command/UI」
  仍成立**——指的是不要重蓋舊**實作**，不是不准讀契約規格。
- 「克隆九百多個 TiXL 節點」是幻覺：TiXL 是 net10.0-windows、SharpDX 177/DX11 133 散進節點本體無抽象層；節點主體是 915 個 C#(.cs 2464)，shader 只 424 且半數是 compute(218)+RWStructuredBuffer(182)，且常是 template 靠 C# runtime 填洞（repo 內 `tixl_mesh_draw_hlsl_to_msl_verdict` 已 reject 機械翻譯）。→ 不克隆，借報告書當藍圖。
- SPIRV-Cross 直接 HLSL→MSL：只解 pixel/vertex shader 那一小塊，不解 C# 節點語意與 DX11 runtime，不是主線解法。
- **（2026-06-07）北極星「Mac 版 TiXL」vs Spine「克隆 TiXL editor ❌ 假路」表面衝突，實則一致。** 澄清：
  北極星要的是**功能/邏輯/體驗對等**（imgui-node-editor 皮 + metal runtime **重現** TiXL），**不是**逐行翻 TiXL 的
  C#/DX11/WinForms 實作（那條焊死、已否決）。Spine 否決的是「克隆實作」；北極星要的是「照邏輯重寫」。兩者同一個方向。

## Session Safety

- **（2026-06-08 已解決）一度跑出兩條平行線**：`codex/js-to-cpp-contract-migration`（值脊椎/節點編輯/harness，本線）
  與 `claude/runtime-workflow-approach-i4BrT`（audio-ingest）從 `a54b8c0` 分岔。柏為裁示「只在一條線工作」→ 已把
  audio-ingest **merge 進本線**（commit `a0359fe`，衝突只在 main.cpp/CMakeLists、各加各行）。**現在唯一工作線 = `codex/js-to-cpp-contract-migration`。**
  claude branch 自此視為歷史，不要再往它加東西、也不要再分新線；要分支前先問柏為。
- 不要照舊 100/100 plan 繼續蓋自建 graph/command/runtimeGraph/UI。
- 不要往 354 vuo-nodes 加東西。
- 工作樹有 `AGENTS.md`、`skills/tixl-vuo-node-port/SKILL.md` 被外部改動，別亂動。
- 動手前先跟柏為確認皮的選擇仍是 imgui-node-editor。

## Next Handoff Sentence

新 session：先讀 memory `[[simple-world-northstar-and-method]]`（★命脈）+ 本檔。
**北極星 = Mac 版 TiXL（柏為選 B，視覺也追，照 TiXL，不確定查 `external/tixl` 源碼，不自創）。
完成定義 = 柏為能親手按到/操作/確認 work，不是 selftest 綠。照〈Roadmap〉順序走、可插隊、不漏。** 方向別重議。

**現況（2026-06-08）：step 0→3 + B0 + 命令層 Phase 1 + 值脊椎 全 ✅；audio-ingest 已 merge；單一線 = `codex/js-to-cpp-contract-migration`（已 push）。全 8 selftest 綠。**
眼手 harness 可全自主驅動編輯器（加節點/拖線/驗證/重啟 app）——驗收不必再靠柏為的手。
**第一個動作 = 跟柏為確認他要挑哪個下一 lane**（見〈Active Lane〉queued：A runtime fan-out / 聲音驅動粒子 / Phase 3 插入節點 / C 視覺）。
唯一懸著的驗收：柏為眼球看粒子脈動（機器證據已齊）。**先問柏為，別自己挑 lane 開幹。**

**文件導讀（省 token）：開場只讀 memory index + 本檔（薄 dashboard）。逐條史在 `2026-06-08-progress-history.md`、
細節在各 spec/plan/contract/個別 memory——要時 grep 關鍵字再讀那段，不要每次全載。RAG 對這規模是殺雞牛刀（見柏為 2026-06-08 討論）。**

寫碼紀律（已就位、沿用）：`metal-cpp-discipline`（共享 header/對齊/AutoreleasePool/ownership）；丟 shader 的 struct
要照 TiXL 64B layout 必用 **packed_float3**（host 3-float struct）；`app/run-dev.sh` 開 validation layer + ASan/UBSan；
GPU 輸出用 `codex-eyes` offscreen readback 驗、**每根線先注 bug 證眼睛**；契約層順序鎖、葉子並行 fan-out。
踩雷清單見 memory northstar 那條（imgui 1.91.8 / MRC -fno-objc-arc / SwPoint 撞 Carbon / ed::EndCreate 在 if 內）。

**不是**接舊 JS/Python runtime；舊 JS 契約**規格**可複用（語言中立）、實作不搬；**不照搬 TiXL Symbol schema**（graph 用自己 native 版）。

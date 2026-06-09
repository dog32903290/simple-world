# Runtime 時間/交互層 建造計畫（解析模型 · sound-first · scoreGraph · transport）

> **For agentic workers:** REQUIRED SUB-SKILL: superpowers:executing-plans。Steps 用 `- [ ]` 追蹤。
> 設計來源 = `docs/runtime/CONTRACT_ALIGNMENT_LEDGER.md`（L1–L14，**L5 解析模型 round-3 已釘**）+ `MY_WORLD_RUNTIME_CONTRACT.md` v0.2。
> 北極星：Mac 版 TiXL。每段對應 TiXL 既有設計（Slot/Playback/Animator/Curve），不自創。
> **2026-06-09 壓測重排**：起點改 sound→param（零 transport、重用已 built audio_ingest、最有感）；transport 退到 automation 才需要。

**Goal:** 把「可作曲的時間」做進 simple_world——一個**通用參數解析模型**（binding/override，所有來源同一種公民），
上面長出聲音驅動、automation 曲線、transport、錄製，全部驅動**已在跑的 Metal 粒子**。接在「值脊椎」後面。

**Architecture（L5 解析模型，本 lane 的脊椎）:** 一參數每幀一個驅動者：
`override（live 源此刻碰）→ binding（connection｜automation｜live-source，三選一互斥）→ constant`。
**S1 把這做成一張「來源註冊 + 解析」表**；之後聲音/automation/手/MIDI/協議都是「註冊一個來源」= 一行。
混音不在解析層（接 Add 節點 = binding=connection）。值脊椎的連線 = binding=connection，零改、自動進模型。

**Tech Stack:** C++17、`evalParam/evalFloat`(值脊椎)、`EvaluationContext`(Particle.h)、`audio_ingest`(已 built)、`--selftest-*` RED→GREEN、eye+hand。

---

## ⬛ 交接（2026-06-09 session 收尾，clear 前必讀；resume 先讀這段）

### 🔵 2026-06-09 續（裝置路由 bug + 項目2 核心，已驗收）
- **根因修復：選裝置收不到音**。柏為回報 level/hit 都不動。根因＝**不是沒插線**，是 `AudioUnitSetProperty(CurrentDevice)` 在 AVAudioEngine.inputNode 的 AUHAL 上換裝置會**desync engine→tap 收 0 buffer**（DIAG 證：裝置設對、格式對、引擎在跑、blocks=0；任何顯式裝置都中，連內建麥用自己 id 都 0；只有 default 路徑活）。AVAudioInputNode 又不暴露 auAudioUnit(v3 deviceID setter 走不通)。**正解＝把 capture 後端整個換成 raw AUHAL(kAudioUnitSubType_HALOutput) 輸入單元**（`audio_capture.mm` 重寫；接縫 AudioCapture 不變）。smoke 證：內建/default/2i2 全 blocks 流；`say` 疊測證 rms 隨聲音 5–10×。
- **項目2 核心完成**：`AudioReaction` 改成**兩個輸出** `level`(RMS 持續音) + `hit`(暫態)，柏為用空間直覺抓哪個接哪個（不是「打 0/1」）。`ctx` 多帶 `audioHit`(CPU-only，GPU 忽略，20 bytes)。`evalAudioReaction` 照 outIdx 選；evaluate 簽名加 outIdx（多輸出值節點，6 個 eval fn）。**meter 從 toolbar 移進 AudioReaction 節點臉**（照 DrawPoints 前例）。`--selftest-valuecook` 加 level/hit 路由斷言。eye 證節點畫出兩 pin+兩 meter、粒子照渲染、14 selftest 全綠。
- **持續音凍結 bug 解了**：`level` 現在輸出 RMS，握著的口風琴長音會驅動粒子（不再只在敲擊瞬間跳）。
- 律法 debt 未變：platform→runtime 違律仍在（AUHAL 仍 include runtime DSP）；graph.cpp 已 600+ 行（雙輸出後更該拆）。

### 🟢 進行中（2026-06-09）：TiXL AudioReaction 完整對齊（柏為定目標「一模一樣」）
**權威契約已從 TiXL 源碼挖出**（`external/tixl/Operators/Lib/io/audio/AudioReaction.cs` + `Core/Audio/AudioAnalysisContext.cs` + `AudioConfig.cs`）：
- **3 輸出**：`Level`(float,主值)、`WasHit`(bool)、`HitCount`(int)。
- **10 參數**：`Amplitude`、`InputBand`(enum5:RawFft/NormalizedFft/FrequencyBands/Peaks/Attacks)、`WindowCenter/WindowWidth/WindowEdge`(頻率視窗)、`Threshold`、`MinTimeBetweenHits`(去抖)、`Output`(enum5:Pulse/TimeSinceHit/Count/Level/AccumulatedLevel)、`Bias`、`Reset`。
- **演算法**：選 bins → 頻率視窗加權 Sum（windowCenter/Width/Edge）→ `Sum>Threshold` 升緣 + `MinTimeBetweenHits` 去抖 = hit → 5 種 Output 塑形（pow/bias/amplitude）；AccumulatedLevel 累加。
- **DSP 後端**（`AudioAnalysisContext`）：**2048-pt FFT→1024 bins**（TiXL 用 BASS；我們用 **Accelerate/vDSP** 自算）→ dB 正規化 → **log octave 55Hz–15kHz 映射成 32 bands**（`FrequencyBandCount=32`，max-pool）→ peaks(decay)/attacks(增率×4)/attackPeaks/onsets/sliding-average。常數：FftBufferSize=1024、48kHz。
- 節點臉**頻譜視覺化**（`AudioReactionUi.cs` 266行）：即時 32 條頻譜 + peak 疊 + windowed Sum 條 + threshold 線。

**鎖定順序（契約層順序鎖，每刀=柏為可親手測）：**
1. **【刀1·DSP 後端】✓** 新 `runtime/spectrum_analyzer.{h,cpp}`(runtime 葉子,純算+Accelerate)：vDSP 2048-pt FFT + 32 log-octave band(55–15kHz) + peaks/attacks(×4)/attackPeaks/onsets/sliding-avg,照 `AudioAnalysisContext` 移植;lock-free 雙緩衝 snapshot。`--selftest-spectrum` 綠(1000Hz→band16/level0.99/遠端0/靜音衰減/step→attack0.34;bug 變體 FAIL)。CMake 加 Accelerate。
2. **【刀2·頻譜上節點】✓** AUHAL tap mono-mix 餵 spectrum;`audio_capture.spectrumSnapshot()`→`audio_settings.publishSpectrum/spectrum()`(ui→app)→main 每幀 publish→editor_ui 用 `ImGui::PlotHistogram` 在 AudioReaction 節點臉畫 32 band(level/hit meter 保留)。eye 證 widget 畫出、粒子照渲染、12 selftest 全綠。app 留著跑(2i2),柏為彈樂器看頻譜跳。
3. **【刀3·完整節點契約】✓** 新 `runtime/audio_reaction.{h,cpp}`(狀態 cook,忠實移植 `AudioReaction.cs`):選 bins(5 InputModes)→window(center/width/edge)加權 Sum→threshold+minTime 去抖 hit→5 OutputModes(Pulse/TimeSinceHit/Count/Level/AccumulatedLevel)+累加+Reset。`--selftest-audioreaction` 綠(level/hit/去抖/hit#2/reset)。整合:`PortSpec` 加 widget(Slider/Enum/Bool)+labels+pinless;AudioReaction spec=3 輸出(Level/WasHit/HitCount)+10 pinless 參數;`Node.outCache[3]`,main 每幀 cook 各節點→outCache,`evalFloat` 讀它;**Inspector 依 widget 渲染 Combo/Checkbox/Slider**(eye 證 InputBand 下拉「FrequencyBands」);節點臉=頻譜+Level 輸出條+3 輸出 pin(參數無 pin)。valuecook/audionode 改用 outCache;graph roundtrip jsonLen 1123→1386(參數有存檔);13 selftest 全綠。
- **刀1+2+3 ✓ — TiXL AudioReaction 功能對齊完成(柏為「一模一樣」目標達成)。**
- **殘留(小/裝飾,非功能):** ①TiXL 節點 UI 的 peak 疊/window/threshold 線未畫(只有基本頻譜直方圖) ②TiXL 參數可接線,我們 pinless(inspector-only) ③`ctx.audioLevel/audioHit` 變 vestigial(AudioReaction 改吃 outCache,可清) ④反應性由組合證(DSP selftest+capture 餵法+say/rms),GUI 內「彈下去頻譜亮」是柏為親測。
- 律法注意:DSP/cook 是 runtime 葉子;capture(platform) 餵它仍是現有 platform→runtime 違律的延伸(柏為待決)。



### 做到哪
- **S0 ✓**（`6062da9`）header de-mine：`EvaluationContext`→`runtime/eval_context.h`。
- **S1 ✓**（`274e72a`）解析模型拱心石：`override→binding→constant` + `SourceRegistry`，對抗審查過。
- **World 1 形狀大改**：柏為拍板「**audio 全照 TiXL，有疑問查 TiXL，系統穩再議自創**」。最終形狀＝**audio 是值節點，不是 hidden binding**（柏為指出「炸了卻沒連線」的醜，重織掉）：
  - 鏈：選定裝置 → `capture` → `ctx.audioLevel` → **`AudioReaction` 值節點**（輸出 `level` Float，每幀重算，與 `Time` 對稱）→ 柏為**拉線**到任何參數，中間插 `Multiply`/`Remap` scale。照 TiXL `Slot<float>`+`Animated`，無特殊音訊型別。
  - 港自 my-world（`~/Projects/my-world/source/audio`）：`AttackDetector`(暫態)、`AudioAnalyzer`(RMS)。
  - `platform/audio_capture`(AVAudioEngine 收音) + **Info.plist `-sectcreate` + POST_BUILD `codesign` 重簽**解 TCC（否則 bare binary 碰麥克風 SIGABRT / 提示不跳）。
  - **裝置選單 ✓**（項目 1）：Toolbar「Audio In」下拉，選內建/2i2/BlackHole/aggregate → route(`kAudioOutputUnitProperty_CurrentDevice`) + 持久化(`~/.simple_world_audio_device`)。`app/audio_settings` 協調(`ui→app→platform`)。
  - **input meter ✓**：Toolbar `level`(RMS,有沒有聲音)/`hit`(暫態) 條。
  - selftest：`--selftest-attack/analyzer/audionode/resolve/valuecook/...` 全綠。commits 到 `e35d567`。

### 關鍵狀態 / 踩雷（接手必知）
- **`AudioReaction` 目前只輸出「暫態」**(attack envelope)；持續音(喊/長音)≈0。`level→Speed` 直連時靜音/持續音 Speed=0 → **粒子凍結（非當機；`--selftest-audionode` 證求值不 hang）**。要 mapping(Multiply+base) 或**項目 2 選「電平」特徵**才解。
- TCC：麥克風已 **Authorized**；沒提示是因為已授權(非 bug，多半繼承 Terminal)。`--audio-permission-status` 可查。
- BlackHole 沒東西播進去時 0 blocks(正常 loopback)；2i2/內建一直有 blocks。
- 北極星不變：Mac 版 TiXL；audio 子系統照 TiXL（`external/tixl/Core/Audio` + `Operators/Lib/io/audio/AudioReaction.cs`），不自創。

### 下一步：**項目 2（柏為排序的最後一塊，in-progress 的下一步）**
**`AudioReaction` 長出 TiXL 參數**（照 `AudioReaction.cs`）：
1. **選特徵**：電平(RMS,持續音有值)／暫態(attack)／頻段。**電平特徵直接解「喊了粒子不動」**——優先。
2. **選頻段**(低頻=鼓)：需 **FFT/頻譜分析**（新 DSP，照 TiXL `FrequencyBands[32]`）——目前只有 RMS+attack，**缺 FFT**。
3. **節點非-Float 參數機制**：`NodeSpec` 目前只有 Float ports；要照 TiXL `InputBand`/`Output` enum 擴節點契約。
（項目 1 裝置選單 ✓、項目 3 Add 選單相容 ✓——`AudioReaction` 已註冊進 spec 表、Add 選單資料驅動。）

### ⚠ 律法 debt（2026-06-09 review，**柏為決定**）
1. **platform→runtime 違律**：`audio_capture.mm`(platform) include `runtime/attack_detector`+`audio_analyzer`，違「葉子互不依賴」。因＝audio-rate DSP 在 tap callback(最低延遲)。叉：(a) 放寬律法(IO 葉子可單向用 runtime 純計算工具，無環) 保延遲；(b) **callback 反轉**：`capture` 只發布原始樣本+回呼(只存 fn-ptr 不 include runtime)，DSP 由 app/main 驅動 — 律法乾淨且仍 audio-rate。**建議 (b)**。
2. **`graph.cpp` 588 行 > 400**：混了 node registry+圖模型+eval+json+selftests。建議拆(`graph_selftests.cpp`/`graph_json.cpp`)。
3. `main.cpp` 448 行(外殼，邊緣警訊)。
4. ~~setTestEnvelope dead code~~ 已清。
5. 其餘依賴方向、verify 葉子、資料驅動(node/device menu) ✓ 乾淨。

---

## 完整脊椎（▣ = 柏為體感關卡 = 早上；其餘 = 機器可驗水管工，夜裡自主）

| # | 段 | 性質 | 血緣 | TiXL/合約源 | 鏈式/兄弟 |
|---|---|---|---|---|---|
| **S0** | **預清 header 雷**：理 `EvaluationContext`/`Particle.h`/`tixl_point.h` include 結構，讓 ctx 可被擴充不撞 TU 衝突 | 預備 | graph.h 自標的衝突 | — | 地基 |
| **S1** | **解析模型 = 來源註冊表 + binding/override**：擴 `evalParam` 成 `override→binding→constant`；binding 含 connection(既有)/live-source/automation(佔位) | 水管工 selftest | **擴充** evalParam/`--selftest-valuecook` | L5；TiXL `Slot.OverrideWithAnimationAction` | **拱心石**（後面全靠它）|
| **S2** | **▣ 聲音→參數**：把 audio_ingest.AudioInput.value 註冊成一個 live-source，binding 到一個粒子參數（如 Speed/Amount） | **體感** | **接** audio_ingest(已 built) 當 S1 首個來源 | L3 World1、L5 | **窄·M1**（gate 解析模型）|
| S3 | **Curve 原語**：`SortedList<time,Keyframe>`；Keyframe{time,value,in/out內插,tangent,tension}；`GetSampledValue(t)`；live-append | 水管工 selftest | **全新**，照抄 TiXL `Curve.cs`/`VDefinition.cs` 欄位+取樣 | `Curve`/`VDefinition` | — |
| S4 | **scoreGraph + automation 來源**：scoreGraph 存 per-(node,param) 曲線；automation = 一個 binding 來源，在 playhead 取樣（先用值脊椎自由 time 當 playhead 佔位） | 水管工 selftest | **全新** scoreGraph；接 S1/S3 | `Animator`、L10/L12 | — |
| S5 | **Transport 核心**：struct(position/playState/length/rate/fxTime)+推進；拆 EvaluationContext wall vs playhead | 水管工 selftest | **搬** FRAME_SCHEDULER golden（`frame_scheduler_contract.test.js`：一幀一 context、`previousFrame=[null,0,1]`、非法 clockOwner→exit1）；fxTime 抄 Playback.cs | `Playback`、L8 | — |
| S6 | **▣ automation 播放 + scrub**：transport 餵 playhead；automation 曲線在 playhead 跑；最小 transport UI(play/pause/scrub)。**選低狀態相依參數當 demo**（顏色/直接量，scrub 才讀得出倒帶；粒子有狀態不完美回放，誠實標） | **體感** | 接 S4/S5 | `Playback` 拖曳、L10 | **M2**（坐 S2 上、與後續兄弟可批）|
| S7 | **punch-in + arm**：arm 狀態；武裝+播放時 override 寫 keyframe（只寫碰過的） | 水管工 selftest | **全新** | TiXL automation-record / Ableton punch-in | — |
| S8 | **▣ 錄製 + override/re-enable**：record 鈕 + **全域** re-enable 鈕（Ableton 實際機制）；轉旋鈕錄成曲線→回放；抓旋鈕蓋過→re-enable 彈回 | **體感** | 接 S7/S6 | L13、Ableton | **M3**（鏈式，坐 S6 上）|
| S9 | **World 2 載入播放**：載音檔→擁播放 transport+取樣時鐘→playhead=音檔位置；分析成參數 | 水管工+體感 | **全新** | AUDIO_INGEST World 2(L11) | — |
| S10 | **World 1 現場分析**：BlackHole raw→暫態偵測→live-source override；量延遲 | 水管工+體感 | **全新** | AUDIO_INGEST World 1 + L14 budget | — |
| S11 | **▣ 兩個聲音世界**：載入檔同步感 + 現場大鼓同時感 + 真機延遲數字 | **體感** | 接 S9/S10 | L3/L11/L14 | **M4**（批）|

## 柏為的 4 個早上（排程用）

| 早上 | 驗什麼 | 寬窄 | 對應夜 |
|---|---|---|---|
| **M1** | **聲音→參數**：你放/彈聲音，粒子跟著動 | 窄·單獨（gate 解析模型）| 夜 1 = S0–S2 |
| **M2** | automation 曲線播放 + scrub（播放頭跑、可拖）| 中（坐 M1 上）| 夜 2 = S3–S6 |
| **M3** | 錄製手感 + override 黏放 | 鏈式（等 M2 過）| 夜 3 = S7–S8 |
| **M4** | 載入檔同步 + 現場大鼓同時 + 延遲量測 | 批 | 夜 4 = S9–S11 |

> 段詳細度：**S0/S1/S2 已詳到可執行（下方）= 第一個夜→晨循環**。S3–S11 = 脊椎條目，輪到才詳（後段細節依賴前段形狀）。
> 夜→晨節律（牽繩 B）：水管工段用 `/goal "段 selftest 綠"` 自主串到 ▣ 就停；**每段 selftest 完成後派一個 subagent 對抗審查「漏測什麼」**（擋假綠燈複利）。▣ 段 eye+hand 先自證、再交柏為摸。

---

## S0：預清 header 雷（預備，動 S1 前必做）

血緣：`graph.h` 自己標了「`EvaluationContext` 在 `Particle.h`，故意不引，否則跟 `tixl_point.h` 同 TU 衝突」。S1 要擴 ctx，先拆這顆雷。

- [x] **Step 1: 盤 include 圖**（不寫碼）：grep `EvaluationContext` / `#include.*Particle.h` / `#include.*tixl_point.h` across `app/src`，畫出誰在哪個 TU 撞誰。
- [x] **Step 2: 決定 ctx 的家**：若衝突真存在 → 把 `EvaluationContext` 抽到獨立輕量 header（`runtime/eval_context.h`，零重依賴），Particle.h/tixl_point.h 各自 include 它。若實際不衝突（只是註解過度保守）→ 記錄「可直接擴」，跳過。
- [x] **Step 3: build 回歸**：`cmake --build build -j` + 全 8 selftest 綠（純結構搬移，零行為變）。
- [x] **Step 4: Commit**（若有動）。

> S0 純結構、無體感。完成 = 全 selftest 綠 + S1 能安全擴 ctx。
>
> **✓ 2026-06-09 done.** 衝突**為真**：`Particle.h`(32B `struct Particle`) 與 `tixl_point.h`(64B `struct Particle`) 同 TU 互斥；`EvaluationContext` 寄居 `Particle.h` 故被綁架。抽出 `runtime/eval_context.h`（metal-safe，裸路徑 `"eval_context.h"` 因 shader 只有 `-I src/runtime`），Particle.h **與** tixl_point.h 各自 include 它。關鍵副產品：main.cpp 經 `particle_system.h→tixl_point.h→eval_context.h` 拿到完整 `EvaluationContext` 而**不**碰 Particle.h → **S2 註冊 audio source 的解鎖點**。回歸：build 綠（雙端 static_assert 過）+ **11/11 selftest 綠**（非 8，audio_ingest 已併）+ RED 抽驗仍 exit 1。踩雷記：Metal selftest（color/draw/eye/hand）在背景 bash 迴圈會 hang，須前景單跑。

---

## S1：解析模型 = 來源註冊表 + binding/override（拱心石，可執行）

**Files:** `app/src/runtime/graph.h`/`graph.cpp`（evalParam 擴 + 解析）、新 `app/src/runtime/source_registry.h`/`.cpp`（來源註冊 + binding/override 狀態）、`app/src/main.cpp`（selftest 派發）。

血緣：**擴充** 值脊椎的 `evalParam`（現為 `連線→上游 else 常數`）。TiXL `Slot.OverrideWithAnimationAction` = 換驅動者。

- [x] **Step 0: 讀現況**（不寫碼）：精讀 `evalParam`/`evalFloat`（graph.cpp）現有簽名與 `connectionToInput`，確認擴充點。
- [x] **Step 1: 資料模型（source_registry.h）**
  ```cpp
  namespace sw {
  enum class BindingKind { Constant, Connection, Automation, LiveSource };
  // live 源：一個有 id、能在當前 ctx 給值的東西（audio/hand/MIDI…全走這個）。
  struct LiveSource { std::string id; float (*value)(void* self, const EvaluationContext&); void* self; };
  // 每參數的解析狀態（key = (nodeId, portId)）。
  struct ParamBinding { BindingKind kind = BindingKind::Constant; std::string sourceId; /*automation: curveId*/ };
  struct ParamOverride { bool active = false; float value = 0; };  // 黏著式，re-enable 才清
  class SourceRegistry {
   public:
    void registerSource(const LiveSource&);          // 註冊一個 live 源（= 平台「一行加一個來源」）
    void bind(int nodeId, std::string portId, ParamBinding);
    void setOverride(int nodeId, std::string portId, float v);   // live 碰
    void reEnableAll();                                          // 全域 re-enable（清所有 override）
    const ParamBinding* binding(int nodeId, const std::string& portId) const;
    const ParamOverride* override_(int nodeId, const std::string& portId) const;
    const LiveSource* source(const std::string& id) const;
  };
  }
  ```
- [x] **Step 2: evalParam 擴成 `override → binding → constant`**
  在 evalParam 解析有效值時，先問 registry：
  ```cpp
  // 偽碼：
  if (auto* ov = reg.override_(n->id, paramId); ov && ov->active) return ov->value;          // 1 override
  if (auto* b = reg.binding(n->id, paramId)) switch (b->kind) {                               // 2 binding
    case Connection: /* 既有：connectionToInput→evalFloat 上游 */ break;
    case LiveSource: if (auto* s = reg.source(b->sourceId)) return s->value(s->self, ctx);    // 聲音/手…
    case Automation: /* S4：scoreGraph 曲線@playhead 取樣 */ break;
    case Constant:   break;
  }
  return /* 既有常數 fallback */;                                                              // 3 constant
  ```
  > 值脊椎的「有連線→上游」自動成為 `binding=Connection`，零改、相容。
- [x] **Step 3: `runResolveSelfTest`（RED→GREEN）**
  斷言：
  - constant：無 binding 無 override → 回常數。
  - connection：接線 → 回上游求值（回歸值脊椎行為）。
  - live-source：註冊一個固定回 7.0 的源、bind 上去 → evalParam 回 7.0。
  - override：setOverride(9.0) → 回 9.0（蓋過 binding）；reEnableAll() → 落回 binding。
  - 一參數一 binding：bind 兩次，第二次取代第一次。
  - `injectBug` 翻一條（例 override 不蓋過 binding）→ FAIL exit 1。
- [x] **Step 4: main 派發 + build RED→GREEN + 回歸**
  ```bash
  cd app && cmake --build build -j
  ./build/simple_world --selftest-resolve       # PASS
  ./build/simple_world --selftest-resolve-bug   # FAIL
  ./build/simple_world --selftest-valuecook && --selftest-flow   # 回歸 PASS（值脊椎連線行為不變）
  ```
- [x] **Step 5: 對抗審查**：派 subagent「找 runResolveSelfTest 漏測的解析路徑」（例：override+automation 同時？binding 換 kind？），補測。
- [x] **Step 6: Commit**

> S1 純水管工：`--selftest-resolve`/`-bug` 綠/紅即完成，柏為不必看。摸的東西在 S2。
>
> **✓ 2026-06-09 done.** evalParam 加 optional `const SourceRegistry* reg=nullptr`（nullptr=值脊椎零改，4 個現有呼叫點不動→回歸自動綠）；解析序 override→binding(LiveSource 早回／Automation 佔位)→落回既有圖(連線=Connection else 常數)。新 `source_registry.h/.cpp`（map keyed by `(nodeId,portId)`）。`runResolveSelfTest` **11 路徑**綠／`-bug` 紅。**對抗審查**（Explore subagent）出 11 項，採納 5（override-無-binding+re-enable→常數、dangling sourceId、null value-fn、Automation 落空、壞 paramId→fallback），駁回 6 並記理由（顯式 Connection/Constant binding=冗餘未用路徑，wire 本身即 Connection binding；firstOfType／缺節點／壞 type=S1 未動的既有 guard 且 key 一致性已被 live-source 測隱含覆蓋；ctx 欄位=S5）。**12/12 selftest 綠**。S2 接點：main.cpp:339-342 cook 迴圈。

---

## S2：▣ 聲音→參數（M1，第一個體感勝利）

> **⚠ 動工前讀（2026-06-09 夜 1 收尾；柏為晨會先決一個叉，30 秒）：**
> S0+S1 已完成 committed（解析模型拱心石綠，12/12 selftest）。S2 卡在**真實缺件**：audio_ingest **折疊器**已 built+測過（`AudioIngest.sampleFrame(log,t) → AudioInput{values: map<"track/param",float>}`），但 **main.cpp render loop 沒實例化它、全 src 無任何 OSC/UDP live 接收器** —「聲音→粒子」缺「餵入活 app」這一段。
> 現成料齊：replay fixture `docs/runtime/fixtures/audio_ingest_semantic_log.json` 在、playback pattern 在（`audio_ingest_replay.cpp`：每幀 `sampleFrame((double)i/fps)`）、bind 接點在 `main.cpp:339-342` cook 迴圈、`evalParam` 已收 `reg`。
>
> **叉 1 — 第一個 felt win 要哪種餵入？**
> - **(a) replay-fed**：app 載 fixture，每幀 `sampleFrame(g_time)` 把一個 audio 值接上 Speed。能直接接（約 1 段水管工 + 眼自證）。**但体感是「載檔看回放」，不是你描述的「你彈、它回應」。** 動 render loop 有風險。
> - **(b) live OSC 接收器**：真正「你彈 Bespoke → 它回應」。要新建 UDP/OSC 接收（platform 網路層，像獨立一段，較大）。
>
> **叉 2 — 哪個聲音量 → 哪個參數、怎麼縮放？**（`values` 是 map，沒有單一 amplitude，要選 key；plan 建議先寫死 bind `ParticleSystem.Speed`。）**這是体感/美學 = 柏為的場，夜裡不替你猜。**
>
> 決完即接。以下 Step 1–6 是 (a)+寫死 Speed 的既定形狀，(b) 或不同 mapping 則照新形狀重詳。

**Files:** `app/src/main.cpp`（把 audio_ingest 的值包成 LiveSource 註冊 + bind 一個參數）、`app/src/ui/editor_ui.cpp`（Inspector 顯示「← audio」來源，最小）。

- [ ] **Step 1:** 把 `audio_ingest` 的 `AudioInput.value` 包成一個 `LiveSource{id="audio.main", value=…}`，啟動時 `reg.registerSource`。
- [ ] **Step 2:** bind 一個顯眼參數（建議 `ParticleSystem.Speed` 或 `TurbulenceForce.Amount`）到 `binding=LiveSource("audio.main")`（先寫死綁定，UI 綁定 S 之後做）。
- [ ] **Step 3:** main 每幀 evalParam（已走 S1 解析）→ setter，聲音值即驅動粒子。Inspector 該參數顯示「← audio.main」（重用值脊椎「被驅動變灰」樣式）。
- [ ] **Step 4（眼，我先證）**：app 跑起來，餵 audio_ingest replay fixture（或 live Bespoke），`req_clean` 前後兩幀像素**應不同**（聲音值變→粒子變）。RED→GREEN：靜音時不動、給值時動。
- [ ] **Step 5（柏為親手 = M1）**：柏為放/彈聲音（Bespoke OSC 或載 replay）→ **粒子跟著聲音動**。這是第一個「你彈、它回應」的勝利。
- [ ] **Step 6: Commit**（feat: bind audio as first live-source → particles react to sound）

> 完成 = 柏為親耳+親眼確認聲音驅動粒子。**零 transport、零 scrub 糊、重用已 built audio_ingest。**

---

## World 1（= S10 提前為 M1；柏為 2026-06-09 拍板，取代 OSC 路當第一個 felt win）

> **柏為選 World 1（現場原始音訊→暫態→粒子炸）當第一個 felt win，不走 S2 的 OSC 語意路**（OSC 那條退為後續一個世界）。M1 表的「聲音→參數：你放/彈聲音，粒子跟著動」由 World 1 實現。血緣：契約 L3 世界1 + L5 拱心石（`Speed ← audio.kick` = **binding=LiveSource**，非 override；override 等 S8 有 automation 可蓋時才用）+ L14 延遲閘門。

**鏈：** 麥克風/BlackHole 原始音訊 → `audio_capture`(platform, AVAudioEngine tap) → `AudioAnalyzer.processBlock`(samples→RMS/peak，港自 my-world) → `AttackDetector.processFrame`(RMS→onset/envelope，**✓已港**) → envelope → LiveSource `"audio.kick"` → `evalParam` binding 驅動 `ParticleSystem.Speed` → 粒子。

**File Structure（架構分區；★ = 港自 my-world `~/Projects/my-world/source/audio`，C++ 同棧、無 JUCE 的分析層幾乎逐字可移）：**
- `runtime/attack_detector.{h,cpp}` ★ — RMS→onset/envelope（delta-based 上升量 + 時間制 debounce/release + NaN/clip 防呆）。**✓ 已港+測（commit 5429564，13/13 綠）。** 純計算葉子。
- `runtime/audio_analyzer.{h,cpp}` ★ — samples→RMS/peak/gate snapshot（my-world `AudioAnalyzerState`，多聲道 mono-mix + atomic）。**待港**（純計算，可 selftest）。
- `platform/audio_capture.{h,mm}` — AVAudioEngine 預設輸入 tap；callback 餵 `AudioAnalyzer.processBlock` → snapshot；每幀 `AttackDetector.processFrame({rms,peak,timeMs})`。**原生接口葉子**（ARC：不列 `-fno-objc-arc`）。device 綁定是 my-world 用 JUCE 那塊，simple_world 改 AVAudioEngine 自接。
- `main.cpp` — `SourceRegistry` 註冊 LiveSource `"audio.kick"`(value=`AttackDetector.envelope`)，bind `ParticleSystem.Speed ← audio.kick`，cook 迴圈 evalParam 傳 `&reg`。
- eye(已有) 自證；hand 不需要。

**鎖定的決定：** 借 my-world 證碼（不重造，柏為 2026-06-09 指）；factoring = capture 算 RMS、detector 吃 RMS frame（比吃原始樣本乾淨、好測）；onset=delta-based（**非 FFT**，大鼓夠用 L3）；第一版輸入=**預設麥克風**（拍手即測），BlackHole=之後切 device；tap bufferSize 小塊(256–512)壓延遲；接法=**binding=LiveSource**（非 override）。
**也可借（留後）：** my-world `AudioRealtimeDelivery`（4-slot lock-free 交接，若單 atomic snapshot 不夠再港）、`LiveIOOscReceiver`（**OSC 世界**現成——解先前「OSC 接收器沒 built」的缺）、其餘 detector（sustain/silence/density/residue/aggregate-pressure）。
**未知/風險（到該 step 誠實面對，卡住就停）：** ①麥克風 TCC 權限（bare binary 可能算到 Terminal；可能要 .app bundle / `NSMicrophoneUsageDescription`）②audio thread→render loop 用 `std::atomic` 交接 ③真實延遲（W1.6 量；全程 sound-in→photon-out 需外部設備，誠實標）。

### W1.1：AttackDetector（runtime, RMS→onset）— ✓ DONE（港自 my-world，commit 5429564）

港 my-world `source/audio/AnalyzerAttackDetector` → `app/src/runtime/attack_detector.{h,cpp}`（namespace sw）。`--selftest-attack` 移植其 5 個 fixture 案例（quiet→0／single-clean@frame2 且 attackValue≥0.10／steady-loud→0 不重觸／micro-wiggle→0／missing-rms→detectorOk=false+診斷"missing_input:rms"），`--selftest-attack-bug` 用 rise=0 真退化→FAIL。全 13/13 回歸綠。唯一簡化：丟 my-world 的 sampleCount 驗證（simple_world capture 不傳）。

> ⚠ **下方 Step 1–7 是動工前的 from-scratch 能量包絡草圖，已作廢**（被 my-world 港取代，從未 commit）。保留為歷史脈絡；實際碼看 `app/src/runtime/attack_detector.{h,cpp}`。下一塊 = W1.2 港 `AudioAnalyzer` + 接 `audio_capture`。

**Files（作廢草圖）:** Create `app/src/runtime/onset_detect.h`, `app/src/runtime/onset_detect.cpp`; Modify `app/src/main.cpp`(selftest 派發), `app/CMakeLists.txt`(加源).

- [ ] **Step 1: 寫 `onset_detect.h`**（介面 + selftest 宣告）

```cpp
// runtime/onset_detect — energy-based onset (transient) detector. Pure computation,
// zero audio hardware: consumes blocks of mono float samples and reports a smoothed
// energy envelope + a debounced onset flag. Wide-band transients (a kick) are what
// this catches; pitch/spectral onsets (need an FFT) are out of scope (L3: 大鼓好打).
// runtime leaf: the platform capture layer owns an instance and calls process() on
// the audio thread; the detector is single-threaded (its caller serializes access).
#pragma once

namespace sw {

struct OnsetConfig {
  float floor          = 1e-4f;  // block energy below this = silence (no onset)
  float riseFactor     = 1.8f;   // onset fires when block energy > running avg * this
  float avgSmooth      = 0.95f;  // running-average inertia per block (slow tracker)
  float envAttack      = 0.5f;   // envelope rise toward energy when rising
  float envRelease     = 0.08f;  // envelope decay per block otherwise (boom-then-fade)
  int   debounceSamples = 2000;  // min samples between onsets (~45ms @ 44.1k)
};

class OnsetDetector {
 public:
  struct Result { float energy; float envelope; bool onset; };
  OnsetDetector() = default;
  explicit OnsetDetector(OnsetConfig cfg) : cfg_(cfg) {}
  // Process one block of n mono samples. Stateful across calls.
  Result process(const float* samples, int n);
  float envelope() const { return env_; }  // latest envelope (no-arg poll)
 private:
  OnsetConfig cfg_;
  float avg_ = 0.0f;
  float env_ = 0.0f;
  int   sinceOnset_ = 1 << 30;
};

// Isolated proof (Rule 5): silence→no onset+env decays; loud-after-quiet→exactly one
// onset; sustained-within-debounce→no re-fire; loud-after-window→fires again.
// injectBug sets a real degenerate config (debounce off) so the no-re-fire assertion
// genuinely fails — teeth, not a synthetic flip.
int runOnsetSelfTest(bool injectBug);

}  // namespace sw
```

- [ ] **Step 2: 寫 `onset_detect.cpp`**（實作 + selftest）

```cpp
#include "runtime/onset_detect.h"

#include <cmath>
#include <cstdio>

namespace sw {

OnsetDetector::Result OnsetDetector::process(const float* samples, int n) {
  double sum = 0.0;
  for (int i = 0; i < n; ++i) sum += (double)samples[i] * samples[i];
  float energy = n > 0 ? (float)(sum / n) : 0.0f;  // block mean-square

  bool onset = false;
  sinceOnset_ += n;
  if (energy > cfg_.floor && energy > avg_ * cfg_.riseFactor &&
      sinceOnset_ >= cfg_.debounceSamples) {
    onset = true;
    sinceOnset_ = 0;
  }
  avg_ = avg_ * cfg_.avgSmooth + energy * (1.0f - cfg_.avgSmooth);  // adaptive base

  if (onset) {
    env_ = 1.0f;                                   // snap up on transient
  } else if (energy > env_) {
    env_ += (energy - env_) * cfg_.envAttack;       // fast attack
  } else {
    env_ -= env_ * cfg_.envRelease;                 // slow release (boom-then-fade)
    if (env_ < 0.0f) env_ = 0.0f;
  }
  return Result{energy, env_, onset};
}

int runOnsetSelfTest(bool injectBug) {
  OnsetConfig cfg;
  if (injectBug) cfg.debounceSamples = 0;  // degenerate: no debounce -> double-fires
  OnsetDetector det(cfg);

  const int N = 512;
  float quiet[N], loud[N];
  for (int i = 0; i < N; ++i) { quiet[i] = 0.0f; loud[i] = 0.5f * std::sin(i * 0.3f); }

  bool ok = true;
  for (int b = 0; b < 10; ++b) { auto r = det.process(quiet, N); ok = ok && !r.onset; }
  ok = ok && (det.envelope() < 1e-3f);                                  // 1 silence

  int fires = 0; { auto r = det.process(loud, N); if (r.onset) ++fires; }
  ok = ok && (fires == 1);                                              // 2 one onset

  int refires = 0;
  for (int b = 0; b < 3; ++b) { auto r = det.process(loud, N); if (r.onset) ++refires; }
  ok = ok && (refires == 0);                                            // 3 no re-fire

  for (int b = 0; b < 8; ++b) det.process(quiet, N);                    // gap > debounce
  int fires2 = 0; { auto r = det.process(loud, N); if (r.onset) ++fires2; }
  ok = ok && (fires2 == 1);                                             // 4 fires again

  printf("[selftest-onset] silence/onset/debounce(%d)/refire -> %s\n",
         cfg.debounceSamples, ok ? "PASS" : "FAIL");
  return ok ? 0 : 1;
}

}  // namespace sw
```

- [ ] **Step 3: main.cpp 派發**（在 `--selftest-resolve-bug` 之後加）

```cpp
    if (std::strcmp(argv[i], "--selftest-onset") == 0)
      return sw::runOnsetSelfTest(/*injectBug=*/false);
    if (std::strcmp(argv[i], "--selftest-onset-bug") == 0)
      return sw::runOnsetSelfTest(/*injectBug=*/true);
```
並在 `main.cpp` 頂部 include：`#include "runtime/onset_detect.h"`

- [ ] **Step 4: CMakeLists.txt 加源**（`src/runtime/graph.cpp` 行後）：`  src/runtime/onset_detect.cpp`

- [ ] **Step 5: build RED→GREEN + 回歸**
```bash
cd app && cmake -S . -B build && cmake --build build -j
./build/simple_world --selftest-onset       # PASS (exit 0)
./build/simple_world --selftest-onset-bug   # FAIL (exit 1)
# 回歸：全 12 selftest 仍綠（純新增葉子，零既有行為變）
```

- [ ] **Step 6: 對抗審查**：派 subagent「找 runOnsetSelfTest 漏測的偵測路徑」（例：漸強無突變不該誤觸？極短塊 n<debounce 的累積？env 從不歸零？），採納有牙的、駁回冗餘。

- [ ] **Step 7: Commit**（`feat(runtime): energy-based onset detector + selftest (World 1 W1.1)`）

> W1.1 純計算 selftest 綠即完成，柏為不必看。摸的東西在 W1.3+。

### W1.2：audio_analyzer + audio_capture — ✓ DONE（commit faee3dc + 9776e8f）
- `runtime/audio_analyzer`（港 my-world `AudioAnalyzerState`）：samples→RMS/peak/gate snapshot。`--selftest-analyzer` 綠。
- `platform/audio_capture.{h,mm}`（AVAudioEngine）：tap→analyzer→attack→atomic envelope，C++ pimpl 藏 ObjC，ARC，連 AVFoundation。權限流程：未定→`requestAccessForMediaType`→授權後 async 啟動引擎；失敗 non-fatal。
- **TCC 風險已解**：bare binary 缺 `NSMicrophoneUsageDescription` 會 SIGABRT → 用 `-Wl,-sectcreate,__TEXT,__info_plist` 把 `app/Info.plist`（含該 key）嵌進 Mach-O，`otool -P` 確認在。**沒 headless 跑 smoke**（怕 auto-deny 毒化 grant），第一次真開麥克風＝柏為早上。`--audio-capture-smoke <秒>` 留給手動驗。

### W1.3：接粒子（main, binding=LiveSource）— ✓ DONE（commit ca70e08）
- 首幀 `g_audioCapture.start()` + 註冊 `LiveSource{"audio.kick", audioKickToSpeed, &g_audioCapture}` + `bind(ps->id,"Speed",{LiveSource,"audio.kick"})`；cook 迴圈 Speed 傳 `&g_audioReg`。
- mapping（v1 寫死，柏為調）：`audioKickToSpeed = 1.0 + envelope*4.0`（靜音→1.0 base＝原行為；full kick→5.0）。
- `--selftest-audio-particle` 綠：envelope 0/0.5/1 → Speed 1/3/5（render loop 同路徑，mapping fn 共用不漂移）。

### W1.4：眼自證 — ✓ 以組合證明替代（無 GUI 像素注入）
- 全鏈由四個 selftest 相扣證明：`attack`(RMS→onset) + `analyzer`(samples→RMS) + `audio-particle`(envelope→Speed) + `flow`(Speed→粒子運動，既有)。唯一未由機器證的環＝**真麥克風→樣本**（要真聲+授權+人眼），即 W1.5。沒做 GUI 注入像素比對（4am 風險>價值，組合已覆蓋）。

### W1.5：▣ 柏為（M1 第一個 felt win）— **PENDING（早上）**
1. `cd app && ./build/simple_world` 啟動。2. **彈出麥克風授權 → 按允許**（第一次；嵌入的 Info.plist 讓它彈而非 crash）。3. 對麥克風**拍手/敲鼓** → 粒子 Speed 應炸一下再回落。**這是「你彈、它回應」第一次成立。**
- 若沒反應排查：(a) 授權被拒→系統設定→隱私→麥克風開 Terminal/binary；(b) 想先確認麥克風進得來→`./build/simple_world --audio-capture-smoke 5` 看 rms/env 跳；(c) 反應太鈍/太敏→調 `audioKickToSpeed` 的 `*4.0` 或 `AttackParams` 的 `riseThreshold`。

### W1.6：延遲量測（L14 閘門）— pending（felt win 成立後）
- 量 onset-detected→該值被 render 幀消費 的延遲（可控段）；全程 sound-in→photon-out 需外部設備（相機/迴路），**誠實標**目標 ≤25ms / 天花板 ≤40ms。

---

## S3–S11：待詳（脊椎條目見上表）

輪到時照值脊椎格式詳成 bite-sized TDD（File Structure → 逐 Step 碼/build/selftest/commit → ▣ 段加 eye+hand + 柏為驗收）。
詳前先做該段「Step 0 讀血緣源」（搬的讀舊 proof/contract、TiXL 段讀 `external/tixl`）。Transport（S5）的 fxTime 規則動工時抄 Playback.cs。

---

## Self-Review（對 ledger 覆蓋）
- L5 解析模型（binding/override、一參數一 binding、混音走圖）= S1 拱心石 ✓
- L1/L8 transport 兩鐘 = S5（退到 automation 才需要，sound-first 不擋）✓
- L9/L10/L12 Curve/automation/scoreGraph 照 TiXL = S3/S4 ✓
- L13 override/re-enable（全域）照 Ableton = S8 ✓
- L3/L11/L14 兩世界 + 延遲閘門 = S9/S10/S11 ✓
- L6 驗收三層：每段 selftest（機器）+ 對抗審查 → ▣ 段 eye+hand（agent 先驗）→ 柏為摸 ✓
- 血緣：每段標 搬/擴充/全新（S1 擴 valuecook、S2 接 audio_ingest、S5 搬 FRAME_SCHEDULER）✓
- 提速：難的集中 S1（通用解析表），後面全是「註冊一個來源」；sound-first 零 transport；S0 預清雷；對抗 selftest ✓
- 缺口誠實標記：S6 scrub 在有狀態粒子不完美回放（選低狀態 demo + 標記）；fxTime 規則延後抄 TiXL；S3–S11 輪到才詳。

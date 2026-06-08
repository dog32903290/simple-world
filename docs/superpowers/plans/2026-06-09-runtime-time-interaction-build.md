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

- [ ] **Step 0: 讀現況**（不寫碼）：精讀 `evalParam`/`evalFloat`（graph.cpp）現有簽名與 `connectionToInput`，確認擴充點。
- [ ] **Step 1: 資料模型（source_registry.h）**
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
- [ ] **Step 2: evalParam 擴成 `override → binding → constant`**
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
- [ ] **Step 3: `runResolveSelfTest`（RED→GREEN）**
  斷言：
  - constant：無 binding 無 override → 回常數。
  - connection：接線 → 回上游求值（回歸值脊椎行為）。
  - live-source：註冊一個固定回 7.0 的源、bind 上去 → evalParam 回 7.0。
  - override：setOverride(9.0) → 回 9.0（蓋過 binding）；reEnableAll() → 落回 binding。
  - 一參數一 binding：bind 兩次，第二次取代第一次。
  - `injectBug` 翻一條（例 override 不蓋過 binding）→ FAIL exit 1。
- [ ] **Step 4: main 派發 + build RED→GREEN + 回歸**
  ```bash
  cd app && cmake --build build -j
  ./build/simple_world --selftest-resolve       # PASS
  ./build/simple_world --selftest-resolve-bug   # FAIL
  ./build/simple_world --selftest-valuecook && --selftest-flow   # 回歸 PASS（值脊椎連線行為不變）
  ```
- [ ] **Step 5: 對抗審查**：派 subagent「找 runResolveSelfTest 漏測的解析路徑」（例：override+automation 同時？binding 換 kind？），補測。
- [ ] **Step 6: Commit**

> S1 純水管工：`--selftest-resolve`/`-bug` 綠/紅即完成，柏為不必看。摸的東西在 S2。

---

## S2：▣ 聲音→參數（M1，第一個體感勝利）

**Files:** `app/src/main.cpp`（把 audio_ingest 的值包成 LiveSource 註冊 + bind 一個參數）、`app/src/ui/editor_ui.cpp`（Inspector 顯示「← audio」來源，最小）。

- [ ] **Step 1:** 把 `audio_ingest` 的 `AudioInput.value` 包成一個 `LiveSource{id="audio.main", value=…}`，啟動時 `reg.registerSource`。
- [ ] **Step 2:** bind 一個顯眼參數（建議 `ParticleSystem.Speed` 或 `TurbulenceForce.Amount`）到 `binding=LiveSource("audio.main")`（先寫死綁定，UI 綁定 S 之後做）。
- [ ] **Step 3:** main 每幀 evalParam（已走 S1 解析）→ setter，聲音值即驅動粒子。Inspector 該參數顯示「← audio.main」（重用值脊椎「被驅動變灰」樣式）。
- [ ] **Step 4（眼，我先證）**：app 跑起來，餵 audio_ingest replay fixture（或 live Bespoke），`req_clean` 前後兩幀像素**應不同**（聲音值變→粒子變）。RED→GREEN：靜音時不動、給值時動。
- [ ] **Step 5（柏為親手 = M1）**：柏為放/彈聲音（Bespoke OSC 或載 replay）→ **粒子跟著聲音動**。這是第一個「你彈、它回應」的勝利。
- [ ] **Step 6: Commit**（feat: bind audio as first live-source → particles react to sound）

> 完成 = 柏為親耳+親眼確認聲音驅動粒子。**零 transport、零 scrub 糊、重用已 built audio_ingest。**

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

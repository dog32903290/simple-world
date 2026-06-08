# Runtime Contract Alignment Ledger

Last updated: 2026-06-09 01:00 Asia/Taipei

這不是合約本身，是**改約前的對齊底稿**。記錄一次跟柏為的長對話（8+ 輪）中，
針對 runtime 合約「時間 / 聲音 / 交互層」含混處的逐項壓力測試與鎖定決定。
何時實作未定 → 先落檔，不靠對話記憶。

來源對話主軸：「壓一下合約、找含混處、了解我內心真正的需求並比較合約」。
比對基準合約：`docs/runtime/MY_WORLD_RUNTIME_CONTRACT.md`、`FRAME_SCHEDULER_CONTRACT.md`、
`AUDIO_INGEST_CONTRACT.md`。

---

## 核心發現（一句話）

合約現在是「一個有紀律、以視覺為唯一領域、用機器證據把關、當外部聲音世界夥伴、
只有一個單調時鐘的 runtime」。柏為本人要的是「一個自由、可擴充的**交互平台**，影像為主軸
但多種協議是平起平坐的公民；時間有編排層＋現場層兩種並存；他的手是『成了』的裁判；
現在當夥伴、但架構要留路給未來自足」。**差的不是幾行字，是地基假設跟本人不同國。**

---

## Locked（已鎖定）

### L1 — transport 時鐘是中央承重
自己擁有一根可 scrub 的 transport 時鐘。時間軸、聲音播放、對拍、「搭 vs 彈」全掛它身上。
一根樑撐全部。

### L2 — 時間 = 一根時鐘 + 兩層 + 一個閥（Ableton 模型）
- 預設 = **彈**：transport 跑、音樂照放、旋鈕當下 override、**無記憶**，底下沒有軸。
- 扳**錄製鈕** = **搭**：此刻一條軸誕生。長度三種 ①音樂長度 ②手動 ③錄到喊停。
  → 需要 **錄製鍵 + 停止/暫停**。
- automation = **A 型**：同一批節點，只記**參數值的曲線**。
  （B 型 = topology-over-time / TimeClips 節點隨時間生死 → 停車 P1）
- 覆寫 = **punch-in**：只寫碰過的，沒碰的留著。
- **儲存** = 凍結「時間→參數曲線」存資料夾；**副本** = 版本分岔。

### L3 — 聲音是兩個世界，原合約誤併成一個
- **世界 2（載入播放）**：自己握時鐘 → 緊密同步免費、可作曲。**柏為的家。**
- **世界 1（現場 BlackHole 實時分析）**：握不到時鐘 → 走**延遲預算**求「人感覺同時」。
  - sample-exact 視覺對拍 = 物理做不到（螢幕 16ms/幀 vs 取樣 0.02ms）。
  - **感知同時 = 做得到**：總延遲壓進 ~20–40ms 融合窗。大鼓（寬頻暫態）好打、
    頻譜/音高（需 FFT 視窗）難打、**真正的殺手在「算圖→上螢幕」present 路徑**。

### L4 — 重心搬移：「視覺 runtime」→「以視覺為主軸的交互圖」
三根地基改動：
1. I/O 邊界 = **可插拔接縫**（聲音/MIDI/OSC/未來協議走同一種「外部源→內部正則值」）。
2. 視覺是**主軸**，非唯一領域（「single-domain visual frame runtime」要鬆綁）。
3. 「never own audio」鐵律**降級**為「external-first，但邊界刻意設計成未來能吸收內部擁有」。
   現在仍不蓋樂器/合成/DSP/音訊輸出引擎，只是不讓「never」滲進架構骨頭。

### L5 — 拱心石 + 參數解析模型（2026-06-09 round 3 釐清，動工前最該清楚的洞）
所有 live source（手/鼓/MIDI/OSC）是同一種公民。參數的有效值，每幀由**唯一一個驅動者**產生：

```
override   — 此刻有 live source 在碰這個參數（黏著式，re-enable 才放）。暫態；非 arm 不存。
  else
binding    — 參數的常駐驅動，三者擇一（互斥）：
               · connection   （上游接線的節點 = 值脊椎既有機制）
               · automation   （scoreGraph 曲線在 playhead 取樣，L10）
               · live-source  （某 live 源常駐綁定，例 Speed ← audio.kick）
  else
constant   — Inspector 預設（Node::params[id]）
```

規則（一次補掉「連線在哪層 / 多源誰贏」兩個洞）：
- **一參數最多一個 binding**，綁新的取代舊的。值脊椎的連線驅動 = `binding=connection`；automation = `binding=automation`；聲音常駐 = `binding=live-source`。三者互斥。
- **解析層不做混音**。要疊加多源 → 接一個 node（Add/Max…），那 node 成為 `binding=connection`。**混音是圖的操作，不是解析層的操作**（同 TiXL：一 input 一上游）。
- **override** = live 源瞬間蓋過 binding；**arm 開** → override 寫進 `binding=automation` 的曲線（punch-in，只寫碰過的，L2/L13）。
- 機制照 TiXL `Slot.OverrideWithAnimationAction`（換驅動者）。

**落地+提速法**：解析段做成**單一「來源註冊 + binding/override 解析」表**，之後聲音/automation/手/MIDI/協議全是「註冊一個來源」= 一行、不是新子系統（憲法鐵律 7）。**難的集中這一段一次解，後面全塌成插件。**
fxTime 精確規則（暫停續跑語義）= **待建 Transport 時去 `external/tixl` Playback.cs 抄準**（非 sound-first 首段所需，延後釘）。

### L6 — 驗收三層（2026-06-09 拍板，柏為修正）
1. **agent 先用 eye+hand 親驗**：agent 有眼睛（截圖/readback）+ 手（注入點擊/鍵盤），要**先自己驅動 app、看到真的成了**——不是只跑測試、不是嘴上說成。這是常規第一關。
2. **機器證據（測試/artifact）** = 防回歸 + 給下一個 agent 接手的護欄。
3. **柏為的手** = 最終權威，但**只對「agent 判斷真有風險」的項目出手**（升級關，非常規）。
原則：別把驗收丟回柏為；agent 自己先驗，risky 的才升級給他。
對齊 northstar「完成定義=柏為親手測得到」+ verify-eye-hand 記憶。

### L7 — Vuo 捨棄（2026-06-09 鎖定）
合約裡所有 Vuo 相關物作廢：`Contract-To-Vuo Proof Gate`、`my_<ExactTiXLName>` custom node、
「Vuo is first host/prototype surface」。TiXL 仍是**語義捐贈者**；宿主層 =
imgui-node-editor 皮 + 自建 Metal 引擎。

### L8 — 兩個時鐘，分層，永不合併（Q2 建議鎖定）
- **FrameScheduler = 牆上時間/幀脈搏**：實時、單調、**永不停**，給 `deltaTime`。transport 暫停它照跑。
- **Transport = 播放頭**：作品位置，play/pause/scrub/loop/record，有長度。
- 機制：每幀 FrameScheduler 出真實 `deltaTime`；Transport 在播就推進 `deltaTime × rate`，
  暫停定住、scrub 跳。transport 被幀脈搏推動，自己保有位置與播放狀態。
- 誰讀哪個：要「真實過多久」→ `FrameScheduler.deltaTime`（粒子/回授/平滑）；
  要「我在曲子哪裡」→ `Transport.position`（automation/聲音播放）。
- 合約動作：Main Clock Contract 長兄弟「**Transport Contract**」。
  **鐵律：牆上時間 vs 播放頭是兩個問題，永不准合成一個值。**
- **TiXL 對照（驗證 + 補強）**：TiXL 有 **4 層時間**——`RunTimeInSecs`(牆上)=我們 FrameScheduler、
  `TimeInBars`(可 scrub 播放頭)=我們 Transport，外加兩個我們沒命名的：
  ① **`FxTimeInBars`**＝播放頭暫停但效果續跑的 idle-motion 時間；
  ② **`LocalTime`**＝可被 compound/TimeClip 重映的區域時間。
  → **建議 Transport 長一個 `FxTime` 兄弟**：暫停＝播放頭定住、但畫面不死（粒子/回授靠它續跑，
  正是 L8 不合併兩鐘的理由）。LocalTime 待有 compound/TimeClip 時再引入。

### L9 — Curve 原語共用、綁定分離（Q3 建議鎖定）
- `Curve` 型別 = 「沿軸取值」原語當作**可接線的值**，下游餵軸取樣。
- automation = **同一原語**綁死 `(參數 × transport 時間)`，歸時間軸層，播放頭求值，punch-in 邊跑邊寫。
- 建議：定義**一個** curve 原語（keyframes/段→值 ＋ 內插 ＋ easing），寫一次；兩種綁定共用。
  硬要求：原語須能**即時 append keyframe**（不只靜態求值）。
- 副作用（好）：型別表「無法可循」的 `Curve` 終於有法 = 「沿軸取樣的值原語」，automation 是首個綁定。

### L10 — automation 目標 = 個別參數，機制照 TiXL（P1 拍板，TiXL 抽取已補實）
時間軸上出現的是「個別節點的某個參數」（A 型參數曲線）。TiXL 實際機制（抄這套）：
- **容器**：`Animator`（Symbol extension）存 `Dict<(節點id, 參數id), Curve[]>`；多分量參數(Vec3)=多條曲線。
  → 正是我們的 scoreGraph(L12)。
- **曲線**：`Curve = SortedList<時間bars, VDefinition>`。VDefinition keyframe 欄位 = 時間 U／值／
  進出內插(Constant/Linear/Smooth/Cubic/Horizontal/Tangent)／切線角／張力／weighted・broken。
  → 對應 L9 原語，**欄位照抄**。
- **綁定**：`Slot.OverrideWithAnimationAction()` 把參數槽原本的 update 動作存起來、換成
  「取樣曲線@播放頭時間 → 寫回 slot.Value」的 lambda；移除動畫=還原原動作。
  → 同 L5 解析堆疊 / L13 override 的機制。
- **求值**：每幀 `EvaluationContext` 把 Playback 時間複製進 `LocalTime`；槽 dirty(`DirtyFlagTrigger.Animated`)
  時呼叫 lambda，`curve.GetSampledValue(ctx.LocalTime)`。
B 型（節點存活）仍停車；TiXL 的 `TimeClip`(時窗 + SourceRange 重映 + speed + layer) 是解凍時的範本。
證據：Animator.cs / Curve.cs / VDefinition.cs / Slot.cs / Playback.cs / TimeClip.cs（external/tixl）。

### L11 — 未來自足拆兩格（P2 拍板）
- ①**載入播放（擁有播放 + 取樣時鐘）= 近期、世界 2 核心**，不是「未來」。
- ②**內部合成/DSP/樂器 = 真正的未來**。現在只保證 `AudioInput` 邊界 **source-agnostic**
  （外部 OSC／外部訊號／內部播放／未來合成全吐同一種 AudioInput）→ 未來加合成 = 多一個 producer。

### L12 — 開第五張圖 `scoreGraph`（譜圖）（P3 拍板）
沿**時間軸**的 authored 真相：transport 設定 + 每參數 automation 曲線 + 存檔版本。
runtimeGraph 在播放頭讀它。**副本/版本 = 同一張節點圖掛多張 scoreGraph**（版本 A/B 共用 patch）。
→ 四圖變五圖：editorGraph / runtimeGraph / commandGraph / collaborationLog / **scoreGraph**。

### L13 — override / re-enable 照 Ableton Live（P4 拍板）
播放中抓旋鈕 → **黏著式 override** 蓋過曲線，按 **re-enable 才放**。
**Ableton 實際機制（F2 已確認）**：「Re-Enable Automation」是控制列上**一顆全域鈕**——
任一參數被 override 它就亮，按下去**一次還原所有**被覆蓋的 automation；Ableton **無**內建單參數
re-enable 鈕。→ 我們照此：**全域 re-enable** 為主；單參數 re-enable 列為「超出 Ableton 的可選擴充」。
機制 = L5 解析堆疊最頂層的 flag+值；re-enable = 清 flag → 參數落回曲線。

### L14 — 現場延遲閘門（P5 拍板）
現場暫態鏈路：**目標 ≤25ms、天花板 ≤40ms**（sound-in → photon-out），當**一級可量測閘門**——
過不了天花板 = 具名失敗。FFT/頻譜類**豁免**但須明列其視窗延遲；present 路徑低延遲是達標前提。
數字**暫定**，真機量過才算實。

---

## Parking Lot（停車區）
- **B 型 topology-over-time**（節點隨時間生死）— 仍停車；TiXL 抽取可能順帶揭示其模型。

## Follow-up 待辦（非停車，是已拍板待落實）
- **F1**：抽 `external/tixl` 參數動畫/時間軸設計 → 落實 L10（**進行中**，Explore agent）。
- **F2**：確認 Ableton override/re-enable 細節 → 落實 L13。
- **F3**：P5 延遲數字真機量測。
- **F4**：把 L1–L14 折進實際合約檔 —— **核心三檔已折入（2026-06-09）**：
  `MY_WORLD_RUNTIME_CONTRACT.md`(v0.2)、`AUDIO_INGEST_CONTRACT.md`、`FRAME_SCHEDULER_CONTRACT.md`。
  殘留清理（次級）：①主合約型別表「Vuo candidate」欄整欄改成 native 型別 ②AUDIO_INGEST
  Non-Goals 那行 sample-accurate 要標 World-1 scoped ③`CONSTANT_IMAGE_CONTRACT.md` 兩行
  my_MainClock/Vuo proof 語言。

---

## Open Questions（仍未碰）
- 無。L6 已於 2026-06-09 拍板（柏為修正為驗收三層）。

---

## Decision Log
- 2026-06-09｜鎖定 L1–L6（時間兩層、聲音兩世界、重心搬移、平台拱心石、evidence 提案）。
- 2026-06-09｜L7 Vuo 捨棄：柏為確認合約 Vuo 部分過期。
- 2026-06-09｜L8 兩個時鐘分層：柏為無想法、採建議（不合併，理由=暫停時幀迴圈必須續跑）。
- 2026-06-09｜L9 Curve 原語共用：柏為要細講、採建議（共用原語 + 即時 append + 補 Curve 型別之法）。
- 2026-06-09｜本檔落地：柏為不確定何時實作，採「ledger 檔 + 記憶指針」雙存。
- 2026-06-09（round 2）｜拍板 P1–P5 + L6：P1 照 TiXL（L10，待抽取）、P2 拆兩格（L11）、
  P3 開 scoreGraph 第五張圖（L12）、P4 照 Ableton（L13）、P5 延遲閘門（L14）、
  L6 修正為驗收三層（agent eye+hand 先驗、risky 才升級柏為）。
- 2026-06-09｜TiXL 抽取完成（Explore agent）→ L10 補實、L8 加 FxTime；證實 L8/L9/L10/L12 與 TiXL 同構。
- 2026-06-09｜F4 折入核心三檔：主合約升 v0.2（重心搬移／五圖+scoreGraph／兩鐘 Clock Contract／
  參數解析堆疊+arm／驗收三層／Vuo 移除／audio 兩世界）、AUDIO_INGEST（兩世界+延遲閘門 L14）、
  FRAME_SCHEDULER（Transport 兄弟+FxTime）。殘留次級清理見 F4。
- 2026-06-09（round 3 壓測）｜**L5 釐清成「binding/override 解析模型」**（一參數一 binding、混音走圖、connection 自動進模型）—
  補掉「連線在哪層／多源誰贏」兩洞；已同步主合約 Port Contract。**建造計畫壓測重排為 sound-first**
  （`plans/2026-06-09-runtime-time-interaction-build.md`）：S0 預清 header 雷 → S1 解析模型拱心石 → S2 ▣ 聲音→參數(M1)；
  transport 退到 S5。加速招：S1 做通用來源註冊表(後面全插件)、每段對抗 selftest 審查。fxTime 規則延後抄 TiXL Playback.cs。

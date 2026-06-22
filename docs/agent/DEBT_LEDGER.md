# DEBT_LEDGER — 債務帳（架構債 + 排修債，讓債在進度表上可被做）

> 柏為 2026-06-20 19:45 下令：「做，讓他可以在進度表上被做」。
>
> **為什麼這份存在**：過去這些債飄在 `spawn_task` 的 suggested-task chip 上。chip 的命運只有兩種——柏為當場點才做，沒點則 app 重啟蒸發（task id 不跨重啟保存）。沒排程、沒 owner、沒觸發條件。證據：`task_602f15ec/2ee58abb/258d9510/3fc122a2` 從 2026-06-11 被抄進每個 Cut 的「排修/柏為域」尾巴，九天十幾次，一條沒做。本帳把它們落地成**有狀態、可被 watcher / sw-batch / 柏為手動撿起來的一行**。
>
> **與 [SEAM_COMPLETION_PLAN](SEAM_COMPLETION_PLAN.md) 的關係（source hierarchy）**：
> - SEAM_COMPLETION_PLAN = **產能線** SSOT（補縫 + 採葉子，往前織網）。
> - 本檔 = **債線** SSOT（架構債 + 排修債，回頭補洞）。
> - 兩條線**踩不同檔、可並行不互撞**：產能線改 registry/runtime ops 加功能；債線拆檔/修 bug/還 parity。一條往前、一條回頭。
> - 衝突仲裁：產能線跑得越快，架構債（破 400 行的檔）長得越多——兩線**對沖**。何時插還債批是柏為的節奏決策（見 §D）。

---

## 0. 狀態語彙

`queued` 排隊未做 · `active` 正在做 · `closed` 做完/已關 · `parked` 暫緩待拍板 · `essential` 判定為本質複雜、豁免鐵律（附理由＝一種「被做」）

---

## A. 架構債（ARCHITECTURE.md rule 4：單檔 < ~400 行）

**硬數據（2026-06-20）：26 / 496 檔破 400 行。** 工法＝sw-batch 承重重構：沿職責縫拆 TU / 資料化，**零行為變更**（`--bite` 全綠 + 一顆 RED 證沒拆壞 + check-arch 綠）。

> ⚠ 兩種醜要分開（persona 鐵律）：**意外醜**（堆積/樣板沒資料化）＝該拆/資料化；**本質醜**（一顆 GPU op ＝ shader+cook+golden+registrar 本來就這麼長）＝硬拆會把肉打散、更糟 → 動作是判定 `essential` + 記錄豁免理由，不是硬拆。單一 op 檔開工前必 scout 判這條。

### P1 — 高槓桿意外醜（明確該拆/資料化，先做）

| 檔 | 行 | 倍率 | 類型 | 狀態 | 做法 |
|----|----|----|------|------|------|
| `runtime/stateful_value_ops.cpp` | 2657 | 6.6× | 拆-ops | queued | ops 集合堆積→沿 op 類別拆多 TU（一族一檔）。最大單塊，最該先拆。 |
| `runtime/node_registry_math.cpp` | 917 | 2.3× | 資料化 | queued | registry 樣板→一張表+builder（**違反 rule 7 資料驅動**，雙重債）。 |
| `runtime/node_registry_point_modify.cpp` | 653 | 1.6× | 資料化 | queued | 同上，registry 樣板資料化。 |
| `ui/keymap.cpp` | 752 | 1.9× | 資料化 | queued | keymap→一張表驅動（加一鍵=加一行資料）。 |
| `runtime/point_graph.cpp` | 746 | 1.9× | 拆-TU | queued | cook driver（point/tex/mesh/cmd/floatlist/string 六條 flow 集中）。**本質複雜**＝沿 cook-flow 職責縫拆 TU（cook_floatlist/string/mesh/tex.cpp），透過 `point_graph_internal.h` 統一 access Impl，保持遞迴 access。string-rail 進來會 ~800，先拆。截圖 chip 即此。 |
| `runtime/value_eval_ops.cpp` | 740 | 1.9× | 拆-ops | queued | value-eval ops 集合，沿 op 類別拆。 |

### P2 — selftest 巨檔（測試碼破鐵律，危害低於產品碼，可緩）

| 檔 | 行 | 類型 | 狀態 | 做法 |
|----|----|------|------|------|
| `runtime/math_ops_selftest.cpp` | 1339 | 拆-selftest | queued | per-op-group 拆。 |
| `runtime/point_ops_selftest.cpp` | 614 | 拆-selftest | queued | 同上。 |
| `app/copy_paste_selftest.cpp` | 443 | 拆-selftest | queued | scout：是否單一場景本質長。 |
| `runtime/transport_selftest.cpp` | 429 | 拆-selftest | queued | 同上。 |
| `selftests.cpp` | 428 | scout | queued | selftest dispatcher，可能是表（資料化）。 |
| `app/soundtrack_selftest.cpp` | 411 | 拆-selftest | queued | 含 task_eb3375a3 的 4x flake harness（見 §C2）。 |
| `app/child_state_selftest.cpp` | 402 | 拆-selftest | queued | 剛破線，低優先。 |

### P3 — 單一 op / header（scout 先判 意外 vs 本質，多半 `essential`）

| 檔 | 行 | 狀態 | 做法 |
|----|----|------|------|
| `runtime/point_ops_rendertarget.cpp` | 595 | queued·scout | 集合 or 單一？ |
| `runtime/point_ops_transformsomepoints.cpp` | 549 | queued·scout | 單一複雜 op，疑 essential。 |
| `runtime/point_ops_rgbtv.cpp` | 538 | queued·scout | RgbTV CRT，本質複雜（memory 證），疑 essential。 |
| `ui/editor_ui.cpp` | 518 | queued·scout | UI，可能可拆 panel。 |
| `runtime/point_ops.h` | 489 | queued·scout | 共享 header，拆風險高（GLOB 撞點），謹慎。 |
| `runtime/point_ops_fractalnoise.cpp` | 466 | queued·scout | 單一 op，疑 essential。 |
| `runtime/point_ops_fastblur.cpp` | 460 | queued·scout | 單一 op，疑 essential。 |
| `runtime/point_ops_rings.cpp` | 456 | queued·scout | 單一 op，疑 essential。 |
| `runtime/point_ops_transformpoints.cpp` | 454 | queued·scout | **含 task_eef5757e 旋轉 bug（見 §B）——拆前先修 bug**。 |
| `runtime/point_ops_drawscreenquad.cpp` | 452 | queued·scout | 單一 op，疑 essential。 |
| `runtime/compound_load.cpp` | 416 | queued·scout | compound 載入，scout。 |
| `runtime/point_graph.h` | 408 | queued·scout | 隨 point_graph.cpp 拆 TU 一起處理。 |

---

## B. 排修 / parity-audit 債（真債，需做）

| id | 內容 | 性質 | 狀態 | 做法 / 入口 |
|----|------|------|------|------|
| ~~**task_32b5b6e5**~~ | ~~string-rail（`b247602`）整個 flat-path-only~~ | ~~🔴 真 correctness bug~~ | **✅ CLOSED `0bb25e2` 2026-06-22** | resident string-wire seam 落地（Cut 102）：extStrOut channel on ResidentNode，resident_eval_flatten 皺褶修，resident_string_cook.cpp NEW，StringLength resident bridge，R-2 LEG20/21 雙腿證。string Phase C NOW OPEN。 |
| task_258d9510 | audit 9 顆已 ship `_multiImageFxSetup` op（pixelate/voronoi/koch/displace/mirrorrepeat/sharpen/chromaticdistortion/detectedges/dither）的 .t3 routing 對不對 | parity audit | queued | 逐顆 .t3 backward-trace（Cut55 trap）。自洽 golden 可能掩蓋 parity bug。 |
| task_3fc122a2 | unwired 2nd-input fallback：sw fork=sample ImageA self-warp，TiXL=黑 null SRV。涵 DistortAndShade+Displace | parity fork | queued | 定 lane-wide convention（對齊 TiXL 黑-fallback or 保留 fork）。開新 multi-image op 前必知。 |
| task_d288a684 | Float-Clamp min>max 行為 | 小 bug | queued | 單顆 op 修 + golden。 |
| task_602f15ec | freshly-spawned node 不進 state.json→scenario「cannot resolve node」cascade（verify/state 層非 cook 層） | verify 基建 | queued | 修 spawn→state.json landing。影響 scenario 測試可信度。 |
| task_2ee58abb | crop teeth 在 `MTL_DEBUG_LAYER=1` 補驗（本機 Metal validation 關，ShaderWrite flag 驗不到） | 驗證補強 | queued | 開 validation layer 跑 crop/mip/fastblur teeth。 |

---

## C. 已關 / 重分類（假債清掉，真債才看得清）

### C1 — closed-by-decision（不是 bug，是已拍板的設計）
- **task_c6a885db** `RgbTV perlin fork` → **closed**。柏為已決策（memory「柏為 你決定+視覺意圖」，commit 脈絡）：CRT glitch 的空間噪聲是 improvement-over-TiXL-WIP（TiXL 該 perlin 節點本身斷開＝WIP/bug）。faithful 數學仍 byte-parity，只 noise distortion 故意分岔。**這不是債，是決策**。

### C2 — 重分類：test-infra 債（非產品 bug）
- **task_eb3375a3 + task_adc40d12**（重複，同一件事）`soundtrack 4.00x chase flake` → 重分類為 **test-harness 債**。根因＝`soundtrack_selftest.cpp:335-384` 的 4x live real-time harness 放大 scheduler jitter，**非產品 bug**。真待辦＝穩定 harness（去 real-time 依賴 or 放寬容差），不是修 soundtrack。狀態 `parked`（不擋產能，run_all 唯一紅、已知）。

### C3 — closed-as-lesson（教訓已內化，非待辦）
- **task_879b5335** dispatch 漏設 `isolation:worktree` 事故 → **closed**。教訓已寫進 memory（[[worktree-base-main-trap]]）+ WORKFLOW，非待辦 bug。

### C4 — closed-as-stale（債帳沒跟上修復，曾誤當活債）
- **task_eef5757e** `transformpoints/randomizepoints Z·Y·X 旋轉序 bug` → **closed＝早已修復**。`871464a`（06-13 17:37）改旋轉序 Z·Y·X→Y·X·Z + 兩檔補多軸 parity golden（37/53/71 對 TiXL CreateFromYawPitchRoll 逐點比；randomizepoints 證偽共病）。`--bite` 綠。**債帳 stale 9 天，2026-06-22 才發現**——曾據此誤建 test-gap 閘 + coverage 工具（已 revert `18ce32c`）。**教訓：信債帳字面、沒先對程式碼；撿任一債前先驗它還活著。**

### C5 — closed-as-fixed（真債，已正式補完）
- **task_32b5b6e5** `string-rail 整個 flat-path-only` → **closed `0bb25e2` 2026-06-22（Cut 102）**。resident string-wire seam 落地：extStrOut channel on ResidentNode（鏡像 extColorOut），resident_eval_flatten.cpp 皺褶修（停止丟棄有接線 String slots），新 resident_string_cook.cpp，StringLength→Float bridge via .size()，frame_cook 接線。R-2 LEG 20+21 雙腿 PROVEN（CombineStrings wire-order + StringLength wired vs const）。refuter MERGE-SAFE（皺褶修 neutralize 非 theater）。**P0 結構閘清除；string Phase C ~34 B2 ops NOW OPEN。**

---

## D. 怎麼被做（自走撿取入口 + 順序）

**撿取入口**：
- 架構債（A）：每檔一張 sw-batch 工單，承重重構工法（§A 表頭）。P3 單一 op 檔先 scout 判 essential/拆。
- 排修債（B）：每條一條 lane，各自修法。

**建議順序（槓桿 × 危害 × 客觀度）**：
1. ~~task_eef5757e~~ **已 closed（871464a 早修，見 §C4）**。~~task_32b5b6e5~~ **已 closed（0bb25e2，2026-06-22，見 §B / §C5）**。P0 結構閘全清，string Phase C OPEN。
2. **A-P1 的 4 顆資料化/拆-ops**（stateful_value_ops 2657 / node_registry_math 917 / point_modify 653 / keymap 752）— 最客觀（行數可量、不需判對錯）、最高槓桿、且 registry 那兩顆雙重違反 rule 7。
3. **point_graph.cpp 拆 TU（A-P1）** — 擋住 point_graph.cpp 繼續長。~~resident string-wire 已 closed（0bb25e2）~~。
4. task_258d9510 / task_3fc122a2（B parity）— 影響已出貨 op 的正確性。
5. 其餘 B + A-P2 selftest + A-P3 scout，隨產能批間隙撿。

**節奏（柏為決策）**：架構債會隨產能線長大。要不要設「破 400 行就 check-arch 紅」的硬閘，把拆檔從可選變必須？這會擋產能（tradeoff），是柏為的板。未設閘前，建議每 N 批產能插一條還債批，否則 A 類只增不減。

---

## E. Conflict Register
- 無雙活躍 lane 衝突。本帳與 SEAM_COMPLETION_PLAN 並行不互撞（不同檔域）。
- 風險：A-P1 拆 `node_registry_*` / `point_graph.cpp` 動的是產能線天天碰的共享檔→**還債批與產能批不可同時跑這幾顆**（會撞 merge）。撿這幾顆時 §F 標 owner。

## F. Session Safety
- 撿 A-P1 共享檔（node_registry_math/point_modify、point_graph.cpp/.h、point_ops.h）的還債 lane 啟動時，須在此標記 owner + 暫停產能線碰同檔。
- 目前：無 active 還債 lane。

## G. Next Handoff Sentence
下個 session 開本檔 §D 順序表，從 A-P1 任一資料化顆起手（task_258d9510/task_3fc122a2 parity 債次之）；動 A-P1 共享檔前先在 §F 佔 owner、暫停產能線碰同檔。**★P0 結構閘全清（2026-06-22）**：~~task_32b5b6e5~~ closed `0bb25e2`（string Phase C NOW OPEN ~34 B2 ops，R-2 各）；~~task_eef5757e~~ closed `871464a`（見 §C4）。產能線進度見 [SEAM_COMPLETION_PLAN](SEAM_COMPLETION_PLAN.md) + lane-state。

# simple_world ⇄ TiXL 全對齊 — 總計劃表（到結束）

> 柏為 2026-06-23 下令:「看完後做一個全部對齊 TiXL 的總計劃，一直到結束。」
> **本檔=頂層路由權威（single progress entrypoint）。** 其餘是 sub-plan:節點/縫=[SEAM_COMPLETION_PLAN](SEAM_COMPLETION_PLAN.md)、債=[DEBT_LEDGER](DEBT_LEDGER.md)、非節點對齊=[alignment/](alignment/README.md)。本檔只當 dashboard + 排序，事實以 git/碼為準。

---

## Current Snapshot（2026-06-23 06:25）

- branch `sw-parity-lane`，HEAD `328b57a`，樹乾淨，check-arch ✅（依賴方向 + 行數閘雙綠）。
- 節點 **364 / ~800**（~45%）。非節點對齊覆蓋已普查 100%（[alignment/_COVERAGE.md](alignment/_COVERAGE.md)）。

## 兩條軌（同時跑，這是全局關鍵）

| 軌 | 內容 | 誰驅動 | 驗證 |
|---|---|---|---|
| **自走軌（autopilot）** | 節點開採 + 引擎縫 + 架構債拆檔 | sw-batch / sw-node-batch（機器自走，柏為不在也跑） | golden + refuter + 行數閘（機器） |
| **柏為軌（attended）** | 非節點子系統:輸出/演出、Variation/Snapshot、UI 範式、檔案/IO/硬體 | 柏為定方向（品味/裝置/判斷） | 多數非 golden 可驗 → 柏為的眼/手重回 parity 鏈 |

★ **核心洞見**:節點在自走軌上會「自己長到 800」（今天又自收 2 縫即證），**所以節點不是瓶頸**。瓶頸是柏為軌——把 simple_world 從「節點編輯器」變成「柏為能上台演出的樂器」。兩軌踩不同檔（自走=runtime ops；柏為=ui/app/output）可並行，**唯一交界=輸出解析度縫動 cook context（見 P1.1，需與自走軌 owner-lock）**。

---

## ★ 最該先做的一件:輸出解析度縫（P1.1）

**理由**(不是憑感覺):
- 節點自走會長完 → 補節點不是最高槓桿。
- 普查證實真正的洞是「**演出/輸出那半**」——沒 Player、沒輸出解析度、沒匯出、預覽固定 512 從不貼合。**有 364 個節點，但柏為還是無法用它演出。**
- 輸出解析度是**承重前置**:`RequestedResolution` 還沒 thread 進 cook context（[alignment/render-output-page.md](alignment/render-output-page.md) 點名）。它一通,**同時解鎖**輸出窗解析度、image-op 跟著解析度走、匯出（需目標解析度）、Player 全螢幕（需解析度）。
- 它是**承重小工程**（引擎管線改，非整個子系統），做完整條演出半身才有地基。

**所以:先把 `RequestedResolution` thread 進 cook context + EvaluationContext，再蓋上面的輸出/演出層。** 這是唯一動 cook 核心的柏為軌項，要跟自走軌排 owner-lock（不可同時碰 cook context）。

---

## 柏為軌 — 階段表（到結束）

### Phase 1｜樂器核心（讓它能上台演出）★最高優先
| # | 項目 | 依賴 | 來源 spec |
|---|---|---|---|
| 1.1 | **輸出解析度縫**（thread RequestedResolution 進 cook）★先做 | 動 cook，owner-lock | render-output-page OUT-01 |
| 1.2 | 輸出窗:解析度選擇器 + Fit/1:1/pan-zoom + **真預覽貼合**（非固定 512） | 1.1 | render-output-page |
| 1.3 | Player / 全螢幕 / Focus Mode（F12 輸出獨佔） | 1.1 | modes / render-output |
| 1.4 | **Variation/Snapshot/Blend 系統**（VJ 現場核心，5 條規格） | command/undo（已有） | missing-subsystems §CORE |
| 1.5 | 截圖 + 影片匯出（+ audio mixdown） | 1.1 | render-output / audio |
| | **里程碑:simple_world = 可上台的 VJ 樂器**（即使節點還沒採滿） | | |

### Phase 2｜手感對齊（讓它「摸起來像 TiXL」）
| # | 項目 | 來源 spec |
|---|---|---|
| 2.1 | 節點分類:namespace/category 軸 + 搜尋 relevancy + 瀏覽樹（**NodeSpec 先加 category 欄位**） | node-classification |
| 2.2 | 畫布:MagGraph 磁吸（節點寬 140/列高 35/共邊貼齊）或至少釘比例 | ui-surface |
| 2.3 | 精密編輯:SliderLadder/RadialSlider + 通用 snapping 介面 + inspector reset-to-default | ui-surface / missing-subsystems |
| 2.4 | 節點本體:inline 值 + 縮圖預覽;**Gradient authoring widget**（柏為域） | ui-surface / missing-subsystems |
| | **里程碑:操作手感 = TiXL** | |

### Phase 3｜完整性（其餘子系統）
| # | 項目 | 來源 spec |
|---|---|---|
| 3.1 | 檔案/專案:.t3-per-symbol vs 維持 .swproj（拍板）+ AssetLibrary 資源瀏覽器 | file-management / missing-subsystems |
| 3.2 | IO 互動層:MIDI variation-controller + LED 回饋 + SpaceMouse（綁 Variation） | missing-subsystems |
| 3.3 | 模式:window layout F1-F10 + timeline 模式 + VJ tapping + 外部節拍同步/BPM 偵測 | modes / missing-subsystems |
| 3.4 | 維運:perf overlay（fps/VSync）+ console + auto-backup + crash-recovery | missing-subsystems |
| | **里程碑:編輯器功能完整** | |

### Phase 4｜硬體/演出整合（階段 6，需柏為裝置）
| # | 項目 | 驗證 |
|---|---|---|
| 4.1 | network/osc/artnet + midi-in + video-in + serial + audio-playback | loopback 可機器驗（network/osc/midi）/ video/serial/audio 需真裝置 + 柏為眼耳 |
| | **里程碑:硬體 VJ 整合 = 全 clone 完成** | |

---

## 自走軌 — 連續背景（與所有 Phase 並行，sw-batch 驅動）

| 流 | 剩量 | 機制 |
|---|---|---|
| 節點開採 | ~436（render 155 / numbers 236 / image 127 / point 135 / io 73 / mesh 51 / field 23 / flow 35，多卡 seam） | 補縫解鎖一島 → fan-out 採 |
| 主線縫 | ~4（texture-array / RWStructuredBuffer / vec-color-field G3-bridge / draw 尾）——source-op、compute-readback 今天已自收 | SEAM_COMPLETION_PLAN 階段 3-4 |
| 大島縫 | render-graph/Layer2d/Execute（155）、flow 控制流（35） | 承重，解鎖最大 |
| 架構債 | grandfather 大檔 ratchet 下降（point_graph.cpp 拆中、value_eval_ops、keymap…） | 行數閘已守住新債 |

★ **自走軌不需柏為**——機器驗證閘扛正確性。柏為只在它撞「TiXL 源碼查無答案的歧義」時拍板。

---

## 完成定義（到結束 = 全 clone）

1. **自走軌**:~800 Operators 全港 + 機器驗證綠;所有檔 ratchet 回 <400。
2. **柏為軌**:Phase 1-4 子系統對齊 TiXL（輸出/Variation/UI 範式/檔案/IO/硬體）。
3. = **完整 TiXL clone + 柏為能原生在 Mac 上演出的樂器**。

## Plan Inventory（sub-plan 角色）
- **本檔 MASTER_PLAN** = 唯一 dashboard + 排序權威。
- SEAM_COMPLETION_PLAN = 自走軌（節點/縫）施工 sub-plan。
- DEBT_LEDGER = 架構債 + parity 真債 sub-ledger。
- alignment/ = 柏為軌（非節點）的 gap+規格 SSOT（6 區 + 普查補強 + missing-subsystems）。

## Session Safety / 並行紀律
- 兩軌可並行但**踩共享檔必 owner-lock**:P1.1 動 cook context、debt 拆 point_graph.cpp/registry = 與自走軌天天碰的檔 → 同檔不可同跑（DEBT_LEDGER §E/§F）。
- 第二 session 寫文件/spec **只寫自己新檔 + pathspec commit，絕不 bare commit**（2026-06-22 血證 [[sw-batch-no-parallel-launch]]）。

## Next Handoff
柏為定 Phase 1 起跑時機。建議從 **P1.1 輸出解析度縫**起手（承重、解鎖整個演出半身）。自走軌（sw-batch）持續背景跑節點+債，不需等。

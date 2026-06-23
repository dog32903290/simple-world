# simple_world ⇄ TiXL 全對齊 — 最快路徑計劃表（全並行）

> 柏為 2026-06-23:「程式碼全翻完了，寫一份全部可以並行、以最快路徑為原則的計劃表。」
> **本檔=頂層路由權威。** sub-plan:節點/縫=[SEAM_COMPLETION_PLAN](SEAM_COMPLETION_PLAN.md)、債=[DEBT_LEDGER](DEBT_LEDGER.md)、非節點 spec=[alignment/](alignment/README.md)。事實以 git/碼為準。

## Current Snapshot（2026-06-23 07:55）
- HEAD `0a73141`，樹乾淨，check-arch ✅（依賴 + 行數閘）。節點 **365/~800**（+SetRequestedResolution）。非節點對齊覆蓋 100%。
- **脊椎 S1 ✅ DONE**（`44234aa`）：context-carried RequestedResolution push/pop，flat+resident，camera aspect tracks，SetRequestedResolution op。harness `--selftest-requestedresolution` PASS/-bug RED，獨立 refuter MERGE-SAFE。**解鎖 L2 輸出窗 + L6 匯出**。NIT 待補：sibling-restore tooth + resident-path golden（production 走 resident）。
- **L1 Variation harness ✅ DONE**（`10e7845`）：springDamp + mixFloat golden（`--selftest-variation`），TiXL Mix/SpringDamp 公式 byte-faithful，獨立 refuter MERGE-SAFE。NIT 待補：asymmetric-weight tooth。scatter RNG deferred。lane 後續=pool/crossfader/UI/document-override。
- **L5/L6 harness 藍圖 ✅ 上磁碟**（`0a73141`，census/L5_IO_HARNESS_BLUEPRINT.md + L6_MAINT_HARNESS_BLUEPRINT.md）→ 下批可直接建。
- **下批候選**：脊椎 S2（render-graph/Layer2d/Execute，解鎖 155 節點）+ 並行 L5 loopback / L6 auto-backup（藍圖已備）/ L1 pool 續 / L3 AssetLibrary。S1 已解鎖 L2 輸出窗 + L6 匯出。
- **★血證（本批 2026-06-23）**：watchdog 30min 閾值對 cook-core build lane 太緊→誤判 S1 死（實際跑 50min，transcript 靜默 33min=長 build 正常）→我派 relay 進同 worktree=雙 driver 險撞。幸最終 worktree state 經我中央 build+--bite 親驗為綠（race 收斂到正確態）。教訓見 memory [[sw-watchdog-cook-core-false-death]]。

---

## 最快路徑原則:一條序列脊椎 + N 條並行 lane

**唯一逼序列的東西 = 動到 cook 核心的檔**（`point_graph.cpp` / `frame_cook` / `resident_eval_graph` / `EvaluationContext`）。所有承重縫 + 拆檔債都擠在這幾個檔上 → 彼此不能並行 = **關鍵路徑（長極）**。
**其他全部踩不同檔域 → 現在就並行開跑，不必等節點大縫。**
每條 lane 的第一步 = **蓋自己的驗證 harness（golden / eye-hand / round-trip），蓋完即自走**（柏為軌塌縮成「裝置 + 品味簽收」殘留，見底）。

---

## ⛓ 關鍵路徑 — Cook-Core 序列脊椎（一條 worker，不可內部並行）

全部動同一批 cook 核心檔，只能照解鎖價值排序：

| 序 | 縫 | 解鎖 | 為何在脊椎 |
|---|---|---|---|
| **S1** | **輸出解析度縫**（RequestedResolution→cook+EvalContext） | 解鎖 L2 輸出窗 + L6 匯出 | 動 EvaluationContext，最先（解鎖最多下游 lane） |
| **S2** | **render-graph / Layer2d / Execute 縫** | 解鎖 155 節點（→餵 L4） | 動 cookCommand 核心，最大島 |
| **S3** | **flow / 控制流縫**（context-var + Execute/Loop） | 解鎖 35 節點（→餵 L4） | 動 eval 核心 |
| **S4** | 殘餘 infra 縫（texture-array / RWStructuredBuffer / vec-color-field G3-bridge）+ **point_graph.cpp 拆檔債**（乾淨點交錯） | 解鎖剩餘島 + 還債 | 同檔，跟 S1-S3 同一 worker |

> 這條是最短完成時間的下限。其餘 lane 全繞著它並行。**S1 先做**——它解鎖最多並行 lane。

---

## ∥ 並行 lane（全部現在開跑，踩不同檔域，互不撞）

| Lane | 檔域 | 內容 | harness（第一步） | 依賴 |
|---|---|---|---|---|
| **L1 Variation/Snapshot** | 新子系統 + app/document override | VJ 現場核心:snapshot 抓取/過濾 + crossfader + Mix 插值 + 觸發（5 規格已備） | golden 對 TiXL Mix 公式 + spring-damp 常數 | **無**——現在開 |
| **L2 UI 範式** | `ui/` | MagGraph 畫布 + 分類瀏覽 + 精密編輯(SliderLadder) + inspector + Gradient widget | eye-hand 斷言（寬度/排序/snap） | 分類←NodeSpec.category 欄位(先加);輸出窗←S1 |
| **L3 檔案/專案** | `app/` document | 存載 + AssetLibrary 瀏覽器 + .t3/.swproj 拍板 | round-trip golden（存→載→相同） | **無**——現在開 |
| **L4 節點開採** | `runtime/` op 葉 | 已解鎖島 fan-out（numbers/image/point/mesh/field） | 已有（golden+refuter） | 持續中;render/flow 島←S2/S3 |
| **L5 IO/硬體** | `platform/` | network/osc/midi（loopback 可機器驗那半） | loopback golden（虛擬 MIDI/localhost UDP） | **無**——現在開（裝置半=殘留） |
| **L6 音訊匯出+維運** | `platform/audio` + `ui/` | audio mixdown/錄製/波形 + perf overlay/console/auto-backup | 檔存在/round-trip | 匯出←S1;perf/backup 現在開 |

> L1/L3/L5/L6 **零依賴，立刻開**。L2 大半立刻開（除輸出窗）。L4 已在跑。

---

## 🔗 同步點（少數跨 lane 依賴，就這幾條）
- L2 輸出窗 UI ← **S1**（解析度縫）
- L6 匯出 ← **S1**
- L4 render/flow 島開採 ← **S2/S3**（但 L4 另有大把已解鎖島可先採，不空等）
- L2 分類 ← **NodeSpec.category 欄位**（L2 第一個 micro-step，graph.h 加欄位=additive，與 L4 讀 NodeSpec 不撞）

## 👁 柏為殘留（**不在關鍵路徑**，異步簽收）
1. **真裝置驗證**:實體 MIDI/video-in/serial/audio-out（loopback 之外那半）。
2. **手感最終簽收**:UI 過了功能 golden 仍要你的眼判「像不像 TiXL」（golden 抓不到的主觀層）。
→ 這兩塊隨時插入，不卡任何 lane 前進。

---

## 並行紀律（血證，不可省）
- **L4 + S* 共享 `point_graph.cpp`/registrar/cook 核心** → 拆檔債(S4)與節點開採(L4)**動同檔不可同跑**（DEBT_LEDGER §E/§F，owner-lock）。S1-S4 是同一條序列 worker 正因如此。
- **跨 session 寫碼/文件**:各 lane 自己 worktree 或只寫自己檔 + **pathspec commit，絕不 bare commit**（2026-06-22 血證 [[sw-batch-no-parallel-launch]]）。
- 每條 lane 一個 orchestrator/owner;NodeSpec.category 那種 shared-header 加欄位由單一 owner 統一加（非並行撞）。

## 完成定義（全 clone）
1. Cook-core 脊椎 S1-S4 補完 → 所有島解鎖。
2. L1-L6 各自 harness 綠 + 自走採盡（~800 節點 + 6 子系統對齊）。
3. 柏為殘留簽收（裝置 + 手感）。
4. 架構債 ratchet 回 <400。
= **完整 TiXL clone + 柏為原生 Mac 演出樂器**。

## Plan Inventory
- 本檔 = 唯一 dashboard + 並行排序權威。
- SEAM_COMPLETION_PLAN = 脊椎 S* + L4 施工 sub-plan。DEBT_LEDGER = S4 拆檔 + 真債。alignment/ = L1-L3/L5-L6 的 spec SSOT。

## Next Handoff
**現在就能開的並行 lane:L1(Variation)/L3(檔案)/L5(IO loopback)/L6(perf+backup)/L2(除輸出窗) + 脊椎 S1(輸出解析度縫)。** L4 持續背景跑。每條 lane 第一步 = 蓋 harness。柏為定要不要一次全開，或先開幾條。

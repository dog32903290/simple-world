# simple_world ⇄ TiXL 全對齊 — 最快路徑計劃表（全並行）

> 柏為 2026-06-23:「程式碼全翻完了，寫一份全部可以並行、以最快路徑為原則的計劃表。」
> **本檔=頂層路由權威。** sub-plan:節點/縫=[SEAM_COMPLETION_PLAN](SEAM_COMPLETION_PLAN.md)、債=[DEBT_LEDGER](DEBT_LEDGER.md)、非節點 spec=[alignment/](alignment/README.md)。事實以 git/碼為準。

## Current Snapshot（2026-06-23 12:12）
- HEAD `c0e78b7`，樹乾淨，check-arch ✅（ratchet OK）。節點 **374/~800**（+Loop）。`--bite` PASS=383 NO-BITE:[]（唯一紅=soundtrack 預存 flake task_eb3375a3，未碰 audio）。非節點對齊覆蓋 100%。
- **🏁 脊椎 S3 flow seam COMPLETE**（S3a context-var bridge + value↔command live-read + S3b Switch + S3c Loop，全 flat+resident machine-verified）。**脊椎只剩 S4（殘餘 infra texture-array/RWStructuredBuffer/vec-color-field + 拆 point_graph 債）。S1+S2+S3 三大脊椎完成 → cook-core 主結構就位。**
- **🏁 脊椎 S3 context-var flow 真 work（hollow 關閉）**：S3a bridge（`1dac633`）+ **value↔command live-read seam**（`b59bd3d`，thread-local LiveCtxVarScope，value-rail GetFloatVar/GetIntVar 在 cook 期讀 live SetVarCmd scope，flat+resident）+ GetIntVar trunc fix（`d588b12`，refuter 抓的 BLOCK）。golden 3 teeth（A probe / **B value-rail-GetVar-under-scope=700** / C GetIntVar-trunc=7）× 2 leg。refuter 雙波驗 make-or-break（thread-local 單線程安全 / memo bypass 無污染 stable-ptr / off-scope byte-identical）。+ **S3b Switch ✅**（`7754d99`，collector cook 只 index-th，flat+resident，resident primary+extraConns index 經 refuter 獨立確認對齊 flat wire order，switchSelectIndex sentinel-before-mod faithful Switch.cs）。+ **S3c Loop ✅**（`c0e78b7`，per-iteration re-cook keystone：index/progress write + cook 子樹 N 次 concat，memo bypass 經 refuter 證 load-bearing，GPU-buffer aliasing 經 12-file 審計確認真窄非 hollow=12/12 TiXL Loop 是 by-value variation）。**🏁 S3 COMPLETE。** NIT：nesting untested（follow-up tooth）+ registry default var-name 便利非 faithful-empty。
- *(S3a/live-read 細節已併入上方 🏁S3 行)* fork note：load path importer 要把 SubGraph-connected SetFloatVar route 到 Cmd type（parity 待補）。ratchet debt task_0b554339。
- **∥ L3 asset-index ✅**（`cf16065`）+ **relink mutation ✅**（`18673b1`，`relinkAsset` 改寫 missing key across instances + soundtrack，golden 4 斷言 + `-bug` RED）：列舉 `Lib:` keys（dedup）+ missing-asset predicate（resolver fn-ptr 注入 headless 可測）。後續=UI/command wrapper(ui/) + undo-able Command form + wire 到 production load missing-asset surface。
- **🏁 脊椎 S2 render-graph 三階全完成（S2a collector + S2c layer-compose + S2b Group SRT）→ 解鎖 155 節點 render/Execute 島大量開採。脊椎只剩 S3(flow) + S4(殘餘 infra + 拆 point_graph 債)。**
- **∥ L4 render-island 葉×3 ✅**（`9273feb`）：**RotateAroundAxis/Shear/Transform**（消費 S2b group-stamp，flat+resident golden，9 teeth）。refuter **抓到真 BLOCK**=RotateAroundAxis 預設 Axis 硬編 (1,0,0) 但 TiXL .t3 是 (0,0,1)=Z（production 未接 Axis 時繞錯軸，golden 顯式設 Axis 遮蔽）→已修白照 TiXL + 加 default-Axis guard probe；Transform golden 只測 translate→補 Pivot≠0/rotZ probe(moved(0.2,0.9) order load-bearing)。Shear MERGE-SAFE。具名 fork(axis 不 renorm/deg→rad/gizmo drop)。**教訓:預設值=會改變 render=品味=照 TiXL(同 KeyColor)；golden 必測 op 比基底多的東西，否則遮蔽 bug。**
- **∥ S3 flow blueprint ✅ 落盤**（`903e93f`，census/S3_FLOW_BLUEPRINT.md）：真縫=**value-rail⇄Command-rail 橋**（`CmdCookCtx` 加 `ContextVarMap*`，context-var 半邊已 shipped）。分階 **S3a**(bridge+SetVar-SubGraph keystone)→**S3b**(Switch 選擇)→**S3c**(Loop re-cook)，帶 flat-resident mirror checklist。下批脊椎照此即時開。
- **⛓ 脊椎 S2a ✅**（`3ae09e5`，含於 `897885e`）：**MultiInput Command collector + Execute keystone**（per S2_RENDERGRAPH_BLUEPRINT）。Execute.CollectedInputs 收集 MultiInput Commands(非新 draw pipeline)，新 `point_ops_execute.cpp`(197行)，closed-form pixel golden selftest。**Execute 島 keystone 在 → render/Execute 島開始可採**。
- **⛓ 脊椎 S2c ✅**（`c3c672d`）：**layer-compose golden**（`--selftest-layercompose`，flat+resident，Normal=GREEN/swap=RED/Additive=YELLOW 自 TiXL `Execute.cs` draw-order + `DefaultRenderingStates.cs` 混合公式推導，`-bug` RED）。**+抓並修 cook-core 真 bug**：resident `cookCommand` 缺 Texture2D input branch(flat 有/resident mirror 從沒加)→production resident path 每個 textured-quad layer 畫黑;resident-leg golden 抓到、mirror flat FORK#1 修好(+8)。refuter MERGE-SAFE(7 面全清，cook-core +8 忠實無 regression)。ratchet 601→609(honest，+8 irreducible，拆檔債 task_f0fa9aa3)。
- **⛓ 脊椎 S2b ✅**（`629c135`）：**Group SRT transform-context push**（`--selftest-group`，flat+resident，T1 translate/T2 scale，`-bug` 兩 leg flip）。Group=Execute(S2a collector 重用)+per-item SRT stamp(Camera-op precedent，SW retained-mode 無 scope stack)。`childO2W·groupSRT` 對 TiXL `Group.cs` row-vector 慣例(refuter 數值證明 origin→0.5 非 mirror 0.15，golden discriminating)，YawPitchRoll(Y,X,Z)/quaternion element-exact，nesting innermost-first，resident 真共用 executor multiply。refuter MERGE-SAFE(9 面全清)。append-only(hasGroup=false→byte-identical)。ratchet 544→553+667→680(拆檔債 task_66b888c7)。Color-drop=既有 v1 no-op fork(NIT)。
- **∥ L4 葉子 +2 ✅**（`897885e`/`82bdd5f`）：**Grain**(image generate noise，Grain.hlsl 1:1，Amount=0 passthrough golden，Time=0 headless fork 具名)+**KeyColor**(image color，重用 ChromaKey kernel，refuter 手算驗證重用語義無偷換，golden greenA=0/redA=255)。refuter MERGE-SAFE；NIT(Key 預設 green)已修白照 TiXL KeyColor.t3。image-filter 家族乾淨葉子近採盡(剩全卡 multi-image/gradient/dynamic-hlsl 縫)。
- **★血證（本批 2026-06-23 二漏接）**：①**write-only lane 的產出 agent 只寫不 commit**(Lane B build 寫 Grain 沒 commit + fixer 只 commit KeyColor)→Grain 三檔差點孤兒。教訓=合流時 orchestrator 必逐 worktree `git status` 核對未追蹤檔，不只信 branch log。②**cook-core build agent 在等 --bite 時 came-to-rest 沒留 build 證據**(SendMessage subagent-resume 此環境無工具)→orchestrator 親驗收尾(cherry-pick A+B 同場一次增量 build 18 秒驗綠+selftest+check-arch)。工法定式仍成立:寫-leaf→refuter→中央合流，但**收尾親驗是 orchestrator 不可外包的硬步驟**。
- **⛓ 脊椎 S1 ✅**（`44234aa`）：context-carried RequestedResolution push/pop，flat+resident，camera aspect，SetRequestedResolution op。refuter MERGE-SAFE。**解鎖 L2 輸出窗 + L6 匯出**。NIT：sibling-restore tooth + resident-path golden。
- **∥ L1 Variation harness ✅**（`10e7845`）：springDamp + mixFloat golden，TiXL 公式 byte-faithful，refuter MERGE-SAFE。
- **∥ L1 pool + crossfader ✅**（`b970758`）：snapshot pool(delete-then-capture/EnabledForSnapshots filter/scan-by-activationIndex)+2-way crossfader(midi/127→blendAmount，springDamp 20/(1/60)，|vel|<0.0005 commit+flip)。golden mix=0/0.5/1 對 TiXL `Lerp(a,b,t)`(per type，int 截斷)+pool overwrite，`-bug` 4 teeth RED。refuter MERGE-SAFE(line ref 逐一開檔驗、golden 重導)。3 具名 fork(numeric-only/in-memory/direct-apply，皆延後子集不改數值)。**2 NIT 標給下游 batch**：①untracked-param→DefaultValue blend(TiXL 主動拉向預設，leaf 留著不動)→document-override batch ②startBlendingTowards 歸零 velocity(TiXL 只 target 變時重置 dampedWeight)→live spring loop 接線。後續=document-override/on-disk JSON pool/scatter RNG+N-way weighted/SnapshotActions slot 語義。
- **∥ L3 檔案 lane 開張 ✅**（`829a698`）：調查發現 round-trip 基礎設施已存在（`compound_save_selftest` .swproj byte-stable + L6 `auto_backup_selftest`）→未重造，填真 gap=**image asset-reference round-trip**（`--selftest-asset-ref`，LoadImage `Lib:` key+float params 存真 .swproj→載回 byte-stable + CJK key + default zero-churn，`-bug` 篡改 key RED）。無新 fork(騎既有 asset-model fork)。風險低跳過獨立 refuter，orchestrator 親驗 selftest+--bite NO-BITE:[]。後續=non-UI asset-index data model(列舉 `Lib:` keys + missing-asset predicate，asset browser ui/ 層的資料底座)。
- **∥ L5 IO loopback ✅**（`5364ff8`）：OSC（localhost UDP）+ virtual CoreMIDI machine-verified half。refuter 抓到 MIDI channel off-by-one（0→1-based）已修 MERGE-SAFE。柏為殘留=真裝置（controller/phone）走同 decode path。後續=LiveSource→graph 綁定。
- **∥ L6 auto-backup ✅**（`dea9155`）：refuter BLOCK（單檔 lossy）→ fixer 修 faithful（asset-sibling bundle soundtrack + 重啟安全 disk-derived index + ms-timestamp + .pending atomic）→ re-refute MERGE-SAFE。+ **retention+restore ✅**（`96ef27b`，binary-thinning survivor-set bit-exact 268K samples 0 mismatch + crash-recovery restore + soundtrack path-relink，refuter MERGE-SAFE；NIT=golden 測 one-shot 非 production-incremental→補 tooth task_6e64a956）。剩 perf_overlay→chip task_8a55df9b、-minimal toggle(單檔 N/A)、startup crash-recovery prompt(ui/ 域)。
- **∥ S2 藍圖 ✅ 上磁碟**（`f895c65`，census/S2_RENDERGRAPH_BLUEPRINT.md）：真縫=**MultiInput Command collector**（Execute.CollectedInputs），非新 draw pipeline。分階 S2a（collector+Execute keystone）→S2c（layer-compose golden）→S2b（Group SRT）。
- **下批候選**（S1+S2+S3 三脊椎完成，重心轉 **L4 大量開採**）：**L4 採 S2/S3 解鎖島**——flow Command ops 殘值（~15）+ render/compose/Execute 葉（從 OP_BACKLOG），render/flow 葉動 register shared 檔→序列合流或自登記化。並行非-register lane：**L1 document-override**（接 pool→running graph via ChangeInputValueCommand，含 2 NIT untracked→default/velocity-reset）/ **L5 LiveSource-bind** / **L2**（ui/ 與 `ui/tixl-node-skin` worktree 協調）。脊椎 **S4**（拆 point_graph 債——point_graph.cpp 836 行/resident 678 行超 cap，task_f0fa9aa3/66b888c7/0b554339；+殘餘 infra texture-array/RWStructuredBuffer/vec-color-field）owner-lock，與 L4 採葉撞 point_graph/register 不可同跑。**建議下批：L4 大量並行採（自登記葉）+ 一條 L1/L5 非-register lane；S4 拆檔挑 L4 不踩的時機單獨序列做。**
- **★血證（本批 2026-06-23）**：watchdog 30min 對 cook-core build lane 太緊→誤判 S1 死（實跑 50min，靜默 33min=長 build 正常）→派 relay 撞同 worktree 雙 driver，幸中央 build+--bite 親驗收斂為綠。閾值已改 55min。詳 memory [[sw-watchdog-cook-core-false-death]]。
- **★工作流摩擦（柏為 2026-06-23 要改工作流，正寫 `docs/agent/PLAYBOOK_SYSTEM_PLAN.md`）**：①Plan agent 無 Write→藍圖落盤要用 Explore 或 orchestrator 代存；②worktree 不 symlink external/tixl→build agent 驗不到 TiXL，refuter（主樹）才是真 parity 閘；③refuter 是真價值閘（本批抓到 L5 channel + L6 lossy 兩個真 BLOCK，golden 自洽過不了 refuter）。

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
**脊椎進度:S1✅ S2a✅(keystone) → 續 S2c/S2b。** 並行可開:L4(render/Execute 島已由 S2a 解鎖,+ point/mesh/field 葉;image-filter 近採盡)/L1(Variation pool/crossfader/UI/document-override,harness✅)/L3(AssetLibrary,harness 待蓋)/L5(LiveSource→graph 綁定,loopback harness✅)/L6(Reduce/Restore backup 續,perf overlay)/L2(MagGraph/分類/SliderLadder/Gradient,**ui/ 域與 `ui/tixl-node-skin` worktree 協調**)。每條未蓋 harness 的 lane 第一步=蓋 harness。下批由 orchestrator 自選未阻塞+不撞檔組批。

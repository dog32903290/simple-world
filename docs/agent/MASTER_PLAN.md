# simple_world ⇄ TiXL 全對齊 — 最快路徑計劃表（全並行）

> 柏為 2026-06-23:「程式碼全翻完了，寫一份全部可以並行、以最快路徑為原則的計劃表。」
> **本檔=頂層路由權威。** sub-plan:節點/縫=[SEAM_COMPLETION_PLAN](SEAM_COMPLETION_PLAN.md)、債=[DEBT_LEDGER](DEBT_LEDGER.md)、非節點 spec=[alignment/](alignment/README.md)。事實以 git/碼為準。
> **開頭定位**：跑 `tools/sw_status.sh`（三區 ① LIVE git+census 現測 / ② STAMPED@結帳 bite / ③ HAND 手寫接力）。**結帳**：`sw_status.sh --stamp <bite PASS>` 蓋章 + 手寫更新下方 Active Lane/Conflict/Next + `--check` 過閘。舊 snapshot 在 [MASTER_PLAN_HISTORY.md](MASTER_PLAN_HISTORY.md)。

## Current Snapshot
<!-- sw_status:begin （機器塊：結帳時 tools/sw_status.sh --stamp <bite PASS> 寫入；勿手改） -->
HEAD: 96e4923
DIRTY: clean
CENSUS: 433 / 749 done
BITE: 485 PASS | NO-BITE=[detectbpm]
STAMP_AT: 2026-06-27T04:28
<!-- sw_status:end -->

- 引擎 clone **57%（427/749）**。★**「clean-leaf 採盡」兩度被推翻**：(1) S2/S3 脊椎查出早已蓋好+golden 綠→單輸入 texture-rail 葉子可採；(2) **multi-image seam 也早已建**（gather 綁 4 input texture，Blend/Displace/Combine3Images 已證）→ **fixed-port 多輸入 op 是乾淨葉子**。**本 session 六批已採 10 顆 image 葉子 + 1 小 seam**（batch1 `627458b` Mandelbrot+DepthBuffer、batch2 `fc92eca` ImageLevels+2×Ryoji+HoneyComb、batch4 `9fa193e` CombineMaterialChannels、batch5 `646544d` HSE+MosiacTiling、batch6 `0fd14a4` MultiInput<Texture2D> gather 擴充 + PickTexture）。**★方法論血證（4-5 次）：census/scout 系統性把「已建的 seam」誤報 gated（S2/S3 脊椎、multi-image gather 都早已建）→ 別信 census done/todo，ground-truth=讀 cook path（派 Plan agent 深讀，不是 Explore census）。** 選葉子要開 .hlsl 親看（單 pass？非 compute-reduction？非 compound？fixed-port？）。
- **柏為-absent 自走可採 = 第三軸體驗復刻尾**（[EXPERIENCE_PARITY_PLAN](EXPERIENCE_PARITY_PLAN.md)：純皮 Tier1 / Output O3 / 維運），eye-hand 驗、不碰 cook-core。
- 本 session 落地：**field 紅修**（`644d100` AudioReaction 救回）+ **quick-add 型別色**（`e427d55`）+ **ui_census 校正×3**（`56a2057`/`708b253`/`7765469`）+ **out-snapshot-png**（`5a9a51f`）+ **★S1 輸出解析度縫端到端完成**（柏為 23:35 授權：`1b53b12` cook-core override hook + `a93f2dc` UI 選擇器,皆 refuter 8/8 SURVIVES）→ B 軌 out-resolution-selector 自動 DONE,B 軌 16→19。

## Active Lane
**none（2026-06-27 04:28 matrix-port-type value-rail seam 落地，HEAD `96e4923`，--bite 485 / FAILED=[]，census 433/749）。柏為「你決定+不停+大接縫自己決定」stance 自走中。**
- **✅ matrix-port-type seam（value-rail）**（`96e4923`）：value-op rail 只吃 Float 埠→Mat4 忠實表示＝**16 連續 Float 埠 row-major**（仿既有 Vec3-as-3-Float fork），**純 additive 零 cook-spine 觸碰、零共享檔**（兩新 leaf `value_op_mulmatrix.cpp`/`transformvec3.cpp`，CMake glob 自動）。**刻意不加字面 `Matrix4x4` dataType**（那會拖新 cook 縫=owner-lock）。MulMatrix（A×B）+ TransformVec3（先 transpose 後 transform，column-vector）忠實 TiXL System.Numerics。Opus refuter MERGE-SAFE：親寫 numpy repro 獨立重算每 golden 數=predictive 非 circular，transpose/order/row-major/32-input-cap 全正確、RED leg 真咬。**★leverage 誠實校正：交付 2 op（value-rail matrix 採盡），非 scout 估的「camera family」**——camera family 需字面 matrix cook-type（owner-lock 縫 deferred）。
- **★純自走葉子＝0（batch8 實證）**：rail 硬約束＝value rail 只 Float/Color、eval 固定 16B、point rail GPU-dispatch→318 顆未做 op 全 seam-gated。葉子階段結束，進 seam 階段。
- **★三 stale seam-map 標籤今晚揪出（方法論血證+3）**：CameraWithRotation「零縫葉」實需 worldToCamera 縫／**field-raymarch「8 op 解鎖 39 SDF」實已全建**（`89ea6a2`，39 SDF production-wired 可見，**別重開**；殘 SDFToColor+analyze 小 additive 非 render 縫；chip 待修 seam_map kind）／46 leaf-ready 實 0。**規則：seam_map/census 數字一律先 ground-truth scout 再投。**
- 前批：維護加固 工作1（gate census `4e68d03`）+工作2（島7 拆檔 `6782e32` refuter 5/5 resident byte-identical，淨−299）+folder-package（`8a817e5`）+ColorThemeEditor（`63b35db`）詳見 history。工作3（決定性 export 帶閘）綁 MV C 塊路2 未來做。

## Conflict Register
- **（已解）MV 工單 +66 行收進 main（`2765fe4`）**：先前 batch-4 期間此改動出現在 main checkout，我誤判為 ColorThemeEditor fixer 越權→park 到 review 分支；**柏為 01:51 澄清＝另一 session 在同 checkout 寫的合法 post-parity 工單**（[[sw-batch-no-parallel-launch]] 雙 session 同 checkout 情境），授權收。review 分支已刪。**教訓：main checkout 冒出任務範圍外改動，可能是平行 session 不是自家 agent——但處理法相同：`git diff --stat` 核範圍 + pathspec commit（這次救了沒混進 theme 批）。**
- 工作1/工作2 零未解衝突（島7 單線 cook-core 已收，tree clean）。**無未解衝突**。
- 非阻塞 follow-up：gate_census absent-gate 改 fail-safe（低）；FP safeStem 外部 `:`-id disambiguator；CTE 剩無-sw-consumer 欄（Widget*/Status* 無 sw 渲染對象→theme 它們是 dead weight，等 sw 真 render 那些 UI 元件再接）；SP 補 LookAt 斷言；RO 補 disk-corrupt leg。皆低優先。
- （更早已解項移 history：soundtrack flake `cd47f72`/field scn `644d100`；chip `task_eb3375a3`/`task_2fc4a37a` 可關，`task_9d081266`=detectbpm NO-BITE 待修。）

## Session Safety
- **★結帳前必 rebuild 再 --bite（本批踩）**：`tools/run_all_selftests.sh --bite` 跑的是 `app/build/simple_world` **既有 binary**（只有「binary 不存在」才報錯，stale binary 靜默跑舊）→ merge 完直接 --bite 會拿到舊數字（本批看到 474 該是 478，差點誤蓋章）。合流後順序＝`cmake --build app/build -j8` → `--bite` → `--stamp`。
- 另有 parked worktree `.worktrees/ui-node-skin`（branch `ui/tixl-node-skin` @ `fd542f5`）= 舊 L2 node-skin lane，未合流，**別當死的清掉**。
- 柏為 2026-06-26 01:09 回場下令「需我在場的工作你先做、不等我、自走到我喊停」→ 結果 S2 脊椎查出早已蓋好，present-requiring 阻塞自動消解 → 轉做 S2 殘餘 image-leaf fan-out（absent-safe）。**S2/S3 脊椎已建，不必再等授權重開**；真正剩的 owner-lock 縫＝S4 殘餘 infra（texture-array/RWStructuredBuffer/vec-color-field）+ point_graph 拆檔債、camera3d value-output Phase2/3、point-sprite render 縫——這些才需柏為在場。
- **worktree 隔離教訓**：派並行 build agent 要在 Agent 呼叫上設 `isolation:worktree`；只在工單寫 `agent_worktree_setup.sh` 而不設 flag → 無 worktree 可 ff → 全跑進 main checkout 共用樹（本批 4 agent 都落 main，幸好 self-registering 零共享檔+建置非同時才無損）。見 [[worktree-base-main-trap]]。
- **拆檔狀態（ratchet headroom）**：✅`output_window.cpp`（301）/✅`editor_ui.cpp`（263）/✅`document.cpp`（`b053941` 400→**31**，解鎖 folder-package-save，chip `task_19264e66` 可關）/✅`point_graph.h`（已拆 cook_ctx）都有餘量。**仍卡無 headroom**：`point_ops.h`@553 grandfather cap（SP 本批靠 trim 註解才容下，再加 point-op 前須拆或騰空間）。**拆檔紀律：拆+裝/降 ratchet 綁一起 [[gate-or-it-rots]]。**
- **merged worktree 都已清**（本 session 十 lane 的 worktree+branch 已清；`review/mv-plan-fixer-addition` 已收進 main 並刪）。**保留** `.worktrees/ui-node-skin`（未合流，故意）。其餘 `worktree-agent-*` 老分支動前先查未合流。
- **★build-storm + 看門狗假死教訓（本 session 兩度踩）**：並行開 4-5 條 build lane → CPU/IO 爭用讓 `--bite` 單一長 bash 呼叫凍住 transcript 20-30 分（**正常、非死**）→ 25 分看門狗誤判死亡。soundtrack lane 假死、split lane 假死還害我派接力 agent（幸好接力工單有 git-state guard 擋住雙-driver——split 其實只是爭用下跑完最後一哩）。**規則：並行 build lane ≤2-3 條；跑 --bite 的 lane 看門狗閾值放 45 分；判死認真死（process 沒了 + 等 60-120s 再 stat），STALE≠死；接力工單必含「git 狀態非預期就 STOP」guard 防雙-driver**。見 [[subagent-death-detection]]/[[sw-watchdog-cook-core-false-death]]。
- **eye-hand 截圖被面板遮擋擋住（本 session raymarch 踩）**：spawned node 生在浮動 Output/Inspector 面板下方→hand 拖線點到面板不到 pin，eye-hand 視覺驗證做不出來。這是 orthogonal UI 問題非 seam；production-path golden（cook→`pg.target()`，與 OutputWindow 同源 texture）是 load-bearing 證明，eye 截圖 best-effort。值得開 chip 解（移開/可關面板 or spawn 到 clear canvas）。

## Next Handoff Sentence
下個 `/sw-batch` 開頭先跑 `tools/sw_status.sh` 定位（步驟 1 硬規）。HEAD `96e4923`，--bite 485，census 433。**★狀態：純自走葉子=0（實證）+ 維護加固 工作1+2 + matrix value-rail seam 完成→進「大 seam 階段」。柏為「你決定+不停+大接縫自己決定」續跑。**
- **★方法論硬規（今晚 3 次踩 stale label）**：任何 seam 開採前**必先 ground-truth scout**（field-raymarch 實已全建/CameraWithRotation 實需縫/46 leaf-ready 實 0）。seam_map「解鎖量」估計**不可信**（matrix 估 camera-family 實交付 2 op）→scout 報真 op 數 + autonomous-safe? + 是否已部分建，再投。
- **下一 seam 候選（autonomous-safe 優先）**：**keyframe-anim**（13 估，timeline 動畫=MV 核心相關，`app/src/app/animation_commands.cpp` 已存在恐已部分建→先 scout）/list-state（12 估，colorlist/stringlist cook 已部分 seed）/flow-var（12，context-var 剩餘通道）。**采法**：clean 的用 golden 對 TiXL 公式 + refuter。
- **★大 prize 但需柏為 greenlight（owner-lock/多 session）**：**GPU-compute-kernel 縫**（解鎖 20+ point 核心家族 SpherePoints/GridPoints/SelectPoints/DrawPoints——node-graph 視覺工具的核心，但 Metal compute infra 巨大）/字面 matrix cook-type（camera family）/string-value（**前置 gate：先 close resident-string-wire `task_32b5b6e5`**）。**這些 routing-priority 真的看柏為 MV 需求，他回來定。**
- **B 軌剩 16 gap**（`ui_census --gaps`）：多為 render-output（卡 output_window cook-core/平台 encoder）或 missing-subsystem（io-editor/audio-export/value-snap 抽介面）——多需 attended 或 decision。純皮零星。
- **需 attended/cook-core**：CameraWithRotation（explicit-worldToCamera 縫）/S4 殘餘 infra/各 seam-build。
- **★compound-graph-host = 假縫 debunked**：別開。`resident_eval_graph.h:11` 過時註解值得改。
**剩餘 owner-lock 縫（需柏為在場）**：S4 殘餘 infra（texture-array/RWStructuredBuffer/vec-color-field G3-bridge）+ point_graph 拆檔債、camera3d value-output Phase2/3、point-sprite render 縫（GlitchDisplace 家族）、生成器 t1 asset-bind 縫（NumberPattern/digit-atlas）。C 桶葉子（多影像/depth/compute/asset/field→image）卡這些縫。
**柏為 decision queue**：①menu-bar chrome 範式（native-NSMenu vs TiXL-imgui）②`startup-lock-conform` unwired 葉子算不算 DONE 門檻③剩餘 owner-lock 縫的開採序。維運 chip：ui_census 其餘 4 區 false-neg 審 `task_a47c8f98`、document.cpp 拆檔 `task_19264e66`（已頂 400 ratchet，動前必拆）、census A 軌 `task_3e02cdcc`、memory shrink `task_2487de3c`。
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
| **S2 ✅** | **render-graph / Layer2d / Execute 縫＝已建+golden 綠** | 解鎖 texture-rail 葉子 fan-out（餵 L4，absent-safe） | 脊椎在 `point_graph.cpp:465`/`_resident.cpp:376`，六 golden 親驗；殘餘是葉子非縫 |
| **S3 ✅** | **flow / 控制流縫（context-var + Execute/Loop）＝已建+golden 綠** | 解鎖 flow 葉子 | SetVar/Switch/Loop/ExecRepeatedly selftest 全 PASS |
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

## 完成定義（全 clone，三軸）
1. **節點軸**：Cook-core 脊椎 S1-S4 補完 → 所有島解鎖；L1-L6 各自 harness 綠 + 自走採盡（~800 節點 + 6 子系統對齊）。
2. **體驗軸**（EXPERIENCE_PARITY_PLAN）：TiXL 使用體驗 1:1 —— 演出迴路接通+UI（Variation/MIDI/snapshot/BPM/Player/Focus）+ 編輯器 9+4 gap + MagGraph/Timeline/Gradient/Audio。
3. 柏為殘留簽收（裝置 + 手感，體驗軸每日檢查點累積）。
4. 架構債 ratchet 回 <400。
= **完整 TiXL clone + 柏為原生 Mac 演出樂器**。

## Plan Inventory
- 本檔 = 唯一 dashboard + 並行排序權威。
- SEAM_COMPLETION_PLAN = 脊椎 S* + L4 施工 sub-plan。DEBT_LEDGER = S4 拆檔 + 真債。alignment/ = L1-L3/L5-L6 的 spec SSOT。
- **[EXPERIENCE_PARITY_PLAN](EXPERIENCE_PARITY_PLAN.md) = 第三軸 sub-plan（柏為 2026-06-24）：TiXL 使用體驗 1:1 復刻**（演出迴路脊椎 P1-P6 + 編輯視覺並行 lane Tier1-3 + MagGraph/Timeline/Gradient/Audio 獨立施工圖）。**併入本並行池**：體驗軸動核心檔的 lane（S0 `graph.h` schema / 演出 P* `frame_cook.cpp` / Tier3 cook）與引擎 cook-core 脊椎 **S1-S4 同一條 owner-lock 不可並行**；純皮 Tier1（`ui/` only）可自由並行。**驗證閘分流**：節點 lane 走 golden+refuter+--bite，體驗 lane 走 eye-hand 截圖比對+每日檢查點。詳見該檔「檔域衝突矩陣」。
- **pull-list（組批時從這 pull 下一顆）**：[census/OP_BACKLOG.md](census/OP_BACKLOG.md) = **L4 節點**狀態分桶；[census/SUBSYSTEM_BACKLOG.md](census/SUBSYSTEM_BACKLOG.md) = **L1/L3/L5/L6 子系統**狀態分桶（DONE/READY-NOW/BLOCKED/柏為殘留）。兩張皆 derived dashboard，肉的 SSOT 在 alignment/ + 藍圖 + git。**脊椎 commit 後掃 SUBSYSTEM_BACKLOG 桶 C 看誰升 READY**（如 L6 audio 匯出隨 S1 升）。

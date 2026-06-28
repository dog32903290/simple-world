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
| `runtime/point_graph.cpp` | 868 | 2.2× | 拆-TU | **`essential` — 不拆（2026-06-24 S4 scope 二度核實）** | **★S4 拆檔判定=GENUINELY IRREDUCIBLE，accept grandfather cap。** 肥肉 flow-bodies(tex~226/mesh/host-value×4/string×2/host-scalar) 早抽進 7 個 extraction TU（point_graph_{tex,mesh,hostvalue,hostscalar,string}_cook.cpp+params/debug/registry）。剩 cook()=① cookNode spine(168 LOC) ② cookCommand spine(151 LOC) ③ forward-decl std::function web(~119 LOC 純 wiring 互遞迴) ④ terminal dispatch(133 LOC,12 branch 各 call 不同 slot)。三條不可拆理由：(1)shared per-cook memo（cooked/feedbackCooked/nodeParams live-read closure/colorListCooked/cmdVisiting/texVisiting）綁 spine=single-frame-coherent pass model 本身,拆要 by-ref 穿 9+ locals (2)std::function web 是承重結構非 bloat(cookNode↔cookCommand 互遞迴) (3)ledger 想要的 shared flat/resident Command helper 不是 byte-identical 移動=跨 int/string 兩 key space 的 graph-abstraction project,違零行為變更。**本質複雜非意外複雜,硬拆 scatter pass logic 更糟（持 persona 本質醜不重織）。** task_f0fa9aa3/66b888c7/0b554339 一併判 essential。唯一 cosmetic trim=mapParam(7行)移 params.cpp,不值一個 verify cycle。 |
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
| `runtime/point_ops_transformpoints.cpp` | 454 | queued·scout | 單一複雜 op，疑 essential。（task_eef5757e 旋轉 bug 早已 closed §C4，拆檔無前置修復。） |
| `runtime/point_ops_drawscreenquad.cpp` | 452 | queued·scout | 單一 op，疑 essential。 |
| `runtime/compound_load.cpp` | 416 | queued·scout | compound 載入，scout。 |
| `runtime/point_graph.h` | 408 | queued·scout | 隨 point_graph.cpp 拆 TU 一起處理。 |

---

## B. 排修 / parity-audit 債（真債，需做）

| id | 內容 | 性質 | 狀態 | 做法 / 入口 |
|----|------|------|------|------|
| ~~**task_32b5b6e5**~~ | ~~string-rail（`b247602`）整個 flat-path-only~~ | ~~🔴 真 correctness bug~~ | **✅ CLOSED `0bb25e2` 2026-06-22** | resident string-wire seam 落地（Cut 102）：extStrOut channel on ResidentNode，resident_eval_flatten 皺褶修，resident_string_cook.cpp NEW，StringLength resident bridge，R-2 LEG20/21 雙腿證。string Phase C NOW OPEN。 |
| ~~task_258d9510~~ | ~~audit 9 顆已 ship `_multiImageFxSetup` op 的 .t3 routing~~ | parity audit | **✅ CLOSED-MOOT 2026-06-24** | 前提失效：9 顆全是 single-pass `cookParam→struct.field`（pixelate/voronoi/koch/displace/mirrorrepeat/sharpen/chromaticdistortion/detectedges/dither），**無一顆用 `_multiImageFxSetup`/FloatsToBuffer**→Cut55 routing trap 不適用。僅 mirrorrepeat 有顯式 STEP-0 trace（其餘 8 顆 1:1 無文件，非 bug）。見 §C6。 |
| task_3fc122a2 | unwired 2nd-input fallback：sw fork=sample ImageA self-warp，TiXL=黑 null SRV。涵 DistortAndShade+Displace | parity fork | queued·**decided** | **決策（orchestrator 自決 2026-06-24，照 TiXL）**：render-changing=品味=照 TiXL→convention 對齊**黑 null SRV**（捨 self-warp fork）。執行延後到下次開 multi-image op（動 DistortAndShade+Displace shader/cook，非本批 chip 範圍，不順手整片重織）。開新 multi-image op 前必先落這條。 |
| ~~task_d288a684~~ | ~~Float-Clamp min>max 行為~~ | 小 bug | **✅ CLOSED `b3b92ad` 2026-06-24** | 見 §C5。`evalClamp` branch form→`fminf(fmaxf(v,lo),hi)` byte-exact TiXL `MathUtils.Clamp`；golden `Clamp(0,5,2)=2`（min>max→max wins）。run_all PASS=411。 |
| task_602f15ec | freshly-spawned node 不進 state.json→scenario「cannot resolve node」cascade（verify/state 層非 cook 層） | verify 基建 | queued | 修 spawn→state.json landing。影響 scenario 測試可信度。 |
| task_2ee58abb | crop teeth 在 `MTL_DEBUG_LAYER=1` 補驗（本機 Metal validation 關，ShaderWrite flag 驗不到） | 驗證補強 | queued | 開 validation layer 跑 crop/mip/fastblur teeth。 |
| contextvar-channel-sub-seam | sw `ContextVarMap`（stateful_value_ops.h:71-74）只有 `floatVars`+`intVars`，TiXL `EvaluationContext` 有 5 獨立 dict（Bool/Int/Float/Object/String）。**bool var ops（`8cdb45e`）暫騎 intVars 0/1=具名 fork**→bool/int 共用 namespace（同名碰撞，TiXL 無）；窄 edge case（單圖同名 reuse bool+int）。**真修＝加 boolVars 通道**（+ stringVars/vec3Vars/matrixVars/objectVars），解 bool 隔離 + 解鎖 GetStringVar/SetVec3Var/SetMatrixVar/SetObjectVar 一族（render/flow WAVE-3 context-var channel sub-seam）。 | parity seam-deferral | queued | 加 N 個 type-channel 到 ContextVarMap + dual-rail resolver + liveGetVar 分支；bool 重導到 boolVars。census S3_FLOW_BLUEPRINT.md:121。 |
| value-output-phase2-cmd-to-value-bridge | GetScreenPos/GetPosition camera modes 卡 **Command→value back-write 縫**：TiXL 它們是 Command op（cook 在 Camera subtree 內讀 context.CameraToClipSpace/WorldToCamera/ObjectToWorld）但 output 是 value（Slot<Vector3>）。sw 兩 rail 不跨此 junction——frame-level value-emit pass（cookValueOutputNodes,frame_cook.cpp:351）跑在 cookResident(375) 前無相機 live；相機 bridge 在 Command rail（CmdCookCtx,cookResident 內）但 output 是 RenderCommand 非 extOut value。 | 承重縫(~3h,R2-R3) | queued | ①cameraToClipSpace+ObjectToWorld 加 camera bridge（additive,~1h R1）②**Command op 從 cookCommand 內寫 value 回自己 ResidentNode extOut[]（load-bearing unknown,無現存 op 這麼做,~1.5h R2-R3）**③GetScreenPos/GetPosition 投影 verbatim（~0.5h）。**等柏為 steer or 排程；別假投影（Cut47）。** value-output Phase 3=PointToMatrix/TransformMatrix 走 extColorOut Vector4[]（camera-free,medium,獨立可先做）。 |
| variation-json-loader-tolerance | `variationFromJson`（`variation_pool_json.h:113-114`）在 `ParameterSetsForChildIds` 整個 absent 時回 **true-empty**，TiXL `Variation.cs:92-93` 回 **false**（bail）。窄 malformed-guard 分歧（round-trip 正常路徑忠實 byte-stable；只有外部畸形 JSON 缺整個 param-sets map 時差）。refuter 判 data-safe NIT。 | parity NIT | queued | 改回 TiXL false-bail（absent ParameterSetsForChildIds→return false）+ 加 golden leg；併入 string/bool variation 批動 loader 時做。`6f21f5f` 引入。 |
| force-cook-fork-coverage | force closed-form golden **複製** cook 的 host-side routing fork 而非**行使**它（golden 直設 `fp.*`，不走 `fillFieldVolumeForceParams`）→ FieldVolume 的 `×0.425` Attraction fork（`point_ops_forceparams.cpp:89`）刪掉 golden 仍綠=零 executable 覆蓋。同 class 風險=未來其他 host-side .t3 routing fork。refuter 確認現值正確（非 bug），但 magic-constant fork 易靜默壞。 | 驗證覆蓋 NIT | queued | 加 `fillFieldVolumeForceParams(Attraction=1)→0.425` 單元斷言，或讓某 force golden/probe 走 cookParticleSim param-fill。`a15bc25` 引入。 |
| ~~pf0c-slotid-guard-indirection~~ | ~~slot-id guard 驗手抄 `migratedOpSlots()` 清單~~ | 驗證覆蓋 NIT | **✅ CLOSED `3b2b2af`（fan-out wave 1）** | Option-B `fieldSlotSpecs()` sink：每 op 自登記真 slot ids，guard loop `fieldSlotSpecs()×fieldSpecSink()`（53 rows），手抄清單刪。**殘留更細 NIT 見下 fieldslot-member-binding-coverage。** |
| wave3-enum-text-assert-gaps | fan-out wave 3 enum text-assert 2 處弱：①CombineFieldColor 的 `wantNonDefault=" * f"` 是 always-true substring（撞 template `clip * float2(...)`）→presence-half 死重，但 case 仍咬靠 `mix(f` absence-half（mix(f 只 CombineFieldColor Mix fold 發），故功能 OK 非壞。②PrismSDF.Sides helper flip（fTriangularPrism vs fHexPrism）無 text tooth（只 Axis 有），idx 0/1→3/6 remap 靜態顯然但無測。 | 驗證覆蓋 NIT | queued | CombineFieldColor presence-anchor 換 fold-exclusive token；PrismSDF.Sides 加 text-assert。`af255c0` 引入，非阻塞。 |
| fieldslot-member-binding-coverage | hardened guard 只保證 `slotId ∈ PortSpec.ids`，**不綁 slotId↔configurer 實際寫的 member**；parameterized round-trip 每 op 只測 1 slot（12/53）→ configurer 打錯 member（對 id 錯 member，如 `applyFloatSlot("Translation.z", n->ty)`）在**未測 slot** 上會雙漏（guard 過+round-trip 沒測）。wave-1 的 op 已 refuter 手動三方核對無誤；CombineSDF slot 在獨立 TU（combinesdf_slots.cpp）離 configurer 更遠、完整性無 check。 | 驗證覆蓋 NIT | queued | **wave 2 必 round-trip 每個 applied slot**（非每 op 1 個）；CombineSDF peel 後 slot 折回 4-arg registrar。`3b2b2af` 引入。 |
| field-combinesdf-at-cap | `field_ops_combinesdf.cpp` PF-0c 後 = **正好 400 行**（≤cap 過閘無 grandfather），但零 headroom→下次 edit（如 PF-0d 或加 combine mode）即破。builder 已 spawn 預防性 chip。 | 架構 maint-watch | queued | 下次動 combinesdf 前先 peel 一個 helper（mode kModes 表或 combine codegen），勿 grandfather bump。 |
| perop-wait-misplant（P2） | ~142 個 production point op cook body 各自 `commit()+waitUntilCompleted()` = clone TiXL 時把「selftest readback 才需要的 wait」誤植進 production（已對碼證實：filterpoints/clearsomepoints 等 production readback 全 `g_cap*`-gated selftest-only，production wait 後不讀=純 CPU-GPU 罰站；TiXL 不 per-op sync）。 | 🟡 真 perf 債 | **deferred-until-clone-done（柏為 2026-06-27 21:02）** | **clone 完成前不動**（現接會亂 cook-core）。2026-06-27 session 試寫 `cook_wait.{h,cpp}` defer-sync helper 已**回退**（untracked/未接線/未進 build；header 假稱 `--benchmark-hazard 已實測`＝不存在的地雷，已移除）。正確順序：①profile 證 wait 真佔幀時間（零數字＝憑信仰）②寫 `--benchmark-hazard` 證 Metal 同 queue **跨 command buffer** shared-storage hazard ordering 足夠（golden 單 op 隔離測不到整鏈同幀 race）③證明後才接。詳 `MASTER_PLAN.md` P2 條目。 |
| rgbtv-scratch-global-key | RgbTV input-mip scratch 用 process-global key `"rgbtv.mipin"`（`point_ops_rgbtv.cpp:159`），無 nodeId。同幀 ≥2 個**不同尺寸** RgbTV → `cachedScratchTex`（`tex_op_cache.cpp:222` realloc-on-size-mismatch）每幀互相 release+realloc（thrash）。 | 🟢 真 bug · **窄觸發 NIT** | queued（低優先） | 單一 RgbTV 或同尺寸=零 thrash；要同幀兩個不同尺寸 RgbTV 才咬，full-frame CRT post 幾乎碰不到→**別當 fps 殺手**。修法一行：key 加 `c.nodeId`（`PointCookCtx.nodeId` 現成）→ `"rgbtv.mipin." + std::to_string(c.nodeId)`。代價=per-node 殘留一 scratch entry（RgbTV 罕見可接受；嚴謹則 node 刪除時 evict）。**Gemini 2026-06-27 perf review 揪出。同次 review 其餘三條裁決（已壓，不登＝避免假債）**：#2 增量patch＝已是 P4（非新項）；#3 Argument Buffers＝過早優化（非瓶頸，binding ~12/op，真瓶頸在 P2 commit-wait）；#4 threadgroup 對齊＝已完成（point/particle `tg=64`、rgbtv `8×8=64` 全 32 倍數）。 |
| op-census-overview-awk | `tools/op_census.sh --overview` 的 % formatter awk one-liner 在**某些 awk 上報 syntax error（`awk: syntax error` / `BEGINif(...)printf`）**——counts 印得出來但 % 欄炸；`--seams` / `<island>` 路徑不受影響可正常用。 | 🛠 工具 bug（非標籤） | queued（低優先·**本機未重現**） | ⚠2026-06-28 在本機 BSD awk (version 20200816) **`--overview` 跑 clean、exit 0、% 欄正常**＝未重現。可能機器/awk 版本特定（GNU vs BSD vs mawk）或已修。**動手前先在報錯的那台 awk 重現**，別憑此條盲改（憑信仰改 awk 比 bug 本身更危險）。若真重現＝把 BEGIN block 的 printf 格式串拆出 `-v` 變數或加空白分隔 `BEGIN {` `if(`。scout 報的是 ground-truth 校正夜批副產物，非本機觀察。 |

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
- **task_d288a684** `Float-Clamp min>max 行為` → **closed `b3b92ad` 2026-06-24**。`runtime/value_eval_ops.cpp:50-54` 的 float `evalClamp` 用 branch form（`if v<lo return lo; if v>hi return hi`），min>max 時回 min；TiXL `MathUtils.Clamp = Min(Max(v,min),max)`（MathUtils.cs:253）回 max。改 `fminf(fmaxf(v,lo),hi)` byte-exact。int-clamp 變體本已 faithful，float 是孤兒。golden `Clamp(0,5,2)=2`（min>max→max wins）pin 修正行為（舊式回 5）。run_all PASS=411（唯一紅=soundtrack 已知 flake §C2）。

### C6 — closed-as-moot（前提失效，非真債）
- **task_258d9510** `audit 9 顆 _multiImageFxSetup op 的 .t3 routing` → **closed-moot 2026-06-24**。Cut55 的 FloatsToBuffer routing trap **不適用**：9 顆 named op（pixelate/voronoi/koch/displace/mirrorrepeat/sharpen/chromaticdistortion/detectedges/dither）全是 single-pass `cookParam(c,"Name",...)→struct.field` 直連，**無一顆走 `_multiImageFxSetup`/FloatsToBuffer 中間數學節點**。殘值僅「8 顆 1:1 mapping 無 STEP-0 文件註解」（mirrorrepeat 有），非 correctness bug。chip 以原措辭已失效；若要 paper trail 可另開小 doc 任務（非債）。

---

## D. 怎麼被做（自走撿取入口 + 順序）

**撿取入口**：
- 架構債（A）：每檔一張 sw-batch 工單，承重重構工法（§A 表頭）。P3 單一 op 檔先 scout 判 essential/拆。
- 排修債（B）：每條一條 lane，各自修法。

**建議順序（槓桿 × 危害 × 客觀度）**：
1. ~~task_eef5757e~~ **已 closed（871464a 早修，見 §C4）**。~~task_32b5b6e5~~ **已 closed（0bb25e2，2026-06-22，見 §B / §C5）**。P0 結構閘全清，string Phase C OPEN。
2. **A-P1 的 4 顆資料化/拆-ops**（stateful_value_ops 2657 / node_registry_math 917 / point_modify 653 / keymap 752）— 最客觀（行數可量、不需判對錯）、最高槓桿、且 registry 那兩顆雙重違反 rule 7。
3. **point_graph.cpp 拆 TU（A-P1）** — 擋住 point_graph.cpp 繼續長。~~resident string-wire 已 closed（0bb25e2）~~。
4. ~~task_258d9510~~（closed-moot §C6）/ task_3fc122a2（B parity，**已決策照 TiXL 黑-fallback**，執行隨下次 multi-image op）— 影響已出貨 op 的正確性。task_d288a684 已 closed §C5。
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

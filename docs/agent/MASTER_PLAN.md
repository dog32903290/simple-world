# simple_world ⇄ TiXL 全對齊 — 最快路徑計劃表（全並行）

> 柏為 2026-06-23:「程式碼全翻完了，寫一份全部可以並行、以最快路徑為原則的計劃表。」
> **本檔=頂層路由權威。** sub-plan:節點/縫=[SEAM_COMPLETION_PLAN](SEAM_COMPLETION_PLAN.md)、債=[DEBT_LEDGER](DEBT_LEDGER.md)、非節點 spec=[alignment/](alignment/README.md)。事實以 git/碼為準。
> **開頭定位**：跑 `tools/sw_status.sh`（三區 ① LIVE git+census 現測 / ② STAMPED@結帳 bite / ③ HAND 手寫接力）。**結帳**：`sw_status.sh --stamp <bite PASS>` 蓋章 + 手寫更新下方 Active Lane/Conflict/Next + `--check` 過閘。舊 snapshot 在 [MASTER_PLAN_HISTORY.md](MASTER_PLAN_HISTORY.md)。
>
> **★★範圍閘（柏為 2026-06-27 21:24 拍板）：TiXL clone 完成前，不開 TiXL-absent 的全新能力。** clone 仍進行時，分岔出 parity 軸的新方向對目前的模樣太有風險（①動到還在變的 cook-core 跟 clone 互踩，同 P2 ②TiXL-absent 能力沒有 TiXL 當 ground-truth＝柏為退出視覺驗證後機器驗證的唯一錨點，golden 體系對它失效）。**判準＝這能力 TiXL 源碼有沒有對應物**：有＝parity 軸（clone 中照做，例 audio-reactive/BpmDetection port 自 TiXL）；沒有＝等 clone 完（例 AI segmentation 節點、真 audio 引擎整合 Ableton/scsynth 子進程+OSC）。劃錯雙向都錯：別把 port-TiXL 誤擋成新能力（白停產能），別把 TiXL-absent 誤當 parity 放行。AI segmentation 方向已壓（即時節點 framing 撞決定性 export+golden 體系，正解＝離線 mask 生成存檔當固定 input；定案前需 spike 真 footage）＝典型「等 clone 完」項。
>
> **★★自走/驗收政策（柏為 2026-06-28 22:29 拍板：「不要每次都在等我在場，我等你做完再慢慢驗」）：柏為預設 ABSENT，agent 自走做完、事後批次驗收。** 每個任務標三類之一：
> - **`[G｜自走-機器閘]`**：golden byte-identical + refuter + `--bite` + check-arch 全綠＝完成，零柏為介入，直接 commit。
> - **`[Y｜自走-事後視覺驗]`**：機器閘守正確性，但有機器測不到的觀感/手感→自走做完+截圖/scenario→落「## 待柏為驗收」佇列→柏為回場一次掃。**做完不等驗收，直接接下一個；驗收非阻塞。**
> - **`[R｜預先拍板]`**：TiXL 無對應物的設計分岔、選錯難回頭→必須柏為先選方向。**因範圍閘＋「分岔預設照 TiXL」，clone 完成前這類趨近空**（TiXL 有=照抄不需拍板；TiXL 無=範圍閘擋到 clone 後）。標 `[R]` 前先自問「照 TiXL 是否就有答案」，是＝其實是假 R，降 G/Y。
> 判準預設照 TiXL（[[baiwei-direct-decide-tixl-default]]）；G/Y 不確定的標 `[?待scout]`，自走 session 先 ground-truth scout 再定（方法論鐵律）。**標籤是 agent 自填自走的，不是等柏為確認的；柏為事後看 plan 可否決任何分類。**

## Current Snapshot
<!-- sw_status:begin （機器塊：結帳時 tools/sw_status.sh --stamp <bite PASS> 寫入；勿手改） -->
HEAD: 51d3ef5
DIRTY: clean
CENSUS: 461 / 749 done
BITE: 529 PASS
STAMP_AT: 2026-06-29T14:23
<!-- sw_status:end -->

- 引擎 clone **57%（427/749）**。★**「clean-leaf 採盡」兩度被推翻**：(1) S2/S3 脊椎查出早已蓋好+golden 綠→單輸入 texture-rail 葉子可採；(2) **multi-image seam 也早已建**（gather 綁 4 input texture，Blend/Displace/Combine3Images 已證）→ **fixed-port 多輸入 op 是乾淨葉子**。**本 session 六批已採 10 顆 image 葉子 + 1 小 seam**（batch1 `627458b` Mandelbrot+DepthBuffer、batch2 `fc92eca` ImageLevels+2×Ryoji+HoneyComb、batch4 `9fa193e` CombineMaterialChannels、batch5 `646544d` HSE+MosiacTiling、batch6 `0fd14a4` MultiInput<Texture2D> gather 擴充 + PickTexture）。**★方法論血證（4-5 次）：census/scout 系統性把「已建的 seam」誤報 gated（S2/S3 脊椎、multi-image gather 都早已建）→ 別信 census done/todo，ground-truth=讀 cook path（派 Plan agent 深讀，不是 Explore census）。** 選葉子要開 .hlsl 親看（單 pass？非 compute-reduction？非 compound？fixed-port？）。
- **柏為-absent 自走可採 = 第三軸體驗復刻尾**（[EXPERIENCE_PARITY_PLAN](EXPERIENCE_PARITY_PLAN.md)：純皮 Tier1 / Output O3 / 維運），eye-hand 驗、不碰 cook-core。
- 本 session 落地：**field 紅修**（`644d100` AudioReaction 救回）+ **quick-add 型別色**（`e427d55`）+ **ui_census 校正×3**（`56a2057`/`708b253`/`7765469`）+ **out-snapshot-png**（`5a9a51f`）+ **★S1 輸出解析度縫端到端完成**（柏為 23:35 授權：`1b53b12` cook-core override hook + `a93f2dc` UI 選擇器,皆 refuter 8/8 SURVIVES）→ B 軌 out-resolution-selector 自動 DONE,B 軌 16→19。

## Active Lane
> **★★STEERING 覆寫（柏為 2026-06-29 15:41 拍板，[R] 級方向，可能不可逆）：停 monolithic 節點產線、轉投「原子地基優先」。** 鐵證：DrawPoints/DrawMesh/RadialPoints/DrawLines/GridPoints/LinePoints/DrawBillboards/TransformPoints 在 TiXL **全是複合非原子**；我方 461 done ≈ 102 atomic + 359 monolithic-compound-equiv＝**一路把 TiXL 複合層壓成單體手寫、真原子層只做 ~102**。後果：無原子地基／無法重放 .t3／無法做鑽進複合的磁吸 UI。**新方向＝建被最多複合重用的原子詞彙（ROI 表 `scratchpad/t3roi.py`）**：① buffer-marshalling（keystone `FloatsToBuffer` 208 顆=58%／GetBufferComponents 163／ExecuteBufferUpdate 117／SrvFromTexture2d 116／IntsToBuffer／GetSRVProperties，全未 port）② DX11 render-state（Rasterizer／InputAssemblerStage／OutputMergerStage／Draw／TransformsConstBuffer，~72 each 未 port）＝一直 defer 的 `shader-graph`+`dx11-wrapper` seam。math/value 層已做。**gating 設計難題**：`GenerateShaderGraphCode`（inline HLSL codegen）撞「預編譯 shader」哲學，決定 draw 類能否重放。**下一步**：spike `FloatsToBuffer` 原子節點 + 最小 .t3 importer，證 byte-parity。**協調：下方 param-completion lane ＝被停的 monolithic 線；另一 session（sad-goodall worktree, commits ca0972e/6686fa1）在跑它，需 redirect。** 詳 [[sw-clone-two-rails-atomic-compound]]。**以下 param-completion lane 內容凍結為歷史，勿續推。**

**〔已凍結 2026-06-29，見上方 STEERING〕parity-gate retrofit〔進行中，柏為 02:38 第一優先〕— 主體 ✅（Radial/Turbulence/DrawPoints 修 + Force 6 + transform 7 確認）。★第三維度 param 補全（柏為 11:40 開，inspector 旋鈕完整性=「sw NodeSpec param 數對齊 TiXL .cs [Input] 數」）進行中：**
- **fan-out 基建 ✅（`eb909ff`）**：拆 generator table（400→168+extra，ratchet 降 180）+ `--dump-nodespec <type>` CLI（folded logical count）+ `tools/nodespec_integrity.sh`（sw FOLDED vs TiXL grep [Input]，自動完整性閘抓漏特製 param）。
- **三顆補完**：RadialPoints（`4082e6f`，★接管另一 session 並行寫的半成品+審查+補完，撞車解決）/GridPoints（`0be5919`，共通 attr 基建 `appendPointOrientationSpec` 抽出）/LinePoints（`46146b9`，18==18，雙色 lerp+11 param，主戰場）。每顆完整性閘綠+golden red-first+refuter SURVIVES。--bite→527。
- **★generator 島 param-completion 完整（13/13 閘綠，`b0845d4` 收尾）**：HexGridPoints 補 Scale（第 11 [Input]，.t3 ScaleVector3 → Result=Size·Scale routing）；PointsOnMesh IsEnabled=named FORK（通用 flow/Execute skip-GPU-pass 開關 guid d68b5569 跨 49 op 共用，非節點特有 → `known_fork_count()` 逐節點 hardcoded case）。red-first 證牙+Opus refuter 兩處 SURVIVES。--bite→528。
- **★fan-out 工法（範式已穩）**：套共通基建（Color+Orientation 小尾巴，別過度抽象）→ 逐顆對 .cs 補特有 param（APPEND-ONLY，pin id port-index based）→ backward-trace .t3 routing（防 silent parity-wrong，如 GridPoints Scale 經 ScaleVector3）→ `nodespec_integrity.sh` 閘綠 → golden red-first（cook-through production NodeSpec）→ 親驗+refuter+commit。值來源:.t3 default+.hlsl 公式為主，.var preset/examples 真實值為 bonus（稀疏）。
- **★跨島偵察完成 → 缺口地圖 `docs/agent/census/PARAM_COMPLETION_MAP.md`（選批 SSOT）**：其他島主流是 `sw>TiXL` EXTRA，**兩種成因要分開**——成因 A（image）resolution trio 是真 baked output-format 慣例非缺口；成因 B（field/mesh）是 `--dump-nodespec` Vec fold bug 假 EXTRA。**閘擴多島前必先修 fold bug（工單 D）**。真缺口序：✅**flow Set*Var 族（最密 6/14，`51d3ef5` 收）**——dual-rail（value-rail+command-rail）縫，SubGraph/ClearAfterExecution=cmd-rail 結構 fork（known_fork），補 LogLevel/LogUpdates/Message 真旋鈕，--bite→529 → 下一步**閘擴多島（工單 D fold-bug 先）**解鎖 field/mesh/image 系統化掃描 → ParticleSystem -11（卡柏為 pool-fork）。
- **剩餘卡柏為**：ParticleSystem [?]（MaxParticleCount pool fork）+ PointToMatrix（latent-risk/cook-core）+ 3 個 [Y] 待驗收（見佇列）。可選增強:DrawPoints default-guard/velocity-seed/RandomJump。
工單＝`docs/agent/PARITY_GATE_PLAN.md`。**
- **本批 `[G][Y]`（`25946ae`）**：建有狀態節點 parity-golden 模板（`app/src/parity_golden_harness.h`：ParityHarness+ParityReport，固定場景 cook→CPU readback→對 TiXL 公式手算斷言+injectBug tooth）+ 修 RadialPoints（Count 2048→100/Radius 2→1）、TurbulenceForce（Amount 15→1/Freq 1.2→1/**Phase 解綁 wall-clock→inspector param 預設0**，修離線 render 決定性）到 TiXL parity。red-first 三態證牙（no-bug GREEN / injectBug RED / 注入舊偏差 RED）。
- **★★parity-gate 新鐵律（本批 refuter 血證，已寫進 PARITY_GATE_PLAN）：parity-golden 必須 cook-through production NodeSpec default，不准繞過 cook 直接打 kernel。** 首版 turbulence golden direct-kernel→假綠（NodeSpec 沒改、改的 cook fallback 是死碼，`resolveNodeParams` 從 `p.def` 填、1.0f fallback 永不 fire）→ refuter 抓出 production 實際還是 15× → 改 cook-through 後 NodeSpec=15 立刻 RED 咬住。**fan-out 每顆務必 cook-through，否則假綠洗白。**
- **★[Y] 待柏為驗收**：見下方佇列（turbulence 變靜止 / radial 點數半徑變，照 TiXL）。
- **Force 類範式首顆 `[G]`（`5b958b5`）**：DirectionalForce cook-through parity golden。**GREEN case（早已忠實，零 production 改動，純補閘）**——scout 證 sw NodeSpec 預設(Amount=0.007/Dir=(0,-1,0)/RandomAmount=0)與 TiXL .t3 完全相同、kernel byte-1:1。三牙範式(T1 direct-kernel 閉式/T2 cook-through 守 NodeSpec/T3 determinism)。**fan-out 並行情報**：golden 檔零撞可並行寫；3 註冊檔(selftests_decls/selftests_point/CMakeLists)會撞→write-leaf 並行+中央接線(point lane 範式)；偏差堆(需修 point_ops production)逐顆序列。**⚠ 教訓：agent 自 commit 違反 orchestrator 親合流→下批工單明令「不自 commit、回報讓 orchestrator 親驗合流」。**
- **Force fan-out 5 顆 `[G]`（`aa1874b`，Workflow write-leaf 並行+中央接線）**：AxisStep/VectorField（CREATE-vel，T2 cook-through **真守 NodeSpec**，refuter 實證改 NodeSpec→T2 RED）+ SnapToAngles/FieldDistance/FieldVolume（TRANSFORM-vel）。**★★承重發現（durable）：TRANSFORM-velocity force 的 cook-through-NodeSpec 守護結構上做不到**——cookParticleSim 種子 vel=0（`point_ops.cpp:172` baked InitialVelocity=0）+ 單 force kernel 不 chain + ParticleSystem NodeSpec 無 InitialVelocity port → TRANSFORM force 永遠看到 vel=0 → faithful no-op，NodeSpec discrim 退 direct-kernel（不抓 NodeSpec drift，SnapToAngles T1b labeling overclaim）。**這三顆 golden 補的是 no-op 契約 + kernel 閉式，非 NodeSpec-drift 守護。** 偏差堆空（六顆 production NodeSpec==.t3 已忠實）。--bite 524，refuter 5/5 SURVIVES。**follow-up（spawn_task）：velocity-seed seam（emitter InitialVelocity port 或 force-chain）讓 TRANSFORM force cook-through 守 NodeSpec=可能；test-coverage 議題非 production parity（production 今天忠實），低急迫但關閉 turbulence 同類洞。RandomJump+Field wired-SDF 行為 golden 同延後（需 field 綁定）。**
- **DrawPoints 換 quad sprite `[G][Y]`（`3310181`）**：從退化 4px 死點換成 6-vert camera-facing quad（柏為最初四顆裡唯一定義級偏差）。cookDrawPoints emit DrawKind::Points2，NodeSpec 接回 PointSize/Color/ScaleFactor/BlendMode（執行器/draw_points2.metal 不動，quad 基建已在）。red-first 證牙（死點不響應 PointSize→RED）。7 顆下游 golden 回歸（忠實 sprite≈1px<4px 死點，像素探針讀不到）→場景設 PointSize=1.5 修（node .t3 default 仍忠實 0.1，零斷言改動，refuter 證非洗白）。refuter 5/5 SURVIVES，--bite 525。**★誠實標註：golden 守 param-response（PointSize 有接+驅動 sprite）非 NodeSpec-default 值（PointSize=0.1 無機械閘，靠 comment 錨 .t3，[Y] 觀感）→ default-guard golden 可選增強待補（同 TRANSFORM force 的 default-drift gap）。fork 延後具名：UsePointsScale Scale.xy/AlphaCutOff/FadeNearest/ZWrite/ZTest/Texture/ColorField/BlendMode-Additive。test flag `drawPointsBugForceV1ForTest()`=function-static CPU driver（非 shader bug-branch，第8顆同範式，不污染 production）。**
- **★durable trap（host-value-emit op）**：`evalResidentFloat` 的 `!evaluate` readback 回 `extOut[i]`，i 是 port 在 `s->ports` 的**全索引（含 inputs）**非 0-based output index → output port 要從 spec 算 output→extOut-slot 映射（PickColor/PickColorFromList 同註）。
- **★durable：String/StringList rail = flat-cook，不進 resident graph**（string_op_registry.h:24）→ 這些 op 的 golden 只覆蓋 flat leg，多輸出受限（StringCookCtx 只帶 extraStrOutputs+scalarOutputs 單值，無 list-output sink；scalar sink outCache[3] 上限 3 個）。新 string/stringlist op 是 explicit CMake list 非 glob（合流要中央補行）。
- **★eye-hand 限制（durable）**：in-process 合成 hand 點不開 `BeginMainMenuBar`（能開 `BeginPopup`）→ menu-bar 類 UI load-bearing 證明＝map.json rect 斷言，dropdown 點擊 best-effort。
- **★★兩條 NAMED LATENT RISK（今天 inert，0 forward consumer；柏為建 PointToMatrix→camera consumer rail 時必先讀）**：① `latent-pointvalue-emit-one-frame-late`——新 pass 寫在 cookResident 後、其餘 emit pass 寫在前；未來若有 in-graph consumer 在 cookResident 期間 pull 這些 emit→讀到**前一幀**值（sw forward push-cook 本質，TiXL 是 pull-graph 無此問題）。② `latent-stale-points-off-display-subtree`——`outBuf` 無 per-frame invalidation＋resident cook 是 target-driven（只 cook 顯示子樹）→Points src 在顯示子樹外的 PointToMatrix 讀到 stale 前幀 buffer（non-null 錯 count）非 identity。**goldens 用 stub accessor 不覆蓋 multi-frame consumer path**——consumer rail 開工前這兩條要先解。
- **★★方法論鐵律（柏為 2026-06-27 定，根因修）：seam 開採前必 ground-truth scout；scout 揪出 stale label（已建/真數/依賴）後**必立刻派 plan-update subagent 寫回 `tools/seam_map.tsv`+census docs**，否則每 session 重栽同坑、差點重蓋已建縫（一晚 6+ 次）。scout→write-back→build，漏寫回=下次重栽。[[sw-groundtruth-writeback-rule]]**

## 待柏為驗收（PENDING BAIWEI-VERIFY）
> `[Y]` 任務自走做完後落這裡，柏為回場一次批次驗收。每項格式＝**做了什麼 + commit hash + 怎麼驗（scenario 名/截圖路徑/一句肉眼判準）+ 機器閘狀態（golden/refuter/--bite）**。**驗收非阻塞：agent 落這裡後直接接下一個 `[Y]`/`[G]`，不等柏為。** 柏為驗過的項移到 [MASTER_PLAN_HISTORY.md](MASTER_PLAN_HISTORY.md)。
- **menu-bar 改 native-only（`5ee250f`，柏為 2026-06-29 現身拍板，推翻 a01467e imgui 版）** — macOS 上 a01467e 的 in-window imgui bar 與 OS 最頂 native bar 重複兩層＝macOS-wrong。已改成只用 native NSMenu、補完整 App/File/Edit/View/Window（Edit=Undo/Redo；View=Assets/Variation/Theme 視窗+Toggle-All+Focus+Fullscreen，經 ui→app fn-ptr seam）。**怎麼驗**：開 app，肉眼看螢幕**最頂**那條 native bar 點開 File/Edit/View 內容對不對（視窗內已無 imgui 條，截圖 `artifacts/menubar_native_only_014658.png` 證視窗乾淨）。**機器閘**：--bite 516 無回歸、check-arch 綠、map.json native_menu_items 證 Edit/View 項出現、Cmd-N/O/S/Z 仍 fire。**checkmark ✅已補（`701b2a1`，柏為授權）**：View 視窗 toggle 顯示打勾反映開關狀態（NSMenu menuNeedsUpdate delegate + setState，binding 在 tracked `platform/menu_appkit_ext`，非 vendored）；--selftest-view-menu-state 證資料路徑，OS 側打勾渲染待你肉眼。**待你簽**：① native bar 內容 OK 嗎 ② View 選單綁哪些視窗（目前 Assets/Variation/Theme+全UI+Focus+全螢幕，你可改）③ 打勾顯示對不對。
- **parity-gate Stage 3 前兩顆 RadialPoints/TurbulenceForce（`25946ae`，柏為 02:38 下令的 parity 修）** — 修預設面板「跟 TiXL 不一樣」的兩顆到 TiXL parity。**怎麼驗**：開 app 看預設場景——turbulence 現在預設**靜止**（Phase=0，TiXL 行為，接 Time 才動）、radial 點數變少（100 非 2048）半徑變小（1 非 2）。**機器閘**：radial-parity/turbulence-parity golden no-bug GREEN+bug RED、--bite 518、refuter 4/4 SURVIVES。**待你簽**：這個「更靜、更少點」的預設面板是否就是 TiXL 的樣子（機器已證數值對 TiXL，這裡只簽觀感無誤）。
- **DrawPoints 換 quad sprite（`3310181`，柏為 02:38 parity 修）** — 預設面板 DrawPoints 從「不分大小的 4px 死點」變成「依 PointSize 的 6-vert camera-facing quad sprite」，照 TiXL DrawPoints.hlsl（11 param 全砍→接回 PointSize/Color/ScaleFactor/BlendMode）。**怎麼驗**：開 app 看預設場景，點現在是 quad sprite（吃 PointSize/Color/blend）；預設場景 PointSize=1.5 可見，裸節點 .t3 預設 0.1≈1px（忠實但小，你可調 PointSize 旋鈕）。**機器閘**：drawpoints-parity golden no-bug GREEN+bug RED、7 顆下游 golden 無回歸、--bite 525、refuter 5/5 SURVIVES。**待你簽**：① quad sprite 觀感對不對 ② 預設場景 PointSize=1.5 vs TiXL 預設場景值（可能需跑真 TiXL 看）③ 純像素 byte parity 標 pixel-deferred-windows（等 Windows TiXL reference）。

## Conflict Register
- **（已解，2026-06-29）graph 三層 header 註釋 stale — production 早已跑巢狀/resident，文件停在 batch-1**：`graph.h`/`compound_graph.h`/`resident_eval_graph.h` 的頭註說「flat Graph 仍是 editor/save/UI/cook 現役」「resident NOT yet wired to production / 等 batch-2 swap」——**全 stale**。亲驗 code：live 帧循環 `frame_cook.cpp run()`:280 `buildEvalGraph(doc::g_lib())` +:387 `pg.cookResident(...)`；canvas `editor_ui.cpp:4`「no flat Graph, no projection layer」遍歷 `Symbol.children/connections`，雙擊 compound child 可鑽入。flat `Graph`+`cook(Graph&)` 只剩 golden T1 test leg（R-2 鐵律:only-flat=production black hole）。**三 header 註釋 + batch-1 plan + node-classification.md(category 欄位已存在)已校正。** 剩餘 cleanup（非脊椎 swap）:golden flat→resident 遷移、stateful-op state key node-id→path、pin-id 工具、Automation 曲線解析。**教訓（第三次同根因）:doc/header 會 stale，狀態看 code 不看註釋 [[gate-or-it-rots]]。**
- **（finding 2026-06-29，候選下一大 seam，待柏為定）clone 策略可裂兩軌 — 原子手刻 + 複合重放 .t3**：TiXL `Lib/`=925 真算子,**原子:複合=563:349（複合 38%,其中 345 純圖、僅 4 混合體）**。複合節點在 TiXL 就是「.t3 子圖」零計算碼→可由載入 .t3 重放,非逐顆手刻（記憶血證:DirectionalBlur/_multiImageFxSetup 手刻複合猜錯內部接線=parity-wrong,重放可滅此類 bug）。**運行時已就位（production 跑巢狀 compound graph）,但缺口=無 `.t3 → SymbolLibrary` importer**（`.t3` 在我方 code 只出現在 golden 註釋,191 個 point_ops 全手刻）。原子軌瓶頸=~200 shader 類（composition 救不了）。**建議:spike 一顆子節點已備齊的非平凡複合,證「載入 .t3 → 我方 cook == TiXL output」byte-parity,再決定裂軌。** 詳 [[sw-clone-two-rails-atomic-compound]]。待量:461「done」裡幾顆是 TiXL-複合被手刻（追溯浪費量）。先前 batch-4 期間此改動出現在 main checkout，我誤判為 ColorThemeEditor fixer 越權→park 到 review 分支；**柏為 01:51 澄清＝另一 session 在同 checkout 寫的合法 post-parity 工單**（[[sw-batch-no-parallel-launch]] 雙 session 同 checkout 情境），授權收。review 分支已刪。**教訓：main checkout 冒出任務範圍外改動，可能是平行 session 不是自家 agent——但處理法相同：`git diff --stat` 核範圍 + pathspec commit（這次救了沒混進 theme 批）。**
- 工作1/工作2 零未解衝突（島7 單線 cook-core 已收，tree clean）。**無未解衝突**。
- 非阻塞 follow-up：gate_census absent-gate 改 fail-safe（低）；FP safeStem 外部 `:`-id disambiguator；CTE 剩無-sw-consumer 欄（Widget*/Status* 無 sw 渲染對象→theme 它們是 dead weight，等 sw 真 render 那些 UI 元件再接）；SP 補 LookAt 斷言；RO 補 disk-corrupt leg。皆低優先。
- （更早已解項移 history：soundtrack flake `cd47f72`/field scn `644d100`；chip `task_eb3375a3`/`task_2fc4a37a` 可關，`task_9d081266`=detectbpm NO-BITE 待修。）

## Session Safety
- **★結帳前必 rebuild 再 --bite（本批踩）**：`tools/run_all_selftests.sh --bite` 跑的是 `app/build/simple_world` **既有 binary**（只有「binary 不存在」才報錯，stale binary 靜默跑舊）→ merge 完直接 --bite 會拿到舊數字（本批看到 474 該是 478，差點誤蓋章）。合流後順序＝`cmake --build app/build -j8` → `--bite` → `--stamp`。
- 另有 parked worktree `.worktrees/ui-node-skin`（branch `ui/tixl-node-skin` @ `fd542f5`）= 舊 L2 node-skin lane，未合流，**別當死的清掉**。
- 柏為 2026-06-26 01:09 回場下令「需我在場的工作你先做、不等我、自走到我喊停」→ 結果 S2 脊椎查出早已蓋好，present-requiring 阻塞自動消解 → 轉做 S2 殘餘 image-leaf fan-out（absent-safe）。**S2/S3 脊椎已建，不必再等授權重開**；真正剩的 owner-lock 縫＝S4 殘餘 infra（texture-array/RWStructuredBuffer/vec-color-field）+ point_graph 拆檔債、camera3d value-output Phase2/3、point-sprite render 縫——這些才需柏為在場。**（→ 2026-06-28 政策已 override：這些重分類為 `[Y]` 自走-事後視覺驗，不需預先在場，見頂部「自走/驗收政策」。此句為 dated record。）**
- **worktree 隔離教訓**：派並行 build agent 要在 Agent 呼叫上設 `isolation:worktree`；只在工單寫 `agent_worktree_setup.sh` 而不設 flag → 無 worktree 可 ff → 全跑進 main checkout 共用樹（本批 4 agent 都落 main，幸好 self-registering 零共享檔+建置非同時才無損）。見 [[worktree-base-main-trap]]。
- **拆檔狀態（ratchet headroom）**：✅`output_window.cpp`（301）/✅`editor_ui.cpp`（263）/✅`document.cpp`（`b053941` 400→**31**，解鎖 folder-package-save，chip `task_19264e66` 可關）/✅`point_graph.h`（已拆 cook_ctx）都有餘量。**仍卡無 headroom**：`point_ops.h`@553 grandfather cap（SP 本批靠 trim 註解才容下，再加 point-op 前須拆或騰空間）。**拆檔紀律：拆+裝/降 ratchet 綁一起 [[gate-or-it-rots]]。**
- **merged worktree 都已清**（本 session 十 lane 的 worktree+branch 已清；`review/mv-plan-fixer-addition` 已收進 main 並刪）。**保留** `.worktrees/ui-node-skin`（未合流，故意）。其餘 `worktree-agent-*` 老分支動前先查未合流。
- **★stale agent worktree pile（本 session 留下 ~17 個 `.claude/worktrees/agent-*`，`git worktree remove` 靜默 no-op 沒清掉）**：已開 spawn_task「Clean stale agent worktree pile」收尾清理。**保留** `.worktrees/ui-node-skin` parked-lane（未合流，故意，別當死的清掉）。
- **★build-storm + 看門狗假死教訓（本 session 兩度踩）**：並行開 4-5 條 build lane → CPU/IO 爭用讓 `--bite` 單一長 bash 呼叫凍住 transcript 20-30 分（**正常、非死**）→ 25 分看門狗誤判死亡。soundtrack lane 假死、split lane 假死還害我派接力 agent（幸好接力工單有 git-state guard 擋住雙-driver——split 其實只是爭用下跑完最後一哩）。**規則：並行 build lane ≤2-3 條；跑 --bite 的 lane 看門狗閾值放 45 分；判死認真死（process 沒了 + 等 60-120s 再 stat），STALE≠死；接力工單必含「git 狀態非預期就 STOP」guard 防雙-driver**。見 [[subagent-death-detection]]/[[sw-watchdog-cook-core-false-death]]。
- **eye-hand 截圖被面板遮擋擋住（本 session raymarch 踩）**：spawned node 生在浮動 Output/Inspector 面板下方→hand 拖線點到面板不到 pin，eye-hand 視覺驗證做不出來。這是 orthogonal UI 問題非 seam；production-path golden（cook→`pg.target()`，與 OutputWindow 同源 texture）是 load-bearing 證明，eye 截圖 best-effort。值得開 chip 解（移開/可關面板 or spawn 到 clear canvas）。

## Next Handoff Sentence
**★柏為 14:38 定向（優先序覆寫）：先補完旋鈕（已 port 節點旋鈕完整性全綠/全覆蓋、確認沒 silent 偏差）→ 再推共享 seam。** 即：閘擴多島看全景 → 逐顆補「乾淨」真缺口 → 卡共享 seam（PF-0d float4x4 / render-state+asset-bind）的具名標記**不補**、留到「補完旋鈕」後的下一階段。

**★★接力（param-completion fan-out，進行中）：generator 島 ✅（13/13，`b0845d4`）+ flow Set*Var 族 ✅（6/6，`51d3ef5`，dual-rail fork）。跨島地圖 `docs/agent/census/PARAM_COMPLETION_MAP.md`（選批 SSOT，含閘擴充工單 A-D 完整 spec + 4 SW_UNKNOWN verdict）。下一批＝閘擴多島（解鎖 field/mesh/image ~139 節點系統化掃描）：先工單 D（`dumpNodeSpec` fold-bug→收斂到既有 positional consume-the-run walk＝Inspector/animGroupForSlot 同源；generator 無 Vec head 數學保證 13/13 不回歸；建議抽 `graph.h` `foldVecRun` 共用 helper + 守護 invariant 斷言），再 A（island .cs 子樹解析 + fork header authority 吃掉 B）+ C（image resolution-trio 排除）+ cook-path 分類（NodeSpec/texReg/cmdReg，非 NodeSpec 標 N/A）。spec 細到可直接實作＝MAP §閘擴多島修法。⚠ graph.h 是 cook-core owner-lock，動前確認無並行 lane 寫它。之後真缺口 fan-out：field TransformField -5/RaymarchField -3、mesh DrawMeshUnlit -11、image RenderTarget -6、string BlendStrings -2。ParticleSystem -11 卡柏為 pool-fork。每顆 golden cook-through production NodeSpec。**

下個 `/sw-batch` 開頭先跑 `tools/sw_status.sh` 定位（HEAD/bite/census 看機器塊，勿信此處手打）。**★狀態：generator+flow 兩島 param-completion 收尾，閘擴多島 spec 已備。零未 commit。**
- **4 顆 SW_UNKNOWN 已查（MAP §SW_UNKNOWN）**：EdgeRepeat/PolarCoordinates(texReg)/Switch(cmdReg)=非 NodeSpec 路徑閘 N/A 正常；**Steps=census 假陽性（"Steps" 是 node_registry 插值 enum-label 字串被 census source#3 grep 誤判，sw 實際未 port）→ 真缺口待採**。census source#3 capitalized-string 掃描會把 enum-label 誤判成 done（done 數略灌水），修法見 MAP。
- **PARITY_GATE_PLAN.md 殘餘**（有狀態重節點，與 param-completion 不同軸）：Stage 2 裝閘（有狀態節點清單 ratchet）尚未做；Force/point-render 可手算 golden、ParticleSystem host-cut input、DrawPoints pixel-deferred 仍在 §清單。
- **fan-out 待採（PARITY_GATE_PLAN §清單）**：ParticleSystem（integrator 已忠實，修 host 砍掉的 input；**MaxParticleCount/IsAutoCount pool fork 是 `[?]` 需柏為拍板留不留**）、DrawPoints（換 DrawPoints2 quad 實作，緊性質探針；**純像素 reference = `pixel-deferred-windows`**）、各 Force（FieldDistance/FieldVolume/RandomJump/AxisStep/SnapToAngles/VectorField）、point/render 可手算（MoveToSDF/PointToMatrix/SnapPointsToGrid/TransformFromClipSpace/DoyleSpiralPoints2/Transform/Shear/RotateAroundAxis）。
- **Stage 2 裝閘時補一條閘**：下游 golden 不准依賴 cook fallback default，共用場景參數必須顯式 pin（本批 particlefield_probe 隱性耦合就是缺這道閘才漏）。
- **parity-gate 做完再回 OBJ/menu lane**：OBJ 續採（LoadObj/WriteToFile/DataPointImportExport）、menu-bar checkmark 已補（`701b2a1`）、chip `task_7c964566`（obj_parse CRLF/負索引 refute）。
- **★先處理（柏為在場）**：① **menu-bar checkmark**（柏為授權「可以先補」）——擴充 vendored metal-cpp wrapper 接 NS::MenuItem setState/menu-validation，讓 View 視窗 toggle 顯示打勾。② chip `task_7c964566`（obj_parse CRLF/負索引 refute，可能真 regression）。
- **OBJ 子-lane 續採（obj_parse seam 已建，[G]）**：① **LoadObj**（mesh rail 已存在；count-before-cook 需 parse-cache 縫＝中等；obj_parse_distinct guard 已補可安全接）② **WriteToFile**（io/file，String rail）③ **DataPointImportExport**（JSON+point-buffer，JSON seam 已開）。**deferred**：SVG/LoadDataClip/network+device island（柏為域）。
- **或換 lane**：菜單③ Command ctx-var `[G]`（低值）；或更大 `[Y]` 縫（camera/3D-render/PointSim，動 cook-core 須先 scout + 4 島守門綠 + refuter）。
- **dict-ctx 已釐清非 loader lane**：無 file Dict producer，唯一 non-device opener=audio GetBeatTimingDetails（audio lane）。
- **autonomous 菜單（解鎖÷風險排序，全已 ground-truth）**：
  - **✅ ① vec4/Color host-value output 家族＝DONE（`c32eed9` 2026-06-28）**——scout 揭穿前提 stale：4 顆早已 BUILT，只剩 PickColorFromList（非 vec4-spread 縫，是 ColorList-consumer→host-value(vec4)-emit）→已建完，--bite 510。**家族完整。**
  - **② B 軌 menu-bar＝`[Y]`（★推薦下一個）**——最後一條乾淨純皮 `ui/` Tier1 skin gap（top menu bar vs floating Toolbar）；自走做完→eye-hand 截圖 + ui_census→落待驗收佇列（UI 觀感柏為事後驗）。註：柏為 decision queue 有「menu-bar chrome 範式 native-NSMenu vs TiXL-imgui」一條——預設照 TiXL（imgui menu bar），不選 native。
  - **③ Command ctx-var 縫＝`[G]`**——medium，SetXxxVarCmd command-rail SubGraph scope（Set{Float,Vec3,String}Var 的延後那半），低值。
- **★效能優先項目（柏為 2026-06-27 指定，按 ROI 排序）**：
  - **✅ P1 PSO 快取接線（DONE `5c22f7f` 2026-06-27，--bite 509）**——51 個 point op cook site 從每幀 `newComputePipelineState` 改走 device-global `cachedComputePSO`（`tex_op_cache.cpp:203`）；同 kernel 只建一次 PSO。**+48 個自建-device selftest 補 `clearTexOpCache()`**（cook 路徑現在把 PSO 存進 process-global cache → selftest device teardown 前不清 = 跨-device UAF；既有 field selftest 契約，device 建立前清）。particle_system/pointtrail/pointtrailfast 用 pre-cached SimState/ring，正確跳過。淨 -146 行。工法：canonical(addnoise)親驗→Workflow 50 檔 fan-out(transform→refuter)→中央合 build。試壓全 golden byte-identical。
  - **P2 批次 GPU 提交 / 移除誤植 per-op wait**（中，142 個 point op 各自 `commit()+waitUntilCompleted()` → 一幀一個 command buffer 末端一次 commit；需穿 `PointCookCtx`）。**★延後到 TiXL clone 完成後再做（柏為 2026-06-27 21:02 拍板）**——現在接會讓 cook-core 碼亂、clone 仍進行中。**診斷已驗證為真債**：per-op `waitUntilCompleted` 是 clone TiXL 時把「selftest readback 才需要的 wait」誤植進 production cook body（已對碼證實：filterpoints/clearsomepoints 等 production readback 全 `g_cap*`-gated = selftest-only，production wait 後不讀 = 純浪費；TiXL 不 per-op sync，整條鏈靠 driver hazard tracking）。**那個 session 已試寫 helper（`cook_wait.{h,cpp}` defer-sync 切換點）但已回退**（untracked/未接線/未進 build，且 header 自稱 `--benchmark-hazard 已實測` 為假 = working-tree 地雷，已移除以防 autonomous 誤接）。**正確順序（非直接接 helper）**：(1) 先 profile 證 wait 真佔可觀幀時間（目前零數字＝否則憑信仰優化）；(2) 寫 `--benchmark-hazard` 證 Metal 同 queue **跨 command buffer** 對 shared-storage buffer 的 hazard ordering 足夠（每 op 獨立 cmdbuf；defer 後跨 cmdbuf 讀寫順序靠這個，golden 單 op 隔離測不到整鏈同幀 race）；(3) 證明後才接線。詳 `DEBT_LEDGER.md §B`。
  - **P3 dirty-flag 接線**（中，`pullResidentFloat`+`bumpLiveSources` 已在 `resident_eval_cache.cpp:196-244` 建好測好，接進 production eval path；MV 調參有感，VJ 幫助有限）
  - **P4 增量 patch 接線**（大，`patchSetConstant`/`patchAddConnection` 等 10 個函式已建好測好，接上後使用者拖滑桿 → 只增量更新不全重建；這是真正解決 push 全刷的那把鑰匙）
  - **注意**：P1+P2 讓全刷**變便宜**；P3+P4 讓全刷**變少**。做完 P1+P2 後 push 的「float 改一個全重算」問題**仍存在**，只是代價更低。P3+P4 才真正解決這個問題。
- **過去「柏為域」重新分類（柏為 2026-06-28 政策：拆成機器閘/事後驗/預先拍板，無一項需預先在場）**：
  - **value-emit consumer rail＝`[Y]`**（方案 A 已拍板，決定文件 `VALUE_EMIT_CONSUMER_RAIL_DECISION.md`：forced-cook+1-frame settle+scope-correct；剩 line 25 兩條 latent-risk 是技術前置非品味→自走解+接 consumer；事後驗 PointToMatrix→camera 視覺結果）
  - **camera object / ICamera＝`[Y]`**（照 TiXL schema；sw forward-push vs TiXL pull-graph 的 adaptation 是技術判斷→自走，事後驗 camera 手感）
  - **3D-render Command tail（~40-60）＝`[Y]`**（render→觀感，自走，事後驗畫面）
  - **PointSimulation pool ABI（~15-25）＝`[Y]`**（照 TiXL ABI，粒子行為 golden 守正確、觀感事後驗）
  - **device-IO / loaders（~30-40）＝`[G]`**（load round-trip golden 機器可驗，照 TiXL 格式；dict-ctx producer 在此）
  **無一項 `[R]`——都不需柏為預先在場，`[Y]` 做完落「## 待柏為驗收」佇列。** value-emit/camera 動 cook-core，照方法論鐵律先 scout + 4 島守門綠 + refuter。
- **★方法論硬規（stale label 反覆踩過）**：seam 開採前**必先 ground-truth scout**，seam_map「解鎖量」不可信。每個 sub-seam 配 CPU-readback golden（debugCookedBuffer）+ injectBug + refuter；碰 cook-core driver 的必驗 4 島守門綠。scout 揪出 stale label 後**必立刻寫回 SSOT**（[[sw-groundtruth-writeback-rule]]）。
- **過去「需 routing/greenlight」重新分類（同政策，全部非 `[R]`）**：reduction 縫(PointsOnImage GPU-determined count,1op 低值)＝**`[G]`**／resident list-state slot(AmplifyValues/Damp/Keep/Merge*)＝**`[G]`**／ContextVarMap string/object 通道＝**`[G]`**(DEBT_LEDGER `contextvar-channel-sub-seam`)／**string-value＝`[G]`，gate 已解**(前置 `task_32b5b6e5` resident string-wire 已 **CLOSED `0bb25e2`**，string Phase C OPEN；舊標「前置 close」=stale，[[sw-string-rail-resident-gate]] 已過時)／字面 matrix cook-type(camera family)＝**`[Y]`**／dict-ctx＝**`[?待scout]`**(死在 device-IO producer→先 scout 是否 `[G]`)。
- **★策略問柏為（適時）**：clone 61%，剩 sub-seam grind vs pivot MV-tooling（你的真 target [[simple-world-real-target-mv-tooling]]）——你定。
- **不需大縫的零星自走（若要 loop 不停可撿）**：B 軌 `ui_census --gaps` 純皮零星（多卡 output_window/encoder/抽介面）；維運 chip（seam_map 重校 `task_5aa45438`、ui_census false-neg `task_a47c8f98`、memory shrink `task_2487de3c`）；CTE/SP/RO 低優先 polish。**但這些是邊角，非承重——大方向等柏為 routing。**
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

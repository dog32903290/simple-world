# simple_world ⇄ TiXL 全對齊 — 最快路徑計劃表（全並行）

> 柏為 2026-06-23:「程式碼全翻完了，寫一份全部可以並行、以最快路徑為原則的計劃表。」
> **本檔=頂層路由權威。** sub-plan:節點/縫=[SEAM_COMPLETION_PLAN](SEAM_COMPLETION_PLAN.md)、債=[DEBT_LEDGER](DEBT_LEDGER.md)、非節點 spec=[alignment/](alignment/README.md)。事實以 git/碼為準。
> **開頭定位**：跑 `tools/sw_status.sh`（三區 ① LIVE git+census 現測 / ② STAMPED@結帳 bite / ③ HAND 手寫接力）。**結帳**：`sw_status.sh --stamp <bite PASS>` 蓋章 + 手寫更新下方 Active Lane/Conflict/Next + `--check` 過閘。舊 snapshot 在 [MASTER_PLAN_HISTORY.md](MASTER_PLAN_HISTORY.md)。

## Current Snapshot
<!-- sw_status:begin （機器塊：結帳時 tools/sw_status.sh --stamp <bite PASS> 寫入；勿手改） -->
HEAD: b77789e
DIRTY: 1 files
CENSUS: 456 / 749 done
BITE: 508 PASS | NO-BITE=[detectbpm]
STAMP_AT: 2026-06-27T16:10
<!-- sw_status:end -->

- 引擎 clone **57%（427/749）**。★**「clean-leaf 採盡」兩度被推翻**：(1) S2/S3 脊椎查出早已蓋好+golden 綠→單輸入 texture-rail 葉子可採；(2) **multi-image seam 也早已建**（gather 綁 4 input texture，Blend/Displace/Combine3Images 已證）→ **fixed-port 多輸入 op 是乾淨葉子**。**本 session 六批已採 10 顆 image 葉子 + 1 小 seam**（batch1 `627458b` Mandelbrot+DepthBuffer、batch2 `fc92eca` ImageLevels+2×Ryoji+HoneyComb、batch4 `9fa193e` CombineMaterialChannels、batch5 `646544d` HSE+MosiacTiling、batch6 `0fd14a4` MultiInput<Texture2D> gather 擴充 + PickTexture）。**★方法論血證（4-5 次）：census/scout 系統性把「已建的 seam」誤報 gated（S2/S3 脊椎、multi-image gather 都早已建）→ 別信 census done/todo，ground-truth=讀 cook path（派 Plan agent 深讀，不是 Explore census）。** 選葉子要開 .hlsl 親看（單 pass？非 compute-reduction？非 compound？fixed-port？）。
- **柏為-absent 自走可採 = 第三軸體驗復刻尾**（[EXPERIENCE_PARITY_PLAN](EXPERIENCE_PARITY_PLAN.md)：純皮 Tier1 / Output O3 / 維運），eye-hand 驗、不碰 cook-core。
- 本 session 落地：**field 紅修**（`644d100` AudioReaction 救回）+ **quick-add 型別色**（`e427d55`）+ **ui_census 校正×3**（`56a2057`/`708b253`/`7765469`）+ **out-snapshot-png**（`5a9a51f`）+ **★S1 輸出解析度縫端到端完成**（柏為 23:35 授權：`1b53b12` cook-core override hook + `a93f2dc` UI 選擇器,皆 refuter 8/8 SURVIVES）→ B 軌 out-resolution-selector 自動 DONE,B 軌 16→19。

## Active Lane
**none — ★SEAM 1 point-into-frame value-emit pass ✅（2026-06-27 16:10 結帳，HEAD `b77789e`，--bite 508 / FAILED=[]，census 456/749）。** cook-core seam scout 推翻早先「觸底」誤判→value-emit 已建（additive 第二 pass，refuter MERGE-SAFE）。**剩唯一 autonomous cook-core 縫＝C-string（低值 2 op，下一條可選）；之後真進柏為域（consumer rail/camera object/PointSim pool/device-IO）。**
- **✅ SEAM 1 value-emit pass（`fe35682` merged → `b77789e`，refuter MERGE-SAFE 6/6，additive：point_graph.cpp/EvalContext/resident diff 全空）**：新 pass `cookPointValueOutputNodes` 在 `frame_cook.cpp:391`（`pg.cookResident` 後）跑，host-read finished StorageModeShared point buffer（`residentCookedPoints(path)` resident-path keyed，非 flat debugCookedBuffer），復用 golden `pointToMatrixRows`。**wired 2 op：PointToMatrix(emit half，flip 自 deferred)＋GetPointDataFromList**（int.Mod Euclidean，W=FX1@12 對位）。fork[getpointdata-vec-as-scalar-ports]=Vec3/float/Vec4 攤上 extOut[8]（同 RequestedResolution Size-as-2-floats，value-identical 只 wire-cardinality forked）。
- **★★兩條 NAMED LATENT RISK（今天 inert，0 forward consumer；柏為建 PointToMatrix→camera consumer rail 時必先讀）**：① `latent-pointvalue-emit-one-frame-late`——新 pass 寫在 cookResident 後、其餘 emit pass 寫在前；未來若有 in-graph consumer 在 cookResident 期間 pull 這些 emit→讀到**前一幀**值（sw forward push-cook 本質，TiXL 是 pull-graph 無此問題）。② `latent-stale-points-off-display-subtree`——`outBuf` 無 per-frame invalidation＋resident cook 是 target-driven（只 cook 顯示子樹）→Points src 在顯示子樹外的 PointToMatrix 讀到 stale 前幀 buffer（non-null 錯 count）非 identity。**goldens 用 stub accessor 不覆蓋 multi-frame consumer path**——consumer rail 開工前這兩條要先解。
- **✅ 3 final autonomous leaf（`504f149`，refuter MERGE-SAFE 全 PASS，spine git diff 空）**：FindClosestPointsOnMesh（mesh-input rail，brute-force Ericson tri-distance 1:1，unwired-Mesh→identity）/ SampleCpuPoints（PointList host rail，Bezier+quat-slerp host math，.t3 TangentScale=0 collapse trap）/ JoinLists（StructuredList MultiInput N-way concat，Result-only，fork[joinlists-length-deferred]=Length int 騎 value rail 延後）。三 rail（mesh-input/CPU-readback/StructuredList）scout 證早已建→這 3 顆是 leaf 非 seam。
- **✅ grind-E trail 3-op（`40aac24` merged → main `efdfd21`）**：PointTrailFast/PointTrail（stateful cross-frame ring，ride 已建 ensureState）+ GrowStrains（stateless，2-input cartesian + GrowthMap 騎 inputTextures[]）。**spine 4 檔 git diff 空**（registry 簽名未加寬，count=f(param) 騎 file-static stash 非 PointCountFn）。refuter MERGE-SAFE 6/6。**★.t3 trap：PointTrailFast ring stride=TrailLength+1 且 CycleIndex==FrameCount（+1/frame，非 advance-by-TrailLength，.cs 註解誤導）。** fork[growstrains-white-growthmap-fallback]（unwired→1×1 white 避 black→NaN 退化）。
- **✅ GrowStrains normalize 修（`e4df20d`）**：refuter 揪出 metal:93,103 加了 TiXL 沒有的 normalize() on rotations（qRotateVec3/qMul 不內部 normalize）→非 default 時非-unit quat 會偏→移掉求 byte-faithful（golden 仍綠，unit quat no-op）。
- **★shader-graph-Field 2-op leaf（`3fea8f6`）refuter MERGE-SAFE**：PointColorWithField（evalField(pos,1).rgb→mix(Color,field,Strength)，.t3 Strength=1/Range dead）+ SelectPointsWithSDF（evalField(pos,0).w，Mode/Mapping/GainAndBias→FX1/FX2，.t3 DiscardNonSelected=FALSE/GainAndBias(0.5,0.5)=identity，52-byte params）。ZERO gather/driver edit，clone MoveToSDF leaf 模板。
- **✅ vec3-clear bug 修(`0704185`)**：grind-C STOP 揪出 grind-B 隱性 bug——`frame_cook` 漏 clear vec3Vars→vec3 ctx-var 跨幀殘留。修=加 `vec3Vars.clear()`(單一 clear site,bool 騎 intVars,string 不存在)+cross-frame golden section G(移掉 fix 則 production FAIL 證咬)。grind-B refuter 漏(golden within-frame only)。
- **⏸ C ctx-var String DEFER**：grind-C scout 證**非 mirror Vec3**——GetStringVar=String value producer/SetStringVar=Command 輸出,String 是獨立 currency(cookStringNodes/extStrOut),float-only cookStatefulValueNodes 裝不下。真 medium seam(thread ContextVarMap 進 string cook+2-pass),2 op 低值→緩。
- **★shader-graph-Field 群 scout 證 stale label**：**PointColorWithField + SelectPointsWithSDF = LEAF FAN-OUT on 已建 `gatherPointFieldTree`**(同 MoveToSDF,更簡單,evalField(pos,w) 選 color/distance,.t3 無破壞性 default 分支)→**便宜採 2 op clone MoveToSDF**(正在派)。CustomPointShader=真 seam(arbitrary HLSL→MSL string,柏為-judgment defer)。
- **✅ grind-B ctx-var Vec3(`5ae642b`)+A-fan-out Damp/Keep(`2e43ca0`)**（合一 refuter MERGE-SAFE，additivity gate 全清）：**SetVec3Var/GetVec3Var**（加 ContextVarMap `vec3Vars` 欄,fork=typed-channel vs TiXL ObjectVariables boxing,round-trip byte-identical,namespace 不撞）+ **DampFloatList**(Method0 LinearDamp)+**KeepFloatValues**(ring accumulator)（ride 已建 FloatList slot,FloatListState 加 5 欄 additive,point_graph 威脅 untouched）。.t3 OVERRIDE C# ctor 全審。NIT:DampFloatList frame-0@lfx=0 dt-gate 微歧(self-correcting,註解誤導,follow-up)。
- **★3 個 deferred 到柏為-call/未來小縫**：**Matrix var**(GetMatrixVar 出 Vector4[]=16 float,stateful out[8] 裝不下,需 matrixVars 通道接 matrix-output cook pass=cross-rail)/**MergeFloatLists**(Ltp/FailOver 需 int-list wire)/DampFloatList Method1 DampedSpring(需 spring 積分+LastFrameDuration)。
- **✅ grind-A list-state resident slot(`143876f`)**：FloatList cook flow 加 per-node 跨幀 state slot（仿已建 ColorList/String host-state，非 GPU ensureState），AmplifyValues 港。**additive**（stateful flag default false，stateless floatlist op byte-unchanged，flat+resident 雙路）。**★.t3 OVERRIDE C# ctor**（Smoothing 0.95/MixAverage 1.0，抓到 trap）。refuter MERGE-SAFE（additivity+persistence 兩 gate，RAMP f3=12.5 證持久）。**slot 解鎖 DampFloatList/KeepFloatValues/Merge*（fan out 中）。**
- **★leaf pool 比 master scout 估的小很多**（field-SDF 1/18、mesh 0/15 都近採盡）——「leaf fan-out」桶多數 ground-truth 後是 seam-gated。真 clean leaf ~20-40 非 ~150-170。**策略：focus cook-core spine(確認產出) + rail-specific seam(texture-into-mesh/points-into-mesh/CPU-noise，各解鎖一族)；leaf 桶先 scout 確認非空再派(別空跑)。** 待寫回 SSOT。
- **★柏為 11:16 表(7 大 island seam)=stale，master scout 全 debunk＋寫回 SSOT(`b037566`)**：**NO dx11 keystone**(最大 myth，render_command.h 已覆蓋)/5-of-7 已建或 partial/3 條獨立已鋪島(field-SDF/point-buffer/Metal-render)。真剩 ~309：~150-170 leaf fan-out(autonomous)+~25-30 cook-core spine(F-E)+~110-130 柏為域(3D render tail/network-io/loaders/gizmo)。**stale trap 在 census docs 非 seam_map。**
- **✅ grind-F second-point-buffer(SetAttributesWithPointFields `24f1bbd`)**：LEAF（inputs[1] 可定址零 cook-core，同 SnapToPoints），2-point attr-transfer blend，refuter MERGE-SAFE byte-faithful。NIT:gradient white-step 1.0 vs 0.9884(sub-1.2% 色 task_07476ef4)。
- **✅ Raster3dField(field-SDF leaf)**：**★finding:field-SDF leaf rail 近採盡——12 未 port 只 1 clean，其餘 11 blocked**(sdftocolor=Gradient 柏為域/heightmap=Texture2D/render2dfield=render Command/subdivpattern3d 高風險)。待寫回 SSOT。
- **✅ point-modify 三 rail（前 6 op，全 refuter MERGE-SAFE）**：SDF/count-multiply（MoveToSDF/SdfReflection/RaymarchPoints）+texture-into-points（SamplePointAttributes/DisplacePoints2d/TransformWithImage）詳見 git。
- **★campaign cook-core 子縫 grind-list**：**F✅ A✅ B✅(Vec3)**→C 緩(medium String/Command seam)→**D 下一條** dict-ctx host rail(~7,純 host 非 device-IO,新 host currency+cookDictNode)→**E** point-sim cross-frame(~8,5 簡單 ride ParticleSystem state factory,E-hard PointSim pool ABI 需柏為)。**+ 並行 leaf:shader-graph-Field 2 op(PointColorWithField/SelectPointsWithSDF clone MoveToSDF)。** rail-seam 後續:Mesh-input(4)/CPU-readback(5-7)/texture-into-mesh/CPU-noise。**spine 序列 + leaf 並行(不同檔);leaf 桶先 scout(已採盡多)。**
- **★需柏為架構決定（grind 到再問，已先擺出）**：**E-hard** PointSimulation pool-growth ABI（growable pool 要新 resident-state ABI 還是復用 ParticleSystem 固定 pool？）/**G** asset/file loader（LoadObjAsPoints/LoadSvg：file-IO/asset lifetime model？device-IO-adjacent）。
- 前批：anim+list harvest（`49582bd`）+matrix seam（`96e4923`）+維護加固 工作1+2+folder-package+ColorThemeEditor 詳見 history。**★★方法論鐵律（柏為 2026-06-27 定，根因修）：seam 開採前必 ground-truth scout；scout 揪出 stale label（已建/真數/依賴）後**必立刻派 plan-update subagent 寫回 `tools/seam_map.tsv`+census docs**，否則每 session 重栽同坑、差點重蓋已建縫（一晚 6+ 次）。scout→write-back→build，漏寫回=下次重栽。[[sw-groundtruth-writeback-rule]]**

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
下個 `/sw-batch` 開頭先跑 `tools/sw_status.sh` 定位。HEAD `e4df20d`，--bite 503，census 452/749（60%）。**★狀態：autonomous campaign 觸底中——final-seam scout（2026-06-27）證 mesh-input/CPU-readback/StructuredList 三 rail 全已建，autonomous 殘渣只剩 3 顆 leaf（FindClosestPointsOnMesh/SampleCpuPoints/JoinLists-Result-only），正在派 build。採完這 3 顆＝orchestrator-autonomous floor 觸底。**
- **★採完 3 顆後 present 柏為「autonomous campaign 完」summary**：剩 ~290 op 全柏為-device/render/camera/sim 域，按 scout 分群：camera3d/3D-render tail ~40-60（DrawMeshAtPoints2/draw-at-points 族/render Command）、PointSimulation pool ABI ~15-25（sim/* 含 SimFollowMeshSurface，E-hard）、**point-buffer→value-emit 架構縫 ~10-15（一條縫解鎖 GetPointDataFromList/PointToMatrix-emit/GetTextureSize 族，柏為設計）**、device-IO ~30-40（dict-ctx/loader/audio/MIDI/file-IO）、gizmo/loader/field-SDF/mesh-gen tail ~80-100（已測近零 autonomous yield）。
- **★最該先問柏為的一條架構縫＝`point-into-frame value-emit pass`**：resident value-emit 看不到 point buffer（PointToMatrix `evaluate==nullptr` defer + GetTextureSize 同壁）→一條縫解鎖 value-out-from-points 整族。
- **★當前下一批＝RaymarchPoints**（own lane，two-mode count-multiply：Raymarch/KeepSteps，consume MovePointsForwardToSDF.hlsl + MaxReflections/PointMode，.t3 buffer-sizing chain 較糾纏）。同 SDF gather + count-multiply rail（如 SdfReflectionLinePoints），但兩 mode + 不同 line layout，需細解 .t3。
- **★後續 point-modify sub-seam（各需 scout）**：second-point-buffer（SetPointAttributesWithPointFields，FieldPoints=2nd point buffer 輸入）/其餘 point/modify 未盤點 op（matrix-uniform/SDF/texture 三 rail 已開，剩的開 .hlsl 親看分類）。**★必查 .t3 defaults（教訓 ×2）+ 碰 driver 的驗 4 島守門。**
- **★方法論硬規（今晚 5 次踩 stale label）**：seam 開採前**必先 ground-truth scout**，seam_map「解鎖量」不可信。每個 point-modify sub-seam 配 CPU-readback golden（debugCookedBuffer）+ injectBug + refuter；碰 cook-core driver 的必驗 4 島守門綠。
- **★仍需柏為 routing/greenlight（真 owner-lock）**：reduction 縫（PointsOnImage，GPU-determined output count，1 op 低值）／resident list-state slot（AmplifyValues/Damp/Keep/Merge*）／ContextVarMap string/object 通道／string-value（前置 close `task_32b5b6e5` [[sw-string-rail-resident-gate]]）／字面 matrix cook-type（camera family）／dict-ctx（死在 device-IO producer，你的域）。
- **★策略問柏為（適時）**：clone 58%，剩 ~12 sub-seam grind vs pivot MV-tooling（你的真 target [[simple-world-real-target-mv-tooling]]）——你定。
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

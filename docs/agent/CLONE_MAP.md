# CLONE_MAP — TiXL 克隆真實地圖(2026-07-04 census 修準 + 三縫對 code 驗證)

**出身**:2026-07-04 修準 op_census(原只涵蓋 image+field 兩島=漏島幻覺;根因=external/tixl
sparse checkout 只 materialize 兩島源夾,已補全 12 島 @ SHA 395c4c55)+ 三條「沒蓋的縫」對 code
驗證(全非空牆,大半是 regex 誤分類)。數字全 code-derive,非手寫。

## 真數(op_census 修準後,749 對上「749 真 Instance<> 節點」)

**749 total / 473 done (63%) / 276 todo (37%)**

| 島 | 總 | 做 | 剩 | 島 | 總 | 做 | 剩 |
|---|---|---|---|---|---|---|---|
| numbers | 223 | 192 | 31 | mesh | 44 | 20 | 24 |
| image | 114 | 76 | 38 | string | 33 | 27 | 6 |
| point | 100 | 70 | 30 | flow | 32 | 14 | 18 |
| render | 69 | 16 | 53 | particle | 19 | 10 | 9 |
| io | 62 | 4 | 58 | data | 1 | 0 | 1 |
| field | 52 | 44 | 8 | | | | |

## 276 todo 三分類(全分類,0 未歸)

### leaf-ready 93 顆 — ⚠實際只 40 原子可手刻,53 是複合(2026-07-04 柏為攔查,見 `docs/agent/LEAF_ATOMIC_CENSUS.md`)
帳面家族:point 17/mesh 17/render 14/camera 14/vecmath 12/flow 8/string 6/field 2/其他 3。
**但歸縫(seam_map regex)從沒切原子/複合這一刀** → 這 93 顆裡 **53 顆(57%)是 TiXL 複合節點
(.t3 有子圖),該走 .t3 importer 巢狀重放、不是手刻**;真正可手刻的原子只 **40 顆**。
最糟:**mesh(15/17 複合)、point(16/17 複合)** 兩個原以為的「大甜點」lane 幾乎全複合;唯一 100%
原子 lane = **vecmath 12**。乾淨白名單 + 53 複合名單見 LEAF_ATOMIC_CENSUS.md。

### seam-build 86 顆 — 要先蓋縫(但帳面高估,見下方三縫驗證)
compound-graph 11 / keyframe-anim 10 / flow-var 10 / particle-force 9 / feedback-advanced 8 /
point-sim 7 / field-raymarch 6 / compute-dispatch 6 / asset-load 5 / list-state 4 / text-font 3 / 零星單顆。

### domain-blocked 97 顆 — 需新裝置/新島/柏為硬體域
device-io 57(MIDI/OSC/DMX/serial=柏為域)/ pbr-lighting 16(新 3D-render 島)/ mesh-draw 7
(DrawMeshUnlit 已建,只 PBR 變體卡)/ postfx 6 / point-io 6(CPU-readback 已建,只 CpuPointToCamera/DMX 卡)/ dict-ctx 5。

## 三縫對 code 驗證(2026-07-04,只信 code;seam-build 帳面高估的證據)

### compound-graph 11 → 縫不存在(host 機器 live,orchestrator 親驗)
- host 機器全 live:`resident_eval_graph.h:178-200` buildEvalGraph 遞迴 inline compound child +
  骨7 boundary-flatten;production 真走巢狀 resident:`app/src/app/frame_cook.cpp:281` buildEvalGraph
  + `:387` cookResident(親驗)。`resident_eval_graph.h:10-13` 舊「NOT yet wired」註釋已標 STALE。
- 11 顆真分佈:**6 可直接採**(BlendImages/MakeTileableImage/MakeTileableImageAdvanced/SubdivisionStretch
  乾淨;CompareImages/Glow 需 golden 驗多pass)+ **2 分類錯的葉子**(FakeLight/NumberPattern 自帶 .hlsl,
  該歸 image-fx 葉縫)+ **3 卡真下游縫**(ScreenCloseUp→pbr-lighting/postfx、SortPixelGlitch→compute-dispatch、
  AdvancedFeedback2→compute+feedback)。
- **骨7b caveat(monitor 親驗)**:host flatten 連 mixed-MultiInput slot 排序都已修
  (`resident_eval_flatten.cpp:270` 單一 sym.connections pass,mesh 家族 replay 不亂序)——**但缺一顆
  mixed-MultiInput golden 牙**(碼修閘沒補,回歸無防護)。派工人補一顆,過新出廠閘。見 memory
  [[gpu-compute-replay-proven-boundary-flatten-next]] 骨7b 收尾。
- **seam_map:59 這條桶該拆**:6 移 image-leaf、2 移 shader-leaf、3 移各自下游縫。

### field-raymarch 6 → executor live 真綠,只 1 顆真接線
- executor live 到完整生產路徑:`field_render.cpp:42-116` renderField2d + `:118-215` renderField3d;
  今天 3 顆 golden 親跑綠(field-render/field-raymarch/raymarchfield-output,binary Jul 4 06:27)。
- 6 顆真分佈:**Render2dField**(純接線~半天,唯一名副其實)+ **VisualizeFieldDistance**(準接線,需驗
  線框 shader 同源)+ **SDFToColor/SdfToVector**(field-graph codegen,接 assembleFieldMSL 非 executor)+
  **SampleFieldPoints/ApplyVectorField**(point-compute,卡 compute-dispatch)。
- 漏網:field 島「SDF generator 已採盡」是錯的,還有 **HeightMapSdf + SubDivPattern3d** 兩顆乾淨 generator 葉。

### feedback-advanced 8 → 縫真沒蓋但「跨幀機器不夠」大幅高估
- 現有機器:2-buffer pair(`point_graph_internal.h:185 FeedbackPair` + `:317 ensureFeedbackPair` + toggle);
  下界證明=AdvancedFeedback/AfterGlow/AfterGlow2/KeepPreviousFrame 全有 golden 綠。**無 N-frame history array**。
- 8 顆四分類:**2 零新機制**(FluidFeedback=AdvancedFeedback clone、WaveForm=無跨幀純 analyze 誤歸)+
  **1 卡子op**(DetectMotion 跨幀走已建 KeepPreviousFrame,真缺 OpticalFlow+TemporalAccumulation 兩子op)+
  **2 需單張 in-place accumulator**(SlidingHistory/RemoveStaticBackground,比 N-history 簡單)+
  **3 真新機制**(TimeDisplace=Texture2DArray N-frame ring;SimpleLiquid/2=compute-UAV 雙緩衝+iteration)。

## 真骨頭(剝掉誤分類後,真正要判斷力蓋的機制,反覆匯聚)

1. **SimpleLiquid×2 幀內迭代機器(原「compute-dispatch」,2026-07-04 軌B scout code 驗證後大幅縮水)**:
   image-compute(`point_ops_crop.cpp`)+point-compute(`point_ops_movepointstosdf.cpp`)路徑**早已 live**,
   本檔原「遠大於6/解鎖最多」是印象誇大。真相=原列 6 顆裡 **3 顆是接線葉子**(SampleFieldPoints/
   ApplyVectorField ~4h、SortPixelGlitch ~8h 含 HLSL→MSL)→降級併入 leaf 波;**AdvancedFeedback2=誤報**
   (走 RenderEncoder 已綠);唯一真骨頭=**SimpleLiquid×2**(一 cook 內多 dispatch+中間 buffer 交換+3
   RWTexture2D 多 UAV,~24h;現有只 1 UAV/跨幀 toggle,缺幀內 ping-pong)。**只解鎖 2 顆。**
2. **persistent accumulator / texture-array**:TimeDisplace(N-slice ring)/ SlidingHistory /
   RemoveStaticBackground(單張 in-place EMA)。三顆,兩種型。
3. **pbr-lighting / 3D-render 島**:16 顆 + ScreenCloseUp 匯入。新島,最大單塊。

**判斷密度:骨3(pbr島,16顆)>骨2(accumulator,3顆)>骨1(SimpleLiquid 幀內迭代,2顆)。骨1 縮水後
「蓋一根解鎖一批」只對 pbr島 真成立;能立即並行的葉子從 93 增至 ~96(3 顆 compute 葉降級)。其餘
seam-build 帳面被誤分類灌水。**

## 複合重放軌 — 產能真引擎(2026-07-04 code-verify,壓測 scout 樂觀後)

**這根之前 CLONE_MAP 三骨頭完全漏了**(那時沒切原子/複合,把複合誤當 leaf 手刻)。驗完真相:
機器已建、資產在庫、真閘是子圖依賴。

- **機器已建(不是漏掉的骨頭,是已蓋)**:t3_import(`sym.atomic=false`+children+connections)、
  開機載入 catalog(`catalog_boot.cpp`,fail-soft)、`entercompound`/`spawnsymbol` 鑽入
  (`document_navigation.cpp:52 pushComposition` 拒原子納複合)、resident flatten 遞迴 inline。
  一顆端到端綠:RadialGradient.t3 import→cook→GPU→閉式 parity(`t3_import_radialgradient_golden.cpp`)。
  commit 46b5b92/141fc9b/a7e6165。**柏為 07-02 拍板的巢狀節點模型已實現**(07-02 memory「模型要重設計」已過時)。
- **資產全在 external/tixl,importer 吃原版**:`assets/catalog_t3/` 的 8 顆 = TiXL 原版逐 byte 相同
  (RadialGradient/BoxGradient diff 證)。「補 291 .t3」是 scout 錯判 —— 不是手刻,是複製/指向 external。
- **真閘=子圖依賴**:複合 child 靠 GUID→sw type 映射解析(`t3_import.cpp` findSpec/swTypeForSymbolGuid)。
  一顆複合能重放 **iff 子圖引用的原子都在 sw registry**。→ **原子軌不是小頭,是複合軌的前置基底**;
  鋪原子=複合子圖有料=複合逐批自動解鎖重放。兩軌其實是一條依賴鏈。
- **可操作(比「補 291 .t3」準)**:(a)大量灌 external 複合 .t3 進 catalog_t3/(boot fail-soft,不 crash)
  → 跑一遍自動生「子圖齊備=現在可重放」清單 vs「卡缺原子」清單;(b)並行鋪原子,可重放集隨之長大。
- **caveat**:draw/text 家族(DrawTubes/Text)TiXL 是複合但本質渲染,sw 已有 DrawPoints/Lines 單一路徑,
  可能單一實作達 parity 繞過重放 —— 逐顆判(見 LEAF_ATOMIC_CENSUS caveat)。

## 給大量產的操作接縫(2026-07-04 軌A scout 排定 11 lane)

- **⚠先過原子閘再談並行(2026-07-04 柏為攔查,見 LEAF_ATOMIC_CENSUS.md)**:93 leaf-ready 裡
  **只 40 顆原子可手刻,53 顆複合走 .t3 importer 重放**。「~107 顆手刻並行」是污染前的錯數 ——
  真正可手刻的第一波 = **40 原子**(那 3 顆 compute 葉 SampleFieldPoints/ApplyVectorField/SortPixelGlitch
  本身也是複合,走已建 executor 重放,不算手刻)。
  registry 併發結構(11 lane、node_registry_draw 拆檔)仍成立,但**每 lane 先套 40 原子白名單**、不整批吃家族數。
- **40 原子 lane 分佈**:vecmath 12(唯一全乾淨)/camera 9/render 5/string 4/flow 4/mesh 2/point 1/field·data·tex 3。
  併發:vecmath/string/flow/mesh/point 各自註冊零競爭;camera/render 走已拆的 node_registry_draw_*(eaa678b)。
- **caveat**:53 複合裡 draw/render 家族(DrawTubes/Text…)本質渲染,sw 已有單一 render 路徑,**可能單一實作達
  parity 非重放整個子圖**——逐顆判;DustParticles/Quiz/Gizmos 這種真場景組合才無疑義重放。
- **★前置已落地(commit eaa678b,2026-07-04)**:`node_registry_draw.cpp` 已拆家族分檔
  (node_registry_draw_render/_camera/_flow/_data.cpp,原檔收斂成純 concat driver、零 literal op 行);
  drawSpecs() 506 顆拆前拆後 `--dump-nodespec-types` diff **IDENTICAL**,build+check-arch+line-ratchet
  +specdedup 全綠。**37 顆的 4 lane 已從 0% 解放到 100% 並行 → 11 lane 全部可並行採。**
  (選拆檔非 orchestrator 代理:後者違 no-fieldwork 律 + 每波重複瓶頸。)
- 每顆過新 golden 出廠閘(GOLDEN_STANDARD 五反型)。
- **judgment 活(刀刃,強腦,非量產)**:SimpleLiquid 幀內迭代(2) → accumulator(3) → pbr-3D 島(16)。
- **柏為域/延後**:device-io 57(硬體)、pbr 16(大島)——clone-first 範圍內晚做。

## census 防呆(這次踩的坑,寫下免重栽)
- op_census 靠 `grep ': Instance<' external/tixl/Operators/Lib` 掃 TiXL 節點宇宙 → **external/tixl
  必須是全島 checkout**,sparse 會結構性漏島(這次只 field/image → 幻覺 166)。
- 開工前檢查:`git -C external/tixl sparse-checkout list` 應含全部 12 個 Operators/Lib/* 島夾;
  漏了先 `sparse-checkout set` 補齊(SHA 鎖 395c4c55,gitignored 不 commit)。
- op_census.sh 的 macOS awk BEGIN{} 插值 parse bug 已修(改 -v 傳參)。

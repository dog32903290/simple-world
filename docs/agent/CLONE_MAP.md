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

### leaf-ready 93 顆 — 縫已建,今天就能並行採(★大量產的甜點)
point-leaf 17 / mesh-leaf 17 / render-leaf 14 / camera-leaf 14 / vecmath-leaf 12 / flow-leaf 8 /
string-leaf 6 / field-leaf 2 / 其他 3。**這 93 顆是「大量並行工人」真正該吃的,不卡任何縫。**

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

1. **compute-dispatch**:census 標 6,但 SortPixelGlitch/SampleFieldPoints/ApplyVectorField/
   SimpleLiquid×2/AdvancedFeedback2 全匯此 → 實際 unlock 遠大於 6。蓋這根解鎖最多。
2. **persistent accumulator / texture-array**:TimeDisplace(N-slice ring)/ SlidingHistory /
   RemoveStaticBackground(單張 in-place EMA)。三顆,兩種型。
3. **pbr-lighting / 3D-render 島**:16 顆 + ScreenCloseUp 匯入。新島,最大單塊。

**這三根才是判斷密度高、該用最強腦當刀刃蓋的。其餘 seam-build 帳面數字被誤分類灌水。**

## 給大量產的操作接縫

- **今天就能大量並行(工人活,不卡縫)**:93 leaf-ready + 三縫解放的 ~10 顆(compound 6 + raymarch 1
  + feedback 2 + field generator 2)≈ **100+ 顆**。按島/家族分 lane 並行(每家族一 lane,registry 共享檔
  由 orchestrator 統一加=/sw-node-batch 既有形),每顆過新 golden 出廠閘(GOLDEN_STANDARD 五反型)。
- **judgment 活(刀刃,強腦,非量產)**:compute-dispatch → accumulator → pbr-3D 島,蓋一根解鎖一批。
- **柏為域/延後**:device-io 57(硬體)、pbr 16(大島)——clone-first 範圍內晚做。

## census 防呆(這次踩的坑,寫下免重栽)
- op_census 靠 `grep ': Instance<' external/tixl/Operators/Lib` 掃 TiXL 節點宇宙 → **external/tixl
  必須是全島 checkout**,sparse 會結構性漏島(這次只 field/image → 幻覺 166)。
- 開工前檢查:`git -C external/tixl sparse-checkout list` 應含全部 12 個 Operators/Lib/* 島夾;
  漏了先 `sparse-checkout set` 補齊(SHA 鎖 395c4c55,gitignored 不 commit)。
- op_census.sh 的 macOS awk BEGIN{} 插值 parse bug 已修(改 -v 傳參)。

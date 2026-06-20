# BLUEPRINT — cpu-point-list seam（補縫計劃階段 1 第三塊）

> Plan scout a2c2ea49 設計（2026-06-20），read-only。SEAM_COMPLETION_PLAN §2 階段 1。
> **build 前置：list-routing 先 commit，本塊 rebase 其上**（同動 point_graph.cpp 相鄰 lambda 群）。

## 0. 一句話結論
census「~7、純 CPU、橋一建即解」是樂觀估（混入 readback 顆）。**真 R1 = 4-6 顆純 CPU 葉子 + 一個 ListToBuffer 上傳橋**；下傳 readback（PointsToCPU）卡 compute-readback seam，**本塊明確排除**。鎖死範圍=「CPU point-list host-rail + ListToBuffer 上傳橋」，第一批 3-4 顆，不碰 readback。**★好消息：不踩 list-routing R-2 坑**——本塊橋走 point graph 渲染（DrawPoints production 路徑），不接 value op evalFloat，golden 用 pixel-readback 自然驗 production。

## 1. 核心發現
TiXL 兩條平行 point 家族，currency 不同：
- **GPU 家族**（point/generate/RadialPoints 等）：currency=`BufferWithViews`（GPU StructuredBuffer）。**sw 已港 ~44**（四流 buffer + compute）。`DrawPoints.cs:12` 只接這個。
- **CPU 家族**（point/_cpu/，6 檔）：currency=`StructuredList<Point>`（CPU Point[] host 記憶體）。**sw 完全沒這型別**。
橋點 `ListToBuffer.cs:21-80`：CPU list → GPU buffer 才能 draw。**這正是 sw 缺的橋**（SEAM_GRAPH.md:76 已標）。

## 2. seam 架構（第 7 條 cook flow，鏡像 FloatList host-rail + ListToBuffer 上傳分支）
- **currency**：新 dataType `"PointList"`，Impl 加 `std::map<std::string, std::vector<SwPoint>> pointListBuf`（鏡像 floatListBuf:103，host 記憶體無 GPU lifetime）。**直接用既有 SwPoint 64B**（不新建型別；fork: Point.Orientation→SwPoint.Rotation / Point.F1→FX1，同 GPU 四流既有對應）。
- **registry**：`pointlist_op_registry.{h,cpp}` 鏡像 floatlist_op_registry，float→SwPoint。
- **cook driver**（point_graph.cpp）：`cookPointListNode` 鏡像 cookFloatListNode（gather PointList input MultiInput → dispatch → 寫 pointListBuf）+ terminal preview + `debugCookedPointList` readback。
- **★ListToBuffer 上傳橋（承重，FloatList 沒有的部分）**：CPU list → GPU buffer（memcpy + StorageModeShared + replaceRegion/contents()，**不需 compute shader**=「比 point-buffer 容易」真義）。推薦做成獨立 ListToBuffer op（PointList in → Points/GPU buffer out），下游 DrawPoints 用既有 GPU 路徑零改動。
- **與 GPU point-buffer 關係**：上傳（CPU→GPU）=本塊純 memcpy 不卡 seam ✅；下傳（GPU→CPU，PointsToCPU）=compute-readback 未建,延後。_cpu 族不需雙向。

## 3. 解鎖分層（census ~7/~15 切清）
- **層 1 純 CPU 葉子（橋一建即解 R1，本塊）**：RadialPointsCpu / LinePointsCpu / LinearPointsCpu / TransformCpuPoint + RepeatAtPointsCpu(雙 list in) / SampleCpuPoints。**真實 ~5-6 顆**。
- **層 2 延後（疊別的未建 seam，非本塊）**：SampleSplinePoint(Layer2d+Execute SubTree) / PointsToCPU·ReadPointColors(compute-readback) / CpuPointToCamera·PointToMatrix(camera ICamera out) / DataPointConverter·LoadObjAsPoints·LoadSvg(file-IO/loader) / APoint(compound+JoinLists)。

## 4. 第一批葉子（防 orphan）
1. **RadialPointsCpu**（橋最小證 + 對照已港 GPU RadialPoints）：params→CPU list→ListToBuffer→DrawPoints→上螢幕。golden 與 GPU RadialPoints closed-form 點座標對拍 + debugCookedPointList readback 對手算。
2. **TransformCpuPoint**（list→list 中間節點 + 橋雙端）：list in→TRS→list out→上傳→draw。
3. **ListToBuffer 本體**（橋承重證）：手填 [p0,p1,p2]→GPU buffer，readback GPU contents() == 原 list（證 memcpy + **64B stride 對齊=packed_float3 雷區必驗**）。
golden 關鍵差異：不只 debugCookedPointList readback，**要上傳 GPU buffer readback contents() 對拍**（證橋非空轉、stride 不錯位）+ pixel-readback 視覺證（production 路徑）。

## 5. 改哪些檔
新（leaf 不撞）：pointlist_op_registry.{h,cpp} / point_ops_radialpointscpu.cpp / point_ops_transformcpupoint.cpp / point_ops_listtobuffer.cpp / golden。
改共享（最小 hook，與 list-routing 同核心檔→§6 合流）：point_graph.cpp(cookPointListNode lambda + ListToBuffer 分支 + terminal hook) / point_graph_internal.h(pointListBuf 一行) / point_graph.h(debugCookedPointList)。
**不碰**：graph.cpp evalFloat（PointList 不入 value rail→與 list-routing evalFloat 撞點=0，好消息）/ resident_eval_graph.cpp。

## 6. 合流順序（硬約束，三塊都動 point_graph.cpp）
1. string-rail ✅ b247602。2. list-routing 先 commit（同動 point_graph.cpp cookHostScalar + graph.cpp evalFloat）。3. **cpu-point-list rebase list-routing 之上才 build**（cookPointListNode 緊鄰 list-routing cookHostScalar/cookFloatListNode 同 lambda 群 line 620-832）。
降撞：cookPointListNode body + ListToBuffer fn 進獨立 leaf；point_graph.cpp 只加最小 dispatch hook（forward-decl + 一個 if(findPointListOp) 分支）；**不碰 graph.cpp**（撞點 0）。⚠ point_graph.cpp 已 937 行破債（DEBT_LEDGER §E/§F），本塊 body 全進 leaf 只加 ~30 行 hook，不惡化；補縫線跑完沿 cook-flow 拆檔還債。

## 7. fork + 零回歸 + RED
fork: pointlist-host-rail（第7 cook flow）/ cpupoint-reuses-swpoint（不新建型別）/ listtobuffer-host-memcpy（Metal shared buffer replaceRegion 替 TiXL DX11 SetupStructuredBuffer）。
零回歸：PointList 全新 dataType→現有 op 無 PointList port→cookPointListNode/上傳分支永不進→既有 44 GPU point op + 所有 flow 位元同構。
RED: pointListInjectBug 毀真輸出（丟末 SwPoint/Position NaN）→ readback 對拍 RED + 上螢幕 pixel RED；ListToBuffer 故意錯 stride(48≠64)→GPU readback 錯位 RED(=packed_float3 對齊牙)；boundary 空 list/單元素/RepeatAt 兩 list 一空。

## 8. 風險 / 盲區
- R-1（已拍板）：census ~7 混入 readback 顆（PointsToCPU 卡 compute-readback）→本塊鎖上傳半邊，不被拖難度。上傳半邊獨立可解+上螢幕+解 4-6 顆,值得做。
- R-2：sw 渲染終端只認 GPU buffer（DrawPoints.cs:12）→ListToBuffer 橋是上螢幕硬前置,不 optional（不建橋則 4 顆 _cpu 全 orphan）。
- R-3：SwPoint 64B stride 對齊（metal-cpp 雷）→上傳 memcpy bytewise 安全,但 ListToBuffer RED 牙必驗 stride。
- R-4：開工前逐顆 .cs 核 input cardinality（RepeatAtPointsCpu 兩 InputSlot 非 MultiInput；TransformCpuPoint 才 MultiInputSlot）。
- R-5：CPU list op count 是 op 內部自算（self-sizes,不走 ensureOut count 路徑,鏡像 FloatList 無 pre-sizing）。
- R-6：本塊只建 flat cook flow；resident 後補（但不碰 evalFloat 故漂移面比 list-routing 小）。★橋走 point graph 渲染(pg.cook,production)非 value evalFloat→不踩 list-routing R-2「flat-only production 接不出」坑,pixel-readback golden 自然驗 production。

## Critical Files
- point_graph.cpp（cookPointListNode + ListToBuffer 分支 + terminal hook；⚠ list-routing 同檔 rebase 其上）
- point_graph_internal.h（pointListBuf store 鏡像 floatListBuf:103）
- floatlist_op_registry.h（pointlist_op_registry 逐行藍本 float→SwPoint）
- tixl_point.h（SwPoint 64B currency + packed_float3 對齊=ListToBuffer stride 牙）
- external/tixl .../render/_dx11/buffer/ListToBuffer.cs + point/_cpu/RadialPointsCpu.cs（上傳橋 + 第一批葉子 ground-truth）

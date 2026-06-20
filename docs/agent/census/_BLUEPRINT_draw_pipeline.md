# BLUEPRINT — draw-pipeline seam（補縫計劃階段 3 第一塊，~13）

> Plan scout a22f5156 設計（2026-06-21），read-only。SEAM_COMPLETION_PLAN §2 階段 3。
> **build 前置：field 5 已 commit（6c4d7a1）；draw-pipeline rebase 主樹之上**（動 point_graph.* 核心須序列）。

## 0. 一句話結論 + ★假設翻轉（研究者層發現）
階段 3 brief 假設 **vec-color-field-output** 是「field 島延續、context 熱 → 最低風險入階段 3」。scout 查 production 路徑後**翻轉**：vec-color 是五候選裡**唯一 R-2 production 路徑不存在的**（無 Field cook 流，`renderField2d` 全是 golden 呼叫者，render template 丟棄 `f.xyz` 只寫 `f.w` 進 R32Float）→ 在 field 島採 vec-color 葉子=大規模重演 golden-綠/production-黑 自欺（list-routing/mesh refuter 抓過的 R-2 原型）。**推薦改採 draw-pipeline**：`cookCommand` resident 流已活 + DrawMeshUnlit 黑洞已修（d81d705）→ 新 cmd 葉子插入即上 resident 螢幕，零 Cut55 trap。

## 1. ★R-2 鐵律逐塊查（裁決軸）
production render 走 `pg.cookResident`（frame_cook.cpp:374）；resident terminal 按 output dataType 分流（point_graph_resident.cpp:532-563）：只有 **Points/Mesh/Texture2D/Command** 四流上螢幕。

| seam | 島 | resident production 流 | R-2 現狀 |
|------|----|----------------------|---------|
| **draw-pipeline** | point→Command | `cookCommand` 已活（resident.cpp:343,549）；DrawLines 已 registerCmdOp（drawlines.cpp:41）走這條 | ★**已存在**——新 cmd 葉子插入即上螢幕 |
| multi-image | Texture2D | `cookTexNode` 已活（resident.cpp:426,545） | ★已存在 |
| feedback | Texture2D | 同上 | 路徑在,但跨幀 prev-frame state=R3 |
| gradient | Texture2D | 同上 | 路徑在,但需先建 `Gradient` host 型別（census 真成本） |
| **vec-color-field-output** | **Field** | **無 Field 流**（grep `"Field"` resident=空） | ✗ **路徑不存在**,須先建整條 Field→render bridge（G3,census 標 needs camera3d/Layer2d） |

## 2. unlock÷風險÷視覺 + 選 draw 而非 multi-image
| seam | unlock | 風險 | R-2 | 綜合 |
|------|-------:|------|-----|------|
| **draw-pipeline** | ~13 | R2-R3（Tubes/Ribbons 重,但 ConnectionLines/DrawLineStrip R1-R2 入口）| ★已活 | **推薦** |
| multi-image | ~16 | R2 + Cut55 trap（每顆 .t3 backward-trace）| ★已活 | 次選 |
| feedback | ~16 | **R3**（ping-pong 跨幀 state）| 路徑在 state 重 | 後 |
| gradient | ~13 | R2 + 建 Gradient host 型別 | 路徑在前置 cpu-upload | 後 |
| vec-color | ~7 | 帳面 R2,**真 R3**（隱含 G3 bridge）| ✗ | **延後** |

選 draw 而非 multi-image（兩者 R-2 都活）：①draw 零 trap（multi-image 每顆 Cut55 routing），且 DrawLines 整檔 precedent ②第一批挑 R1-R2 入口（風險梯度最緩,適合視覺核心第一塊打通信心）③point 家族 context 熱（剛採 cpu-point-list ee4a99f + sim 44d4f96）。
> ⚠ 誠實標：~13 是 seam 盤點估非 census 硬數。開工前須 backward-trace 哪些純 line draw（R1-R2）vs geometry-shader 等價（Tubes/Ribbons R3）。保守第一批乾淨葉子=3-5 顆。

## 3. seam 架構
draw 葉子 = `registerCmdOp` 自登記（**conflict-free**,每顆 .cpp）。precedent 整條已存在：
- `CmdCookCtx`（point_graph.h）吃 `cc.points/count/inputCommand/params`→回 `RenderCommand`。
- 自登記：`registerCmdOp("DrawLines", cookDrawLines)`（drawlines.cpp:41）。
- production：resident.cpp:343 `cookCommand` 已 gather Points（:375-381）+Command（:391-398）,terminal :548-550 `execIntoTarget`。**新 cmd 葉子零接線即上 resident 螢幕。**

「擴充」不是建新流,是：(a) 補 cmd 葉子;(b) **若某顆需新 RenderCommand 欄位**（LineWidth-per-strip/Ribbon 寬度法線）才動 `RenderCommand`/`RenderDrawItem`（point_graph.h）+ MSL draw shader=**唯一可能動共享核心的點**（須謹慎,見 §6）。

## 4. 第一批葉子（2-3 顆,含 production-path golden）
挑 **ConnectionLines**（連相鄰點,R1-R2 純 line topology）第一顆：
1. **flat golden**（仿 drawlines.cpp 內嵌）：CPU 填已知點→ConnectionLines→RenderTarget→readback,閉式對線段覆蓋 texel。
2. **★production-path golden（R-2 鐵律,不可省）**：走 `pg.cookResident` 的 leg（Points 源→ConnectionLines→RenderTarget terminal→readback `p_->target` 證非 0 pixel）。precedent=cpu-point-list LEG4 `ringLit=904`（ee4a99f）+ mesh DrawMeshUnlit production 黑洞修（d81d705 resident.cpp:382-390）。**這兩塊都因 refuter 抓「flat 綠/resident 接不出」才補,draw 第一顆自帶 resident leg 不重蹈。**
3. **injectBug**：drop line topology / 錯 color param→兩 leg 都 RED。
> draw 好處：`cookCommand` resident 流已存在+DrawMeshUnlit 黑洞已修→第一顆大概率「插入即活」,但**仍須親手寫 resident golden 證實,不假設**（R-2 鐵律意義）。

## 5. fork（具名,回報必含）
- ConnectionLines line topology/wrap vs TiXL .cs（相鄰 vs closed-loop）逐行;
- LineWidth/Color param prefix 對 sw 凍結慣例（cookParam/cookVecN）;
- 若 RenderCommand 加欄位,命名 fork 註解+回報。

## 6. 零回歸 + 合流順序（與 point_graph.cpp 核心,DEBT_LEDGER §E/§F）
零回歸：additive 自登記 cmd op,不改既有 emit;動 point_graph.h（若加 RenderCommand 欄位）須預設值令既有 DrawLines/DrawScreenQuad byte-identical（新欄位 0-init）;DrawLines 既有 golden 續綠;--bite FAILED:[]+check-arch。
合流：①葉子各自 .cpp（conflict-free 可並行 fan-out）→refuter 逐顆對 TiXL→orchestrator **最後一次合流**統一加共享（point_graph.h 欄位/node_registry_draw.cpp/selftests.cpp/CMakeLists）+一次合 build。②動 point_graph.* 的 lane 與任何同動 point_graph_*/point_ops.h 的還債 lane **不可同跑同檔**（DEBT_LEDGER §E）。③**若第一批不需動 RenderCommand（純 Points→Command 用既有 draw item）→完全不碰 point_graph 核心=最理想第一批選法**（ConnectionLines 若能用既有 line draw item 即屬此,開工前確認）。
> ★scout 鐵律（vec2 全廢血證）：判每顆是否已港,必 grep **兩套註冊**（registerCmdOp 自登記 + node_registry_draw.cpp 舊式）。已見 DrawLines/PairPointsForGridWalkLines/PairPointsForSplines 在 draw 檔群→確認第一批挑的不是已港的。

## 7. 風險
- **R3 尾巴**：Ribbons/Tubes 需 per-segment 法線/寬度幾何（TiXL geometry-shader 等價,Metal 無 GS→compute 展開或 instanced quad）=R3,第一批不碰。
- **unlock 估算誤差**：~13 盤點估,開工前 backward-trace。
- **RenderCommand 欄位擴張**：多顆各要不同欄位恐讓 point_graph.h 破 400 行（已 421）→沿職責縫評估拆 draw-specific command struct。

## 8. draw 第二批 backlog（scout a52f692e 盤點 TiXL point/draw/ 23 op，2026-06-21）
**已港 9**：DrawPoints/DrawLines/DrawClosedLines/DrawBillboards/DrawScreenQuad/ClearRenderTarget/Camera/DrawMeshUnlit/RenderTarget。
**R1-R2 候選 4（draw 第二批，全撞 draw core serial）**：
1. **DrawPoints2**（R1，最 trivial）：points variant，param Color/Radius/Texture(opt)/Z-test/Blend/FadeNearest/UseWForSize → reuse draw_points.metal（param radius 取代 scale）。shared core YES（可能 RenderDrawItem 欄位）。
2. **DrawLinesBuildup**（R1）：lines + TransitionProgress/VisibleRange（漸進可見度 reveal），無 per-frame state → reuse draw_lines.metal（TransitionProgress VS sample）。shared core YES（param 加）。
3. **DrawMovingPoints**（R1-R2）：points + velocity-stretch（沿 velocity 向量拉長 quad），VelocityStretch/JumpThreshold=CPU filter → draw_points.metal velocity-quad orientation branch。shared core PARTIAL。
4. **DrawRayLines**（R1-R2）：points 當 ray origin，連 origin→(origin+W·dir) → draw_lines.metal ray-endpoint indexing（非 Points[i]→Points[i+1]）。shared core PARTIAL。
**R2 卡 ColorField host-eval seam（延後，需先建）**：DrawPointsShaded/DrawLinesShaded（ShaderGraphNode ColorField host 評估→shader-node-to-MSL 編譯）。
**R3 延後**：DrawConnectionLines（spatial-hash-map+Gradient host）/DrawPointsDOF（per-frame bucket sort DOF state）/DrawRibbons/DrawTubes（geometry-shader 等價,Metal 無 GS→compute 展開）。
**OOO（非 line/point draw）**：DrawMeshAtPoints2（mesh instancing+Gradient/Curve）/VisualizePoints（gizmo debug）。
**★draw 第二批工法**：全撞 draw core（render_command.h 加欄位/draw_*.metal branch/executor）=DEBT_LEDGER §E serial→**不可並行寫-leaf**（多 lane 撞 render_command.h 欄位 append）→一個 agent 序列做 2-3 顆（DrawPoints2+DrawLinesBuildup 最 trivial 先）或 orchestrator 逐顆派。每顆仍 R-2 鐵律（flat+resident golden）。

## Critical Files
- app/src/runtime/point_ops_drawlines.cpp（cmd 葉子+內嵌 golden 完整 precedent,第一批照抄）
- app/src/runtime/point_graph_resident.cpp（R-2 production：cookCommand :343、terminal :548、DrawMeshUnlit 黑洞修 :382=resident golden 範本）
- app/src/runtime/point_graph.h（CmdCookCtx/RenderCommand/RenderDrawItem/registerCmdOp=唯一可能動的共享核心）
- app/src/app/frame_cook.cpp（:374 pg.cookResident=production 入口,resident golden 對齊此呼叫）
- app/src/runtime/node_registry_draw.cpp（draw 家族舊式註冊,scout grep 兩套確認未已港）
- external/tixl Operators/Lib/point/draw/ConnectionLines.cs（第一葉子 authority）

## 附：vec-color-field-output 改做時的入口（延後,需先建 G3 bridge）
關鍵檔：runtime/field_render.cpp、shaders/templates/field_render_template.metal、runtime/field_graph.cpp、point_graph_resident.cpp 新 Field terminal；TiXL 權威 field/adjust/SetSDFMaterial.cs + use/SdfToVector.cs。G3 bridge=建 Render2dField/VisualizeFieldDistance 作 Command/texture terminal + RGBA target + template 寫 `f.xyz`，依賴 camera3d/Layer2d，宜單列大 seam。

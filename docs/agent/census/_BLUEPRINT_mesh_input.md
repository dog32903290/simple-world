# BLUEPRINT — mesh-input seam（補縫計劃階段 2，census ~29）

> Plan scout a4ac1a9f 設計（2026-06-20），read-only。SEAM_COMPLETION_PLAN §2 階段 2。
> **build 前置：cpu-point-list 先 commit，本塊 rebase 其上**（同動 point_graph.cpp/_resident.cpp/_internal.h 核心）。

## 0. 一句話結論
mesh-input 可建，但 census「~29 R1-R2」樂觀。真 R1 = **6-10 顆純 mesh→mesh（count 可前算）**；其餘 ~19 疊副 seam（texture/field/point/loader/dynamic-shader），不算一塊解鎖。**★核心是 R-2 production 坑（list-routing 同構）：production resident path 對 Mesh 全盲，第一批 golden 必走 cookResident，否則自欺。**

## 1. ★R-2 既有洞（最重要發現，硬證）
- production 入口=`frame_cook.cpp:374 pg.cookResident`。`point_graph_resident.cpp` cookCommand input loop **只處理 Points/Command**（無 `dataType=="Mesh"` 分支）；terminal dispatch 無 mesh 分支。grep mesh 於 _resident.cpp=零命中。
- **既有 DrawMeshUnlit（commit bbe9feb camera3d Cut99）在 production resident 是黑的**：接 NGonMesh 在 flat pg.cook 渲染紅 quad，但 production cookResident 無 Mesh 分支→meshVtx nullptr→cookDrawMeshUnlit:50 `if(!c.meshVtx) return rc`→空 chain→螢幕黑。**既有未發現洞,mesh-input seam 必順手補。**
- 同 string-rail flat-only：**lane 早期 seam 系統性只建 flat golden,沒證 production resident**（[[simple-world-compound-lane-state]] R-2 教訓）。
- → 第一批必含 production-path golden（cookResident pixel-readback）。census R1-R2 實際拉到 R2。

## 2. 現況 backward-trace
- mesh-pipeline 是**純 generator 形狀**：MeshCookCtx(mesh_op_registry.h:48-70) 只有 output_vertices/indices,無 input。count-first 契約:MeshCountFn(params,&vtx,&idx) 從自己 params 算 size→ensureMesh 預 size→cook 只 contents() 寫。**count 純 params,不看上游=generator-only**。
- 半條 input 橋只在 flat：point_graph.cpp:401-405+661-669 cookMeshInto 服務 DrawMeshUnlit 借 buffer（Command 消費 Mesh），非「mesh op 消費 mesh 產 mesh」。MeshCookCtx 仍無 input。
- TiXL 契約:TransformMesh.cs(in×1,count=入頂點)/CombineMeshes.cs(MultiInput,count=Σ,index rebase)/DisplaceMesh.cs(+Texture2D=疊 texture seam)。**消費型 count 取決上游,現 MeshCountFn(params) 簽名表達不了**=seam 核心改點。

## 3. seam 設計
- **mesh_op_registry.h（核心）**:① MeshCookCtx 加 `const SwMeshView* inputMeshes; int inputMeshCount`（SwMeshView={const MTL::Buffer* vtx; uint32_t vtxCount; const MTL::Buffer* idx; uint32_t faceCount} borrowed single-frame,同 Texture2D gather 生命週期）② MeshCountFn 簽名升級吃 input counts:`void(params, const SwMeshView* inputs, int n, uint32_t& vtx, uint32_t& idx)`（generator 忽略 inputs;TransformMesh 回 inputs[0].vtxCount;CombineMeshes 回 Σ）=**fork-mesh-1**。
- **point_graph.cpp（flat,~30 行）**:cookMeshNode 前插 input gather loop（clone cookFloatListNode port-walk）,每個 Mesh input port recurse cookMeshNode→debugCookedMesh 收 SwMeshView,MultiInput 展開→count(params,views,n)→ensureMesh→cook。cookMeshNode 改 std::function 支自遞迴。
- **★point_graph_resident.cpp（production,~40 行,R-2 必須）**:新增 resident cookMeshNode（目前不存在,mirror flat 但走 ResidentInput::Connection/srcNodePath,同 cookResidentPointList:240 形狀）+ cookCommand 加 `else if(dataType=="Mesh")` 分支填 cc.meshVtx/idx/faceCount（**同時修 §1 DrawMeshUnlit 既有洞**）+ terminal mesh dispatch。

## 4. 解鎖分層（~29 拆真假）
- **A 純 mesh→mesh gather 一建即解(真 R1) ~6-10**:TransformMesh(in×1 count=入,首選)/CombineMeshes(MultiInput count=Σ index rebase)/FlipNormals/RecomputeNormals/TransformMeshUVs/SplitMeshVertices·CollapseVertices(count 變需確認可前算)。
- **B 疊副 seam(非本批) ~19**:DisplaceMesh/TextureDisplaceMesh(texture-into-mesh)/ColorVerticesWithField·SelectVerticesWithSDF(field)/BlendMeshToPoints·ScatterMeshFaces·RepeatMeshAtPoints(point island)/Custom*Shader·DeformMesh(dynamic-shader)/LoadObj(loader)/boolean(count 無法前算 compute-readback)。

## 5. 第一批葉子（含 production golden）
1. **TransformMesh**(in×1 count=入):closed-form golden(QuadMesh 4 頂點→平移/縮放逐頂點對 TiXL 矩陣)+meshInjectBug 真 cook。CPU readback(flat)驗資料。
2. **CombineMeshes**(MultiInput):兩 QuadMesh→8 頂點 4 面,index 後半 +4(rebase)。
3. **★production-path golden(R-2 鐵閘)**:QuadMesh→TransformMesh→DrawMeshUnlit→RenderTarget **走 pg.cookResident** pixel-readback 證變換 mesh 真上螢幕。injectBug=resident cookCommand 漏 Mesh 分支→黑→RED。**證 production resident gather 活+釘死 §1 洞,同 list-routing resident_host_scalar_cook golden 同位物。**

## 6. fork + 零回歸 + 合流順序
fork: mesh-1(MeshCountFn 簽名擴 input)/mesh-2(boolean/count 無法前算不納本批)。
零回歸:NGon/Quad MeshCountFn 只多收忽略參數,cook 不變,flat mesh/DrawMeshUnlit golden byte-identical。--bite FAILED:[] + check-arch。
RED:TransformMesh meshInjectBug 腐化頂點 RED;production golden resident 漏 Mesh 分支→黑 RED(證 production gather 真在路徑)。
**合流順序(★同核心檔序列)**:1.等 cpu-point-list commit(現 staged point_graph.cpp/.h/_internal.h/_resident.cpp 都 M)。2.rebase 其上(cookMeshNode input gather 與 cpu-point-list cookResidentPointList 同檔鄰近區,手改撞行高機率)。3.mesh-input 與 cpu-point-list **不可同跑同檔**(DEBT_LEDGER §E/§F);read-only 設計可並行,build 序列。4.葉子 leaf conflict-free,seam 合流後 sw-node-batch 並行採。

## 7. 風險
- ★R-2 production(高,已證真洞):mesh render 走 cookResident,resident 對 mesh 全盲。只 flat=必自欺(重演 Cut47)。緩解:第一批必 resident-path golden(§5.3)。
- count-first vs input-dependent count(中):Transform/Combine count 可前算 OK;boolean/collapse cook 中才知 size 不適配 count-first→延後或兩階段 cook。本批只收 count 可前算者。
- census ~29 多疊副 seam(中):真解 6-10,其餘各等對應島。建議 SEAM_COMPLETION_PLAN §2 階段 2 標「mesh-input ~29(真 R1 6-10,其餘疊副 seam)」。
- camera 依賴(低):純 mesh→mesh 資料 golden 不需;production pixel golden 用 default camera(camera3d 已建 ec8216c)足。
- index rebase(中,CombineMeshes):合併第二 mesh index +前者頂點數,golden 逐 index 對。
- dynamic-shader 盲區:Custom*Shader/DeformMesh runtime HLSL,static metallib 覆蓋未證→延後。

## Critical Files
- mesh_op_registry.h（MeshCookCtx 加 input + MeshCountFn 升級 + SwMeshView）
- point_graph.cpp（flat cookMeshNode input gather,line 637-669 區）
- ★point_graph_resident.cpp（production resident mesh gather + cookCommand Mesh 分支,line 280-332/457-479,R-2 承重+修 DrawMeshUnlit 既有洞）
- external/tixl .../mesh/modify/TransformMesh.cs（第一葉子 authority）
- point_ops_drawmeshunlit.cpp（既有 Mesh-input 借用 + production golden 藍本）

# ENGINE_GAP_GLUE — .t3 子節點膠水積木需求表

掃描日期：2026-07-10　掃描範圍：145 顆壓平複合（node_health.sh --class flattened）+ 190 顆未做複合（--class undone，過濾 TiXL kind=compound）= 335 顆目標複合，逐顆讀 external/tixl/Operators/Lib 對應 .t3 的 Children 區段。找到 .t3：335／335；找不到：0；解析失敗：0。

方法（可重跑）：`bash tools/node_health.sh --class flattened` / `--class undone` 拿兩份清單（op/island/relpath 三欄）→ undone 用 relpath 讀 .t3 檢查 `"Children": []` 篩出複合 →對每顆複合的 .t3，取 `"Children"` 到 `"Connections"` 之間的區段，用錨定 regex `"Id":\s*"[^"]+"\s*/\*([A-Za-z0-9_]+)\*/,\s*\n\s*"SymbolId"` 抽子節點型別名（只認子節點自己的 Id 註解，排除 InputValues/Outputs 內的槽位名註解——已用 BlendPoints.t3 驗證 12/12 精確零雜訊）。SUPPORTED 對照 `app/src/runtime/t3_import_maps.cpp` TABLE ③ `swTypeForSymbolGuid`（20 entry 白名單，用 TiXL 側 .t3 實際顯示名比對——已用全 Lib guid 掃描驗證，含一處改名陷阱 Const↔IntValue）；PARTIAL/MISSING 對照 sw 全域已支援算子名集合（node_registry_*.cpp 錨定行 + leaf-stem 檔名 + register 呼叫，同 node_health.sh 的 done-set 邏輯獨立重跑）。**`.t3 形狀`欄是名稱比對（非 guid）的盡力猜測**：只在 Lib 底下找同名 .t3 判斷 Lib-atom/Lib-compound/TypeOperators-atom，不驗證是否為 `swTypeForSymbolGuid` 表裡那個確切 guid 的symbol——例如 `FloatsToBuffer`（.cs class）恰好在 Lib 另有一顆同名 .t3 wrapper，會被標成「Lib-atom」，但 SUPPORTED/PARTIAL/MISSING 判定本身用的是精確 guid 白名單，不受此影響；`.t3 形狀`欄只作為「這個名字要新寫的話大概是哪一種形狀的活」的參考，不要當成精確分類。

## 頂部摘要

1. **總計 564 種相異子節點型別**，分布：SUPPORTED 19（已在 .t3 importer 的 glue 白名單）、HANDLED(fold) 1（ComputeShader——特殊 fold pass 吃掉，非缺口）、PARTIAL 210（sw 別處已有同名算子，但沒接進 .t3 child-import 路徑）、**MISSING 334**（sw 完全沒有——含三種形狀，見下方 t3_kind 拆解）。
2. **MISSING 334 顆按 TiXL 側形狀拆解**：TypeOperators-atom（無 .t3，純 C# code-op，真正要新寫 C++ 引擎積木） 219 種；Lib-atom（Lib 有 .t3 但 sw 從未 port，屬另一條「未做原子」backlog，非本表核心） 61 種；Lib-compound(nested)（本身是另一顆複合子圖，理論上該由巢狀遞迴吃掉，但正式 import 的 resolver 只索引「catalog 資料夾」不是全 925 顆 Lib——見下方意外②） 54 種。
3. **MISSING 裡按擋量最痛的前 5**：`SrvFromTexture2d`（擋 102 顆，Lib-atom）；`SamplerState`（擋 79 顆，TypeOperators-atom(no .t3)）；`OutputMergerStage`（擋 60 顆，Lib-atom）；`RasterizerState`（擋 60 顆，TypeOperators-atom(no .t3)）；`VertexShader`（擋 60 顆，TypeOperators-atom(no .t3)）。

## 意外發現（掃描中浮現，非任務原本假設）

- **① `ComputeShader` 不是缺口，是被 fold 吃掉**：它在 138 顆複合的 Children 裡出現，天真統計會排MISSING 榜首，但 `t3_import.cpp:202-210` 有專門的 fold pass 把它的 `Source` 字串疊到父`ComputeShaderStage`/`_ExecuteCombineBuffers` 的 `KernelName` strOverride 上——這個 child 從來不會变成一個「未映射」節點。已在表中標 `HANDLED(fold)` 排除，避免下一棒誤判要新寫一顆 ComputeShader 節點。
- **② 巢狀複合子節點（Lib-compound）的正式 import resolver 目前只索引「catalog 資料夾」，不是全925 顆 Lib**：`t3_import_recurse.cpp` 的遞迴機制本身是通用、能吃巢狀複合子節點的（`t3ResolveNestedCompound`），但唯一接它的生產呼叫點 `catalog_boot.cpp:44-46` 明講「Scoped to THIS folder（no 925-wide Lib scan——that is an independent production-scale decision）」。也就是說本表列的 54 種 `Lib-compound(nested)` 型別（如 `_ImageFxShaderSetupStatic` 擋 32 顆、`PickBlendMode` 擋 36 顆）現在一律 unmapped skip——除非先把 resolver 的索引範圍從 catalog 資料夾擴到全 Lib（或至少擴到這 335 顆複合遞迴觸及的子圖），否則這批不會因為遞迴機制存在就自動解決。
- **③ 有一批（~60 顆複合份量）根本不是 compute-shader 形狀，是 DX11 光柵管線形狀**：`VertexShader`/`PixelShader`/`RasterizerState`/`SamplerState`/`DepthStencilState`/`OutputMergerStage`/`InputAssemblerStage`/`SetPixelAndVertexShaderStage`/`Draw`/`Rasterizer`/`SrvFromTexture2d`/`GetTextureSize` 這一串（多數 MISSING、少數已 Lib-atom）都是 fixed-function mesh/render 管線的狀態物件，跟 `ComputeShaderStage` 走的 compute-dispatch 完全是另一條引擎骨頭——`buffer_ops_computeshaderstage.cpp`目前只綁 `enc->setBuffer`，沒有 `enc->setTexture`／沒有光柵管線狀態機。這批複合不會因為compute-glue 補完而解鎖，需要獨立立項（見 ENGINE_GAP_BUFFER_SHAPES.md 的 texture-bound 形狀）。
- **④（方法論註記）名稱比對非 guid 比對，有小量漏檢**：全 Lib guid 掃描證實 20 個白名單 guid 裡，19 個的 TiXL 實例名跟 sw 內部名一致，唯一例外 `Const`（sw 名）↔ `IntValue`（TiXL .t3 實際顯示名，guid `cc07b314`，33 次都顯示 IntValue、0 次顯示 Const）——已在本表修正用 `IntValue` 比對。但同一 guid 底下仍有少量使用者手動改名的實例（如 GetBufferComponents 底下 4 次被叫成 `Vertices`、`GetIndices` 等），這批仍會被本表誤判成獨立型別名——實際上是 SUPPORTED 積木的改名實例，數量小（個位數/型別），不影響 top 排名，但逐顆施工前建議照 guid 再核一次。
- **⑤（最重要）52 種子節點型別名字本身就是另一顆「145 壓平複合」裡的 op**：不少複合的 Children 直接嵌了另一顆 TiXL 複合當子節點（如 `SimForceOffset` 嵌 `AddNoise`、`FadingSlideShow` 嵌 `Blend`），而這些嵌入的 op 名稱剛好命中 node_health.sh 認定「sw 已壓平」的 145 顆之一。這批（52 種型別）本表用獨立重跑的 swDoneSet 抓到 49 種標成 PARTIAL（表示 sw 確實有），但漏抓 3 種（`CombineBuffers`/`DrawMesh`/`KochKaleidoskope`）標成 MISSING——逐一核過：`CombineBuffers` 是 2026-07-08 已退場的舊壓平 atom，`compound_save.cpp:30` 留了 uuid fallback 但沒有新 NodeSpec 可路由（見上表已標註）；`KochKaleidoskope`（TiXL 拼法）↔ sw 檔名 `point_ops_kochkaleidoscope.cpp`（英式拼法 Kaleidoscope）——單純拼字分岔造成的假 MISSING；`TransformFromClipSpace`（TiXL 名）↔ sw 檔名 `point_ops_transformpointsfromclipspace.cpp`（sw 加了 `Points`）——同樣是改名分岔。**結論給下一棒**：這 52 種型別（列表可用 `main table ∩ 145 flattened op names` 重跑取得）大多數不需要「新寫」——真正該做的引擎工作是幫 `t3_import.cpp` 的子節點解析加一個「子節點 SymbolId 命中另一顆 sw 已壓平複合的 guid → 直接接到 sw 那顆手刻 atom」的路由（跟現有 `swTypeForSymbolGuid` 同機制，只是資料源從 20-entry 白名單換成 145 顆壓平清單），比逐顆手動補 glue row 或等 catalog resolver 擴大索引範圍都便宜。命中改名/拼字分岔的極少數（本次抓到 2 個）才需要人工核對。

## 主表 — 出現在 ≥2 顆複合的型別（按擋量降冪；singleton 見文末附錄）

276 種型別出現在 ≥2 顆複合裡（覆蓋所有真正「可重用」的積木；恰好只出現 1 次的 288 種長尾放文末附錄，多半是單一複合的客製小節點，優先度低）。

| 積木名 | 出現複合數 | 總出現次數 | sw 現況 | .t3 形狀 | 證據 |
|---|---:|---:|---|---|---|
| `FloatsToBuffer` | 194 | 207 | SUPPORTED | Lib-atom | t3_import_maps.cpp:51 (swTypeForSymbolGuid) |
| `GetBufferComponents` | 151 | 324 | SUPPORTED | Lib-atom | t3_import_maps.cpp:53 (swTypeForSymbolGuid) |
| `ComputeShaderStage` | 149 | 178 | SUPPORTED | TypeOperators-atom(no .t3) | t3_import_maps.cpp:59 (swTypeForSymbolGuid) |
| `Execute` | 149 | 204 | PARTIAL | Lib-atom | node_registry_draw_flow.cpp:103 (NodeSpec anchor) |
| `CalcDispatchCount` | 138 | 160 | SUPPORTED | Lib-atom | t3_import_maps.cpp:61 (swTypeForSymbolGuid) |
| `ComputeShader` | 138 | 171 | HANDLED(fold) | TypeOperators-atom(no .t3) | t3_import.cpp:202-210 — ComputeShader.Source folds onto the ComputeShaderStage/_ExecuteCombineBuffers child's KernelName strOverride (computeshader-source-folded-onto-stage fork); this child is CONSUMED by the fold, never becomes an unmapped sw child. Not a gap. |
| `IntToFloat` | 117 | 208 | SUPPORTED | Lib-atom | t3_import_maps.cpp:74 (swTypeForSymbolGuid) |
| `ExecuteBufferUpdate` | 115 | 117 | SUPPORTED | Lib-atom | t3_import_maps.cpp:55 (swTypeForSymbolGuid) |
| `Vector2Components` | 115 | 211 | SUPPORTED | Lib-atom | t3_import_maps.cpp:85 (swTypeForSymbolGuid) |
| `SrvFromTexture2d` | 102 | 163 | MISSING | Lib-atom | no sw atom found by name (node_registry + leaf-stem scan) and not in glue whitelist |
| `StructuredBufferWithViews` | 97 | 103 | SUPPORTED | TypeOperators-atom(no .t3) | t3_import_maps.cpp:60 (swTypeForSymbolGuid) |
| `Vector4Components` | 93 | 166 | SUPPORTED | Lib-atom | t3_import_maps.cpp:82 (swTypeForSymbolGuid) |
| `SamplerState` | 79 | 86 | MISSING | TypeOperators-atom(no .t3) | no sw atom found by name (node_registry + leaf-stem scan) and not in glue whitelist |
| `BoolToFloat` | 73 | 110 | SUPPORTED | Lib-atom | t3_import_maps.cpp:69 (swTypeForSymbolGuid) |
| `IntsToBuffer` | 73 | 77 | SUPPORTED | Lib-atom | t3_import_maps.cpp:52 (swTypeForSymbolGuid) |
| `GetSRVProperties` | 67 | 72 | SUPPORTED | Lib-atom | t3_import_maps.cpp:54 (swTypeForSymbolGuid) |
| `TransformsConstBuffer` | 66 | 66 | SUPPORTED | Lib-atom | t3_import_maps.cpp:57 (swTypeForSymbolGuid) |
| `Vector3Components` | 64 | 107 | SUPPORTED | Lib-atom | t3_import_maps.cpp:75 (swTypeForSymbolGuid) |
| `ClampInt` | 63 | 79 | PARTIAL | Lib-atom | value_op_clampint.cpp (leaf stem) |
| `Rasterizer` | 61 | 62 | PARTIAL | Lib-atom | node_registry_draw_renderstate.cpp:26 (NodeSpec anchor) |
| `InputAssemblerStage` | 60 | 61 | PARTIAL | Lib-atom | node_registry_draw_renderstate.cpp:74 (NodeSpec anchor) |
| `OutputMergerStage` | 60 | 61 | MISSING | Lib-atom | no sw atom found by name (node_registry + leaf-stem scan) and not in glue whitelist |
| `RasterizerState` | 60 | 60 | MISSING | TypeOperators-atom(no .t3) | no sw atom found by name (node_registry + leaf-stem scan) and not in glue whitelist |
| `VertexShader` | 60 | 61 | MISSING | TypeOperators-atom(no .t3) | no sw atom found by name (node_registry + leaf-stem scan) and not in glue whitelist |
| `Draw` | 59 | 61 | PARTIAL | Lib-atom | node_registry_draw_renderstate.cpp:87 (NodeSpec anchor) |
| `GetTextureSize` | 59 | 63 | MISSING | Lib-atom | no sw atom found by name (node_registry + leaf-stem scan) and not in glue whitelist |
| `PixelShader` | 52 | 54 | MISSING | TypeOperators-atom(no .t3) | no sw atom found by name (node_registry + leaf-stem scan) and not in glue whitelist |
| `BoolToInt` | 48 | 71 | PARTIAL | Lib-atom | value_op_booltoint.cpp (leaf stem) |
| `MultiplyInt` | 47 | 59 | PARTIAL | Lib-atom | value_op_multiplyint.cpp (leaf stem) |
| `RenderTarget` | 47 | 58 | PARTIAL | Lib-atom | node_registry_draw_render.cpp:182 (NodeSpec anchor) |
| `Multiply` | 44 | 74 | PARTIAL | Lib-atom | node_registry_math_arithmetic.cpp:27 (NodeSpec anchor) |
| `_MeshBufferComponents` | 42 | 43 | SUPPORTED | Lib-atom | t3_import_maps.cpp:66 (swTypeForSymbolGuid) |
| `DepthStencilState` | 37 | 37 | MISSING | TypeOperators-atom(no .t3) | no sw atom found by name (node_registry + leaf-stem scan) and not in glue whitelist |
| `LoadImage` | 37 | 41 | PARTIAL | Lib-atom | point_ops_loadimage.cpp (leaf stem) |
| `PickBlendMode` | 36 | 36 | MISSING | Lib-compound(nested) | no sw atom found by name (node_registry + leaf-stem scan) and not in glue whitelist |
| `VisibleGizmos` | 35 | 35 | PARTIAL | Lib-atom | node_registry_draw_gizmo.cpp:24 (NodeSpec anchor) |
| `SetPixelAndVertexShaderStage` | 33 | 33 | MISSING | Lib-atom | no sw atom found by name (node_registry + leaf-stem scan) and not in glue whitelist |
| `_ImageFxShaderSetupStatic` | 32 | 32 | MISSING | Lib-compound(nested) | no sw atom found by name (node_registry + leaf-stem scan) and not in glue whitelist |
| `HasTimeChanged` | 31 | 36 | PARTIAL | Lib-atom | node_registry_math_time.cpp:45 (NodeSpec anchor) |
| `Transform` | 30 | 42 | PARTIAL | Lib-atom | node_registry_draw_transform.cpp:49 (NodeSpec anchor) |
| `PixelShaderStage` | 28 | 28 | MISSING | TypeOperators-atom(no .t3) | no sw atom found by name (node_registry + leaf-stem scan) and not in glue whitelist |
| `VertexShaderStage` | 28 | 28 | MISSING | TypeOperators-atom(no .t3) | no sw atom found by name (node_registry + leaf-stem scan) and not in glue whitelist |
| `GradientsToTexture` | 27 | 27 | PARTIAL | Lib-atom | point_ops_gradientstotexture.cpp (leaf stem) |
| `GenerateShaderGraphCode` | 26 | 26 | MISSING | Lib-atom | no sw atom found by name (node_registry + leaf-stem scan) and not in glue whitelist |
| `UseFallbackTexture` | 26 | 27 | PARTIAL | Lib-atom | point_ops_usefallbacktexture.cpp (leaf stem) |
| `_AssembleMeshBuffers` | 26 | 26 | SUPPORTED | Lib-atom | t3_import_maps.cpp:67 (swTypeForSymbolGuid) |
| `Div` | 25 | 31 | PARTIAL | Lib-atom | node_registry_math_arithmetic.cpp:77 (NodeSpec anchor) |
| `DrawLines` | 25 | 60 | PARTIAL | Lib-compound(nested) | node_registry_draw_render.cpp:44 (NodeSpec anchor) |
| `IntValue` | 24 | 32 | SUPPORTED | TypeOperators-atom(no .t3) | t3_import_maps.cpp:70 (swTypeForSymbolGuid) |
| `AddInts` | 23 | 29 | PARTIAL | Lib-atom | value_op_addints.cpp (leaf stem) |
| `ResolutionConstBuffer` | 23 | 23 | MISSING | Lib-atom | no sw atom found by name (node_registry + leaf-stem scan) and not in glue whitelist |
| `BlendState` | 22 | 31 | MISSING | TypeOperators-atom(no .t3) | no sw atom found by name (node_registry + leaf-stem scan) and not in glue whitelist |
| `CalcInt2DispatchCount` | 21 | 28 | MISSING | Lib-atom | no sw atom found by name (node_registry + leaf-stem scan) and not in glue whitelist |
| `RenderTargetBlendDescription` | 21 | 22 | MISSING | TypeOperators-atom(no .t3) | no sw atom found by name (node_registry + leaf-stem scan) and not in glue whitelist |
| `Value` | 21 | 25 | MISSING | TypeOperators-atom(no .t3) | no sw atom found by name (node_registry + leaf-stem scan) and not in glue whitelist |
| `Add` | 20 | 22 | PARTIAL | Lib-atom | node_registry_math_arithmetic.cpp:57 (NodeSpec anchor) |
| `ContextCBuffers` | 20 | 20 | MISSING | Lib-atom | no sw atom found by name (node_registry + leaf-stem scan) and not in glue whitelist |
| `Group` | 20 | 24 | PARTIAL | Lib-atom | point_ops_group.cpp (leaf stem) |
| `ComputeShaderFromSource` | 19 | 19 | MISSING | TypeOperators-atom(no .t3) | no sw atom found by name (node_registry + leaf-stem scan) and not in glue whitelist |
| `BlendColors` | 18 | 18 | PARTIAL | Lib-atom | value_op_blendcolors.cpp (leaf stem) |
| `CommonPointSets` | 18 | 39 | PARTIAL | Lib-atom | point_ops_commonpointsets.cpp (leaf stem) |
| `Layer2d` | 18 | 33 | PARTIAL | Lib-compound(nested) | point_ops_layer2d.cpp (leaf stem) |
| `TransformMatrix` | 18 | 19 | SUPPORTED | Lib-atom | t3_import_maps.cpp:62 (swTypeForSymbolGuid) |
| `Vector3` | 18 | 26 | MISSING | TypeOperators-atom(no .t3) | no sw atom found by name (node_registry + leaf-stem scan) and not in glue whitelist |
| `DrawBoxGizmo` | 17 | 18 | PARTIAL | Lib-compound(nested) | pointlist_ops_drawboxgizmo.cpp (leaf stem) |
| `ExecuteTextureUpdate` | 17 | 18 | MISSING | Lib-atom | no sw atom found by name (node_registry + leaf-stem scan) and not in glue whitelist |
| `GetParticleComponents` | 17 | 17 | MISSING | Lib-atom | no sw atom found by name (node_registry + leaf-stem scan) and not in glue whitelist |
| `Int2Components` | 17 | 20 | PARTIAL | Lib-atom | value_op_int2components.cpp (leaf stem) |
| `UavFromTexture2d` | 17 | 27 | MISSING | Lib-atom | no sw atom found by name (node_registry + leaf-stem scan) and not in glue whitelist |
| `_ExecuteParticleUpdate` | 17 | 17 | MISSING | Lib-atom | no sw atom found by name (node_registry + leaf-stem scan) and not in glue whitelist |
| `_Time_old` | 17 | 18 | MISSING | Lib-atom | no sw atom found by name (node_registry + leaf-stem scan) and not in glue whitelist |
| `Remap` | 16 | 20 | PARTIAL | Lib-atom | node_registry_math_arithmetic.cpp:103 (NodeSpec anchor) |
| `FloatToInt` | 15 | 21 | PARTIAL | Lib-atom | value_op_floattoint.cpp (leaf stem) |
| `Texture2d` | 15 | 18 | MISSING | TypeOperators-atom(no .t3) | no sw atom found by name (node_registry + leaf-stem scan) and not in glue whitelist |
| `GetForegroundColor` | 14 | 14 | PARTIAL | Lib-atom | node_registry_math_anim.cpp:177 (NodeSpec anchor) |
| `ScaleVector3` | 14 | 17 | PARTIAL | Lib-atom | value_op_scalevector3.cpp (leaf stem) |
| `Switch` | 14 | 16 | PARTIAL | Lib-atom | point_ops_switch.cpp (leaf stem) |
| `TimeConstBuffer` | 13 | 13 | MISSING | Lib-atom | no sw atom found by name (node_registry + leaf-stem scan) and not in glue whitelist |
| `TransformPoints` | 13 | 30 | PARTIAL | Lib-compound(nested) | point_ops_transformpoints.cpp (leaf stem) |
| `_multiImageFxSetupStatic` | 13 | 14 | MISSING | Lib-compound(nested) | no sw atom found by name (node_registry + leaf-stem scan) and not in glue whitelist |
| `AddVec3` | 12 | 12 | PARTIAL | Lib-atom | value_op_addvec3.cpp (leaf stem) |
| `And` | 12 | 12 | PARTIAL | Lib-atom | value_op_and.cpp (leaf stem) |
| `Any` | 12 | 13 | PARTIAL | Lib-atom | value_op_any.cpp (leaf stem) |
| `Int2` | 12 | 15 | MISSING | TypeOperators-atom(no .t3) | no sw atom found by name (node_registry + leaf-stem scan) and not in glue whitelist |
| `Loop` | 12 | 12 | PARTIAL | Lib-atom | node_registry_draw_flow.cpp:116 (NodeSpec anchor) |
| `RepeatAtPoints` | 12 | 19 | PARTIAL | Lib-compound(nested) | point_ops_repeatatpoints.cpp (leaf stem) |
| `ClampedSampler` | 11 | 11 | MISSING | TypeOperators-atom(no .t3) | no sw atom found by name (node_registry + leaf-stem scan) and not in glue whitelist |
| `GetPbrParameters` | 11 | 11 | MISSING | Lib-atom | no sw atom found by name (node_registry + leaf-stem scan) and not in glue whitelist |
| `Text` | 11 | 11 | MISSING | Lib-compound(nested) | no sw atom found by name (node_registry + leaf-stem scan) and not in glue whitelist |
| `Camera` | 10 | 10 | PARTIAL | Lib-atom | node_registry_draw_camera.cpp:19 (NodeSpec anchor) |
| `CompareInt` | 10 | 12 | PARTIAL | Lib-atom | value_op_compareint.cpp (leaf stem) |
| `CountInt` | 10 | 10 | PARTIAL | Lib-atom | node_registry_math_logic.cpp:179 (NodeSpec anchor) |
| `DrawQuad` | 10 | 11 | MISSING | Lib-compound(nested) | no sw atom found by name (node_registry + leaf-stem scan) and not in glue whitelist |
| `FirstValidTexture` | 10 | 10 | PARTIAL | Lib-atom | point_ops_firstvalidtexture.cpp (leaf stem) |
| `PickTexture` | 10 | 18 | PARTIAL | Lib-atom | point_ops_picktexture.cpp (leaf stem) |
| `PixelShaderFromSource` | 10 | 10 | MISSING | TypeOperators-atom(no .t3) | no sw atom found by name (node_registry + leaf-stem scan) and not in glue whitelist |
| `GetIntVar` | 9 | 12 | PARTIAL | Lib-atom | node_registry_math_contextvar.cpp:70 (NodeSpec anchor) |
| `HasIntChanged` | 9 | 11 | PARTIAL | Lib-atom | node_registry_math_logic.cpp:209 (NodeSpec anchor) |
| `SrvFromStructuredBuffer` | 9 | 9 | MISSING | Lib-atom | no sw atom found by name (node_registry + leaf-stem scan) and not in glue whitelist |
| `_GridPoints_Old` | 9 | 9 | MISSING | Lib-compound(nested) | no sw atom found by name (node_registry + leaf-stem scan) and not in glue whitelist |
| `_ImageFxShaderSetup2` | 9 | 9 | MISSING | Lib-compound(nested) | no sw atom found by name (node_registry + leaf-stem scan) and not in glue whitelist |
| `__padding` | 9 | 11 | MISSING | TypeOperators-atom(no .t3) | no sw atom found by name (node_registry + leaf-stem scan) and not in glue whitelist |
| `_multiImageFxSetup` | 9 | 11 | MISSING | Lib-compound(nested) | no sw atom found by name (node_registry + leaf-stem scan) and not in glue whitelist |
| `All` | 8 | 9 | PARTIAL | Lib-atom | value_op_all.cpp (leaf stem) |
| `Blur` | 8 | 13 | PARTIAL | Lib-compound(nested) | point_ops_blur.cpp (leaf stem) |
| `Clamp` | 8 | 8 | PARTIAL | Lib-atom | node_registry_math_arithmetic.cpp:87 (NodeSpec anchor) |
| `CurvesToTexture` | 8 | 9 | PARTIAL | Lib-atom | point_ops_curvestotexture.cpp (leaf stem) |
| `DrawPoints` | 8 | 10 | PARTIAL | Lib-compound(nested) | node_registry_draw_render.cpp:23 (NodeSpec anchor) |
| `LinePoints` | 8 | 10 | PARTIAL | Lib-compound(nested) | point_ops_linepoints.cpp (leaf stem) |
| `RadialPoints` | 8 | 14 | PARTIAL | Lib-compound(nested) | resident_cook_parity_selftest.cpp:126 (register call) |
| `ReadFile` | 8 | 10 | PARTIAL | Lib-atom | string_ops_readfile.cpp (leaf stem) |
| `RequestedResolution` | 8 | 8 | PARTIAL | Lib-atom | node_registry_math_anim.cpp:155 (NodeSpec anchor) |
| `Time` | 8 | 9 | PARTIAL | Lib-atom | node_registry_math_time.cpp:26 (NodeSpec anchor) |
| `Vector2` | 8 | 9 | MISSING | TypeOperators-atom(no .t3) | no sw atom found by name (node_registry + leaf-stem scan) and not in glue whitelist |
| `Clamped` | 7 | 7 | MISSING | TypeOperators-atom(no .t3) | no sw atom found by name (node_registry + leaf-stem scan) and not in glue whitelist |
| `CombineBuffers` | 7 | 8 | MISSING(見意外⑤，可能已有 uuid-fallback) | Lib-compound(nested) | compound_save.cpp:30 shows a KEPT uuid row for a RETIRED flat atom (廢棄節點退場 pilot #2, 2026-07-08) — census swDoneSet false negative, needs re-check |
| `DrawScreenQuad` | 7 | 7 | PARTIAL | Lib-compound(nested) | node_registry_draw_render.cpp:133 (NodeSpec anchor) |
| `DrawSphereGizmo` | 7 | 10 | PARTIAL | Lib-compound(nested) | pointlist_ops_drawspheregizmo.cpp (leaf stem) |
| `ListToBuffer` | 7 | 10 | PARTIAL | Lib-atom | pointlist_ops_listtobuffer.cpp (leaf stem) |
| `MaxInt` | 7 | 8 | PARTIAL | Lib-atom | value_op_maxint.cpp (leaf stem) |
| `Modulo` | 7 | 8 | PARTIAL | Lib-atom | node_registry_math_arithmetic.cpp:173 (NodeSpec anchor) |
| `SearchAndReplace` | 7 | 12 | PARTIAL | Lib-atom | string_ops_searchandreplace.cpp (leaf stem) |
| `StructuredBuffer` | 7 | 7 | MISSING | TypeOperators-atom(no .t3) | no sw atom found by name (node_registry + leaf-stem scan) and not in glue whitelist |
| `UavFromStructuredBuffer` | 7 | 7 | MISSING | Lib-atom | no sw atom found by name (node_registry + leaf-stem scan) and not in glue whitelist |
| `AnimValue` | 6 | 6 | PARTIAL | Lib-atom | node_registry_math_anim.cpp:55 (NodeSpec anchor) |
| `ApplyTransformMatrix` | 6 | 6 | MISSING | Lib-atom | no sw atom found by name (node_registry + leaf-stem scan) and not in glue whitelist |
| `DrawMesh` | 6 | 8 | MISSING | Lib-compound(nested) | no sw atom found by name (node_registry + leaf-stem scan) and not in glue whitelist |
| `GetTextureFromContext` | 6 | 8 | MISSING | Lib-atom | no sw atom found by name (node_registry + leaf-stem scan) and not in glue whitelist |
| `RecomputeNormals` | 6 | 6 | PARTIAL | Lib-compound(nested) | mesh_ops_recomputenormals.cpp (leaf stem) |
| `Vec2ToVec3` | 6 | 7 | PARTIAL | Lib-atom | value_op_vec2tovec3.cpp (leaf stem) |
| `Blob` | 5 | 6 | PARTIAL | Lib-compound(nested) | point_ops_blob.cpp (leaf stem) |
| `ConvertFormat` | 5 | 7 | MISSING | Lib-compound(nested) | no sw atom found by name (node_registry + leaf-stem scan) and not in glue whitelist |
| `FloatToString` | 5 | 7 | PARTIAL | Lib-atom | string_ops_floattostring.cpp (leaf stem) |
| `GenerateMips` | 5 | 5 | MISSING | Lib-atom | no sw atom found by name (node_registry + leaf-stem scan) and not in glue whitelist |
| `GetCamTransformBuffer` | 5 | 5 | MISSING | Lib-atom | no sw atom found by name (node_registry + leaf-stem scan) and not in glue whitelist |
| `GetFloatVar` | 5 | 5 | PARTIAL | Lib-atom | node_registry_math_contextvar.cpp:37 (NodeSpec anchor) |
| `PickInt` | 5 | 6 | PARTIAL | Lib-atom | value_op_pickint.cpp (leaf stem) |
| `Point` | 5 | 19 | MISSING | TypeOperators-atom(no .t3) | no sw atom found by name (node_registry + leaf-stem scan) and not in glue whitelist |
| `RgbaToColor` | 5 | 6 | PARTIAL | Lib-atom | value_op_rgbatocolor.cpp (leaf stem) |
| `String` | 5 | 9 | MISSING | TypeOperators-atom(no .t3) | no sw atom found by name (node_registry + leaf-stem scan) and not in glue whitelist |
| `TransformImage` | 5 | 11 | PARTIAL | Lib-compound(nested) | point_ops_transformimage.cpp (leaf stem) |
| `clampedSampler` | 5 | 5 | MISSING | TypeOperators-atom(no .t3) | no sw atom found by name (node_registry + leaf-stem scan) and not in glue whitelist |
| `Blend` | 4 | 9 | PARTIAL | Lib-compound(nested) | point_ops_blend.cpp (leaf stem) |
| `BlendWithMask` | 4 | 10 | PARTIAL | Lib-compound(nested) | point_ops_blendwithmask.cpp (leaf stem) |
| `CamPosition` | 4 | 4 | PARTIAL | Lib-atom | node_registry_draw_camera.cpp:83 (NodeSpec anchor) |
| `CombineStrings` | 4 | 6 | PARTIAL | Lib-atom | string_ops_combinestrings.cpp (leaf stem) |
| `Compare` | 4 | 5 | PARTIAL | Lib-atom | node_registry_math_logic.cpp:33 (NodeSpec anchor) |
| `DrawBillboards` | 4 | 4 | PARTIAL | Lib-compound(nested) | node_registry_draw_render.cpp:116 (NodeSpec anchor) |
| `HasValueIncreased` | 4 | 4 | PARTIAL | Lib-atom | node_registry_math_logic.cpp:47 (NodeSpec anchor) |
| `Indices` | 4 | 4 | MISSING | TypeOperators-atom(no .t3) | no sw atom found by name (node_registry + leaf-stem scan) and not in glue whitelist |
| `Int2ToVector2` | 4 | 4 | MISSING | Lib-atom | no sw atom found by name (node_registry + leaf-stem scan) and not in glue whitelist |
| `LinearSampler` | 4 | 4 | MISSING | TypeOperators-atom(no .t3) | no sw atom found by name (node_registry + leaf-stem scan) and not in glue whitelist |
| `NewVertices` | 4 | 4 | MISSING | TypeOperators-atom(no .t3) | no sw atom found by name (node_registry + leaf-stem scan) and not in glue whitelist |
| `PairPointsForLines` | 4 | 8 | PARTIAL | Lib-compound(nested) | point_ops_pairpointsforlines.cpp (leaf stem) |
| `PickString` | 4 | 4 | PARTIAL | Lib-atom | string_ops_pickstring.cpp (leaf stem) |
| `Pow` | 4 | 7 | PARTIAL | Lib-atom | node_registry_math_arithmetic.cpp:163 (NodeSpec anchor) |
| `RoundedRect` | 4 | 4 | PARTIAL | Lib-compound(nested) | point_ops_roundedrect.cpp (leaf stem) |
| `ScaleVector2` | 4 | 4 | PARTIAL | Lib-atom | value_op_scalevector2.cpp (leaf stem) |
| `SplitMeshVertices` | 4 | 4 | PARTIAL | Lib-compound(nested) | mesh_ops_splitmeshvertices.cpp (leaf stem) |
| `UseTextureReference` | 4 | 4 | PARTIAL | Lib-atom | point_ops_usetexturereference.cpp (leaf stem) |
| `VertexStride` | 4 | 4 | MISSING | TypeOperators-atom(no .t3) | no sw atom found by name (node_registry + leaf-stem scan) and not in glue whitelist |
| `Vertices` | 4 | 4 | MISSING | TypeOperators-atom(no .t3) | no sw atom found by name (node_registry + leaf-stem scan) and not in glue whitelist |
| `WrappedSampler` | 4 | 4 | MISSING | TypeOperators-atom(no .t3) | no sw atom found by name (node_registry + leaf-stem scan) and not in glue whitelist |
| `_BlobOld` | 4 | 10 | MISSING | Lib-compound(nested) | no sw atom found by name (node_registry + leaf-stem scan) and not in glue whitelist |
| `padding` | 4 | 5 | MISSING | TypeOperators-atom(no .t3) | no sw atom found by name (node_registry + leaf-stem scan) and not in glue whitelist |
| `Ceil` | 3 | 3 | PARTIAL | Lib-atom | node_registry_math_arithmetic.cpp:183 (NodeSpec anchor) |
| `DefineLensFlare` | 3 | 3 | MISSING | Lib-atom | no sw atom found by name (node_registry + leaf-stem scan) and not in glue whitelist |
| `Displace` | 3 | 3 | PARTIAL | Lib-compound(nested) | point_ops_displace.cpp (leaf stem) |
| `DrawMeshUnlit` | 3 | 4 | PARTIAL | Lib-compound(nested) | node_registry_draw_render.cpp:169 (NodeSpec anchor) |
| `FirstValidBuffer` | 3 | 3 | MISSING | Lib-compound(nested) | no sw atom found by name (node_registry + leaf-stem scan) and not in glue whitelist |
| `Grain` | 3 | 3 | PARTIAL | Lib-compound(nested) | point_ops_grain.cpp (leaf stem) |
| `IntDiv` | 3 | 3 | PARTIAL | Lib-atom | value_op_intdiv.cpp (leaf stem) |
| `KeepPreviousFrame` | 3 | 3 | PARTIAL | Lib-atom | point_ops_keeppreviousframe.cpp (leaf stem) |
| `LinearGradient` | 3 | 9 | PARTIAL | Lib-compound(nested) | point_ops_lineargradient.cpp (leaf stem) |
| `Log` | 3 | 5 | PARTIAL | Lib-atom | node_registry_math_arithmetic.cpp:204 (NodeSpec anchor) |
| `MouseInput` | 3 | 3 | PARTIAL | Lib-atom | node_registry_math_input.cpp:71 (NodeSpec anchor) |
| `Not` | 3 | 3 | PARTIAL | Lib-atom | value_op_not.cpp (leaf stem) |
| `Once` | 3 | 3 | MISSING | Lib-atom | no sw atom found by name (node_registry + leaf-stem scan) and not in glue whitelist |
| `Or` | 3 | 4 | PARTIAL | Lib-atom | value_op_or.cpp (leaf stem) |
| `OrientPoints` | 3 | 3 | PARTIAL | Lib-compound(nested) | point_ops_orientpoints.cpp (leaf stem) |
| `Padding` | 3 | 6 | MISSING | TypeOperators-atom(no .t3) | no sw atom found by name (node_registry + leaf-stem scan) and not in glue whitelist |
| `PickFloat` | 3 | 7 | PARTIAL | Lib-atom | value_op_pickfloat.cpp (leaf stem) |
| `PickMeshBuffer` | 3 | 6 | PARTIAL | Lib-atom | mesh_ops_pickmeshbuffer.cpp (leaf stem) |
| `PickSDXVector4` | 3 | 12 | MISSING | Lib-atom | no sw atom found by name (node_registry + leaf-stem scan) and not in glue whitelist |
| `PickStringFromList` | 3 | 4 | PARTIAL | Lib-atom | string_ops_pickstringfromlist.cpp (leaf stem) |
| `QuadMesh` | 3 | 3 | PARTIAL | Lib-atom | mesh_ops_quadmesh.cpp (leaf stem) |
| `RandomizePoints` | 3 | 3 | PARTIAL | Lib-compound(nested) | point_ops_randomizepoints.cpp (leaf stem) |
| `Reset` | 3 | 3 | MISSING | TypeOperators-atom(no .t3) | no sw atom found by name (node_registry + leaf-stem scan) and not in glue whitelist |
| `RtvFromTexture2d` | 3 | 4 | MISSING | Lib-atom | no sw atom found by name (node_registry + leaf-stem scan) and not in glue whitelist |
| `SampleGradient` | 3 | 3 | PARTIAL | Lib-atom | value_op_samplegradient.cpp (leaf stem) |
| `ScaleSize` | 3 | 5 | PARTIAL | Lib-atom | value_op_scalesize.cpp (leaf stem) |
| `SetTime` | 3 | 3 | PARTIAL | Lib-atom | node_registry_draw_flow.cpp:74 (NodeSpec anchor) |
| `Sigmoid` | 3 | 3 | PARTIAL | Lib-atom | node_registry_math_arithmetic.cpp:255 (NodeSpec anchor) |
| `SliceViewPort` | 3 | 5 | PARTIAL | Lib-atom | node_registry_draw_render.cpp:317 (NodeSpec anchor) |
| `SphereMesh` | 3 | 3 | PARTIAL | Lib-atom | mesh_ops_spheremesh.cpp (leaf stem) |
| `Sub` | 3 | 3 | PARTIAL | Lib-atom | node_registry_math_arithmetic.cpp:67 (NodeSpec anchor) |
| `SubInts` | 3 | 3 | PARTIAL | Lib-atom | value_op_subints.cpp (leaf stem) |
| `TransformMesh` | 3 | 5 | PARTIAL | Lib-compound(nested) | mesh_ops_transformmesh.cpp (leaf stem) |
| `Vector4` | 3 | 3 | MISSING | TypeOperators-atom(no .t3) | no sw atom found by name (node_registry + leaf-stem scan) and not in glue whitelist |
| `VertexCount` | 3 | 4 | MISSING | TypeOperators-atom(no .t3) | no sw atom found by name (node_registry + leaf-stem scan) and not in glue whitelist |
| `Viewport` | 3 | 3 | MISSING | Lib-atom | no sw atom found by name (node_registry + leaf-stem scan) and not in glue whitelist |
| `Wrapped` | 3 | 3 | MISSING | TypeOperators-atom(no .t3) | no sw atom found by name (node_registry + leaf-stem scan) and not in glue whitelist |
| `_QuizUp` | 3 | 3 | MISSING | Lib-compound(nested) | no sw atom found by name (node_registry + leaf-stem scan) and not in glue whitelist |
| `_padding` | 3 | 5 | MISSING | TypeOperators-atom(no .t3) | no sw atom found by name (node_registry + leaf-stem scan) and not in glue whitelist |
| `BlendOnWhites` | 2 | 2 | MISSING | TypeOperators-atom(no .t3) | no sw atom found by name (node_registry + leaf-stem scan) and not in glue whitelist |
| `BufferA` | 2 | 2 | MISSING | TypeOperators-atom(no .t3) | no sw atom found by name (node_registry + leaf-stem scan) and not in glue whitelist |
| `BufferB` | 2 | 2 | MISSING | TypeOperators-atom(no .t3) | no sw atom found by name (node_registry + leaf-stem scan) and not in glue whitelist |
| `CTA` | 2 | 2 | MISSING | TypeOperators-atom(no .t3) | no sw atom found by name (node_registry + leaf-stem scan) and not in glue whitelist |
| `CenterGlow` | 2 | 2 | MISSING | TypeOperators-atom(no .t3) | no sw atom found by name (node_registry + leaf-stem scan) and not in glue whitelist |
| `CheckerBoard` | 2 | 2 | PARTIAL | Lib-compound(nested) | point_ops_checkerboard.cpp (leaf stem) |
| `Clear` | 2 | 2 | MISSING | TypeOperators-atom(no .t3) | no sw atom found by name (node_registry + leaf-stem scan) and not in glue whitelist |
| `Color` | 2 | 3 | MISSING | TypeOperators-atom(no .t3) | no sw atom found by name (node_registry + leaf-stem scan) and not in glue whitelist |
| `ColorGrade` | 2 | 3 | PARTIAL | Lib-compound(nested) | point_ops_colorgrade.cpp (leaf stem) |
| `CompareImages` | 2 | 2 | MISSING | Lib-compound(nested) | no sw atom found by name (node_registry + leaf-stem scan) and not in glue whitelist |
| `ComputeImageDifference` | 2 | 2 | MISSING | Lib-compound(nested) | no sw atom found by name (node_registry + leaf-stem scan) and not in glue whitelist |
| `Count` | 2 | 2 | MISSING | TypeOperators-atom(no .t3) | no sw atom found by name (node_registry + leaf-stem scan) and not in glue whitelist |
| `CustomPointShader` | 2 | 2 | MISSING | Lib-compound(nested) | no sw atom found by name (node_registry + leaf-stem scan) and not in glue whitelist |
| `CylinderMesh` | 2 | 4 | PARTIAL | Lib-atom | mesh_ops_cylindermesh.cpp (leaf stem) |
| `Damp` | 2 | 2 | PARTIAL | Lib-atom | node_registry_math_stateful.cpp:30 (NodeSpec anchor) |
| `DelayTriggerChange` | 2 | 3 | PARTIAL | Lib-atom | node_registry_math_time.cpp:161 (NodeSpec anchor) |
| `Digital` | 2 | 2 | MISSING | TypeOperators-atom(no .t3) | no sw atom found by name (node_registry + leaf-stem scan) and not in glue whitelist |
| `DrawLensFlares` | 2 | 6 | MISSING | Lib-compound(nested) | no sw atom found by name (node_registry + leaf-stem scan) and not in glue whitelist |
| `DrawLensShimmer` | 2 | 4 | MISSING | Lib-compound(nested) | no sw atom found by name (node_registry + leaf-stem scan) and not in glue whitelist |
| `FilesInFolder` | 2 | 2 | PARTIAL | Lib-atom | stringlist_ops_filesinfolder.cpp (leaf stem) |
| `FilterPoints` | 2 | 2 | PARTIAL | Lib-compound(nested) | point_ops_filterpoints.cpp (leaf stem) |
| `Floor` | 2 | 5 | PARTIAL | Lib-atom | node_registry_math_arithmetic.cpp:130 (NodeSpec anchor) |
| `GeometryShader` | 2 | 2 | MISSING | TypeOperators-atom(no .t3) | no sw atom found by name (node_registry + leaf-stem scan) and not in glue whitelist |
| `GetFrameSpeedFactor` | 2 | 2 | PARTIAL | Lib-atom | node_registry_math_time.cpp:143 (NodeSpec anchor) |
| `GetIndices` | 2 | 2 | MISSING | TypeOperators-atom(no .t3) | no sw atom found by name (node_registry + leaf-stem scan) and not in glue whitelist |
| `GetMatrixVar` | 2 | 2 | PARTIAL | Lib-atom | node_registry_math_anim.cpp:311 (NodeSpec anchor) |
| `GetShadowMap` | 2 | 2 | MISSING | TypeOperators-atom(no .t3) | no sw atom found by name (node_registry + leaf-stem scan) and not in glue whitelist |
| `GetVertices` | 2 | 2 | MISSING | TypeOperators-atom(no .t3) | no sw atom found by name (node_registry + leaf-stem scan) and not in glue whitelist |
| `HSLToColor` | 2 | 3 | PARTIAL | Lib-atom | value_op_hsltocolor.cpp (leaf stem) |
| `ImageLevels` | 2 | 3 | PARTIAL | Lib-compound(nested) | point_ops_imagelevels.cpp (leaf stem) |
| `IntToWrapmode` | 2 | 2 | PARTIAL | Lib-atom | node_registry_math_intbasic.cpp:51 (NodeSpec anchor) |
| `IsIntEven` | 2 | 2 | PARTIAL | Lib-atom | value_op_isinteven.cpp (leaf stem) |
| `KeepFloatValues` | 2 | 2 | PARTIAL | Lib-atom | floatlist_ops_keepfloatvalues.cpp (leaf stem) |
| `LinearSamplePointAttributes` | 2 | 2 | PARTIAL | Lib-compound(nested) | point_ops_linearsamplepointattributes.cpp (leaf stem) |
| `Magnitude` | 2 | 2 | PARTIAL | Lib-atom | value_op_magnitude.cpp (leaf stem) |
| `MeshVerticesToPoints` | 2 | 5 | PARTIAL | Lib-compound(nested) | point_ops_meshverticestopoints.cpp (leaf stem) |
| `MinInt` | 2 | 2 | PARTIAL | Lib-atom | value_op_minint.cpp (leaf stem) |
| `MultiIris` | 2 | 2 | MISSING | TypeOperators-atom(no .t3) | no sw atom found by name (node_registry + leaf-stem scan) and not in glue whitelist |
| `MultiplyInts` | 2 | 3 | PARTIAL | Lib-atom | value_op_multiplyints.cpp (leaf stem) |
| `NGon` | 2 | 6 | PARTIAL | Lib-compound(nested) | point_ops_ngon.cpp (leaf stem) |
| `NewFaceIndices` | 2 | 2 | MISSING | TypeOperators-atom(no .t3) | no sw atom found by name (node_registry + leaf-stem scan) and not in glue whitelist |
| `NormalizeVector3` | 2 | 2 | PARTIAL | Lib-atom | value_op_normalizevector3.cpp (leaf stem) |
| `Output` | 2 | 2 | MISSING | TypeOperators-atom(no .t3) | no sw atom found by name (node_registry + leaf-stem scan) and not in glue whitelist |
| `PickStringPart` | 2 | 2 | PARTIAL | Lib-atom | string_ops_pickstringpart.cpp (leaf stem) |
| `PointLight` | 2 | 5 | MISSING | Lib-compound(nested) | no sw atom found by name (node_registry + leaf-stem scan) and not in glue whitelist |
| `PointSimulation` | 2 | 2 | MISSING | Lib-compound(nested) | no sw atom found by name (node_registry + leaf-stem scan) and not in glue whitelist |
| `PointsToCPU` | 2 | 2 | PARTIAL | Lib-atom | pointlist_ops_pointstocpu.cpp (leaf stem) |
| `RadialGradient` | 2 | 2 | PARTIAL | Lib-compound(nested) | point_ops_radialgradient.cpp (leaf stem) |
| `RandomColorEdgeGlow` | 2 | 2 | MISSING | TypeOperators-atom(no .t3) | no sw atom found by name (node_registry + leaf-stem scan) and not in glue whitelist |
| `RemapColor` | 2 | 3 | PARTIAL | Lib-compound(nested) | point_ops_remapcolor.cpp (leaf stem) |
| `ResultPoints` | 2 | 2 | MISSING | TypeOperators-atom(no .t3) | no sw atom found by name (node_registry + leaf-stem scan) and not in glue whitelist |
| `SetContextTexture` | 2 | 2 | MISSING | Lib-atom | no sw atom found by name (node_registry + leaf-stem scan) and not in glue whitelist |
| `SetFloatVar` | 2 | 3 | PARTIAL | Lib-atom | node_registry_math_contextvar.cpp:26 (NodeSpec anchor) |
| `SetFog` | 2 | 2 | PARTIAL | Lib-atom | node_registry_draw_shading.cpp:66 (NodeSpec anchor) |
| `SetIntVar` | 2 | 3 | PARTIAL | Lib-atom | node_registry_math_contextvar.cpp:54 (NodeSpec anchor) |
| `Shimmer` | 2 | 2 | MISSING | TypeOperators-atom(no .t3) | no sw atom found by name (node_registry + leaf-stem scan) and not in glue whitelist |
| `Size` | 2 | 2 | MISSING | TypeOperators-atom(no .t3) | no sw atom found by name (node_registry + leaf-stem scan) and not in glue whitelist |
| `Sparkle` | 2 | 2 | MISSING | TypeOperators-atom(no .t3) | no sw atom found by name (node_registry + leaf-stem scan) and not in glue whitelist |
| `Star` | 2 | 2 | MISSING | TypeOperators-atom(no .t3) | no sw atom found by name (node_registry + leaf-stem scan) and not in glue whitelist |
| `StringLength` | 2 | 2 | PARTIAL | Lib-atom | string_ops_stringlength.cpp (leaf stem) |
| `SwapTextures` | 2 | 2 | PARTIAL | Lib-atom | point_ops_swaptextures.cpp (leaf stem) |
| `TimeToString` | 2 | 2 | MISSING | Lib-compound(nested) | no sw atom found by name (node_registry + leaf-stem scan) and not in glue whitelist |
| `Vec3Distance` | 2 | 2 | PARTIAL | Lib-atom | value_op_vec3distance.cpp (leaf stem) |
| `VertexShaderFromSource` | 2 | 2 | MISSING | TypeOperators-atom(no .t3) | no sw atom found by name (node_registry + leaf-stem scan) and not in glue whitelist |
| `VisualizePoints` | 2 | 2 | MISSING | Lib-compound(nested) | no sw atom found by name (node_registry + leaf-stem scan) and not in glue whitelist |
| `_BuildSpatialHashMap` | 2 | 2 | MISSING | Lib-compound(nested) | no sw atom found by name (node_registry + leaf-stem scan) and not in glue whitelist |
| `_CameraGizmo` | 2 | 2 | MISSING | Lib-compound(nested) | no sw atom found by name (node_registry + leaf-stem scan) and not in glue whitelist |
| `_ReadIntFromGpuBuffer` | 2 | 2 | MISSING | Lib-atom | no sw atom found by name (node_registry + leaf-stem scan) and not in glue whitelist |
| `_RenderFontBuffer` | 2 | 2 | MISSING | Lib-atom | no sw atom found by name (node_registry + leaf-stem scan) and not in glue whitelist |
| `_trippleImageFxSetup` | 2 | 2 | MISSING | Lib-compound(nested) | no sw atom found by name (node_registry + leaf-stem scan) and not in glue whitelist |
| `linearSampler` | 2 | 2 | MISSING | TypeOperators-atom(no .t3) | no sw atom found by name (node_registry + leaf-stem scan) and not in glue whitelist |
| `texSampler` | 2 | 2 | MISSING | TypeOperators-atom(no .t3) | no sw atom found by name (node_registry + leaf-stem scan) and not in glue whitelist |

## 附錄 — 只出現在 1 顆複合的型別（長尾，288 種，按名稱字母序，精簡欄位）

| 積木名 | 所在複合 | sw 現況 | .t3 形狀 |
|---|---|---|---|
| `A` | `JonBakerSDFLoader` | MISSING | TypeOperators-atom(no .t3) |
| `Abs` | `SimCentricalOffset` | PARTIAL | Lib-atom |
| `AddInt2` | `PointsOnImage` | PARTIAL | Lib-atom |
| `AddNoise` | `SimForceOffset` | MISSING | Lib-compound(nested) |
| `AddVec2` | `FadingSlideShow` | PARTIAL | Lib-atom |
| `AnalyzeFloatList` | `_ValueQuizGraph` | PARTIAL | Lib-atom |
| `AnalyzeMeshBuffers` | `CombineMeshes` | MISSING | Lib-compound(nested) |
| `ApplyCamMatrices` | `DrawCamGizmos` | MISSING | Lib-atom |
| `ApplyCamTransform` | `VisualizeCamTrail` | MISSING | Lib-atom |
| `ArrayLength` | `TimeDisplace` | MISSING | TypeOperators-atom(no .t3) |
| `AspectRatio` | `AfterGlow` | MISSING | TypeOperators-atom(no .t3) |
| `AttenuationRadius` | `SpatialAudioPlayerGizmo` | MISSING | TypeOperators-atom(no .t3) |
| `AuxBuffer` | `PrefixSum` | MISSING | TypeOperators-atom(no .t3) |
| `Axis` | `VisualizePoints` | MISSING | TypeOperators-atom(no .t3) |
| `B` | `JonBakerSDFLoader` | MISSING | TypeOperators-atom(no .t3) |
| `Background` | `Sketch` | MISSING | TypeOperators-atom(no .t3) |
| `BgModel` | `RemoveStaticBackground` | MISSING | TypeOperators-atom(no .t3) |
| `BlendEnabled` | `CustomPixelShader` | MISSING | TypeOperators-atom(no .t3) |
| `BlendOnWhites01` | `PickBlendMode` | MISSING | TypeOperators-atom(no .t3) |
| `BlurSize` | `DepthOfField` | MISSING | TypeOperators-atom(no .t3) |
| `BlurWithMask` | `ShadowPlane` | MISSING | Lib-compound(nested) |
| `BufferA1` | `SimpleLiquid2` | MISSING | TypeOperators-atom(no .t3) |
| `BufferB1` | `SimpleLiquid2` | MISSING | TypeOperators-atom(no .t3) |
| `BufferB2` | `SimpleLiquid2` | MISSING | TypeOperators-atom(no .t3) |
| `BuildAsciiFontSorting` | `AsciiRender` | MISSING | Lib-atom |
| `C` | `JonBakerSDFLoader` | MISSING | TypeOperators-atom(no .t3) |
| `COMPLETED` | `_QuizUp` | MISSING | TypeOperators-atom(no .t3) |
| `CamPos` | `DrawPointsDOF` | MISSING | TypeOperators-atom(no .t3) |
| `CameraWithRotation` | `Equirectangle` | PARTIAL | Lib-atom |
| `CellCount` | `_BuildSpatialHashMap` | MISSING | TypeOperators-atom(no .t3) |
| `CellEntries` | `_BuildSpatialHashMap` | MISSING | TypeOperators-atom(no .t3) |
| `ChunkDefs` | `DrawMeshChunksAtPoints` | MISSING | TypeOperators-atom(no .t3) |
| `ChunkSizes` | `DrawMeshChunksAtPoints` | MISSING | TypeOperators-atom(no .t3) |
| `ClampedDrawFaceCount` | `DrawMeshChunksAtPoints` | MISSING | TypeOperators-atom(no .t3) |
| `ClearLinesOnReset` | `DrawConnectionLines` | MISSING | TypeOperators-atom(no .t3) |
| `ClearRenderTarget` | `SetShadow` | PARTIAL | Lib-atom |
| `ColorOutput` | `SimpleLiquid2` | MISSING | TypeOperators-atom(no .t3) |
| `CombineMeshes` | `VisualizeSpotLights` | PARTIAL | Lib-compound(nested) |
| `CompareFloatLists` | `ValueQuiz` | PARTIAL | Lib-atom |
| `ConnectPoints` | `DrawConnectionLines` | MISSING | TypeOperators-atom(no .t3) |
| `ConvertColors` | `AdvancedFeedback2` | PARTIAL | Lib-compound(nested) |
| `ConvertEquirectangle` | `Equirectangle` | MISSING | Lib-compound(nested) |
| `CountForALine` | `RaymarchPoints` | MISSING | TypeOperators-atom(no .t3) |
| `CountWithReflections` | `RaymarchPoints` | MISSING | TypeOperators-atom(no .t3) |
| `CubeMesh` | `DrawCamGizmos` | PARTIAL | Lib-atom |
| `CurrentCamMatrices` | `SetShadow` | PARTIAL | Lib-atom |
| `CurrentIter` | `JumpFloodFill` | MISSING | TypeOperators-atom(no .t3) |
| `CurrentIterPow` | `JumpFloodFill` | MISSING | TypeOperators-atom(no .t3) |
| `Curve` | `_ValueQuizGraph` | MISSING | TypeOperators-atom(no .t3) |
| `CustomPixelShader` | `VisualizeUvMap` | MISSING | Lib-compound(nested) |
| `CustomSDF` | `JonBakerSDFLoader` | PARTIAL | Lib-atom |
| `CutOffTransparency` | `DrawPointsShaded` | MISSING | TypeOperators-atom(no .t3) |
| `CylceBuffer` | `PointTrail` | MISSING | TypeOperators-atom(no .t3) |
| `DampVec2` | `_ValueQuizGraph` | PARTIAL | Lib-atom |
| `DeltaTime` | `VerletRibbonForce` | MISSING | TypeOperators-atom(no .t3) |
| `DepthBufferAsGrayScale` | `SetShadow` | PARTIAL | Lib-compound(nested) |
| `DepthOfField` | `ScreenCloseUp` | MISSING | Lib-compound(nested) |
| `DepthStencil` | `SetShadow` | MISSING | TypeOperators-atom(no .t3) |
| `DigitCharLinePoints` | `NumberLinePoints` | MISSING | TypeOperators-atom(no .t3) |
| `Down` | `Equirectangle` | MISSING | TypeOperators-atom(no .t3) |
| `DrawCamGizmos` | `SetShadow` | MISSING | Lib-compound(nested) |
| `DrawInstancedIndirect` | `DrawPointsDOF` | MISSING | Lib-atom |
| `DrawLinesAlt` | `VisualizeUvMap` | MISSING | TypeOperators-atom(no .t3) |
| `DrawMeshAtPoints` | `VisualizeSpotLights` | MISSING | Lib-compound(nested) |
| `DrawRibbons` | `DustParticles` | MISSING | Lib-compound(nested) |
| `DrawSpots` | `VisualizeSpotLights` | MISSING | TypeOperators-atom(no .t3) |
| `DsvFromTexture2d` | `SetShadow` | MISSING | Lib-atom |
| `Emit` | `ParticleSystem` | MISSING | TypeOperators-atom(no .t3) |
| `ExecuteArrows` | `SpatialAudioPlayerGizmo` | MISSING | TypeOperators-atom(no .t3) |
| `ExecuteCones` | `SpatialAudioPlayerGizmo` | MISSING | TypeOperators-atom(no .t3) |
| `ExecuteHeightmapSdf` | `HeightMapSdf` | MISSING | Lib-atom |
| `ExecuteImage2dSdf` | `Image2dSDF` | MISSING | Lib-atom |
| `ExecuteLocators` | `SpatialAudioPlayerGizmo` | MISSING | TypeOperators-atom(no .t3) |
| `ExecuteRepeatFieldAtPoints` | `RepeatFieldAtPoints` | MISSING | Lib-atom |
| `ExecuteValueUpdate` | `ComputeImageDifference` | MISSING | Lib-atom |
| `FaceDrawData` | `DrawMeshChunksAtPoints` | MISSING | TypeOperators-atom(no .t3) |
| `FilePathParts` | `VideoClip` | PARTIAL | Lib-atom |
| `FogParameters` | `DrawLinesShaded` | MISSING | TypeOperators-atom(no .t3) |
| `FractalNoise` | `MakeTileableImageAdvanced` | PARTIAL | Lib-compound(nested) |
| `FrameCount` | `PointTrailFast` | MISSING | TypeOperators-atom(no .t3) |
| `FrameCounter` | `PointTrail` | MISSING | TypeOperators-atom(no .t3) |
| `FrameIndex` | `SortPoints` | MISSING | TypeOperators-atom(no .t3) |
| `FreezeValue` | `LinkToMidiTime` | PARTIAL | Lib-atom |
| `FrontLight` | `ProjectLight` | MISSING | TypeOperators-atom(no .t3) |
| `Geometry` | `SetShadow` | MISSING | TypeOperators-atom(no .t3) |
| `GeometryShaderStage` | `TextureToCubeMap` | MISSING | TypeOperators-atom(no .t3) |
| `GetAllCameras` | `DrawCamGizmos` | MISSING | Lib-atom |
| `GetAllSpatialAudioPlayers` | `DrawSpatialAudioGizmos` | MISSING | Lib-atom |
| `GetAttributeFromJsonString` | `JonBakerSDFLoader` | MISSING | Lib-atom |
| `GetLightPosition` | `ShadowPlane` | MISSING | Lib-atom |
| `GetPosition` | `Locator` | PARTIAL | Lib-atom |
| `GetPrefilteredSpecular` | `DrawMeshWithShadow` | MISSING | TypeOperators-atom(no .t3) |
| `GetVerrtices` | `MeshVerticesToPoints` | MISSING | TypeOperators-atom(no .t3) |
| `Gitch` | `RgbTV` | MISSING | TypeOperators-atom(no .t3) |
| `GizmoColor` | `SelectPoints` | MISSING | TypeOperators-atom(no .t3) |
| `Goal` | `_QuizUp` | MISSING | TypeOperators-atom(no .t3) |
| `GridPlane` | `_OutputWindowGrid` | MISSING | Lib-compound(nested) |
| `HasBooleanChanged` | `RemoveStaticBackground` | PARTIAL | Lib-atom |
| `HasValueChanged` | `SetEnvironment` | PARTIAL | Lib-atom |
| `HasValueDecreased` | `DrawConnectionLines` | PARTIAL | Lib-atom |
| `IFFT` | `ImageFFT` | MISSING | TypeOperators-atom(no .t3) |
| `IndexBuffer` | `SortPoints` | MISSING | TypeOperators-atom(no .t3) |
| `IndicesStride` | `RepeatMeshAtPoints` | MISSING | TypeOperators-atom(no .t3) |
| `InnerConeGizmo` | `SpatialAudioPlayerGizmo` | MISSING | TypeOperators-atom(no .t3) |
| `InputBufferSize` | `SortPoints` | MISSING | TypeOperators-atom(no .t3) |
| `InputJson` | `JonBakerSDFLoader` | MISSING | TypeOperators-atom(no .t3) |
| `Int3` | `DrawPointsDOF` | MISSING | TypeOperators-atom(no .t3) |
| `IntAdd` | `FadingSlideShow` | PARTIAL | Lib-atom |
| `IntListLength` | `NumberLinePoints` | PARTIAL | Lib-atom |
| `IntListToBuffer` | `NumberLinePoints` | MISSING | Lib-atom |
| `Invert` | `PickBlendMode` | MISSING | TypeOperators-atom(no .t3) |
| `InvertBufferCount` | `DrawPointsDOF` | MISSING | TypeOperators-atom(no .t3) |
| `IsBufferDirty` | `RecomputeNormals` | MISSING | Lib-atom |
| `IsGreater` | `DustParticles` | PARTIAL | Lib-atom |
| `JoinLists` | `WaveForm` | PARTIAL | Lib-atom |
| `KeepInTextureArray` | `TimeDisplace` | PARTIAL | Lib-atom |
| `KeepPreviousPointBuffer` | `DrawMovingPoints` | MISSING | Lib-atom |
| `KochKaleidoskope` | `RgbTV` | MISSING | Lib-compound(nested) |
| `LastIter` | `JumpFloodFill` | MISSING | TypeOperators-atom(no .t3) |
| `LastIterPow` | `JumpFloodFill` | MISSING | TypeOperators-atom(no .t3) |
| `Lerp` | `RepeatWithMotionBlur` | PARTIAL | Lib-atom |
| `LightDepthBuffer` | `ProjectLight` | MISSING | TypeOperators-atom(no .t3) |
| `LightTexture` | `ProjectLight` | MISSING | TypeOperators-atom(no .t3) |
| `LinePointPairs` | `DrawConnectionLines` | MISSING | TypeOperators-atom(no .t3) |
| `LineTextPoints` | `NumberLinePoints` | MISSING | Lib-atom |
| `MaskRaw` | `RemoveStaticBackground` | MISSING | TypeOperators-atom(no .t3) |
| `MaskRefined` | `RemoveStaticBackground` | MISSING | TypeOperators-atom(no .t3) |
| `MaxCount` | `SetAttributesWithPointFields` | MISSING | TypeOperators-atom(no .t3) |
| `MaxInt2` | `CombineMaterialChannels` | PARTIAL | Lib-atom |
| `MaxPointsPerDigit` | `NumberLinePoints` | MISSING | TypeOperators-atom(no .t3) |
| `MirrorOnceSampler` | `AdvancedFeedback2` | MISSING | TypeOperators-atom(no .t3) |
| `MockStrings` | `AnimRandomString` | PARTIAL | Lib-atom |
| `ModInt` | `DrawConnectionLines` | PARTIAL | Lib-atom |
| `NeighbourFaceIndices` | `RecomputeNormals` | MISSING | TypeOperators-atom(no .t3) |
| `Noise` | `RgbTV` | MISSING | TypeOperators-atom(no .t3) |
| `NormalTexture` | `ProjectLight` | MISSING | TypeOperators-atom(no .t3) |
| `NumOfItems` | `JonBakerSDFLoader` | MISSING | TypeOperators-atom(no .t3) |
| `NumberLinePoints` | `PointInfoLines` | MISSING | Lib-compound(nested) |
| `Offset` | `JonBakerSDFLoader` | MISSING | TypeOperators-atom(no .t3) |
| `OpticalFlow` | `DetectMotion` | MISSING | Lib-compound(nested) |
| `OrgResolution` | `TransformImage` | MISSING | TypeOperators-atom(no .t3) |
| `OrthoProjector` | `ProjectLight` | MISSING | TypeOperators-atom(no .t3) |
| `OrthographicCamera` | `SetShadow` | PARTIAL | Lib-atom |
| `OutA` | `JonBakerSDFLoader` | MISSING | TypeOperators-atom(no .t3) |
| `OutB` | `JonBakerSDFLoader` | MISSING | TypeOperators-atom(no .t3) |
| `OutC` | `JonBakerSDFLoader` | MISSING | TypeOperators-atom(no .t3) |
| `OutCode` | `JonBakerSDFLoader` | MISSING | TypeOperators-atom(no .t3) |
| `OutOffset` | `JonBakerSDFLoader` | MISSING | TypeOperators-atom(no .t3) |
| `OuterConeGizmo` | `SpatialAudioPlayerGizmo` | MISSING | TypeOperators-atom(no .t3) |
| `PadVec2Range` | `_ValueQuizGraph` | PARTIAL | Lib-atom |
| `PairPointsForSplines` | `VisualizePoints` | PARTIAL | Lib-compound(nested) |
| `ParticleGridBuffer` | `_BuildSpatialHashMap` | MISSING | TypeOperators-atom(no .t3) |
| `ParticleGridCellBuffer` | `_BuildSpatialHashMap` | MISSING | TypeOperators-atom(no .t3) |
| `ParticleGridCountBuffer` | `_BuildSpatialHashMap` | MISSING | TypeOperators-atom(no .t3) |
| `ParticleGridHashBuffer` | `_BuildSpatialHashMap` | MISSING | TypeOperators-atom(no .t3) |
| `Particles` | `ParticleSystem` | MISSING | TypeOperators-atom(no .t3) |
| `PassIndexStart` | `SortPoints` | MISSING | TypeOperators-atom(no .t3) |
| `PerlinNoise` | `RgbTV` | PARTIAL | Lib-atom |
| `PerlinNoise2` | `FadingSlideShow` | PARTIAL | Lib-atom |
| `PhaseMock` | `SoftTransformPoints` | MISSING | TypeOperators-atom(no .t3) |
| `PickObject` | `ProjectLight` | PARTIAL | Lib-atom |
| `PickPointList` | `MovePointsToCurveSpace` | PARTIAL | Lib-atom |
| `PickVector3` | `DrawLineGrid` | PARTIAL | Lib-atom |
| `PlayVideo` | `VideoClip` | PARTIAL | Lib-atom |
| `PointCount` | `RepeatMeshAtPoints` | MISSING | TypeOperators-atom(no .t3) |
| `PointSampler` | `GodRays` | MISSING | TypeOperators-atom(no .t3) |
| `PointTrailFast` | `DustParticles` | PARTIAL | Lib-compound(nested) |
| `Position` | `RenderWithMotionBlur` | MISSING | TypeOperators-atom(no .t3) |
| `PrefixSum` | `_BuildSpatialHashMap` | MISSING | Lib-compound(nested) |
| `PremultipliedAlpha` | `PickBlendMode` | MISSING | TypeOperators-atom(no .t3) |
| `PreviousPositions` | `VerletRibbonForce` | MISSING | TypeOperators-atom(no .t3) |
| `ProgressBar` | `_QuizUp` | MISSING | TypeOperators-atom(no .t3) |
| `Projector` | `ProjectLight` | MISSING | TypeOperators-atom(no .t3) |
| `ProjectorCamera` | `ProjectLight` | MISSING | TypeOperators-atom(no .t3) |
| `Quality` | `DepthOfField` | MISSING | TypeOperators-atom(no .t3) |
| `RangeY` | `_ValueQuizGraph` | MISSING | TypeOperators-atom(no .t3) |
| `Raster` | `TextureMapForce` | PARTIAL | Lib-compound(nested) |
| `RemoveColor` | `VisualizeSpotLights` | MISSING | TypeOperators-atom(no .t3) |
| `ResolutionFactor` | `Glow` | MISSING | TypeOperators-atom(no .t3) |
| `ResultBuffer` | `PrefixSum` | MISSING | TypeOperators-atom(no .t3) |
| `ReuseCamera` | `ProjectLight` | PARTIAL | Lib-atom |
| `RotateX` | `ScreenCloseUp` | MISSING | TypeOperators-atom(no .t3) |
| `RotateY` | `ScreenCloseUp` | MISSING | TypeOperators-atom(no .t3) |
| `Round` | `SortPoints` | PARTIAL | Lib-atom |
| `RowSelect` | `JonBakerSDFLoader` | MISSING | TypeOperators-atom(no .t3) |
| `RyojiPattern2` | `RgbTV` | PARTIAL | Lib-compound(nested) |
| `SampleCurve` | `HoneyCombTiles` | PARTIAL | Lib-atom |
| `ScaleFactor` | `GetImageBrightness` | MISSING | TypeOperators-atom(no .t3) |
| `ScaleResolution` | `TransformImage` | PARTIAL | Lib-atom |
| `ScatterParticlesInCells` | `_BuildSpatialHashMap` | MISSING | TypeOperators-atom(no .t3) |
| `SelectPointsWithSDF` | `SDFToColor` | PARTIAL | Lib-compound(nested) |
| `SetMaterial` | `ScreenCloseUp` | PARTIAL | Lib-atom |
| `SetMatrixVar` | `SetShadow` | PARTIAL | Lib-atom |
| `SetPlaybackTime` | `LinkToMidiTime` | PARTIAL | Lib-atom |
| `SetPointAttributes` | `VisualizePoints` | PARTIAL | Lib-compound(nested) |
| `SetPointLight` | `PointLight` | PARTIAL | Lib-atom |
| `SetRequestedResolution` | `DrawAsSplitView` | PARTIAL | Lib-atom |
| `Setup` | `RgbTV` | MISSING | TypeOperators-atom(no .t3) |
| `ShadowBias` | `ProjectLight` | MISSING | TypeOperators-atom(no .t3) |
| `ShadowMapSize` | `SetShadow` | MISSING | TypeOperators-atom(no .t3) |
| `ShadowScale` | `ProjectLight` | MISSING | TypeOperators-atom(no .t3) |
| `Shape` | `LenseFlareHoop` | MISSING | TypeOperators-atom(no .t3) |
| `SimDirectionalOffset` | `DustParticles` | PARTIAL | Lib-compound(nested) |
| `SimForceOffset` | `DustParticles` | PARTIAL | Lib-compound(nested) |
| `SimNoiseOffset` | `DustParticles` | PARTIAL | Lib-compound(nested) |
| `SoftTransformPoints` | `DustParticles` | PARTIAL | Lib-compound(nested) |
| `Sort` | `DrawPointsDOF` | MISSING | TypeOperators-atom(no .t3) |
| `SortingSpeed` | `SortPoints` | MISSING | TypeOperators-atom(no .t3) |
| `Source` | `ExtrudeCurves` | MISSING | TypeOperators-atom(no .t3) |
| `SpatialAudioPlayerGizmo` | `DrawSpatialAudioGizmos` | MISSING | Lib-compound(nested) |
| `SpherePoints` | `SimCentricalOffset` | PARTIAL | Lib-compound(nested) |
| `SplitString` | `DrawAsSplitView` | PARTIAL | Lib-atom |
| `SpreadScale` | `RemoveStaticBackground` | MISSING | TypeOperators-atom(no .t3) |
| `Sum` | `TransformSomePoints` | PARTIAL | Lib-atom |
| `SwitchBlendState` | `PickBlendMode` | MISSING | Lib-atom |
| `TargetPointCount` | `NumberLinePoints` | MISSING | TypeOperators-atom(no .t3) |
| `TemporalAccumulation` | `DetectMotion` | MISSING | Lib-compound(nested) |
| `TextureToCubeMap` | `SetEnvironment` | MISSING | Lib-compound(nested) |
| `ThreshholdMock` | `SoftTransformPoints` | MISSING | TypeOperators-atom(no .t3) |
| `Tint` | `TextGrid` | PARTIAL | Lib-compound(nested) |
| `TotalPassCount` | `SortPoints` | MISSING | TypeOperators-atom(no .t3) |
| `TotalStepCount` | `SortPoints` | MISSING | TypeOperators-atom(no .t3) |
| `TransformFromClipSpace` | `VisualizePoints` | MISSING | Lib-compound(nested) |
| `TransformListenerArrow` | `SpatialAudioPlayerGizmo` | MISSING | TypeOperators-atom(no .t3) |
| `TransformMeshUVs` | `ScreenCloseUp` | PARTIAL | Lib-compound(nested) |
| `TransformSourceArrow` | `SpatialAudioPlayerGizmo` | MISSING | TypeOperators-atom(no .t3) |
| `TriangleCount` | `ExtrudeCurves` | MISSING | TypeOperators-atom(no .t3) |
| `TriggerAnim` | `_QuizUp` | PARTIAL | Lib-atom |
| `TryParse` | `JonBakerSDFLoader` | PARTIAL | Lib-atom |
| `TypoGridBuffer` | `TextGrid` | MISSING | Lib-atom |
| `UVsViewer` | `VisualizeUvMap` | MISSING | Lib-compound(nested) |
| `Up` | `Equirectangle` | MISSING | TypeOperators-atom(no .t3) |
| `UpdateChunkSizes` | `DrawMeshChunksAtPoints` | MISSING | TypeOperators-atom(no .t3) |
| `UpdateCount` | `VerletRibbonForce` | MISSING | TypeOperators-atom(no .t3) |
| `UpdateDrawData` | `DrawMeshChunksAtPoints` | MISSING | TypeOperators-atom(no .t3) |
| `UpdateIndices` | `DrawMeshChunksAtPoints` | MISSING | TypeOperators-atom(no .t3) |
| `UseImage_Alpha` | `PickBlendMode` | MISSING | TypeOperators-atom(no .t3) |
| `UsingEmitCount` | `ParticleSystem` | MISSING | TypeOperators-atom(no .t3) |
| `ValueRaster` | `_ValueQuizGraph` | PARTIAL | Lib-compound(nested) |
| `ValueToRate` | `SetSpeedFactors` | PARTIAL | Lib-atom |
| `ValuesToTexture` | `PlotValueCurve` | PARTIAL | Lib-atom |
| `ValuesToTexture2` | `_ValueQuizGraph` | PARTIAL | Lib-atom |
| `Vertical` | `Blur` | MISSING | TypeOperators-atom(no .t3) |
| `VerticeSize` | `RecomputeNormals` | MISSING | TypeOperators-atom(no .t3) |
| `ViewCamera` | `ProjectLight` | MISSING | TypeOperators-atom(no .t3) |
| `ViewDir` | `DrawPointsDOF` | MISSING | TypeOperators-atom(no .t3) |
| `WidthToHeight` | `Blur` | MISSING | TypeOperators-atom(no .t3) |
| `Wrap` | `TransformImage` | MISSING | TypeOperators-atom(no .t3) |
| `WrapPointPosition` | `DustParticles` | MISSING | Lib-compound(nested) |
| `Xor` | `LegacyParticleSimulation` | PARTIAL | Lib-atom |
| `Yours` | `_QuizUp` | MISSING | TypeOperators-atom(no .t3) |
| `ZVizScale` | `RemoveStaticBackground` | MISSING | TypeOperators-atom(no .t3) |
| `_AdjustFeedbackImage` | `AdvancedFeedback` | MISSING | Lib-compound(nested) |
| `_AnimValueOld` | `HexGridPoints` | MISSING | Lib-atom |
| `_ComputeDepthToLinear` | `DepthOfField` | MISSING | Lib-compound(nested) |
| `_ComputeLightOcclusions` | `GetPointLightOccclusion` | MISSING | Lib-atom |
| `_DispatchSceneDraws` | `DrawScene` | MISSING | Lib-atom |
| `_DoyleSpiralRoot` | `DoyleSpiralPoints2` | MISSING | Lib-atom |
| `_DrawPointInfo` | `VisualizePoints` | MISSING | Lib-compound(nested) |
| `_DrawQuads` | `ColorPhysarum` | MISSING | Lib-compound(nested) |
| `_ExecuteCombineBuffers` | `CombineBuffers` | SUPPORTED | Lib-atom |
| `_ExecuteFastBlurPasses` | `FastBlur` | MISSING | Lib-atom |
| `_ExecuteSdfToColor` | `SDFToColor` | MISSING | Lib-atom |
| `_FractalNoiseOld` | `AdvancedFeedback2` | MISSING | Lib-compound(nested) |
| `_GetSketchPoints` | `Sketch` | MISSING | Lib-atom |
| `_LenseFlareHoopPosition` | `LenseFlareHoop` | MISSING | Lib-atom |
| `_ReadBackImageDifference` | `ComputeImageDifference` | MISSING | Lib-atom |
| `_ReprojectShadowMap` | `ShadowPlane` | MISSING | Lib-compound(nested) |
| `_SetParticleSystemComponents` | `ParticleSystem` | MISSING | Lib-atom |
| `_SketchImpl` | `Sketch` | MISSING | Lib-atom |
| `_SpecularPrefilter` | `SetEnvironment` | MISSING | Lib-atom |
| `_ValueQuizGraph` | `ValueQuiz` | MISSING | Lib-compound(nested) |
| `_VisualizeTBN` | `VisualizeMesh` | MISSING | Lib-compound(nested) |
| `__Padding` | `ProjectLight` | MISSING | TypeOperators-atom(no .t3) |
| `__Padding__` | `_DrawQuads` | MISSING | TypeOperators-atom(no .t3) |
| `__alignment` | `MirrorRepeat` | MISSING | TypeOperators-atom(no .t3) |
| `_omit` | `PointTrailFast` | MISSING | TypeOperators-atom(no .t3) |
| `constraints` | `VerletRibbonForce` | MISSING | TypeOperators-atom(no .t3) |
| `draw` | `DrawMeshChunksAtPoints` | PARTIAL | Lib-atom |
| `faces` | `RecomputeNormals` | MISSING | TypeOperators-atom(no .t3) |
| `intParams` | `DrawMeshChunksAtPoints` | MISSING | TypeOperators-atom(no .t3) |
| `newIndicesBuffer` | `RepeatMeshAtPoints` | MISSING | TypeOperators-atom(no .t3) |
| `newVertexBuffer` | `RepeatMeshAtPoints` | MISSING | TypeOperators-atom(no .t3) |
| `numsteps` | `ImageFFT` | MISSING | TypeOperators-atom(no .t3) |
| `vertices` | `RecomputeNormals` | MISSING | TypeOperators-atom(no .t3) |
| `xy` | `DrawLineGrid` | MISSING | TypeOperators-atom(no .t3) |
| `xz` | `DrawLineGrid` | MISSING | TypeOperators-atom(no .t3) |
| `yz` | `DrawLineGrid` | MISSING | TypeOperators-atom(no .t3) |

## 對帳

- 目標複合：145 壓平 + 190 未做·複合 = 335 顆
- 找到 .t3 並成功解析：335 顆；找不到 .t3：0 顆；解析失敗：0 顆
- 相異子節點型別：564 種（總出現次數 6215 次）
- SUPPORTED 19 + HANDLED(fold) 1 + PARTIAL 210 + MISSING 334 = 564
- 主表（≥2 複合）276 + 附錄（=1 複合）288 = 564

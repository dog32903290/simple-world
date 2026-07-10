# ENGINE_GAP_BUFFER_SHAPES — compute shader SRV/UAV buffer 形狀分布表

掃描日期：2026-07-10　掃描範圍：external/tixl/Operators/Lib/Assets/shaders/**/*.hlsl 全集（377 個 .hlsl，含 numthreads 的 compute shader 190 個——這是「全域」掃描，不限於已在ENGINE_GAP_GLUE.md 追蹤的 335 顆目標複合；一顆 .hlsl 可能被多個 .t3 引用，owner 對照走全925 顆 Lib，不只 335 顆子集）。

方法（可重跑）：對每個含 `numthreads` 的 .hlsl，用正則抓宣告行`(RW?)(StructuredBuffer|Buffer|ByteAddressBuffer|Texture*D...)<...> Name : (register(x#)|x#);`，依 `RW` 前綴分 SRV/UAV、依是否為 `Texture*` 分 buffer 類/texture 類，依槽位 (t#/u#/b#) 去重計數；island 依路徑前綴分（points→point / 3d/mesh→mesh / img→image / particles→particle / field→field / 其餘→misc 等）。owner 算子名：掃全 925 顆 Lib .t3 的 `ComputeShader` 子節點 `Source` 值（`Lib:shaders/<relpath>`）反查引用它的頂層複合名。

## 頂部摘要

**頭部形狀建議前置清單（累積覆蓋 ≥80%，共 9 種形狀 / 155 顆 = 81.6%）：**

| # | 形狀 | 顆數 | 佔比 | 累積 | sw 現況 |
|---:|---|---:|---:|---:|---|
| 1 | `1SRV-buf+1UAV-buf` | 55 | 28.9% | 28.9% | **已支援**（generic ComputeShaderStage 綁定 + 已有 proven kernel） |
| 2 | `1UAV-buf` | 28 | 14.7% | 43.7% | **已支援**（generic ComputeShaderStage 綁定 + 已有 proven kernel） |
| 3 | `2SRV-buf+1UAV-buf` | 22 | 11.6% | 55.3% | **已支援**（generic ComputeShaderStage 綁定 + 已有 proven kernel） |
| 4 | `1SRV-buf+1UAV-buf+1SRV-tex` | 13 | 6.8% | 62.1% | **未支援**——需新引擎工作 |
| 5 | `1SRV-tex+1UAV-tex` | 9 | 4.7% | 66.8% | **未支援**——需新引擎工作 |
| 6 | `1UAV-tex` | 9 | 4.7% | 71.6% | **未支援**——需新引擎工作 |
| 7 | `1UAV-buf+1SRV-tex` | 8 | 4.2% | 75.8% | **未支援**——需新引擎工作 |
| 8 | `2SRV-tex+1UAV-tex` | 7 | 3.7% | 79.5% | **未支援**——需新引擎工作 |
| 9 | `2SRV-buf+2UAV-buf` | 4 | 2.1% | 81.6% | **未支援**——需新引擎工作 |

前 3 大形狀（105 顆 = 55.3%）已由現有 `buffer_ops_computeshaderstage.cpp` 的 generic 綁定機制（CS_MAX_CB=4 / CS_MAX_SRV=8 / CS_MAX_UAV=4，`computeshaderstage_params.h`）+ 9 個已 port 的 kernel 證實可行，純 buffer 類（無 texture）。第 4-9 名（共 50 顆 = 26.3%）全部涉及 **texture SRV/UAV 綁定**——`buffer_ops_computeshaderstage.cpp` 目前只有 `enc->setBuffer`，完全沒有 `enc->setTexture` 路徑，這是引擎的真缺口（見下方意外①）。

**長尾建議（撞到才補，20 種形狀 / 35 顆 = 18.4%）：**零散大 SRV/UAV 數形狀（如 `5SRV-buf+1UAV-buf`、`1SRV-buf+5UAV-buf`），每種只 1-4 顆，多半是 mesh 拓樸重建（SplitVertices 類）或粒子空間雜湊（spatial-hash-map）等單顆客製 kernel，不值得預先設計通用形狀，等真的退場撞到再逐顆補 kernel 綁定即可（不影響 seam 設計）。

## 意外發現

- **① Texture 綁定是 generic 引擎缺口，不是「多幾個 kernel」的問題**：`buffer_ops_computeshaderstage.cpp` 的整個綁定機制建立在 `bufferwithviews-collapse-to-mtlbuffer` 的假設上（SRV/UAV/Buffer 全部是同一顆 `SwBuffer`/`MTL::Buffer`），對 Texture2D/RWTexture2D 完全沒有對應路徑（沒有 `enc->setTexture`，`inputBufferPorts` 分類只認 ConstantBuffers/ShaderResources/Uavs 三個 Buffer port，NodeSpec 也只宣告`Buffer` dataType）。全域 190 顆 compute shader 裡有 SRV-tex 或 UAV-tex 的共 66 顆（34.7%），集中在 image 島（LUT/濾鏡類）但 point/mesh/particle/sprite 也都有零星幾顆——這是繼 compute-buffer keystone 之後的**第二個 keystone 級缺口**，建議獨立立項（texture-bound compute stage 或擴充現有 stage 支援 mixed buffer+texture 綁定），而非算進逐顆退場的工作量。
- **② 多 UAV 輸出目前只有一個 Output port，第二顆 UAV 的結果沒有出口**：`ComputeShaderStage` 的 NodeSpec 只有一個 `Output`（Buffer）；cook 端 `*c.output = *uavs.front();` 只轉發第一個 UAV。但 `2SRV-buf+2UAV-buf`（4 顆）等形狀宣告了兩個 UAV——TiXL 端這類 kernel 通常靠兩個獨立 `StructuredBufferWithViews` 分別接住，sw 現有單一 Output port接不住第二個。真的排到這批退場時，這不是換 kernel 名字能解決的，需要 NodeSpec 加第二個Output port + cook 端轉發邏輯。
- **③ 唯一真超過綁定上限的 shader**：`points/spatial-hash-map/spatial-hash-map.hlsl`（cb=1, srv=1, uav=5）——5 個 UAV **超過** `computeshaderstage_params.h` 的 `CS_MAX_UAV=4`（Metal flat buffer-index partition u0..u3 只留 4 格）。這顆不只缺單一 Output port（意外②同款問題的加劇版），連 generic 綁定迴圈本身都塞不下第 5 個 UAV（`for (size_t i = 0; i < uavs.size() && i < (size_t)CS_MAX_UAV; ++i)` 會靜默丟棄第 5 個）——需要先擴大 `CS_UAV_BASE`/`CS_MAX_UAV` 的位元分區，此外還撞上意外②的多 Output port 缺口。這顆是空間雜湊建構的核心 kernel，建議留到 texture/多 UAV 缺口都補完後再排。

## 全表 — 全部 29 種形狀（按顆數降冪）

| 形狀 | 顆數 | 佔比 | island 分布 | sw 已支援? | 代表節點（最簡單封印候選） |
|---|---:|---:|---|---|---|
| `1SRV-buf+1UAV-buf` | 55 | 28.9% | point×37, mesh×13, field×2, particle×2, misc×1 | 已支援（見上） | TransformPoints / AddNoise / SnapPointsToGrid / ClearSomePoints / mesh-TransformVertices / mesh-LegacyNoiseDisplace（6 kernel 已驗證同形狀） |
| `1UAV-buf` | 28 | 14.7% | point×16, particle×9, misc×3 | 已支援（見上） | WrapPointPosition（in-place UAV, no SRV） |
| `2SRV-buf+1UAV-buf` | 22 | 11.6% | point×12, mesh×7, particle×3 | 已支援（見上） | SnapToPoints / BlendPoints（dual-SRV + per-SRV elementCount aux） |
| `1SRV-buf+1UAV-buf+1SRV-tex` | 13 | 6.8% | point×10, mesh×3 | 未支援 | `PointsFromMeshData`（points/generate/PointsFromMeshData.hlsl，10 children） |
| `1SRV-tex+1UAV-tex` | 9 | 4.7% | image×7, 3d-other×1, point×1 | 未支援 | `_ComputeDepthToLinear`（img/post-fx/depth-to-linear.hlsl，11 children） |
| `1UAV-tex` | 9 | 4.7% | image×4, 3d-other×2, point×2, misc×1 | 未支援 | `_ComputeBRDFLookup`（3d/rendering/ComputeBrdfLookupTexture-cs.hlsl，6 children） |
| `1UAV-buf+1SRV-tex` | 8 | 4.2% | image×4, point×3, particle×1 | 未支援 | `GetImageBrightness`（img/analyze/cs-GetImageBrightness.hlsl，18 children） |
| `2SRV-tex+1UAV-tex` | 7 | 3.7% | image×5, misc×1, point×1 | 未支援 | `_DepthOfField`（img/post-fx/dof.hlsl，9 children） |
| `2SRV-buf+2UAV-buf` | 4 | 2.1% | mesh×2, point×2 | 未支援 | `SplitMeshVertices`（3d/mesh/mesh-SplitVertices.hlsl，17 children） |
| `3SRV-buf+1UAV-buf` | 4 | 2.1% | point×3, mesh×1 | 未支援 | `FindClosestPointsOnMesh`（points/onmesh/FindClosestPointOnMesh.hlsl，13 children） |
| `1SRV-buf+2UAV-buf` | 4 | 2.1% | point×3, particle×1 | 未支援 | `BoundingBoxPoints`（points/generate/BoundingBoxPoints.hlsl，21 children） |
| `2SRV-buf+1UAV-buf+1SRV-tex` | 3 | 1.6% | mesh×1, point×1, sprite×1 | 未支援 | `GrowStrains`（points/sim/GrowStrains.hlsl，19 children） |
| `1SRV-buf+1UAV-buf+2SRV-tex` | 3 | 1.6% | point×2, mesh×1 | 未支援 | `MapPointAttributes`（points/modify/MapPointAttributes.hlsl，15 children） |
| `1UAV-buf+2SRV-tex` | 3 | 1.6% | image×1, particle×1, point×1 | 未支援 | `ComputeImageDifference`（img/analyze/compute-image-difference.hlsl，19 children） |
| `2SRV-buf+1UAV-buf+2SRV-tex` | 2 | 1.1% | mesh×1, point×1 | 未支援 | `SetAttributesWithPointFields`（points/modify/SetPointAttributesWithPointFields.hlsl，20 children） |
| `1SRV-tex+2UAV-tex` | 2 | 1.1% | image×2 | 未支援 | `JumpFloodFill`（img/generate/img-generate-JumpFloodFill.hlsl，31 children） |
| `2UAV-buf` | 2 | 1.1% | particle×1, point×1 | 未支援 | `CollisionForce`（particles/ParticleCollisionForce.hlsl，13 children） |
| `5SRV-buf+1UAV-buf` | 1 | 0.5% | mesh×1 | 未支援 | `DrawMeshChunksAtPoints`（3d/mesh/chunks/MeshChunks-UpdateFaceDrawData.hlsl，59 children） |
| `2SRV-buf+2UAV-buf+2SRV-tex` | 1 | 0.5% | mesh×1 | 未支援 | （無法反查 owner） |
| `4SRV-tex+1UAV-tex` | 1 | 0.5% | image×1 | 未支援 | `RemoveStaticBackground`（img/analyze/remove-static-background-cs3-output.hlsl，45 children） |
| `1SRV-tex+3UAV-tex` | 1 | 0.5% | image×1 | 未支援 | `SimpleLiquid`（img/fluid-fx/SimpleLiquid-cs.hlsl，31 children） |
| `1SRV-tex+4UAV-tex` | 1 | 0.5% | image×1 | 未支援 | `SimpleLiquid2`（img/fluid-fx/SimpleLiquid2-cs.hlsl，33 children） |
| `1UAV-buf+2SRV-tex+1UAV-tex` | 1 | 0.5% | image×1 | 未支援 | `ColorPhysarum`（img/fx/stylize/color-physarum-cs.hlsl，41 children） |
| `1SRV-buf+3UAV-buf` | 1 | 0.5% | particle×1 | 未支援 | `VerletRibbonForce`（particles/VerletRibbonForce.hlsl，24 children） |
| `4UAV-buf` | 1 | 0.5% | point×1 | 未支援 | `DrawPointsDOF`（points/draw-sorted/sort-5-WriteSortIndices.hlsl，67 children） |
| `3UAV-buf` | 1 | 0.5% | point×1 | 未支援 | `DrawPointsDOF`（points/draw-sorted/sort-6-SetupDrawArgs.hlsl，67 children） |
| `1SRV-buf+1UAV-buf+1UAV-tex` | 1 | 0.5% | point×1 | 未支援 | `SortPoints`（points/modify/SortPointsDebug.hlsl，68 children） |
| `3SRV-buf+2UAV-buf+1SRV-tex` | 1 | 0.5% | point×1 | 未支援 | `PointsOnMesh`（points/onmesh/DistributePointsOnMesh.hlsl，25 children） |
| `1SRV-buf+5UAV-buf` | 1 | 0.5% | point×1 | 未支援 | `_BuildSpatialHashMap`（points/spatial-hash-map/spatial-hash-map.hlsl，28 children） |

## 對帳

- Assets/shaders/ 下總 .hlsl：377；含 `numthreads` 的 compute shader：190（掃描全集，非抽樣）
- 形狀種數：29；顆數總和 190（應=190，已核對一致）
- 全 925 顆 Lib .t3 中反查到 ComputeShader.Source 引用的相異 .hlsl：314 個（部分 compute shader 只被 #include 或未被任何 .t3 引用，owner 反查會落空，已在全表用「無法反查 owner」標註，不影響形狀計數本身）
- CS_MAX_CB=4 / CS_MAX_SRV=8 / CS_MAX_UAV=4（computeshaderstage_params.h）超限：1 顆（spatial-hash-map.hlsl，uav=5 > CS_MAX_UAV=4，見意外③）

# Lane point_modify — TiXL 缺口掃描候選（2026-06-14）

掃描 agent af98e31e360312463。權威＝external/tixl `/Lib/point/modify/` + `/transform/`。23 顆缺口。

## ⚠️ 承重 flag
昨夜 Lane B 想做的 **SelectPointsByRange / ScalePointsAboutCenter 不在 TiXL** → 自創（非 parity）。
不重做這 2 顆。改做 TiXL 真有的。

## Cheap 候選（真 cheap，貼合現有單 buffer in/out 模板）
| op | TiXL | kernel | 接縫 | 派工腦 |
|---|---|---|---|---|
| **ClearSomePoints** | modify/ClearSomePoints.cs + .hlsl | ✅ | 單 buffer in/out + hash(Ratio,Seed,Repeat) 標 NAN | **首選**，Sonnet 機械（純 hash kill，貼合模板） |
| PointAttributeFromNoise | modify/PointAttributeFromNoise.cs | ✅ | 條件 cheap：UseRemapCurve=false 純 noise；=true 要 Gradient curve | Sonnet，**但 grep 確認 curve gate 可關** |
| SnapToPoints | transform/SnapToPoints.cs + .hlsl | ✅ | **2-buffer**（A snap→B），像 CombineBuffers=moderate 接縫 | 次選（count-policy 契約先想） |

## Moderate（需狀態/enum，排後）
SelectPoints（spatial volume selection，需 W selection state 設計）/ ReorientLinePoints（切線 rot Y·X·Z，W weight）/
ResampleLinePoints（line order dep）/ TransformFromClipSpace（baked camera）/ MovePointsToCurveSpace（curve logic）。

## Expensive（texture/SDF/curve/mesh/sim，defer §D 子系統）
SortPoints(camera+bitonic) / SelectPointsWithSDF / SamplePointsByCameraDistance / AttributesFromImageChannels /
PointColorWithField / MapPointAttributes / LinearSamplePointAttributes / SamplePointColorAttributes /
SetAttributesWithPointFields / TransformWithImage / MoveToSDF(iterative) / CustomPointShader / IkChain(sim) /
FindClosestPointsOnMesh。

## Phase 1 決策
首發 **ClearSomePoints**（最 cheap、單 buffer、貼合模板、Sonnet）。過了再加 PointAttributeFromNoise（先確認 curve gate）。

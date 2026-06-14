# Lane generators — TiXL 缺口掃描候選（2026-06-14）

掃描 agent a6d6fe64ce14212fe 回報。權威＝external/tixl `/Lib/point/generate/`。

## ⚠️ 承重 flag
昨夜 Lane A 想做的 **BoxGridPoints / TubePoints / ConcentricCirclesPoints 不在 TiXL** generators 目錄
→ 可能是昨夜自創（非 parity）。北極星=clone TiXL 不自創 → **A 重做不照這 3 顆**，改做 TiXL 真有的。
（殘餘疑：也可能在別命名/別目錄，未窮舉；不阻塞，先做確認存在的。）

## Cheap 候選（3 顆，但架構接縫不同）

| op | TiXL .cs | GPU kernel? | ports | 架構契合 | 派工腦 |
|---|---|---|---|---|---|
| **DoyleSpiralPoints2** | point/generate/DoyleSpiralPoints2.cs + .hlsl | ✅ 有 .hlsl | 10+（純值/vec） | ✅ 貼合 RadialPoints/HexGridPoints 模板 | **Opus**（Doyle spiral 代數複雜，cheap-input≠trivial-impl，批次18 教訓） |
| CommonPointSets | point/generate/CommonPointSets.cs | ❌ CPU static lookup | 1（Set enum 0-6） | ⚠️ CPU 生成=新接縫（現有都 GPU kernel） | 先評估接縫 |
| RepetitionPoints | point/generate/RepetitionPoints.cs | ❌ CPU loop | 8（transform 迭代） | ⚠️ CPU 生成=新接縫 | 先評估接縫 |

## Phase 1 generators lane 決策
- **首選 DoyleSpiralPoints2**：唯一貼合現有 GPU 模板的 cheap 候選。Opus 移植（對 .hlsl 逐字）。
- CommonPointSets/RepetitionPoints：CPU 生成接縫先不開（偏離「純 GPU kernel」模板，需架構評估——能否用 PointCookCtx map buffer CPU 寫入）。排後，或本批做接縫探針。
- 不做 BoxGrid/Tube/ConcentricCircles（TiXL 無對應，非 parity）。

## Expensive（排除，需 buffer/texture/mesh input）
PointsOnImage / MeshVerticesToPoints / PointsOnMesh / PointTrail / BoundingBoxPoints /
RepeatAtPoints / SubdivideLinePoints（吃 Points input=transformer 非 generator）。

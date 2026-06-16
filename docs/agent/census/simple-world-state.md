# simple_world 目前能力快照
> A1 agent 產出 — 2026-06-16。只讀不動碼。證據欄位格式：`檔名:行號` 或 selftest 名。

---

## 已 port 的 op（總計 ~110 顆）

### 圖注
- seam 欄只標**最關鍵**的 seam；多 seam 複合者標主要。
- 「老式 registerTexOp」= 早於 ImageFilterOp 自登記方案、用舊 API 註冊，目前無 NodeSpec 掛進選單（存疑，見盲區）。

### A. Image-filter family（25 顆，自登記 ImageFilterOp / ImageFilterComputeOp）

| simple_world op | 對應 TiXL op | seam | 證據(檔:行/selftest) |
|---|---|---|---|
| AdjustColors | AdjustColors | image-filter | `point_ops_adjustcolors.cpp:116` |
| Blur | Blur | image-filter | `point_ops_blur.cpp:120` |
| ChannelMixer | ChannelMixer | image-filter | `point_ops_channelmixer.cpp:135` |
| ChromaB | ChromaB | image-filter | `point_ops_chromab.cpp:103` |
| ChromaKey | ChromaKey | image-filter | `point_ops_chromakey.cpp:112` |
| ChromaticDistortion | ChromaticDistortion | image-filter | `point_ops_chromaticdistortion.cpp:107` |
| ColorGrade | ColorGrade | image-filter | `point_ops_colorgrade.cpp:160` |
| ConvertColors | ConvertColors | image-filter | `point_ops_convertcolors.cpp:116` |
| Crop | Crop | image-filter + compute | `point_ops_crop.cpp:129` ImageFilterComputeOp; selftest `resident-crop` |
| DetectEdges | DetectEdges | image-filter | `point_ops_detectedges.cpp:109` |
| Displace | Displace | image-filter + multi-image | `point_ops_displace.cpp:120`; 第1顆 multi-image 消費者 |
| DistortAndShade | DistortAndShade | image-filter + multi-image | `point_ops_distortandshade.cpp:148`; 第2顆 multi-image 消費者; selftest `distortandshade` |
| Dither | Dither | image-filter | `point_ops_dither.cpp:125` |
| FastBlur | FastBlur | image-filter + multi-pass | `point_ops_fastblur.cpp:305` ImageFilterComputeOp; selftest `resident-fastblur` |
| KochKaleidoscope | KochKaleidoscope | image-filter | `point_ops_kochkaleidoscope.cpp:129` |
| MirrorRepeat | MirrorRepeat | MirrorRepeat | `point_ops_mirrorrepeat.cpp:135` |
| NormalMap | NormalMap | image-filter | `point_ops_normalmap.cpp:100` |
| Pixelate | Pixelate | image-filter | `point_ops_pixelate.cpp:113` |
| RgbTV | RgbTV | image-filter + asset-texture + mip | `point_ops_rgbtv.cpp:248` ImageFilterComputeOp; assetKey=`Lib:images/perlin-noise-rgb.png`; selftest `resident-rgbtv` |
| Sharpen | Sharpen | image-filter | `point_ops_sharpen.cpp:107` |
| StarGlowStreaks | StarGlowStreaks | image-filter | `point_ops_starglowstreaks.cpp:151` |
| Tint | Tint | image-filter | `point_ops_tint.cpp:118` |
| ToneMapping | ToneMapping | image-filter | `point_ops_tonemapping.cpp:118` |
| TransformImage | TransformImage | image-filter | `point_ops_transformimage.cpp:151` |
| VoronoiCells | VoronoiCells | image-filter | `point_ops_voronoicells.cpp:115` |

**備注（老式 registerTexOp，存疑）：**

| simple_world op | 對應 TiXL op | seam | 狀態備注 |
|---|---|---|---|
| PolarCoordinates | PolarCoordinates | image-filter | `point_ops_polarcoordinates.cpp:113` 用 `registerTexOp` 非 ImageFilterOp；**無 NodeSpec 無 kTable 入口**；cook + metal 存在，但主樹 registerBuiltinPointOps 未呼叫它；只存在 worktree 中的 register call |
| EdgeRepeat | EdgeRepeat | image-filter | `point_ops_edgerepeat.cpp:122` 同上；**無 NodeSpec 無 kTable 入口** |

---

### B. Point generator family（9 顆）

| simple_world op | 對應 TiXL op | seam | 證據 |
|---|---|---|---|
| RadialPoints | RadialPoints | value-graph | `node_registry_generators.cpp:12`; `point_ops_register_generators.cpp:24` |
| LinePoints | LinePoints | value-graph | `node_registry_generators.cpp:33`; `registerLinePointsOp()` |
| GridPoints | GridPoints | value-graph | `node_registry_generators.cpp:54`; `registerGridPointsOp()` |
| SpherePoints | SpherePoints | value-graph | `node_registry_generators.cpp:78`; `registerSpherePointsOp()` |
| HexGridPoints | HexGridPoints | value-graph | `node_registry_generators.cpp:102` |
| DoyleSpiralPoints | DoyleSpiralPoints2 | value-graph | `node_registry_generators.cpp:143` |
| RepetitionPoints | RepetitionPoints | value-graph | `node_registry_generators.cpp:190`; GPU fork of CPU op |
| CommonPointSets | CommonPointSets | value-graph | `node_registry_generators.cpp:227`; CPU-fill fork |
| BoundingBoxPoints | BoundingBoxPoints | value-graph | `node_registry_generators.cpp:241`; CPU-readback |

---

### C. Point modify/transform family（21 顆）

| simple_world op | 對應 TiXL op | seam | 證據 |
|---|---|---|---|
| TransformPoints | TransformPoints | value-graph | `node_registry_point_modify.cpp:13` |
| OrientPoints | OrientPoints | value-graph | `node_registry_point_modify.cpp:34` |
| RandomizePoints | RandomizePoints | value-graph | `node_registry_point_modify.cpp:50` |
| SetPointAttributes | SetPointAttributes | value-graph | `node_registry_point_modify.cpp:82` |
| AddNoise | AddNoise | value-graph | `node_registry_point_modify.cpp:120` |
| FilterPoints | FilterPoints | value-graph | `node_registry_point_modify.cpp:155` |
| PolarTransformPoints | PolarTransformPoints | value-graph | `node_registry_point_modify.cpp:175` |
| WrapPoints | WrapPoints | value-graph | `node_registry_point_modify.cpp:199` |
| BoundPoints | BoundPoints | value-graph | `node_registry_point_modify.cpp:216` |
| TransformSomePoints | TransformSomePoints | value-graph | `node_registry_point_modify.cpp:242` |
| WrapPointPosition | WrapPointPosition | value-graph | `node_registry_point_modify.cpp:268` |
| SnapPointsToGrid | SnapPointsToGrid | value-graph | `node_registry_point_modify.cpp:289` |
| ClearSomePoints | ClearSomePoints | value-graph | `node_registry_point_modify.cpp:319` |
| ReorientLinePoints | ReorientLinePoints | value-graph | `node_registry_point_modify.cpp:336` |
| SelectPoints | SelectPoints | value-graph | `node_registry_point_modify.cpp:416` |
| SoftTransformPoints | SoftTransformPoints | value-graph | `node_registry_point_modify.cpp:452` |
| OffsetPoints | _OffsetPoints | value-graph | `node_registry_point_modify.cpp:521` |
| PointAttributeFromNoise | PointAttributeFromNoise | value-graph | `node_registry_point_modify.cpp:531` |
| ResampleLinePoints | ResampleLinePoints | value-graph | `node_registry_point_modify.cpp:357` |
| SubdivideLinePoints | SubdivideLinePoints | value-graph | `node_registry_point_modify.cpp:380` |
| PairPointsForLines | PairPointsForLines | value-graph | `point_ops_register_point_combine.cpp:13`; `point_ops_pairpointsforlines.cpp:156` |

---

### D. Point combine family（2 顆）

| simple_world op | 對應 TiXL op | seam | 證據 |
|---|---|---|---|
| CombineBuffers | CombineBuffers | value-graph | `node_registry_point_combine.cpp:11` |
| SnapToPoints | SnapToPoints | value-graph | `node_registry_point_combine.cpp:46` |

---

### E. PickPointList（1 顆，老式 registerPointOp）

| simple_world op | 對應 TiXL op | seam | 證據 |
|---|---|---|---|
| PickPointList | PickPointList | value-graph | `point_ops_register_point_combine.cpp:14`; `point_ops_pickpointlist.cpp:110` |

---

### F. Draw / render family（4 顆）

| simple_world op | 對應 TiXL op | seam | 證據 |
|---|---|---|---|
| DrawPoints | DrawPoints | value-graph | `node_registry_draw.cpp:13`; `point_ops_register_draw.cpp:15` |
| DrawLines | DrawLines | value-graph | `node_registry_draw.cpp:19` |
| DrawBillboards | DrawBillboards | value-graph | `node_registry_draw.cpp:37` |
| RenderTarget | RenderTarget | value-graph | `node_registry_draw.cpp:51` |

---

### G. Particle family（7 顆）

| simple_world op | 對應 TiXL op | seam | 證據 |
|---|---|---|---|
| TurbulenceForce | TurbulenceForce | particle-system | `node_registry_particle.cpp:13` |
| DirectionalForce | DirectionalForce | particle-system | `node_registry_particle.cpp:28` |
| VectorFieldForce | VectorFieldForce | particle-system | `node_registry_particle.cpp:47` |
| VelocityForce | VelocityForce | particle-system | `node_registry_particle.cpp:61` |
| AxisStepForce | AxisStepForce | particle-system | `node_registry_particle.cpp:76` |
| SnapToAnglesForce | SnapToAnglesForce | particle-system | `node_registry_particle.cpp:108` |
| ParticleSystem | ParticleSystem | particle-system | `node_registry_particle.cpp:121` |

---

### H. Math / value family（~50 顆）

所有純 Float 值運算 + stateful value ops，全踩 `value-graph` seam，列舉如下：

**靜態（evalXxx 函數）：** Time / Const / Multiply / Sine / Add / Sub / Div / Clamp / Remap / Abs / Floor / Lerp / Sum / Sqrt / Pow / Modulo / Ceil / SmoothStep / Log / Cos / Round / Atan2 / Sigmoid / IsGreater / Compare / DivideVector2 / Vec2ToVec3 / EulerToAxisAngle / RemapVec2 / PadVec2Range / AddVec3 / SubVec3 / ScaleVector3 / Magnitude / DotVec3 / Vec3Distance / Vector3Components / RotateVector3 / InvertFloat / CrossVec3 / LerpVec3 / NormalizeVector3 / RoundVec3 / AddVec2 / DotVec2 / Vec2Magnitude / Vector2Components / ScaleVector2 / BlendValues

**Stateful（eval=nullptr，cookStatefulValueNodes 驅動）：** AudioReaction / Damp / DampAngle / DampVec2 / DampVec3 / DeltaSinceLastFrame / FreezeValue / Spring / SpringVec2 / SpringVec3 / Ease / EaseVec2 / EaseVec3 / HasValueIncreased / HasValueDecreased / HasValueChanged / DetectPulse / Accumulator / HasVec2Changed / HasVec3Changed / PeakLevel

**證據：** `node_registry_math.cpp`（整檔）

---

## 已建成的 seam

| seam | 狀態 | 證據 |
|---|---|---|
| `value-graph` | ✓ 確認 | `graph.h` / `resident_eval_graph.cpp` / `value_eval_ops.cpp`；Float/Vec slot、Curve 動畫、stateful-value 路徑；selftests `graph` / `compound-graph` / `resident-eval-graph` |
| `compound` | ✓ 確認 | `compound_graph.h:1`（Symbol/SymbolChild/SymbolConnection 完整實作）；`compound_save.cpp`；selftest `compound-spec` / `compound-save` / `bypass-compound` |
| `transport` | ✓ 確認 | `transport.h:1`（position/fxTime/bpm 兩時鐘）；selftest `transport` |
| `particle-system` | ✓ 確認 | `particle_system.h:25`（emit/cycle/pool GPU）；6顆 force op + ParticleSystem；selftest `particle-decay` |
| `image-filter` | ✓ 確認 | `image_filter_op_registry.h`（self-registration seam）；`point_graph.cpp:cookTexNode`；25顆 image-filter op；selftests 在 imageFilterSelfTests() sink |
| `multi-pass` | ✓ 確認 | `tex_op_cache.h:49`（`cachedScratchTex`）；`point_ops_fastblur.cpp:2`「first real consumer of the multi-pass scratch seam」；selftest `resident-fastblur` |
| `mip` | ✓ 確認 | `image_filter_op_registry.h:57`（`imageFilterMippedOutputTypes` sink）；`point_graph.cpp:395-421`（`generateMipmaps` blit in cookTexNode）；selftest `mipgen`（`point_ops_mipgen_selftest.cpp`）；**注意：RgbTV 是目前唯一 mippedOutput=true 消費者** |
| `asset-texture` | ✓ 確認 | `image_filter_op_registry.h:63`（`imageFilterAssetTextures` sink + `cachedAssetTexture` cache）；`point_graph.cpp:411`（cookTexNode 綁 `assetTexture`）；RgbTV 是唯一消費者（`assetKey=Lib:images/perlin-noise-rgb.png`） |
| `png-decode` | ✓ 確認 | `platform/image_decode.h`（macOS-native ImageIO PNG→RGBA8）；`image_filter_op_registry.h:82`（fn-ptr seam `AssetTextureDecoder`）；selftest `imagedecode` |
| `multi-image` | ✓ 確認 | `point_graph.h:137`（`inputTextures[kMaxTexInputs]={nullptr×4}`）；`point_graph.cpp:402`（gather loop `inputTextures[0/1]`）；2 顆消費者：Displace（第1）+ DistortAndShade（第2）；selftest `distortandshade`（resident_distortandshade_selftest.cpp:3 明確說明） |

---

## brief 未列、但已有的 seam

| seam 短名 | 描述 | 證據 |
|---|---|---|
| `stateful-value` | 跨幀狀態值 op（Damp/Spring/Ease/Accumulator 等），per-instance memory，`cookStatefulValueNodes` 驅動 | `stateful_value_ops.h`；`node_registry_math.cpp:42`（Damp 的 evaluate=nullptr 說明） |
| `multi-input-float` | MultiInput Float port（單一 pin 接多條線，Sum/BlendValues/PickPointList 用）| `node_registry_math.cpp:338`（Sum spec，`multiInput=true`）；`point_ops_pickpointlist.cpp` |
| `compute-leaf` | Compute shader（.metal compute kernel，ShaderWrite output，Crop/FastBlur/RgbTV 用）| `image_filter_op_registry.h:107`（ImageFilterComputeOp）；`imageFilterComputeTypes()` sink |
| `audio-reaction` | 即時音訊分析（FFT/peak/attack），AudioReaction 節點讀 spectrum，platform 層 audio capture | `audio_analyzer.cpp`；`audio_reaction.cpp`；selftest `audio-reaction` |
| `curve-anim` | Curve animator：Curve key-frame 動畫，對 Float slot 做 bars→值求值 | `curve_animator.h`；`curve_animator_selftest.cpp`；selftest `curve-animator` |
| `resident-eval` | 常駐增量 eval graph（frame-stable，resident cook 路徑）| `resident_eval_graph.h`；selftest `resident-eval-graph` |
| `point-line` | NaN-Scale Separator（DrawLines 折線斷開 + ResampleLinePoints/SubdivideLinePoints/ReorientLinePoints 線段語義）| `node_registry_draw.cpp:24`（DrawLines fork note）；`resamplelinepoints_params.h` |
| `draw-cmd` | Points→Command→RenderTarget 渲染鏈（DrawPoints/DrawLines/DrawBillboards）| `point_ops_register_draw.cpp:14`；`node_registry_draw.cpp` |

---

## 盲區 / 存疑

1. **PolarCoordinates / EdgeRepeat 孤兒狀態**：這兩顆 op 有完整 cook + metal shader，但使用早於 ImageFilterOp 自登記方案的 `registerTexOp` 直接呼叫，且**主樹 `registerBuiltinPointOps` 未呼叫** `registerPolarCoordOp()` / `registerEdgeRepeatOp()`（只在 worktree 的 `point_ops_register_image_filter.cpp` 出現）。它們同時**沒有 NodeSpec**（不在 `imageFilterSpecSink()`）＆**沒有 kTable 入口**。實際執行時 cook 可能是可用的（`registerTexOp` 有掛），但 canvas 選單找不到它們，也無 selftest 串進 `--selftest-list`。**存疑：是真 orphan 還是另有 caller 我沒找到？建議 synthesis 或 Phase C 前確認。**

2. **mip seam 只有 RgbTV 一顆消費者**：Cut 53 建的 mip seam infra 目前只有 RgbTV 掛 `mippedOutput=true`（`imageFilterMippedOutputTypes`）。其他可能需要 mip 的 op（如 LightRaysFx/FakeLight — 未 port）尚未測試路徑是否正確。

3. **multi-image unwired-input fallback 未統一**：DistortAndShade 的 ImageB 未接線時 simple_world fork 是 sample ImageA self-warp（而非 TiXL 的黑色 null SRV），同 Displace。task_3fc122a2 在追此 lane-wide convention 修。synthesis 應標注此 fork 仍 open。

4. **source-op seam 未建**：LoadImage 等 source-type op（從無到有產生 Texture2D）需要 `source-op` seam（brief 的已知未建清單），目前 simple_world 零 image-loading source op（只有 asset-texture 路徑從 PNG 綁 t1，非 source op）。

5. **op 總數浮動**：Math family 的 stateful ops 數量依 `node_registry_math.cpp` 實際行數統計，本次計約 21 顆 stateful + ~30 顆靜態，合計 math family ~51 顆。全部 family 加總約 110-115 顆（含 PolarCoordinates / EdgeRepeat 孤兒 2 顆）。精確數字要等 synthesis 做 join。

6. **VectorFieldForce fork 場**：VectorFieldForce 的 ShaderGraphNode field 輸入目前 baked 成常數 (1,1,1)（named fork-VFF）— 這表示 simple_world 的粒子 vector field 行為與 TiXL 有明確分岔，Phase C 排序時需知。

---

## 摘要（給 orchestrator）

- **已 port op 總數**：約 112 顆（25 image-filter + 9 generator + 21 modify + 2 combine + 1 pickpointlist + 4 draw + 7 particle + ~51 math）；另有 PolarCoordinates / EdgeRepeat 2 顆孤兒（cook 存在但選單不可達）。
- **已建成 seam 逐條**：value-graph ✓ / compound ✓ / transport ✓ / particle-system ✓ / image-filter ✓ / multi-pass ✓ / mip ✓ / asset-texture ✓ / png-decode ✓ / multi-image ✓
- **宣稱建好但找不到的**：無。10 條 brief 列出的 seam 全部在 codebase 找到明確證據。
- **brief 未列但已有的 seam**：stateful-value / multi-input-float / compute-leaf / audio-reaction / curve-anim / resident-eval / point-line / draw-cmd（共 8 條）。
- **主要盲區**：PolarCoordinates / EdgeRepeat 孤兒狀態（無 NodeSpec、無 kTable 入口、主樹未呼叫 register）；multi-image unwired fallback fork 仍 open（task_3fc122a2）。

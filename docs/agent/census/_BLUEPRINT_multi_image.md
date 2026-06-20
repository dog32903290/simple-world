# BLUEPRINT — multi-image seam（補縫計劃階段 3，census ~16-17）

> Explore scout a779d8ea 盤點（2026-06-21），read-only。SEAM_COMPLETION_PLAN §2 階段 3。
> ★seam 本身已建（_multiImageFxSetup/Static 2-image，Displace/DistortAndShade 證）+ R-2 production cookTexNode 已活。逐顆 Cut55 .t3 backward-trace。

## 0. 一句話結論
階段 3 最大未開塊（17 op 盤）。**R2-ready 乾淨 4 顆**（Blend/BlendWithMask/Combine3Images/CombineMaterialChannels2，全 **clean 1:1 cbuffer 無 Cut55 trap**）。第一批 **Blend → BlendWithMask**（2-image setup 已建）→ Combine3Images（3-image _trippleImageFxSetup 需確認/建）。DistortAndShade 有 Cut55 trap（Vector decomp）defer。其餘卡 feedback/curve/time state 延後。

## 1. seam 狀態（決定第一批選法）
- **2-image setup `_multiImageFxSetup`/`_multiImageFxSetupStatic`**：sw 已建（lane Displace/DistortAndShade 消費證），R-2 production cookTexNode 已活。→ Blend/BlendWithMask 插即可。
- **3-image setup `_trippleImageFxSetup`**：sw **可能未港**（Combine3Images/CombineMaterialChannels2 用）→ 第一批前確認，未港則 Combine3Images 需先建 3-image gather（次批）。
- ★第一批先證 **2-image** 消費路徑（Blend），再擴 3-image。

## 2. R2-ready 候選（clean 1:1 cbuffer，無 Cut55 trap）
| # | op | 路徑 | input | routing | 優先 |
|---|----|----|------|---------|------|
| 1 | **Blend** | `image/use/Blend.t3`+.cs | ImageA/B,ColorA/B,BlendMode,ScaleMode,AlphaMode | ✅ CLEAN 1:1（Vector4Components→FloatsToBuffer 直填,IntToFloat for modes,**無數學節點**）| ★P1 最乾淨 |
| 2 | **BlendWithMask** | `image/use/BlendWithMask.t3`+.cs | ImageA/B,Mask,ColorA/B,Resolution | ✅ CLEAN 1:1（Vector4Components→cbuffer 無 math junction）| ★P1 |
| 3 | **Combine3Images** | `image/use/Combine3Images.t3`+.cs | ImageA/B/C,ColorA/B/C,SelectChannel_R/G/B,SelectAlpha | ✅ CLEAN 1:1（3×Vector4Components→4×IntToFloat→FloatsToBuffer）| P2（需 _trippleImageFxSetup）|
| 3b | CombineMaterialChannels2 | `image/use/CombineMaterialChannels2.t3` | 同 Combine3Images（PBR channel pack）| ✅ CLEAN（同 img-combine-3.hlsl）| P2（Combine3Images 後 reuse）|

shader：Blend=`Lib:shaders/img/fx/Blend.hlsl`（169 行 simple math）/BlendWithMask=`img/fx/BlendWithMask.hlsl`/Combine3Images=`img/use/img-combine-3.hlsl`（112 行 channel routing）。

## 3. 延後（疊副 seam / state）
- **DistortAndShade**（`fx/distort/`，2-image+shade）⚠ **Cut55 trap**：Vector2Components(Center)+Vector4Components(ShadeColor) 分解進 FloatsToBuffer（mixed junction）→ 逐 .t3 trace 才安全。P3。
- **OpticalFlow**（`analyze/`，2-image compare→motion vector）：analysis 非 blend，不同消費模型。P4。
- **KeepPreviousFrame**（feedback 跨幀 prev-frame buffer）=feedback seam（階段 3 另塊 R3）。
- **CombineMaterialChannels**（curve RemapRoughness）=curve-port seam 依賴。
- **RgbTV**（已港，single+time state，非 multi-image）/AsciiRender（stylize）/MakeTileableImage（transform）/CustomPixelShader（meta）/TriangleGridTransition（obsolete）排除。

## 4. 第一批建議（證 seam 消費路徑活）
> ✅ **Blend + BlendWithMask 落地 commit `7969339`**（refuter 8 攻擊全清 MERGE-SAFE×2）。★承重發現：**3-image gather 無需 seam 擴充**——cookTexNode inputTextures[] gather（kMaxTexInputs=4）按 **spec port order** 非 connection order（point_graph.cpp:524-538，每 Texture2D port occupies next slot wired-or-not）→ t0/t1/t2 wire-order-independent deterministic。.t3 routing clean 1:1（MultiInputSlot file-order 無 sort，純 splitter/cast 無算術 junction，DirectionalBlur trap absent）。caveat（非 blocker）：aspect-fit path 無 golden exercise（標準 sibling coverage）。**→ Combine3Images/CombineMaterialChannels2（3-image channel pack）gather 已鋪路,採中**。
1. **Blend**（最乾淨 2-image，零 state，clean 1:1，_multiImageFxSetupStatic 已建）：兩 image input gather → blend mode math → RGBA out。golden=雙 image（已知 pixel）→ Blend(mode=Normal/Add) → readback 對 closed-form blend 公式（Cut62-63 d=0 飽和 plateau 非退化）。★production resident golden（cookTexNode 真走 production，image filter 島已在 resident）。
2. **BlendWithMask**（2img+mask，reuse Blend routing pattern）：mask-driven lerp。
3. （次批）Combine3Images：先確認/建 _trippleImageFxSetup（3-image gather），channel pack。

## 5. 工法（同 image filter 採葉子）
- image filter 自登記（imageFilterSpecSink，conflict-free）+ cookTexNode R-2 production 已活。
- ★每顆 .t3 backward-trace FloatsToBuffer connection-order（Cut55 血證：DirectionalBlur 因 mis-route 丟棄）。Blend/BlendWithMask/Combine3Images 已驗 clean 1:1，但開工仍逐顆複核。
- golden：closed-form pixel-readback（雙/三 image 已知值 → blend math 手算）+ ★production resident（cookTexNode）+ injectBug RED。
- multi-input image gather：確認 sw _multiImageFxSetup 的 input texture gather（ImageA/B/Mask）在 cook 路徑正確綁 t0/t1/t2。

## Critical Files
- external/tixl image/use/Blend.cs + Blend.t3 + Lib:shaders/img/fx/Blend.hlsl（第一葉 authority）
- external/tixl image/fx/_/_multiImageFxSetupStatic.cs（2-image setup 機制）+ _trippleImageFxSetup.cs（3-image，P2 前確認 sw 港否）
- sw image filter 自登記 sink（imageFilterSpecSink）+ cookTexNode（R-2 production）+ 已港 multi-image 消費者（Displace/DistortAndShade routing precedent，含 Cut55 .t3 trace 教訓）

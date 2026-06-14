# Lane image_filter — TiXL 缺口掃描候選（2026-06-14）

掃描 agent ac404423ce8f22445。權威＝external/tixl `/Lib/image/color/` + `/fx/`。現有 5 顆：Blur/Displace/Tint/ChromaticAbberation/AdjustColors。

## ⚠️ 風險：昨夜 Lane D 完全沒跑起來
image_filter 是「半成熟、風險中」家族。**先驗 1 顆過了再加**，不一口氣堆 10+。
現有 Tint/AdjustColors 是 Texture2D→Texture2D per-pixel 模板，新 cheap op 照它。

## Cheap 第一梯隊（純 per-pixel，單 texture in + 值參數，貼合 Tint/AdjustColors）
| op | TiXL color/ | 摘要 | 派工腦 |
|---|---|---|---|
| **ChannelMixer** | ChannelMixer.cs | out = MultiplyR*r + MultiplyG*g + ... + Add（線性矩陣） | **首選**，Sonnet（最純 per-pixel，無 enum 分支） |
| **HSE** | HSE.cs | Hue/Saturation/Exposure，RGB→HSV→改→RGB | Sonnet |
| ToneMapping | ToneMapping.cs | enum 多模式 HDR→LDR 曲線（Aces/Reinhard/Filmic/AgX） | Sonnet（enum 分支，逐模式對 .hlsl） |
| ColorGrade | ColorGrade.cs | Gain/Gamma/Lift + 放射 vignette | Sonnet |
| KeyColor | KeyColor.cs | HSV 距離綠幕 key | Sonnet |
| ConvertColors | ConvertColors.cs | RGB↔OK-Lab/LCh 矩陣 | Sonnet |
| Pixelate | fx/stylize/Pixelate.cs | 坐標 quantize 平均 | Sonnet |
| Steps | fx/stylize/Steps.cs | 亮度量化 + **Gradient LUT**（curve 依賴，標 moderate） | 後 |
| Dither | fx/stylize/Dither.cs | Floyd-Steinberg（相鄰像素，近 per-pixel） | 後 |
| ConvertFormat | ConvertFormat.cs | 純格式轉換（可能太瑣碎，無視覺差） | skip? |

## Moderate（sampling kernel，排後）
Sharpen(Laplacian 3×3) / DetectEdges(Sobel) / DirectionalBlur / PolarCoordinates / BubbleZoom(Gradient) /
HoneyCombTiles / MosiacTiling。

## Expensive（多 texture/迭代/時域故障，defer）
Bloom(mip 金字塔) / ColorPhysarum(sim) / RgbTV / GlitchDisplace / SortPixelGlitch / ScreenCloseUp(DoF) /
AsciiRender(字體管線) / ColorGradeDepth(depth buffer) / DistortAndShade(2-tex) / TimeDisplace(2-tex) / LightRaysFx(2-tex)。

## Phase 1 決策
首發 **ChannelMixer**（最純 per-pixel 線性，貼合 Tint）。**先單顆驗綠 + 截圖**，過了再加 HSE/ToneMapping/ColorGrade。
昨夜沒跑起來→這 lane 保守，1 顆過了才擴。

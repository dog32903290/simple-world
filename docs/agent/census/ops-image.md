# Census: image/ (127 ops)

掃描方法：read-only sweep — grep .cs slot 宣告 + .t3 SymbolId 查子 op GUID（逐桶確認代表性樣本，不逐行深讀）。
此類別全部為 compound 模式：.cs 只有 slot 宣告，無 GPU 邏輯，實際執行由 .t3 內子 op 組合決定。

---

## image/analyze (7 ops)

| op | 一句功能 | 主要 seam | 狀態 | 風險 | 備註 |
|----|---------|----------|------|------|------|
| CompareImages | 比較兩張貼圖差異，疊加 LinearGradient mask | gradient-widget | BLOCKED:gradient-widget | R2 | 子 op：LinearGradient×2(gradient-widget dep) + ImageLevels + BlendWithMask + TransformImage + ExecuteTextureUpdate。輸出 Texture2D |
| DetectMotion | 偵測畫面動態區域 | Layer2d+Execute | BLOCKED:Layer2d+Execute | R3 | 子 op：Layer2d×2 + RenderTarget×2 + _multiImageFxSetupStatic(內建)。時間相干 |
| GetImageBrightness | 讀取貼圖亮度回 CPU | NEW-SEAM:compute-readback | BLOCKED:compute-readback | R3 | 子 op：ComputeShaderStage×6 + Execute×2。需要 UAV→staging buffer CPU readback |
| ImageLevels | 貼圖 histogram levels/curve 調整 | image-filter | READY-LEAF | R1 | 子 op：_ImageFxShaderSetup2（動態 hlsl ref）。已確認同 _ImageFxShaderSetupStatic 模式，只是 runtime shader 載入 |
| OpticalFlow | 計算兩張貼圖的光流場 | multi-image | BLOCKED:multi-image | R3 | 子 op：_multiImageFxSetupStatic + PickTexture + BoolToInt + IntToFloat。時間相干，需雙輸入 |
| RemoveStaticBackground | 從視訊流去除靜態背景 | NEW-SEAM:compute-readback | BLOCKED:compute-readback | R3 | 子 op：ComputeShaderStage×9 + FloatsToBuffer×6 + RenderTarget×4 + Layer2d×2 + Execute×3。複雜時間累積 |
| WaveForm | 將貼圖內容渲染成波形圖 | Layer2d+Execute | BLOCKED:Layer2d+Execute | R3 | 子 op：DrawLines×2 + SamplerState + SrvFromTexture2d + Execute×2 + Layer2d×4。需要幾何線段繪製 |

---

## image/color (11 ops)

| op | 一句功能 | 主要 seam | 狀態 | 風險 | 備註 |
|----|---------|----------|------|------|------|
| AdjustColors | HSV/曝光/色調多參數色彩調整 | image-filter | READY-LEAF | R1 | 子 op：_ImageFxShaderSetupStatic + AdjustColors.hlsl。已審計此類 |
| ChannelMixer | RGB 通道混合矩陣 | image-filter | READY-LEAF | R1 | 子 op：_ImageFxShaderSetupStatic |
| ColorGrade | 色彩分級（暗/中/亮） | image-filter | READY-LEAF | R1 | 子 op：_ImageFxShaderSetupStatic |
| ColorGradeDepth | 帶深度 buffer 的色彩分級 | gradient-widget | BLOCKED:gradient-widget | R2 | 子 op：GradientsToTexture + Execute×2 + FloatsToBuffer×2 + RenderTarget×3。需 gradient-widget；次要：Layer2d-like RenderTarget pipeline |
| ConvertColors | 色彩空間轉換（線性/sRGB/HSV） | image-filter | READY-LEAF | R1 | 子 op：_ImageFxShaderSetupStatic |
| ConvertFormat | 貼圖格式轉換（HDR/RGBA/etc） | NEW-SEAM:texture-format-convert | BLOCKED:texture-format-convert | R2 | 子 op：ComputeShaderStage×5 + Execute×2 + RenderTarget×2。需要不同 DXGI format 輸出 RenderTarget |
| HSE | Hue/Saturation/Exposure + 可選第二貼圖 | multi-image | BLOCKED:multi-image | R2 | 子 op：_multiImageFxSetupStatic (GUID cc34a183)。R2 因 FloatsToBuffer routing trap |
| KeyColor | 色彩鍵去背（chroma key） | image-filter | READY-LEAF | R1 | 子 op：_ImageFxShaderSetupStatic |
| RemapColor | 以第二張貼圖重映射顏色 | multi-image | BLOCKED:multi-image | R2 | 子 op：_multiImageFxSetupStatic + TransformImage + AddInts。R2 routing |
| Tint | 單色著色疊加 | image-filter | READY-LEAF | R1 | 子 op：_ImageFxShaderSetup2（動態載入 Tint.hlsl）；與 AdjustColors 同模式 |
| ToneMapping | HDR tone mapping（ACES/Reinhard 等） | image-filter | READY-LEAF | R1 | 子 op：_ImageFxShaderSetupStatic |

---

## image/fx/_ (8 ops，內部工具/primitive 層)

| op | 一句功能 | 主要 seam | 狀態 | 風險 | 備註 |
|----|---------|----------|------|------|------|
| BlurWithMask | 帶 mask 的模糊（內部工具） | multi-image | BLOCKED:multi-image | R2 | 子 op：_multiImageFxSetupStatic×3。R2 routing |
| BuildAsciiFontSorting | ASCII 字元排序貼圖（font atlas 工具） | source-op | BLOCKED:source-op | R2 | 子 op：TimeToString + FloatToString + CompareImages + _Time_old + PickStringFromList + FloatToInt + LoadImage。需要 source-op (LoadImage) + CompareImages seam 聯集 |
| _AdjustFeedbackImage | 回饋影格色彩調整（内部） | feedback | BLOCKED:feedback | R2 | 子 op：_ImageFxShaderSetupStatic + _AdjustFeedbackImage 參照。TiXL 回饋 ping-pong |
| _ExecuteBloomPasses | Bloom 多趟執行（內部 executor） | multi-pass | READY-LEAF | R2 | 子 op：_ExecuteBloomPasses self-ref + Execute。simple_world 已有 multi-pass seam（FastBlur 驗證過） |
| _ExecuteFastBlurPasses | FastBlur 多趟（內部 executor）| multi-pass | READY-LEAF | R2 | 已 port（Cut 54）。multi-pass seam 消費者 |
| _SpecularPrefilter | 3D 環境貼圖 specular prefilter | NEW-SEAM:cubemap-prefilter | BLOCKED:cubemap-prefilter | R3 | 子 op 需 CubeMap 輸入 + GeometryShader。完全超出 2D image pipeline |
| _multiImageFxSetup | 雙輸入 image fx 動態版（含 math 路由）| multi-image | BLOCKED:multi-image | R2 | Cut 55 routing trap 源頭：FloatsToBuffer×2 + Execute×2 + RenderTarget×2。所有消費者均需 STEP-0 backward-trace |
| _multiImageFxSetupStatic | 雙輸入 image fx 靜態版 | multi-image | BLOCKED:multi-image | R2 | 同上但 shader 靜態綁定。已被 Displace/DistortAndShade/RgbTV 消費（Cut 58-59 已建 seam） |
| _trippleImageFxSetup | 三輸入 image fx | multi-image | BLOCKED:multi-image | R2 | FloatsToBuffer×2 + Execute×2 + RenderTarget×3。第三張 t2 尚無消費者被 port |

---

## image/fx/_obsolete (1 op)

| op | 一句功能 | 主要 seam | 狀態 | 風險 | 備註 |
|----|---------|----------|------|------|------|
| TriangleGridTransition | 舊版三角格轉場（廢棄） | multi-image | BLOCKED:multi-image | R2 | 廢棄。子 op：_multiImageFxSetupStatic。不建議 port |

---

## image/fx/blur (5 ops)

| op | 一句功能 | 主要 seam | 狀態 | 風險 | 備註 |
|----|---------|----------|------|------|------|
| Bloom | 泛光效果 | multi-pass | READY-LEAF | R3 | 子 op：_ExecuteBloomPasses + Execute。消費 multi-pass；視覺判斷 brittle |
| Blur | 高斯模糊（單趟） | image-filter | READY-LEAF | R1 | 子 op：_ImageFxShaderSetupStatic + Blur.hlsl。已 port（Cut 51）|
| DirectionalBlur | 方向性模糊 | multi-image | BLOCKED:multi-image | R2 | **Cut 55 routing trap**：_multiImageFxSetupStatic compound，FloatsToBuffer 路由非 1:1（Size←op.Size/Samples×RefineSizeFactor×0.03，NumberOfSamples←RefinementSamples）。丟棄並具名。必須 STEP-0 backward-trace .t3 再港 |
| FastBlur | 高品質多趟模糊 | multi-pass | READY-LEAF | R1 | 子 op：_ExecuteFastBlurPasses + Execute。已 port（Cut 54）|
| Sharpen | 銳化 | image-filter | READY-LEAF | R1 | 子 op：_ImageFxShaderSetupStatic |

---

## image/fx/distort (9 ops)

| op | 一句功能 | 主要 seam | 狀態 | 風險 | 備註 |
|----|---------|----------|------|------|------|
| BubbleZoom | 泡泡放大鏡/魚眼 | gradient-widget | BLOCKED:gradient-widget | R2 | 子 op：_multiImageFxSetupStatic + GradientsToTexture。gradient-widget 決定邊緣 falloff；R2 routing |
| ChromaticDistortion | 色差畸變 | image-filter | READY-LEAF | R1 | 子 op：_ImageFxShaderSetup2（動態）。已在 Cut55 audit task_258d9510 中列為待查 |
| Displace | 以第二張圖 warp 第一張（位移） | multi-image | READY-LEAF | R1 | 已 port（Cut 59 DistortAndShade 同 seam）。multi-image seam 第 1 消費者 |
| DistortAndShade | warp + shade 雙輸入（2-input） | multi-image | READY-LEAF | R1 | 已 port（Cut 59）。multi-image seam 第 2 消費者 |
| EdgeRepeat | 邊緣重複拉伸 | image-filter | READY-LEAF | R1 | 子 op：_ImageFxShaderSetup2（動態）|
| FieldToImage | 將 Field 型別轉為貼圖 | Layer2d+Execute | BLOCKED:Layer2d+Execute | R3 | 子 op：ResolutionConstBuffer + IntsToBuffer + Execute + RasterizerState + GradientsToTexture。需 Layer2d-style pipeline + gradient-widget |
| KochKaleidoskope | Koch 碎形萬花筒 | image-filter | READY-LEAF | R1 | 子 op：_ImageFxShaderSetup2（動態）|
| PolarCoordinates | 極座標轉換 | image-filter | READY-LEAF | R1 | 子 op：_ImageFxShaderSetupStatic |
| TimeDisplace | 以時間為索引的位移（temporal warp） | multi-image | BLOCKED:multi-image | R2 | 子 op：_multiImageFxSetupStatic + AddInts + BoolToInt。次要：temporal-random（時間參照） |

---

## image/fx/feedback (7 ops)

| op | 一句功能 | 主要 seam | 狀態 | 風險 | 備註 |
|----|---------|----------|------|------|------|
| AdvancedFeedback | 高級回饋效果（warp+blend 累積） | feedback | BLOCKED:feedback | R3 | 子 op：Layer2d×2 + Execute×6 + RenderTarget×2 + _AdjustFeedbackImage。最複雜回饋 |
| AdvancedFeedback2 | 高級回饋 v2 | feedback | BLOCKED:feedback | R3 | 子 op：Layer2d×2 + Execute×4 + FloatsToBuffer×2 + RenderTarget×5 |
| AfterGlow | 餘輝/殘影效果 | feedback | BLOCKED:feedback | R3 | 子 op：Layer2d×7 + Execute×4 + RenderTarget×7。大量 multi-target 渲染 |
| AfterGlow2 | 餘輝 v2（更高品質） | feedback | BLOCKED:feedback | R3 | 子 op：Layer2d×6 + Execute×4 + RenderTarget×2 |
| FluidFeedback | 流體模擬回饋 | feedback | BLOCKED:feedback | R3 | 子 op：Layer2d×4 + Execute×2 + RenderTarget×2 |
| SimpleLiquid | 簡單液體模擬 | feedback | BLOCKED:feedback | R3 | 子 op：ComputeShaderStage×6 + FloatsToBuffer×2 + Execute×3 + RenderTarget×2 |
| SimpleLiquid2 | 簡單液體模擬 v2 | feedback | BLOCKED:feedback | R3 | 子 op：ComputeShaderStage×6 + FloatsToBuffer×2 + Execute×4 |

---

## image/fx/glitch (4 ops)

| op | 一句功能 | 主要 seam | 狀態 | 風險 | 備註 |
|----|---------|----------|------|------|------|
| GlitchDisplace | Glitch 位移（pixel-sort 風格） | Layer2d+Execute | BLOCKED:Layer2d+Execute | R3 | 子 op：PixelShader + Execute×4 + FloatsToBuffer×2 + SamplerState + RasterizerState + RenderTarget×2 + MultiplyInt。使用直接 DX11 pixel shader pipeline（非 _ImageFxSetup wrapper） |
| RgbTV | CRT TV 效果（scanline/vignette/RGB shift） | asset-texture | READY-LEAF | R2 | 已 port（Cut 58）。消費 asset-texture seam（perlin noise t1）+ multi-image seam（RgbTV shader）。perlin 改進 fork 具名（task_c6a885db） |
| SortPixelGlitch | 像素排序 glitch | NEW-SEAM:compute-readback | BLOCKED:compute-readback | R3 | 子 op：ComputeShaderStage×3 + FloatsToBuffer×4 + Execute×4 + BlendState + OutputMergerStage + RenderTarget×3。需要 GPU↔CPU readback 做 sort ordering |
| SubdivisionStretch | 以色帶驅動的分塊拉伸 | gradient-widget | BLOCKED:gradient-widget | R2 | 子 op：GradientsToTexture + _multiImageFxSetupStatic + BoolToInt + LoadImage（default fallback）。gradient-widget 決定色帶；routing R2 |

---

## image/fx/stylize (14 ops)

| op | 一句功能 | 主要 seam | 狀態 | 風險 | 備註 |
|----|---------|----------|------|------|------|
| AsciiRender | ASCII 字元藝術渲染 | Layer2d+Execute | BLOCKED:Layer2d+Execute | R3 | 子 op：VertexShader + InputAssemblerStage + FloatsToBuffer×2 + Int2Components + RenderTarget×3 + Execute×2。需要幾何頂點 pipeline |
| ChromaticAbberation | 色差（紫邊/邊緣分色） | image-filter | READY-LEAF | R1 | 子 op：_ImageFxShaderSetupStatic |
| ColorPhysarum | Physarum slime mold 模擬（彩色） | Layer2d+Execute | BLOCKED:Layer2d+Execute | R3 | 子 op：ComputeShaderStage×6 + FloatsToBuffer×4 + Layer2d×2 + Execute×2 + RenderTarget×3。compute + Layer2d 混合 |
| DetectEdges | 邊緣檢測（Sobel/Canny） | image-filter | READY-LEAF | R1 | 子 op：_ImageFxShaderSetupStatic。已在 task_258d9510 audit 清單 |
| Dither | 抖動（Floyd-Steinberg/Bayer） | image-filter | READY-LEAF | R1 | 子 op：_ImageFxShaderSetupStatic |
| FakeLight | 假光源/法線貼圖光照 | multi-image | BLOCKED:multi-image | R2 | 子 op：_multiImageFxSetupStatic。需要 asset-texture（法線圖 t1）+ routing trace |
| Glow | 光暈效果（多 Layer2d pass） | Layer2d+Execute | BLOCKED:Layer2d+Execute | R3 | 子 op：Layer2d×10 + Execute×2 + RenderTarget×2。最多 Layer2d 使用顆（10 次） |
| HoneyCombTiles | 蜂巢格磚紋 | multi-image | BLOCKED:multi-image | R2 | 子 op：_multiImageFxSetupStatic×2。雙輸入 warp |
| LightRaysFx | 放射光線/神聖光效 | Layer2d+Execute | BLOCKED:Layer2d+Execute | R3 | 子 op：VertexShader + InputAssemblerStage + PixelShader + Draw + FloatsToBuffer×2 + Execute×7 + RenderTarget×2。幾何射線 + multi-pass |
| MosiacTiling | 馬賽克磁磚效果 | multi-image | BLOCKED:multi-image | R2 | 子 op：_multiImageFxSetupStatic。雙輸入（ImageA + tile source） |
| Pixelate | 像素化（馬賽克格） | multi-image | BLOCKED:multi-image | R2 | 子 op：_multiImageFxSetupStatic。R2 routing |
| ScreenCloseUp | 螢幕放大鏡/特寫 | Layer2d+Execute | BLOCKED:Layer2d+Execute | R3 | 子 op：Execute×4 + RenderTarget×2。Layer2d 類 pipeline |
| StarGlowStreaks | 星芒/光芒條紋 | image-filter | READY-LEAF | R1 | 子 op：_ImageFxShaderSetupStatic |
| Steps | 色階梯度（以色帶驅動） | gradient-widget | BLOCKED:gradient-widget | R2 | 子 op：GradientsToTexture + Execute×2 + FloatsToBuffer×2 + RenderTarget×3。需 gradient-widget |
| VoronoiCells | Voronoi 細胞紋理 | image-filter | READY-LEAF | R1 | 子 op：_ImageFxShaderSetup2。已在 task_258d9510 audit 清單 |

---

## image/generate/_obsolete (2 ops)

| op | 一句功能 | 主要 seam | 狀態 | 風險 | 備註 |
|----|---------|----------|------|------|------|
| _BlobOld | 舊版 Blob（廢棄） | image-filter | READY-LEAF | R1 | 子 op：_ImageFxShaderSetupStatic。廢棄，不 port |
| _FractalNoiseOld | 舊版碎形噪聲（廢棄） | image-filter | READY-LEAF | R1 | 子 op：_ImageFxShaderSetupStatic。廢棄，不 port |

---

## image/generate/basic (9 ops)

| op | 一句功能 | 主要 seam | 狀態 | 風險 | 備註 |
|----|---------|----------|------|------|------|
| Blob | Blob/有機形狀生成 | image-filter | READY-LEAF | R1 | 子 op：_ImageFxShaderSetupStatic。source-like（無 texture 輸入），但走 image-filter |
| BoxGradient | 矩形方塊漸層 | gradient-widget | BLOCKED:gradient-widget | R2 | 子 op：GradientsToTexture + _multiImageFxSetupStatic。gradient 決定色帶 |
| CheckerBoard | 棋盤格 | image-filter | READY-LEAF | R1 | 子 op：_ImageFxShaderSetupStatic |
| LinearGradient | 線性漸層（色帶） | gradient-widget | BLOCKED:gradient-widget | R2 | 子 op：_ImageFxShaderSetup2 + GradientsToTexture（2c3d2c26 = LinearGradient 本身 GUID）。gradient-widget 決定色帶樣貌 |
| NGon | 正多邊形 SDF 生成 | image-filter | READY-LEAF | R1 | 子 op：_ImageFxShaderSetupStatic |
| NGonGradient | 正多邊形漸層 | gradient-widget | BLOCKED:gradient-widget | R2 | 子 op：GradientsToTexture + _multiImageFxSetupStatic |
| RadialGradient | 放射漸層 | gradient-widget | BLOCKED:gradient-widget | R2 | 子 op：GradientsToTexture + _multiImageFxSetupStatic |
| RenderTarget | 建立空白 RenderTarget 貼圖 | image-filter | READY-LEAF | R1 | 子 op：Texture2d（type op）+ RenderTarget native。本身就是 RenderTarget wrapper |
| RoundedRect | 圓角矩形 SDF | image-filter | READY-LEAF | R1 | 子 op：_ImageFxShaderSetupStatic |

---

## image/generate/fractal (1 op)

| op | 一句功能 | 主要 seam | 狀態 | 風險 | 備註 |
|----|---------|----------|------|------|------|
| MandelbrotFractal | Mandelbrot 碎形 | gradient-widget | BLOCKED:gradient-widget | R2 | 子 op：_ImageFxShaderSetupStatic + GradientsToTexture。gradient-widget 決定色彩映射 |

---

## image/generate/load (4 ops)

| op | 一句功能 | 主要 seam | 狀態 | 風險 | 備註 |
|----|---------|----------|------|------|------|
| ImageSequenceClip | 圖像序列播放（依時間索引） | source-op | BLOCKED:source-op | R2 | 子 op：TimeToString + FloatToString + CompareImages + _Time_old + PickStringFromList + FloatToInt + LoadImage + FilesInFolder。需 source-op + transport + string utils |
| LoadImage | 從磁碟載入單張圖像 | source-op | BLOCKED:source-op | R1 | .cs 有 native 邏輯（ResourceManager.CreateTextureResource）但仍需 source-op seam：路徑解析 + async load + SRV 建立。127 行 native C#，已有 png-decode + asset-texture，差 source-op path-watcher |
| LoadImageFromUrl | 從 URL 載入圖像 | NEW-SEAM:network-fetch | BLOCKED:network-fetch | R3 | 需要 HTTP fetch + async decode。超出目前 platform 能力 |
| LoadSvgAsTexture2D | 載入 SVG 為貼圖 | NEW-SEAM:svg-rasterize | BLOCKED:svg-rasterize | R3 | 需要 SVG rasterizer。超出目前 platform 能力 |

---

## image/generate/misc (3 ops)

| op | 一句功能 | 主要 seam | 狀態 | 風險 | 備註 |
|----|---------|----------|------|------|------|
| JumpFloodFill | Jump Flood Algorithm（Voronoi/SDF 生成） | NEW-SEAM:compute-readback | BLOCKED:compute-readback | R3 | 子 op：ComputeShaderStage×3 + Execute。多趟 compute + dispatch ordering |
| Sketch | 手繪草稿筆觸效果（幾何轉 image） | Layer2d+Execute | BLOCKED:Layer2d+Execute | R3 | 子 op：RadialPoints + Group + PointsToCPU + Loop + UseFallbackTexture + Layer2d×2 + Execute×5 + RenderTarget×4。需要 points/geometry pipeline + Layer2d |
| SlidingHistory | 滑動歷史幀堆疊（時間橫向疊加） | feedback | BLOCKED:feedback | R3 | 子 op：ComputeShaderStage×3 + FloatsToBuffer×2 + SrvFromTexture2d + IntsToBuffer + PickInt + Int2 + RenderTarget + Execute×3。時間回饋 ping-pong |

---

## image/generate/noise (5 ops)

| op | 一句功能 | 主要 seam | 狀態 | 風險 | 備註 |
|----|---------|----------|------|------|------|
| FractalNoise | 碎形噪聲（fBm Perlin） | image-filter | READY-LEAF | R1 | 子 op：_ImageFxShaderSetupStatic |
| Grain | 底片顆粒噪聲 | image-filter | READY-LEAF | R1 | 子 op：_ImageFxShaderSetupStatic |
| ShardNoise | 碎片/晶格噪聲 | image-filter | READY-LEAF | R1 | 子 op：_ImageFxShaderSetupStatic |
| TileableNoise | 可重複貼合噪聲 | image-filter | READY-LEAF | R1 | 子 op：_ImageFxShaderSetupStatic |
| WorleyNoise | Worley/細胞噪聲 | image-filter | READY-LEAF | R1 | 子 op：_ImageFxShaderSetupStatic |

---

## image/generate/pattern (9 ops)

| op | 一句功能 | 主要 seam | 狀態 | 風險 | 備註 |
|----|---------|----------|------|------|------|
| FraserGrid | Fraser 干涉格紋 | image-filter | READY-LEAF | R1 | 子 op：_ImageFxShaderSetupStatic |
| NumberPattern | 數字矩陣圖案 | multi-image | BLOCKED:multi-image | R2 | 子 op：RenderTarget + LoadImage + _Time_old + Add + _multiImageFxSetupStatic + RenderTarget×2。需 source-op(LoadImage) + multi-image |
| Raster | 掃描線柵格 | image-filter | READY-LEAF | R1 | 子 op：_ImageFxShaderSetupStatic |
| Rings | 同心圓環 | image-filter | READY-LEAF | R1 | 子 op：_ImageFxShaderSetupStatic |
| RyojiPattern1 | Ryoji Ikeda 風格圖案 v1 | image-filter | READY-LEAF | R2 | 子 op：_ImageFxShaderSetup2（動態）。大型 .t3（429 行）但模式同 AdjustColors；R2 因多參數 |
| RyojiPattern2 | Ryoji Ikeda 風格圖案 v2 | image-filter | READY-LEAF | R2 | 子 op：_ImageFxShaderSetup2（動態）。489 行 .t3；R2 因多參數 |
| SinForm | 正弦波形圖案 | image-filter | READY-LEAF | R1 | 子 op：_ImageFxShaderSetupStatic |
| ValueRaster | 灰階值柵格 | image-filter | READY-LEAF | R1 | 子 op：_ImageFxShaderSetupStatic |
| ZollnerPattern | Zöllner 錯覺圖案 | image-filter | READY-LEAF | R1 | 子 op：_ImageFxShaderSetupStatic |

---

## image/generate/MunchingSquares2 (1 op)

| op | 一句功能 | 主要 seam | 狀態 | 風險 | 備註 |
|----|---------|----------|------|------|------|
| MunchingSquares2 | Munching Squares 碎形動畫 | image-filter | READY-LEAF | R1 | 子 op：_ImageFxShaderSetupStatic |

---

## image/transform (6 ops)

| op | 一句功能 | 主要 seam | 狀態 | 風險 | 備註 |
|----|---------|----------|------|------|------|
| Crop | 裁切貼圖（指定區域） | image-filter | READY-LEAF | R1 | 已 port（Cut 52）。ComputeShader + custom executor |
| ImageFFT | 貼圖 FFT 頻域變換 | NEW-SEAM:fft-compute | BLOCKED:fft-compute | R3 | 子 op：ComputeShaderStage×6 + Execute×2。需要 FFT dispatch 序列 + 複數貼圖格式 |
| MakeTileableImage | 製作可重複貼合圖（基本） | gradient-widget | BLOCKED:gradient-widget | R2 | 子 op：TransformImage + LinearGradient（GradientsToTexture dep）+ BlendWithMask×3 |
| MakeTileableImageAdvanced | 製作可重複貼合圖（進階） | image-filter | READY-LEAF | R2 | 子 op：BlendWithMask（多次）。只用 image-filter seam 可組合；R2 因多參數 |
| MirrorRepeat | 鏡像重複貼圖 | image-filter | READY-LEAF | R1 | 子 op：_ImageFxShaderSetup2。已在 task_258d9510 audit 清單 |
| TransformImage | 2D 變換（平移/旋轉/縮放） | image-filter | READY-LEAF | R1 | 子 op：_ImageFxShaderSetupStatic（ifx=1）。基礎 2D transform |

---

## image/use (18 ops)

| op | 一句功能 | 主要 seam | 狀態 | 風險 | 備註 |
|----|---------|----------|------|------|------|
| Blend | 雙圖混合（多種 blend mode） | multi-image | BLOCKED:multi-image | R2 | 子 op：_multiImageFxSetupStatic（cc34a183）。R2 routing；**BlendMode/AlphaMode 枚舉多** |
| BlendImages | 多圖序列混合選擇器 | multi-image | BLOCKED:multi-image | R2 | 子 op：Blend + PickTexture + RgbaToColor。compound 組合，需 multi-image seam |
| BlendWithMask | 帶 mask 的三輸入混合 | multi-image | BLOCKED:multi-image | R2 | 子 op：_multiImageFxSetupStatic + Execute×2 + FloatsToBuffer×2 + RenderTarget×3。三輸入（需 _trippleImageFxSetup 或相等）|
| Combine3Images | 組合 3 張貼圖 | multi-image | BLOCKED:multi-image | R2 | 子 op：_multiImageFxSetupStatic。三輸入 |
| CombineMaterialChannels | 合併材質通道（RGBA 分通道） | multi-image | BLOCKED:multi-image | R2 | 子 op：_multiImageFxSetupStatic + Execute×2 + FloatsToBuffer×2 + RenderTarget×3 |
| CombineMaterialChannels2 | 合併材質通道 v2 | multi-image | BLOCKED:multi-image | R2 | 子 op：_multiImageFxSetupStatic |
| CustomPixelShader | 用戶自定義 pixel shader | gradient-widget | BLOCKED:gradient-widget | R3 | 子 op：GradientsToTexture + Execute×2 + FloatsToBuffer×2 + RenderTarget×3。允許使用者指定 .hlsl；gradient 是其中一個可選輸入 |
| DepthBufferAsGrayScale | 深度 buffer 轉灰階貼圖 | NEW-SEAM:compute-readback | BLOCKED:compute-readback | R3 | 子 op：UavFromTexture2d + ExecuteTextureUpdate + ComputeShaderStage×3 + FloatsToBuffer×2 + RenderTarget + Execute。需 UAV RW |
| FirstValidTexture | 選第一個非空貼圖 | compound | COMPOUND | R1 | 純邏輯選擇：MultiInputSlot<Texture2D>→第一個非空輸出。子 op：value-graph 邏輯 |
| Fxaa | FXAA 反鋸齒 | multi-image | BLOCKED:multi-image | R2 | 子 op：_multiImageFxSetupStatic + Execute×2 + FloatsToBuffer×2 + RenderTarget×3 |
| KeepInTextureArray | 維護 Texture2D array（ring buffer） | NEW-SEAM:texture-array | BLOCKED:texture-array | R3 | .cs native 190 行：CopyIntoArray + arrayDesc。需要 Texture2DArray D3D 資源 |
| KeepPreviousFrame | 保存上一幀（簡單 ping-pong） | feedback | BLOCKED:feedback | R2 | .cs native 89 行：CopyResource ping-pong。TiXL feedback 基元；比 AdvancedFeedback 簡單 |
| NormalMap | 從高度圖生成法線貼圖 | image-filter | READY-LEAF | R1 | 子 op：_ImageFxShaderSetupStatic |
| PickTexture | 從多輸入按 index 選貼圖 | compound | COMPOUND | R1 | .cs native 45 行：MultiInputSlot<Texture2D>→index 選。value-graph |
| RenderWithMotionBlur | 帶運動模糊的渲染（疊合多幀） | feedback | BLOCKED:feedback | R3 | 子 op：_multiImageFxSetupStatic + Execute×2 + FloatsToBuffer×2 + RenderTarget×3。時間累積 |
| SwapTextures | 每幀交換兩貼圖（ping-pong helper） | feedback | BLOCKED:feedback | R2 | .cs native 47 行：swap logic。需要 ping-pong buffer 機制 |
| UseFallbackTexture | 若主貼圖空則用 fallback | compound | COMPOUND | R1 | .cs native 37 行：null 檢查選擇。value-graph |
| UseTextureReference | 貼圖引用（pass-through wrapper） | compound | COMPOUND | R1 | .cs native 33 行：直接 pass Texture2D ref |
| _KeepPreviousFrame_Old1 | 舊版保存上一幀（廢棄） | feedback | BLOCKED:feedback | R2 | 廢棄。子 op：_multiImageFxSetupStatic。不建議 port |

---

## 摘要

- **總 op 數**：127（含 _obsolete 3 顆廢棄）
- **READY-LEAF**：38 顆（可直接進 Phase C 開採）
- **COMPOUND（純邏輯）**：4 顆（FirstValidTexture/PickTexture/UseFallbackTexture/UseTextureReference）

### BLOCKED seam 分佈

| seam | op 數 | 代表 op |
|------|-------|---------|
| `image-filter` 踩 READY | 38 | Blur/AdjustColors/FractalNoise/Rings/Dither... |
| `multi-image` | 20 | Blend/Displace(已port)/DirectionalBlur/FakeLight/MosiacTiling/HoneyCombTiles/Pixelate/BlendWithMask... |
| `gradient-widget` | 12 | BubbleZoom/LinearGradient/BoxGradient/RadialGradient/NGonGradient/MandelbrotFractal/Steps/SubdivisionStretch/MakeTileableImage/CustomPixelShader/ColorGradeDepth/RemapColor |
| `Layer2d+Execute` | 10 | Glow/Bloom/AfterGlow/AdvancedFeedback/Sketch/AsciiRender/GlitchDisplace/WaveForm/LightRaysFx/ScreenCloseUp/ColorPhysarum/DetectMotion/FieldToImage |
| `feedback` | 10 | AfterGlow/AdvancedFeedback/SimpleLiquid/SlidingHistory/KeepPreviousFrame/SwapTextures/RenderWithMotionBlur... |
| `source-op` | 3 | LoadImage/ImageSequenceClip/BuildAsciiFontSorting |
| `compute-readback` | 5 | GetImageBrightness/RemoveStaticBackground/SortPixelGlitch/DepthBufferAsGrayScale/JumpFloodFill |

### 已 port（synthesis 時 join）

已知：Blur / Crop / FastBlur / RgbTV / Displace / DistortAndShade（Cut 51-59）。
MirrorRepeat / ChromaticDistortion / DetectEdges / Dither / Sharpen / Pixelate / VoronoiCells / KochKaleidoskope 在 task_258d9510 audit 清單。

### NEW-SEAM 清單

| NEW-SEAM 短名 | 描述 | 擋幾顆 |
|--------------|------|--------|
| `compute-readback` | GPU compute → staging buffer → CPU 讀回（UAV + MapRead） | 5（GetImageBrightness/RemoveStaticBackground/SortPixelGlitch/DepthBufferAsGrayScale/JumpFloodFill） |
| `texture-format-convert` | 目標 DXGI format 不同於預設 RGBA8 的 RenderTarget 建立（HDR/R32 etc） | 1（ConvertFormat） |
| `texture-array` | Texture2DArray D3D resource + 環形 buffer 管理 | 1（KeepInTextureArray） |
| `cubemap-prefilter` | CubeMap 輸入 + GeometryShader specular prefilter（3D pipeline） | 1（_SpecularPrefilter） |
| `fft-compute` | 多趟 FFT compute dispatch + 複數 texture format | 1（ImageFFT） |
| `network-fetch` | HTTP async fetch + decode | 1（LoadImageFromUrl） |
| `svg-rasterize` | SVG 向量圖形光柵化 | 1（LoadSvgAsTexture2D） |

### 意外/盲區

1. **DirectionalBlur 是 BLOCKED:multi-image（非 READY）**：雖然只用 _multiImageFxSetupStatic，但 Cut 55 已確認 .t3 routing 有中間數學節點（Size/Samples 相除 + ×0.03 + RefineSizeFactor），不是 1:1。任何消費 _multiImageFxSetup* 的 op 開採前必做 backward-trace。

2. **_ImageFxShaderSetup2 vs _ImageFxShaderSetupStatic 差異**：10 顆 op 用 dynamic shader（2b20afce），其他用 static（bd0b9c5b）。兩者 seam 相同（image-filter），但 dynamic 版需要 runtime hlsl 載入。simple_world 若目前只支援 static，dynamic 版需一行 platform 差異。MirrorRepeat/EdgeRepeat/KochKaleidoskope/ChromaticDistortion/VoronoiCells/Tint/ImageLevels/RyojiPattern1/RyojiPattern2/SinForm 均屬此。

3. **GlitchDisplace 直接用 PixelShader + Execute，不走 _ImageFxSetup wrapper**：這表示它實際上踩的是 Layer2d+Execute 底層 DX11 API，比一般 image-filter 複雜。

4. **FieldToImage 既需 gradient-widget 又需 Layer2d+Execute**：主要阻擋 seam 以 Layer2d+Execute 列（更大的 unlock）。

5. **MunchingSquares2 放在 generate/ 根目錄下（不在 basic/noise/pattern 任何子目錄）**：分類無誤但位置特殊。

6. **_SpecularPrefilter 在 image/fx/_ 目錄但完全不是 2D image op**（輸入是 CubeMap + GeometryShader）。這是 3D 環境渲染 primitive，放錯目錄，port 優先級最低。

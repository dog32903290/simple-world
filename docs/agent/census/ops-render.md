# Census: render/ (155 ops)

掃描方法：grep base class / Slot<> 輸出型別 / SymbolId 子 op 數 / .cs 實作模式。
所有 155 顆 op 同時有 .cs + .t3（TiXL 的 op 格式每顆皆有）。
「COMPOUND」判定：.t3 內含 SymbolId 引用（子 op），.cs 提供 C# 宿主邏輯。

---

## 子目錄概覽

| 子目錄 | 顆數 | 主要輸出型別 | 簡述 |
|--------|------|-------------|------|
| `basic/` | 8 | Command, Texture2D | Layer2d / DrawScreenQuad / Text 等核心繪製 op |
| `camera/` | 11 | Command | 攝影機系統（perspective/orbit/ortho/blend） |
| `gizmo/` | 15 | Command | 3D 線框輔助視覺（gizmo draw calls） |
| `postfx/` | 7 | Texture2D, Command | 後製效果（GodRays/SSAO/DoF/MotionBlur） |
| `scene/` | 2 | Command, SceneSetup | GLTF 場景載入與繪製 |
| `shading/` | 22 | Command | 光照 / 材質 / 環境 / PBR 上下文設定 |
| `sprite/` | 4 | Command, BufferWithViews | 點精靈 / 文字精靈 |
| `transform/` | 8 | Command | 3D 變換（Group/Transform/Rotate/SpreadIntoGrid） |
| `utils/` | 4 | Command, Texture2D | RepeatMotionBlur / SplitView / ConvertEquirect |
| `analyze/` | 2 | Command | GpuMeasure / GetScreenPos |
| `_/` | 24 | Command, Texture2D, Buffer | 內部工具 op（lens flare / depth / font） |
| `_dx11/api/` | 23 | 各式 DX11 handle | DX11 API 包裝（Draw/FloatsToBuffer/RTV/UAV） |
| `_dx11/buffer/` | 9 | BufferWithViews | buffer 工具（ListToBuffer/SwapBuffers） |
| `_dx11/fxsetup/` | 10 | Texture2D, BlendState | shader stage 設定 / image fx 包裝 |

---

## 完整 per-op 表

### basic/ (8 ops)

| op | 一句功能 | 主要 seam | 狀態 | 風險 | 備註 |
|----|---------|-----------|------|------|------|
| Layer2d | 2D 貼圖合成層（Transform+BlendMode+Draw） | Layer2d+Execute | BLOCKED:Layer2d+Execute | R3 | COMPOUND：內部含 Draw/Execute/OutputMergerStage/SamplerState 等 ~23 子 op；是整個 Layer2d 系統的主消費者 |
| DrawScreenQuad | 全螢幕四邊形繪製（Texture→Command） | Layer2d+Execute | BLOCKED:Layer2d+Execute | R2 | COMPOUND：~19 子 op，內含 Rasterizer/Draw/SamplerState 等；比 Layer2d 簡單但共用同一套 Execute 接縫 |
| DrawScreenQuadAdvanced | 同 DrawScreenQuad 加 DepthBuffer 輸入 | Layer2d+Execute | BLOCKED:Layer2d+Execute | R2 | COMPOUND：~21 子 op；加 DepthBuffer input |
| Text | 文字渲染（BitmapFont→Command） | NEW-SEAM:bitmapfont | BLOCKED:NEW-SEAM:bitmapfont | R3 | COMPOUND：~24 子 op；需 BmFont asset 載入 + 字元幾何生成；TransformCallbackSlot |
| TextOutlines | 文字輪廓繪製 | NEW-SEAM:bitmapfont | BLOCKED:NEW-SEAM:bitmapfont | R3 | COMPOUND：~25 子 op；類似 Text |
| FadingSlideShow | 圖片目錄淡入淡出幻燈片 | NEW-SEAM:folder-image-source | BLOCKED:NEW-SEAM:folder-image-source | R3 | COMPOUND：~29 子 op；需掃目錄+動態 asset 載入；雙輸出 Command+Texture2D |
| DustParticles | 簡易灰塵粒子（軟粒子 noise 運動） | particle-system | BLOCKED:particle-system | R2 | COMPOUND：~33 子 op；需 particle-system seam；有 Texture_ 輸入 |
| ShadowPlane | 陰影接收平面繪製 | NEW-SEAM:shadow-map | BLOCKED:NEW-SEAM:shadow-map | R3 | COMPOUND：~21 子 op；需 shadow map 管線；與 SetShadow 連動 |

### camera/ (11 ops)

| op | 一句功能 | 主要 seam | 狀態 | 風險 | 備註 |
|----|---------|-----------|------|------|------|
| Camera | 透視攝影機（設 WorldToCamera / CameraToClipSpace context） | NEW-SEAM:camera3d | BLOCKED:NEW-SEAM:camera3d | R2 | .cs 直接操作 context.WorldToCamera/CameraToClipSpace；ICamera + ICameraPropertiesProvider；輸出 Command + Object Reference |
| OrbitCamera | 軌道攝影機（Spin/Orbit angle + damping） | NEW-SEAM:camera3d | BLOCKED:NEW-SEAM:camera3d | R2 | 複雜角度插值；temporal-random 部分（wobble seed） |
| CameraWithRotation | 有旋轉模式的透視攝影機 | NEW-SEAM:camera3d | BLOCKED:NEW-SEAM:camera3d | R2 | 多旋轉模式（YPR/Quaternion）；ICamera |
| OrthographicCamera | 正交攝影機 | NEW-SEAM:camera3d | BLOCKED:NEW-SEAM:camera3d | R1 | 較簡單；只設 ortho proj matrix |
| ActionCamera | 第一人稱 WASD 互動攝影機 | NEW-SEAM:camera3d | BLOCKED:NEW-SEAM:camera3d | R3 | 需即時輸入（Forward/Sideways/UpDown slots）；狀態累積 |
| BlendCameras | 在多個 ICamera 間插值混合 | NEW-SEAM:camera3d | BLOCKED:NEW-SEAM:camera3d | R2 | MultiInputSlot<object> 收集相機引用；ICamera |
| ReuseCamera | 把攝影機 reference 插入場景樹 | NEW-SEAM:camera3d | BLOCKED:NEW-SEAM:camera3d | R1 | 簡單引用轉發 |
| ShiftCamera | 平移/縮放攝影機（context 矩陣偏移） | NEW-SEAM:camera3d | BLOCKED:NEW-SEAM:camera3d | R1 | 簡單 matrix offset |
| CamPosition | 從 context 讀出攝影機位置/方向/AspectRatio | NEW-SEAM:camera3d | BLOCKED:NEW-SEAM:camera3d | R1 | 純讀取 context；輸出 Slot<Command> + Vector3 + Vector3 + float |
| CurrentCamMatrices | 讀出 WorldToClipSpace 矩陣 | NEW-SEAM:camera3d | BLOCKED:NEW-SEAM:camera3d | R1 | 輸出 Vector4[] |
| VisualizeCamTrail | 繪製攝影機軌跡 gizmo | NEW-SEAM:camera3d | BLOCKED:NEW-SEAM:camera3d | R2 | 在 camera/analyze/ 子目錄；需 camera3d + gizmo draw |

### gizmo/ (15 ops)

所有 gizmo op 輸出 Command，在 3D viewport 中繪製線框/形狀。核心需要 camera3d context（ObjectToWorld/WorldToCamera/CameraToClipSpace）。

| op | 一句功能 | 主要 seam | 狀態 | 風險 | 備註 |
|----|---------|-----------|------|------|------|
| DrawLineGrid | 3D 線框網格 | NEW-SEAM:camera3d | BLOCKED:NEW-SEAM:camera3d | R1 | COMPOUND；線段繪製 |
| DrawSphereGizmo | 3D 球形 gizmo | NEW-SEAM:camera3d | BLOCKED:NEW-SEAM:camera3d | R1 | COMPOUND |
| DrawBoxGizmo | 3D 盒形 gizmo | NEW-SEAM:camera3d | BLOCKED:NEW-SEAM:camera3d | R1 | COMPOUND |
| DrawCamGizmos | 繪製攝影機視錐 gizmo | NEW-SEAM:camera3d | BLOCKED:NEW-SEAM:camera3d | R2 | COMPOUND；需 camera reference |
| DrawSpatialAudioGizmos | 空間音訊 gizmo 視覺化 | NEW-SEAM:camera3d | BLOCKED:NEW-SEAM:camera3d | R2 | COMPOUND；音訊 slot 輸入 |
| GridPlane | 地面網格平面 | NEW-SEAM:camera3d | BLOCKED:NEW-SEAM:camera3d | R1 | COMPOUND |
| Locator | 位置/軸心標記 | NEW-SEAM:camera3d | BLOCKED:NEW-SEAM:camera3d | R1 | TransformCallbackSlot；ITransformable |
| PlotValueCurve | 曲線數值繪製 gizmo | NEW-SEAM:camera3d | BLOCKED:NEW-SEAM:camera3d | R2 | COMPOUND；時間/動畫相關 |
| VisibleGizmos | MultiInputSlot<Command> gizmo 收集器 | NEW-SEAM:camera3d | BLOCKED:NEW-SEAM:camera3d | R1 | 類似 Group；收集多個 gizmo Command |
| ConeGizmo | 錐形 gizmo | NEW-SEAM:camera3d | BLOCKED:NEW-SEAM:camera3d | R1 | 輸出 StructuredList (Points)；COMPOUND |
| DrawSphere (_/) | 3D 實心球（gizmo/_/） | NEW-SEAM:camera3d | BLOCKED:NEW-SEAM:camera3d | R2 | TransformCallbackSlot；ITransformable |
| _CameraGizmo | 攝影機符號繪製 | NEW-SEAM:camera3d | BLOCKED:NEW-SEAM:camera3d | R2 | 內部工具 |
| _DrawPointInfo | 點資料 debug 視覺化 | NEW-SEAM:camera3d | BLOCKED:NEW-SEAM:camera3d | R2 | 輸入 BufferWithViews；gizmo 視覺化 |
| _OutputWindowGrid | 輸出視窗網格 | NEW-SEAM:camera3d | BLOCKED:NEW-SEAM:camera3d | R1 | 內部工具 |
| _VisualizeTBN | 切線空間 TBN 視覺化 | NEW-SEAM:camera3d | BLOCKED:NEW-SEAM:camera3d | R2 | 輸入 MeshBuffers；需 mesh-pipeline |

### postfx/ (7 ops)

| op | 一句功能 | 主要 seam | 狀態 | 風險 | 備註 |
|----|---------|-----------|------|------|------|
| GodRays | 光束放射（Radial Blur on depth） | NEW-SEAM:depth-buffer | BLOCKED:NEW-SEAM:depth-buffer | R3 | 輸入 Image+DepthBuffer+CameraRef；多 pass shader；需 camera3d context |
| SSAO | Screen-Space Ambient Occlusion | NEW-SEAM:depth-buffer | BLOCKED:NEW-SEAM:depth-buffer | R3 | 輸入 Texture2D+DepthBuffer；多 pass；雙輸出 |
| DepthOfField | 景深模糊（Color+Depth） | NEW-SEAM:depth-buffer | BLOCKED:NEW-SEAM:depth-buffer | R3 | 輸入 TextureBuffer+DepthBuffer |
| MotionBlur | 速度場動態模糊 | NEW-SEAM:depth-buffer | BLOCKED:NEW-SEAM:depth-buffer | R3 | 輸入 Image+DepthMap+CameraRef；需 camera3d |
| TemporalAccumulation | 逐幀累積（ping-pong）FeedbackAmount | feedback | BLOCKED:feedback | R3 | 輸入 Texture+FeedbackAmount；典型 feedback seam |
| ProjectLight | 投影光（render scene from light POV） | NEW-SEAM:shadow-map | BLOCKED:NEW-SEAM:shadow-map | R3 | 輸入 Scene(Command)+CameraRef+Image；場景再渲染 |
| _ProjectedLight | ProjectLight 的內部實作 op | NEW-SEAM:shadow-map | BLOCKED:NEW-SEAM:shadow-map | R3 | 內部工具 |

### scene/ (2 ops)

| op | 一句功能 | 主要 seam | 狀態 | 風險 | 備註 |
|----|---------|-----------|------|------|------|
| LoadGltfScene | 載入 GLTF 場景為 SceneSetup + MeshBuffers | NEW-SEAM:gltf-scene | BLOCKED:NEW-SEAM:gltf-scene | R3 | SharpGLTF 依賴；輸出 SceneSetup+MeshBuffers+PbrMaterial；需檔案資源系統 |
| DrawScene | 繪製 SceneSetup（PBR 材質從 context） | NEW-SEAM:gltf-scene | BLOCKED:NEW-SEAM:gltf-scene | R3 | ICompoundWithUpdate；需 camera3d + PBR 材質 context |

### shading/ (22 ops)

| op | 一句功能 | 主要 seam | 狀態 | 風險 | 備註 |
|----|---------|-----------|------|------|------|
| SetMaterial | 設定 PbrMaterial 參數與 texture maps | NEW-SEAM:pbr-material | BLOCKED:NEW-SEAM:pbr-material | R2 | 輸出 Command+PbrMaterial Reference；4 個 texture map 輸入 |
| UseMaterial | 把 PbrMaterial reference 套入 context | NEW-SEAM:pbr-material | BLOCKED:NEW-SEAM:pbr-material | R1 | 輸入 PbrMaterial；context 注入 |
| DefineMaterials | MultiInput<PbrMaterial> 收集注入 context | NEW-SEAM:pbr-material | BLOCKED:NEW-SEAM:pbr-material | R2 | ICompoundWithUpdate；收集多材質 |
| SetEnvironment | IBL 環境貼圖設定（Equirect→cubemap probe） | NEW-SEAM:pbr-material | BLOCKED:NEW-SEAM:pbr-material | R3 | 輸入 Texture2D；Fallback enum（Studio/Cathedral/Black） |
| Equirectangle | Equirectangular 貼圖作為環境 | NEW-SEAM:pbr-material | BLOCKED:NEW-SEAM:pbr-material | R2 | 輸出 Texture2D |
| TextureToCubeMap | 2D 貼圖轉 CubeMap | NEW-SEAM:cubemap | BLOCKED:NEW-SEAM:cubemap | R3 | 輸出 Texture2D（CubeMap）；需 cubemap render-target |
| PointLight | 點光源定義（ITransformable） | NEW-SEAM:lighting | BLOCKED:NEW-SEAM:lighting | R1 | 輸出 Command；Position+Intensity+Color+Range |
| SetPointLight | 把 PointLight 注入 context | NEW-SEAM:lighting | BLOCKED:NEW-SEAM:lighting | R1 | 輸出 Command；context 注入 |
| SetFog | 霧效參數設定 | NEW-SEAM:lighting | BLOCKED:NEW-SEAM:lighting | R1 | 輸出 Command；Fog params |
| SetShadow | 陰影參數設定 | NEW-SEAM:shadow-map | BLOCKED:NEW-SEAM:shadow-map | R2 | 輸出 Command；shadow bias/splits |
| LenseFlareSetup | 鏡頭光暈設定 | NEW-SEAM:lens-flare | BLOCKED:NEW-SEAM:lens-flare | R2 | 輸出 Command |
| LenseFlareSetupAdvanced | 進階鏡頭光暈設定 | NEW-SEAM:lens-flare | BLOCKED:NEW-SEAM:lens-flare | R2 | 輸出 Command |
| SetRequestedResolution | 設定渲染解析度（從 Texture2D 讀） | NEW-SEAM:render-state | BLOCKED:NEW-SEAM:render-state | R1 | 輸出 Texture2D；讀 Description.Width/Height |
| IntToWrapmode | int→TextureAddressMode 轉換 | value-graph | TRIVIAL | R1 | 輸出 TextureAddressMode enum |
| GetPointLightOccclusion | 計算點光源遮蔽率 | NEW-SEAM:lighting | BLOCKED:NEW-SEAM:lighting | R3 | 輸出 float；需深度/遮蔽計算 |
| ContextCBuffers (_/) | 把多個 Buffer 注入 shader context | NEW-SEAM:render-state | BLOCKED:NEW-SEAM:render-state | R2 | 輸出 Buffer；內部工具 |
| DrawQuad (_/) | 繪製一個四邊形 pass | NEW-SEAM:render-state | BLOCKED:NEW-SEAM:render-state | R2 | 輸出 Command；輸入 Texture2D |
| FourPointLights (_/) | 四盞點光源合併注入 context | NEW-SEAM:lighting | BLOCKED:NEW-SEAM:lighting | R2 | 輸出 Command |
| GetAllCameras (_/) | 收集場景中所有攝影機 | NEW-SEAM:camera3d | BLOCKED:NEW-SEAM:camera3d | R2 | 輸出 Vector3 |
| GetCamTransformBuffer (_/) | 攝影機變換矩陣打包為 Buffer | NEW-SEAM:camera3d | BLOCKED:NEW-SEAM:camera3d | R2 | 輸出 Buffer |
| GetPbrParameters (_/) | 讀取 PBR 參數為 Buffer | NEW-SEAM:pbr-material | BLOCKED:NEW-SEAM:pbr-material | R2 | 輸出 Buffer |
| GetTextureFromContext (_/) | 從 context 讀 texture | NEW-SEAM:render-state | BLOCKED:NEW-SEAM:render-state | R1 | 輸出 Texture2D |
| SetContextTexture (_/) | 把 texture 注入 context | NEW-SEAM:render-state | BLOCKED:NEW-SEAM:render-state | R1 | 輸出 Command |
| _DispatchSceneDraws (_/) | 分發場景繪製 call | NEW-SEAM:gltf-scene | BLOCKED:NEW-SEAM:gltf-scene | R3 | 輸出 Command；最終繪製分發 |
| _DrawLenseFlare_Old (_/) | 舊版鏡頭光暈繪製（廢棄） | NEW-SEAM:lens-flare | BLOCKED:NEW-SEAM:lens-flare | R3 | 標記廢棄；可跳過 |
| _GetSceneDefinitionPoints (_/) | 從場景定義讀取 StructuredList | NEW-SEAM:gltf-scene | BLOCKED:NEW-SEAM:gltf-scene | R2 | 輸出型別不明確（empty grep）；內部工具 |
| DrawLensFlares (_/) | 繪製鏡頭光暈 | NEW-SEAM:lens-flare | BLOCKED:NEW-SEAM:lens-flare | R2 | 輸出 Command；輸入 Texture2D |

### sprite/ (4 ops)

| op | 一句功能 | 主要 seam | 狀態 | 風險 | 備註 |
|----|---------|-----------|------|------|------|
| DrawPointSprites | 點精靈繪製（BufferWithViews→Command） | NEW-SEAM:point-sprite | BLOCKED:NEW-SEAM:point-sprite | R2 | 輸入 Points+SpriteBuffer+Texture；需 geometry shader 或 billboard |
| DrawPointSpritesShaded | PBR 著色點精靈 | NEW-SEAM:point-sprite | BLOCKED:NEW-SEAM:point-sprite | R3 | 輸入 GPoints+Sprites+Texture；需 pbr-material |
| TextSprites | 文字精靈（BmFont→BufferWithViews） | NEW-SEAM:bitmapfont | BLOCKED:NEW-SEAM:bitmapfont | R3 | 輸出 PointBuffer+SpriteBuffer+Texture2D；載入 BmFont asset |
| SampleSpriteAttributes | 從貼圖取樣精靈屬性 | NEW-SEAM:point-sprite | BLOCKED:NEW-SEAM:point-sprite | R2 | 輸出 BufferWithViews；TransformCallbackSlot；_experimental |

### transform/ (8 ops)

所有 transform op 輸出 Command，操作 context.ObjectToWorld 矩陣，在執行時傳遞給子節點。

| op | 一句功能 | 主要 seam | 狀態 | 風險 | 備註 |
|----|---------|-----------|------|------|------|
| Transform | 基本 SRT transform（Position/Rotation/Scale） | NEW-SEAM:camera3d | BLOCKED:NEW-SEAM:camera3d | R1 | 操作 context.ObjectToWorld；ITransformable；TransformCallback |
| Group | 多子命令 + SRT 包裝（MultiInput） | NEW-SEAM:camera3d | BLOCKED:NEW-SEAM:camera3d | R1 | ITransformable；有 profiling slot |
| RotateAroundAxis | 繞任意軸旋轉 | NEW-SEAM:camera3d | BLOCKED:NEW-SEAM:camera3d | R1 | 矩陣旋轉；操作 context.ObjectToWorld |
| RotateTowards | 旋轉朝向目標點 | NEW-SEAM:camera3d | BLOCKED:NEW-SEAM:camera3d | R1 | LookAt 式旋轉 |
| Shear | 剪切變換 | NEW-SEAM:camera3d | BLOCKED:NEW-SEAM:camera3d | R1 | 4x4 矩陣剪切 |
| SliceViewPort | 視口切片（Viewport 矩形變換） | NEW-SEAM:camera3d | BLOCKED:NEW-SEAM:camera3d | R2 | 需 Viewport DX11 state |
| SpreadIntoGrid | 把子命令排列成網格 | NEW-SEAM:camera3d | BLOCKED:NEW-SEAM:camera3d | R2 | 需循環 N 次渲染 pass |
| SpreadLayout | 自訂排列佈局（Point[] 驅動） | NEW-SEAM:camera3d | BLOCKED:NEW-SEAM:camera3d | R2 | 輸入 PointList 驅動位置 |

### utils/ (4 ops)

| op | 一句功能 | 主要 seam | 狀態 | 風險 | 備註 |
|----|---------|-----------|------|------|------|
| RequestedResolution | 從 context 讀取渲染解析度（→Int2/int/float） | value-graph | READY-LEAF | R1 | 純讀取 context.RequestedResolution；無 shader；TRIVIAL |
| DrawAsSplitView | MultiInput<Command> 並排/切片顯示 | NEW-SEAM:camera3d | BLOCKED:NEW-SEAM:camera3d | R2 | 需 Viewport 分割；RepeatView 或 SliceView |
| RepeatWithMotionBlur | 多次渲染同一 Command 並動態模糊疊加 | NEW-SEAM:render-state | BLOCKED:NEW-SEAM:render-state | R3 | 輸入 SubGraph(Command)；需多 pass render-target blend |
| ConvertEquirectangle | Equirectangular→Texture2D 轉換 | NEW-SEAM:cubemap | BLOCKED:NEW-SEAM:cubemap | R3 | 輸出 Texture2D；輸入 Image；需 cubemap 轉換管線 |

### analyze/ (2 ops)

| op | 一句功能 | 主要 seam | 狀態 | 風險 | 備註 |
|----|---------|-----------|------|------|------|
| GpuMeasure | GPU 時間戳查詢（DisjointQuery） | NEW-SEAM:gpu-query | BLOCKED:NEW-SEAM:gpu-query | R2 | 輸出 Command；SharpDX GPU query 包裝；Mac Metal 需替代 API |
| GetScreenPos | 3D 世界座標→螢幕 NDC 座標換算 | NEW-SEAM:camera3d | BLOCKED:NEW-SEAM:camera3d | R1 | 純 CPU 矩陣乘法；輸出 Command+Vector3；只需 context 矩陣 |

### _/ (24 ops)

| op | 一句功能 | 主要 seam | 狀態 | 風險 | 備註 |
|----|---------|-----------|------|------|------|
| ApplyCamTransform | 把 ICamera Reference 套入 context 矩陣 | NEW-SEAM:camera3d | BLOCKED:NEW-SEAM:camera3d | R1 | 純 context 注入 |
| ApplyCamMatrices | 把 Vector4[] 矩陣列套入 context | NEW-SEAM:camera3d | BLOCKED:NEW-SEAM:camera3d | R1 | 輸入 Vector4[]；純矩陣操作 |
| ApplyTransformMatrix | 把 4x4 矩陣套入 ObjectToWorld | NEW-SEAM:camera3d | BLOCKED:NEW-SEAM:camera3d | R1 | 輸入 Vector4[]（4列）；純矩陣 |
| ComputeImageDifference | 計算兩張貼圖差異（float） | image-filter | BLOCKED:image-filter | R2 | 輸出 float；輸入 Texture2D×2；需 compute read-back |
| DefineLensFlare | 定義鏡頭光暈資料（→StructuredList） | NEW-SEAM:lens-flare | BLOCKED:NEW-SEAM:lens-flare | R2 | 輸出 StructuredList |
| DrawLensShimmer | 繪製鏡頭閃光（shader draw call） | NEW-SEAM:lens-flare | BLOCKED:NEW-SEAM:lens-flare | R2 | COMPOUND；輸出 Command |
| GetLightPosition | 從 context 讀點光源位置 | NEW-SEAM:lighting | BLOCKED:NEW-SEAM:lighting | R1 | 輸出 Vector3 |
| LenseFlareHoop | 鏡頭光暈環繪製 | NEW-SEAM:lens-flare | BLOCKED:NEW-SEAM:lens-flare | R2 | COMPOUND；輸出 Command |
| PickSDXVector4 | 從 SharpDX 結構選取 Vector4 分量 | value-graph | TRIVIAL | R1 | 輸出 float；純值選取 |
| ReprojectToUV | 把 Mesh UV 重投影為 Texture2D | NEW-SEAM:camera3d | BLOCKED:NEW-SEAM:camera3d | R3 | COMPOUND；輸入 MeshBuffers+Camera；輸出 Texture2D |
| ReuseCamera2 | Camera reference 轉發 v2 | NEW-SEAM:camera3d | BLOCKED:NEW-SEAM:camera3d | R1 | 同 ReuseCamera |
| TextGrid | 字元網格繪製（grid layout text） | NEW-SEAM:bitmapfont | BLOCKED:NEW-SEAM:bitmapfont | R3 | COMPOUND；輸入 DisplaceTexture；字元格排版 |
| TransformMatrix | 建立 4x4 變換矩陣（→Vector4[]） | value-graph | READY-LEAF | R1 | 純 CPU 矩陣建構；輸出 Vector4[]；可直接移植 |
| TypoGridBuffer | 文字排版 Buffer 生成 | NEW-SEAM:bitmapfont | BLOCKED:NEW-SEAM:bitmapfont | R2 | 輸出 Buffer+VertexCount；需字元幾何 |
| _ComputeBRDFLookup | 預計算 BRDF LUT（Texture2D） | NEW-SEAM:pbr-material | BLOCKED:NEW-SEAM:pbr-material | R3 | COMPOUND；輸出 Texture2D；compute shader |
| _ComputeDepthToLinear | 深度貼圖線性化（compute） | NEW-SEAM:depth-buffer | BLOCKED:NEW-SEAM:depth-buffer | R2 | COMPOUND；輸入 DepthBuffer；輸出 Command |
| _ComputeLightOcclusions | 計算光源遮蔽積分 | NEW-SEAM:lighting | BLOCKED:NEW-SEAM:lighting | R3 | COMPOUND；輸入 InputImage；輸出 float |
| _DepthOfField | 景深內部實作（blur passes） | NEW-SEAM:depth-buffer | BLOCKED:NEW-SEAM:depth-buffer | R3 | COMPOUND；輸入 Color+DepthBuffer；multi-pass |
| _LenseFlareHoopPosition | 光暈環螢幕座標計算 | NEW-SEAM:lens-flare | BLOCKED:NEW-SEAM:lens-flare | R1 | 輸出 Vector2；純 CPU |
| _ProcessLayer2d | Layer2d 變換矩陣計算（→Vector4[]） | value-graph | READY-LEAF | R1 | 純 CPU 幾何計算；輸出 Vector4[]；可直接移植 |
| _ReadIntFromGpuBuffer | 從 GPU buffer 讀回 int 值（CPU readback） | RWStructuredBuffer | BLOCKED:RWStructuredBuffer | R3 | 輸出 Command；需 GPU→CPU readback |
| _RenderFontBuffer | 字型幾何 buffer 渲染 | NEW-SEAM:bitmapfont | BLOCKED:NEW-SEAM:bitmapfont | R3 | 輸出 Buffer；字元幾何生成 |
| _ReprojectShadowMap | Shadow map 重投影（Texture2D） | NEW-SEAM:shadow-map | BLOCKED:NEW-SEAM:shadow-map | R3 | COMPOUND；輸出 Texture2D |
| _VisLenseFxZone | 鏡頭 FX 區域視覺化 | NEW-SEAM:lens-flare | BLOCKED:NEW-SEAM:lens-flare | R1 | COMPOUND；輸出 Command |

### _dx11/api/ (23 ops)

這組是 DX11 API 的 C# 包裝（RTV/DSV/UAV/SRV 建立、Draw call、buffer、sampler state 等）。Mac Metal 需要完全不同的對應 API，全部需要 NEW-SEAM:dx11-api-wrapper（或改為 Metal 等效）。

| op | 一句功能 | 主要 seam | 狀態 | 風險 | 備註 |
|----|---------|-----------|------|------|------|
| FloatsToBuffer | float params→GPU cbuffer | NEW-SEAM:dx11-api-wrapper | BLOCKED:NEW-SEAM:dx11-api-wrapper | R2 | ★Cut55 routing trap 根源；simple_world 已有對應能力（FloatsToBuffer 在 imageFilterFxSetup）；可評估複用 |
| Draw | DX11 DrawPrimitive call | NEW-SEAM:dx11-api-wrapper | BLOCKED:NEW-SEAM:dx11-api-wrapper | R2 | IRenderStatsProvider；simple_world 內建對應 |
| DrawInstancedIndirect | GPU indirect draw | NEW-SEAM:dx11-api-wrapper | BLOCKED:NEW-SEAM:dx11-api-wrapper | R3 | 需 indirect dispatch buffer |
| ClearRenderTarget | 清除 RTV | NEW-SEAM:dx11-api-wrapper | BLOCKED:NEW-SEAM:dx11-api-wrapper | R1 | 輸出 Command |
| RtvFromTexture2d | Texture2D→RenderTargetView | NEW-SEAM:dx11-api-wrapper | BLOCKED:NEW-SEAM:dx11-api-wrapper | R2 | IStatusProvider；輸出 RenderTargetView |
| DsvFromTexture2d | Texture2D→DepthStencilView | NEW-SEAM:dx11-api-wrapper | BLOCKED:NEW-SEAM:dx11-api-wrapper | R2 | 輸出 DepthStencilView |
| UavFromTexture2d | Texture2D→UAV | NEW-SEAM:dx11-api-wrapper | BLOCKED:NEW-SEAM:dx11-api-wrapper | R2 | 輸出 UnorderedAccessView |
| UavFromBuffer | Buffer→UAV | NEW-SEAM:dx11-api-wrapper | BLOCKED:NEW-SEAM:dx11-api-wrapper | R2 | 輸出 UnorderedAccessView |
| UavFromStructuredBuffer | StructuredBuffer→UAV | RWStructuredBuffer | BLOCKED:RWStructuredBuffer | R2 | 輸出 UnorderedAccessView |
| SrvFromTexture2d | Texture2D→SRV | NEW-SEAM:dx11-api-wrapper | BLOCKED:NEW-SEAM:dx11-api-wrapper | R1 | 輸出 ShaderResourceView |
| SrvFromStructuredBuffer | StructuredBuffer→SRV | NEW-SEAM:dx11-api-wrapper | BLOCKED:NEW-SEAM:dx11-api-wrapper | R1 | 輸出 ShaderResourceView |
| GenerateMips | 生成 mipmap | mip | READY-LEAF | R1 | simple_world 已有 mip seam；可直接移植評估 |
| GetTextureSize | 讀 Texture2D 寬高（→Int2） | value-graph | READY-LEAF | R1 | 純讀 texture description |
| GetSRVProperties | 讀 SRV 屬性（→int） | NEW-SEAM:dx11-api-wrapper | BLOCKED:NEW-SEAM:dx11-api-wrapper | R1 | 輸出 int |
| CalcDispatchCount | 計算 compute dispatch 個數 | value-graph | READY-LEAF | R1 | 純整數除法；輸出 Int3 |
| CalcInt2DispatchCount | 計算 Int2 dispatch 個數 | value-graph | READY-LEAF | R1 | 同上；Int2 版本 |
| InputAssemblerStage | 設定 IA stage（topology/buffers） | NEW-SEAM:dx11-api-wrapper | BLOCKED:NEW-SEAM:dx11-api-wrapper | R2 | 輸出 Command |
| OutputMergerStage | 設定 OM stage（RTV/DSV/blend） | NEW-SEAM:dx11-api-wrapper | BLOCKED:NEW-SEAM:dx11-api-wrapper | R2 | 輸出 Command |
| Rasterizer | 設定 Rasterizer state | NEW-SEAM:dx11-api-wrapper | BLOCKED:NEW-SEAM:dx11-api-wrapper | R1 | 輸出 Command |
| Viewport | 設定 DX11 Viewport | NEW-SEAM:dx11-api-wrapper | BLOCKED:NEW-SEAM:dx11-api-wrapper | R1 | 輸出 RawViewportF |
| ResolutionConstBuffer | 解析度→cbuffer | NEW-SEAM:dx11-api-wrapper | BLOCKED:NEW-SEAM:dx11-api-wrapper | R1 | 輸出 Buffer |
| TimeConstBuffer | 時間→cbuffer | transport | BLOCKED:transport | R1 | 輸出 Buffer；需 transport seam（時間） |
| TransformsConstBuffer | 變換矩陣→cbuffer | NEW-SEAM:camera3d | BLOCKED:NEW-SEAM:camera3d | R1 | 輸出 Buffer；ObjectToWorld/WorldToCamera |

### _dx11/buffer/ (9 ops)

| op | 一句功能 | 主要 seam | 狀態 | 風險 | 備註 |
|----|---------|-----------|------|------|------|
| FloatsToBuffer | （見 api/FloatsToBuffer） | - | - | - | 此為 buffer/ 子目錄的工具 op |
| FirstValidBuffer | MultiInput<BufferWithViews>→取第一個非空 | value-graph | READY-LEAF | R1 | 純 buffer 選擇邏輯 |
| GetBufferComponents | BufferWithViews→分解 SRV/UAV | NEW-SEAM:dx11-api-wrapper | BLOCKED:NEW-SEAM:dx11-api-wrapper | R1 | 輸出 ShaderResourceView |
| IntsToBufferWithViews | int[] → BufferWithViews | NEW-SEAM:dx11-api-wrapper | BLOCKED:NEW-SEAM:dx11-api-wrapper | R1 | 輸出 BufferWithViews |
| IsBufferDirty | 檢查 buffer dirty 狀態（→bool） | value-graph | READY-LEAF | R1 | 純狀態查詢 |
| ListToBuffer | StructuredList → BufferWithViews | NEW-SEAM:dx11-api-wrapper | BLOCKED:NEW-SEAM:dx11-api-wrapper | R1 | 輸出 BufferWithViews |
| PickBuffer | 條件選擇 BufferWithViews | value-graph | READY-LEAF | R1 | 條件選取 |
| SwapBuffers | ping-pong 雙 buffer 交換 | feedback | BLOCKED:feedback | R2 | 輸出 BufferWithViews；feedback 關鍵元件 |
| Texture3dComponents | Texture3D 分解元件 | NEW-SEAM:dx11-api-wrapper | BLOCKED:NEW-SEAM:dx11-api-wrapper | R2 | 輸出 Texture3dWithViews；Texture3D 需要獨立 seam |
| UseFallbackBuffer | 有效 buffer 或 fallback 選擇 | value-graph | READY-LEAF | R1 | COMPOUND；條件選取 |

### _dx11/fxsetup/ (10 ops)

| op | 一句功能 | 主要 seam | 狀態 | 風險 | 備註 |
|----|---------|-----------|------|------|------|
| _ImageFxShaderSetup2 | image filter shader 設定（v2） | image-filter | BLOCKED:image-filter | R2 | simple_world 已有 image-filter；但此為 DX11 具體實作 |
| _ImageFxShaderSetupStatic | image filter shader 設定（static） | image-filter | BLOCKED:image-filter | R2 | 同上 |
| ShowTexture2d | 輸出/預覽 Texture2D | value-graph | READY-LEAF | R1 | 輸出 Texture2D；simple_world 有對應 |
| ShowTexture3d | 輸出/預覽 Texture3D | NEW-SEAM:dx11-api-wrapper | BLOCKED:NEW-SEAM:dx11-api-wrapper | R2 | 輸出 Texture3dWithViews |
| ExecuteTextureUpdate | Texture2D 熱更新執行 | image-filter | BLOCKED:image-filter | R2 | 輸出 Texture2D |
| ExecuteBufferUpdate | Buffer 熱更新執行 | NEW-SEAM:dx11-api-wrapper | BLOCKED:NEW-SEAM:dx11-api-wrapper | R2 | 輸出 BufferWithViews |
| ExecuteValueUpdate | float 值熱更新執行 | value-graph | READY-LEAF | R1 | 輸出 float |
| PickBlendMode | int→BlendState 選取 | NEW-SEAM:dx11-api-wrapper | BLOCKED:NEW-SEAM:dx11-api-wrapper | R1 | COMPOUND；輸出 BlendState |
| SwitchBlendState | BlendState context 切換 | NEW-SEAM:dx11-api-wrapper | BLOCKED:NEW-SEAM:dx11-api-wrapper | R1 | 輸出 BlendState |
| PrefixSum | GPU prefix sum（compute） | NEW-SEAM:dx11-api-wrapper | BLOCKED:NEW-SEAM:dx11-api-wrapper | R3 | COMPOUND；compute dispatch |
| SetPixelAndVertexShaderStage | 設定 PS+VS shader stage | NEW-SEAM:dx11-api-wrapper | BLOCKED:NEW-SEAM:dx11-api-wrapper | R2 | 輸出 Command |

---

## 摘要

- **總 op 數**：155
- **READY-LEAF**：13 顆（可直接進 Phase C）
- **TRIVIAL**：2 顆（RequestedResolution / IntToWrapmode）
- **BLOCKED**：140 顆

### READY-LEAF 清單（13 顆）
1. `RequestedResolution` — 讀 context.RequestedResolution
2. `TransformMatrix` — CPU 矩陣建構
3. `_ProcessLayer2d` — CPU Layer2d 幾何計算
4. `PickSDXVector4` — 純值選取
5. `GetTextureSize` — 讀 texture description
6. `CalcDispatchCount` — 整數除法
7. `CalcInt2DispatchCount` — 整數除法 Int2
8. `GenerateMips` — mip seam 已建
9. `ShowTexture2d` — 已有對應
10. `ExecuteValueUpdate` — 純 float 更新
11. `FirstValidBuffer` — buffer 條件選取
12. `IsBufferDirty` — 狀態查詢
13. `PickBuffer` — buffer 條件選取
14. `UseFallbackBuffer` — COMPOUND 但邏輯純 value-graph
15. `GetScreenPos` — 純 CPU 矩陣乘（需 camera3d context 讀；若 context 已有可輕鬆移植）

### BLOCKED seam 分佈（解鎖數排序）

| seam | 阻擋顆數 | 說明 |
|------|---------|------|
| NEW-SEAM:camera3d | ~45 | 攝影機/transform/gizmo 全類別；解鎖整個 3D render graph |
| NEW-SEAM:dx11-api-wrapper | ~25 | DX11 API 包裝（Metal 替換）；render 管線底層 |
| NEW-SEAM:pbr-material | ~10 | PBR 材質系統（SetMaterial/SetEnvironment/DefineMaterials 等） |
| NEW-SEAM:lighting | ~8 | 點光源 / fog / occlusion |
| NEW-SEAM:depth-buffer | ~8 | postfx 後製（GodRays/SSAO/DoF/MotionBlur） |
| Layer2d+Execute | ~5 | Layer2d/DrawScreenQuad/DrawScreenQuadAdvanced 核心 2D 合成 |
| NEW-SEAM:lens-flare | ~9 | 鏡頭光暈系統（可評估跳過） |
| NEW-SEAM:bitmapfont | ~7 | 文字渲染（Text/TextOutlines/TextGrid/TextSprites） |
| NEW-SEAM:shadow-map | ~5 | 陰影管線 |
| NEW-SEAM:gltf-scene | ~4 | GLTF 場景載入（LoadGltfScene/DrawScene） |
| NEW-SEAM:point-sprite | ~3 | 點精靈（DrawPointSprites） |
| feedback | ~2 | TemporalAccumulation / SwapBuffers |
| NEW-SEAM:cubemap | ~2 | TextureToCubeMap / ConvertEquirectangle |
| RWStructuredBuffer | ~2 | _ReadIntFromGpuBuffer / UavFromStructuredBuffer |
| NEW-SEAM:gpu-query | 1 | GpuMeasure（Mac 替代 API 存疑） |
| NEW-SEAM:folder-image-source | 1 | FadingSlideShow |
| NEW-SEAM:render-state | ~4 | RepeatWithMotionBlur / ContextCBuffers 等工具 |
| particle-system | 1 | DustParticles |
| image-filter | ~4 | _ImageFxShaderSetup2 / ExecuteTextureUpdate 等（seam 已建但 DX11 wrapper 差異） |

### 冒出的 NEW-SEAM（本類別首次命名）

| 短名 | 一句描述 | 擋幾顆 |
|------|---------|-------|
| `camera3d` | 3D 攝影機/投影矩陣注入 context（WorldToCamera/CameraToClipSpace/ObjectToWorld） | ~45 |
| `dx11-api-wrapper` | DX11 RTV/DSV/UAV/SRV/BlendState/Viewport/IA/OM 等 API 包裝（Metal 替換層） | ~25 |
| `pbr-material` | PBR 材質參數系統（BaseColor/Roughness/Metal/NormalMap/EmissiveMap + PbrMaterial struct） | ~10 |
| `lighting` | 點光源/環境光/霧效 context 注入 | ~8 |
| `depth-buffer` | 深度貼圖讀寫（DSV 建立 + 線性化 compute） | ~8 |
| `lens-flare` | 鏡頭光暈系統（StructuredList 資料定義 + 多 pass 繪製） | ~9 |
| `bitmapfont` | BitmapFont asset 載入 + 字元幾何 buffer 生成 | ~7 |
| `shadow-map` | Shadow map 管線（投影渲染 + 重投影採樣） | ~5 |
| `gltf-scene` | GLTF 場景載入（SharpGLTF）+ SceneSetup struct + PBR dispatch | ~4 |
| `point-sprite` | 點精靈 billboard geometry shader 或 vertex expand | ~3 |
| `render-state` | 泛用 render target context 設定（resolution/texture/multi-pass blend） | ~4 |
| `cubemap` | Texture2D→CubeMap 轉換管線 | ~2 |
| `gpu-query` | GPU timestamp 查詢（Metal 替代 MTLCounterSampleBuffer） | 1 |
| `folder-image-source` | 目錄圖片動態掃描+載入（FadingSlideShow 特有） | 1 |

### Layer2d+Execute 覆蓋範圍（已知最大未建 seam）

直接被 `Layer2d+Execute` 阻擋的 op：
- `Layer2d`（主消費者）
- `DrawScreenQuad`
- `DrawScreenQuadAdvanced`

說明：Layer2d 本身是 compound op（內含 Draw/Execute/OutputMergerStage/SamplerState 等 ~23 子 op）。整個 2D 合成管線從這裡展開。解鎖後，camera3d seam 中的許多 transform op 也能部份受益（transform 系列本身只操作 context 矩陣，不直接依賴 Layer2d，但在典型場景圖中配對使用）。

### 意外/盲區

1. **_dx11 整體**：所有 `_dx11/api/` op 是 Windows DX11 直接 API 包裝（RenderTargetView、DepthStencilView、UnorderedAccessView 等 SharpDX 物件）。Mac Metal 路徑需要全套替換 seam，且這些 op 在 simple_world 的 render pipeline 底層（compound op 的 internal graph 裡大量使用），不是孤立的葉子。
2. **`_dx11` op 在 compound 內部**：上方 basic/gizmo/postfx 中所有 compound op 的 .t3 內部都引用了 `_dx11/api/` 的子 op（Draw、OutputMergerStage、SamplerState、RtvFromTexture2d 等）。實際上，`dx11-api-wrapper` seam 是 `camera3d` seam 和 `Layer2d+Execute` seam 的「底層依賴」，不能分開建。
3. **GpuMeasure**：依賴 SharpDX GPU timestamp query（DisjointQuery/Timestamp）。Mac Metal 替代是 MTLCounterSampleBuffer，API 差異很大，建議標為低優先/跳過。
4. **LoadGltfScene / DrawScene**：使用 SharpGLTF（NuGet）。Mac 可引入但 mesh pipeline、PBR dispatch 全是新的，投資大。
5. **camera3d 是最大解鎖鍵**：解鎖 camera3d → 自動解鎖 transform/ 全部（8 顆）+ gizmo/ 全部（15 顆）+ camera/ 全部（11 顆）共 ~34 顆，加上 _/ 裡的 Apply* op 等，共 ~45 顆。但 camera3d 本身依賴 `dx11-api-wrapper`（context 矩陣要被 TransformsConstBuffer/Draw 消費），所以 **建議順序：dx11-api-wrapper → camera3d → Layer2d+Execute**。

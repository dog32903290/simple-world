# SEAM_GRAPH — 接縫依賴圖 + Phase B 排序

> Phase A synthesis（2026-06-16）。合成自 8 份 census 檔（A1 state + 7 類別分類）。
> 只讀不動碼。數字以各類別檔內的表為準；跨類別加總時存疑標「約」。
> **這是 orchestrator 排 Phase B「先蓋哪塊大接縫」的決策表。**

---

> ## ★★ 2026-06-27 GROUND-TRUTH 校正（master scout，code-cited）— 本檔 §1-§5 大量過時，先讀這段
>
> 下表的「✗ 未建」標籤是 2026-06-16 Phase A 快照，**已被後續施工推翻**。每 session 照舊標差點重蓋已建縫（一夜抓到 6+ 次）。真實狀態（逐檔 file:line 驗）：
>
> | seam | §2 舊標 | **真實（2026-06-27 file:line 驗）** |
> |---|---|---|
> | `shader-graph` ~64 | ✗ 未建 | **BUILT** — `field_graph_builder.h` + 42×`field_ops_*.cpp` + `field_render.cpp`（raymarch executor）。~64 就是 field 島，已採 42/52，剩 ~10 terminal 葉。**無獨立「通用 shader-graph」縫**（inline-HLSL = `_ImageFxShaderSetup` = image-filter rail，已建）。 |
> | `point-buffer` ~90 | ✗ 未建（單塊解鎖最多） | **BUILT** — `point_graph_registry.cpp:29` 四-map registrar（cook/draw/cmd/tex）；PointCookCtx 完整（inputTextures[]/count-multiply）。point 已採 61/100，剩 ~39 葉 fan-out + 子縫 point-sim/feedback。 |
> | `Layer2d+Execute` ~37 | ✗ 未建（依賴 dx11） | **PARTIAL ~70%** — `point_ops_{execute,loop,switch,layer2d}.cpp` BUILT（Execute/Loop/Switch/Layer2d/Bloom/AfterGlow/LightRaysFx）。剩 ~12（Sketch/AsciiRender/GlitchDisplace/WaveForm/ScreenCloseUp/DetectMotion/FieldToImage）。**無 dx11 前置。** |
> | `dx11-api-wrapper` ~25 | ✗ 未建（camera3d/Layer2d 前置） | **N/A — Metal 上不是真縫。** `render_command.h:40-110` DrawKind/BlendMode enum 已 1:1 吸收 D3D11 RTV/DSV/BlendState/Viewport（ScreenQuad/Layer2d/Mesh/Clear 皆 DrawKind）。**「dx11 keystone」依賴是 FALSE**——刪/標 N/A。 |
> | `camera3d` ~50 | ✗ 未建（依賴 dx11） | **core-BUILT，零 dx11 依賴** — `field_camera.h` 全 Mat4 stack + `point_ops_camera.cpp`（Camera/OrthographicCamera）+ `resident_matrix_output_cook.cpp`。剩 ~25-29（gizmo 0/15、camera 變體、value-output Phase2/3 = 需柏為）。 |
> | `mesh-pipeline` ~49 | ✗ 未建 | **BUILT** — mesh-input seam done（`d81d705`），31 mesh 檔，meshSpecSink live，Transform/Combine/FlipNormals/RecomputeNormals 已採（`3c65ae2`）。剩 ~10-15（Draw* 需 camera3d，gltf/obj loader 需柏為）。 |
> | `feedback` ~16 | ✗ 未建 | **BUILT** — ping-pong seam（`5385e6b`）+ multi-pass executor（`15161e3`，Bloom parity）。 |
>
> **校正後依賴圖：NO dx11 keystone。三條已鋪、互不依賴的島**（field-SDF / point-buffer / Metal-render）可平行採。唯一序列脊椎 = cook-core 檔（point_graph.cpp / frame_cook / resident_eval）。
>
> **★★ 2026-06-27 二次 GROUND-TRUTH（leaf-pool 校正，code-cited）：上表的「剩 ~39 葉 fan-out / ~10 terminal 葉」是 GPU-rail op **總殘數**，不是 clean-leaf 數。逐桶對碼後 GPU-rail 三桶真實 clean-leaf 池 ≈ 5（field~1 已採 / mesh-gen-modify 0 / point ~4-5 Draw* 變體），其餘全卡 rail-specific 子 seam（cross-frame-sim / mesh-input → point / shader-graph-Field input / CPU-readback / CPU-simplex-noise / texture-into-mesh / points-into-mesh / loader / GPU-determined-output-count）。「~150-170 葉 fan-out」估算已 debunk。PointsOnImage 不是 leaf = 4-pass prefix-sum（GPU-determined-output-count）。子 seam 簇表見 `SEAM_COMPLETION_PLAN.md §0′-seam-cluster`。⚠ value-rail 葉桶（vecmath/string/numbers/image/camera-leaf/render-leaf）不在此校正，仍是真 clean fan-out。**
> **★★ 2026-06-27 三次 GROUND-TRUTH（final-autonomous-seam scout，code-cited）：上行列為「sub-seam」的 mesh-input / CPU-readback / StructuredList 三條其實是 ✅BUILT rail（非待蓋 seam）——其上 op 是葉，非 seam-gated。三條合計剩的 autonomous leaf 恰 3，現已 ✅全 BUILT @504f149（--bite 506，refuter MERGE-SAFE，autonomous leaf residue 觸底）：FindClosestPointsOnMesh（mesh-input rail，PointCookCtx::meshVtx point_graph_cook_ctx.h:88；point_ops_findclosestpointsonmesh.cpp）/ SampleCpuPoints（CPU-readback rail，production commit+wait+StorageModeShared 直讀，ReadPointColors+PointsToCPU 已建；pointlist_ops_samplecpupoints.cpp；fork=samplecpupoints-singleinput / cpupoint-reuses-swpoint）/ JoinLists（StructuredList rail，MultiInput PointList 已展開 point_graph_hostvalue_cook.cpp:64-108；pointlist_ops_joinlists.cpp，Result-only；fork=joinlists-length-deferred=Length int→value rail deferred 柏為域）。其餘撞 ✦單一柏為架構 seam = point-into-frame value-emit pass（resident value-emit 看不到 point buffer，named defer resident_matrix_output_cook.cpp:28 / node_registry_math_anim.cpp:212-228），一 seam 解鎖 GetPointDataFromList + PointToMatrix-emit + GetTextureSize + value-out-from-point 全族 ~10-15。柏為域乾淨線＝凡從 point buffer emit value / 是 camera / render / simulate。dict-ctx 不是乾淨 host rail（死在 device-IO producer）→ 柏為域。**
> 同夜其他 debunked：field-raymarch BUILT（executor）、keyframe-anim BUILT（騎 curve_animator）、GPU-compute generators parity-complete、matrix output BUILT（resident_matrix_output_cook.cpp）、dict-ctx 是 pure-host（非 device-IO）、string-wire 閘（task_32b5b6e5）已 CLOSED。
> **真實 SSOT = `docs/agent/SEAM_COMPLETION_PLAN.md`（標 ✅+commit）。** 本檔 §1-§5 以下為 2026-06-16 歷史快照，僅供依賴鏈骨架參考，「✗」欄一律以上表為準。

---

## 0. 命名統一（合併別名）

各類別 agent 對同一塊 seam 命名有出入。本檔統一如下，原別名列在括號：

| 統一名 | 合併的別名 | 說明 |
|--------|-----------|------|
| `point-buffer` | （render census 無此名，但其 `dx11-api-wrapper` 的 buffer 子集 + field census 的 `particle-system(point buffer)` 部分重疊） | GPU `StructuredBuffer<Point>`（BufferWithViews：SRV+UAV）alloc/bind/dispatch。point 類別通用貨幣 |
| `shader-graph` | particle census 的 `NEW-SEAM:shader-graph`、point census 的 `sdf-field` | ShaderGraphNode 內嵌 HLSL 程式碼圖（IGraphNodeOp + inline snippet 組合）。field/ 全島的根 |
| `Layer2d+Execute` | render census 同名、flow census 同名 | Layer2d render-target 合成 + Command/Execute dispatcher。已知最大塊 |
| `camera3d` | render census `camera3d`、flow census `context-3d`、point census `camera3d` | 3D 攝影機/投影矩陣注入 eval-context（WorldToCamera/CameraToClipSpace/ObjectToWorld） |
| `dx11-api-wrapper` | render census 同名 | DX11 RTV/DSV/UAV/SRV/BlendState/Viewport/IA/OM API 包裝的 Metal 替換層 |
| `mesh-pipeline` | mesh census `mesh-pipeline`、particle census `mesh-pipeline`、render census `_VisualizeTBN` 的 MeshBuffers | MeshBuffers 型別 + vertex/index buffer 分配 + CPU 幾何 |
| `feedback` | brief 同名、point census `feedback`、render census `feedback` | ping-pong / 時間累積 buffer。已知未建 |
| `network-io` | io census `network-io`；**`osc`/`artnet-dmx`/`camera-tracking` 的共同 UDP 底層** | TCP/UDP/WebSocket/HTTP（macOS Network.framework） |
| `cpu-upload-texture` | numbers census 同名 | CPU float/RGBA array → MTLTexture（R32/RGBA32F），不走 shader |
| `compute-readback` | image census `compute-readback`、numbers census `cpu-readback-texture`、point census `readback-cpu`、render census `RWStructuredBuffer` readback 部分 | GPU UAV/texture → CPU staging readback |
| `context-var` | flow census 同名 | eval-context 上的 typed 變數字典（Float/Int/String/Bool/Vec3/Matrix/Object Variables） |
| `cpu-point-list` | point census 同名、numbers census `structured-list-cpu` | CPU 側 StructuredList<T> 讀寫，無 GPU dispatch |
| `RWStructuredBuffer` | brief 同名 | 可讀寫 structured compute buffer（SRV+UAV，非 Point 專用） |

**注意未合併的細分**：`gradient-widget`（brief 已列）保持獨立；`asset-texture`/`mip`/`multi-image`/`source-op`/`png-decode`/`transport`/`particle-system` 全部沿用 brief/state 既有命名（已建或已知未建）。

---

## 1. 已建成 seam（踩這些 = 乾淨葉子可直接開採）

| seam | 已建? | 證據（A1） | 現有消費者 |
|------|------|-----------|-----------|
| `value-graph` | ✓ | graph.h / resident_eval_graph.cpp | ~51 math + 多數 numbers/string |
| `transport` | ✓ | transport.h:1 | Time/Anim 系列 |
| `compound` | ✓ | compound_graph.h:1 | 純 .t3 compound |
| `particle-system` | ✓ | particle_system.h:25 | ParticleSystem + 6 force |
| `image-filter` | ✓ | image_filter_op_registry.h | 25 image-filter op |
| `multi-pass` | ✓ | tex_op_cache.h:49 | FastBlur |
| `mip` | ✓ | image_filter_op_registry.h:57 | **僅 RgbTV（mip seam 近 orphan，補消費者）** |
| `asset-texture` | ✓ | image_filter_op_registry.h:63 | **僅 RgbTV** |
| `png-decode` | ✓ | platform/image_decode.h | asset-texture 內部 |
| `multi-image` | ✓ | point_graph.h:137 | Displace + DistortAndShade（2 消費者） |
| `audio-analysis` | ✓（brief 未列，A1 補） | audio_analyzer.cpp / audio_reaction.cpp | AudioReaction |
| `stateful-value` | ✓（A1 補） | stateful_value_ops.h | Damp/Spring/Ease… |

---

## 2. 未建 seam — 跨類別解鎖總表（按 解鎖數÷風險 ROI 排序）

> 「總解鎖 op 數」= 跨全部類別、把該 seam 列為**主擋**的 op 加總（次擋不重複計）。
> 風險取該 seam 解鎖 op 群的**中位**風險（最便宜的代表 op）。
> 數字含 obsolete/WIP（實際可採會略少，見 OP_BACKLOG 的 SKIP 桶）。

| # | seam | 已建? | 主擋解鎖數 | 代表風險 | 依賴的 seam | ROI 評語 |
|---|------|------|-----------|---------|------------|---------|
| 1 | **`point-buffer`** | ✅BUILT（2026-06-27 校正：point_graph_registry.cpp:29 四-map registrar）| 剩 ~39 葉 fan-out（point 已採 61/100） | R1 | 無 | **縫已建，葉 fan-out。** 非待蓋大縫 |
| 2 | **`shader-graph`** | ✅BUILT（2026-06-27 校正：field_graph_builder.h+42×field_ops_*.cpp+field_render.cpp）| 剩 ~10 terminal 葉（field 已採 42/52） | R1 | 無 | **field 島已建。** 無獨立通用 shader-graph 縫 |
| 3 | **`context-var`** | ✗ | **15**（flow Get/Set Var 全家桶 + GetForegroundColor） | R1（純字典讀寫） | 無（**與 Layer2d+Execute 無依賴，可獨立建**） | **最便宜的中型解鎖。** 投入小、零前置、解鎖 15 顆 |
| 4 | **`cpu-upload-texture`** | ✗ | **4**（ValuesToTexture/ValuesToTexture2/GradientsToTexture/CurvesToTexture） | R2 | png-decode 已有 MTLTexture 基礎 | **Metal API 現成**（MTLTexture replaceRegion）。低投入，補 audio-vis 工具鏈；GradientsToTexture 是多顆 gradient op 的底層 |
| 5 | **`Layer2d+Execute`** | ✅PARTIAL~70%（2026-06-27 校正：point_ops_{execute,loop,switch,layer2d}.cpp）| 剩 ~12（Sketch/AsciiRender/GlitchDisplace/WaveForm/ScreenCloseUp/DetectMotion/FieldToImage） | R3 | **無 dx11 前置（FALSE 依賴已刪）** | 核心已建，剩進階 fx 葉 |
| 6 | **`dx11-api-wrapper`** | ❌N/A（2026-06-27 校正：Metal 上非真縫）| — | — | — | **render_command.h:40-110 DrawKind/BlendMode 已 1:1 吸收 D3D11 RTV/DSV/BlendState/Viewport。「dx11 keystone」依賴是 FALSE。** |
| 7 | **`camera3d`** | ✅core-BUILT（2026-06-27 校正：field_camera.h Mat4 stack+point_ops_camera.cpp+resident_matrix_output_cook.cpp）| 剩 ~25-29（gizmo 0/15、camera 變體、value-output Phase2/3=需柏為） | R1 | **零 dx11 依賴** | 矩陣 bridge 已蓋；gizmo/變體 fan-out |
| 8 | **`mesh-pipeline`** | ✅BUILT（2026-06-27 校正：mesh-input seam d81d705，31 mesh 檔，meshSpecSink live）| 剩 ~10-15（Draw* 需 camera3d，gltf/obj loader 需柏為） | R1 | 無 | Transform/Combine/FlipNormals/RecomputeNormals 已採（3c65ae2） |
| 9 | **`feedback`** | ✅BUILT（2026-06-27 校正：ping-pong seam 5385e6b + multi-pass executor 15161e3 Bloom parity）| 剩進階 cross-frame 消費者（feedback-advanced 桶） | R3 | — | KeepPreviousFrame/SwapTextures 已採 |
| 10 | **`gradient-widget`** | ✗ | **~14**（image 12 + field 2 次要） | R2 | 部分配 `cpu-upload-texture`（GradientsToTexture） | 柏為 authoring 域（畫色帶）；解鎖漸層生成器群 |
| 11 | **`source-op`** | ✗ | **3**（LoadImage/ImageSequenceClip/BuildAsciiFontSorting） | R1（LoadImage 本身） | png-decode 已有；差 path-watcher + async load | LoadImage 是乾淨消費者（decoder 已建），差 source 路徑 |
| 12 | **`network-io`** | ✗ | **~9 主擋 + ~14 共用底層**（TCP/UDP/WS/HTTP；**osc 2 / artnet-dmx 8 / camera-tracking 4 的 UDP 底層**） | R1（RequestUrl 輕量） | 無（macOS Network.framework） | **建通用 UDP 原語＝osc/artnet/camera-tracking 工作量大減** |
| 13 | `midi` | ✗ | 10 | R2 | 無（macOS CoreMIDI） | 柏為域（VJ/演出）；獨立子系統 |
| 14 | `video-input` | ✗ | 9 | R3 | 無（macOS AVFoundation） | 投資大報酬中；Windows DirectShow 深綁，macOS 全替換 |
| 15 | `compute-readback` | ✗ | **~12**（image 5 + point 3 + numbers 1 + render 2 + flow 1） | R2-R3 | 無（Metal staging blit） | GPU→CPU 讀回；JumpFloodFill/SortPixelGlitch/PointsToCPU 等 |
| 16 | `RWStructuredBuffer` | ✗ | **~7**（particle 2 + render 2 + numbers 1 + flow 2） | R2-R3 | 部分配 dx11-api-wrapper | 可讀寫 structured buffer；Verlet/Reconstructive force |
| 17 | `cpu-point-list` | ✗ | **~15**（point _cpu 6 + helper + io + numbers structured-list 3） | R1 | 配 point-buffer 的 ListToBuffer 上傳 | **比 point-buffer 容易**，純 CPU；值得早建 |
| 18 | `pbr-material` | ✗ | ~10（render shading） | R2 | 依賴 camera3d | 3D 材質系統；3D 鏈後段 |
| 19 | `lighting` | ✗ | ~8（render shading） | R1 | 依賴 camera3d | 點光/霧/遮蔽 |
| 20 | `depth-buffer` | ✗ | ~8（render postfx） | R3 | 依賴 camera3d + dx11 | GodRays/SSAO/DoF/MotionBlur |
| 21 | `bitmapfont` | ✗ | ~7（render Text 系列） | R3 | 依賴 Layer2d+Execute + asset | 文字渲染；BmFont asset 載入 |
| 22 | `lens-flare` | ✗ | ~9（render） | R2 | 依賴 camera3d + Layer2d | 可評估跳過 |
| 23 | `shadow-map` | ✗ | ~5（render） | R3 | 依賴 camera3d + depth-buffer | 陰影管線 |
| 24 | `dict-context` | ✗ | 4（numbers Select*FromDict） | R2 | 無 | TiXL Dict<T> 子系統，小眾 |
| 25 | `data-recording` | ✗ | 4（io LoadDataClip/SimulateIoData/DataRecording/MidiRecording） | R2-R3 | 配 midi/osc | MIDI/OSC 錄製 DataSet |
| 26 | `artnet-dmx` | ✗ | 8（io DMX 族） | R2 | **依賴 network-io（UDP）** | 燈控；UDP 上協定解碼 |
| 27 | `camera-tracking` | ✗ | 4（io FreeD/PosiStage） | R2 | **依賴 network-io（UDP）** | 相機追蹤；UDP 上協定解碼 |
| 28 | `audio-playback-op` | ✗ | 5（io AudioPlayer 族） | R2 | 無（macOS AVAudioEngine；sw 目前 soundtrack-only） | per-op 音源池，非 soundtrack |
| 29 | `keyboard-mouse` | ✗ | 3（io Keyboard/Mouse） | R1 | sw imgui io 已接，差 op 橋接 | 輸入 op |
| 30 | `serial` | ✗ | 3（io serial） | R2 | macOS /dev/tty.* | 串列輸出 |
| 31 | `osc` | ✗ | 2（io Osc） | R2 | **依賴 network-io（UDP）** | OSC 解碼 |
| 32 | `int-cbuffer` | ✗ | 1（IntsToBuffer） | R2 | 配 dx11-api-wrapper | 動態 constant buffer |
| 33 | `gltf-scene` | ✗ | ~5（render scene 2 + mesh LoadGltf + shading dispatch） | R3 | 依賴 mesh-pipeline + camera3d + pbr | SharpGLTF；投資大報酬低 |
| 34 | `obj-loader` | ✗ | 3（mesh LoadObj/LoadObjEdges + point LoadObjAsPoints） | R2 | 配 mesh-pipeline / cpu-point-list | OBJ parser CPU |
| — | 其他單顆 NEW-SEAM | ✗ | 各 1-2 | — | — | trigger-dirty(2)/keyframe-edit(2)/cubemap(2)/spatial-hash-grid(1)/gpu-query(1)/ableton-link(1)/svg-loader+font-line(3)/texture-format-convert(1)/texture-array(1)/cubemap-prefilter(1)/fft-compute(1)/network-fetch(1)/svg-rasterize(1)/skillquest(5)/raw-srv(1)/sketch-context(2)/cpu-point-array(2)/beat-timing-details(1)/render-state(4)/point-sprite(3)/gamepad(1)。多數低優先 |

---

## 3. Seam 間依賴鏈（不能跳過的前置）

```
dx11-api-wrapper  (最底層 Metal API 替換層, ~25)
   ├──> camera3d            (~50)  ── 3D render graph 根
   │       ├──> transform/  全 8 顆
   │       ├──> gizmo/       全 15 顆
   │       ├──> camera/      全 11 顆
   │       ├──> pbr-material (~10) ──> gltf-scene (~5)
   │       ├──> lighting     (~8)
   │       ├──> depth-buffer (~8) ──> shadow-map (~5)
   │       └──> lens-flare   (~9)
   └──> Layer2d+Execute      (~37) ── 2D 合成 + Command dispatcher
           ├──> flow/ Execute 族 (16)
           ├──> image/ Glow/Bloom/AfterGlow… (10)
           ├──> render basic (DrawScreenQuad…)
           ├──> point/ draw 全 16 顆 (需 point-buffer + Layer2d+Execute)
           └──> bitmapfont (~7, +asset)

shader-graph  (~64, 自足無前置)
   └──> field/ 全 60 顆
   └──> raymarch (~6, field render/use 子集) ──> 部分需 camera3d
   └──> particle CustomForce/FieldDistanceForce… (4)
   └──> mesh ColorVerticesWithField… (4 次要)

point-buffer  (~90, 自足無前置)
   ├──> point generate/modify/sim/transform/combine 全島
   ├──> cpu-point-list (~15) ── ListToBuffer 上傳橋
   └──> feedback (point 版 KeepPreviousPointBuffer)

network-io  (UDP/TCP 通用底層)
   ├──> osc          (2)
   ├──> artnet-dmx   (8)
   └──> camera-tracking (4)   ← 三族共用 UDP，先建 network-io 工作量大減

mesh-pipeline  (~49, CPU 幾何自足)
   └──> mesh draw 子集 ── 次要依賴 camera3d + Layer2d+Execute
```

**三條互不依賴的獨立島**（可平行蓋）：① `dx11-api-wrapper → camera3d/Layer2d` 鏈、② `shader-graph → field` 島、③ `point-buffer → point` 島。`network-io` 是第四條獨立 IO 鏈。

---

## 4. Phase B 建議建設順序（最高 ROI 先）

每塊註明「配哪顆 op 當驗證」（brief 鐵律：每塊接縫配一顆真 op，否則變 orphan，如現在的 mip/asset-texture 近 orphan）。

| 序 | seam | 解鎖數 | 風險 | 前置 | **配驗證 op** | 理由 |
|----|------|-------|------|------|--------------|------|
| **B1** | `point-buffer` | ~90 | R1 | 無 | **RadialPoints / GridPoints**（純數學 compute，golden 易對 TiXL） | 單塊解鎖最多；自足無前置；驗證 op 數學直白。蓋完立刻開 ~90 顆乾淨葉子 |
| **B2** | `shader-graph` | ~64 | R1 | 無 | **SphereSDF / BoxSDF + Render2dField**（generator 驗 graph 組裝，Render2dField 驗 raymarch 接縫） | 解鎖整個 field 島；自足；60 顆 R1/R2 同時可採 |
| **B3** | `context-var` | 15 | R1 | 無 | **SetFloatVar → GetFloatVar 對拍**（同一 graph 寫讀驗字典） | 最便宜中型解鎖；零前置；與 Layer2d 解耦 |
| **B4** | `cpu-upload-texture` | 4 | R2 | 無（Metal 現成） | **GradientsToTexture**（驗 RGBA32F 上傳 + 是多顆 gradient op 底層） | Metal replaceRegion 現成；補 gradient/audio-vis 鏈 |
| **B5** | `dx11-api-wrapper` | ~25 | R2 | 無 | **ClearRenderTarget / Draw**（最小 render call 驗 Metal 替換） | camera3d + Layer2d 的共同前置，**必先蓋**才能解 3D/2D 大島 |

**B5 之後**：`Layer2d+Execute`（解 ~37，配 DrawScreenQuad 驗證）與 `camera3d`（解 ~50，配 Transform 驗證）兩條 3D/2D 大鏈一起鋪；`mesh-pipeline`（配 QuadMesh 純 CPU 幾何）可與 B1/B2 平行（自足無前置）；`network-io`（配 UDPInput round-trip）獨立鏈任意時點插入。

---

## 5. Phase B/C 策略建議

**關鍵數字**：census 顯示 **~285 顆 READY-LEAF-NOW**（踩已建成 seam、不需蓋任何新 seam 就能開採；扣掉已 port 的 ~112 顆後仍有 **~173 顆全新可採葉子**）。其中 numbers/string TRIVIAL 占 ~140（純 value-graph/transport，R1，零風險）。

**推薦（一個，不並列）：Phase C 立刻並行啟動，與 Phase B 大 seam 平行織。**

理由：
1. **存量夠大、夠乾淨**：~173 顆全新葉子全部踩**已驗證**的 seam（value-graph/transport/image-filter/multi-image/asset-texture/particle-system 都已有 selftest 綠 + 已 port 消費者）。這不是投機——是把已鋪好的承重線上的空位填滿。等 Phase B 才開採＝讓已建地基閒置。
2. **零相互阻塞**：Phase C 的葉子開採（numbers/string/image-filter/SDF-無關 leaf）與 Phase B 的 seam 建設**踩不同檔**（葉子改 node_registry/point_ops，seam 改引擎底層），可並行 lane 互不撞。
3. **風險對沖**：Phase B 大 seam（point-buffer/dx11/Layer2d）是 R2-R3 重工，可能卡關；Phase C 葉子是 R1 快贏，維持批次節奏、不讓 session 空轉。
4. **驗證紀律已就位**：每顆葉子有 golden+refuter+scenario 對 TiXL 機器驗證的工法（Cut 51-59 已跑順），不需柏為下場。

**最該先蓋的 2-3 塊 Phase B seam**：
1. **`point-buffer`**（解 ~90，自足，R1 驗證 op）— 解鎖數碾壓其他，且是 point 島唯一地基。
2. **`shader-graph`**（解 ~64，自足，R1 驗證 op）— 解鎖整個 field 島，與 point-buffer 無依賴可同時動。
3. **`dx11-api-wrapper`**（解 ~25 但更重要是**前置**）— 不直接解鎖最多，但 camera3d(~50) + Layer2d(~37) 都卡它，是 3D/2D 大島的唯一鑰匙；早鋪解後續最大批。

**並行編排建議**：Phase C 葉子開採（Sonnet 機械 lane，吃 numbers/string/image-filter leaf）＋ Phase B 三塊地基（Opus 承重 lane，point-buffer / shader-graph / dx11-api-wrapper）同跑。numbers/string 共 ~140 顆 TRIVIAL 是最大、最安全的並行燃料。

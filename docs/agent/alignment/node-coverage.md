# 節點/op 覆蓋缺口 — TiXL parity gap + 規格

> 來源:tixl-parity-gap-audit workflow(2026-06-22,6 區 12 agent 唯讀清查;每條 gap 對 simple_world 實際碼 verify 過真偽)。
> 計數:真 gap 2 · 部分 7 · stale已做 0。

## 真 gap(已驗證確實未做)

### [important] io/ 全島 73 op 未港（MIDI/OSC/Artnet/HTTP/video/file/serial 輸入輸出）

- **對齊規格**:保留原 spec：柏為演出/VJ 域。需蓋 network-io（macOS Network.framework，一次解鎖 osc 2 + dmx 8 + tcp/udp/websocket + http 1）；midi（CoreMIDI，10 op）；video-input（AVFoundation，8 op，投資大）。建議優先 network-io（解鎖最多）+ RequestUrl（最輕量 R1 入口）。對齊：OSC/Artnet 封包 byte-format 對 TiXL 協定碼。
- **驗證證據**:grep -rniE 'MidiInput|OscInput|SendOsc|RequestUrl|ArtnetOutput|DmxOutput|WebSocketClient|SerialInput|AudioFileInput' 在整個 app/src/（非僅 runtime）= 完全空。確認零實作。audio_analyzer.cpp/audio_reaction.cpp 是 audio-analysis，非 io/audio 的 player/input op。

### [polish] census 兩張表（OP_BACKLOG/SEAM_GRAPH）DONE 桶 stale，DONE 數已從~112 翻倍

- **對齊規格**:保留 spec：refresh census：(1) 重算桶 A DONE（用 leaf op 檔 + node_registry 內聯 specs 精確點名，現約 300+ 非 112）；(2) SEAM_GRAPH §1『已建 seam』補列 stateful-string / field-sdf-codegen(renderField2d) / point-buffer-proven / camera3d / Layer2d / context-var（這些 seam 在驗證中發現都已落地但 census 未列）；(3) 桶 B READY-LEAF 扣掉已港。否則 census 當排程表會把已港的當待採、把已建 seam 當未建（本次驗證即抓到 render/flow 兩條 gap 因 census/債帳視角而誤判為 missing）。不影響 TiXL parity 本身，但影響開採排程準確度。
- **驗證證據**:OP_BACKLOG.md:9 桶 A 仍寫『DONE（已 port，~112 顆）』。實際 leaf op 檔點名：value_op 79 + stateful ~21（10 檔）+ string 25 + field_ops 37 + mesh_ops 7 + point_ops 124 + pointlist 6 + colorlist 5 + gradient 4 + floatlist 1 + host_scalar 2，再加 node_registry_*.cpp 內聯 specs（math arithmetic/anim/logic/vector/time/draw/generators/particle/contextvar）。光 leaf 檔已 ~300+，registered NodeSpec 遠超 112。census 為 2026-06-16 快照，已嚴重低估約 2.5-3 倍。

## 部分完成(做了一半)

### [core] render/ 全島 155 op 未港（3D 渲染圖 + Layer2d 合成 + Execute 命令系統）

- **剩什麼**:gap 列的三塊 seam 有兩塊已建：camera3d seam（#2 WorldToCamera/CameraToClipSpace 注入）＝已建（Camera op stamp + RenderTarget 組 ObjectToClipSpace）；Layer2d render-target 合成（含 blend mode）＝已建。剩餘真缺：(a) Execute/ExecuteOnce 命令 dispatcher（flow 控制流，grep Execute 仍 0 NodeSpec）；(b) render/_dx11(43) API 包裝、render/shading(27) PBR、render/gizmo(15)、render/transform(8 純矩陣 R1)、camera(11 OrbitCamera/ActionCamera 等變體—現只有 v1 單 Camera 無 roll/lensShift)。即『155 op』裡 5 顆左右的承重骨架已落地，剩約 150 個下游 op（含矩陣 R1 可大量開採）待採。severity 仍 core（量最大），但『missing/grep=0』描述失真，正確狀態＝partial：seam 骨架已過，是 fan-out 開採階段非從零蓋 seam。
- **現況證據**:上一棒宣稱『grep DrawMesh/Layer2d/Execute/PerspectiveCamera/WorldToCamera 全 0 NodeSpec』= 假。實證：(1) Camera op 已註冊 node_registry_draw.cpp（{"Camera","Camera"}+FieldOfView/AspectRatio）+ point_ops_camera.cpp registerCameraOp() 實作 push/pop WorldToCamera/CameraToClipSpace（per-item stamp，註解明列 PerspectiveFovRH/LookAtRH 公式）。(2) Layer2d op 已活：point_ops_layer2d.cpp cookLayer2d()/registerLayer2dOp()，RenderTarget executor point_ops_rendertarget.cpp:374 DrawKind::Layer2d 用 ObjectToClipSpace 投影合成 + blend mode。(3) DrawMeshUnlit 已註冊（node_registry_draw.cpp:165 + point_ops_drawmeshunlit.cpp，TiXL Lib.mesh.draw.DrawMeshUnlit，depth-tested）。(4) RenderTarget/DrawScreenQuad+BlendMode(Normal/Additive)/ClearRenderTarget 都註冊。CmdCookCtx (point_graph.h:100-101) 帶 objectToCamera/cameraToWorld 矩陣欄位。

### [important] mesh/ 51 op 僅港 7（純 CPU 幾何），draw(8)+loaders+多數 modify 缺

- **剩什麼**:保留 spec，微調：已港=純 CPU 幾何 modify 子集 7 顆。缺 (a) mesh/generate 多數 generator（Cube/Cylinder/Sphere/Torus/Icosahedron 純 CPU R1，可立即大量開採）；(b) LoadObj/LoadGltf（需 mesh-loader seam）；(c) mesh/draw 剩 7 顆變體（DrawMeshUnlit 已有，camera3d 已建可承接其餘）；(d) mesh/modify 的 Deform/Displace/Warp（GPU vertex compute seam）。優先補 generate 純 CPU primitive。
- **現況證據**:確認 mesh_ops_*.cpp = 7 檔（CombineMeshes/FlipNormals/NGonMesh/QuadMesh/RecomputeNormals/TransformMesh/TransformMeshUVs），mesh_op_registry.cpp 用 meshSpecSink() 自登記。mesh/generate primitive（Cube/Sphere/Torus/Cylinder/Icosahedron）grep 在 mesh 域 = 0（只在 point modify/generators 的 enum 字串出現，非 mesh op）。LoadObj/LoadGltf = 0。注意修正：mesh/draw 的 DrawMeshUnlit 已港（見 render-island），非全 8 顆缺；現缺 7 顆 draw 變體。

### [important] point/ 135 op：draw 變體、PointSimulation 核心、CPU-list、mesh-sampling 仍缺

- **剩什麼**:保留 spec：基本 draw 已有；缺進階 draw 變體（Ribbons/Tubes/DOF/Shaded）— 注意 camera3d seam 現已建可直接承接，建議可立即補。point/sim 的 PointSimulation 仍需 RWStructuredBuffer + ping-pong feedback seam（真缺）。對齊對 TiXL .hlsl compute + golden CPU-readback。
- **現況證據**:point_ops_*.cpp = 124 leaf 檔（drawpoints2/drawlines/drawbillboards/drawclosedlines/drawlinesbuildup/drawscreenquad/layer2d/drawmeshunlit/camera/rendertarget 都在），pointlist_ops_*.cpp = 6（CPU-list）。進階 draw 變體 grep（DrawRibbons|DrawTubes|DrawPointsDOF|DrawConnectionLines|DrawLinesShaded|DrawMovingPoints）= 0 → 確認缺。point/sim grep（PointSimulation|ApplyRandomWalk|GrowStrains|SimFollowMeshSurface|SimPointMeshCollisions）= 0 → 確認缺。基本 draw + CPU-list 已有。

### [important] field/ 60 op 港 37，剩 23 全卡在 field-render/use seam（raymarch/SDFToColor/shadergraph）

- **剩什麼**:保留 spec 但修正 seam 狀態：23 個下游 NodeSpec（field/use + field/render + 4 顆剩餘 generate + adjust SetSDFMaterial）確實未註冊＝真缺。但『全卡在未建 seam』失真—shader-graph inline-MSL codegen + 2D field render（assembleFieldMSL + renderField2d）已建並有消費者（CustomSDF）。剩餘真缺：(1) raymarch（SDF ray-march 成像素，對 TiXL raymarch.hlsl 步進公式）；(2) SDFToColor/SdfToVector field→color 映射 op 化；(3) 把 renderField2d 包成 Render2dField NodeSpec。算 partial—seam 半建，是把現有 codegen pipeline 接出成 op + 補 raymarch。
- **現況證據**:field_ops_*.cpp = 37（與 gap 宣稱一致）。field/use+render 消費者 NodeSpec grep（RaymarchField|SDFToColor|RaymarchPoints|GenerateShaderGraphCode|Render2dField|SampleFieldPoints|ApplyVectorField|SdfToVector|SetSDFMaterial）= 0 → 23 顆消費者確實未註冊為 op。BUT 重要修正：shader-graph codegen seam 部分已建—field_render.cpp renderField2d() + field_graph.cpp assembleFieldMSL()（『inline MSL codegen』，PSO src-cache 鍵 srcHash 零 per-frame compile），field_node_registry.h/field_ops_customsdf.cpp 已呼叫 renderField2d。即 gap 宣稱『GenerateShaderGraphCode 撞 Metal 預編譯哲學需設計』的核心 seam（field→MSL 字串組裝 + render）其實已落地。

### [polish] numbers/ 236 op：list/dict/iterator/keyframe/bpm 整批未港（純值已大量補上）

- **剩什麼**:保留 spec：純標量/向量缺口已小（已驗證大量已港）。剩三組真缺需 seam：(1) list/dict 家族需 structured-list-cpu + dict context-var seam（IterateList/Iterator 還需 iterator 求值上下文）；(2) keyframe 進階（FindKeyframes/SetKeyframes/EaseKeys）需 curve-edit seam；(3) BPM/SetTime 系列(R2)需 transport 寫回（現只讀）。GridPosition/TransformVec3/MulMatrix/Vector3Gizmo/RemapValues 是踩 value-graph 乾淨葉子可立即補（R1）。
- **現況證據**:value_op_*.cpp = 79 + stateful_value_ops_*.cpp（10 檔，~21 op）。純值大量已港（int: clampint/compareint/inttofloat/modint/multiplyint/multiplyints/pickint/maxint/minint/intadd/intdiv/subints/sumints/isinteven/getaprime；bool: all/and/any/or/not/xor/pickbool；vec: addvec2/3/dotvec2/3/4/cross/lerp/normalize/rotate/pickvector2/3/vector4components；color/gradient: hsbtocolor/hsltocolor/oklchtocolor/blendcolors/pickcolor/rgbatocolor/samplegradient）。list/dict/iterator grep（IterateList|Iterator|GetIteration|SelectFloatFromDict|SelectVec3FromDict|AmplifyValues|AnalyzeFloatList|RemapFloatList|SmoothValues|SumRange）= 0 → 確認缺。

### [polish] image/ 127 op：fx/generate/use 仍有大批未港（filter 主力已港 25）

- **剩什麼**:保留 spec：image-filter seam 已建，fx/generate 乾淨單輸入葉子可繼續沿 seam 大量開採（READY-LEAF）。image/use 的 LoadImage 需 source-op seam（png-decode 已有，差 path-watcher+async）；image/analyze 的 JumpFloodFill/SortPixelGlitch 需 compute-readback seam；image/use Layer 合成需 Layer2d（已建，可接）。優先續採 image/fx + image/generate。對齊 golden d=0 飽和 plateau。
- **現況證據**:23 個 point_ops_*.cpp 用 imageFilterSpecSink（blur/crop/channelmixer/dither/raster/boxgradient/tileablenoise 等），image-filter seam 確實已建可續採。image/use LoadImage 與 image/analyze（JumpFloodFill|SortPixelGlitch|ImageSequenceClip）grep = 0 → 確認缺。

### [important] flow/ 35 op 整類未港（context-var 字典 + Execute/Loop 控制流）

- **剩什麼**:保留 spec 但修正：context-var seam（SEAM_GRAPH §2 #3）＝已建，且 Get/SetFloatVar + Get/SetIntVar 已採。剩餘真缺：(a) 其餘 typed var（Get/Set Bool/String/Vec3/Matrix/Object Var + GetForegroundColor，約 9-11 顆，踩已建 ContextVarMap 即可，R1 立即可採）；(b) 控制流 Execute/ExecuteOnce/ExecRepeatedly/Loop/Once/Switch/TimeClip/BlendScenes/LogMessage/LoadSoundtrack（依賴 Execute 命令 dispatcher，與 render-island Execute 同批，真缺）。不再是『整類未港』—字典骨架 + Float/Int var 已落地。
- **現況證據**:上一棒宣稱『grep GetFloatVar/SetFloatVar/Loop/Switch 全 0 NodeSpec』= 部分假。實證：node_registry_math_contextvar.cpp 已註冊 GetFloatVar/SetFloatVar/GetIntVar/SetIntVar（14 個 var 相關 match）+ BlendValues/FallbackDefault；stateful_value_ops_context_vars.cpp 實作 + ContextVarMap 已是 stateful op 求值簽名的標準參數（stateful_value_ops_*.cpp 每顆 step fn 都帶 ContextVarMap*）。即 context-var seam（在 eval-context 上的 typed 變數字典）＝已建，Float/Int 兩型已採。控制流 grep Execute/Loop/Switch = 0 → 確認缺。

## verify 戳破的 stale(survey 說沒做、其實做了)

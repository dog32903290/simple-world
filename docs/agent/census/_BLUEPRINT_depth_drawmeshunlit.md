# Build Blueprint: depth seam + DrawMeshUnlit (Cut 99 scout) — first 3D mesh on screen

Scout verdict: GO, NOT PBR-blocked. DrawMeshUnlit is genuinely unlit IN TiXL (no fork). Build depth seam + DrawMeshUnlit TOGETHER (depth seam's only faithful consumer is the depth-tested mesh; Layer2d is correctly depth-OFF in TiXL so can't prove depth without a fork).

## GO/NO-GO (decided): DrawMeshUnlit, zero PBR
- DrawMeshUnlit.t3 children: VertexShader c79222cf + PixelShader f9014b64 → both `Lib:shaders/3d/mesh/mesh-DrawUnlit.hlsl` vsMain/psMain.
- psMain (mesh-DrawUnlit.hlsl:68-108) non-cubemap branch = `albedo * Color * vertexColor`; albedo=BaseColorMap2.Sample (white fallback), vertexColor=1 when UseVertexColor=false(default), UseCubeMap default false. NO ComputePbr/PointLights/IBL/BRDF. `#include pbr.hlsl` only for PbrVertex struct, no lighting fns called.
- cbuffers: b0 Transforms (have ObjectToClipSpace), b1 Params (Color/AlphaCutOff/BlurLevel/UseCubeMap/UseVertexColor). textures: t0 PbrVertices, t1 FaceIndices (= our mesh buffers), t2 BaseColorMap2 (white fallback), t3 CubeMap (default off).
- DrawMeshUnlit.t3 wires LoadImage(Lib:images/basic/white.png)→UseFallbackTexture→t2. Default (no Texture wired): albedo=white(1,1,1,1), vertexColor=1, AlphaCutOff=0 → psMain = Color (default (1,1,1,1)).
- ★Contrast DrawMesh (PBR, mesh-Draw.hlsl:208-247 ComputePbr+b3 PointLights+b4 PbrParams+t4-7 RSMO/Normal/PrefilteredSpecular/BRDF+{FIELD_CALL}) = BLOCKED like raymarch3D → DEFER.

## Depth seam (small, local, point_ops_rendertarget.cpp)
TiXL DrawMeshUnlit.t3 DepthStencilState 61714c96: EnableZWrite=true, Comparison=LessEqual, EnableZTest=true. Rasterizer 6e672779: CullMode=Back, FrontCounterClockwise=true.
- render-pass (cookRenderTarget ~:127-143): alloc depth texture MTLPixelFormatDepth32Float same W×H as c.output, StorageModePrivate, TextureUsageRenderTarget; pass->depthAttachment() LoadActionClear clearDepth=1.0 StoreActionDontCare.
- PSO (makeDrawPSO ~:52-93): add depthAttachmentPixelFormat(Depth32Float) param for the mesh PSO. ★GOTCHA: once depth is attached, EVERY PSO in the pass must declare depthAttachmentPixelFormat — give non-mesh draws (Points/Lines/Billboard/ScreenQuad/Layer2d) a depth-DISABLED state (compare always, write off) so 2D composites unaffected; re-run their goldens.
- depth-stencil state (new, before mesh draw): MTLDepthStencilDescriptor depthCompareFunction=LessEqual, depthWriteEnabled=true; enc->setDepthStencilState. + enc->setFrontFacingWinding(CounterClockwise) + setCullMode(Back) (match FrontCounterClockwise=true + Cull Back).
- ★clip-z: perspectiveFovRH (field_camera.h:60-62) already emits D3D [0,1] z (M33=far/(near-far)), matches Metal + LessEqual → NO remap. NOT entangled with MSAA (executor single-sample).

## DrawMeshUnlit op wiring
- mesh nodes cook into p_->meshVtxBuf/meshIdxBuf (point_graph.cpp:553-575, debugCookedMesh :151). 
- new DrawKind::Mesh=6 (render_command.h:38-52) + RenderDrawItem += const MTL::Buffer* meshVtx, meshIdx, uint32_t meshIndexCount (append after existing fields). Reuse existing objectToClipSpace[16] + camera-stamp fields (:128-135) verbatim (executor composes ObjectToClipSpace :282-309).
- mesh terminal (point_graph.cpp:647-654, currently cook-and-clear) extended to a draw path OR a new DrawKind::Mesh item borrowing vtx+idx buffers + camera context.
- VS new mesh_draw_unlit.metal: index-buffer-driven via SV_VertexID/[[vertex_id]]: faceIndex=vid/3, faceVertexIndex=vid%3, vertex=PbrVertices[FaceIndices[faceIndex][faceVertexIndex]] (HLSL vsMain:46-65). Bind SwVertex buf + SwTriIndex buf (sw_mesh.h:23-37 MSL-shareable). Position = mul4row(ObjectToClipSpace, float4(vertex.Position,1)) (reuse draw_quad_xf.metal:37-45). Draw = drawPrimitives(Triangle, 0, meshIndexCount*3) — 3 verts/face, SV_VertexID-driven, NO Metal index buffer (matches .t3 Draw child vertexCount=faceCount×3 via MultiplyInt ×3 child 0e36b565).
- PS mesh_draw_unlit_fs = sample t2(white)×Color×vertexColor; default → Color. v1: bind in-code 1×1 white texture for t2 (★named fork: dodges image-decode/asset seam, byte-identical to white.png fallback for default no-Texture case; real Texture input = follow-up).
- camera: reuse defaultLayerCameraForward(aspect) (executor :125) + objectToClipSpace (:308); Camera op (Cut 96-98) stamps per-item → mesh items inherit free.
- winding: sw mesh_ops_quadmesh.cpp Int3 order (QuadMesh.cs:106-107) + FrontCounterClockwise=true + default camera looking -z → CCW=front → setFrontFacingWinding(CounterClockwise).

## GOLDEN (deterministic plateaus, single-sample, host-computed projected positions):
- Tooth A (mesh renders flat color): QuadMesh (Cut 90) → DrawMeshUnlit Color=(1,0,0,1) → RenderTarget default camera+depth. Quad at z=0 facing camera; host-project center via objectToClipSpace(identity, defaultW2C, defaultC2C) → interior pixel. Assert interior=(255,0,0)[=Color since albedo=white], far-corner=clear. injectBug=drop ObjectToClipSpace mul → mis-project → interior=background → RED. (also catches winding: vanished quad → interior=background.)
- Tooth B (depth occlusion, THE depth tooth): two QuadMesh, near z=+0.5 Color=red, far z=-0.5 Color=green, overlapping in screen. LessEqual+ZWrite → overlap pixel=red (near occludes far). injectBug=disable depth-stencil state → draw order/z-fight → overlap≠red → RED. Proves depth does occlusion not just attached.

## Scope: ONE cut (depth seam + DrawMeshUnlit coherent = first 3D mesh; depth seam non-orphan since mesh is its faithful consumer). If build agent finds it too big, split: depth seam (proven by Tooth B mechanism) then DrawMeshUnlit — but Layer2d can't prove depth (depth-off in TiXL=fork), so the mesh IS needed for a faithful depth tooth → prefer one cut.
## Forks: in-code 1×1 white t2 (byte-identical default), winding CCW, DrawMesh(PBR) deferred. Risk: all-PSOs-declare-depth-format (give non-mesh depth-disabled state, re-run their goldens); winding (golden catches vanished quad); clip-z non-risk (already D3D [0,1]).
## Critical files: point_ops_rendertarget.cpp (depth attach+state+PSO format), render_command.h (DrawKind::Mesh+mesh fields), new mesh_draw_unlit.metal + point_ops_drawmeshunlit.cpp+golden, point_graph.cpp (mesh terminal draw path), sw_mesh.h (vertex layout), field_camera.{h,cpp}, draw_quad_xf.metal (mul4row template). TiXL: mesh/draw/DrawMeshUnlit.{cs,t3}, Assets/shaders/3d/mesh/mesh-DrawUnlit.hlsl.

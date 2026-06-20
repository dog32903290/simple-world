// runtime/render_command — the Command stream's currency (TiXL's Slot<Command>).
//
// TiXL splits point rendering into three slot types: BufferWithViews (raw GPU point
// data) -> Command (deferred render instruction) -> Texture2D (displayable image).
// This header is the middle one. A point op outputs a buffer (already: MTL::Buffer of
// SwPoint). DrawPoints turns that buffer into a RenderCommand. RenderTarget executes
// the RenderCommand into a Texture2D, and is where resolution is pinned.
//
// Why a DATA RECORD, not a closure (TiXL's {PrepareAction,RestoreAction}):
// TiXL's Prepare/Restore inject into an immediate-mode DX11 device context. We are
// retained-mode — each RenderTarget opens its own commandBuffer→encoder→endEncoding,
// so Prepare/Restore are render-pass boundaries owned by the EXECUTOR, not carried by
// the op. And compositing (blend layering two point bags) is just chaining: a record
// chain appends with vector::insert (O(n), zero GPU, zero buffer copy) and stays
// introspectable (debug sees how many draws, each draw's count). N closures would each
// open their own pass and clear each other out.
//
// Zone: runtime leaf (no upward deps). Pure CPU container — borrows buffer pointers,
// never retains. A RenderCommand lives shorter than one cook() (single-frame memo);
// it must NOT be stored across frames (the borrowed buffers are PointGraph-owned and
// reused next frame).
#pragma once
#include <cstdint>
#include <vector>

namespace MTL {
class Buffer;
class Texture;
}  // namespace MTL

namespace sw {

// How a point bag is rasterized. The Command stream is shape-agnostic (TiXL splits the
// three draw ops as distinct Slot<Command> producers — DrawPoints/DrawLines/DrawBillboards);
// the EXECUTOR (cookRenderTarget) reads this discriminator to pick the PSO + primitive. Kept
// in the data record (not three executors) so a chain can MIX kinds in one render pass, and
// so the cmd ops stay pure data-stampers (zero render code), exactly like DrawPoints.
enum class DrawKind : uint32_t {
  Points = 0,      // draw_points_vs: 1 point-prim per Point (PrimitiveTypePoint)
  Lines = 1,       // draw_lines_vs: 6-vert screen-space quad per segment i→i+1 (PrimitiveTypeTriangle)
  Billboards = 2,  // draw_billboards_vs: 6-vert camera-facing quad per Point (PrimitiveTypeTriangle)
  ScreenQuad = 3,  // draw_screenquad_vs: 6-vert clip-space quad sampling srcTexture (TiXL DrawScreenQuad);
                   // no point buffer — the only kind driven by srcTexture instead of points.
  Layer2d = 5,     // draw_quad_xf_vs: 6-vert quad sampling srcTexture, but TRANSFORMED by
                   // ObjectToClipSpace (TiXL Layer2d → draw-Quad-vs.hlsl). Parallel to ScreenQuad
                   // (F2: a DISTINCT shader, NOT a flag on ScreenQuad — TiXL ships 2 shaders; the
                   // clip-space ScreenQuad leaf stays untouched). Reads objectToClipSpace[16] below.
  Clear = 4,       // not a draw: a chain-clear directive (TiXL ClearRenderTarget). When it is the
                   // FIRST chain item the executor sets the pass clear color from it (color[]); the
                   // retained-mode pass already clears once, so this is free. A non-first Clear (mid
                   // chain re-clear) needs a clear-quad and is deferred (no-op for now).
  Mesh = 6,        // mesh_draw_unlit_vs: SV_VertexID-driven triangle list reading a SwVertex buffer +
                   // SwTriIndex buffer (TiXL DrawMeshUnlit → mesh-DrawUnlit.hlsl). The FIRST 3D draw
                   // kind: it reads objectToClipSpace[16] + the camera-stamp fields (same as Layer2d)
                   // and is depth-TESTED (LessEqual + ZWrite) — every other kind draws depth-disabled.
                   // No point buffer; driven by meshVtx/meshIdx/meshIndexCount below.
};

// Per-item blend equation (TiXL SharedEnums.BlendModes, factors from Core/Rendering/
// DefaultRenderingStates.cs). THIS BATCH ships Normal + Add only — Screen/Multiply are not a
// single Metal blend equation (they need shader-side compositing) and are deferred. Numbered to
// match the TiXL enum ordering (Normal=0, Additive=1) so the .t3 BlendMode int maps directly.
enum class BlendMode : uint32_t {
  Normal = 0,    // alpha-over: src*SrcA + dst*(1-SrcA)  (DefaultRenderingStates.DefaultBlendState)
  Additive = 1,  // add:        src*SrcA + dst*1         (DefaultRenderingStates.AdditiveBlendState)
};

// One draw in a render command chain: a point bag + how to draw it. New fields are appended
// AFTER viewExtent so existing aggregate inits (RenderDrawItem{pts,count,extent}) stay valid —
// they default to a white DrawPoints item, the pre-batch-13 behavior.
struct RenderDrawItem {
  const MTL::Buffer* points = nullptr;  // borrowed (PointGraph per-node out buffer); not retained
  uint32_t count = 0;                   // points to draw (== segment producer count; see kind)
  float viewExtent = 3.5f;              // world half-extent → NDC (hardcoded DrawPoints value)
  DrawKind kind = DrawKind::Points;     // which shape op produced this item
  // Draw params (TiXL DrawLines.Color/LineWidth, DrawBillboards.Scale/Color). All draw kinds
  // read color (multiplied with per-Point.Color); Lines uses lineWidth, Billboards uses size.
  float color[4] = {1.0f, 1.0f, 1.0f, 1.0f};  // tint (TiXL Color default = white)
  float lineWidth = 0.02f;                     // DrawLines.LineWidth (.t3 default 0.02)
  float size = 1.0f;                           // DrawBillboards.Scale (.t3 default 1.0)
  // DrawClosedLines (TiXL Lib.point.draw.DrawClosedLines → DrawLinesAlt.hlsl GetWrappedIndex). The
  // closed-loop variant of DrawKind::Lines: segment i connects Points[i]→Points[(i+1)%shapePts],
  // wrapping the last point back to the first (a closed polygon), vs DrawLines' open Points[i]→[i+1].
  // pointsPerShape>0 splits the bag into that-many-point closed shapes (TiXL PointsPerShape, .t3
  // default 0 = ONE shape using every point). Read ONLY by DrawKind::Lines; lineClosed=false +
  // pointsPerShape=0 → the exact pre-batch open DrawLines path (executor draws (count-1) segments,
  // shader never wraps) → byte-identical. The two fields travel together: lineClosed flips the
  // executor's segment count (count instead of count-1) AND the shader's wrap modulo.
  bool lineClosed = false;        // true → wrap last→first (closed loop); false → open DrawLines
  uint32_t pointsPerShape = 0;    // TiXL PointsPerShape (0 = single shape over the whole bag)
  // ScreenQuad fields (TiXL DrawScreenQuad). srcTexture is the sampled input — borrowed (a
  // PointGraph-owned upstream tex node output, same single-frame lifetime as `points`; NEVER
  // retained). position/width size+place the clip-space quad. Ignored by Points/Lines/Billboards.
  const MTL::Texture* srcTexture = nullptr;    // sampled image (null → black, defined, no crash)
  float position[2] = {0.0f, 0.0f};            // DrawScreenQuad.Position (clip-space offset)
  float width = 1.0f;                          // DrawScreenQuad.Width  (quad half-extent X)
  float height = 1.0f;                         // DrawScreenQuad.Height (quad half-extent Y)
  // How this item composites onto what's already drawn. DrawPoints stays Normal (opaque path
  // uses no blend at all — see executor); DrawScreenQuad reads the BlendMode param.
  BlendMode blendMode = BlendMode::Normal;
  // DrawScreenQuad HDR clamp upper bound (TiXL constant float4(1000,1000,1000,1) — RGB headroom,
  // alpha capped at 1). NOT a node input; the cook path always emits this default. The clamp
  // golden overrides it (and the -bug leg corrupts it) to drive the real shader clamp ceiling.
  float clampMax[4] = {1000.0f, 1000.0f, 1000.0f, 1.0f};
  // Layer2d ObjectToClipSpace (TiXL Layer2d → draw-Quad-vs.hlsl). ROW-MAJOR (m[r*4+c]); the ONLY
  // matrix the xf VS reads (F3: the other 9 TransformBufferLayout matrices are DEAD for Layer2d, NOT
  // built). The cook DRIVER (cookRenderTarget) fills this from the default camera × ObjectToWorld
  // at draw time (it knows the output aspect = the resolution-pin point); the op leaves it identity.
  // Ignored by every kind except Layer2d. Default identity (a no-op transform = clip-space passthrough).
  float objectToClipSpace[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
  // Layer2d drop-mul tooth: when false the xf VS skips the ObjectToClipSpace mul (raw clip space,
  // = the ScreenQuad behavior). The render golden's injectBug sets this false to prove the seam mul
  // is load-bearing (a mis-placed quad). Production always leaves it true. Ignored by non-Layer2d kinds.
  bool applyTransform = true;
  // ── Layer2d transform-stack (Cut 2): the RAW SRT params (TiXL _ProcessLayer2d inputs) ──
  // The op (cookLayer2d) stamps these; the EXECUTOR composes ObjectToWorld = S·R·T at draw time,
  // because the ScaleMode aspect coupling needs viewAspect (from the camera, executor-local F1) AND
  // imageAspect (from srcTexture). This mirrors TiXL: _ProcessLayer2d evals against context's
  // CameraToClipSpace. When layer2dComposeSRT is false the executor uses objectToClipSpace[16] AS the
  // ObjectToWorld verbatim (the Cut-1 seam tooth path that drove a hand-built matrix); when true it
  // composes from these params (production + the Cut-2 transform teeth). Ignored by non-Layer2d kinds.
  bool layer2dComposeSRT = false;
  float layerScale = 1.0f;              // TiXL Scale (.t3 default 1.0)
  float layerStretch[2] = {1.0f, 1.0f}; // TiXL Stretch (.t3 default (1,1))
  float layerRotateDeg = 0.0f;          // TiXL Rotate (.t3 default 0) — degrees
  float layerPosZ = 0.0f;               // TiXL PositionZ (.t3 default 0); position[] above = PositionXy
  uint32_t layerScaleMode = 0;          // TiXL ScaleMode (.t3 default 0 = FitHeight)
  // ── Camera op (Cut 3): explicit per-item camera (TiXL Camera.cs push/pop, mechanism Option a) ──
  // TiXL's Camera evaluates its subtree with context.WorldToCamera/CameraToClipSpace temporarily set
  // to ITS matrices, then restores (Camera.cs:36-45). We are retained-mode with a per-item executor —
  // there is no runtime scope stack to push onto. Instead the Camera op STAMPS its raw camera params
  // onto every item its subtree produced (cookCamera); the EXECUTOR, when it composes a Layer2d item's
  // ObjectToClipSpace, uses THIS camera's WorldToCamera/CameraToClipSpace instead of the driver-local
  // default (F1). This reproduces push/pop without a scope stack — like the FloatList/context-var
  // host-context precedent. hasCamera=false → the executor uses defaultLayerCameraForward (no Camera op).
  //   NESTING/push-pop semantics: the INNERMOST camera wins. cookCamera stamps only items where
  //   !hasCamera, so a deeper Camera (which already stamped) is respected by an outer one — exactly
  //   restoring the previous context on pop. Ignored by every kind that does not read a camera (only
  //   Layer2d reads it today; ScreenQuad is raw-clip by design).
  // RAW params (NOT the matrices): the executor builds worldToCamera=lookAtRH(eye,target,up) and
  // cameraToClipSpace=perspectiveFovRH(fovDeg, aspect, near, far). Aspect mirrors Camera.cs:53-55:
  // camAspect>0 uses it, else the executor's output aspect (the RequestedResolution fallback).
  bool hasCamera = false;
  float camEye[3] = {0.0f, 0.0f, 0.0f};
  float camTarget[3] = {0.0f, 0.0f, 0.0f};
  float camUp[3] = {0.0f, 1.0f, 0.0f};
  float camFovDeg = 45.0f;     // TiXL FieldOfView (degrees; .t3 default 45)
  float camNear = 0.01f;       // TiXL ClipPlanes.X
  float camFar = 1000.0f;      // TiXL ClipPlanes.Y
  float camAspect = -1.0f;     // TiXL AspectRatio (.t3 default 0 → <0.0001 → use output aspect); <=0 = output aspect
  // ── DrawKind::Mesh (TiXL DrawMeshUnlit → mesh-DrawUnlit.hlsl) ──────────────────────────────────
  // The two mesh-currency buffers (sw_mesh.h SwVertex 80B / SwTriIndex 12B), borrowed from the cooked
  // upstream mesh node (PointGraph meshVtxBuf/meshIdxBuf, single-frame lifetime like `points`; NEVER
  // retained). meshIndexCount = the FACE count; the VS draws meshIndexCount*3 vertices (3 verts/face,
  // SV_VertexID-driven: faceIndex=vid/3, faceVertexIndex=vid%3 → FaceIndices[faceIndex][fvi]). A mesh
  // item composes its ObjectToClipSpace EXACTLY like Layer2d (the objectToClipSpace[16]+camera fields
  // above) — the executor builds it from the default camera (or the stamped Camera op). Ignored by
  // every non-Mesh kind. Default null/0 = nothing to draw (the executor skips the item).
  const MTL::Buffer* meshVtx = nullptr;   // borrowed SwVertex buffer (t0 PbrVertices)
  const MTL::Buffer* meshIdx = nullptr;   // borrowed SwTriIndex buffer (t1 FaceIndices)
  uint32_t meshIndexCount = 0;            // FACE count; vertexCount = meshIndexCount*3 (TiXL MultiplyInt ×3)
};

// A render command chain: draw items in execution order (later items composite on top).
// DrawPoints produces a 1-item chain; RenderTarget concatenates all upstream chains.
struct RenderCommand {
  std::vector<RenderDrawItem> items;
};

// Test-only CPU executor hook (the depth tooth): when true, cookRenderTarget draws DrawKind::Mesh items
// with the depth-DISABLED state instead of LessEqual+ZWrite — so the depth-occlusion golden (Tooth B)
// can prove depth does OCCLUSION (with it set, draw-order decides → near-red no longer wins → the
// overlap turns green = the bite). OFF in production (zero behavior change). This is a CPU executor flag,
// NOT a shader bug-branch (no test seam in the production .metal — constitution rule). Parallel to
// mesh_op_registry.h's meshInjectBug(). Defined in point_ops_rendertarget.cpp.
bool& meshDepthDisableForTest();

}  // namespace sw

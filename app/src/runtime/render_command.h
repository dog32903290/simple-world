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
#include <functional>
#include <string>
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
  Points2 = 7,     // draw_points2_vs: 6-vert screen-facing quad per Point sized by Radius (TiXL
                   // DrawPoints2 → DrawPoints.hlsl Radius variant). Like Billboards but the size knob
                   // is `size` (= Radius*10.8) and `useWForSize` scales by Point.FX1. Reads color/size/
                   // useWForSize. Its OWN shader/PSO → DrawKind::Points (v1 DrawPoints) stays untouched.
  LinesBuildup = 8,// draw_lines_buildup_vs: open polyline (Points[i]→Points[i+1], like Lines) with a
                   // per-fragment W-reveal (TiXL DrawLinesBuildup → DrawLinesBuildup.hlsl). Reads
                   // color/lineWidth + transitionProgress/visibleRange. Its OWN shader/PSO →
                   // DrawKind::Lines (DrawLines/DrawClosedLines) stays untouched.
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
  // ── OrthographicCamera (TiXL OrthographicCamera.cs): same camera STAMP, ORTHOGRAPHIC projection ──
  // OrthographicCamera shares the eye/target/up (LookAtRH WorldToCamera) + near/far (NearFarClip) + aspect
  // fields above, but its CameraToClipSpace is orthoRH(size) instead of perspectiveFovRH. camOrtho=true
  // tells the executor to build the ortho matrix. The ortho VIEW SIZE = Stretch · Scale · (aspect,1)
  // (OrthographicCamera.cs:33) — the RAW Scale/Stretch travel here (NOT the resolved size) because the
  // aspect factor needs the output (RequestedResolution) aspect, finalized executor-side exactly like
  // Layer2d's ScaleMode and the Camera op's AspectRatio fallback. camFovDeg is DEAD when camOrtho (ortho
  // has no fov). camOrtho=false → the perspective path above is byte-identical (no behaviour change).
  bool camOrtho = false;
  float camOrthoScale = 1.0f;          // TiXL Scale (.t3 default 1.0)
  float camOrthoStretch[2] = {1.0f, 1.0f};  // TiXL Stretch (.t3 default (1,1))
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
  // ── DrawKind::Points2 (TiXL DrawPoints2 → DrawPoints.hlsl Radius variant) ──────────────────────
  // useWForSize: TiXL UseWForSize (.t3 default true) — when true the per-Point W (FX1) scales each
  // sprite (the shader's ScaleFX==1 path). DrawPoints2 uses `size` for the sprite size = Radius*10.8.
  // Read ONLY by DrawKind::Points2; default true matches the .t3 default. Ignored by every other kind
  // (DrawBillboards reads `size` but never `useWForSize`) → those items are byte-identical.
  bool useWForSize = true;
  // ── DrawKind::LinesBuildup (TiXL DrawLinesBuildup → DrawLinesBuildup.hlsl) ──────────────────────
  // The progressive-reveal params: transitionProgress sweeps the visible window along the polyline
  // (OffsetU = transitionProgress - 0.01 in the shader); visibleRange = the window width. Read ONLY
  // by DrawKind::LinesBuildup; the .t3 defaults are 0.5/0.5. Ignored by every other kind → byte-identical.
  float transitionProgress = 0.5f;  // TiXL TransitionProgress (.t3 default 0.5)
  float visibleRange = 0.5f;        // TiXL VisibleRange (.t3 default 0.5)
  // ── Group SRT (S2b): the accumulated parent transform-context push (TiXL Group.cs ObjectToWorld) ──
  // TiXL Group sets context.ObjectToWorld = Multiply(groupSRT, prevObjectToWorld) around its collected
  // child Commands, then restores on exit (Group.cs:54-82). SW is retained-mode per-item (no runtime
  // ObjectToWorld scope stack) so — exactly like the Camera op's per-item camera stamp (hasCamera/cam*
  // above) — the Group op STAMPS its accumulated SRT onto every subtree item, and the EXECUTOR
  // right-multiplies it into the item's own ObjectToWorld: finalO2W = layerO2W · groupObjectToWorld
  // (row-vector v·M; the group is the PARENT transform applied AFTER the child's own = TiXL's
  // child·context order). NESTING accumulates: an outer Group does it.group = it.group · outerSRT, so a
  // child sees v·childO2W·innerSRT·outerSRT (innermost first). hasGroup=false → identity → the pre-S2b
  // path is byte-identical (the executor skips the multiply). ROW-MAJOR (m[r*4+c]), same convention as
  // objectToClipSpace / layer2dObjectToWorld. Read by Layer2d + Mesh (the kinds that compose ObjectToWorld;
  // every other kind ignores it). Default identity (a no-op = no group push).
  bool hasGroup = false;
  float groupObjectToWorld[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
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

// S2a test-only DRIVER flag (the MultiInput Command collector tooth): when true, cookCommand's
// MultiInput Command branch COLLAPSES to the first wire (the `break` bug) instead of concatenating all
// N wired Command chains in wire order. So --selftest-execute's -bug leg drops every layer past the
// first → the composited chain loses items → the golden goes RED. OFF in production (zero behavior
// change — the loop concatenates every wire). This is a CPU DRIVER flag, NOT a shader bug-branch (no
// test seam in any .metal — constitution rule); parallel to meshDepthDisableForTest above. Defined in
// point_ops_execute.cpp; read by the flat (point_graph.cpp) + resident (point_graph_resident.cpp)
// collectors. Single-input Command ports (Camera/SetRequestedResolution) are unaffected (they already
// take only the first wire; the flag is a no-op for them).
bool& executeCollectFirstOnlyForTest();

// S3b Switch (TiXL flow/Switch.cs): the Command-collector SUB-SELECT. Unlike Execute (concat ALL wires),
// Switch cooks ONLY the index-th wired Command (wrap, negative-safe), -2 = all, -1/empty = none. The
// SELECTION is a cook-core hook in the SAME MultiInput Command collector branch Execute/SetVarCmd live in:
// the driver, on a Switch node, reads the Index param + counts the N wired Commands and concatenates only
// the selected one. switchSelectIndex() is the SINGLE source of truth both the flat (point_graph.cpp) and
// resident (point_graph_resident.cpp) collectors call, so the wrap/negative/empty math can NEVER diverge
// (the §3 off-by-one trap: resident wires = primary + extraConns). Defined in point_ops_switch.cpp.
constexpr int kSwitchSelectAll = -2;   // cook every wire (TiXL Switch.cs index==-2)
constexpr int kSwitchSelectNone = -1;  // cook no wire (TiXL Switch.cs index==-1 OR count==0)
// rawIndex = the (truncated-to-int) Switch.Index param; count = number of wired Command inputs gathered.
// → kSwitchSelectAll / kSwitchSelectNone, or the wrapped index in [0, count) (the single wire to cook).
int switchSelectIndex(int rawIndex, int count);
// Test-only DRIVER flag (the Switch sub-select tooth): true → the collector IGNORES the selection and cooks
// ALL wires (== Execute), so --selftest-switch's -bug leg draws the wrong branch → center-pixel RED. OFF in
// production (zero behaviour change). A CPU DRIVER flag, NOT a shader bug-branch (constitution rule); read by
// both collectors, parallel to executeCollectFirstOnlyForTest(). Defined in point_ops_switch.cpp.
bool& switchIgnoreIndexForTest();

// ─────────────────────────────── S3c Loop (TiXL flow/Loop.cs) ───────────────────────────────
// The re-cook keystone: cook the wired SubGraph `Count` times; iteration i writes index→BOTH Float+Int dicts
// and progress→Float into the live ContextVarMap FIRST, then re-cooks the subtree (so a value-rail Get*Var
// inside it reads i/progress live), concatenating each iteration's items into one chain. Faithful to
// Loop.cs:23-40: index=i (Float+Int), progress = (Count==1 ? 0 : i/(float)(Count-1)), and DELIBERATELY does
// NOT restore index/progress after the loop (Loop.cs:21 TODO leaks them — match it, do NOT "fix").
//
// loopRunIterations() is the SINGLE per-iteration mechanism both the flat (point_graph.cpp) and resident
// (point_graph_resident.cpp) collectors call — the var write + live-scope + re-cook + concat can NEVER fork
// between legs (the S2c/S3a blood lesson: a resident-only miss → production cooks the subtree once with the
// LAST index → only the final iteration's layer draws). The leg supplies `cookOneIteration`: a callback that
// FRESH-cooks the subtree and returns its items (the leg knows how to reach the single wired Command source;
// the helper owns the var/scope/concat). `vars` may be null (golden callers without a map) → no var write,
// the subtree still cooks `count` times (a benign no-op-var loop). `count<=0` → empty (TiXL `for i<end`).
//   ★re-cook memo note: the helper engages a LiveCtxVarScope per iteration so the driver's nodeParams memo
//   resolves the subtree's Float params FRESH each call (the scope-aware uncached branch) — that is how a
//   value-rail GetFloatVar(index) yields a DISTINCT value per iteration. (A subtree that produces a per-node
//   GPU Points BUFFER would still alias one output buffer across iterations — out of Loop's scope; the
//   faithful per-iteration distinctness lives in the items' by-value transform/param fields, like the golden.)
void loopRunIterations(int count, const std::string& indexVar, const std::string& progressVar,
                       struct ContextVarMap* vars, RenderCommand& out,
                       const std::function<RenderCommand()>& cookOneIteration);

// S3c -bug DRIVER flags (mirror of switchIgnoreIndexForTest / executeCollectFirstOnlyForTest), read by
// loopRunIterations on BOTH legs. OFF in production (zero behaviour change).
//   (a) loopBugCookOnceForTest: drop the for-loop → cook the subtree ONCE (one iteration's items) → the
//       chain has 1 item instead of Count → RED.
//   (b) loopBugReuseFirstForTest: cook ONCE then replicate that first iteration's items Count times WITHOUT
//       re-cook / without a fresh var write → every item carries iteration-0's index → all identical → RED.
// Defined in point_ops_loop.cpp.
bool& loopBugCookOnceForTest();
bool& loopBugReuseFirstForTest();

// ─────────────────── S3c ExecRepeatedly: re-cook the MultiInput wires `count` times ───────────────────
// The Loop SIBLING with no var injection (TiXL ExecRepeatedly.cs:34-53): it cooks the COLLECTED Command
// wires (MultiInput, wire order) `repeatCount` times, concatenating every repetition's items. Unlike Loop
// it writes NO index/progress context-var (the subtree just re-executes for its side-effects N times) and
// it is MultiInput (Loop is a single SubGraph). repeatCount is clamped [0,100] (ExecRepeatedly.cs:24) — the
// driver clamps before calling. execRepeatedlyRunRepetitions() is the SINGLE re-cook mechanism BOTH the flat
// (point_graph.cpp) and resident (point_graph_resident.cpp) collectors call so the loop can NEVER fork (the
// S2c/S3a blood lesson: a resident-only miss → production executes the subtree once instead of N times). The
// leg supplies `cookAllWiresOnce`: a callback that FRESH-cooks every wired Command source in wire order and
// returns their concatenated items (the leg knows how to reach the wires; the helper owns the repeat+concat).
// count<=0 → empty (ExecRepeatedly.cs:25 `if repeatCount<=0 return`).
void execRepeatedlyRunRepetitions(int count, RenderCommand& out,
                                  const std::function<RenderCommand()>& cookAllWiresOnce);

// S3c ExecRepeatedly -bug DRIVER flag (mirror of loopBugCookOnceForTest), read by
// execRepeatedlyRunRepetitions on BOTH legs. OFF in production. When true: cook the wires ONCE (drop the
// repeat loop) → the chain has 1×wires items instead of count×wires → the item-count assertion goes RED.
// Defined in point_ops_execrepeatedly.cpp.
bool& execRepeatedlyBugRunOnceForTest();

}  // namespace sw

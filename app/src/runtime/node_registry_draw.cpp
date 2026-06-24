// runtime/node_registry_draw — NodeSpec table for DRAW/RENDER ops.
// Ops that produce Command or Texture2D outputs from a point bag: DrawPoints, DrawLines,
// DrawBillboards, RenderTarget.  Split from node_registry.cpp (批次16-R, ARCHITECTURE rule 4).
#include "runtime/node_registry_draw.h"
#include "runtime/graph.h"

namespace sw {

// The render-island transform-context specs (RotateAroundAxis/Shear/Transform/RotateTowards) live in
// node_registry_draw_transform.cpp (peeled out for the 400-line ratchet); appended in source order below.
const std::vector<NodeSpec>& drawTransformSpecs();

const std::vector<NodeSpec>& drawSpecs() {
  static const std::vector<NodeSpec> specs = [] {
    std::vector<NodeSpec> base = {
      // DrawPoints (TiXL Slot<Command> out): points bag in -> a render Command out. The Command
      // output wires into a RenderTarget, which executes it into a Texture2D.
      {"DrawPoints", "DrawPoints",
       {{"points", "points", "Points", true},
        {"out", "out", "Command", false}},
       nullptr},
      // DrawLines (TiXL Lib.point.draw.DrawLines): connects the point bag into a polyline —
      // Points[i]→Points[i+1], each segment a screen-space-thickened quad (draw_lines.metal).
      // Points in → Command out (same cmd flow as DrawPoints). Params mirror DrawLines.t3:
      // Color (Vec4, white) + LineWidth (0.02). W(FX1)=NaN breaks the polyline — forward
      // parity: no production op writes the NaN separator yet (fork named in draw_lines.metal).
      // FORK (named): TiXL's camera/texture/UV/ShrinkWithDistance/Fog/Blend/ZTest are dropped
      // (no camera system — DrawPoints' baked-ortho fork class); flat untextured band.
      {"DrawLines", "DrawLines",
       {{"points", "points", "Points", true},
        {"out", "out", "Command", false},
        {"Color.x", "Color", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 4},
        {"Color.y", "Color.y", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
        {"Color.z", "Color.z", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
        {"Color.w", "Color.w", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
        {"LineWidth", "LineWidth", "Float", true, 0.02f, 0.0f, 1.0f}},
       nullptr},
      // DrawClosedLines (TiXL Lib.point.draw.DrawClosedLines → DrawLinesAlt.hlsl): the closed-loop
      // sibling of DrawLines — connects the bag into a polyline AND wraps the last point back to the
      // first (Points[last]→Points[0]), closing each shape. PointsPerShape>0 splits the bag into
      // closed shapes of that many points (.t3 default 0 = one shape over all points). Points in →
      // Command out (same cmd flow + DrawKind::Lines shader as DrawLines; only lineClosed differs).
      // Params mirror DrawClosedLines.t3: Color (Vec4 white) + LineWidth (0.02) + PointsPerShape (0).
      // FORK (named): inherits DrawLines' band fork class (no camera/Transforms/Fog/UV/texture/miter);
      // LineOffset/WidthFactor/ShrinkWithDistance/TransitionProgress/Blend/ZTest/ZWrite dropped.
      {"DrawClosedLines", "DrawClosedLines",
       {{"points", "points", "Points", true},
        {"out", "out", "Command", false},
        {"Color.x", "Color", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 4},
        {"Color.y", "Color.y", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
        {"Color.z", "Color.z", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
        {"Color.w", "Color.w", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
        {"LineWidth", "LineWidth", "Float", true, 0.02f, 0.0f, 1.0f},
        {"PointsPerShape", "PointsPerShape", "Float", true, 0.0f, 0.0f, 100000.0f}},
       nullptr},
      // DrawPoints2 (TiXL Lib.point.draw.DrawPoints2 → DrawPoints.hlsl Radius variant): the
      // Radius-driven DrawPoints — draws the bag as screen-facing quad sprites sized by Radius (the
      // .t3 routes Radius → ×10.8 → the shader PointSize), optionally scaled per-point by W (FX1,
      // UseWForSize). Points in → Command out (DrawKind::Points2 — its own shader, v1 DrawPoints
      // untouched). Params mirror DrawPoints2.t3: Color (Vec4 white) + Radius (0.01) + UseWForSize (1).
      // FORK (named): camera/Transforms/Fog/FadeNearest/BlendMode/ZTest/ZWrite + the Texture_ sprite
      // atlas (asset-bind seam not built → omitted, flat square sprite) dropped (no camera system).
      {"DrawPoints2", "DrawPoints2",
       {{"points", "points", "Points", true},
        {"out", "out", "Command", false},
        {"Color.x", "Color", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 4},
        {"Color.y", "Color.y", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
        {"Color.z", "Color.z", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
        {"Color.w", "Color.w", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
        {"Radius", "Radius", "Float", true, 0.01f, 0.0f, 10.0f},
        {"UseWForSize", "UseWForSize", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Bool}},
       nullptr},
      // DrawLinesBuildup (TiXL Lib.point.draw.DrawLinesBuildup → DrawLinesBuildup.hlsl): DrawLines'
      // polyline with a progressive W-reveal — TransitionProgress sweeps a VisibleRange-wide visible
      // window along the line (each point's W=FX1 is the reveal coord). Points in → Command out
      // (DrawKind::LinesBuildup — its own shader, DrawLines/DrawClosedLines untouched). Params mirror
      // DrawLinesBuildup.t3: Color (Vec4 white) + LineWidth (0.02) + TransitionProgress (0.5) +
      // VisibleRange (0.5). FORK (named): camera/Transforms/ShrinkWithDistance/Fog/miter/BlendMode/
      // ZTest/ZWrite + the Texture_ sample (white-pixel no-op) + Color×ForegroundColor theme coupling
      // dropped (DrawLines fork class); Texture_ port deferred (asset-bind seam not built).
      {"DrawLinesBuildup", "DrawLinesBuildup",
       {{"points", "points", "Points", true},
        {"out", "out", "Command", false},
        {"Color.x", "Color", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 4},
        {"Color.y", "Color.y", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
        {"Color.z", "Color.z", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
        {"Color.w", "Color.w", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
        {"LineWidth", "LineWidth", "Float", true, 0.02f, 0.0f, 1.0f},
        {"TransitionProgress", "TransitionProgress", "Float", true, 0.5f, 0.0f, 1.0f},
        {"VisibleRange", "VisibleRange", "Float", true, 0.5f, 0.0f, 1.0f}},
       nullptr},
      // DrawBillboards (TiXL Lib.point.draw.DrawBillboards): expands each Point into a
      // screen-facing quad sprite (draw_billboards.metal). Points in → Command out. Params mirror
      // DrawBillboards.t3: Scale (1.0) + Color (Vec4, white); per-point Scale.xy stretch kept
      // (UsePointScale default true). FORK (named): camera/atlas/sprite-texture/scatter/rotation/
      // curves dropped (no camera system); flat untextured screen-facing quad.
      {"DrawBillboards", "DrawBillboards",
       {{"points", "points", "Points", true},
        {"out", "out", "Command", false},
        {"Scale", "Scale", "Float", true, 1.0f, 0.0f, 1000.0f},
        {"Color.x", "Color", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 4},
        {"Color.y", "Color.y", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
        {"Color.z", "Color.z", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
        {"Color.w", "Color.w", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1}},
       nullptr},
      // DrawScreenQuad (TiXL Lib.render.basic.DrawScreenQuad): a textured fullscreen quad — samples
      // a Texture2D input, tints by Color, sizes/places by Width/Height/Position, composites by
      // BlendMode. Texture2D in → Command out. The 19 dx11 sub-ops in DrawScreenQuad.t3 collapse into
      // ONE Metal render pass (the executor's DrawKind::ScreenQuad case). Params mirror
      // DrawScreenQuad.cs defaults. FORK (named): NO camera (ObjectToClipSpace is commented out in
      // vs-draw-viewport-quad.hlsl — clean leaf); EnableDepthTest/Write + Filter dropped (no
      // depth-stencil seam yet); BlendMode shipped = Normal+Additive only (Multiply/Invert/... → Normal).
      {"DrawScreenQuad", "DrawScreenQuad",
       {{"Texture", "Texture", "Texture2D", true},
        {"out", "out", "Command", false},
        {"Color.x", "Color", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 4},
        {"Color.y", "Color.y", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
        {"Color.z", "Color.z", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
        {"Color.w", "Color.w", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
        {"Width", "Width", "Float", true, 1.0f, 0.0f, 100.0f},
        {"Height", "Height", "Float", true, 1.0f, 0.0f, 100.0f},
        {"BlendMode", "BlendMode", "Float", true, 0.0f, 0.0f, 8.0f, Widget::Enum,
         {"Normal", "Additive", "Multiply", "Invert", "None", "PreMultiplied", "BlendOnWhite",
          "BlendOnWhite01", "UseImageAlpha"}, true},
        {"Position.x", "Position", "Float", true, 0.0f, -10.0f, 10.0f, Widget::Vec, {}, true, 2},
        {"Position.y", "Position.y", "Float", true, 0.0f, -10.0f, 10.0f, Widget::Vec, {}, true, 1}},
       nullptr},
      // ClearRenderTarget (TiXL Lib.render._dx11.api.ClearRenderTarget): a chain-clear directive —
      // Command out (no input). Maps to the executor's pass clear color when it is the FIRST chain
      // item (faithful + free; the retained-mode pass clears once via LoadActionClear). Proves
      // Command-chain ordering with a 2nd op type. FORK (named): the DSV depth clear is dropped (no
      // depth-stencil seam); a non-first mid-chain re-clear (clear-quad) is deferred.
      {"ClearRenderTarget", "ClearRenderTarget",
       {{"out", "out", "Command", false},
        {"ClearColor.x", "ClearColor", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 4},
        {"ClearColor.y", "ClearColor.y", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
        {"ClearColor.z", "ClearColor.z", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
        {"ClearColor.w", "ClearColor.w", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1}},
       nullptr},
      // Camera (TiXL Lib.render.camera.Camera): wraps a Command subtree and renders it through an
      // explicit camera (Position/Target/Up/FieldOfView/ClipPlanes) instead of the driver-local default
      // (Camera.cs push/pop). Command in → Command out (the op stamps its camera onto every subtree item;
      // the RenderTarget executor builds WorldToCamera/CameraToClipSpace from those params). FORK (named):
      // offset/roll/lensShift dropped (Camera.cs:82-103 commented embellishments); AspectRatio default 0
      // → the output (RequestedResolution) aspect (Camera.cs:53-55). v1: no OrbitCamera/ActionCamera, no depth.
      {"Camera", "Camera",
       {{"command", "command", "Command", true},
        {"out", "out", "Command", false},
        {"Position.x", "Position", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, true, 3},
        {"Position.y", "Position.y", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, true, 1},
        {"Position.z", "Position.z", "Float", true, 2.4142135f, -100.0f, 100.0f, Widget::Vec, {}, true, 1},
        {"Target.x", "Target", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, true, 3},
        {"Target.y", "Target.y", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, true, 1},
        {"Target.z", "Target.z", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, true, 1},
        {"Up.x", "Up", "Float", true, 0.0f, -1.0f, 1.0f, Widget::Vec, {}, true, 3},
        {"Up.y", "Up.y", "Float", true, 1.0f, -1.0f, 1.0f, Widget::Vec, {}, true, 1},
        {"Up.z", "Up.z", "Float", true, 0.0f, -1.0f, 1.0f, Widget::Vec, {}, true, 1},
        {"FieldOfView", "FieldOfView", "Float", true, 45.0f, 1.0f, 179.0f},
        {"ClipPlanes.x", "ClipPlanes", "Float", true, 0.01f, 0.0001f, 1000.0f, Widget::Vec, {}, true, 2},
        {"ClipPlanes.y", "ClipPlanes.y", "Float", true, 1000.0f, 0.0001f, 100000.0f, Widget::Vec, {}, true, 1},
        {"AspectRatio", "AspectRatio", "Float", true, 0.0f, 0.0f, 10.0f}},
       nullptr},
      // OrthographicCamera (TiXL Lib.render.camera.OrthographicCamera): the perspective Camera's twin — wraps
      // a Command subtree and renders it through an ORTHOGRAPHIC projection (no perspective foreshortening; a
      // farther eye does NOT shrink the view). Command in → Command out (the op stamps its ortho camera onto
      // every subtree item; the RenderTarget executor builds CameraToClipSpace = orthoRH(size) where
      // size = Stretch·Scale·(aspect,1), OrthographicCamera.cs:33). FORKS (named): Roll dropped
      // (OrthographicCamera.cs:30 rollRotation); Position default = TiXL's exact .t3 value (NOT rounded); no
      // Reference output; AspectRatio default 0 → output (RequestedResolution) aspect (cs:25-28). v1: Scale +
      // eye/target/up + near/far shipped (Stretch defaults (1,1)); no OrbitCamera/ActionCamera, no depth.
      {"OrthographicCamera", "OrthographicCamera",
       {{"command", "command", "Command", true},
        {"out", "out", "Command", false},
        {"Position.x", "Position", "Float", true, -0.0015059264f, -100.0f, 100.0f, Widget::Vec, {}, true, 3},
        {"Position.y", "Position.y", "Float", true, 0.0014562709f, -100.0f, 100.0f, Widget::Vec, {}, true, 1},
        {"Position.z", "Position.z", "Float", true, 10.0f, -100.0f, 100.0f, Widget::Vec, {}, true, 1},
        {"Target.x", "Target", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, true, 3},
        {"Target.y", "Target.y", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, true, 1},
        {"Target.z", "Target.z", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, true, 1},
        {"Up.x", "Up", "Float", true, 0.0f, -1.0f, 1.0f, Widget::Vec, {}, true, 3},
        {"Up.y", "Up.y", "Float", true, 1.0f, -1.0f, 1.0f, Widget::Vec, {}, true, 1},
        {"Up.z", "Up.z", "Float", true, 0.0f, -1.0f, 1.0f, Widget::Vec, {}, true, 1},
        {"Scale", "Scale", "Float", true, 1.0f, 0.0001f, 100.0f},
        {"Stretch.x", "Stretch", "Float", true, 1.0f, 0.0001f, 100.0f, Widget::Vec, {}, true, 2},
        {"Stretch.y", "Stretch.y", "Float", true, 1.0f, 0.0001f, 100.0f, Widget::Vec, {}, true, 1},
        {"NearFarClip.x", "NearFarClip", "Float", true, 0.1f, 0.0001f, 1000.0f, Widget::Vec, {}, true, 2},
        {"NearFarClip.y", "NearFarClip.y", "Float", true, 1000.0f, 0.0001f, 100000.0f, Widget::Vec, {}, true, 1},
        {"AspectRatio", "AspectRatio", "Float", true, 0.0f, 0.0f, 10.0f}},
       nullptr},
      // DrawMeshUnlit (TiXL Lib.mesh.draw.DrawMeshUnlit): the FIRST 3D mesh — a depth-tested,
      // genuinely-unlit triangle mesh (mesh-DrawUnlit.hlsl; psMain default = albedo(white)·Color = Color).
      // Mesh in → Command out (DrawKind::Mesh). The executor attaches a Depth32Float buffer and draws it
      // LessEqual/ZWrite/CCW-front/Cull-Back (TiXL DepthStencilState 61714c96 + Rasterizer 6e672779).
      // Color (Vec4, .t3 default white) is the only param shipped v1. FORKS (named): in-code 1×1 white t2
      // (no Texture input → byte-identical white.png albedo); BlendMode/FillMode/Cull/AlphaCutOff/BlurLevel/
      // UseVertexColor/UseCubeMap/Texture deferred (defaults only); DrawMesh (the PBR variant) deferred.
      {"DrawMeshUnlit", "DrawMeshUnlit",
       {{"Mesh", "Mesh", "Mesh", true},
        {"out", "out", "Command", false},
        {"Color.x", "Color", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 4},
        {"Color.y", "Color.y", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
        {"Color.z", "Color.z", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
        {"Color.w", "Color.w", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1}},
       nullptr},
      // RenderTarget (TiXL Lib.image.generate.basic.RenderTarget): executes a Command chain into a
      // sized Texture2D — the RESOLUTION PIN. Command in, Texture2D out; Resolution enum picks the
      // output size (WindowFollow tracks the viewport, fixed modes pin a standard size, Custom reads
      // CustomW/H); ClearColor is the background. See docs/runtime/RENDER_TARGET_CONTRACT.md.
      {"RenderTarget", "RenderTarget",
       {{"command", "command", "Command", true},
        {"out", "out", "Texture2D", false},
        {"Resolution", "Resolution", "Float", true, 0.0f, 0.0f, 4.0f, Widget::Enum,
         {"WindowFollow", "HD720", "HD1080", "UHD4K", "Custom"}, true},
        {"CustomW", "CustomW", "Float", true, 512.0f, 1.0f, 8192.0f},
        {"CustomH", "CustomH", "Float", true, 512.0f, 1.0f, 8192.0f},
        {"ClearColor.x", "ClearColor", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 4},
        {"ClearColor.y", "ClearColor.y", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
        {"ClearColor.z", "ClearColor.z", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
        {"ClearColor.w", "ClearColor.w", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1}},
       nullptr},
      // SetRequestedResolution (TiXL Lib.render.shading.SetRequestedResolution): the EXPLICIT override
      // op of the S1 output-resolution seam. Wraps a Command subtree; while cooking it, pushes
      // context.RequestedResolution = new Int2(Width or current, Height or current) * Multiply, clamped
      // [1,16384], then restores (SetRequestedResolution.cs:18-28 save/set/cook-child/restore). The
      // PUSH happens in the cook driver (cookCommand) BEFORE the subtree cooks — the op cook itself just
      // forwards the subtree's items (the driver owns the push because the subtree is cooked there).
      // Width/Height default 0 = "use the current RequestedResolution" (so a bare Multiply scales the
      // ambient size); Multiply default 1. Command in → Command out.
      {"SetRequestedResolution", "SetRequestedResolution",
       {{"command", "command", "Command", true},
        {"out", "out", "Command", false},
        {"Width", "Width", "Float", true, 0.0f, 0.0f, 16384.0f},
        {"Height", "Height", "Float", true, 0.0f, 0.0f, 16384.0f},
        {"Multiply", "Multiply", "Float", true, 1.0f, 0.0f, 16.0f}},
       nullptr},
      // SetFloatVarCmd (TiXL Lib.flow.context.SetFloatVar, the SubGraph branch :26-45): the Command-rail
      // twin of the value-rail SetFloatVar — wraps a Command SubGraph and, while cooking it, pushes
      // FloatValue into context.FloatVariables[VariableName], restoring after (hadPrev ? prev : ClearAfter ?
      // keep : Remove). The PUSH/RESTORE lives in the cook driver (cookCommand S3a branch); the op cook just
      // forwards the subtree's items (like SetRequestedResolution). VariableName is a String input (strDef
      // "f", the float-only value rail carries it on the String channel). NAMED FORK: TiXL's ONE dual-branch
      // node becomes two sw types — the no-SubGraph float write stays the value-rail "SetFloatVar"; this is
      // the SubGraph (Command) half. Command in → Command out. .t3: FloatValue=0, ClearAfterExecution=false.
      {"SetFloatVarCmd", "SetFloatVar",
       {{"SubGraph", "SubGraph", "Command", true},
        {"out", "out", "Command", false},
        {"VariableName", "VariableName", "String", true, 0.0f, 0.0f, 1.0f, Widget::Slider, {}, false, 1, false, "f"},
        {"FloatValue", "FloatValue", "Float", true, 0.0f, -1000.0f, 1000.0f},
        {"ClearAfterExecution", "ClearAfterExecution", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Bool, {}, true}},
       nullptr},
      // SetIntVarCmd (TiXL Lib.flow.context.SetIntVar, the SubGraph branch :38-64): the int twin of
      // SetFloatVarCmd. Value arrives on a Float port (no Int port type) → truncated toward zero (C# (int)
      // cast convention) and pushed into context.IntVariables[VariableName] around the SubGraph. strDef "i".
      // Command in → Command out. .t3: Value=0, ClearAfterExecution=false.
      {"SetIntVarCmd", "SetIntVar",
       {{"SubGraph", "SubGraph", "Command", true},
        {"out", "out", "Command", false},
        {"VariableName", "VariableName", "String", true, 0.0f, 0.0f, 1.0f, Widget::Slider, {}, false, 1, false, "i"},
        {"Value", "Value", "Float", true, 0.0f, -1000.0f, 1000.0f},
        {"ClearAfterExecution", "ClearAfterExecution", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Bool, {}, true}},
       nullptr},
      // Execute (TiXL Lib.flow.Execute): the S2a KEYSTONE — a MULTIINPUT Command port that concatenates
      // N wired Command chains in wire-declaration order into ONE chain (Execute.cs CollectedInputs). The
      // cook-core collector (cookCommand's MultiInput Command branch) does the gather+concat; this op just
      // gates on IsEnabled. Command(MultiInput) in → Command out. This is what lets the ~155 Slot<Command>
      // render ops compose (every render op outputs Command and they only chain through a MultiInput Group/
      // Execute). The Command port carries multiInput=true (the {..., false, 1, true} positional tail = the
      // FloatsToList/Values precedent); no Float fields are meaningful on it (placeholders). IsEnabled is a
      // Widget::Bool (.t3 DefaultValue = true). FORK (named): VisibleGizmos = Execute without transform =
      // the same op; Group (Execute + SRT push) is S2b, out of scope here.
      {"Execute", "Execute",
       {{"Command", "Command", "Command", true, 0.0f, 0.0f, 1.0f, Widget::Slider, {}, false, 1, true},
        {"out", "out", "Command", false},
        {"IsEnabled", "IsEnabled", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Bool, {}, true}},
       nullptr},
      // Loop (TiXL Lib.flow.Loop): the S3c RE-COOK keystone — cooks the wired SubGraph Count times, each
      // iteration writing index→Float+Int and progress→Float context-vars first (Loop.cs:25-35), concatenating
      // every iteration's items. The per-iteration var write + live-scope + re-cook + concat lives in the
      // cook-core collector (cookCommand's Loop branch → loopRunIterations), shared flat+resident; this op just
      // forwards the built chain. Faithful no-restore after the loop (Loop.cs:21 TODO leaks index/progress).
      // Command(SubGraph) in → Command out. IndexVariable/ProgressVariable on the String channel (strDef
      // "Index"/"Progress" — TiXL's input slots have no default name). .t3: Count=0.
      {"Loop", "Loop",
       {{"SubGraph", "SubGraph", "Command", true},
        {"out", "out", "Command", false},
        {"Count", "Count", "Float", true, 0.0f, 0.0f, 1000.0f},
        {"IndexVariable", "IndexVariable", "String", true, 0.0f, 0.0f, 1.0f, Widget::Slider, {}, false, 1, false, "Index"},
        {"ProgressVariable", "ProgressVariable", "String", true, 0.0f, 0.0f, 1.0f, Widget::Slider, {}, false, 1, false, "Progress"}},
       nullptr},
      // ExecuteOnce (TiXL Lib.flow.ExecuteOnce): the GATED Execute — a MultiInput Command port that
      // concatenates N wired chains in wire order (== Execute, the S2a collector) only when Trigger is set;
      // not triggered ⇒ empty (no draws). The driver's MultiInput Command collector does the gather+concat
      // (zero cook-core change); the op cook applies the Trigger gate (like Execute applies IsEnabled).
      // Command(MultiInput) in → Command out. NAMED BEHAVIORAL FORK: TiXL gates on
      // Trigger.DirtyFlag.IsDirty (a per-frame self-clearing edge latch → execute exactly once per trigger
      // edge; a held-true Trigger fires once-ever in TiXL, every-frame in sw); sw models it as the Trigger
      // VALUE (>0.5 ⇒ execute, ≤0.5 ⇒ skip) — cross-frame edge-latch deferred (needs frame-state). This
      // is NOT a faithful deferral like SkipFrameCount: TiXL has no value that disables once-ness.
      // OutputTrigger bool output dropped (no bool Command-side port; editor wiring not a draw effect).
      // .t3: Trigger DefaultValue=false.
      {"ExecuteOnce", "ExecuteOnce",
       {{"Command", "Command", "Command", true, 0.0f, 0.0f, 1.0f, Widget::Slider, {}, false, 1, true},
        {"out", "out", "Command", false},
        {"Trigger", "Trigger", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Bool, {}, true}},
       nullptr},
      // LogMessage (TiXL Lib.flow.LogMessage): a TRANSPARENT Command-rail SubGraph passthrough that fires a
      // host log side-effect while forwarding the wrapped subtree's draw items unchanged (LogMessage.cs:53).
      // The single (non-MultiInput) SubGraph is cooked by the driver's existing collector (zero cook-core
      // change); the op cook forwards the chain + emits the Message to a log sink when logLevel>None and (if
      // OnlyOnChanges) the text changed (LogMessage.cs:39-48). Command(SubGraph) in → Command out. FORKS
      // (named): perf timing (_dampedPreviousUpdateDuration / Playback.RunTimeInSecs / UpdateTime level) +
      // _nestingLevel indent dropped (no Playback clock / editor pane); Message string is on the String
      // channel, resolved by the op via a process-scoped per-node map — the one deferred prod string-thread
      // wire (no behaviour-bearing render path ships a LogMessage gate; authoring/telemetry node). .t3:
      // OnlyOnChanges=false, LogLevel default Messages(1).
      {"LogMessage", "LogMessage",
       {{"SubGraph", "SubGraph", "Command", true},
        {"out", "out", "Command", false},
        {"OnlyOnChanges", "OnlyOnChanges", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Bool, {}, true},
        {"LogLevel", "LogLevel", "Float", true, 1.0f, 0.0f, 2.0f, Widget::Enum,
         {"None", "Messages", "UpdateTime"}, true}},
       nullptr},
      // ExecRepeatedly (TiXL Lib.flow.ExecRepeatedly): the Loop SIBLING — a MultiInput Command port whose
      // wired subtrees RE-EXECUTE `RepeatCount` (clamped [0,100], :24) times, concatenating each repetition,
      // with NO context-var injection (unlike Loop's index/progress). The per-repetition re-cook + concat
      // lives in the cook-core collector (cookCommand's ExecRepeatedly branch → execRepeatedlyRunRepetitions),
      // shared flat+resident; this op just forwards the built chain. Command(MultiInput) in → Command out.
      // FORK (named): SkipFrameCount + _callsSinceLastRefresh (:27-34) are a per-frame skip-throttle counter;
      // sw ships the SkipFrameCount=0 .t3 default (execute every call) — the frame-skip throttle is the
      // deferred frame-state half (same class as ExecuteOnce's DirtyFlag latch). .t3: RepeatCount=1,
      // SkipFrameCount=0.
      {"ExecRepeatedly", "ExecRepeatedly",
       {{"Command", "Command", "Command", true, 0.0f, 0.0f, 1.0f, Widget::Slider, {}, false, 1, true},
        {"out", "out", "Command", false},
        {"RepeatCount", "RepeatCount", "Float", true, 1.0f, 0.0f, 100.0f},
        {"SkipFrameCount", "SkipFrameCount", "Float", true, 0.0f, 0.0f, 10000.0f}},
       nullptr},
    };
    // Append the render-island transform-context specs (peeled leaf) in source order — table order unchanged.
    const std::vector<NodeSpec>& tf = drawTransformSpecs();
    base.insert(base.end(), tf.begin(), tf.end());
    return base;
  }();
  return specs;
}

}  // namespace sw

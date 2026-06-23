// runtime/node_registry_draw — NodeSpec table for DRAW/RENDER ops.
// Ops that produce Command or Texture2D outputs from a point bag: DrawPoints, DrawLines,
// DrawBillboards, RenderTarget.  Split from node_registry.cpp (批次16-R, ARCHITECTURE rule 4).
#include "runtime/node_registry_draw.h"
#include "runtime/graph.h"

namespace sw {

const std::vector<NodeSpec>& drawSpecs() {
  static const std::vector<NodeSpec> specs = {
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
      // RotateAroundAxis (TiXL Lib.render.transform.RotateAroundAxis): wraps a Command subtree and pushes
      // ONE axis-angle rotation onto context.ObjectToWorld (Matrix4x4.CreateFromAxisAngle(Axis, Angle°)).
      // Command in → Command out (the op stamps the rotation onto every subtree item via the Group
      // per-item group-stamp mechanism; the executor right-multiplies it into ObjectToWorld). A thinner
      // sibling of Group — no SRT, no IsEnabled (TiXL's op has none). .t3 defaults: Axis (0,0,1)=Z, Angle 0.
      {"RotateAroundAxis", "RotateAroundAxis",
       {{"command", "command", "Command", true},
        {"out", "out", "Command", false},
        {"Angle", "Angle", "Float", true, 0.0f, -360.0f, 360.0f},
        {"Axis.x", "Axis", "Float", true, 0.0f, -1.0f, 1.0f, Widget::Vec, {}, true, 3},
        {"Axis.y", "Axis.y", "Float", true, 0.0f, -1.0f, 1.0f, Widget::Vec, {}, true, 1},
        {"Axis.z", "Axis.z", "Float", true, 1.0f, -1.0f, 1.0f, Widget::Vec, {}, true, 1}},
       nullptr},
      // Shear (TiXL Lib.render.transform.Shear): wraps a Command subtree and pushes a SHEAR matrix onto
      // context.ObjectToWorld (Identity with M12=Translation.Y, M21=Translation.X, M14=Translation.Z).
      // Command in → Command out (Group per-item group-stamp mechanism). No IsEnabled (TiXL has none).
      // The input is named "Translation" in TiXL (= the 3 shear amounts X/Y/Z); .t3 default (0,0,0).
      {"Shear", "Shear",
       {{"command", "command", "Command", true},
        {"out", "out", "Command", false},
        {"Translation.x", "Translation", "Float", true, 0.0f, -10.0f, 10.0f, Widget::Vec, {}, true, 3},
        {"Translation.y", "Translation.y", "Float", true, 0.0f, -10.0f, 10.0f, Widget::Vec, {}, true, 1},
        {"Translation.z", "Translation.z", "Float", true, 0.0f, -10.0f, 10.0f, Widget::Vec, {}, true, 1}},
       nullptr},
      // Transform (TiXL Lib.render.transform.Transform): the full-TRS render-island transform with a PIVOT
      // — the general sibling of Group (Group = Transform with pivot 0 + color/enable). Wraps a Command
      // subtree and pushes M = T(-Pivot)·S·R·T(+Pivot)·T(Translation) onto context.ObjectToWorld (Group
      // per-item group-stamp). Scale×UniformScale; Rotation = yaw(Y)/pitch(X)/roll(Z) degrees; no IsEnabled
      // (TiXL has none). FORK (named): TransformCallback editor gizmo hook dropped. .t3 defaults: Scale
      // (1,1,1), UniformScale 1, Rotation/Translation/Pivot (0,0,0).
      {"Transform", "Transform",
       {{"command", "command", "Command", true},
        {"out", "out", "Command", false},
        {"Translation.x", "Translation", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, true, 3},
        {"Translation.y", "Translation.y", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, true, 1},
        {"Translation.z", "Translation.z", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, true, 1},
        {"Rotation.x", "Rotation", "Float", true, 0.0f, -360.0f, 360.0f, Widget::Vec, {}, true, 3},
        {"Rotation.y", "Rotation.y", "Float", true, 0.0f, -360.0f, 360.0f, Widget::Vec, {}, true, 1},
        {"Rotation.z", "Rotation.z", "Float", true, 0.0f, -360.0f, 360.0f, Widget::Vec, {}, true, 1},
        {"Scale.x", "Scale", "Float", true, 1.0f, -10.0f, 10.0f, Widget::Vec, {}, true, 3},
        {"Scale.y", "Scale.y", "Float", true, 1.0f, -10.0f, 10.0f, Widget::Vec, {}, true, 1},
        {"Scale.z", "Scale.z", "Float", true, 1.0f, -10.0f, 10.0f, Widget::Vec, {}, true, 1},
        {"UniformScale", "UniformScale", "Float", true, 1.0f, 0.0f, 10.0f},
        {"Pivot.x", "Pivot", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, true, 3},
        {"Pivot.y", "Pivot.y", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, true, 1},
        {"Pivot.z", "Pivot.z", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, true, 1}},
       nullptr},
  };
  return specs;
}

}  // namespace sw

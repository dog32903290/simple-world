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
  };
  return specs;
}

}  // namespace sw

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

// runtime/node_registry_draw_renderstate — NodeSpec rows for the Seam 2 RENDER-STATE ops (Rasterizer,
// and OutputMerger when it lands). Peeled out of node_registry_draw.cpp to keep that file under the
// 400-line ratchet (ARCHITECTURE rule 4) when the Seam 2 render-state family landed.
//
// These ops wrap a single Command subtree and STAMP an accumulated FrozenRenderState (cull/fill/winding/
// depth-bias/blend/depth) onto every subtree draw item — the Camera-stamp / Group per-item push/pop
// mechanism (render_command.h hasRenderState/frozen). The cook leaf is point_ops_renderstate.cpp; this is
// only their UI spec. Appended into drawSpecs() (node_registry_draw.cpp) in source order — table order
// unchanged from before the peel.
#include "runtime/node_registry_draw.h"
#include "runtime/graph.h"

namespace sw {

const std::vector<NodeSpec>& drawRenderStateSpecs() {
  static const std::vector<NodeSpec> specs = {
      // Rasterizer (TiXL Lib.render._dx11.api.Rasterizer + Types.Gfx.RasterizerState — Seam 2 render-state
      // spike). Wraps a Command subtree and STAMPS its accumulated FrozenRenderState (cull/fill/winding +
      // depth-bias) onto every subtree draw item; the RenderTarget executor reads the stamp to materialize
      // a cached PSO + set the encoder cull/winding/depthBias. SW folds RasterizerState's params directly
      // (no separate RasterizerState currency). CullMode enum: the census wires only None/Back (Front
      // shipped for completeness); FillMode Solid (Wireframe dormant fork). DepthBias is Bucket-B EMERGENT
      // (numeric output not formula-portable) — a deferred golden. FORKS (named, all dormant per census):
      // Viewports/ScissorRectangles/AntialiasedLine/DepthClip/MultiSample/ScissorEnabled deferred. Command
      // in → Command out. Defaults = DX11 defaults (Back cull, Solid fill, CW front, no bias).
      {"Rasterizer", "Rasterizer",
       {{"command", "command", "Command", true},
        {"out", "out", "Command", false},
        {"CullMode", "CullMode", "Float", true, 2.0f, 0.0f, 2.0f, Widget::Enum, {"None", "Front", "Back"}, true},
        {"FillMode", "FillMode", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Enum, {"Solid", "Wireframe"}, true},
        {"FrontCounterClockwise", "FrontCounterClockwise", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Bool, {}, true},
        {"DepthBias", "DepthBias", "Float", true, 0.0f, -100.0f, 100.0f},
        {"SlopeScaledDepthBias", "SlopeScaledDepthBias", "Float", true, 0.0f, -100.0f, 100.0f},
        {"DepthBiasClamp", "DepthBiasClamp", "Float", true, 0.0f, -100.0f, 100.0f}},
       nullptr,
       "render.shading"},
  };
  return specs;
}

}  // namespace sw

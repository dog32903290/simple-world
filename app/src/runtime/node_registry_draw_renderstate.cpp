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
      // OutputMerger (TiXL Lib.render._dx11.api.OutputMergerStage + BlendState + DepthStencilState — Seam 2
      // OM stage). Wraps a Command subtree and STAMPS its accumulated blend + depth FrozenRenderState half
      // onto every subtree draw item; the RenderTarget executor reads the stamp to materialize the PSO's
      // colorAttachment blend + the encoder MTLDepthStencilState. SW folds BlendState's params directly (no
      // separate BlendState currency — same posture as Rasterizer). Census (PLAN §1, BlendState 25 consumers):
      // BlendEnable true×22/false×11; SourceBlend {SrcAlpha,One,Zero,InvDestColor}; DestinationBlend
      // {InvSrcAlpha,One,InvSrcColor,Zero,SrcColor,SrcAlpha}; BlendOp {Add,ReverseSubtract,Min}. ColorWriteMask
      // hardcoded All (no port). FORKS (named, all dormant/guard per census): AlphaToCoverage / IndependentBlend
      // (always false → field shipped, path guarded), BlendFactor(constant-color)/BlendSampleMask/StencilRef
      // (dynamic encoder state, no census wire), RTV/UAV/DSV resource binds (RenderTarget executor owns
      // attachments), logic-op / dual-source (Bucket-C guards, never fire). Command in → Command out. Defaults =
      // DX11 OM defaults (BlendEnable FALSE = opaque One/Zero/Add, DepthEnable FALSE = sw 2D depth-inert).
      {"OutputMerger", "OutputMerger",
       {{"command", "command", "Command", true},
        {"out", "out", "Command", false},
        {"BlendEnable", "BlendEnable", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Bool, {}, true},
        {"SourceBlend", "SourceBlend", "Float", true, 1.0f, 0.0f, 9.0f, Widget::Enum,
         {"Zero", "One", "SrcAlpha", "InvSrcAlpha", "SrcColor", "InvSrcColor", "DestColor", "InvDestColor",
          "DestAlpha", "InvDestAlpha"}, true},
        {"DestinationBlend", "DestinationBlend", "Float", true, 0.0f, 0.0f, 9.0f, Widget::Enum,
         {"Zero", "One", "SrcAlpha", "InvSrcAlpha", "SrcColor", "InvSrcColor", "DestColor", "InvDestColor",
          "DestAlpha", "InvDestAlpha"}, true},
        {"BlendOp", "BlendOp", "Float", true, 0.0f, 0.0f, 4.0f, Widget::Enum,
         {"Add", "Subtract", "ReverseSubtract", "Min", "Max"}, true},
        {"DepthEnable", "DepthEnable", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Bool, {}, true},
        {"DepthWrite", "DepthWrite", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Bool, {}, true},
        {"DepthCompare", "DepthCompare", "Float", true, 1.0f, 0.0f, 7.0f, Widget::Enum,
         {"Never", "Less", "Equal", "LessEqual", "Greater", "NotEqual", "GreaterEqual", "Always"}, true}},
       nullptr,
       "render.shading"},
  };
  return specs;
}

}  // namespace sw

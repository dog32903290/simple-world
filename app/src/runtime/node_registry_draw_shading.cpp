// runtime/node_registry_draw_shading — NodeSpec rows for the render.shading Command-scope PBR ops:
// SetMaterial / SetPointLight / SetFog. Peeled into its own family leaf (parallel-lane peel) so the
// pbr-material render-pass lane and this node-registration lane never touch a shared table — the
// render-pass worker owns the shader/pass/context-stack seam; this file owns only the graph node.
//
// WHY evaluate == nullptr (data-layer-only, no cook consumer yet): each op's VALUE currency + resolver
// is already ported and golden'd — SwPbrParameters/swResolvePbrParameters (sw_pbr_material.h,
// --selftest-pbrmaterial), SwPointLight/swResolvePointLight (sw_point_light.h, --selftest-pointlight),
// SwFogParameters/swResolveFog (sw_fog.h, --selftest-fog). What is BLOCKED is the render pass that reads
// the pushed context stack (the NEW-SEAM:pbr-material / :lighting island, CLONE_MAP.md:117). So these
// rows register the GRAPH NODE (drag-able, wire-able, census-recognised) with its ports + .t3 defaults;
// they carry NO evaluate and, like SetTime/SetFloatVarCmd (node_registry_draw_flow.cpp), rely on the
// Command-rail cook driver to forward the wrapped SubTree's draw items unchanged. When the pbr render
// pass lands, its per-op cook stamps the scope (push SwPbrParameters/SwPointLight/SwFogParameters) — the
// node needs no re-registration. This is the same "hang the node now, the scope-push activates later"
// posture the task specifies.
//
// PORT SCOPE (named fork, = the value-slice boundary sw_pbr_material.h documents): SetMaterial's four
// TEXTURE-MAP inputs (BaseColorMap / EmissiveColorMap / NormalMap / RoughnessMetallicOcclusionMap) are
// GPU ShaderResourceViews consumed only by the PBR draw shader — the deferred render-pass half. They are
// NOT exposed here (no Texture port type on the value/command rail, and no consumer). The numeric
// parameter inputs + MaterialId (the pure-value slice) ARE exposed. SetPointLight/SetFog have no texture
// inputs — every input maps to a port.
#include "runtime/node_registry_draw.h"
#include "runtime/graph.h"

namespace sw {

const std::vector<NodeSpec>& drawShadingSpecs() {
  static const std::vector<NodeSpec> specs = {
      // SetMaterial (TiXL Lib.render.shading.SetMaterial): wraps a Command SubTree and, while cooking it,
      // pushes a PbrMaterial (built from BaseColor/EmissiveColor/Roughness/Specular/Metal via
      // swResolvePbrParameters — SetMaterial.cs:34-38) onto context.PbrMaterial + context.Materials,
      // restoring after (SetMaterial.cs:48-65). SubTree(Command) in → Command out. The push/restore is the
      // deferred pbr render-pass scope; this op forwards the subtree items (SetTime precedent). Params
      // mirror SetMaterial.t3: BaseColor=(1,1,1,1), EmissiveColor=(0,0,0,1), Roughness=0.25, Specular=1.0,
      // Metal=0.0, MaterialId="". FORK (named): the four Texture2D map inputs are the deferred render-pass
      // half (see PORT SCOPE above). The Reference<PbrMaterial> output is dropped (no PbrMaterial value
      // rail; the Command output is the load-bearing one).
      {"SetMaterial", "SetMaterial",
       {{"SubTree", "SubTree", "Command", true},
        {"out", "out", "Command", false},
        {"BaseColor.x", "BaseColor", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, false, 4},
        {"BaseColor.y", "BaseColor.y", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, false, 1},
        {"BaseColor.z", "BaseColor.z", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, false, 1},
        {"BaseColor.w", "BaseColor.w", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, false, 1},
        {"EmissiveColor.x", "EmissiveColor", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Vec, {}, false, 4},
        {"EmissiveColor.y", "EmissiveColor.y", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Vec, {}, false, 1},
        {"EmissiveColor.z", "EmissiveColor.z", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Vec, {}, false, 1},
        {"EmissiveColor.w", "EmissiveColor.w", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, false, 1},
        {"Roughness", "Roughness", "Float", true, 0.25f, 0.0f, 1.0f},
        {"Specular", "Specular", "Float", true, 1.0f, 0.0f, 100.0f},
        {"Metal", "Metal", "Float", true, 0.0f, 0.0f, 1.0f},
        {"MaterialId", "MaterialId", "String", true, 0.0f, 0.0f, 1.0f, Widget::Slider, {}, false, 1, false, ""}},
       nullptr,
       "render.shading"},
      // SetPointLight (TiXL Lib.render.shading.SetPointLight): pushes a PointLight (built from
      // Position/Intensity/Color/Range/Decay via swResolvePointLight — SetPointLight.cs:17-21) onto
      // context.PointLights around the wrapped Command SubGraph, popping after (cs:22-26). Command in →
      // Command out. The push/pop is the deferred lighting render-pass scope; this op forwards the subtree
      // items. Params mirror SetPointLight.t3: Position=(0,0.0001,0), Intensity=1.0, Color=(1,1,1,1),
      // Range=100.0, Decay=2.0.
      {"SetPointLight", "SetPointLight",
       {{"Command", "Command", "Command", true},
        {"out", "out", "Command", false},
        {"Position.x", "Position", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 3},
        {"Position.y", "Position.y", "Float", true, 0.0001f, -100.0f, 100.0f, Widget::Vec, {}, false, 1},
        {"Position.z", "Position.z", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 1},
        {"Intensity", "Intensity", "Float", true, 1.0f, 0.0f, 100.0f},
        {"Color.x", "Color", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, false, 4},
        {"Color.y", "Color.y", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, false, 1},
        {"Color.z", "Color.z", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, false, 1},
        {"Color.w", "Color.w", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, false, 1},
        {"Range", "Range", "Float", true, 100.0f, 0.0f, 1000.0f},
        {"Decay", "Decay", "Float", true, 2.0f, 0.0f, 10.0f}},
       nullptr,
       "render.shading"},
      // SetFog (TiXL Lib.render.shading.SetFog): builds a FogParameters (from Distance/Bias/Color via
      // swResolveFog — SetFog.cs:20-25) and scopes it as context.FogParameters around the wrapped Command
      // SubGraph, restoring after (cs:27-33). Command in → Command out. The scope push is the deferred
      // render-pass half; this op forwards the subtree items. Params mirror SetFog.t3: Distance=10.0,
      // Bias=2.0, Color=(0,0,0,1). (Note: these are the SetFog INPUT-SLOT defaults, distinct from the
      // 10000/2 ambient fallback buffer SetFog never reads — see sw_fog.h's TWO-defaults note.)
      {"SetFog", "SetFog",
       {{"Command", "Command", "Command", true},
        {"out", "out", "Command", false},
        {"Distance", "Distance", "Float", true, 10.0f, 0.0f, 10000.0f},
        {"Bias", "Bias", "Float", true, 2.0f, 0.0f, 10.0f},
        {"Color.x", "Color", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Vec, {}, false, 4},
        {"Color.y", "Color.y", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Vec, {}, false, 1},
        {"Color.z", "Color.z", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Vec, {}, false, 1},
        {"Color.w", "Color.w", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, false, 1}},
       nullptr,
       "render.shading"},
  };
  return specs;
}

}  // namespace sw

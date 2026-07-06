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
// runtime/node_registry_draw_shading — NodeSpec rows for the render.shading PBR family (parallel-lane peel,
// mirror of node_registry_draw_render/_camera/_flow): SetMaterial / SetPointLight / SetFog / UseMaterial /
// DefineMaterials (the context-stack writers) + DrawMeshPbr (the lit-mesh draw). Peeled into its own file so
// the render lane can extend the shading family without touching drawRenderSpecs()'s shared table AND to keep
// each file ≤400 (ARCHITECTURE.md rule 4). drawSpecs() appends drawShadingSpecs() in source order.
//
// TiXL authority: external/tixl Operators/Lib/render/shading/{SetMaterial,UseMaterial,DefineMaterials}.cs +
// SetPointLight/SetFog + Lib/mesh/draw/DrawMesh.cs. The cook fns are in point_ops_pbrshading.cpp; the scope
// push/pop is in the two cook drivers; these rows are only the menu/findSpec surface (ports drive the gather).
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
      // DrawMeshPbr (DrawMesh → mesh-Draw.hlsl): the LIT mesh. Mesh in → Command out (DrawKind::MeshPbr).
      // Reads the scoped material/lights/fog; Cook-Torrance direct lighting (mesh_draw_pbr.metal). Color
      // (Vec4, .t3 white) tints the lit color. FORKS: IBL/ambient + texture maps deferred (mesh_pbr_params.h).
      {"DrawMeshPbr", "DrawMeshPbr",
       {{"Mesh", "Mesh", "Mesh", true},
        {"out", "out", "Command", false},
        {"Color.x", "Color", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 4},
        {"Color.y", "Color.y", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
        {"Color.z", "Color.z", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
        {"Color.w", "Color.w", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1}},
       nullptr,
       "mesh.draw"},
      // SetMaterial (SetMaterial.cs): pushes a PbrMaterial for its SubTree. BaseColor/EmissiveColor(Vec4),
      // Roughness/Specular/Metal(Float). Texture-map inputs deferred (next battle). Push/pop in the driver.
      {"SetMaterial", "SetMaterial",
       {{"SubTree", "SubTree", "Command", true},
        {"out", "out", "Command", false},
        {"BaseColor.x", "BaseColor", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 4},
        {"BaseColor.y", "BaseColor.y", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
        {"BaseColor.z", "BaseColor.z", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
        {"BaseColor.w", "BaseColor.w", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
        {"EmissiveColor.x", "EmissiveColor", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 4},
        {"EmissiveColor.y", "EmissiveColor.y", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
        {"EmissiveColor.z", "EmissiveColor.z", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
        {"EmissiveColor.w", "EmissiveColor.w", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
        {"Roughness", "Roughness", "Float", true, 0.5f, 0.0f, 1.0f},
        {"Specular", "Specular", "Float", true, 10.0f, 0.0f, 100.0f},
        {"Metal", "Metal", "Float", true, 0.0f, 0.0f, 1.0f}},
       nullptr,
       "render.shading"},
      // SetPointLight (SetPointLight.cs): pushes one PointLight for its SubTree. Position(Vec3), Intensity/
      // Range/Decay(Float), Color(Vec4). Input-slot defaults 0 (sw_point_light.h note — Decay input 0, not 2).
      {"SetPointLight", "SetPointLight",
       {{"SubTree", "SubTree", "Command", true},
        {"out", "out", "Command", false},
        {"Position.x", "Position", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, true, 3},
        {"Position.y", "Position.y", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, true, 1},
        {"Position.z", "Position.z", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, true, 1},
        {"Intensity", "Intensity", "Float", true, 0.0f, 0.0f, 100.0f},
        {"Color.x", "Color", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 4},
        {"Color.y", "Color.y", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
        {"Color.z", "Color.z", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
        {"Color.w", "Color.w", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
        {"Range", "Range", "Float", true, 0.0f, 0.0f, 1000.0f},
        {"Decay", "Decay", "Float", true, 0.0f, 0.0f, 10.0f}},
       nullptr,
       "render.shading"},
      // SetFog (SetFog.cs): pushes FogParameters for its SubTree. Color(Vec4), Distance/Bias(Float). Input-
      // slot defaults 0 (sw_fog.h: NOT the 10000/2 ambient default — that lives in the fallback buffer).
      {"SetFog", "SetFog",
       {{"SubTree", "SubTree", "Command", true},
        {"out", "out", "Command", false},
        {"Color.x", "Color", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 4},
        {"Color.y", "Color.y", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
        {"Color.z", "Color.z", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
        {"Color.w", "Color.w", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
        {"Distance", "Distance", "Float", true, 0.0f, 0.0f, 100000.0f},
        {"Bias", "Bias", "Float", true, 0.0f, 0.0f, 10.0f}},
       nullptr,
       "render.shading"},
      // UseMaterial (UseMaterial.cs): pushes a material referenced BY NAME (the DefineMaterials library) as
      // the active one for its SubTree. MaterialReference rides the String channel (named fork — sw has no
      // PbrMaterial slot currency yet).
      {"UseMaterial", "UseMaterial",
       {{"SubTree", "SubTree", "Command", true},
        {"out", "out", "Command", false}},
       nullptr,
       "render.shading"},
      // DefineMaterials (DefineMaterials.cs): registers named materials into a by-name library its SubGraph
      // can reference via UseMaterial. v1: registers ONE material (MaterialId + params) — the multi-define
      // MultiInput fork is next battle.
      {"DefineMaterials", "DefineMaterials",
       {{"SubGraph", "SubGraph", "Command", true},
        {"out", "out", "Command", false},
        {"BaseColor.x", "BaseColor", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 4},
        {"BaseColor.y", "BaseColor.y", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
        {"BaseColor.z", "BaseColor.z", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
        {"BaseColor.w", "BaseColor.w", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
        {"Roughness", "Roughness", "Float", true, 0.5f, 0.0f, 1.0f},
        {"Specular", "Specular", "Float", true, 10.0f, 0.0f, 100.0f},
        {"Metal", "Metal", "Float", true, 0.0f, 0.0f, 1.0f}},
       nullptr,
       "render.shading"},
  };
  return specs;
}

}  // namespace sw

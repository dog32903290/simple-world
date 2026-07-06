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

// runtime/mesh_pbr_item — the per-RenderDrawItem PBR currency (material + point lights + fog) a DrawMeshPbr
// stamps from the live SetMaterial/SetPointLight/SetFog scope (sw_pbr_scope.h), read by the executor to fill
// the MeshPbrParams cbuffer. Kept OUT of render_command.h (at its 400-line cap) — that header carries only
// the DrawKind::MeshPbr enum + a `MeshPbrItem pbr` member; this leaf holds the payload struct.
//
// By VALUE inside the RenderDrawItem (not a borrowed pointer): a render chain copies items with
// vector::insert (Camera/Group/render-state precedent), and this payload is resolved-CPU-value data (no GPU
// handle) so copying is cheap+safe across the single-frame chain. The material/light/fog values are all
// closed-form host values (SwPbrParameters/SwPointLight/SwFogParameters), so no lifetime coupling to any op.
#pragma once
#include <cstdint>

#include "runtime/sw_fog.h"           // SwFogParameters
#include "runtime/sw_pbr_material.h"  // SwPbrParameters
#include "runtime/sw_point_light.h"   // SwPointLight

namespace sw {

// Must match mesh_pbr_params.h MESH_PBR_MAX_LIGHTS / sw_pbr_scope.h kMaxPbrLights (mesh-Draw.hlsl Lights[8]).
constexpr int kMeshPbrItemMaxLights = 8;

// The scoped PBR currency for one lit mesh draw. hasPbr=false on a RenderDrawItem → the item is a plain
// DrawKind::Mesh (unlit) and this payload is unread. A DrawMeshPbr op sets hasPbr=true and fills these from
// the live scope (default material / zero lights / ambient-default fog when no scope op is present).
struct MeshPbrItem {
  SwPbrParameters material;                        // b4 PbrParams (default = TiXL _defaultParameters)
  SwPointLight lights[kMeshPbrItemMaxLights] = {}; // b3 Lights[] (only the first lightCount are live)
  int lightCount = 0;                              // b3 ActiveLightCount (clamped to kMeshPbrItemMaxLights)
  SwFogParameters fog;                             // b2 FogParams (default = SetFog-at-defaults = all 0)
  bool fogActive = false;                          // false → the executor uses swFogAmbientDefault()
};

}  // namespace sw

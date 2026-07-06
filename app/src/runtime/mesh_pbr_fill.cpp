// runtime/mesh_pbr_fill — pack a DrawKind::MeshPbr RenderDrawItem's currency into the MeshPbrParams cbuffer.
// Peeled out of the point_ops_rendertarget.cpp executor case (ARCHITECTURE.md rule 4 — that file is at its
// line-count ratchet). The executor computes the three matrices (o2c / o2cam / c2w) and hands them in; this
// helper copies the material/fog/lights + scalar tail (the verbose per-field copy). Pure CPU, runtime leaf.
#include "runtime/mesh_pbr_fill.h"

#include <Metal/Metal.hpp>

#include "runtime/field_camera.h"     // Mat4 / mat4Identity / mat4Mul / mat4Inverse / objectToClipSpace / LayerCameraForward
#include "runtime/mesh_pbr_params.h"  // MeshPbrParams + MESH_PBR_MAX_LIGHTS + MESHPBR_* bindings
#include "runtime/render_command.h"   // RenderDrawItem / MeshPbrItem
#include "runtime/sw_fog.h"           // swFogAmbientDefault

namespace sw {

void fillMeshPbrParams(const RenderDrawItem& it, const float o2c[16], const float o2cam[16],
                       const float c2w[16], MeshPbrParams& M) {
  for (int i = 0; i < 16; ++i) {
    M.objectToClipSpace[i] = o2c[i];
    M.objectToCamera[i] = o2cam[i];
    M.cameraToWorld[i] = c2w[i];
  }
  M.color[0] = it.color[0]; M.color[1] = it.color[1]; M.color[2] = it.color[2]; M.color[3] = it.color[3];
  const SwPbrParameters& mat = it.pbr.material;
  M.baseColor[0] = mat.baseColor.x; M.baseColor[1] = mat.baseColor.y;
  M.baseColor[2] = mat.baseColor.z; M.baseColor[3] = mat.baseColor.w;
  M.emissiveColor[0] = mat.emissiveColor.x; M.emissiveColor[1] = mat.emissiveColor.y;
  M.emissiveColor[2] = mat.emissiveColor.z; M.emissiveColor[3] = mat.emissiveColor.w;
  M.roughness = mat.roughness; M.specular = mat.specular; M.metal = mat.metal;
  // Fog: the scoped SetFog, or the FogSettings ambient default (10000/2) when none in scope.
  SwFogParameters fog = it.pbr.fogActive ? it.pbr.fog : swFogAmbientDefault();
  M.fogColor[0] = fog.color.x; M.fogColor[1] = fog.color.y; M.fogColor[2] = fog.color.z; M.fogColor[3] = fog.color.w;
  M.fogDistance = fog.distance; M.fogBias = fog.bias;
  int lc = it.pbr.lightCount;
  if (lc > MESH_PBR_MAX_LIGHTS) lc = MESH_PBR_MAX_LIGHTS;
  if (lc < 0) lc = 0;
  M.activeLightCount = (uint32_t)lc;
  for (int i = 0; i < lc; ++i) {
    const SwPointLight& L = it.pbr.lights[i];
    M.lights[i].position[0] = L.position.x; M.lights[i].position[1] = L.position.y;
    M.lights[i].position[2] = L.position.z; M.lights[i].intensity = L.intensity;
    M.lights[i].color[0] = L.color.x; M.lights[i].color[1] = L.color.y;
    M.lights[i].color[2] = L.color.z; M.lights[i].color[3] = L.color.w;
    M.lights[i].range = L.range; M.lights[i].decay = L.decay;
  }
  M.applyTransform = it.applyTransform ? 1u : 0u;  // drop-mul golden tooth (mis-project bite)
}

// Encode ONE DrawKind::MeshPbr item into `enc` (the whole draw, peeled from the executor case). The caller
// has already gated (buffers non-null) + built/handed the PSO. Composes the three matrices from `cam` +
// item Group SRT, fills the cbuffer, binds the mesh buffers, sets dsDraw (dsMesh or dsDisabled for the depth
// tooth) + CCW-front + Cull Back, draws faces×3, then RESTORES dsRestore/CW/CullNone so a 2D item after a
// mesh is byte-identical (mirror of the DrawKind::Mesh case).
void encodeMeshPbrDraw(MTL::RenderCommandEncoder* enc, const RenderDrawItem& it,
                       MTL::RenderPipelineState* pso, const LayerCameraForward& cam,
                       MTL::DepthStencilState* dsDraw, MTL::DepthStencilState* dsRestore) {
  enc->setRenderPipelineState(pso);
  Mat4 meshO2W = mat4Identity();  // a mesh's own O2W = identity (SRT stack = deferred parent Transform op)
  if (it.hasGroup) { for (int i = 0; i < 16; ++i) meshO2W.m[i] = it.groupObjectToWorld[i]; }
  Mat4 o2c = objectToClipSpace(meshO2W, cam.worldToCamera, cam.cameraToClipSpace);
  Mat4 o2cam = mat4Mul(meshO2W, cam.worldToCamera);  // fog: posInCamera = pos·ObjectToCamera (mesh-Draw.hlsl:120)
  Mat4 cam2world{};                                  // eye: (0,0,0,1)·CameraToWorld (mesh-Draw.hlsl:227)
  if (!mat4Inverse(cam.worldToCamera, cam2world)) cam2world = mat4Identity();
  MeshPbrParams M{};
  fillMeshPbrParams(it, o2c.m, o2cam.m, cam2world.m, M);
  enc->setVertexBuffer(const_cast<MTL::Buffer*>(it.meshVtx), 0, MESHPBR_PbrVertices);
  enc->setVertexBuffer(const_cast<MTL::Buffer*>(it.meshIdx), 0, MESHPBR_FaceIndices);
  enc->setVertexBytes(&M, sizeof(M), MESHPBR_Params);
  enc->setFragmentBytes(&M, sizeof(M), MESHPBR_Params);
  enc->setDepthStencilState(dsDraw);
  enc->setFrontFacingWinding(MTL::WindingCounterClockwise);  // TiXL FrontCounterClockwise=true
  enc->setCullMode(MTL::CullModeBack);                       // TiXL Cull Back
  enc->drawPrimitives(MTL::PrimitiveTypeTriangle, NS::UInteger(0),
                      NS::UInteger((NS::UInteger)it.meshIndexCount * 3));
  enc->setDepthStencilState(dsRestore);
  enc->setFrontFacingWinding(MTL::WindingClockwise);
  enc->setCullMode(MTL::CullModeNone);
}

}  // namespace sw

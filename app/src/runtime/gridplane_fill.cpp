// runtime/gridplane_fill — see gridplane_fill.h for the peel rationale (ratchet split of
// point_ops_rendertarget.cpp, mirror of mesh_pbr_fill.cpp / layer2d_fill.cpp).
#include "runtime/gridplane_fill.h"

#include <Metal/Metal.hpp>

#include "runtime/draw_params.h"    // DrawGridPlaneParams + DRAWGRIDPLANE_Params binding
#include "runtime/field_camera.h"   // Mat4/mat4Identity/mat4Inverse/objectToClipSpace/LayerCameraForward
#include "runtime/render_command.h" // RenderDrawItem

namespace sw {

void encodeGridPlaneDraw(MTL::RenderCommandEncoder* enc, const RenderDrawItem& it, MTL::RenderPipelineState* pso,
                         const LayerCameraForward& cam) {
  enc->setRenderPipelineState(pso);
  // ObjectToWorld: the S·R stamp point_ops_gridplane.cpp bakes onto groupObjectToWorld (Scale(100,100,1)
  // · Rotation, GridPlane.t3's internal Transform child — cookTransform's exact math with pivot=0/
  // translation=0). Identity when unstamped (never happens in production; the mesh/Layer2d precedent).
  Mat4 o2w = mat4Identity();
  if (it.hasGroup) { for (int i = 0; i < 16; ++i) o2w.m[i] = it.groupObjectToWorld[i]; }
  // HLSL VS's host-hoisted `scale` (draw-GridPlane.hlsl:57): mul(float4(1,1,1,1),WorldToObject).xy / Size
  // — WorldToObject = inverse(ObjectToWorld); "mul ones-row" = the sum of each column across all 4 rows
  // (row-vector v·M with v=(1,1,1,1)). No vertex_id dependency (every vertex computes the SAME constant),
  // so this executor computes it ONCE instead of shipping WorldToObject + inverting 6× on the GPU.
  Mat4 w2o{};
  if (!mat4Inverse(o2w, w2o)) w2o = mat4Identity();  // singular fallback (never happens for a valid S·R)
  float sumCol0 = w2o.m[0] + w2o.m[4] + w2o.m[8] + w2o.m[12];
  float sumCol1 = w2o.m[1] + w2o.m[5] + w2o.m[9] + w2o.m[13];
  float sizeSafe = (it.size != 0.0f) ? it.size : 1.0f;  // Size==0 degenerate guard (never a valid .t3 value)
  DrawGridPlaneParams P{};
  P.color[0] = it.color[0]; P.color[1] = it.color[1];
  P.color[2] = it.color[2]; P.color[3] = it.color[3];
  P.size = it.size;
  P.scale = it.gridScale;
  P.scaleUV[0] = sumCol0 / sizeSafe;
  P.scaleUV[1] = sumCol1 / sizeSafe;
  Mat4 o2c = objectToClipSpace(o2w, cam.worldToCamera, cam.cameraToClipSpace);
  for (int i = 0; i < 16; ++i) P.objectToClipSpace[i] = o2c.m[i];
  enc->setVertexBytes(&P, sizeof(P), DRAWGRIDPLANE_Params);
  enc->setFragmentBytes(&P, sizeof(P), DRAWGRIDPLANE_Params);
  enc->drawPrimitives(MTL::PrimitiveTypeTriangle, NS::UInteger(0), NS::UInteger(6));
}

}  // namespace sw

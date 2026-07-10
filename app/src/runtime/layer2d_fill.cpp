// runtime/layer2d_fill — see layer2d_fill.h for the peel rationale (ratchet split of point_ops_rendertarget.cpp,
// mirror of mesh_pbr_fill.cpp).
#include "runtime/layer2d_fill.h"

#include <Metal/Metal.hpp>

#include "runtime/draw_params.h"    // DrawQuadXfParams + DRAWQUADXF_* bindings
#include "runtime/field_camera.h"   // Mat4/mat4Mul/objectToClipSpace/viewAspectFromClip/layer2dObjectToWorld/
                                     // layer2dScaleModeApply/Layer2dScaleMode/LayerCameraForward
#include "runtime/render_command.h" // RenderDrawItem

namespace sw {

void encodeLayer2dDraw(MTL::RenderCommandEncoder* enc, const RenderDrawItem& it, MTL::RenderPipelineState* pso,
                       MTL::SamplerState* sampler, const LayerCameraForward& cam) {
  enc->setRenderPipelineState(pso);
  DrawQuadXfParams P{};
  P.color[0] = it.color[0]; P.color[1] = it.color[1];
  P.color[2] = it.color[2]; P.color[3] = it.color[3];
  P.position[0] = it.position[0]; P.position[1] = it.position[1];
  P.width = it.width; P.height = it.height;
  P.clampMax[0] = it.clampMax[0]; P.clampMax[1] = it.clampMax[1];
  P.clampMax[2] = it.clampMax[2]; P.clampMax[3] = it.clampMax[3];
  // F1: the EXECUTOR finishes ObjectToClipSpace (TransformBufferLayout.cs:13-16 order: o2w·worldToCamera·
  // cameraToClipSpace). Cut 2: ObjectToWorld = the SRT stack (TiXL _ProcessLayer2d) composed HERE — ScaleMode
  // couples viewAspect (camera) + imageAspect (srcTexture). Cut 3 + camera-B: `cam` = itemCamera's resolved
  // stamped push + ShiftCamera nudge; both the SRT viewAspect AND the projection use it (= context's
  // CameraToClipSpace in TiXL).
  Mat4 objectToWorld{};
  if (it.layer2dComposeSRT) {
    // viewAspect = CameraToClipSpace.M22/M11 (_ProcessLayer2d.cs:37). imageAspect = srcW/srcH.
    float viewAspect = viewAspectFromClip(cam.cameraToClipSpace);
    float imgW = (float)it.srcTexture->width(), imgH = (float)it.srcTexture->height();
    float imageAspect = (imgH > 0.0f) ? imgW / imgH : 1.0f;
    // scale = Scale * Stretch (cs:40), then ScaleMode adjusts scale.X/Y (cs:49-101).
    float scaleX = it.layerScale * it.layerStretch[0];
    float scaleY = it.layerScale * it.layerStretch[1];
    layer2dScaleModeApply((Layer2dScaleMode)it.layerScaleMode, imageAspect, viewAspect, scaleX, scaleY);
    objectToWorld = layer2dObjectToWorld(scaleX, scaleY, it.layerRotateDeg, it.position[0], it.position[1],
                                         it.layerPosZ);
  } else {
    // Legacy path: the item carries ObjectToWorld verbatim (Cut-1 seam-tooth driving a hand-built matrix).
    // Kept so the seam-presence golden can drive an arbitrary matrix.
    for (int i = 0; i < 16; ++i) objectToWorld.m[i] = it.objectToClipSpace[i];
  }
  // S2b GROUP SRT: a parent Group op stamped its accumulated transform onto this item (TiXL Group.cs
  // context.ObjectToWorld = Multiply(groupSRT, prev)). Right-multiply it (row-vector v·M → the group is the
  // PARENT applied AFTER the child's own O2W = child·group). Identity when no Group → byte-identical.
  if (it.hasGroup) {
    Mat4 grp{}; for (int i = 0; i < 16; ++i) grp.m[i] = it.groupObjectToWorld[i];
    objectToWorld = mat4Mul(objectToWorld, grp);
  }
  Mat4 o2c = objectToClipSpace(objectToWorld, cam.worldToCamera, cam.cameraToClipSpace);
  for (int i = 0; i < 16; ++i) P.objectToClipSpace[i] = o2c.m[i];
  P.applyTransform = it.applyTransform ? 1u : 0u;  // drop-mul golden tooth
  enc->setVertexBytes(&P, sizeof(P), DRAWQUADXF_Params);
  enc->setFragmentBytes(&P, sizeof(P), DRAWQUADXF_Params);
  enc->setFragmentTexture(const_cast<MTL::Texture*>(it.srcTexture), 0);
  enc->setFragmentSamplerState(sampler, 0);
  enc->drawPrimitives(MTL::PrimitiveTypeTriangle, NS::UInteger(0), NS::UInteger(6));
}

}  // namespace sw

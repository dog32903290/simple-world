// runtime/mesh_pbr_fill — pack a DrawKind::MeshPbr item into MeshPbrParams (peeled from the executor case
// for the point_ops_rendertarget.cpp line-count ratchet). The executor builds the three ROW-MAJOR matrices
// (objectToClipSpace / objectToCamera / cameraToWorld) and calls this to copy the material/fog/lights.
#pragma once

#include "runtime/mesh_pbr_params.h"  // MeshPbrParams (global-scope struct — MSL-shareable, no namespace)

namespace MTL {
class RenderCommandEncoder;
class RenderPipelineState;
class DepthStencilState;
}  // namespace MTL

namespace sw {

struct RenderDrawItem;       // render_command.h
struct LayerCameraForward;   // field_camera.h

// Fill `M` from `it.pbr` (material/lights/fog) + the three precomputed row-major matrices. `it.applyTransform`
// → M.applyTransform (the drop-mul golden tooth). No-op-safe on a non-MeshPbr item (the caller gates on kind).
void fillMeshPbrParams(const RenderDrawItem& it, const float o2c[16], const float o2cam[16],
                       const float c2w[16], MeshPbrParams& M);

// Encode one DrawKind::MeshPbr item (whole draw: PSO + matrices + cbuffer + dsDraw/CCW/CullBack + restore).
// Peeled from the executor case for the ratchet. Caller gates buffers non-null + builds the PSO. dsDraw =
// dsMesh (or dsDisabled for the depth-tooth); dsRestore = dsDisabled (byte-identical 2D item after a mesh).
void encodeMeshPbrDraw(MTL::RenderCommandEncoder* enc, const RenderDrawItem& it,
                       MTL::RenderPipelineState* pso, const LayerCameraForward& cam,
                       MTL::DepthStencilState* dsDraw, MTL::DepthStencilState* dsRestore);

}  // namespace sw

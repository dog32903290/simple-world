// runtime/resident_camera_value_cook — cookCameraValueOutputNodes: the PRODUCTION (resident-path)
// cook-emit pass for the CAMERA value-output family (CamPosition / CurrentCamMatrices): ops that read
// the AMBIENT CAMERA off the cook context (TiXL context.WorldToCamera / context.CameraToClipSpace) and
// emit values — the camera twin of cookValueOutputNodes (RequestedResolution, value-output-rail Phase 1).
//
// TiXL authority: Operators/Lib/render/camera/CamPosition.cs + CurrentCamMatrices.cs. Both ops carry
// evaluate==nullptr (the value comes from the camera CONTEXT, which a pure evaluate() cannot see) and
// ride the extOut / extColorOut readback channels, cooked once per frame from cook_host_values.cpp —
// the exact contract of cookValueOutputNodes / cookMatrixOutputNodes.
//
// AMBIENT-CAMERA RESOLUTION (fork, named) — fork-camera-value-structural-enclosing-walk:
//   TiXL resolves the ambient camera at EVAL time (the Camera op pushes context.WorldToCamera around its
//   subtree; CamPosition's Update reads it mid-evaluation). This frame-level pass resolves it
//   STRUCTURALLY instead: walk DOWNSTREAM from the op along Command-typed wires; the first camera-family
//   node reached through its Command input = the innermost enclosing camera (its push is the ambient
//   context TiXL would have installed). No enclosing camera → the DEFAULT camera
//   (EvaluationContext.SetDefaultCamera) at the frame's requested-resolution aspect. Faithful for static
//   graphs (the graph IS the scope structure); forked only in WHEN the resolution happens (build-shape
//   vs eval-order). Same frame-level bargain as RequestedResolution Phase 1 (a mid-cook
//   SetRequestedResolution scope is invisible there too).
//
// runtime leaf: pure CPU (field_camera math + resident graph walk). No Metal, no upward deps.
#pragma once

namespace sw {

struct ResidentEvalGraph;
struct ResidentEvalCtx;

// Per-frame PRODUCTION cook for the camera value-output ops. Walks the resident graph; for each
// CamPosition resolves the enclosing camera's forward matrices and writes Position/Direction/AspectRatio
// onto extOut[1..7] (CamPosition.cs:29-38); for each CurrentCamMatrices writes the 4 transposed
// WorldToClipSpace rows onto extColorOut (CurrentCamMatrices.cs:28-42, the
// fork-matrix-as-4-vec4-on-extColorOut channel). Mutates g. Pure CPU, no Metal.
void cookCameraValueOutputNodes(ResidentEvalGraph& g, const ResidentEvalCtx& ctx);

// Register the camera value ops' Command-rail cooks (CamPosition / CurrentCamMatrices — a NO-OP
// RenderCommand: TiXL's Command output draws nothing; the values ride the frame-level pass above).
// Called from registerPointOps' draw registrar (point_ops_register_draw.cpp).
void registerCameraValueOps();

// -bug seam (the goldens' true-cook corrosion): when true the enclosing-camera walk is SEVERED — every
// camera value op reads the DEFAULT camera even under a wired Camera (the pre-op behaviour). The emitted
// Position/Direction/AspectRatio flip away from the enclosing camera's values → RED. OFF in production.
bool& cameraValueBugSkipEnclosing();

}  // namespace sw

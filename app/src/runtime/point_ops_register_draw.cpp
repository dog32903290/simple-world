// runtime/point_ops_register_draw — per-family registrar for DRAW ops (Points → Command, and
// the Command → Texture2D RenderTarget terminal). Split from point_ops.cpp's central
// registerBuiltinPointOps (node_registry.cpp pattern, ARCHITECTURE rule 7). Adding a draw op
// edits ONLY this file. Central builder unchanged.
//
// Zero behaviour change: op names + cook bindings verbatim from the original central function
// (cookDrawPoints is inline in point_ops.cpp, declared in point_ops.h; the rest are leaf fns,
// registerRenderTargetOp/Draw* declared in point_ops.h).
#include "runtime/point_graph.h"  // registerCmdOp
#include "runtime/point_ops.h"    // cookDrawPoints, registerDrawLinesOp/Billboards/RenderTargetOp
#include "runtime/point_ops_orthographiccamera.h"  // C2: registerOrthographicCameraOp (Command→Command ortho push)
#include "runtime/point_ops_setvarcmd.h"  // S3a: registerSetVarCmdOps (Command-rail SetFloatVarCmd/SetIntVarCmd)
#include "runtime/point_ops_forwardbeattaps.h"  // VJ: registerForwardBeatTapsOp (TapProvider publish + SubTree forward)
#include "runtime/point_ops_settime.h"  // SetTime: registerSetTimeOp (subtree-time scope around SubTree)
#include "runtime/point_ops_spread.h"     // render lane: registerSpreadOps (SpreadIntoGrid/SpreadLayout per-wire SRT)
#include "runtime/point_ops_getscreenpos.h"  // render lane: registerGetScreenPosOp (world→screen projection + value latch)
#include "runtime/point_ops_getposition.h"  // flow.context: registerGetPositionOp (transform-scope read + value latch)
#include "runtime/point_ops_gpumeasure.h"  // render.analyze: registerGpuMeasureOp (GPU-time measure + value latch)
#include "runtime/point_ops_sliceviewport.h"  // render.transform: registerSliceViewPortOp (cell viewport + clip stamp)
#include "runtime/point_ops_shiftcamera.h"  // camera-B: registerShiftCameraOp (Command→Command projection nudge)
#include "runtime/point_ops_visiblegizmos.h"  // camera-B: registerVisibleGizmosOp (MultiInput Command visibility gate)
#include "runtime/point_ops_reusecamera.h"  // camera-B: registerReuseCameraOp (referenced-camera push)
#include "runtime/point_ops_orbitcamera.h"  // camera-B: registerOrbitCameraOp (animated orbit/wobble camera)

namespace sw {

// Render-island transform leaves (point_ops_{rotatearoundaxis,shear,transform}.cpp). Declared here (their
// only caller) rather than in the at-cap point_ops.h — keeps the god-header off the linecount ratchet.
void registerRotateAroundAxisOp();
void registerShearOp();
void registerTransformOp();
void registerRotateTowardsOp();  // Command → Command (LookAt-style facing rotation push; point_ops_rotatetowards.cpp)
void registerSwitchOp();  // S3b: Command(MultiInput) → Command (cook-core sub-select; point_ops_switch.cpp)
void registerPickObjectOp();  // data.object: Command(MultiInput) → Command (Mod pick; point_ops_pickobject.cpp)
void registerCpuPointToCameraOp();  // point.helper: Points+Command → Command (point[0] camera stamp; point_ops_cpupointtocamera.cpp)
void registerLoopOp();    // S3c: Command(SubGraph) → Command (cook-core RE-COOK per iteration; point_ops_loop.cpp)
void registerExecuteOnceOp();     // S3b: Command(MultiInput) → Command (gated concat-all; point_ops_executeonce.cpp)
void registerLoadSoundtrackOp();  // flow lane: Command(MultiInput) → Command (Execute twin, IsEnabled gate; audio rides app/soundtrack — point_ops_loadsoundtrack.cpp)
void registerResetSubtreeTriggerOp();  // flow lane: Command(SubGraph) → Command (transparent passthrough; invalidation = named no-op fork — point_ops_resetsubtreetrigger.cpp)
void registerLogMessageOp();      // S3b: Command(SubGraph) → Command (passthrough + log sink; point_ops_logmessage.cpp)
void registerExecRepeatedlyOp();  // S3c: Command(MultiInput) → Command (cook-core RE-COOK ×RepeatCount; point_ops_execrepeatedly.cpp)
void registerRasterizerOp();      // Seam 2: Command → Command (render-state STAMP: cull/fill/winding/depthBias; point_ops_renderstate.cpp)
void registerOutputMergerOp();    // Seam 2: Command → Command (render-state STAMP: blend + depth STAGE; point_ops_renderstate.cpp)
void registerInputAssemblerOp();  // Seam 2: Command → Command (render-state STAMP: PrimitiveTopology; point_ops_inputassembler.cpp)
void registerDrawExplicitOp();    // Seam 2: Command SOURCE (DrawKind::Explicit raw N-vertex draw; point_ops_draw_explicit.cpp)
void registerCameraValueOps();    // camera-A: CamPosition Command no-op hook (values ride the frame-level
                                  // cookCameraValueOutputNodes pass; resident_camera_value_cook.cpp)
void registerCameraWithRotationOp();  // camera-A: rotation-driven camera push (point_ops_camerawithrotation.cpp)
void registerBlendCamerasOp();        // camera-A: slerp-blend of referenced cameras (point_ops_blendcameras.cpp)

void registerDrawPointOps() {
  registerCmdOp("DrawPoints", cookDrawPoints);  // Points → Command (was a draw op)
  registerDrawLinesOp();                        // Points → Command (DrawKind::Lines, lane L)
  registerDrawClosedLinesOp();                  // Points → Command (DrawKind::Lines + closed, draw seam)
  registerDrawPoints2Op();                      // Points → Command (DrawKind::Points2, Radius variant, draw 第二批)
  registerDrawLinesBuildupOp();                 // Points → Command (DrawKind::LinesBuildup, W-reveal, draw 第二批)
  registerDrawBillboardsOp();                   // Points → Command (DrawKind::Billboards, lane L)
  registerDrawScreenQuadOps();                  // Texture2D → Command (DrawKind::ScreenQuad) + ClearRenderTarget
  registerLayer2dOp();                          // Texture2D → Command (DrawKind::Layer2d, camera-context seam)
  registerCameraOp();                           // Command → Command (explicit camera push/pop, Cut 3)
  registerOrthographicCameraOp();                // Command → Command (ORTHOGRAPHIC projection push, camera3d C2)
  registerCameraValueOps();                      // Command SOURCE no-op (CamPosition — values on the frame-level pass)
  registerCameraWithRotationOp();                // Command → Command (rotation-driven camera push, camera-A)
  registerBlendCamerasOp();                      // Command → Command (slerp-blend of camera refs, camera-A)
  registerShiftCameraOp();                       // Command → Command (additive CameraToClipSpace nudge, camera-B)
  registerVisibleGizmosOp();                     // Command(MultiInput) → Command (gizmo visibility gate, camera-B)
  registerReuseCameraOp();                       // Command → Command (referenced-camera push via Object wire, camera-B)
  registerOrbitCameraOp();                       // Command → Command (animated orbit/wobble camera push, camera-B)
  registerExecuteOp();                           // Command(MultiInput) → Command (S2a KEYSTONE: N-chain concat)
  registerGroupOp();                             // Command(MultiInput) → Command (S2b: Execute + SRT transform-context push)
  registerSpreadOps();                           // Command(MultiInput) → Command (render lane: per-WIRE SRT — SpreadIntoGrid/SpreadLayout)
  registerGetScreenPosOp();                      // Command SOURCE (render lane: world→screen projection into the value latch)
  registerGetPositionOp();                       // Command SOURCE (flow.context: transform-scope read into the value latch)
  registerGpuMeasureOp();                        // Command PASSTHROUGH (render.analyze: GPU-time measure into the value latch)
  registerSliceViewPortOp();                     // Command PASSTHROUGH (render.transform: cell viewport rect + clip scale stamp)
  registerRotateAroundAxisOp();                  // Command → Command (axis-angle transform-context push, S2 island)
  registerShearOp();                             // Command → Command (shear transform-context push, S2 island)
  registerTransformOp();                         // Command → Command (full TRS+pivot transform-context push, S2 island)
  registerRotateTowardsOp();                     // Command → Command (LookAt-style facing rotation push, render/flow WAVE-1)
  registerSetRequestedResolutionOp();           // Command → Command (explicit RequestedResolution push/pop, S1 + Cmd sibling)
  registerSetVarCmdOps();                        // Command → Command (S3a context-var SubGraph scope: SetFloatVarCmd/SetIntVarCmd)
  registerForwardBeatTapsOp();                   // Command → Command (VJ: publish beat/resync/slide into TapProvider, forward SubTree)
  registerSetTimeOp();                           // Command → Command (subtree-time scope: LocalFxTime[/LocalTime] push around SubTree)
  registerSwitchOp();                            // Command(MultiInput) → Command (S3b: cook-core sub-select by Index)
  registerPickObjectOp();                        // Command(MultiInput) → Command (data.object: Mod pick by Index)
  registerCpuPointToCameraOp();                  // Points+Command → Command (point.helper: point[0] camera stamp)
  registerLoopOp();                              // Command(SubGraph) → Command (S3c: cook-core RE-COOK per iteration)
  registerExecuteOnceOp();                       // Command(MultiInput) → Command (S3b: gated concat-all by Trigger)
  registerLoadSoundtrackOp();                    // Command(MultiInput) → Command (flow: Execute twin, IsEnabled gate)
  registerResetSubtreeTriggerOp();               // Command(SubGraph) → Command (flow: transparent passthrough)
  registerLogMessageOp();                        // Command(SubGraph) → Command (S3b: transparent passthrough + log sink)
  registerExecRepeatedlyOp();                    // Command(MultiInput) → Command (S3c: cook-core RE-COOK ×RepeatCount)
  registerRasterizerOp();                        // Command → Command (Seam 2: render-state STAMP — Rasterizer spike)
  registerOutputMergerOp();                      // Command → Command (Seam 2: render-state STAMP — OutputMerger blend/depth stage)
  registerInputAssemblerOp();                    // Command → Command (Seam 2: render-state STAMP — InputAssembler topology)
  registerDrawExplicitOp();                      // Command SOURCE (Seam 2: DrawKind::Explicit raw N-vertex draw leaf)
  registerDrawMeshUnlitOp();                    // Mesh → Command (DrawKind::Mesh, the FIRST 3D mesh, Cut 99)
  registerRenderTargetOp();                     // Command → Texture2D (the resolution pin)
}

}  // namespace sw

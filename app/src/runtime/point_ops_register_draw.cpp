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

namespace sw {

// Render-island transform leaves (point_ops_{rotatearoundaxis,shear,transform}.cpp). Declared here (their
// only caller) rather than in the at-cap point_ops.h — keeps the god-header off the linecount ratchet.
void registerRotateAroundAxisOp();
void registerShearOp();
void registerTransformOp();
void registerRotateTowardsOp();  // Command → Command (LookAt-style facing rotation push; point_ops_rotatetowards.cpp)
void registerSwitchOp();  // S3b: Command(MultiInput) → Command (cook-core sub-select; point_ops_switch.cpp)
void registerLoopOp();    // S3c: Command(SubGraph) → Command (cook-core RE-COOK per iteration; point_ops_loop.cpp)
void registerExecuteOnceOp();     // S3b: Command(MultiInput) → Command (gated concat-all; point_ops_executeonce.cpp)
void registerLogMessageOp();      // S3b: Command(SubGraph) → Command (passthrough + log sink; point_ops_logmessage.cpp)
void registerExecRepeatedlyOp();  // S3c: Command(MultiInput) → Command (cook-core RE-COOK ×RepeatCount; point_ops_execrepeatedly.cpp)
void registerRasterizerOp();      // Seam 2: Command → Command (render-state STAMP: cull/fill/winding/depthBias; point_ops_renderstate.cpp)

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
  registerExecuteOp();                           // Command(MultiInput) → Command (S2a KEYSTONE: N-chain concat)
  registerGroupOp();                             // Command(MultiInput) → Command (S2b: Execute + SRT transform-context push)
  registerRotateAroundAxisOp();                  // Command → Command (axis-angle transform-context push, S2 island)
  registerShearOp();                             // Command → Command (shear transform-context push, S2 island)
  registerTransformOp();                         // Command → Command (full TRS+pivot transform-context push, S2 island)
  registerRotateTowardsOp();                     // Command → Command (LookAt-style facing rotation push, render/flow WAVE-1)
  registerSetRequestedResolutionOp();           // Command → Command (explicit RequestedResolution push/pop, S1)
  registerSetVarCmdOps();                        // Command → Command (S3a context-var SubGraph scope: SetFloatVarCmd/SetIntVarCmd)
  registerSwitchOp();                            // Command(MultiInput) → Command (S3b: cook-core sub-select by Index)
  registerLoopOp();                              // Command(SubGraph) → Command (S3c: cook-core RE-COOK per iteration)
  registerExecuteOnceOp();                       // Command(MultiInput) → Command (S3b: gated concat-all by Trigger)
  registerLogMessageOp();                        // Command(SubGraph) → Command (S3b: transparent passthrough + log sink)
  registerExecRepeatedlyOp();                    // Command(MultiInput) → Command (S3c: cook-core RE-COOK ×RepeatCount)
  registerRasterizerOp();                        // Command → Command (Seam 2: render-state STAMP — Rasterizer spike)
  registerDrawMeshUnlitOp();                    // Mesh → Command (DrawKind::Mesh, the FIRST 3D mesh, Cut 99)
  registerRenderTargetOp();                     // Command → Texture2D (the resolution pin)
}

}  // namespace sw

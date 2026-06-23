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
#include "runtime/point_ops_setvarcmd.h"  // S3a: registerSetVarCmdOps (Command-rail SetFloatVarCmd/SetIntVarCmd)

namespace sw {

// Render-island transform leaves (point_ops_{rotatearoundaxis,shear,transform}.cpp). Declared here (their
// only caller) rather than in the at-cap point_ops.h — keeps the god-header off the linecount ratchet.
void registerRotateAroundAxisOp();
void registerShearOp();
void registerTransformOp();
void registerSwitchOp();  // S3b: Command(MultiInput) → Command (cook-core sub-select; point_ops_switch.cpp)

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
  registerExecuteOp();                           // Command(MultiInput) → Command (S2a KEYSTONE: N-chain concat)
  registerGroupOp();                             // Command(MultiInput) → Command (S2b: Execute + SRT transform-context push)
  registerRotateAroundAxisOp();                  // Command → Command (axis-angle transform-context push, S2 island)
  registerShearOp();                             // Command → Command (shear transform-context push, S2 island)
  registerTransformOp();                         // Command → Command (full TRS+pivot transform-context push, S2 island)
  registerSetRequestedResolutionOp();           // Command → Command (explicit RequestedResolution push/pop, S1)
  registerSetVarCmdOps();                        // Command → Command (S3a context-var SubGraph scope: SetFloatVarCmd/SetIntVarCmd)
  registerSwitchOp();                            // Command(MultiInput) → Command (S3b: cook-core sub-select by Index)
  registerDrawMeshUnlitOp();                    // Mesh → Command (DrawKind::Mesh, the FIRST 3D mesh, Cut 99)
  registerRenderTargetOp();                     // Command → Texture2D (the resolution pin)
}

}  // namespace sw

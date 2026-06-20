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

namespace sw {

void registerDrawPointOps() {
  registerCmdOp("DrawPoints", cookDrawPoints);  // Points → Command (was a draw op)
  registerDrawLinesOp();                        // Points → Command (DrawKind::Lines, lane L)
  registerDrawClosedLinesOp();                  // Points → Command (DrawKind::Lines + closed, draw seam)
  registerDrawBillboardsOp();                   // Points → Command (DrawKind::Billboards, lane L)
  registerDrawScreenQuadOps();                  // Texture2D → Command (DrawKind::ScreenQuad) + ClearRenderTarget
  registerLayer2dOp();                          // Texture2D → Command (DrawKind::Layer2d, camera-context seam)
  registerCameraOp();                           // Command → Command (explicit camera push/pop, Cut 3)
  registerDrawMeshUnlitOp();                    // Mesh → Command (DrawKind::Mesh, the FIRST 3D mesh, Cut 99)
  registerRenderTargetOp();                     // Command → Texture2D (the resolution pin)
}

}  // namespace sw
